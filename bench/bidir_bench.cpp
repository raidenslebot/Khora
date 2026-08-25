// DOES SEARCHING FROM BOTH ENDS BEAT SEARCHING FROM ONE?
//
// Forward-only construction is exponential in depth, and every previous cycle in
// this project answered that by buying a bigger pool. Depth 3 needed 36,854
// behaviours for one task; the bounded conditionals added for edge cases turned
// out to be unreachable in principle at any pool size worth holding. Raising a
// budget against an exponential is not a strategy.
//
// Bidirectional search attacks the exponent instead. Forward from the input,
// backward from the target by inverting operations, meet in the middle: two
// searches of depth d/2 rather than one of depth d.
//
// THE COMPARISON THAT MATTERS is not solve rate alone -- it is BEHAVIOURS
// EXPLORED to reach the same answer. Solve rate at a fixed budget conflates two
// things; nodes-to-solution isolates the one under test. If bidirectional wins
// on solve rate but explores just as much, it bought nothing but luck.
//
// The tasks are graded by the DEPTH of their known solution, because the whole
// claim is about how cost scales with depth. A method that wins at depth 1 and
// loses at depth 4 has the wrong shape, and the table will say so.

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

std::uint64_t rs = 0xB1D12ULL;
std::uint64_t rnd() {
    std::uint64_t z = (rs += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

struct Task {
    const char* name;
    std::size_t depth;                       // known solution depth
    std::function<Value(const Value&)> ref;
};

std::int64_t sum_of(const Value& v) {
    std::int64_t s = 0;
    for (const auto x : v) s += x;
    return s;
}

// Tasks chosen so the depth is known, and specifically so several of them are
// reachable by INVERSION -- shifted, scaled and reversed outputs are exactly the
// shapes a backward step can deduce rather than stumble upon.
std::vector<Task> suite() {
    return {
        {"sum",             1, [](const Value& v) { return Value{sum_of(v)}; }},
        {"reverse",         1, [](const Value& v) { return Value(v.rbegin(), v.rend()); }},
        {"sort",            1, [](const Value& v) { Value o = v; std::sort(o.begin(), o.end()); return o; }},

        {"sorted_desc",     2, [](const Value& v) { Value o = v; std::sort(o.rbegin(), o.rend()); return o; }},
        {"sum_of_squares",  2, [](const Value& v) { std::int64_t s = 0; for (auto x : v) s += x * x; return Value{s}; }},
        {"scale_3",         2, [](const Value& v) { Value o; for (auto x : v) o.push_back(x * 3); return o; }},
        {"shift_7",         2, [](const Value& v) { Value o; for (auto x : v) o.push_back(x + 7); return o; }},

        {"rev_scaled_3",    3, [](const Value& v) { Value o(v.rbegin(), v.rend());
                                                    for (auto& x : o) x *= 3; return o; }},
        {"sorted_shift_5",  3, [](const Value& v) { Value o = v; std::sort(o.begin(), o.end());
                                                    for (auto& x : o) x += 5; return o; }},
        {"max_minus_min",   3, [](const Value& v) { if (v.empty()) return Value{};
                                                    const auto mm = std::minmax_element(v.begin(), v.end());
                                                    return Value{*mm.second - *mm.first}; }},
        {"scaled_shift",    3, [](const Value& v) { Value o; for (auto x : v) o.push_back(x * 2 + 10); return o; }},

        {"rev_sorted_x3",   4, [](const Value& v) { Value o = v; std::sort(o.rbegin(), o.rend());
                                                    for (auto& x : o) x *= 3; return o; }},
        {"sorted_desc_p1",  4, [](const Value& v) { Value o = v; std::sort(o.rbegin(), o.rend());
                                                    for (auto& x : o) x += 1; return o; }},
        {"rev_shift_scale", 4, [](const Value& v) { Value o(v.rbegin(), v.rend());
                                                    for (auto& x : o) x = x * 2 + 4; return o; }},
    };
}

Spec make(const Task& t) {
    Spec s;
    s.name = t.name;
    auto draw = [&](std::size_t len) {
        Value v;
        for (std::size_t i = 0; i < len; ++i) {
            v.push_back(static_cast<std::int64_t>(rnd() % 30) - 12);
        }
        return v;
    };
    for (std::size_t i = 0; i < 10; ++i) { Value in = draw(1 + i % 6); s.cases.push_back({in, t.ref(in)}); }
    for (std::size_t i = 0; i < 5; ++i)  { Value in = draw(7 + i);     s.holdout.push_back({in, t.ref(in)}); }
    return s;
}

} // namespace

int main(int argc, char** argv) {
    const std::size_t cap = (argc > 1) ? std::stoul(argv[1]) : 60000;

    const auto tasks = suite();
    std::printf("Does searching from both ends beat searching from one?\n\n");
    std::printf("  %zu tasks graded by the depth of their known solution.\n", tasks.size());
    std::printf("  pool cap %zu, 10 visible cases, 5 held out and drawn longer.\n\n", cap);

    std::printf("  task             | d | forward           | bidirectional     | nodes\n");
    std::printf("  -----------------+---+-------------------+-------------------+-------\n");

    std::size_t f_solved = 0, b_solved = 0;
    std::size_t f_nodes = 0, b_nodes = 0;
    double f_secs = 0, b_secs = 0;
    std::size_t depth_f[6] = {0}, depth_b[6] = {0}, depth_n[6] = {0};

    for (const Task& t : tasks) {
        rs = 0xB1D12ULL;                       // identical cases for both arms
        const Spec s = make(t);

        const auto t0 = clk::now();
        const BuildResult fwd = construct(s, cap, nullptr);
        const double fs = std::chrono::duration<double>(clk::now() - t0).count();

        const auto t1 = clk::now();
        const BuildResult bid = construct_bidir(s, cap, nullptr);
        const double bs = std::chrono::duration<double>(clk::now() - t1).count();

        const bool fok = fwd.proof == Proof::Generalised;
        const bool bok = bid.proof == Proof::Generalised;
        f_solved += fok; b_solved += bok;
        f_nodes += fwd.nodes_considered;
        b_nodes += bid.nodes_considered;
        f_secs += fs; b_secs += bs;
        const std::size_t d = std::min<std::size_t>(t.depth, 5);
        depth_n[d]++; depth_f[d] += fok; depth_b[d] += bok;

        char ratio[32] = "-";
        if (fok && bok && bid.nodes_considered > 0) {
            std::snprintf(ratio, sizeof ratio, "%.1fx",
                          static_cast<double>(fwd.nodes_considered) /
                          static_cast<double>(bid.nodes_considered));
        }
        std::printf("  %-16s | %zu | %-9s %7zu | %-9s %7zu | %s\n",
                    t.name, t.depth,
                    fok ? "solved" : "MISS", fwd.nodes_considered,
                    bok ? "solved" : "MISS", bid.nodes_considered, ratio);

        // PRINT THE PROGRAM, and probe it independently. Twice in this project a
        // clean-looking table turned out to be produced by a broken oracle, so a
        // solve rate is not evidence on its own: the expression has to be
        // readable and it has to survive inputs the search never saw.
        if (bok && !fok) {
            std::printf("      %s\n", bid.recipe.render().c_str());
            std::size_t agree = 0;
            const std::size_t tries = 200;
            for (std::size_t k = 0; k < tries; ++k) {
                Value in;
                const std::size_t len = rnd() % 14;
                for (std::size_t j = 0; j < len; ++j) {
                    in.push_back(static_cast<std::int64_t>(rnd() % 200) - 100);
                }
                if (bid.recipe.apply(in, nullptr) == t.ref(in)) ++agree;
            }
            std::printf("      independent probes: %zu/%zu agree with the reference\n",
                        agree, tries);
        }
    }

    std::printf("\n  BY DEPTH -- the column that decides whether the shape is right\n");
    std::printf("    depth | tasks | forward | bidirectional\n");
    std::printf("    ------+-------+---------+--------------\n");
    for (std::size_t d = 1; d <= 5; ++d) {
        if (depth_n[d] == 0) continue;
        std::printf("    %5zu | %5zu | %7zu | %13zu\n", d, depth_n[d], depth_f[d], depth_b[d]);
    }

    std::printf("\n  totals: forward %zu/%zu in %zu nodes (%.2f s)\n",
                f_solved, tasks.size(), f_nodes, f_secs);
    std::printf("          bidir   %zu/%zu in %zu nodes (%.2f s)\n",
                b_solved, tasks.size(), b_nodes, b_secs);
    if (b_nodes > 0 && f_nodes > 0) {
        std::printf("          %.2fx the nodes, %.2fx the time\n",
                    static_cast<double>(f_nodes) / static_cast<double>(b_nodes),
                    f_secs / std::max(1e-9, b_secs));
    }

    std::printf("\n  HOW TO READ IT\n");
    std::printf("    Solve rate at a fixed budget conflates two things. NODES is the\n");
    std::printf("    number under test: if bidirectional reaches the same answers while\n");
    std::printf("    exploring far fewer behaviours, the exponent moved. If it wins on\n");
    std::printf("    solve rate while exploring just as much, it bought luck.\n");
    std::printf("    A method whose advantage GROWS with depth has the right shape. One\n");
    std::printf("    that wins at depth 1 and loses at depth 4 does not.\n");
    return 0;
}
