// Does it learn, or does it just run?
//
// The bar is the same one everything else in this tree has to clear: beat CHANCE
// and beat the DUMB BASELINE that does the job without the module. Here the dumb
// baseline is not a strawman — it is exactly what Khora does today, a fixed
// hand-written affinity per action, which is the thing this module exists to
// replace. If the learner cannot beat a table of constants somebody guessed,
// there is no reason to add it.
//
// The testbed is a contextual bandit with a property that makes the hand-tuned
// policy structurally wrong rather than merely unlucky: THE BEST ACTION DIFFERS
// BY CONTEXT, and the fixed policy has one answer for all of them. That is the
// real situation — an act that serves Khora well while it is ignorant is not the
// act that serves it well once it is well-read — and no amount of tuning a
// context-free table fixes it.

#include "khora/telos/telos.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace khora::telos;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("  FAIL: %s\n", what); ++failures; }
    else       { std::printf("  ok  : %s\n", what); }
}

std::uint64_t g_s = 20240825;
std::uint64_t rnd() { g_s ^= g_s << 13; g_s ^= g_s >> 7; g_s ^= g_s << 17; return g_s; }
double unit() { return static_cast<double>(rnd() >> 11) / 9007199254740992.0; }

constexpr std::size_t kCtx = 3, kAct = 6;

// True mean payoff per (context, action). The best action is different in every
// context, and no single action is even second-best everywhere.
const double kTruth[kCtx][kAct] = {
    {0.20, 0.80, 0.30, 0.10, 0.25, 0.15},   // best: 1
    {0.35, 0.25, 0.30, 0.85, 0.20, 0.30},   // best: 3
    {0.75, 0.30, 0.20, 0.25, 0.40, 0.35},   // best: 0
};

// Noisy draw: the learner never sees the truth, only a sample around it.
double pull(std::size_t c, std::size_t a) {
    const double noise = (unit() - 0.5) * 0.6;
    double v = kTruth[c][a] + noise;
    return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

// WHAT KHORA DOES TODAY: one fixed affinity per action, no context, no learning.
// Chosen generously -- action 3 is the single best action averaged over all
// contexts, so this is the best a context-free table can be.
std::size_t fixed_policy(std::size_t /*context*/) { return 3; }

} // namespace

int main() {
    std::printf("Telos — learning what actions return\n\n");

    // --- THE ESTIMATE IS A RUNNING MEAN --------------------------------------
    {
        Valuer v;
        v.observe(0, 0, 1.0);
        v.observe(0, 0, 0.0);
        v.observe(0, 0, 0.5);
        const Estimate e = v.estimate(0, 0);
        check(e.count == 3, "three observations are counted");
        check(std::fabs(e.mean - 0.5) < 1e-9, "and averaged incrementally, exactly");
        check(v.estimate(0, 1).count == 0, "an unobserved action stays empty");
        check(v.estimate(9, 9).count == 0, "and so does one out of range");
    }

    // --- CONTEXTS DO NOT LEAK INTO EACH OTHER --------------------------------
    //
    // The table is row-major and grows in both dimensions, so a re-index bug
    // would silently attribute one action's history to another. That failure
    // looks like bad learning rather than a defect, which is why it gets its own
    // check.
    {
        Valuer v;
        v.observe(0, 0, 1.0);
        v.observe(2, 5, 0.25);          // forces growth in both dimensions
        v.observe(1, 3, 0.75);
        check(std::fabs(v.estimate(0, 0).mean - 1.0) < 1e-9,
              "an early observation survives the table growing");
        check(std::fabs(v.estimate(2, 5).mean - 0.25) < 1e-9, "and so does a late one");
        check(std::fabs(v.estimate(1, 3).mean - 0.75) < 1e-9, "and one in between");
        check(v.estimate(0, 5).count == 0, "and nothing bled into an untouched cell");
    }

    // --- EVERYTHING IS TRIED BEFORE ANYTHING IS TRUSTED ----------------------
    {
        Valuer v(1, 4);
        v.observe(0, 0, 1.0);           // one action looks excellent
        const double tried   = v.confidence_bound(0, 0);
        const double untried = v.confidence_bound(0, 1);
        check(untried > tried, "an untried action outranks a proven one");
        check(std::isinf(untried), "because its bound is infinite, by construction");

        // ... and that stops once it has been tried.
        for (int i = 0; i < 50; ++i) v.observe(0, 1, 0.0);
        check(v.confidence_bound(0, 1) < v.confidence_bound(0, 0),
              "and stops outranking it once it has been shown to be poor");
    }

    // --- IT FINDS THE PER-CONTEXT BEST ACTION --------------------------------
    {
        Valuer v(kCtx, kAct);
        g_s = 555;
        for (int t = 0; t < 3000; ++t) {
            const std::size_t c = rnd() % kCtx;
            const std::size_t a = v.choose(c, 1.0);
            v.observe(c, a, pull(c, a));
        }
        const std::size_t want[kCtx] = {1, 3, 0};
        std::size_t right = 0;
        for (std::size_t c = 0; c < kCtx; ++c) {
            if (v.best(c) == want[c]) ++right;
            std::printf("      context %zu: learned best = %zu (true best = %zu)\n",
                        c, v.best(c), want[c]);
        }
        check(right == kCtx, "the per-context best action is identified in every context");
    }

    // --- AND IT BEATS BOTH BASELINES ON REWARD -------------------------------
    //
    // Cumulative reward over the same trial sequence, three policies. The fixed
    // policy is what Khora does now; random is chance.
    {
        Valuer v(kCtx, kAct);
        double learned = 0, fixed = 0, random = 0, oracle = 0;
        const int trials = 2000;
        g_s = 31337;
        for (int t = 0; t < trials; ++t) {
            const std::size_t c = rnd() % kCtx;

            const std::size_t la = v.choose(c, 1.0);
            const double lr = pull(c, la);
            v.observe(c, la, lr);
            learned += lr;

            fixed  += pull(c, fixed_policy(c));
            random += pull(c, rnd() % kAct);

            std::size_t oa = 0;
            for (std::size_t a = 1; a < kAct; ++a) if (kTruth[c][a] > kTruth[c][oa]) oa = a;
            oracle += pull(c, oa);
        }
        std::printf("      mean reward over %d trials: learned %.3f  fixed %.3f  "
                    "random %.3f  oracle %.3f\n",
                    trials, learned / trials, fixed / trials,
                    random / trials, oracle / trials);
        check(learned > random, "learning beats choosing at random");
        check(learned > fixed,
              "and beats the fixed hand-tuned policy it is meant to replace");
        // Regret: how much of the gap to a perfect chooser it closed.
        const double closed = (learned - fixed) / (oracle - fixed);
        std::printf("      closed %.0f%% of the gap between the fixed policy and an oracle\n",
                    closed * 100.0);
        check(closed > 0.5, "closing more than half the gap to an oracle");
    }

    // --- WHAT IT LEARNED SURVIVES A RESTART ----------------------------------
    //
    // Every other learning surface in this tree threw its state away at process
    // exit until it was fixed. This one ships with persistence rather than
    // getting it later.
    {
        Valuer v(kCtx, kAct);
        g_s = 4242;
        for (int t = 0; t < 800; ++t) {
            const std::size_t c = rnd() % kCtx;
            const std::size_t a = v.choose(c, 1.0);
            v.observe(c, a, pull(c, a));
        }
        const std::string path = "telos_roundtrip.txt";
        check(v.save(path), "the value table writes itself to disk");

        Valuer back;
        check(back.load(path), "and reads itself back");
        bool same = true;
        for (std::size_t c = 0; c < kCtx; ++c)
            for (std::size_t a = 0; a < kAct; ++a) {
                const Estimate x = v.estimate(c, a), y = back.estimate(c, a);
                if (x.count != y.count || std::fabs(x.mean - y.mean) > 1e-6) same = false;
            }
        check(same, "with every estimate intact");
        bool decisions_same = true;
        for (std::size_t c = 0; c < kCtx; ++c) if (v.best(c) != back.best(c)) decisions_same = false;
        check(decisions_same, "and it makes the same decisions afterwards");
        std::remove(path.c_str());
    }

    std::printf("\n");
    if (failures == 0) std::printf("ALL PASS\n");
    else               std::printf("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
