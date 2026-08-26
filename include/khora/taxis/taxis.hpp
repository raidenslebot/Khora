#pragma once

// TAXIS — what part of speech a word is, learned from where it stands.
//
// (τάξις, arrangement: the root under "syntax".)
//
// An audit of this tree found no syntactic analysis of any kind: no tagger, no
// chunker, no parser, no notion anywhere that a token has a grammatical
// category. Every rule in the relation extractor is a match over raw surface
// tokens, and that is measurably what is wrong with it. Scored against an
// external IS-A bar, the copula rule "X is a Y" gets 1.89% [1.34, 2.65] while
// the Hearst frames get 6.84% [3.51, 12.91], and the copula produces 56% of
// everything the system knows. The reason is visible in the output:
//
//     man is-a social          <- "man is a social being"
//     time is-a greatest       <- "the greatest time"
//     body is-a held           <- a participle read as a noun
//
// None of those is a parsing accident. The extractor takes the last content word
// of the noun phrase as the head, which is right, and then has no way to tell
// whether the word it landed on is a noun at all.
//
// WHAT THIS IS. Counts of the word that came before, and a category read off
// them. No tagged corpus, because there is no tagged corpus here and buying one
// would be a dependency; the signal is entirely distributional and comes from
// the same books everything else reads.
//
// THE ONE IDEA THAT MAKES IT WORK. A determiner is the obvious cue for a noun --
// "the X" -- and it is not enough on its own, because a determiner precedes
// adjectives just as happily: "a social being" puts "social" straight after "a".
// Determiner context is a NOUN-PHRASE signal, not a noun signal, and using it
// alone is how "man is-a social" happens.
//
// Degree adverbs are the cue that separates them. "very", "too", "so", "quite",
// "rather" precede adjectives and adverbs and essentially never precede a bare
// noun -- "very social" is ordinary English and "very being" is not. So three
// counts are kept:
//
//     NOUN       preceded by a determiner or possessive
//     ADJECTIVE  preceded by a degree adverb
//     VERB       preceded by "to" or by a modal
//
// AND THEY ARE NOT WEIGHED AGAINST EACH OTHER, which is the second thing that
// had to be measured before it was right. Taking the largest of the three looks
// obvious and fails, because almost every adjective follows a determiner far
// more often than it follows "very" -- "the best man" is commoner than "very
// best" -- so the argmax calls it a noun. Measured that way the gate removed
// 8.0% of the objects a separate blocklist says cannot be objects at all,
// against a 7.8% base rate over everything: no discrimination whatever. Reading
// the marked evidence first instead takes that to 94.3%.
//
// WHAT IT IS NOT. Not a tagger in the sequence-labelling sense: a word gets one
// category for the whole corpus, so "state" is a noun everywhere and the verb
// reading is lost. Real taggers are per-occurrence and need either a tagged
// corpus or an HMM with EM over one. This is the type-level approximation, it
// costs three integer counters per word, and the question it has to answer --
// can this token be the head of a noun phrase -- is a type-level question.
//
// It is measured against WordNet, whose 35,767 member words are all nouns, and
// against the IS-A bar it was built to move.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace khora::taxis {

enum class Tag : std::uint8_t { Unknown = 0, Noun, Adjective, Verb };

const char* tag_name(Tag t) noexcept;

class Taxis {
public:
    // Accumulate context counts from one sentence. Sentences rather than
    // documents, because the word before the first token of a sentence is the
    // last token of the previous one and tells you nothing.
    void observe(const std::vector<std::string>& sentence);

    // Merge another Taxis in (additive), for a parallel pass over the corpus.
    void absorb(const Taxis& other);

    // HOW MUCH OF THE NOUN-PHRASE EVIDENCE HAS TO BE THE MARKED KIND.
    //
    // Reading the degree count first and ignoring its size was the second wrong
    // rule here. Over eight million tokens almost any word follows "so" or "too"
    // twice, so an absolute floor of 2 on marked evidence called 692 of 1,807
    // certain nouns adjectives -- a 38% false-positive rate on words WordNet
    // says are nouns. The marked evidence has to be a real FRACTION of the
    // noun-phrase evidence, not merely present.
    //
    // The value is a calibration, not a derivation. extraction_bench prints the
    // whole curve against two reference sets Taxis never sees -- a blocklist of
    // objects that cannot be the object of a relation, and WordNet nouns:
    //
    //     share | known BAD cut | known GOOD cut | ratio
    //      0.00 |         94.3% |          38.4% |  2.46
    //      0.05 |         51.1% |           2.9% | 17.77   <- chosen
    //      0.10 |         44.3% |           1.6% | 27.61
    //      0.15 |         22.7% |           0.7% | 31.59
    //      0.25 |         17.0% |           0.5% | 34.22
    //      0.40 |          4.5% |           0.2% | 27.38
    //      1.01 |          0.0% |           0.2% |  0.00   (rule off entirely)
    //
    // 0.05 removes half the known errors and keeps 97.1% of the certain nouns.
    // Note that maximising the ratio column would have chosen 0.25 and been
    // wrong: the ratio rewards cutting almost nothing, and a filter that removes
    // 17% of the errors is not worth much however clean it is per cut.
    static constexpr double kAdjRatio = 0.10;

    // The same argument for verbs, and the cue that reaches what degree adverbs
    // cannot. "social" has 232 determiners in front of it and 2 degree adverbs on
    // the live corpus, so the adjective rule never fires for it; "held" has 37
    // and 1. Participles follow an auxiliary instead -- "was held", "is done" --
    // and a bare noun after an auxiliary is much rarer than after a determiner.
    static constexpr double kVerbRatio = 0.65;

    Tag tag(const std::string& w) const { return tag(w, kAdjRatio, kVerbRatio); }
    Tag tag(const std::string& w, double adj_ratio,
            double verb_ratio = kVerbRatio) const;
    bool is_noun(const std::string& w) const { return tag(w) == Tag::Noun; }

    // POSITIVE evidence that a word is not a noun, which is a different claim
    // from is_noun() being false and the difference is most of the cost of using
    // this. 35% of the WordNet nouns in these books fall below the evidence
    // floor and come back Unknown; refusing those is refusing on absence of
    // evidence, and it throws away a third of the good ones to catch the bad.
    bool rejects_noun(const std::string& w) const {
        const Tag t = tag(w);
        return t == Tag::Adjective || t == Tag::Verb;
    }

    // The raw evidence, exposed because a category with no counts behind it is
    // not inspectable and this one has to be argued with.
    struct Evidence {
        std::uint32_t det = 0;      // after a/an/the/this/his/...
        std::uint32_t degree = 0;   // after very/more/too/so/...
        std::uint32_t modal = 0;    // after to/can/will/must/...
        std::uint32_t seen = 0;     // total occurrences
    };
    Evidence evidence(const std::string& w) const;

    // A category needs to have been seen more than once before it counts, for
    // the same reason every other floor in this system exists: one sighting is
    // a coincidence. Below it the word is Unknown rather than guessed.
    static constexpr std::uint32_t kFloor = 2;

    std::size_t vocabulary() const noexcept { return ev_.size(); }
    std::size_t tagged() const noexcept;

    void save(const std::filesystem::path& prefix) const;
    void load(const std::filesystem::path& prefix);

private:
    std::unordered_map<std::string, Evidence> ev_;
};

} // namespace khora::taxis
