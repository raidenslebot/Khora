// Soma demo — simulate a day in Khora's drive system. Inject events,
// tick time forward, watch drives compete to pick the next action.

#include "khora/soma/soma_nexus.hpp"

#include <chrono>
#include <cstdio>
#include <span>
#include <vector>

using namespace std::chrono_literals;
using namespace khora::soma;

namespace {
void print_snapshot(const SomaNexus& s, const char* label) {
    const auto snap = s.snapshot();
    std::printf("  [%-22s] ", label);
    for (std::size_t i = 0; i < kDriveCount; ++i) {
        std::printf("%-16s=%0.3f  ", drive_name(static_cast<Drive>(i)), snap[i]);
    }
    std::printf("\n");
}

Affinity make_affinity(double cur, double pre, double mas, double eff, double op) {
    Affinity a{};
    a.per_drive[(int)Drive::Curiosity]        = cur;
    a.per_drive[(int)Drive::Preservation]     = pre;
    a.per_drive[(int)Drive::Mastery]          = mas;
    a.per_drive[(int)Drive::Efficiency]       = eff;
    a.per_drive[(int)Drive::OperatorAffinity] = op;
    return a;
}
}

int main() {
    SomaNexus s;
    std::printf("Soma Nexus demo — drive arbitration under simulated stimuli\n\n");
    print_snapshot(s, "startup");

    // Three candidate actions, each with a different drive profile.
    const std::vector<std::pair<const char*, Affinity>> menu = {
        {"explore unknown",    make_affinity(+1.0, -0.3, +0.5, -0.4,  0.0)},
        {"serve operator",     make_affinity( 0.0, +0.1,  0.0, -0.2, +1.0)},
        {"consolidate memory", make_affinity(-0.2, +0.5, +0.7, +0.3,  0.0)},
        {"idle / sleep",       make_affinity(-0.5, +0.3, -0.2, +1.0, -0.4)},
    };
    std::vector<Affinity> options;
    for (const auto& m : menu) options.push_back(m.second);

    auto print_choice = [&](const char* label) {
        auto [idx, val] = s.choose_best(std::span<const Affinity>{options.data(), options.size()});
        std::printf("  [%-22s] chooses: \"%s\"  (valence = %+.3f)\n",
                    label, menu[idx].first, val);
    };

    // Event 1: novel input arrives — curiosity and mastery spike.
    s.stimulate(Drive::Curiosity, +0.4);
    s.stimulate(Drive::Mastery,   +0.2);
    print_snapshot(s, "novel input");
    print_choice("novel input");

    // Tick 2 seconds — drives decay toward setpoints.
    for (int i = 0; i < 20; ++i) s.tick(100ms);
    print_snapshot(s, "after 2s idle");
    print_choice("after 2s idle");

    // Event 2: operator gives a direct command.
    s.stimulate(Drive::OperatorAffinity, +0.2);  // already high; clamps near 1.0
    print_snapshot(s, "operator command");
    print_choice("operator command");

    // Event 3: high resource pressure — preservation up, efficiency up, curiosity down.
    s.stimulate(Drive::Preservation, +0.3);
    s.stimulate(Drive::Efficiency,   +0.5);
    s.stimulate(Drive::Curiosity,    -0.4);
    print_snapshot(s, "resource pressure");
    print_choice("resource pressure");

    // Event 4: extended idle — everything returns toward setpoints.
    for (int i = 0; i < 50; ++i) s.tick(100ms);
    print_snapshot(s, "after 5s idle");
    print_choice("after 5s idle");

    std::printf("\nDone — drive arbitration shifted the chosen action across contexts.\n");
    return 0;
}
