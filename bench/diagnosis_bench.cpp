// WAS THE DIAGNOSIS RIGHT, OR WAS THE TEST UNDERPOWERED?
//
// Four string tasks failed, and I claimed each had one specific missing
// capability behind it: set membership, a scan that stops at a delimiter, a
// conditional on a value range, and comparison between neighbours. Gt, Member,
// Until and Delta were added to test exactly that.
//
// The re-run still failed all four -- but that run cannot support the
// conclusion, and noticing why matters more than the result. The tasks there
// share a growing library, so by the fourteenth task level 0 holds around 28
// entries and a single binary level is ~28 x 28 x 18 candidates. That exhausts a
// 12,000 pool before depth 2 is reached. `first_word` should be
// until(x, mul(4, 8)) -- the same shape as count_spaces, which succeeded.
//
// A test that cannot distinguish "the capability is missing" from "the budget
// ran out" answers neither question. This one isolates the four tasks: no shared
// library, a large pool, and both engines.

#include "khora/techne/techne.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace khora::techne;
using clk = std::chrono::high_resolution_clock;

namespace {

std::uint64_t rs = 0xD1A6ULL;
std::uint64_t rnd() {
    std::uint64_t z = (rs += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

Value encode(const std::string& s) {
    Value v;
    for (const char c : s) v.push_back(static_cast<std::int64_t>(static_cast<unsigned char>(c)));
    return v;
}

struct Task {
    const char* name;
    const char* capability;                 // the one I claimed was missing
    std::function<Value(const Value&)> ref;
};

std::vector<Task> suite() {
    return {
        {"count_vowels", "Member: set membership",
         [](const Value& v) { std::int64_t n = 0;
             for (auto c : v) if (c=='a'||c=='e'||c=='i'||c=='o'||c=='u') ++n;
             return Value{n}; }},
        {"first_word", "Until: a scan that stops at a delimiter",
         [](const Value& v) { Value o; for (auto c : v) { if (c == 32) break; o.push_back(c); } return o; }},
        {"title_case", "Gt: a conditional on a value range",
         [](const Value& v) { Value o = v;
             if (!o.empty() && o[0] >= 'a' && o[0] <= 'z') o[0] -= 32;
             return o; }},
        {"dedup_adjacent", "Delta: comparison between neighbours",
         [](const Value& v) { Value o; for (auto c : v) if (o.empty() || o.back() != c) o.push_back(c);
             return o; }},
        // A control: this one is known reachable and at the SAME depth as
        // first_word. If the control passes and first_word does not, the budget
        // is not the explanation.
        {"count_spaces", "control -- known reachable at depth 2",
         [](const Value& v) { std::int64_t n = 0; for (auto c : v) if (c == 32) ++n; return Value{n}; }},
    };
}

Spec make(const Task& t) {
    Spec s;
    s.name = t.name;
    static const char* words[] = {
        "hello world", "the quick", "abc def", "one two three", "x y",
        "aeiou here", "no vowels rhythm", "sooner", "aabbcc", "mississippi",
        "a", "", "zz top", "keep going", "last one here", "double  space",
    };
    for (std::size_t i = 0; i < 14; ++i) {
        const Value in = encode(words[i % 16]);
        s.cases.push_back({in, t.ref(in)});
    }
    for (std::size_t i = 0; i < 6; ++i) {
        const Value in = encode(std::string(words[(i + 3) % 16]) + " tail");
        s.holdout.push_back({in, t.ref(in)});
    }
    return s;
}

} // namespace

int main(int argc, char** argv) {
    const std::size_t cap = (argc > 1) ? std::stoul(argv[1]) : 400000;

    std::printf("Was the diagnosis right, or was the test underpowered?\n\n");
    std::printf("  Four tasks, ISOLATED: no shared library, pool %zu, both engines.\n", cap);
    std::printf("  Plus a control at the same depth as first_word, so a failure can be\n");
    std::printf("  attributed to capability rather than to budget.\n\n");

    std::printf("  task           | claimed missing capability          | forward | bidir\n");
    std::printf("  ---------------+-------------------------------------+---------+-------\n");

    std::size_t any = 0;
    for (const Task& t : suite()) {
        const Spec s = make(t);

        const auto t0 = clk::now();
        const BuildResult f = construct(s, cap, nullptr);
        const double fs = std::chrono::duration<double>(clk::now() - t0).count();

        const auto t1 = clk::now();
        const BuildResult b = construct_bidir(s, cap, nullptr);
        const double bs = std::chrono::duration<double>(clk::now() - t1).count();

        const bool fok = f.proof == Proof::Generalised;
        const bool bok = b.proof == Proof::Generalised;
        if (fok || bok) ++any;

        std::printf("  %-14s | %-35s | %-7s | %s\n", t.name, t.capability,
                    fok ? "solved" : "no", bok ? "solved" : "no");
        if (fok) std::printf("      forward: %s   (%zu nodes, %.1f s)\n",
                             f.recipe.render().c_str(), f.nodes_considered, fs);
        if (bok && !fok) std::printf("      bidir:   %s   (%zu nodes, %.1f s)\n",
                                     b.recipe.render().c_str(), b.nodes_considered, bs);

        // Independent probes for anything that claims success.
        const BuildResult& win = fok ? f : b;
        if (fok || bok) {
            std::size_t agree = 0;
            for (std::size_t k = 0; k < 300; ++k) {
                std::string str;
                const std::size_t len = rnd() % 16;
                for (std::size_t j = 0; j < len; ++j) {
                    const int pick = static_cast<int>(rnd() % 30);
                    str += (pick < 26) ? static_cast<char>('a' + pick) : ' ';
                }
                const Value in = encode(str);
                if (win.recipe.apply(in, nullptr) == t.ref(in)) ++agree;
            }
            std::printf("      probes:  %zu/300 %s\n", agree,
                        agree == 300 ? "-- correct" : "-- WRONG, certified but not right");
        }
    }

    std::printf("\n  VERDICT\n");
    if (any == 0) {
        std::printf("    None solved even isolated with a %zu pool and both engines.\n", cap);
        std::printf("    The four operations were necessary and are not sufficient: the\n");
        std::printf("    diagnosis named real gaps but not the whole gap. What remains is\n");
        std::printf("    that each of these needs a CONSTANT the machine cannot name (32,\n");
        std::printf("    97, 117) built from arithmetic, on top of the new operation -- so\n");
        std::printf("    the true depth is higher than the capability alone suggests.\n");
    } else {
        std::printf("    %zu solved once isolated. The earlier failure was the shared\n", any);
        std::printf("    library exhausting the pool before depth 2, not a missing\n");
        std::printf("    capability -- the test was underpowered and the diagnosis stands\n");
        std::printf("    for those that now pass.\n");
    }
    return 0;
}
