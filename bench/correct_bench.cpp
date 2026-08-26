// CERTIFIED IS NOT CORRECT. HOW MUCH OF WHAT KHORA WRITES IS ACTUALLY RIGHT?
//
// The synthesiser returns a proof state with every program, and the states mean
// very different things:
//
//   Tested       passes the cases it was shown. Says nothing about anything else.
//   Generalised  passes cases it was NOT shown. Evidence, and still a sample.
//   Verified     an adversary hunted for a counterexample and failed. Stronger,
//                and still a sample -- it cannot distinguish "none exists" from
//                "none was drawn".
//   Exhaustive   checked on EVERY input in a stated finite domain. A proof.
//
// The throughput measurement says the gap between those is not academic: of the
// programs it solves under adversarial probing, **35.5% survive**. Nearly two
// thirds of what the system calls a solution is wrong somewhere the probes did
// not look. That is the quality ceiling of the whole organ, and no amount of
// speed matters above it.
//
// THE QUESTION THIS ASKS, which the existing benches do not. Exhaustive checking
// is available and proves a program over a BOUNDED domain -- short lists, small
// values. A proof there is not a proof everywhere. So:
//
//   does proving a program on a small domain predict that it is correct on a
//   much larger one it was never checked against?
//
// If yes, bounded proof is the answer to "guaranteed correct" and the bound is
// an implementation detail. If no, the bound IS the limit and saying "proved"
// oversells it. Either answer is worth more than the speed number.
//
// Every arm is scored on the SAME held-out wilderness: lists up to twice as long
// over values ten times wider than anything the proof domain contains, plus the
// edge cases that break fitted programs. That set is never used to accept or
// refine anything -- it exists only to grade.

#include "khora/techne/techne.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace khora::techne;
using clk = std::chrono::steady_clock;

namespace {

std::uint64_t mix(std::uint64_t& s) {
    s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s;
}

std::pair<double, double> wilson(std::size_t k, std::size_t n) {
    if (n == 0) return {0.0, 0.0};
    const double z = 1.96, p = (double)k / (double)n;
    const double d = 1.0 + z * z / (double)n;
    const double c = p + z * z / (2.0 * (double)n);
    const double m = z * std::sqrt(p * (1 - p) / (double)n + z * z / (4.0 * (double)n * (double)n));
    return {100.0 * (c - m) / d, 100.0 * (c + m) / d};
}

// --- the task family -------------------------------------------------------
//
// Compositions of primitives, which is what the throughput stream uses. Written
// out as C++ lambdas so the reference is unambiguous and the synthesiser cannot
// accidentally be handed its own implementation.
using Fn = std::function<Value(const Value&)>;

Value cap_all(Value v) { for (auto& x : v) x = cap_value(x); return v; }

std::vector<std::pair<std::string, Fn>> unit_ops() {
    return {
        {"rev",   [](const Value& v) { return Value(v.rbegin(), v.rend()); }},
        {"sort",  [](const Value& v) { Value o = v; std::sort(o.begin(), o.end()); return o; }},
        {"tail",  [](const Value& v) { return v.empty() ? v : Value(v.begin() + 1, v.end()); }},
        {"init",  [](const Value& v) { return v.empty() ? v : Value(v.begin(), v.end() - 1); }},
        {"inc",   [](const Value& v) { Value o = v; for (auto& x : o) x = cap_value(x + 1); return o; }},
        {"dbl",   [](const Value& v) { Value o = v; for (auto& x : o) x = cap_value(x * 2); return o; }},
        {"neg",   [](const Value& v) { Value o = v; for (auto& x : o) x = cap_value(-x); return o; }},
        {"len",   [](const Value& v) { return Value{(std::int64_t)v.size()}; }},
        {"sum",   [](const Value& v) { std::int64_t s = 0; for (auto x : v) s = cap_value(s + x); return Value{s}; }},
        {"head",  [](const Value& v) { return v.empty() ? Value{} : Value{v.front()}; }},
    };
}

struct Task {
    std::string name;
    Fn          f;
};

// A task is one or two primitives composed. Deeper than two and almost nothing
// is solvable at this pool size, which would measure the search budget instead
// of the verification.
std::vector<Task> generate(std::size_t n, std::uint64_t seed) {
    const auto ops = unit_ops();
    std::vector<Task> out;
    std::uint64_t s = seed;
    while (out.size() < n) {
        const std::size_t a = mix(s) % ops.size();
        const bool two = (mix(s) % 3) != 0;
        if (!two) { out.push_back({ops[a].first, ops[a].second}); continue; }
        const std::size_t b = mix(s) % ops.size();
        const Fn fa = ops[a].second, fb = ops[b].second;
        out.push_back({ops[b].first + "(" + ops[a].first + ")",
                       [fa, fb](const Value& v) { return cap_all(fb(fa(v))); }});
    }
    return out;
}

// --- the wilderness: how correctness is GRADED, never how it is accepted ----
//
// Deliberately outside the proof domain in both directions -- longer lists and
// values an order of magnitude wider -- plus the edges that break fitted
// programs. If a bounded proof means anything, it has to survive here.
std::vector<Value> wilderness() {
    std::vector<Value> w = {
        {}, {0}, {1}, {-1}, {0, 0, 0, 0, 0}, {7, 7, 7},
        {1000000000}, {-1000000000}, {999999999, 999999999},
        {1, 2, 3, 4, 5, 6, 7, 8}, {8, 7, 6, 5, 4, 3, 2, 1},
        {-50, 50}, {123456, -123456, 0},
    };
    std::uint64_t s = 0x51EED10DULL;
    for (std::size_t i = 0; i < 200; ++i) {
        Value v;
        const std::size_t len = mix(s) % 9;
        for (std::size_t j = 0; j < len; ++j)
            v.push_back((std::int64_t)(mix(s) % 40001) - 20000);
        w.push_back(v);
    }
    return w;
}

bool right_in_the_wild(const Recipe& r, const Fn& f, const std::vector<Value>& w) {
    for (const Value& in : w) if (r.apply(in, nullptr) != f(in)) return false;
    return true;
}

struct Arm {
    const char* name;
    std::size_t accepted = 0;      // the system said yes
    std::size_t right = 0;         // and it was right in the wild
    std::size_t trap_accepted = 0; // the same, restricted to the trap tasks
    std::size_t trap_right = 0;
    double      seconds = 0.0;
};

} // namespace

int main(int argc, char** argv) {
    const std::size_t N   = (argc > 1) ? std::stoul(argv[1]) : 240;
    const std::size_t cap = (argc > 2) ? std::stoul(argv[2]) : 12000;

    // The proof domain. 781 inputs; every list of length 0..4 over -2..2.
    const std::int64_t lo = -2, hi = 2;
    const std::size_t  mlen = 4;

    std::printf("Correct — certified is not correct, so how much of it is right?\n\n");
    std::printf("  %zu composed tasks, pool cap %zu\n", N, cap);
    std::printf("  proof domain     : every list of length 0..%zu over %lld..%lld\n",
                mlen, (long long)lo, (long long)hi);
    const auto wild = wilderness();
    std::printf("  graded on        : %zu held-out inputs, lists to length 8 over\n"
                "                     -20000..20000 -- OUTSIDE the proof domain in both\n"
                "                     directions, and never used to accept or refine\n\n",
                wild.size());

    std::size_t domain_size = 0;
    { std::size_t p = 1;
      for (std::size_t k = 0; k <= mlen; ++k) { domain_size += p; p *= (std::size_t)(hi - lo + 1); } }

    auto tasks = generate(N, 0xC0FFEEULL);

    // --- THE TRAPS, and the bench is not honest without them -----------------
    //
    // The first version of this scored 97-100% on every arm, which contradicts
    // the 35.5% the throughput stream reports and does not explain it. The reason
    // was the task family: one- and two-op compositions of clean primitives are
    // easy, and most have an exact primitive match, so the bench never entered
    // the regime where the problem exists.
    //
    // These are tasks whose behaviour OUTSIDE the proof domain differs from
    // inside it. A list longer than 4 or a value beyond +/-2 is where they
    // change, and the proof domain contains neither -- so a program can be
    // genuinely PROVED on all 781 inputs and still be wrong. That is the case
    // the question was asked about, and without it the answer is a foregone
    // conclusion.
    {
        const auto trap = [](const char* n, Fn f) { return Task{n, f}; };
        std::vector<Task> traps = {
            // Invisible below length 5: the proof domain stops at 4.
            trap("drop4", [](const Value& v) {
                return v.size() <= 4 ? Value{} : Value(v.begin() + 4, v.end()); }),
            trap("fifth", [](const Value& v) {
                return v.size() < 5 ? Value{} : Value{v[4]}; }),
            trap("len_gt4", [](const Value& v) {
                return Value{v.size() > 4 ? 1 : 0}; }),
            // Invisible below |value| > 2: the proof domain stops at 2.
            trap("clamp3", [](const Value& v) {
                Value o = v; for (auto& x : o) x = x > 3 ? 3 : (x < -3 ? -3 : x); return o; }),
            trap("big_only", [](const Value& v) {
                Value o; for (auto x : v) if (x > 2 || x < -2) o.push_back(x); return o; }),
            trap("sign_at_10", [](const Value& v) {
                Value o; for (auto x : v) o.push_back(x >= 10 ? 1 : 0); return o; }),
        };
        for (std::size_t r = 0; r < 6; ++r)
            for (const Task& t : traps) tasks.push_back(t);
    }
    const std::size_t n_trap = 36;
    std::printf("  of which %zu are TRAPS: tasks whose behaviour changes only OUTSIDE\n"
                "  the proof domain -- longer than %zu, or wider than %lld. A bounded\n"
                "  proof can be genuine and still wrong on these, which is the whole\n"
                "  question and the reason they are here.\n\n",
                n_trap, mlen, (long long)hi);

    Arm a_cert{"certified (Generalised)"};
    Arm a_samp{"+ 300 random probes"};
    Arm a_prov{"+ EXHAUSTIVE proof"};
    Arm a_hyb {"+ proof AND extremes"};

    std::size_t proved_count = 0;
    std::size_t idx = 0;
    for (Task &t_ref : tasks) { const Task& t = t_ref;
        const bool is_trap = (idx++ >= N);
        Spec spec;
        spec.name = t.name;
        // Ten visible cases and three held out, drawn from the proof domain so
        // that no arm gets to see the wilderness.
        std::uint64_t s = 0xBEEFULL + std::hash<std::string>{}(t.name);
        for (int k = 0; k < 13; ++k) {
            Value in;
            const std::size_t len = mix(s) % (mlen + 1);
            for (std::size_t j = 0; j < len; ++j)
                in.push_back((std::int64_t)(mix(s) % (std::size_t)(hi - lo + 1)) + lo);
            Case c(in, t.f(in));
            if (k >= 10) spec.holdout.push_back(c); else spec.cases.push_back(c);
        }
        Oracle oracle = [&t](const Value& in) { return t.f(in); };

        // --- arm A: what the default path accepts ---------------------------
        {
            const auto t0 = clk::now();
            const BuildResult b = construct_best(spec, cap, nullptr, true);
            a_cert.seconds += std::chrono::duration<double>(clk::now() - t0).count();
            if (b.proof == Proof::Generalised || b.proof == Proof::Verified) {
                ++a_cert.accepted;
                if (is_trap) ++a_cert.trap_accepted;
                const bool ok = right_in_the_wild(b.recipe, t.f, wild);
                if (ok) ++a_cert.right;
                if (ok && is_trap) ++a_cert.trap_right;
            }
        }

        // --- arm B: sampled adversarial verification ------------------------
        {
            auto ps = std::make_shared<std::uint64_t>(0xA11CEULL + a_samp.accepted);
            Prober prober = [ps](std::size_t k) -> Value {
                static const Value edges[] = {{}, {0}, {1}, {-1}, {2, 2}, {-2, -1, 0, 1, 2}};
                if (k < sizeof(edges) / sizeof(edges[0])) return edges[k];
                Value v;
                const std::size_t len = mix(*ps) % 6;
                for (std::size_t j = 0; j < len; ++j)
                    v.push_back((std::int64_t)(mix(*ps) % 5) - 2);
                return v;
            };
            const auto t0 = clk::now();
            Verification vf;
            const BuildResult b = synthesise_verified(spec, cap, oracle, prober, 300, 6,
                                                      nullptr, &vf);
            a_samp.seconds += std::chrono::duration<double>(clk::now() - t0).count();
            if (b.proof == Proof::Verified || b.proof == Proof::Generalised) {
                ++a_samp.accepted;
                if (is_trap) ++a_samp.trap_accepted;
                const bool ok = right_in_the_wild(b.recipe, t.f, wild);
                if (ok) ++a_samp.right;
                if (ok && is_trap) ++a_samp.trap_right;
            }
        }

        // --- arm D: the proof AND the edges of the deployment range ---------
        //
        // This was written inline here first and is now techne::synthesise_hardened,
        // so the bench measures the SHIPPED function rather than a copy of it that
        // could drift. If this arm regresses, the library regressed.
        {
            const auto t0 = clk::now();
            Exhaust ex;
            const BuildResult b = synthesise_hardened(spec, cap, oracle, lo, hi, mlen, 6,
                                                      nullptr, &ex);
            a_hyb.seconds += std::chrono::duration<double>(clk::now() - t0).count();
            if (b.proof == Proof::Exhaustive) {
                ++a_hyb.accepted;
                if (is_trap) ++a_hyb.trap_accepted;
                const bool ok = right_in_the_wild(b.recipe, t.f, wild);
                if (ok) ++a_hyb.right;
                if (ok && is_trap) ++a_hyb.trap_right;
            }
        }

        // --- arm C: exhaustive proof over the bounded domain ----------------
        {
            const auto t0 = clk::now();
            Exhaust ex;
            const BuildResult b = synthesise_exhaustive(spec, cap, oracle, lo, hi, mlen, 6,
                                                        nullptr, &ex);
            a_prov.seconds += std::chrono::duration<double>(clk::now() - t0).count();
            if (b.proof == Proof::Exhaustive) {
                ++proved_count;
                ++a_prov.accepted;
                if (is_trap) ++a_prov.trap_accepted;
                const bool ok = right_in_the_wild(b.recipe, t.f, wild);
                if (ok) ++a_prov.right;
                if (ok && is_trap) ++a_prov.trap_right;
            } else if (b.proof == Proof::Generalised || b.proof == Proof::Verified) {
                // It solved something and could not prove it. That is a REFUSAL
                // under this arm, and counting it as an acceptance would be the
                // whole point missed.
            }
        }
    }

    std::printf("  arm                     | accepted | right in the wild | 95%% CI          | seconds\n");
    std::printf("  ------------------------+----------+-------------------+-----------------+--------\n");
    for (const Arm* a : {&a_cert, &a_samp, &a_prov, &a_hyb}) {
        const auto ci = wilson(a->right, a->accepted);
        std::printf("  %-23s | %4zu/%-3zu | %6zu  %6.1f%%   | [%5.1f%%, %5.1f%%] | %6.1f\n",
                    a->name, a->accepted, tasks.size(), a->right,
                    a->accepted ? 100.0 * (double)a->right / (double)a->accepted : 0.0,
                    ci.first, ci.second, a->seconds);
    }

    std::printf("\n  ...and restricted to the %zu TRAP tasks alone:\n", n_trap);
    std::printf("  arm                     | accepted | right in the wild | 95%% CI\n");
    std::printf("  ------------------------+----------+-------------------+-----------------\n");
    for (const Arm* a : {&a_cert, &a_samp, &a_prov, &a_hyb}) {
        const auto ci = wilson(a->trap_right, a->trap_accepted);
        std::printf("  %-23s | %4zu/%-3zu | %6zu  %6.1f%%   | [%5.1f%%, %5.1f%%]\n",
                    a->name, a->trap_accepted, n_trap, a->trap_right,
                    a->trap_accepted ? 100.0 * (double)a->trap_right / (double)a->trap_accepted : 0.0,
                    ci.first, ci.second);
    }

    std::printf("\n  ACCEPTED is how often the system said yes. RIGHT IN THE WILD is how often\n"
                "  it was then correct on inputs outside anything it checked. The second\n"
                "  column is the only one that measures quality; the first measures nerve.\n");

    std::printf("\n  === DOES A BOUNDED PROOF SURVIVE OUTSIDE ITS BOUND? ===\n");
    if (a_prov.accepted == 0) {
        std::printf("    nothing was proved, so the question does not arise here.\n");
    } else {
        const double rate = 100.0 * (double)a_prov.right / (double)a_prov.accepted;
        const auto ci = wilson(a_prov.right, a_prov.accepted);
        std::printf("    %zu programs were PROVED on all %zu inputs of the bounded domain.\n",
                    a_prov.accepted, domain_size);
        std::printf("    Of those, %zu (%.1f%% [%.1f, %.1f]) are also correct on the wilderness.\n",
                    a_prov.right, rate, ci.first, ci.second);
        if (rate >= 99.0)
            std::printf("    A proof on the small domain PREDICTS correctness outside it here, so\n"
                        "    the bound is an implementation detail rather than the limit.\n");
        else
            std::printf("    A proof on the small domain does NOT guarantee correctness outside\n"
                        "    it. The bound IS the limit, and calling such a program 'proved'\n"
                        "    without naming the domain oversells it by %.1f points.\n", 100.0 - rate);
    }

    std::printf("\n  WHAT THIS CANNOT SEE. The wilderness is 213 inputs, not all of them, so\n"
                "  'right in the wild' is itself a sample and an upper bound on wrongness\n"
                "  rather than a proof of rightness. The task family is compositions of ten\n"
                "  list primitives; nothing here speaks to programs with control flow,\n"
                "  state, or side effects, which is most programs.\n");
    return 0;
}
