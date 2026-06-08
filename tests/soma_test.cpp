// Tests for the Soma Nexus.

#include "khora/soma/soma_nexus.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

namespace {
int g_total = 0;
int g_failed = 0;
}

#define EXPECT(cond, msg) do { \
    ++g_total; \
    if (!(cond)) { ++g_failed; std::fprintf(stderr, "FAIL: %s  (%s:%d)\n", (msg), __FILE__, __LINE__); } \
} while (0)

#define EXPECT_NEAR(a, b, tol, msg) do { \
    ++g_total; \
    const double _a = (a); const double _b = (b); const double _t = (tol); \
    if (std::fabs(_a - _b) > _t) { \
        ++g_failed; \
        std::fprintf(stderr, "FAIL: %s  expected %.4f got %.4f  (%s:%d)\n", \
                     (msg), _b, _a, __FILE__, __LINE__); \
    } \
} while (0)

using namespace std::chrono_literals;
using namespace khora::soma;

int main() {

    // 1. Default state: strengths equal setpoints (the personality).
    {
        SomaNexus s;
        EXPECT_NEAR(s.strength(Drive::Curiosity),        0.60, 1e-9, "default Curiosity");
        EXPECT_NEAR(s.strength(Drive::Preservation),     0.70, 1e-9, "default Preservation");
        EXPECT_NEAR(s.strength(Drive::Mastery),          0.50, 1e-9, "default Mastery");
        EXPECT_NEAR(s.strength(Drive::Efficiency),       0.40, 1e-9, "default Efficiency");
        EXPECT_NEAR(s.strength(Drive::OperatorAffinity), 0.80, 1e-9, "default OperatorAffinity");
    }

    // 2. stimulate adjusts strength and clamps to [0, 1].
    {
        SomaNexus s;
        s.stimulate(Drive::Curiosity, +0.5);
        EXPECT_NEAR(s.strength(Drive::Curiosity), 1.0, 1e-9, "stimulate clamps to 1.0");
        s.stimulate(Drive::Curiosity, -5.0);
        EXPECT_NEAR(s.strength(Drive::Curiosity), 0.0, 1e-9, "stimulate clamps to 0.0");
    }

    // 3. tick decays toward setpoint exponentially.
    {
        SomaNexus s;
        s.stimulate(Drive::Curiosity, +0.4);   // 0.60 -> 1.00 (clamped)
        const double before = s.strength(Drive::Curiosity);
        EXPECT_NEAR(before, 1.0, 1e-9, "Curiosity is 1.0 before tick");

        s.tick(500ms);
        const double after = s.strength(Drive::Curiosity);
        EXPECT(after < before, "tick reduced Curiosity (decay toward setpoint 0.6)");
        EXPECT(after > 0.6,    "tick did not overshoot setpoint");

        // Many ticks should converge near setpoint.
        for (int i = 0; i < 30; ++i) s.tick(500ms);
        EXPECT_NEAR(s.strength(Drive::Curiosity), 0.6, 0.05, "many ticks converge to setpoint");
    }

    // 4. evaluate computes weighted sum across drives.
    {
        SomaNexus s;
        // Force a known state.
        s.reset_all();   // all at setpoints
        Affinity a{};
        a.per_drive[static_cast<std::size_t>(Drive::Curiosity)]        = 1.0;
        a.per_drive[static_cast<std::size_t>(Drive::Preservation)]     = 0.0;
        a.per_drive[static_cast<std::size_t>(Drive::Mastery)]          = 0.0;
        a.per_drive[static_cast<std::size_t>(Drive::Efficiency)]       = 0.0;
        a.per_drive[static_cast<std::size_t>(Drive::OperatorAffinity)] = 0.0;
        // Valence = strength(Curiosity) * 1 = 0.6
        EXPECT_NEAR(s.evaluate(a), 0.6, 1e-9, "evaluate picks Curiosity * 1.0");
    }

    // 5. choose_best returns the action with highest weighted valence.
    {
        SomaNexus s;
        s.reset_all();
        Affinity explore{};   explore.per_drive[(int)Drive::Curiosity]        = 1.0;
        Affinity serve{};     serve.per_drive[(int)Drive::OperatorAffinity]   = 1.0;
        Affinity conserve{};  conserve.per_drive[(int)Drive::Efficiency]      = 1.0;

        std::vector<Affinity> options = {explore, serve, conserve};
        auto [idx, val] = s.choose_best(std::span<const Affinity>{options.data(), options.size()});
        // Defaults: Curiosity=0.6, OperatorAffinity=0.8, Efficiency=0.4 → serve wins.
        EXPECT(idx == 1, "default personality picks 'serve' (OperatorAffinity wins)");
        EXPECT_NEAR(val, 0.8, 1e-9, "winning valence equals OperatorAffinity strength");
    }

    // 6. Stimulating a drive can change which action wins.
    {
        SomaNexus s;
        s.reset_all();
        s.stimulate(Drive::Curiosity, +0.5);  // Curiosity -> 1.0 (clamped)

        Affinity explore{};   explore.per_drive[(int)Drive::Curiosity]        = 1.0;
        Affinity serve{};     serve.per_drive[(int)Drive::OperatorAffinity]   = 1.0;

        std::vector<Affinity> options = {explore, serve};
        auto [idx, val] = s.choose_best(std::span<const Affinity>{options.data(), options.size()});
        EXPECT(idx == 0, "after curiosity spike, 'explore' wins over 'serve'");
        EXPECT_NEAR(val, 1.0, 1e-9, "winning valence is Curiosity=1.0");
    }

    // 7. Thread-safety: many stimulators don't crash and bounds hold.
    {
        SomaNexus s;
        constexpr int N_THREADS = 8;
        constexpr int N_OPS     = 1000;
        std::vector<std::thread> threads;
        for (int t = 0; t < N_THREADS; ++t) {
            threads.emplace_back([&s, t]{
                for (int i = 0; i < N_OPS; ++i) {
                    const Drive d = static_cast<Drive>((t * 7 + i) % kDriveCount);
                    s.stimulate(d, (i % 2 == 0) ? 0.01 : -0.01);
                    if (i % 50 == 0) s.tick(10ms);
                }
            });
        }
        for (auto& th : threads) th.join();
        bool in_range = true;
        for (auto v : s.snapshot()) {
            if (v < 0.0 || v > 1.0) { in_range = false; break; }
        }
        EXPECT(in_range, "all drives stay in [0, 1] under concurrent stimulation");
    }

    std::printf("\nSoma tests: %d/%d passed (%d failed).\n",
                g_total - g_failed, g_total, g_failed);
    return g_failed == 0 ? 0 : 1;
}
