// Containment regression gate. Proves the core cage behaviour that every higher
// safety guarantee rests on: a contained command launches and is captured, and a
// runaway is killed by the Job Object on timeout (tree-kill). The full integrity
// tier (low-IL non-admin) is reported for visibility. Explicit returns, not assert,
// so the checks hold in Release (NDEBUG) builds.

#include "khora/bulwark/bulwark.hpp"

#include <iostream>
#include <string>

static int fail(const char* m) { std::cerr << "bulwark_test FAIL: " << m << "\n"; return 1; }

int main() {
    auto echo = khora::bulwark::execute_contained("echo bulwark_unit_ok", 15000);
    if (!echo.ran)                                                    return fail("contained command did not launch");
    if (echo.output.find("bulwark_unit_ok") == std::string::npos)    return fail("contained output not captured");

    auto runaway = khora::bulwark::execute_contained("ping -n 30 127.0.0.1", 1500);
    if (!(runaway.timed_out && runaway.killed_by_job))               return fail("runaway not killed by the job");

    std::string rep;
    const int tier = khora::bulwark::self_check(rep);
    std::cout << "bulwark_test: launch+capture ok; runaway killed; self_check tier="
              << tier << "\n" << rep;
    if (tier < 1)                                                    return fail("containment tier 0 (cage not applied)");

    std::cout << "bulwark_test passed\n";
    return 0;
}
