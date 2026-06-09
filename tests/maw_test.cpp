// Maw unit test: generation is never empty, recording dedups, distinct charting
// counts correctly. (The contained execution itself is exercised by BulwarkTest;
// here we test the drive's bookkeeping.) Explicit returns, Release-safe.

#include "khora/maw/maw.hpp"

#include <iostream>

static int fail(const char* m) { std::cerr << "maw_test FAIL: " << m << "\n"; return 1; }

int main() {
    khora::maw::Maw maw;
    maw.seed();
    if (maw.stats().verbs == 0) return fail("seed produced no verbs");

    for (int i = 0; i < 100; ++i)
        if (maw.generate().empty()) return fail("generate produced an empty command");

    const bool n1 = maw.record("dir C:\\", 0, false, "Volume in drive C is OS");
    const bool n2 = maw.record("dir C:\\", 0, false, "Volume in drive C is OS");
    if (!n1) return fail("first record should be novel");
    if (n2)  return fail("duplicate command counted as novel");
    if (maw.stats().distinct != 1) return fail("distinct count after one unique != 1");

    if (!maw.record("whoami /groups", 0, false, "Mandatory Label Low"))
        return fail("a different command should be novel");
    if (maw.stats().distinct != 2) return fail("distinct count after two unique != 2");

    std::cout << "maw_test passed (verbs=" << maw.stats().verbs
              << ", coverage=" << maw.coverage() << ")\n";
    return 0;
}
