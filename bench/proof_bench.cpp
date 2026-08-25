// HOW MUCH OF "VERIFIED" SURVIVES A COMPLETE CHECK?
//
// This module has three strengths of claim and they are not close to equal:
//
//   Generalised  passes every visible case and every held-out case. A statement
//                about a fixed sample, and the weakest thing worth reporting.
//   Verified     an adversary drew 300 inputs and found no disagreement. Much
//                stronger, and still sampling -- it cannot tell "no
//                counterexample exists" from "none was drawn".
//   Exhaustive   checked on EVERY input in a stated finite domain. A proof over
//                that domain, and the only one of the three that is not
//                evidence.
//
// The point of this bench is the GAP between them. Each level should reject
// programs the level below accepted, and the size of that rejection is the
// measure of how much a weaker claim was overstating. If exhaustive checking
// rejects nothing that sampling accepted, then sampling was adequate and the
// extra machinery is ceremony -- which is a result too, and would be reported
// as one.
//
// THE DOMAIN IS PART OF THE CLAIM. Lists of length 0..4 over the five values
// -2..2 is 781 inputs. Small enough to enumerate completely; large enough to
// contain the empty list, singletons, duplicates, negatives, zeroes, sorted and
// reverse-sorted orders -- every shape that breaks a program fitted to a handful
// of mid-sized random examples. A program proved here is proved for short lists
// of small integers and nothing more is asserted.

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

std::uint64_t rs = 0x9F00FULL;
std::uint64_t rnd() {
    std::uint64_t z = (rs += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

struct Task { const char* name; std::function<Value(const Value&)> ref; };

std::int64_t sum_of(const Value& v) {
    std::int64_t s = 0;
    for (const auto x : v) s += x;
    return s;
}

// Deliberately mixed: some clean, some with edge-case behaviour that a sample
// is likely to miss. A suite of only clean tasks would show no gap between the
// three levels and would prove nothing about the levels.
std::vector<Task> suite() {
    return {
        {"length",        [](const Value& v) { return Value{(std::int64_t)v.size()}; }},
        {"reverse",       [](const Value& v) { return Value(v.rbegin(), v.rend()); }},
        {"sort",          [](const Value& v) { Value o = v; std::sort(o.begin(), o.end()); return o; }},
        {"sum",           [](const Value& v) { return Value{sum_of(v)}; }},
        {"double_each",   [](const Value& v) { Value o; for (auto x : v) o.push_back(x * 2); return o; }},
        {"shift_1",       [](const Value& v) { Value o; for (auto x : v) o.push_back(x + 1); return o; }},
        {"squares",       [](const Value& v) { Value o; for (auto x : v) o.push_back(x * x); return o; }},
        {"first",         [](const Value& v) { return v.empty() ? Value{} : Value{v.front()}; }},
        {"last",          [](const Value& v) { return v.empty() ? Value{} : Value{v.back()}; }},
        {"tail",          [](const Value& v) { return v.size() > 1 ? Value(v.begin()+1, v.end()) : Value{}; }},
        {"sorted_desc",   [](const Value& v) { Value o = v; std::sort(o.rbegin(), o.rend()); return o; }},
        {"count_pos",     [](const Value& v) { std::int64_t n = 0; for (auto x : v) if (x > 0) ++n; return Value{n}; }},
        {"sum_pos",       [](const Value& v) { std::int64_t s = 0; for (auto x : v) if (x > 0) s += x; return Value{s}; }},
        {"max_minus_min", [](const Value& v) { if (v.empty()) return Value{};
                                               const auto mm = std::minmax_element(v.begin(), v.end());
                                               return Value{*mm.second - *mm.first}; }},
        // The one that caught the system out before: len rebuilt as sum(div(x,x))
        // counts NON-ZEROS, and only an input containing a zero reveals it.
        {"count_nonzero", [](const Value& v) { std::int64_t n = 0; for (auto x : v) if (x != 0) ++n; return Value{n}; }},
        {"minimum",       [](const Value& v) { return v.empty() ? Value{} : Value{*std::min_element(v.begin(), v.end())}; }},
    };
}

Spec make(const Task& t) {
    Spec s;
    s.name = t.name;
    // Mid-sized random examples with NO zeroes and NO empty list, which is what
    // a careless specification looks like and exactly the situation where the
    // three levels of claim come apart.
    auto draw = [&](std::size_t len) {
        Value v;
        for (std::size_t i = 0; i < len; ++i) {
            std::int64_t x = 0;
            while (x == 0) x = static_cast<std::int64_t>(rnd() % 19) - 9;
            v.push_back(x);
        }
        return v;
    };
    for (std::size_t i = 0; i < 10; ++i) { Value in = draw(2 + i % 4); s.cases.push_back({in, t.ref(in)}); }
    for (std::size_t i = 0; i < 5; ++i)  { Value in = draw(6 + i);     s.holdout.push_back({in, t.ref(in)}); }
    return s;
}

} // namespace

int main(int argc, char** argv) {
    const std::size_t cap  = (argc > 1) ? std::stoul(argv[1]) : 30000;
    const std::int64_t lo  = (argc > 2) ? std::stoll(argv[2]) : -2;
    const std::int64_t hi  = (argc > 3) ? std::stoll(argv[3]) :  2;
    const std::size_t mlen = (argc > 4) ? std::stoul(argv[4]) :  4;

    std::size_t domain = 0, p = 1;
    for (std::size_t l = 0; l <= mlen; ++l) { domain += p; p *= static_cast<std::size_t>(hi - lo + 1); }

    std::printf("How much of \"verified\" survives a COMPLETE check?\n\n");
    std::printf("  domain: every list of length 0..%zu over values %lld..%lld = %zu inputs\n",
                mlen, (long long)lo, (long long)hi, domain);
    std::printf("  specifications are drawn WITHOUT zeroes and WITHOUT the empty list,\n");
    std::printf("  which is what a careless specification looks like and exactly where\n");
    std::printf("  the three levels of claim come apart.\n\n");

    const auto tasks = suite();
    std::printf("  task           | certified | sampled 300 | EXHAUSTIVE | program\n");
    std::printf("  ---------------+-----------+-------------+------------+---------\n");

    std::size_t n_cert = 0, n_sampled = 0, n_exhaust = 0;
    const auto t0 = clk::now();

    for (const Task& t : tasks) {
        const Spec s = make(t);
        Oracle oracle = [&t](const Value& in) { return t.ref(in); };

        // Level 1: certified against the specification alone.
        const BuildResult b = construct(s, cap, nullptr);
        if (!b.certified()) {
            std::printf("  %-14s | %-9s |      -      |     -      | -\n", t.name, "no");
            continue;
        }
        ++n_cert;

        // Level 2: 300 random probes, the strength this module has been
        // reporting as "verified".
        std::size_t agree = 0;
        for (std::size_t k = 0; k < 300; ++k) {
            Value in;
            const std::size_t len = rnd() % 8;
            for (std::size_t j = 0; j < len; ++j) {
                in.push_back(static_cast<std::int64_t>(rnd() % 21) - 10);
            }
            if (b.recipe.apply(in, nullptr) == oracle(in)) ++agree;
        }
        const bool sampled_ok = (agree == 300);
        if (sampled_ok) ++n_sampled;

        // Level 3: every input in the domain.
        const Exhaust e = check_exhaustive(b.recipe, nullptr, oracle, lo, hi, mlen);
        if (e.clean) ++n_exhaust;

        std::printf("  %-14s | %-9s | %-11s | %-10s | %s\n", t.name, "yes",
                    sampled_ok ? "clean" : "WRONG",
                    e.clean ? "PROVED" : "WRONG",
                    b.recipe.render().c_str());
        if (!e.clean) {
            std::printf("                   counterexample: [");
            for (std::size_t i = 0; i < e.counterexample.size(); ++i) {
                std::printf("%s%lld", i ? "," : "", (long long)e.counterexample[i]);
            }
            std::printf("]  got ");
            const Value got = b.recipe.apply(e.counterexample, nullptr);
            std::printf("[");
            for (std::size_t i = 0; i < got.size(); ++i) std::printf("%s%lld", i ? "," : "", (long long)got[i]);
            std::printf("] want [");
            const Value wnt = oracle(e.counterexample);
            for (std::size_t i = 0; i < wnt.size(); ++i) std::printf("%s%lld", i ? "," : "", (long long)wnt[i]);
            std::printf("]\n");
        }
    }

    std::printf("\n  certified %zu, of those clean on 300 probes %zu, PROVED on all %zu inputs %zu\n",
                n_cert, n_sampled, domain, n_exhaust);
    if (n_sampled > n_exhaust) {
        std::printf("  %zu program%s passed 300 random probes and is WRONG inside the domain.\n",
                    n_sampled - n_exhaust, (n_sampled - n_exhaust) == 1 ? "" : "s");
        std::printf("  That is the gap between evidence and proof, measured rather than argued.\n");
    } else if (n_sampled == n_exhaust && n_cert > 0) {
        std::printf("  Sampling rejected exactly what enumeration rejected on this suite.\n");
        std::printf("  The proof is still worth more -- it cannot be lucky -- but on these\n");
        std::printf("  tasks it bought no additional rejections, and that is the honest\n");
        std::printf("  reading rather than a claim that it did.\n");
    }

    // ---- and now REPAIR: refine against complete counterexamples ------------
    std::printf("\n  REFINING against complete counterexamples\n");
    std::size_t repaired = 0, attempted = 0;
    for (const Task& t : tasks) {
        const Spec s = make(t);
        Oracle oracle = [&t](const Value& in) { return t.ref(in); };
        const BuildResult first = construct(s, cap, nullptr);
        if (!first.certified()) continue;
        if (check_exhaustive(first.recipe, nullptr, oracle, lo, hi, mlen).clean) continue;
        ++attempted;
        Exhaust e;
        const BuildResult fixed = synthesise_exhaustive(s, cap, oracle, lo, hi, mlen, 6, nullptr, &e);
        if (fixed.proof == Proof::Exhaustive) {
            ++repaired;
            std::printf("    %-14s repaired -> %s\n", t.name, fixed.recipe.render().c_str());
        } else {
            std::printf("    %-14s NOT repairable within the budget\n", t.name);
        }
    }
    std::printf("    %zu of %zu wrong programs repaired by feeding back complete\n",
                repaired, attempted);
    std::printf("    counterexamples. A counterexample from an exhaustive hunt cannot be\n");
    std::printf("    a lucky draw, so the refinement sequence cannot cycle.\n");

    std::printf("\n  %.1f s total\n", std::chrono::duration<double>(clk::now() - t0).count());
    return 0;
}
