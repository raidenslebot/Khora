// IS KHORA READING THE WRONG THINGS?
//
// Inspecting the learned relations found that almost none survive being asked
// to have been seen twice, and diagnosed a mismatch rather than a bug: Hearst
// patterns ("X is a Y", "X such as Y") were designed for DEFINITIONAL text --
// dictionaries, encyclopedias, textbooks -- and Khora's Curator forages
// Gutenberg, which is mostly novels and philosophy. "X is a Y" in Austen is
// rhetoric; in Plato it is a position under examination. The extractor harvests
// metaphor as fact.
//
// That was a diagnosis, not a measurement. This is the measurement, and it needs
// no new data: Khora's own shelf already holds both genres. Run the SAME
// extractor over a literary set and a descriptive set and compare what comes out.
//
// Probes are the most frequent content words of each corpus rather than a fixed
// list, so each genre is asked about its own subject matter and neither is
// judged on vocabulary it never uses.

#include "khora/lexicon/lexicon.hpp"
#include "khora/ligature/ligature.hpp"
#include "khora/reservoir/reservoir.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace khora::ligature;

namespace {

const std::unordered_set<std::string>& impossible_objects() {
    static const std::unordered_set<std::string> s = {
        "come", "gone", "done", "been", "got", "made", "taken", "given", "seen",
        "known", "found", "said", "told", "put", "kept", "left", "held", "brought",
        "become", "arrived", "passed", "failed", "exhausted", "grown", "deprived",
        "plunged", "occurred", "rendered", "followed", "elicited", "taught",
        "signified", "down", "too", "away", "perhaps", "hitherto", "before",
        "anything", "nothing", "yourself", "himself", "herself", "another",
        "uniformly", "easy", "best", "greatest", "fair", "ordinary", "similar",
        "necessary", "tyrannical", "unsuccessful", "exact", "through", "outside"
    };
    return s;
}

struct Corpus {
    const char* label;
    std::vector<std::string> titles;
};

struct Outcome {
    std::size_t   tokens = 0, sentences = 0;
    std::uint64_t triples = 0;
    std::size_t   objects = 0, impossible = 0;
    std::size_t   at2 = 0, at3 = 0;          // objects surviving a support floor
    std::vector<std::string> examples;
};

Outcome study(khora::reservoir::Reservoir& res, const Corpus& c) {
    Outcome o;
    Ligature lig;
    std::unordered_map<std::string, std::size_t> freq;

    for (const auto& title : c.titles) {
        auto text = res.read(title);
        if (!text) continue;
        for (const auto& s : khora::lexicon::tokenize_sentences(*text)) {
            ++o.sentences;
            o.tokens += s.size();
            lig.extract(s);
            for (const auto& w : s) if (w.size() >= 4) ++freq[w];
        }
    }
    o.triples = lig.triple_count();

    // Probe the corpus's own most frequent content words.
    std::vector<std::pair<std::size_t, std::string>> by_freq;
    by_freq.reserve(freq.size());
    for (const auto& [w, n] : freq) by_freq.emplace_back(n, w);
    std::sort(by_freq.rbegin(), by_freq.rend());

    std::vector<std::pair<std::uint32_t, std::string>> best;
    std::size_t probed = 0;
    for (const auto& [n, w] : by_freq) {
        if (probed >= 60) break;
        ++probed;
        (void)n;
        for (const Relation rel : {Relation::IsA, Relation::Causes, Relation::HasPart}) {
            for (const auto& [obj, sup] : lig.objects(rel, w, 8)) {
                ++o.objects;
                if (impossible_objects().count(obj)) ++o.impossible;
                if (sup >= 2) ++o.at2;
                if (sup >= 3) {
                    ++o.at3;
                    best.emplace_back(sup, w + " " + relation_name(rel) + " " + obj);
                }
            }
        }
    }
    std::sort(best.rbegin(), best.rend());
    for (std::size_t i = 0; i < best.size() && i < 12; ++i) {
        o.examples.push_back(std::to_string(best[i].first) + "x  " + best[i].second);
    }
    return o;
}

void report(const char* label, const Outcome& o) {
    std::printf("\n  === %s ===\n", label);
    std::printf("  %zu tokens, %zu sentences, %llu triples\n",
                o.tokens, o.sentences, static_cast<unsigned long long>(o.triples));
    std::printf("  relations per 1000 tokens: %.2f\n",
                o.tokens ? 1000.0 * o.triples / o.tokens : 0.0);
    std::printf("  probed objects %zu | impossible %zu (%.1f%%)"
                " | support>=2 %zu (%.1f%%) | support>=3 %zu (%.1f%%)\n",
                o.objects, o.impossible,
                o.objects ? 100.0 * o.impossible / o.objects : 0.0,
                o.at2, o.objects ? 100.0 * o.at2 / o.objects : 0.0,
                o.at3, o.objects ? 100.0 * o.at3 / o.objects : 0.0);
    std::printf("  best-corroborated relations it learned:\n");
    if (o.examples.empty()) std::printf("    (none reached support 3)\n");
    for (const auto& e : o.examples) std::printf("    %s\n", e.c_str());
}

} // namespace

int main(int argc, char** argv) {
    const std::string dir = (argc > 1) ? argv[1] : "data/reservoir";
    khora::reservoir::Reservoir res(dir);
    res.load_catalog();
    if (res.catalog().empty()) { std::printf("empty catalog\n"); return 0; }

    std::printf("Same extractor, two genres from Khora's own shelf\n");

    const Corpus literary{
        "LITERARY AND PHILOSOPHICAL (what the Curator mostly forages)",
        {"Pride and Prejudice", "The Republic", "Moby Dick", "Jane Eyre",
         "Crime and Punishment", "Thus Spake Zarathustra", "Great Expectations",
         "Wuthering Heights"}};

    const Corpus descriptive{
        "DESCRIPTIVE AND SCIENTIFIC (what Hearst patterns were designed for)",
        {"The Origin of Species", "The Outline of Science", "The Descent of Man",
         "An Account of the Insects Noxious to Agriculture and Plants in New Zealand",
         "The Voyage of the Beagle", "The Chemical History of a Candle",
         "Six Lectures on Light", "The Story of the Heavens"}};

    const Outcome a = study(res, literary);
    const Outcome b = study(res, descriptive);
    report(literary.label, a);
    report(descriptive.label, b);

    std::printf("\n  Both sets are eight books read by the same extractor with the\n"
                "  same rules. The only variable is what kind of text it is.\n");
    return 0;
}
