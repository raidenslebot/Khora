// PROVENANCE — can Khora name the memories that produced an answer, such that
// deleting exactly those changes it?
//
// Neither tissue nor a language model can do this.
//
// A brain has no introspective access to which memories produced a thought; the
// report it gives you is itself a construction, and the confabulation
// literature is what happens when you take that report at face value.
//
// A language model's chain-of-thought is not causally load-bearing. It is
// generated alongside the answer rather than consulted to produce it, so
// deleting the source it cites does not change what it says. The citation is a
// plausible story about the answer, not the answer's cause.
//
// Here the citation IS the cause. A cell is primed because specific segments
// crossed threshold; each segment was grown during one specific episode; the
// tag was written at that moment and read back unchanged. The claim is
// therefore falsifiable in the strongest form available:
//
//     forget exactly the cited episodes  -> the answer MUST change
//     forget the same number of others   -> the answer MUST NOT change
//
// A system that passes only the first half has a citation that is merely
// correlated. Both halves together are causation.

#include "khora/cortex/temporal_memory.hpp"
#include "khora/lattice/sdr.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace khora::cortex;
using khora::lattice::Sdr;
using khora::lattice::bind;
using khora::lattice::permute;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("  FAIL: %s\n", what); ++failures; }
    else         std::printf("  ok  : %s\n", what);
}

struct Fact { int s, r, o; };

Sdr atom(const char* k, int i) { return Sdr::from_hash(std::string(k) + std::to_string(i)); }
Sdr key_of(const Fact& f) { return bind(permute(atom("subj", f.s), 1), atom("rel", f.r)); }

// How strongly the object of `f` is predicted after its key is presented.
double confidence(TemporalMemory& tm, const Fact& f) {
    tm.reset();
    tm.compute(key_of(f), false);
    const Sdr want = atom("obj", f.o);
    std::size_t hit = 0;
    for (std::size_t b = 0; b < khora::lattice::kSdrBlocks; ++b)
        if (tm.predicted_columns().contains(b, want.index(b))) ++hit;
    return static_cast<double>(hit) / khora::lattice::kSdrBlocks;
}

std::vector<Fact> make_corpus(int n) {
    std::vector<Fact> v;
    for (int i = 0; i < n; ++i) v.push_back({i, i % 8, (i * 7 + 3) % 64});
    return v;
}

void teach(TemporalMemory& tm, const std::vector<Fact>& facts) {
    for (std::size_t i = 0; i < facts.size(); ++i) {
        tm.reset();
        tm.compute(key_of(facts[i]), true, static_cast<std::uint32_t>(i));
        tm.compute(atom("obj", facts[i].o), true, static_cast<std::uint32_t>(i));
    }
}

} // namespace

int main() {
    std::printf("Provenance test\n");

    constexpr int kFacts = 200;
    const std::vector<Fact> facts = make_corpus(kFacts);
    const std::size_t target = 42;

    // --- the citation is small and correct ----------------------------------
    {
        TemporalMemory tm(TemporalMemoryConfig::episodic());
        teach(tm, facts);

        const double before = confidence(tm, facts[target]);
        tm.reset();
        tm.compute(key_of(facts[target]), false);
        const auto cited = tm.explain();

        std::printf("  fact %zu: predicted at %.2f, cites %zu episode(s):",
                    target, before, cited.size());
        for (std::size_t i = 0; i < cited.size() && i < 8; ++i)
            std::printf(" %u", cited[i]);
        std::printf("\n");

        check(before > 0.9, "the fact is confidently predicted before anything is removed");
        check(!cited.empty(), "the prediction cites at least one episode");
        check(cited.size() <= 5, "the citation is small (5 or fewer episodes)");
        bool names_itself = false;
        for (const std::uint32_t c : cited) if (c == target) names_itself = true;
        check(names_itself, "and it names the episode that actually taught this fact");
    }

    // --- deleting the cited episodes CHANGES the answer ---------------------
    {
        TemporalMemory tm(TemporalMemoryConfig::episodic());
        teach(tm, facts);
        const double before = confidence(tm, facts[target]);

        tm.reset();
        tm.compute(key_of(facts[target]), false);
        const auto cited = tm.explain();

        std::size_t removed = 0;
        for (const std::uint32_t c : cited) removed += tm.forget(c);
        const double after = confidence(tm, facts[target]);
        std::printf("  forgetting the %zu cited episode(s) (%zu segments):"
                    " %.2f -> %.2f\n", cited.size(), removed, before, after);

        check(removed > 0, "forgetting the cited episodes removes stored structure");
        check(after < 0.1, "and the prediction collapses");
    }

    // --- deleting the SAME NUMBER of others does NOT --------------------------
    //
    // Without this half, a citation could be merely correlated with the answer.
    {
        TemporalMemory tm(TemporalMemoryConfig::episodic());
        teach(tm, facts);
        const double before = confidence(tm, facts[target]);

        tm.reset();
        tm.compute(key_of(facts[target]), false);
        const auto cited = tm.explain();

        // Same count, deliberately chosen from elsewhere in the corpus.
        std::size_t removed = 0, n = 0;
        for (std::uint32_t i = 0; n < cited.size() && i < kFacts; ++i) {
            bool is_cited = false;
            for (const std::uint32_t c : cited) if (c == i) is_cited = true;
            if (is_cited) continue;
            removed += tm.forget(i);
            ++n;
        }
        const double after = confidence(tm, facts[target]);
        std::printf("  forgetting %zu UNcited episode(s) (%zu segments):"
                    " %.2f -> %.2f\n", n, removed, before, after);

        check(removed > 0, "the control actually removed a comparable amount of structure");
        check(after == before, "and the prediction is completely unaffected");
    }

    // --- and it is selective across the whole corpus -------------------------
    //
    // One episode forgotten must take exactly its own fact with it and leave
    // every other fact standing. That is the property that makes provenance
    // usable rather than anecdotal.
    {
        TemporalMemory tm(TemporalMemoryConfig::episodic());
        teach(tm, facts);

        int intact = 0;
        const int probes = 40;
        for (int i = 0; i < probes; ++i) {
            if (confidence(tm, facts[static_cast<std::size_t>(i) * 3 + 1]) > 0.9) ++intact;
        }
        tm.forget(static_cast<std::uint32_t>(target));

        int still_intact = 0;
        for (int i = 0; i < probes; ++i) {
            if (confidence(tm, facts[static_cast<std::size_t>(i) * 3 + 1]) > 0.9) ++still_intact;
        }
        const double gone = confidence(tm, facts[target]);
        std::printf("  after forgetting one episode: that fact %.2f,"
                    " %d/%d other facts still intact (was %d)\n",
                    gone, still_intact, probes, intact);
        check(gone < 0.1, "the forgotten fact is gone");
        check(still_intact == intact, "and every other fact is untouched");
    }

    std::printf("\n");
    if (failures == 0) std::printf("ALL PASS\n");
    else               std::printf("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
