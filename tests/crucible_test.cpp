// Crucible test — the relational reasoning forge.
//
// The Crucible is the module that measures Khora's vector-symbolic reasoning:
// bind role to filler, bundle the pairs into one record glyph, then recover a
// filler by unbinding and cleaning up against the codebook. It had no test at
// all, and the two defects below had been shipping because of it.
//
// Dependency-free, explicit returns rather than assert, so it holds in Release.

#include "khora/crucible/crucible.hpp"

#include <cstdio>
#include <string>
#include <vector>

using khora::crucible::RelationalCrucible;
using khora::crucible::Record;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("  FAIL: %s\n", what); ++failures; }
    else         std::printf("  ok  : %s\n", what);
}

// A world large enough that crosstalk is a real force, not a rounding error.
void load(RelationalCrucible& c, int n) {
    for (int i = 0; i < n; ++i) {
        Record r;
        r.subject = "s" + std::to_string(i);
        r.fields  = {{"capital",   "c" + std::to_string(i)},
                     {"currency",  "m" + std::to_string(i)},
                     {"language",  "l" + std::to_string(i)},
                     {"continent", "k" + std::to_string(i % 7)}};
        c.add_record(std::move(r));
    }
}

} // namespace

int main() {
    std::printf("Crucible test\n");

    RelationalCrucible c;
    load(c, 512);
    c.build();
    check(c.record_count() == 512, "records loaded");
    check(c.role_count() == 4, "roles interned");

    // Baseline reasoning: the answers come from algebra, not from a lookup.
    check(c.query_field("s7", "capital") == "c7", "structured unbind recovers a filler");
    check(c.analogy("s3", "s9", "c3") == "c9", "analogy transfers a role across records");

    {
        const auto t = c.trial_structured_unbind(0.95);
        std::printf("  R=1 structured unbind %zu/%zu = %.2f%%\n",
                    t.correct, t.trials, 100.0 * t.score);
        check(t.score >= 0.99, "structured unbind is near-perfect at redundancy 1");
    }

    // REGRESSION. Encoding stores copy r as perm_r(role XOR filler). Decoding
    // used to unbind with the UNPERMUTED role and permute afterwards, which
    // cancels only at r = 0 -- so copies 1..R-1 were pure noise and every extra
    // redundancy level made recovery WORSE. Measured before the fix, over these
    // same 512 records: R=4 97.02%, R=6 61.47%, R=8 28.66%. The Crucible's
    // "evolution" escalates redundancy on shortfall, so the one self-improvement
    // lever it had was driving accuracy down whenever it engaged.
    //
    // Redundancy is not expected to IMPROVE anything here -- see the note below
    // -- but it must never destroy what works.
    for (int R : {2, 3, 4, 6, 8}) {
        c.set_redundancy(R);
        const auto t = c.trial_structured_unbind(0.95);
        char msg[96];
        std::snprintf(msg, sizeof msg,
                      "redundancy %d does not degrade structured unbind", R);
        std::printf("  R=%d structured unbind %zu/%zu = %.2f%%\n",
                    R, t.correct, t.trials, 100.0 * t.score);
        check(t.score >= 0.99, msg);
    }

    // Every decode path must use the same inverse. query_holographic applied no
    // redundant vote at all, so it read copies 1..R-1 as noise even after
    // query_field was corrected.
    {
        c.set_redundancy(4);
        const auto t = c.trial_holographic(8, 0.70);
        std::printf("  R=4 holographic K=8  %zu/%zu = %.2f%%\n",
                    t.correct, t.trials, 100.0 * t.score);
        check(t.score >= 0.99, "holographic recall uses the redundant unbind too");
        c.set_redundancy(1);
    }

    // CAPACITY BASELINE, for the sparse substrate to be measured against.
    //
    // Records are superposed into ONE 10,000-bit glyph. With dense ~50% random
    // hypervectors, crosstalk sets a hard cliff somewhere past K=32, and no
    // amount of redundancy moves it -- bundling R permuted copies spends the
    // same bits to carry the same information, so it cannot add capacity. This
    // curve is what a genuinely sparse encoding has to beat to be worth the
    // migration.
    std::printf("  capacity (dense, R=1):");
    for (const std::size_t K : {8u, 16u, 32u, 64u}) {
        const auto t = c.trial_holographic(K, 0.70);
        std::printf("  K=%zu %.1f%%", K, 100.0 * t.score);
    }
    std::printf("\n");
    check(c.trial_holographic(8, 0.70).score >= 0.99, "capacity: K=8 is exact");
    check(c.trial_holographic(16, 0.70).score >= 0.95, "capacity: K=16 holds up");

    if (failures == 0) std::printf("ALL PASS\n");
    else               std::printf("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
