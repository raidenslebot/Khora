// bulwark_probe — proves Khora's containment cage in ISOLATION, before any
// autonomous driver is allowed to use it. Runs the Bulwark's self_check canaries
// (a contained command launches; a write to C:\Windows is denied at full tier; a
// runaway is killed by the job on timeout) and prints the achieved tier. Exit code
// is the tier, so CI/scripts can gate on it.

#include "khora/bulwark/bulwark.hpp"

#include <iostream>
#include <string>

int main() {
    std::string report;
    const int tier = khora::bulwark::self_check(report);
    std::cout << "Khora Bulwark self-check:\n" << report
              << "achieved containment tier: " << tier
              << (tier >= 2 ? "  (FULL — autonomous exploration may be permitted)\n"
                            : tier == 1 ? "  (resource cage only — integrity NOT proven)\n"
                                        : "  (FAILED — autonomous exploration WITHHELD)\n");
    return tier;
}
