#include "khora/taxis/taxis.hpp"

#include <algorithm>
#include <fstream>
#include <unordered_set>

namespace khora::taxis {

const char* tag_name(Tag t) noexcept {
    switch (t) {
        case Tag::Noun:      return "noun";
        case Tag::Adjective: return "adj";
        case Tag::Verb:      return "verb";
        default:             return "?";
    }
}

namespace {

// Determiners and possessives. A word after one of these is somewhere inside a
// noun phrase -- which is not the same as being the noun, and that distinction
// is the whole point of the next set.
const std::unordered_set<std::string>& determiners() {
    static const std::unordered_set<std::string> s = {
        "a", "an", "the", "this", "that", "these", "those",
        "his", "her", "its", "their", "our", "my", "your",
        "some", "any", "no", "every", "each", "another", "such"
    };
    return s;
}

// Degree adverbs. These precede adjectives and adverbs and essentially never
// precede a bare noun: "very social" is ordinary and "very being" is not. This
// is the set that separates the adjective from the head it modifies, and
// without it a determiner-only rule calls both of them nouns.
const std::unordered_set<std::string>& degrees() {
    static const std::unordered_set<std::string> s = {
        // NOT "more", "most", "less" or "least", which were here and had to go.
        // They are quantifiers as often as they are degree adverbs -- "more
        // water", "most men", "less time" all put a noun straight after one --
        // so they carry the adjective signal and the noun signal at once, which
        // is exactly the confusion this set exists to resolve.
        "very", "too", "so", "quite", "rather", "extremely", "highly",
        "somewhat", "fairly", "entirely", "wholly", "utterly", "exceedingly",
        "peculiarly", "singularly", "remarkably", "unusually", "perfectly"
    };
    return s;
}

// The infinitive marker and the modals. A word after these is a verb, with the
// one systematic exception of "to" as a preposition ("to the house") -- which is
// why the determiner check runs first and wins the token.
const std::unordered_set<std::string>& modals() {
    static const std::unordered_set<std::string> s = {
        "to", "can", "cannot", "will", "would", "shall", "should",
        "may", "might", "must", "could", "let", "must"
    };
    return s;
}

} // namespace

void Taxis::observe(const std::vector<std::string>& sentence) {
    for (std::size_t i = 0; i < sentence.size(); ++i) {
        Evidence& e = ev_[sentence[i]];
        ++e.seen;
        if (i == 0) continue;                       // nothing before it in THIS sentence
        const std::string& prev = sentence[i - 1];
        // Checked in this order, and each token contributes to one bucket only.
        // "to the house" must not count "house" as a verb, so the determiner
        // immediately before it decides and the "to" two back is irrelevant.
        if (determiners().count(prev))      ++e.det;
        else if (degrees().count(prev))     ++e.degree;
        else if (modals().count(prev))      ++e.modal;
    }
}

void Taxis::absorb(const Taxis& other) {
    for (const auto& [w, o] : other.ev_) {
        Evidence& e = ev_[w];
        e.det += o.det; e.degree += o.degree; e.modal += o.modal; e.seen += o.seen;
    }
}

Taxis::Evidence Taxis::evidence(const std::string& w) const {
    const auto it = ev_.find(w);
    return it == ev_.end() ? Evidence{} : it->second;
}

Tag Taxis::tag(const std::string& w, double adj_ratio) const {
    const Evidence e = evidence(w);

    // ADJECTIVE EVIDENCE IS MARKED, AND IT ALSO HAS TO BE A REAL SHARE. Both
    // halves of that were arrived at by getting it wrong first.
    //
    // Taking the largest of the three counts fails, because almost every
    // adjective follows a determiner far more often than it follows "very" --
    // "the best man" is commoner than "very best" -- so an argmax calls it a
    // noun. Measured that way the gate removed 8.0% of the objects a separate
    // blocklist says cannot be objects at all, against a 7.8% base rate over
    // everything: no discrimination whatever.
    //
    // Reading the marked evidence first instead, with only an absolute floor,
    // fails the other way. Over eight million tokens almost any word follows
    // "so" or "too" twice, so that called 38% of words WordNet certifies as
    // nouns adjectives. Presence is not enough; the marked evidence has to be a
    // fraction of the noun-phrase evidence.
    //
    // A determiner in front proves only that the word is somewhere inside a noun
    // phrase, which adjectives and nouns both are. A degree adverb in front is
    // something a bare noun mostly does not do. The ratio is how much of the
    // first kind of evidence the second kind has to displace.
    const double np = static_cast<double>(e.det) + static_cast<double>(e.degree);
    if (e.degree >= kFloor && np > 0.0 &&
        static_cast<double>(e.degree) / np >= adj_ratio) return Tag::Adjective;
    if (e.det    >= kFloor) return Tag::Noun;
    if (e.modal  >= kFloor) return Tag::Verb;
    return Tag::Unknown;
}

std::size_t Taxis::tagged() const noexcept {
    std::size_t n = 0;
    for (const auto& [w, e] : ev_) {
        (void)e;
        if (tag(w) != Tag::Unknown) ++n;
    }
    return n;
}

void Taxis::save(const std::filesystem::path& prefix) const {
    std::filesystem::create_directories(prefix);
    std::ofstream out(prefix / "main.tax");
    if (!out) return;
    for (const auto& [w, e] : ev_) {
        out << w << '\t' << e.det << '\t' << e.degree << '\t' << e.modal
            << '\t' << e.seen << '\n';
    }
}

void Taxis::load(const std::filesystem::path& prefix) {
    std::ifstream in(prefix / "main.tax");
    if (!in) return;
    ev_.clear();
    std::string w;
    Evidence e;
    while (in >> w >> e.det >> e.degree >> e.modal >> e.seen) ev_[w] = e;
}

} // namespace khora::taxis
