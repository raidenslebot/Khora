// DOES THE EXTRACTOR LEARN TRUE THINGS?
//
// Khora's Ligature holds 19,475 typed relations pulled from 22 MB of real
// books, and the whole symbolic layer -- deduce(), Crystallize, the abstraction
// tower, infer_path -- stands on them. Inspecting them for the first time found
// this:
//
//     man     is-a: animal(7) creature(4) weber(4) social(3)
//     time     has: come(10) arrived(4) workers(4)
//     body    is-a: row(16) matter(2)
//     mind     has: life(4) eyes(4) arms(1)
//     derived: "man has range", "justice has down", "justice has done"
//
// Every derivation's chain HELD. The reasoning was sound; the premises were
// garbage. And deduce()'s own self-benchmark reported 1.000, because it
// constructs its own cases -- a system inventing a benchmark and scoring itself
// perfectly on it while producing visible nonsense on real data.
//
// Two causes, both structural rather than tuning:
//
//   NO SENTENCE BOUNDARIES. The shared tokenizer discards punctuation, and the
//   extractor scans up to five words past a verb for the head of its object. It
//   therefore walks straight into the next sentence. "weber" and "adler" are
//   proper nouns from the following sentence; "row" was asserted 16 times.
//
//   has/have DID NOT REQUIRE A DETERMINER, though is/are did. English's perfect
//   tense then reads as possession: "the time has come" -> HAS-PART(time, come).
//
// This measures both rules on the same real books, before and against after.
// There is no ground truth for "is this relation true", so what is counted is
// what CAN be counted mechanically: how many objects are function words or
// participles that could not be a noun, and how many relations survive at all.

#include "khora/lexicon/lexicon.hpp"
#include "khora/ligature/ligature.hpp"
#include "khora/reservoir/reservoir.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <vector>

using namespace khora::ligature;

namespace {

// Words that cannot be the object of a taxonomic or part-whole relation. Past
// participles and adverbs, mostly -- the signature of the perfect-tense bug.
// A count of how many objects land in this set is a lower bound on the error
// rate, since it catches only the unambiguous cases.
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

struct Report {
    std::uint64_t triples = 0;
    std::size_t   objects_sampled = 0;
    std::size_t   impossible = 0;
};

Report audit(const Ligature& lig, const std::vector<std::string>& probes) {
    Report r;
    r.triples = lig.triple_count();
    for (const auto& p : probes) {
        for (const Relation rel : {Relation::IsA, Relation::Causes, Relation::HasPart}) {
            for (const auto& [o, c] : lig.objects(rel, p, 8)) {
                (void)c;
                ++r.objects_sampled;
                if (impossible_objects().count(o)) ++r.impossible;
            }
        }
    }
    return r;
}

void show(const Ligature& lig, const std::vector<std::string>& probes, const char* label) {
    std::printf("\n  --- %s ---\n", label);
    for (const auto& p : probes) {
        const auto isa = lig.objects(Relation::IsA, p, 3);
        const auto has = lig.objects(Relation::HasPart, p, 3);
        if (isa.empty() && has.empty()) continue;
        std::printf("  %-11s", p.c_str());
        if (!isa.empty()) {
            std::printf(" is-a:");
            for (const auto& [o, c] : isa) std::printf(" %s(%u)", o.c_str(), c);
        }
        if (!has.empty()) {
            std::printf("  has:");
            for (const auto& [o, c] : has) std::printf(" %s(%u)", o.c_str(), c);
        }
        std::printf("\n");
    }
}

} // namespace

int main(int argc, char** argv) {
    const std::string dir = (argc > 1) ? argv[1] : "data/reservoir";
    khora::reservoir::Reservoir res(dir);
    res.load_catalog();
    const auto cat = res.catalog();
    std::printf("Relation extraction, measured on Khora's own books\n");
    if (cat.empty()) { std::printf("  empty catalog\n"); return 0; }

    const std::vector<std::string> probes = {
        "man", "woman", "government", "money", "light", "time", "water",
        "law", "justice", "labour", "body", "mind", "state", "war", "nature"
    };

    Ligature flat, split;
    std::size_t books = 0, sentences = 0, tokens = 0;
    for (const auto& t : cat) {
        if (books >= 20) break;
        auto text = res.read(t.title);
        if (!text || text->size() < 40000) continue;
        ++books;

        // OLD: the whole document as one token stream, boundaries invisible.
        const auto flat_toks = khora::lexicon::tokenize(*text);
        tokens += flat_toks.size();
        flat.extract(flat_toks);

        // NEW: one call per sentence, so nothing can read past the full stop.
        for (const auto& s : khora::lexicon::tokenize_sentences(*text)) {
            ++sentences;
            split.extract(s);
        }
    }

    std::printf("  %zu books, %zu tokens, %zu sentences (%.1f words each)\n",
                books, tokens, sentences,
                sentences ? static_cast<double>(tokens) / sentences : 0.0);

    const Report a = audit(flat, probes);
    const Report b = audit(split, probes);

    std::printf("\n  rule set                  | triples | objects | impossible objects\n");
    std::printf("  --------------------------+---------+---------+--------------------\n");
    std::printf("  document-at-once (old)    | %7llu | %7zu |  %3zu  (%.1f%%)\n",
                static_cast<unsigned long long>(a.triples), a.objects_sampled,
                a.impossible,
                a.objects_sampled ? 100.0 * a.impossible / a.objects_sampled : 0.0);
    std::printf("  per-sentence + det rule   | %7llu | %7zu |  %3zu  (%.1f%%)\n",
                static_cast<unsigned long long>(b.triples), b.objects_sampled,
                b.impossible,
                b.objects_sampled ? 100.0 * b.impossible / b.objects_sampled : 0.0);

    show(flat,  probes, "BEFORE: document-at-once");
    show(split, probes, "AFTER: per-sentence, has/have needs a determiner");

    // CORROBORATION. A relation asserted once, in one book, by one pattern, is
    // a guess. One asserted repeatedly across a corpus has been seen agreeing
    // with itself. The Plexus already refuses co-occurrences below a floor of
    // three at query time; the Ligature has no such rule and surfaces
    // support-1 triples as facts.
    //
    // This is the cheapest available filter and the question is how much of
    // the noise it removes -- and how much of the signal goes with it.
    std::printf("\n  === CORROBORATION FLOOR ===\n");
    std::printf("    min support | surviving objects | impossible | rate\n");
    std::printf("    ------------+-------------------+------------+-------\n");
    for (const std::uint32_t floor : {1u, 2u, 3u, 5u}) {
        std::size_t kept = 0, bad = 0;
        for (const auto& p : probes) {
            for (const Relation rel : {Relation::IsA, Relation::Causes, Relation::HasPart}) {
                for (const auto& [o, c] : split.objects(rel, p, 8)) {
                    if (c < floor) continue;
                    ++kept;
                    if (impossible_objects().count(o)) ++bad;
                }
            }
        }
        std::printf("    %11u | %17zu | %10zu | %5.1f%%\n", floor, kept, bad,
                    kept ? 100.0 * bad / kept : 0.0);
    }
    std::printf("\n    --- what survives a floor of 3 ---\n");
    for (const auto& p : probes) {
        bool any = false;
        for (const Relation rel : {Relation::IsA, Relation::Causes, Relation::HasPart}) {
            for (const auto& [o, c] : split.objects(rel, p, 8)) {
                if (c < 3) continue;
                if (!any) { std::printf("    %-11s", p.c_str()); any = true; }
                std::printf(" %s-%s(%u)", relation_name(rel), o.c_str(), c);
            }
        }
        if (any) std::printf("\n");
    }

    std::printf("\n  'impossible objects' counts objects that cannot be the object of\n"
                "  a taxonomic or part-whole relation -- past participles and adverbs,\n"
                "  the signature of reading perfect tense as possession. It catches\n"
                "  only unambiguous cases, so it is a LOWER BOUND on the error rate.\n");
    return 0;
}
