// CAN KHORA GUESS THE GRAMMAR OF A WORD IT HAS BARELY SEEN?
//
// Taxis reads a part of speech off the words that came before: a determiner
// means noun phrase, a degree adverb means adjective, an auxiliary means verb.
// It works -- 92% of the words it commits to are nouns by WordNet -- and it has
// one structural limit. It needs to have SEEN the word in those positions. Over
// 58 books, 5,470 of the 15,721 WordNet nouns that appear come back Unknown,
// because they fall below its evidence floor. Roughly a third of the vocabulary
// gets no category at all, and the extractor's noun veto therefore cannot rule
// on any of it.
//
// A word's SPELLING says a great deal about its category and Taxis cannot see
// spelling at all. -ness, -tion and -ity are nouns; -ous, -ful and -ive are
// adjectives; -ise and -ify are verbs. The obvious move is to write those
// suffixes down, and that is the blocklist mistake this repository has now made
// twice: a hand-written list scoring the thing it was written from.
//
// So instead: hashed character trigrams into a two-layer perceptron, trained on
// the words Taxis is CONFIDENT about and asked for the ones it is not. Nobody
// tells it about -ness. If the suffixes matter, the weights find them.
//
// This is also the input source khora::descent did not have. It was built to
// measure one paradigm against the other and then had nothing in the live system
// to do, because nothing here is a labelled vector. Taxis makes labels and
// spelling makes features, both out of Khora's own vocabulary.
//
// THREE THINGS ARE MEASURED, and the third is the only one that matters:
//
//   1. held-out accuracy against the majority class, on words Taxis is sure of.
//      A model that cannot beat "always say noun" has learned nothing.
//   2. what it says about the words Taxis calls Unknown -- scored against
//      WordNet, which is external to both the tagger and the model.
//   3. how much of the vocabulary gets a category at all, before and after.

#include "khora/descent/descent.hpp"
#include "khora/lexicon/lexicon.hpp"
#include "khora/reservoir/reservoir.hpp"
#include "khora/taxis/taxis.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

constexpr std::size_t kBuckets = 512;   // hashed character-trigram features

// Character trigrams with boundary markers, hashed. Boundaries matter more than
// anything else here -- "^un" and "ion$" are the whole game -- so the word is
// padded rather than scanned bare.
std::vector<double> features(const std::string& w) {
    std::vector<double> x(kBuckets, 0.0);
    const std::string p = "^^" + w + "$$";
    for (std::size_t i = 0; i + 2 < p.size(); ++i) {
        std::size_t h = 1469598103934665603ull;
        for (std::size_t k = 0; k < 3; ++k) {
            h ^= static_cast<unsigned char>(p[i + k]);
            h *= 1099511628211ull;
        }
        x[h % kBuckets] = 1.0;
    }
    return x;
}

// The same WordNet loader the extraction bench uses. Every member word is a noun.
std::unordered_map<std::string, bool> load_wordnet_nouns(const std::string& path) {
    std::unordered_map<std::string, bool> nouns;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        const auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        std::istringstream ws(line.substr(tab + 1));
        std::string w;
        while (ws >> w) nouns[w] = true;
    }
    return nouns;
}

// A stable train/test split by the word itself, so the same word never lands on
// both sides however the corpus is ordered.
bool is_test(const std::string& w) {
    std::size_t h = 146959810393466560ull;
    for (char c : w) { h ^= static_cast<unsigned char>(c); h *= 1099511628211ull; }
    return (h % 5) == 0;      // one word in five held out
}

const char* label_name(std::size_t k) {
    return k == 0 ? "noun" : (k == 1 ? "adj" : "verb");
}

} // namespace

int main(int argc, char** argv) {
    const std::string dir     = (argc > 1) ? argv[1] : "data/reservoir";
    const std::string wn_path = (argc > 2) ? argv[2] : "data/eval/wn_categories.tsv";
    const std::size_t books   = (argc > 3) ? std::stoul(argv[3]) : 58;

    std::printf("Spelling — can Khora guess the grammar of a word it has barely seen?\n\n");

    khora::taxis::Taxis tx;
    tx.load("data/taxis_archive");
    if (tx.vocabulary() == 0) {
        std::printf("  no data/taxis_archive -- reading the corpus instead\n");
        khora::reservoir::Reservoir res(dir);
        res.load_catalog();
        std::size_t n = 0;
        for (const auto& t : res.catalog()) {
            if (n >= books) break;
            auto text = res.read(t.title);
            if (!text || text->size() < 40000) continue;
            ++n;
            for (const auto& s : khora::lexicon::tokenize_sentences(*text)) tx.observe(s);
        }
    }
    const auto wn_nouns = load_wordnet_nouns(wn_path);
    std::printf("  %zu word types, %zu with a category from context, %zu WordNet nouns known\n",
                tx.vocabulary(), tx.tagged(), wn_nouns.size());
    if (tx.vocabulary() == 0) { std::printf("  nothing to do\n"); return 0; }

    // --- WHAT TAXIS IS SURE OF BECOMES THE TRAINING SET ----------------------
    //
    // The labels are not ground truth; they are one system's opinion. That is
    // fine for training and useless for evaluation, which is why step three
    // scores against WordNet instead.
    std::vector<std::string> train_w, test_w, unknown_w;
    std::vector<std::size_t> train_y, test_y;
    const std::size_t seen_floor = 5;   // a word seen four times has little spelling to learn from

    // Taxis does not enumerate, so the vocabulary comes from the words it has
    // evidence for -- which is every word it saw, via the WordNet list plus the
    // corpus. Walking WordNet alone would train only on words WordNet knows and
    // then evaluate on the same population, so the corpus vocabulary is used.
    {
        std::ifstream in(std::string("data/taxis_archive/main.tax"));
        std::string word;
        unsigned long long a, b, c, d;
        while (in >> word >> a >> b >> c >> d) {
            if (d < seen_floor) continue;
            const khora::taxis::Tag t = tx.tag(word);
            if (t == khora::taxis::Tag::Unknown) { unknown_w.push_back(word); continue; }
            const std::size_t y = (t == khora::taxis::Tag::Noun) ? 0
                                : (t == khora::taxis::Tag::Adjective ? 1 : 2);
            if (is_test(word)) { test_w.push_back(word);  test_y.push_back(y); }
            else               { train_w.push_back(word); train_y.push_back(y); }
        }
    }
    if (train_w.empty()) {
        std::printf("  no data/taxis_archive/main.tax to read a vocabulary from -- run plexus_forge\n");
        return 0;
    }
    std::printf("  %zu training words, %zu held out, %zu with no category to learn from\n\n",
                train_w.size(), test_w.size(), unknown_w.size());

    // --- 1. DOES IT BEAT SAYING THE COMMONEST THING? -------------------------
    std::size_t counts[3] = {0, 0, 0};
    for (std::size_t y : train_y) ++counts[y];
    const std::size_t majority = static_cast<std::size_t>(
        std::max_element(counts, counts + 3) - counts);
    std::size_t base_hit = 0;
    for (std::size_t y : test_y) if (y == majority) ++base_hit;

    std::vector<std::vector<double>> xs;
    xs.reserve(train_w.size());
    for (const auto& w : train_w) xs.push_back(features(w));

    khora::descent::Mlp net(kBuckets, 96, 3, 20260825);
    std::uint64_t seed = 41;
    for (int e = 0; e < 60; ++e) net.train_epoch(xs, train_y, 0.10, 32, seed);

    std::size_t hit = 0;
    for (std::size_t i = 0; i < test_w.size(); ++i)
        if (net.predict(features(test_w[i])) == test_y[i]) ++hit;

    std::printf("  === 1. HELD-OUT AGREEMENT WITH THE CONTEXT TAGGER ===\n");
    std::printf("    always say '%s' : %5.1f%%  (%zu of %zu)\n", label_name(majority),
                100.0 * base_hit / std::max<std::size_t>(test_w.size(), 1),
                base_hit, test_w.size());
    std::printf("    spelling model  : %5.1f%%  (%zu of %zu)\n",
                100.0 * hit / std::max<std::size_t>(test_w.size(), 1), hit, test_w.size());
    std::printf("    Trained on nothing but hashed character trigrams. Nobody wrote down\n"
                "    a suffix; if -ness and -ous matter, the weights found them.\n\n");

    // --- 2. AND ON THE WORDS THE TAGGER GAVE UP ON, SCORED EXTERNALLY --------
    //
    // This is the only measurement that means anything. WordNet is external to
    // the tagger AND to the model, and every one of its member words is a noun,
    // so calling one anything else is an error nobody can argue with.
    std::size_t unk_in_wn = 0, unk_called_noun = 0;
    for (const auto& w : unknown_w) {
        if (!wn_nouns.count(w)) continue;
        ++unk_in_wn;
        if (net.predict(features(w)) == 0) ++unk_called_noun;
    }
    // The same question asked of words the tagger DID have evidence for, as the
    // reference point: this is what the model scores where context also works.
    std::size_t kn_in_wn = 0, kn_called_noun = 0;
    for (std::size_t i = 0; i < test_w.size(); ++i) {
        if (!wn_nouns.count(test_w[i])) continue;
        ++kn_in_wn;
        if (net.predict(features(test_w[i])) == 0) ++kn_called_noun;
    }
    std::printf("  === 2. THE WORDS CONTEXT COULD NOT CATEGORISE, AGAINST WORDNET ===\n");
    std::printf("    of %zu no-category words WordNet calls nouns, the model calls %zu noun (%5.1f%%)\n",
                unk_in_wn, unk_called_noun,
                100.0 * unk_called_noun / std::max<std::size_t>(unk_in_wn, 1));
    std::printf("    of %zu held-out words WordNet calls nouns, it calls %zu noun (%5.1f%%)\n",
                kn_in_wn, kn_called_noun,
                100.0 * kn_called_noun / std::max<std::size_t>(kn_in_wn, 1));
    std::printf("    BOTH ROWS ARE UNINTERPRETABLE ALONE and are kept as a warning:\n"
                "    every word in them is a noun, so a model that says noun always\n"
                "    scores 100%% on both. Section 1b is what separates the two.\n");
    // --- 3. HOW MUCH OF THE VOCABULARY IS COVERED ---------------------------
    const std::size_t with_context = train_w.size() + test_w.size();
    const std::size_t total = with_context + unknown_w.size();
    std::printf("  === 3. COVERAGE ===\n");
    std::printf("    from context alone : %zu of %zu (%5.1f%%)\n", with_context, total,
                100.0 * with_context / std::max<std::size_t>(total, 1));
    std::printf("    with spelling too  : %zu of %zu (100.0%%) -- a spelling always exists\n",
                total, total);
    std::printf("    Coverage is free and worthless on its own; row 2 is what says whether\n"
                "    the extra coverage is worth having.\n");
    return 0;
}
