// ContextTree test — the pawl, and the properties it has to have.
//
// This module exists because the corpus measurement said deep context is a dead
// end on prose: 8-word contexts recur 0.32% of the time even at 7.66M tokens,
// while 1/2/3-word contexts recur 66.9 / 30.7 / 13.5%. So the requirement is not
// "predict well" in the abstract, it is:
//
//   1. Predict from the LONGEST context that has actually been seen, and fall
//      back when it has not. That is the generalisation pressure TemporalMemory
//      lacks -- the pawl on the ratchet.
//   2. STAY BOUNDED. TemporalMemory grew to 2.9M segments on 24k tokens with no
//      ceiling. This must hold a hard node budget under a stream that never
//      stops producing novel contexts.
//   3. Refuse to predict when it genuinely has nothing, rather than guessing.

#include "khora/cortex/context_tree.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace khora::cortex;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("  FAIL: %s\n", what); ++failures; }
    else         std::printf("  ok  : %s\n", what);
}

std::uint64_t rs = 0xC7C7C7ULL;
std::uint32_t rnd() {
    std::uint64_t z = (rs += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return static_cast<std::uint32_t>((z ^ (z >> 31)) & 0xFFFFFFFFu);
}

} // namespace

int main() {
    std::printf("ContextTree test\n");

    // --- it learns a repeating sequence, and knows which order it used ------
    {
        ContextTree ct;
        for (int rep = 0; rep < 10; ++rep) {
            ct.reset();
            for (const std::uint32_t s : {1u, 2u, 3u, 4u, 5u}) ct.observe(s);
        }
        ct.reset();
        ct.observe(1); ct.observe(2); ct.observe(3);
        const auto p = ct.predict();
        std::printf("  after 1,2,3 -> %u (order %zu, conf %.2f)\n",
                    p.symbol, p.order, p.confidence);
        check(p.known && p.symbol == 4, "predicts the continuation of a learned sequence");
        check(p.order >= 2, "and uses a deep context to do it");
    }

    // --- THE PAWL: it backs off rather than failing -------------------------
    //
    // Train a deep pattern, then present a context whose long form was never
    // seen but whose short form was. A system without backoff has nothing to
    // say. This one must drop to the order that exists.
    {
        ContextTree ct;
        for (int rep = 0; rep < 10; ++rep) {
            ct.reset();
            for (const std::uint32_t s : {10u, 20u, 30u, 40u}) ct.observe(s);
        }
        ct.reset();
        // 99 was never seen before 30, so the order-2 context (99,30) is novel
        // while the order-1 context (30) is well attested.
        ct.observe(99); ct.observe(30);
        const auto p = ct.predict();
        std::printf("  after unseen 99 then 30 -> %u (order %zu)\n", p.symbol, p.order);
        check(p.known, "still predicts when the long context is novel");
        check(p.symbol == 40, "backs off to the order that was seen, and gets it right");
        check(p.order <= 1, "and reports the shorter order it actually used");
    }

    // --- it refuses when it truly has nothing -------------------------------
    {
        ContextTree ct;
        const auto p = ct.predict();
        check(!p.known, "an empty model refuses to predict rather than guessing");
    }

    // --- min_count: one sighting is not a pattern ---------------------------
    {
        ContextTreeConfig cfg;
        cfg.min_count = 2;
        ContextTree ct(cfg);
        ct.observe(7); ct.observe(8);       // seen exactly once
        ct.reset();
        ct.observe(7);
        const auto p = ct.predict();
        check(!p.known || p.order == 0,
              "a context seen once does not predict from that context");
    }

    // --- BOUNDED under a stream of pure novelty -----------------------------
    //
    // The failure this module exists to avoid. TemporalMemory, fed contexts
    // that never recur, allocated forever: 2.9M segments on 24k tokens. Feed
    // this one 40,000 entirely random symbols -- the worst possible case, zero
    // recurrence by construction -- and it must hold its budget.
    {
        ContextTreeConfig cfg;
        cfg.max_nodes = 20000;
        ContextTree ct(cfg);
        for (int i = 0; i < 40000; ++i) ct.observe(rnd() % 100000);
        std::printf("  40,000 novel symbols -> %zu nodes (budget %zu), %zu evicted\n",
                    ct.nodes(), cfg.max_nodes, ct.evicted());
        check(ct.nodes() <= cfg.max_nodes,
              "holds a hard node budget against a stream of pure novelty");
        check(ct.evicted() > 0, "and evicts to do it, rather than never filling");
    }

    // --- the budget does not destroy what is worth keeping ------------------
    //
    // Eviction by utility, not by age: a pattern that keeps being USED should
    // survive a flood of novelty that arrives after it.
    {
        ContextTreeConfig cfg;
        cfg.max_nodes = 5000;
        ContextTree ct(cfg);
        // Learn a pattern, and keep querying it so it accrues utility.
        for (int rep = 0; rep < 50; ++rep) {
            ct.reset();
            for (const std::uint32_t s : {101u, 102u, 103u}) ct.observe(s);
            ct.reset();
            ct.observe(101); ct.observe(102);
            (void)ct.predict();
        }
        // Now flood with noise.
        for (int i = 0; i < 20000; ++i) ct.observe(200000 + (rnd() % 100000));
        ct.reset();
        ct.observe(101); ct.observe(102);
        const auto p = ct.predict();
        std::printf("  after a 20,000-symbol flood -> %u (order %zu), %zu nodes\n",
                    p.symbol, p.order, ct.nodes());
        check(p.known && p.symbol == 103,
              "a used pattern survives a flood of novelty (utility-based eviction)");
    }

    // --- order usage is reported, so backoff is visible ---------------------
    {
        ContextTree ct;
        for (int rep = 0; rep < 5; ++rep) {
            ct.reset();
            for (std::uint32_t s = 1; s <= 6; ++s) ct.observe(s);
        }
        ct.reset();
        for (std::uint32_t s = 1; s <= 5; ++s) { ct.observe(s); (void)ct.predict(); }
        std::size_t total = 0;
        for (const std::size_t u : ct.order_usage()) total += u;
        check(total > 0, "order usage is counted, so backoff depth is measurable");
    }

    std::printf("\n");
    if (failures == 0) std::printf("ALL PASS\n");
    else               std::printf("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
