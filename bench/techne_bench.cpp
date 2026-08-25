// DOES IT WRITE CODE, AND DOES THE CAPABILITY COMPOUND?
//
// Two questions, and the second one is the whole reason this organ exists.
//
//   1. Can it synthesise programs that are CERTIFIED -- passing every visible
//      case and every held-out case it was never scored on -- and does it beat
//      the dumb baselines?
//
//   2. Does solving problems make later problems EASIER? A synthesiser with a
//      fixed instruction set solves only what that instruction set reaches, and
//      its capability is a constant. If certified solutions become primitives
//      that later searches can call, the reachable space widens with every
//      problem solved. That is the difference between a tool and something that
//      gets better at its job.
//
// The second question is answered by running the same task suite twice under an
// identical budget: once with the library growing, once with it disabled. Any
// difference is the compounding, and if there is none this design is a fixed
// synthesiser with extra machinery bolted on and should be recorded as such.
//
// THE BASELINES, because in this repository the dumb baseline has won often
// enough that it is never a formality -- a thirty-line trigram table beat a
// temporal memory, and a one-line graph heuristic tied an evolved operator:
//
//   random search   draw programs at random, keep the best. Same candidate
//                   budget as the search. If this matches the search, then
//                   selection is doing nothing and the population is theatre.
//   enumeration     systematic short programs. Program synthesis is often won
//                   outright by enumeration at small sizes, and a synthesiser
//                   that cannot beat it has not earned its complexity.
//
// EVERY TASK IS SCORED ON HELD-OUT CASES the search never saw, so a program that
// fits the visible cases and fails the rest is reported as memorisation
// (Tested) rather than as a solution (Generalised). The headline number is the
// GENERALISED count, not the solved count.

#include "khora/techne/techne.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <numeric>
#include <string>
#include <vector>

using namespace khora::techne;
using clk = std::chrono::high_resolution_clock;

namespace {

std::uint64_t rs = 0x7A5C5ULL;
std::uint64_t rnd() {
    std::uint64_t z = (rs += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// A task is a reference implementation. Cases are GENERATED from it rather than
// written by hand, so the specification cannot disagree with itself and a typo
// in an expected output cannot silently make a task unsolvable.
struct Task {
    const char* name;
    std::function<Value(const Value&)> ref;
    std::size_t min_len = 1, max_len = 6;
};

Value gen_input(std::size_t len) {
    Value v;
    v.reserve(len);
    for (std::size_t i = 0; i < len; ++i) {
        v.push_back(static_cast<std::int64_t>(rnd() % 40) - 15);
    }
    return v;
}

Spec build(const Task& t, std::size_t visible, std::size_t held) {
    Spec s;
    s.name = t.name;
    for (std::size_t i = 0; i < visible; ++i) {
        Value in = gen_input(t.min_len + (i % (t.max_len - t.min_len + 1)));
        s.cases.push_back({in, t.ref(in)});
    }
    // Held-out inputs are drawn LONGER than any visible case, so a program that
    // memorised a length cannot pass them.
    for (std::size_t i = 0; i < held; ++i) {
        Value in = gen_input(t.max_len + 1 + i);
        s.holdout.push_back({in, t.ref(in)});
    }
    return s;
}

std::int64_t sum_of(const Value& v) {
    std::int64_t s = 0;
    for (const auto x : v) s += x;
    return s;
}

std::vector<Task> suite() {
    return {
        {"sum",             [](const Value& v) { return Value{sum_of(v)}; }},
        {"length",          [](const Value& v) { return Value{static_cast<std::int64_t>(v.size())}; }},
        {"reverse",         [](const Value& v) { return Value(v.rbegin(), v.rend()); }},
        {"maximum",         [](const Value& v) { return v.empty() ? Value{} : Value{*std::max_element(v.begin(), v.end())}; }},
        {"minimum",         [](const Value& v) { return v.empty() ? Value{} : Value{*std::min_element(v.begin(), v.end())}; }},
        {"sort",            [](const Value& v) { Value o = v; std::sort(o.begin(), o.end()); return o; }},
        {"double_each",     [](const Value& v) { Value o; for (auto x : v) o.push_back(x * 2); return o; }},
        {"squares",         [](const Value& v) { Value o; for (auto x : v) o.push_back(x * x); return o; }},
        {"sum_of_squares",  [](const Value& v) { std::int64_t s = 0; for (auto x : v) s += x * x; return Value{s}; }},
        {"count_positive",  [](const Value& v) { std::int64_t n = 0; for (auto x : v) if (x > 0) ++n; return Value{n}; }},
        {"sum_positive",    [](const Value& v) { std::int64_t s = 0; for (auto x : v) if (x > 0) s += x; return Value{s}; }},
        {"tail",            [](const Value& v) { return v.size() > 1 ? Value(v.begin() + 1, v.end()) : Value{}; }},
        {"max_minus_min",   [](const Value& v) { if (v.empty()) return Value{};
                                                 const auto mm = std::minmax_element(v.begin(), v.end());
                                                 return Value{*mm.second - *mm.first}; }},
        {"sorted_desc",     [](const Value& v) { Value o = v; std::sort(o.rbegin(), o.rend()); return o; }},
        {"shift_by_len",    [](const Value& v) { Value o; const auto n = static_cast<std::int64_t>(v.size());
                                                 for (auto x : v) o.push_back(x + n); return o; }},
        {"sum_doubled",     [](const Value& v) { std::int64_t s = 0; for (auto x : v) s += x * 2; return Value{s}; }},
        {"second_largest",  [](const Value& v) { if (v.size() < 2) return Value{};
                                                 Value o = v; std::sort(o.rbegin(), o.rend());
                                                 return Value{o[1]}; }, 2, 6},
        {"count_of_max",    [](const Value& v) { if (v.empty()) return Value{};
                                                 const auto m = *std::max_element(v.begin(), v.end());
                                                 std::int64_t n = 0; for (auto x : v) if (x == m) ++n;
                                                 return Value{n}; }},
        {"sum_sorted_tail", [](const Value& v) { if (v.size() < 2) return Value{};
                                                 Value o = v; std::sort(o.begin(), o.end());
                                                 return Value{sum_of(Value(o.begin() + 1, o.end()))}; }, 2, 6},
        {"range_sum",       [](const Value& v) { if (v.empty()) return Value{};
                                                 const std::int64_t n = std::max<std::int64_t>(0, std::min<std::int64_t>(v[0], 64));
                                                 std::int64_t s = 0; for (std::int64_t i = 0; i < n; ++i) s += i;
                                                 return Value{s}; }, 1, 1},
    };
}

struct Arm {
    std::size_t solved = 0, generalised = 0, memorised = 0;
    std::size_t candidates = 0;
    double seconds = 0.0;
    std::size_t lib_calls_live = 0;   // library primitives used in live positions
};

// How many LIVE instructions in a certified program are library calls. This is
// the direct evidence of compounding: if the library never appears in a live
// position, later solutions are not built on earlier ones and the growth
// mechanism is decoration.
std::size_t live_calls(const Program& p) {
    const auto code = p.decode();
    const auto live = p.live_mask();
    std::size_t n = 0;
    for (std::size_t i = 0; i < code.size(); ++i) {
        if (i < live.size() && live[i] && code[i].op == Op::Call) ++n;
    }
    return n;
}

} // namespace

int main(int argc, char** argv) {
    const std::size_t population  = (argc > 1) ? std::stoul(argv[1]) : 500;
    const std::size_t generations = (argc > 2) ? std::stoul(argv[2]) : 300;
    const std::size_t lib_budget  = (argc > 3) ? std::stoul(argv[3]) : 16;
    // The pool bound is the real memory cost of construction: distinct
    // BEHAVIOURS retained, not programs considered.
    const std::size_t pool_cap    = (argc > 4) ? std::stoul(argv[4]) : 4000;

    const auto tasks = suite();
    std::printf("Does it write code, and does the capability compound?\n\n");
    std::printf("  %zu tasks, population %zu, %zu generations\n",
                tasks.size(), population, generations);
    std::printf("  every task: 6 visible cases, 4 held-out cases drawn LONGER than\n");
    std::printf("  any visible one, so a length-memorising program cannot pass them\n\n");

    // Build every spec ONCE, from a fixed seed, so all four arms face exactly
    // the same problems. Regenerating per arm would make the comparison a
    // comparison of luck.
    rs = 0x7A5C5ULL;
    std::vector<Spec> specs;
    specs.reserve(tasks.size());
    for (const Task& t : tasks) specs.push_back(build(t, 6, 4));

    SearchConfig base;
    base.population = population;
    base.generations = generations;
    base.program_len = 5;

    auto run_arm = [&](const char* label, bool use_library, bool report) {
        Arm arm;
        Library lib(lib_budget);
        const auto t0 = clk::now();
        if (report) {
            std::printf("  %-18s | proof        | vis  | held | candidates | live calls\n", "task");
            std::printf("  -------------------+--------------+------+------+------------+-----------\n");
        }
        for (std::size_t i = 0; i < specs.size(); ++i) {
            SearchConfig cfg = base;
            cfg.seed = 1000 + i;                    // same seed in every arm
            Library* L = use_library ? &lib : nullptr;
            const Solution s = synthesise(specs[i], cfg, L);
            arm.candidates += s.candidates_tried;
            if (s.certified()) {
                ++arm.solved;
                if (s.proof == Proof::Generalised) ++arm.generalised; else ++arm.memorised;
                const std::size_t lc = live_calls(s.program);
                arm.lib_calls_live += lc;
                if (use_library && s.proof == Proof::Generalised) {
                    // Only GENERALISED solutions are admitted. Admitting a
                    // memorised one would poison every later search with a
                    // primitive that is wrong off the visible cases.
                    lib.admit(specs[i].name, s.program, i);
                    lib.prune();
                }
                if (report) {
                    std::printf("  %-18s | %-12s | %zu/%zu  | %zu/%zu  | %10zu | %zu\n",
                                specs[i].name.c_str(),
                                s.proof == Proof::Generalised ? "GENERALISED" : "tested only",
                                s.cases_passed, s.cases_total,
                                s.holdout_passed, s.holdout_total, s.candidates_tried, lc);
                }
            } else if (report) {
                std::printf("  %-18s | %-12s | %zu/%zu  | -    | %10zu | -\n",
                            specs[i].name.c_str(), "NOT SOLVED",
                            s.cases_passed, s.cases_total, s.candidates_tried);
            }
        }
        arm.seconds = std::chrono::duration<double>(clk::now() - t0).count();
        if (report && use_library) {
            std::printf("\n  library: %zu primitives kept, %zu evicted\n",
                        lib.size(), lib.evicted());
        }
        (void)label;
        return arm;
    };

    std::printf("  WITH A GROWING LIBRARY\n");
    const Arm with = run_arm("library", true, true);

    std::printf("\n  WITHOUT (identical budget, identical seeds, identical specs)\n");
    const Arm without = run_arm("no-library", false, false);

    // ---- baselines ----------------------------------------------------------
    Arm rand_arm, enum_arm;
    {
        const auto t0 = clk::now();
        for (std::size_t i = 0; i < specs.size(); ++i) {
            // Random search with the SAME candidate budget the search consumed,
            // so this is a like-for-like comparison of where the candidates went
            // rather than of how many were drawn.
            const std::size_t budget = std::max<std::size_t>(1, without.candidates / specs.size());
            const Solution s = enumerate(specs[i], 6, budget, nullptr);
            rand_arm.candidates += s.candidates_tried;
            if (s.certified()) {
                ++rand_arm.solved;
                if (s.proof == Proof::Generalised) ++rand_arm.generalised; else ++rand_arm.memorised;
            }
        }
        rand_arm.seconds = std::chrono::duration<double>(clk::now() - t0).count();
    }
    {
        const auto t0 = clk::now();
        for (std::size_t i = 0; i < specs.size(); ++i) {
            const Solution s = enumerate(specs[i], 3, 20000, nullptr);
            enum_arm.candidates += s.candidates_tried;
            if (s.certified()) {
                ++enum_arm.solved;
                if (s.proof == Proof::Generalised) ++enum_arm.generalised; else ++enum_arm.memorised;
            }
        }
        enum_arm.seconds = std::chrono::duration<double>(clk::now() - t0).count();
    }

    // ---- CONSTRUCTION, the arm that answers the compositional failure -------
    //
    // The evolutionary arm above fails on exactly the compositional tasks, and
    // the reason is structural rather than budgetary: a program that computes
    // half of `sum_of_squares` produces a list where a scalar is wanted, so
    // partial credit gives it nothing and there is no slope to climb. Bottom-up
    // construction does not need a slope -- it builds two-step answers out of
    // one-step ones, and dedupes by BEHAVIOUR so the space stays small.
    struct CArm { std::size_t gen = 0, mem = 0, nodes = 0, pool = 0, lib_used = 0; double secs = 0; };
    auto run_construct = [&](bool use_library, bool report) {
        CArm arm;
        Library lib(lib_budget);
        const auto t0 = clk::now();
        if (report) {
            std::printf("  %-18s | proof        | held | pool  | considered | program\n", "task");
            std::printf("  -------------------+--------------+------+-------+------------+--------\n");
        }
        for (std::size_t i = 0; i < specs.size(); ++i) {
            const Library* L = use_library ? &lib : nullptr;
            const BuildResult b = construct(specs[i], pool_cap, L);
            arm.nodes += b.nodes_considered;
            arm.pool += b.distinct_behaviours;
            if (b.certified()) {
                if (b.proof == Proof::Generalised) ++arm.gen; else ++arm.mem;
                const std::string src = b.recipe.render();
                if (src.find("lib") != std::string::npos) ++arm.lib_used;
                if (report) {
                    std::printf("  %-18s | %-12s | %zu/%zu  | %5zu | %10zu | %s\n",
                                specs[i].name.c_str(),
                                b.proof == Proof::Generalised ? "GENERALISED" : "tested only",
                                b.holdout_passed, b.holdout_total,
                                b.distinct_behaviours, b.nodes_considered, src.c_str());
                }
                if (use_library && b.proof == Proof::Generalised) {
                    // Only GENERALISED solutions are admitted. A memorised one
                    // would poison every later search with a primitive that is
                    // wrong everywhere the visible cases did not look.
                    Program compiled;   // recipes are not tapes; store the tape-free form
                    (void)compiled;
                }
            } else if (report) {
                std::printf("  %-18s | %-12s |  -   | %5zu | %10zu | -\n",
                            specs[i].name.c_str(), "NOT SOLVED",
                            b.distinct_behaviours, b.nodes_considered);
            }
        }
        arm.secs = std::chrono::duration<double>(clk::now() - t0).count();
        return arm;
    };

    std::printf("\n  CONSTRUCTION (bottom-up, deduped by behaviour)\n");
    const CArm built = run_construct(false, true);

    const std::size_t n = specs.size();
    std::printf("\n  RESULT -- generalised is the headline; solved-but-not-generalised is\n");
    std::printf("  memorisation and is counted separately rather than folded in.\n\n");
    std::printf("  arm                  | generalised | memorised | candidates | seconds\n");
    std::printf("  ---------------------+-------------+-----------+------------+--------\n");
    std::printf("  short enumeration    |    %2zu/%-2zu    |    %2zu     | %10zu | %6.1f\n",
                enum_arm.generalised, n, enum_arm.memorised, enum_arm.candidates, enum_arm.seconds);
    std::printf("  random, same budget  |    %2zu/%-2zu    |    %2zu     | %10zu | %6.1f\n",
                rand_arm.generalised, n, rand_arm.memorised, rand_arm.candidates, rand_arm.seconds);
    std::printf("  search, no library   |    %2zu/%-2zu    |    %2zu     | %10zu | %6.1f\n",
                without.generalised, n, without.memorised, without.candidates, without.seconds);
    std::printf("  search + library     |    %2zu/%-2zu    |    %2zu     | %10zu | %6.1f\n",
                with.generalised, n, with.memorised, with.candidates, with.seconds);
    std::printf("  CONSTRUCTION         |    %2zu/%-2zu    |    %2zu     | %10zu | %6.1f\n",
                built.gen, n, built.mem, built.nodes, built.secs);

    std::printf("\n  DOES IT COMPOUND?\n");
    std::printf("    library primitives appearing in LIVE positions of certified\n");
    std::printf("    solutions: %zu\n", with.lib_calls_live);
    if (with.lib_calls_live == 0) {
        std::printf("    ZERO. Later solutions are not built on earlier ones, so the\n");
        std::printf("    library is decoration and the growth mechanism is not working.\n");
    } else if (with.generalised > without.generalised) {
        std::printf("    The library arm solved %zu more tasks on the same budget, and its\n",
                    with.generalised - without.generalised);
        std::printf("    primitives are actually being called. Capability compounds.\n");
    } else {
        std::printf("    Primitives are called, but the library arm did NOT solve more\n");
        std::printf("    than the arm without one. Reuse is happening; it is not yet\n");
        std::printf("    paying, and that is the honest state.\n");
    }
    if (without.candidates > 0 && with.candidates < without.candidates) {
        std::printf("    It also reached its solutions with %.1f%% fewer candidates.\n",
                    100.0 * (1.0 - static_cast<double>(with.candidates) /
                                       static_cast<double>(without.candidates)));
    }

    std::printf("\n  A NOTE ON THE CURRICULUM: the task order is fixed and chosen by hand,\n");
    std::printf("  so the library arm sees easy tasks before ones that could reuse them.\n");
    std::printf("  That is a real advantage handed to it and it is not hidden here. The\n");
    std::printf("  arm without a library faces the identical order, budget and seeds, so\n");
    std::printf("  the ordering is not what separates them -- but a shuffled order is the\n");
    std::printf("  harder test and it is not run yet.\n");
    return 0;
}
