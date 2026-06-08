// Tests for the Reverie Loom.

#include "khora/reverie/reverie_loom.hpp"
#include "khora/reverie/reverie_scheduler.hpp"

#include <chrono>
#include <cstdio>
#include <shared_mutex>
#include <string>
#include <thread>

namespace {
int g_total = 0;
int g_failed = 0;
}

#define EXPECT(cond, msg) do { \
    ++g_total; \
    if (!(cond)) { ++g_failed; std::fprintf(stderr, "FAIL: %s  (%s:%d)\n", (msg), __FILE__, __LINE__); } \
} while (0)

using khora::lattice::Glyph;
using khora::lattice::Lattice;
using khora::cortex::PredictiveColumn;
using khora::soma::SomaNexus;
using khora::reverie::ReverieLoom;

int main() {
    // 1. Empty memory yields no dreams.
    {
        Lattice mem;
        PredictiveColumn cortex(2);
        SomaNexus soma;
        ReverieLoom loom(mem, cortex, soma);

        const auto s = loom.dream_once();
        EXPECT(!s.retained, "dream from empty memory is not retained");
        EXPECT(loom.dreams().size() == 0, "dream lattice still empty");
        EXPECT(loom.cycles() == 1, "cycle counter advanced");
    }

    // 2. Populated memory + low threshold → most dreams retained.
    {
        Lattice mem;
        for (int i = 0; i < 100; ++i) {
            mem.store("m" + std::to_string(i), Glyph::random(0x1000 + i));
        }
        PredictiveColumn cortex(2);
        SomaNexus soma;
        ReverieLoom loom(mem, cortex, soma);
        loom.set_satisfaction_threshold(0.1);  // permissive

        const std::size_t retained = loom.dream_n(200);
        EXPECT(retained > 100, "permissive threshold retains majority of 200 dreams");
        EXPECT(loom.cycles() == 200, "cycle count matches dream_n input");
        EXPECT(loom.retained() == retained, "retained() matches dream_n return");
    }

    // 3. Very high threshold → no dreams retained.
    {
        Lattice mem;
        for (int i = 0; i < 50; ++i) {
            mem.store("m" + std::to_string(i), Glyph::random(i + 1));
        }
        PredictiveColumn cortex(2);
        SomaNexus soma;
        ReverieLoom loom(mem, cortex, soma);
        loom.set_satisfaction_threshold(10.0);  // unattainable

        const std::size_t retained = loom.dream_n(100);
        EXPECT(retained == 0, "unattainable threshold retains nothing");
    }

    // 4. Retained dreams are new — not bit-equal to any original memory.
    {
        Lattice mem;
        for (int i = 0; i < 30; ++i) {
            mem.store("m" + std::to_string(i), Glyph::random(0xA000 + i));
        }
        PredictiveColumn cortex(2);
        SomaNexus soma;
        ReverieLoom loom(mem, cortex, soma);
        loom.set_satisfaction_threshold(0.1);

        loom.dream_n(50);
        bool any_duplicate = false;
        for (const auto& [_dl, dream_glyph] : loom.dreams()) {
            for (const auto& [_ml, mem_glyph] : mem) {
                if (dream_glyph == mem_glyph) { any_duplicate = true; break; }
            }
            if (any_duplicate) break;
        }
        EXPECT(!any_duplicate, "no dream is bit-equal to any source memory");
    }

    // 5. Determinism: same seed + same memory ⇒ same dreams.
    {
        Lattice mem;
        for (int i = 0; i < 20; ++i) mem.store("m" + std::to_string(i), Glyph::random(i + 1));

        PredictiveColumn cortex_a(2);
        SomaNexus soma_a;
        ReverieLoom loom_a(mem, cortex_a, soma_a, /*seed=*/12345);
        loom_a.set_satisfaction_threshold(0.0);   // retain all so we have full sequences
        loom_a.dream_n(20);

        PredictiveColumn cortex_b(2);
        SomaNexus soma_b;
        ReverieLoom loom_b(mem, cortex_b, soma_b, /*seed=*/12345);
        loom_b.set_satisfaction_threshold(0.0);
        loom_b.dream_n(20);

        EXPECT(loom_a.dreams().size() == loom_b.dreams().size(),
               "same seed yields same number of dreams");
        bool all_match = true;
        for (std::size_t i = 0; i < loom_a.dreams().size(); ++i) {
            const std::string label = "dream_" + std::to_string(i);
            auto ga = loom_a.dreams().recall(label);
            auto gb = loom_b.dreams().recall(label);
            if (!ga.has_value() || !gb.has_value() || !(*ga == *gb)) {
                all_match = false; break;
            }
        }
        EXPECT(all_match, "deterministic dreams under fixed seed");
    }

    // 6. Scheduler: start, run, stop. Cycles advance, no crash.
    //    Windows wait_for has ~15ms timer resolution; pick numbers loose
    //    enough that this isn't flaky.
    {
        Lattice mem;
        for (int i = 0; i < 50; ++i) mem.store("m" + std::to_string(i), Glyph::random(i + 1));
        PredictiveColumn cortex(2);
        SomaNexus soma;
        ReverieLoom loom(mem, cortex, soma);
        loom.set_satisfaction_threshold(0.0);

        std::shared_mutex mu;
        khora::reverie::ReverieScheduler sched(loom, mu);

        sched.start(std::chrono::milliseconds(5));
        EXPECT(sched.is_running(), "scheduler is_running after start");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        sched.stop();
        EXPECT(!sched.is_running(), "scheduler stopped after stop()");
        EXPECT(sched.cycles_run() >= 3, "scheduler ran multiple cycles");
        EXPECT(loom.cycles() == sched.cycles_run(),
               "loom cycle count matches scheduler cycle count");
    }

    // 7. Consolidation: retained dreams are fed back into the cortex,
    //    growing its observation count.
    {
        Lattice mem;
        for (int i = 0; i < 40; ++i) mem.store("m" + std::to_string(i), Glyph::random(i + 1));
        PredictiveColumn cortex(2);
        SomaNexus soma;
        ReverieLoom loom(mem, cortex, soma);
        loom.set_satisfaction_threshold(0.0);
        loom.set_consolidation(true);

        const auto obs_before = cortex.observations();
        loom.dream_n(50);
        const auto obs_after = cortex.observations();

        EXPECT(obs_after > obs_before, "cortex.observations grew under consolidation");
        EXPECT(loom.consolidations() > 0, "loom recorded consolidations");
        EXPECT(loom.consolidations() == (obs_after - obs_before),
               "every consolidation == one cortex.step");
    }

    // 8. Consolidation off (default): cortex unchanged.
    {
        Lattice mem;
        for (int i = 0; i < 40; ++i) mem.store("m" + std::to_string(i), Glyph::random(i + 1));
        PredictiveColumn cortex(2);
        SomaNexus soma;
        ReverieLoom loom(mem, cortex, soma);
        loom.set_satisfaction_threshold(0.0);
        EXPECT(!loom.consolidation(), "consolidation off by default");

        const auto obs_before = cortex.observations();
        loom.dream_n(20);
        EXPECT(cortex.observations() == obs_before, "cortex untouched when consolidation off");
        EXPECT(loom.consolidations() == 0, "consolidations counter stays zero");
    }

    // 9. Scheduler: start / stop are idempotent.
    {
        Lattice mem;
        mem.store("a", Glyph::random(1));
        PredictiveColumn cortex(2);
        SomaNexus soma;
        ReverieLoom loom(mem, cortex, soma);

        std::shared_mutex mu;
        khora::reverie::ReverieScheduler sched(loom, mu);

        sched.start(std::chrono::milliseconds(5));
        sched.start(std::chrono::milliseconds(5));  // second start is a no-op
        EXPECT(sched.is_running(), "double-start does not crash");
        sched.stop();
        sched.stop();  // double-stop is no-op
        EXPECT(!sched.is_running(), "double-stop ends running state");
    }

    std::printf("\nReverie tests: %d/%d passed (%d failed).\n",
                g_total - g_failed, g_total, g_failed);
    return g_failed == 0 ? 0 : 1;
}
