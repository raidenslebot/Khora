// IS WHAT KHORA LEARNED FROM READING ACTUALLY TRUE?
//
// The Ligature holds 19,475 typed relations -- is-a, causes, has-part -- pulled
// out of 22 MB of real books by pattern extraction. The Crystallize module votes
// new relations out of them, deduce() chains them into derived facts, and the
// whole symbolic layer of Khora stands on top.
//
// Nobody has ever looked at them.
//
// That is the uncomfortable question and it comes before anything built on it:
// reasoning soundly over false premises produces confident falsehood, and an
// audit of this codebase already flagged the specific way the extractor can go
// wrong -- its object window is a fixed number of words, so it runs past a
// sentence boundary. "a sparrow is a small bird. friction causes heat" would
// yield IS-A(sparrow, heat).
//
// So: dump what it learned, sample it, and count how much survives inspection.
// No metric here is automatic, because there is no ground truth to check
// against -- the point is to put the actual triples on screen where they can be
// judged, and to measure the structural properties that CAN be checked
// mechanically: how much of the taxonomy is reachable, how deep it goes, whether
// it contains cycles, and whether deduce() derives anything that its own chain
// does not support.

#include "khora/ligature/ligature.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <vector>

using namespace khora::ligature;

namespace {

std::uint64_t seed = 0x4B4E4F57ULL;
std::uint64_t rnd() {
    std::uint64_t z = (seed += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

} // namespace

int main(int argc, char** argv) {
    const std::string prefix = (argc > 1) ? argv[1] : "data/ligature_archive/main";
    Ligature lig;
    lig.load(prefix);

    std::printf("What Khora learned by reading\n");
    std::printf("  %llu triples from %llu assertions\n\n",
                static_cast<unsigned long long>(lig.triple_count()),
                static_cast<unsigned long long>(lig.assertions()));
    if (lig.triple_count() == 0) {
        std::printf("  nothing loaded -- pass the ligature archive prefix\n");
        return 0;
    }

    // Concepts to look at. Deliberately ordinary words that any of these books
    // would discuss, chosen before seeing any output rather than after.
    const std::vector<std::string> probes = {
        "man", "woman", "government", "money", "light", "time", "water",
        "law", "justice", "labour", "body", "mind", "state", "war", "nature"
    };

    std::printf("  === WHAT IT BELIEVES ===\n");
    int shown = 0, with_isa = 0;
    for (const auto& p : probes) {
        const auto isa = lig.objects(Relation::IsA, p, 4);
        const auto cau = lig.objects(Relation::Causes, p, 3);
        const auto has = lig.objects(Relation::HasPart, p, 3);
        if (isa.empty() && cau.empty() && has.empty()) continue;
        ++shown;
        if (!isa.empty()) ++with_isa;
        std::printf("  %-11s", p.c_str());
        if (!isa.empty()) {
            std::printf(" is-a:");
            for (const auto& [o, c] : isa) std::printf(" %s(%u)", o.c_str(), c);
        }
        if (!cau.empty()) {
            std::printf("  causes:");
            for (const auto& [o, c] : cau) std::printf(" %s(%u)", o.c_str(), c);
        }
        if (!has.empty()) {
            std::printf("  has:");
            for (const auto& [o, c] : has) std::printf(" %s(%u)", o.c_str(), c);
        }
        std::printf("\n");
    }
    std::printf("\n  %d of %zu probe concepts have any relation at all;"
                " %d have an is-a.\n", shown, probes.size(), with_isa);

    // Structural properties that CAN be checked without ground truth.
    std::printf("\n  === STRUCTURE ===\n");

    // Taxonomic depth: how far up does is-a actually chain? A taxonomy that is
    // one level deep everywhere is a list of labels, not a hierarchy.
    {
        int reached2 = 0, reached3 = 0, sampled = 0;
        for (const auto& p : probes) {
            const auto l1 = lig.objects(Relation::IsA, p, 1);
            if (l1.empty()) continue;
            ++sampled;
            const auto l2 = lig.objects(Relation::IsA, l1[0].first, 1);
            if (l2.empty()) continue;
            ++reached2;
            const auto l3 = lig.objects(Relation::IsA, l2[0].first, 1);
            if (!l3.empty()) ++reached3;
        }
        std::printf("  taxonomic depth from %d probes with an is-a:"
                    " %d reach 2 levels, %d reach 3\n", sampled, reached2, reached3);
    }

    // Symmetry check. is-a is ANTI-symmetric: if x is-a y then y is-a x must be
    // false. Any pair asserting both directions is a definite extraction error,
    // and needs no ground truth to detect.
    {
        int both = 0, checked = 0;
        for (const auto& p : probes) {
            for (const auto& [o, c] : lig.objects(Relation::IsA, p, 8)) {
                (void)c;
                ++checked;
                if (lig.count(Relation::IsA, o, p) > 0) {
                    ++both;
                    std::printf("  CONTRADICTION: %s is-a %s AND %s is-a %s\n",
                                p.c_str(), o.c_str(), o.c_str(), p.c_str());
                }
            }
        }
        std::printf("  anti-symmetry: %d of %d sampled is-a pairs assert BOTH"
                    " directions\n", both, checked);
    }

    // === DERIVED FACTS, AND WHETHER THEIR CHAINS HOLD UP ===
    //
    // deduce() produces facts Khora never read, each with the chain that
    // produced it. The chain is what makes the claim checkable: every step of it
    // must be a relation that is actually asserted.
    std::printf("\n  === WHAT IT DERIVES (facts it never read) ===\n");
    int derivations = 0, sound_chains = 0;
    for (const auto& p : probes) {
        const auto infs = lig.deduce(p, 3);
        for (std::size_t i = 0; i < infs.size() && i < 2; ++i) {
            const auto& f = infs[i];
            ++derivations;
            std::printf("  %s %s %s   (support %u, via:",
                        p.c_str(), relation_name(f.relation), f.object.c_str(),
                        f.support);
            for (const auto& v : f.via) std::printf(" %s", v.c_str());
            std::printf(")\n");

            // Check the chain: subject -> via[0] -> ... -> object must each be a
            // relation the Ligature actually holds. A derivation whose own chain
            // does not hold is not reasoning, it is a coincidence with a label.
            bool sound = true;
            std::string cur = p;
            for (const auto& v : f.via) {
                if (lig.count(Relation::IsA, cur, v) == 0 &&
                    lig.count(Relation::Causes, cur, v) == 0 &&
                    lig.count(Relation::HasPart, cur, v) == 0) { sound = false; break; }
                cur = v;
            }
            if (sound && lig.count(f.relation, cur, f.object) == 0) sound = false;
            if (sound) ++sound_chains;
            else       std::printf("      ^ CHAIN DOES NOT HOLD\n");
        }
    }
    std::printf("\n  %d derivations sampled, %d have chains that actually hold\n",
                derivations, sound_chains);

    // deduce()'s own self-measurement, for comparison with the above.
    std::printf("\n  deduce() self-benchmark (its own constructed cases): %.3f\n",
                lig.benchmark_deduction(200, 42));
    std::printf("\n  Note the difference between the two numbers above. The"
                " self-benchmark\n  constructs cases it knows are derivable and"
                " measures recall on them.\n  The chain check asks whether"
                " derivations from REAL learned relations\n  are supported by"
                " relations that were really asserted. Only the second\n"
                "  can catch a soundly-reasoned falsehood.\n");
    return 0;
}
