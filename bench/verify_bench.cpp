// IS THE PROPERTY TRUE OF EVERY REACHABLE STATE, AND IF NOT, WHICH ONE?
//
// Logos proves goals. Given a query it searches for bindings that satisfy it,
// and its own header is explicit about the other half being absent: no SAT, no
// SMT, no negation. An audit of the rest of the tree found nothing else either
// -- no model checking, no temporal logic, no invariant checking, no bounded
// model checking anywhere. So Khora can answer "is there an x with P(x)" and
// has no way at all to answer "is P true of EVERY state this thing can reach,
// and if it is not, show me the state where it fails".
//
// That second question is the one you ask about your own code, and the answer
// that matters is the counterexample. This bench builds the machinery from
// nothing -- a DPLL SAT solver, then a bounded model checker on top of it --
// and points it at the Logos resolver itself.
//
// WHAT IT FOUND, up front so it can be argued with. Logos's `solve()` spends a
// depth budget: every fact match and every rule application costs one unit, and
// a rule application whose body has two atoms leaves TWO goals outstanding
// where there was one. Discharging a goal costs at least one unit. So a state
// in which the remaining budget is smaller than the number of outstanding goals
// cannot produce an answer -- not "probably will not", cannot, by counting.
// `solve()` has no such test and enters those states. The model checker refutes
// the invariant `depth >= |goals|` with a four-step trace, and over the full
// reachable set of the model 20 of the 39 reachable states are in that region.
// The fix is one line in solve(); I do not own that file here, so what is
// reported is the model-level count and a measured exponential on the real
// engine, not a measured saving.
//
// WHAT IS PROVED VERSUS MEASURED. Everything the SAT solver says is checked:
// a SAT answer by evaluating the returned assignment against every clause, an
// UNSAT answer against exhaustive enumeration on small instances. Everything
// the model checker says is checked twice: the SAT encoding is cross-checked
// against explicit breadth-first search over the whole state space, and every
// counterexample trace is replayed through the transition function from the
// initial state before it is printed. An unchecked solver is worthless and an
// unchecked counterexample is worse than worthless.
//
// WHAT THIS HARNESS CANNOT SEE, stated here and again at the end:
//   - The model of solve() is a MODEL. It reproduces the budget arithmetic and
//     the goal-stack arithmetic; it does not run the C++. Its predictions are
//     validated against the real Engine where that is possible (§7) and are
//     unvalidated where it is not (§5's state counts).
//   - "PROVED" from a bounded model checker means proved to depth k. It is
//     upgraded to unconditional only where BFS shows the reachable set closes
//     within k, and that is reported per property.
//   - The SAT solver is DPLL, not CDCL. Its absolute times are perhaps two
//     orders of magnitude off a modern solver. The SHAPE of the scaling curve
//     and the location of the phase transition are properties of the problem
//     and survive that; the milliseconds are properties of this code.

#include "khora/logos/logos.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <random>
#include <string>
#include <vector>

using namespace khora::logos;

namespace {

// ---------------------------------------------------------------------------
// Small shared plumbing.
// ---------------------------------------------------------------------------

using Clock = std::chrono::steady_clock;
double ms_since(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

int checks_failed = 0;
void require(bool cond, const char* what) {
    if (!cond) { std::printf("    CHECK FAILED: %s\n", what); ++checks_failed; }
}

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// Wilson score interval, as used elsewhere in this bench directory. A bare
// percentage over 80 instances is not a measurement.
std::pair<double, double> wilson(std::size_t k, std::size_t n) {
    if (n == 0) return {0.0, 0.0};
    const double z = 1.96, p = static_cast<double>(k) / static_cast<double>(n);
    const double d = 1.0 + z * z / n;
    const double c = p + z * z / (2 * n);
    const double s = z * std::sqrt(p * (1 - p) / n + z * z / (4.0 * n * n));
    return {100.0 * (c - s) / d, 100.0 * (c + s) / d};
}

// ---------------------------------------------------------------------------
// §A  CNF, and a DPLL solver.
//
// Textbook DPLL: unit propagation, pure literal elimination, backtracking
// search. No clause learning, no restarts, no watched literals -- this is the
// 1962 algorithm, chosen because it is small enough to be read and checked in
// one sitting, which is the whole point of building it rather than linking one.
//
// The one non-textbook part is bookkeeping: clause counters instead of
// rescanning the formula at each node. `nsat_[c]` counts true literals in
// clause c, `nun_[c]` counts unassigned ones, and `act_[l]` counts how many
// UNSATISFIED clauses still contain literal l. That last one makes both the
// pure-literal test and the branching heuristic O(vars) instead of O(formula),
// which is the difference between n=40 and n=100 being reachable.
// ---------------------------------------------------------------------------

inline int lidx(int lit) { return lit > 0 ? 2 * (lit - 1) : 2 * (-lit - 1) + 1; }

struct Cnf {
    int nvars = 0;
    std::vector<std::vector<int>> cl;

    // Duplicate literals break the counters (one variable, two decrements) and
    // tautologies are noise, so both are removed at the door.
    void add(std::vector<int> c) {
        std::sort(c.begin(), c.end(), [](int a, int b) {
            const int aa = std::abs(a), bb = std::abs(b);
            return aa != bb ? aa < bb : a < b;
        });
        c.erase(std::unique(c.begin(), c.end()), c.end());
        for (std::size_t i = 1; i < c.size(); ++i)
            if (c[i] == -c[i - 1]) return;
        for (int l : c) nvars = std::max(nvars, std::abs(l));
        cl.push_back(std::move(c));
    }
};

enum class Sat { True, False, Unknown };

struct Stats {
    long long decisions = 0, assigns = 0, conflicts = 0;
    bool budget_hit = false;
};

class Dpll {
public:
    Dpll(const Cnf& f, long long budget) : f_(f), budget_(budget) { init(); }

    // Branch on these variables first, in this order, before falling back to
    // the occurrence heuristic.
    //
    // This is not a speed tweak, it is the difference between the BMC section
    // finishing and not. The unrolling constrains x[i+1] GIVEN x[i] and u[i]
    // and not the other way round, so a solver left to its own devices guesses
    // state bits and then has to refute each guess against four possible
    // choices -- 128 branches per step where there are 4. Deciding the choice
    // variables in step order makes every state bit a consequence of unit
    // propagation, and the search tree becomes exactly the reachable trace
    // tree. Measured: 4^k leaves instead of hanging.
    void decide_first(std::vector<int> order) { order_ = std::move(order); }

    Sat solve() {
        if (empty_clause_) return Sat::False;
        const bool r = search();
        if (st.budget_hit) return Sat::Unknown;
        return r ? Sat::True : Sat::False;
    }

    // 1-indexed by variable: 1 true, -1 false, 0 never needed. Unassigned
    // variables are safe to read as false -- the search only reports success
    // once every clause holds a literal that is actually true, so any
    // completion of the partial assignment is still a model.
    const std::vector<std::int8_t>& value() const { return val_; }

    Stats st;

private:
    const Cnf& f_;
    long long budget_;
    std::vector<std::int8_t> val_;
    std::vector<std::vector<int>> occ_;
    std::vector<int> nsat_, nun_, act_, trail_, units_, order_;
    std::size_t order_at_ = 0;
    int sat_clauses_ = 0;
    bool empty_clause_ = false;

    void init() {
        val_.assign(f_.nvars + 1, 0);
        occ_.assign(2 * static_cast<std::size_t>(f_.nvars), {});
        act_.assign(2 * static_cast<std::size_t>(f_.nvars), 0);
        nsat_.assign(f_.cl.size(), 0);
        nun_.assign(f_.cl.size(), 0);
        for (std::size_t c = 0; c < f_.cl.size(); ++c) {
            nun_[c] = static_cast<int>(f_.cl[c].size());
            if (nun_[c] == 0) { empty_clause_ = true; continue; }
            for (int l : f_.cl[c]) {
                occ_[lidx(l)].push_back(static_cast<int>(c));
                ++act_[lidx(l)];
            }
            if (nun_[c] == 1) units_.push_back(static_cast<int>(c));
        }
    }

    // Returns false when this assignment empties a clause. It still finishes
    // both occurrence loops -- bailing early would leave the counters in a
    // state undo_to() cannot reverse.
    bool assign_lit(int lit) {
        const int v = std::abs(lit);
        const std::int8_t want = lit > 0 ? 1 : -1;
        if (val_[v] != 0) return val_[v] == want;
        val_[v] = want;
        trail_.push_back(v);
        ++st.assigns;
        for (int c : occ_[lidx(lit)]) {
            --nun_[c];
            if (nsat_[c]++ == 0) {
                ++sat_clauses_;
                for (int l2 : f_.cl[c]) --act_[lidx(l2)];
            }
        }
        bool ok = true;
        for (int c : occ_[lidx(-lit)]) {
            --nun_[c];
            if (nsat_[c] == 0) {
                if (nun_[c] == 0) ok = false;
                else if (nun_[c] == 1) units_.push_back(c);
            }
        }
        return ok;
    }

    void undo_to(std::size_t m) {
        order_at_ = 0;   // cheap: the order list is a few dozen entries
        while (trail_.size() > m) {
            const int v = trail_.back();
            trail_.pop_back();
            const int tl = val_[v] > 0 ? v : -v;
            for (int c : occ_[lidx(tl)]) {
                ++nun_[c];
                if (--nsat_[c] == 0) {
                    --sat_clauses_;
                    for (int l2 : f_.cl[c]) ++act_[lidx(l2)];
                }
            }
            for (int c : occ_[lidx(-tl)]) ++nun_[c];
            val_[v] = 0;
        }
    }

    bool propagate() {
        while (!units_.empty()) {
            const int c = units_.back();
            units_.pop_back();
            if (nsat_[c]) continue;
            if (nun_[c] == 0) { units_.clear(); return false; }
            if (nun_[c] != 1) continue;
            int lit = 0;
            for (int l : f_.cl[c]) if (val_[std::abs(l)] == 0) { lit = l; break; }
            if (!lit) continue;
            if (!assign_lit(lit)) { units_.clear(); return false; }
        }
        return true;
    }

    // Highest total occurrence count in still-unsatisfied clauses, phase toward
    // the commoner polarity. Crude, but it is the difference between the size
    // sweep finishing and not.
    int pick_branch() {
        while (order_at_ < order_.size() && val_[order_[order_at_]] != 0) ++order_at_;
        if (order_at_ < order_.size()) {
            const int v = order_[order_at_];
            return act_[lidx(v)] >= act_[lidx(-v)] ? v : -v;
        }
        int best = 0, bestn = -1;
        for (int v = 1; v <= f_.nvars; ++v) {
            if (val_[v]) continue;
            const int p = act_[lidx(v)], n = act_[lidx(-v)];
            if (p + n > bestn) { bestn = p + n; best = (p >= n) ? v : -v; }
        }
        return bestn > 0 ? best : 0;
    }

    bool search() {
        const std::size_t mark = trail_.size();
        if (!propagate()) { ++st.conflicts; undo_to(mark); return false; }

        // Pure literal elimination. A literal whose negation appears in no
        // unsatisfied clause can be set true without ever losing a model, so it
        // is set without a decision point and without a backtrack alternative.
        for (;;) {
            std::vector<int> pure;
            for (int v = 1; v <= f_.nvars; ++v) {
                if (val_[v]) continue;
                const int p = act_[lidx(v)], n = act_[lidx(-v)];
                if (p > 0 && n == 0) pure.push_back(v);
                else if (n > 0 && p == 0) pure.push_back(-v);
            }
            if (pure.empty()) break;
            bool ok = true;
            for (int l : pure) if (!assign_lit(l)) { ok = false; break; }
            if (!ok || !propagate()) { ++st.conflicts; undo_to(mark); return false; }
        }

        if (sat_clauses_ == static_cast<int>(f_.cl.size())) return true;
        if (st.decisions >= budget_) { st.budget_hit = true; return false; }

        const int lit = pick_branch();
        if (lit == 0) { undo_to(mark); return false; }
        ++st.decisions;
        for (int s = 0; s < 2; ++s) {
            const int try_lit = s == 0 ? lit : -lit;
            const std::size_t m2 = trail_.size();
            if (assign_lit(try_lit) && search()) return true;
            undo_to(m2);
            if (st.budget_hit) break;
        }
        undo_to(mark);
        return false;
    }
};

// ---------------------------------------------------------------------------
// §B  Checking the checker.
// ---------------------------------------------------------------------------

// A reported SAT answer is worth nothing until the assignment is evaluated.
bool model_satisfies(const Cnf& f, const std::vector<std::int8_t>& val) {
    for (const auto& c : f.cl) {
        bool ok = false;
        for (int l : c) {
            const std::int8_t v = val[std::abs(l)];
            if ((l > 0 && v == 1) || (l < 0 && v == -1)) { ok = true; break; }
        }
        if (!ok) return false;
    }
    return true;
}

// Ground truth for small instances: every one of the 2^n assignments.
bool brute_force_sat(const Cnf& f) {
    const std::uint32_t lim = 1u << f.nvars;
    for (std::uint32_t a = 0; a < lim; ++a) {
        bool all = true;
        for (const auto& c : f.cl) {
            bool ok = false;
            for (int l : c) {
                const bool v = ((a >> (std::abs(l) - 1)) & 1u) != 0;
                if ((l > 0) == v) { ok = true; break; }
            }
            if (!ok) { all = false; break; }
        }
        if (all) return true;
    }
    return false;
}

Cnf random_3sat(int n, int m, std::mt19937& rng) {
    Cnf f;
    std::uniform_int_distribution<int> V(1, n);
    std::bernoulli_distribution B(0.5);
    for (int i = 0; i < m; ++i) {
        const int a = V(rng);
        int b = V(rng); while (b == a) b = V(rng);
        int c = V(rng); while (c == a || c == b) c = V(rng);
        f.add({B(rng) ? a : -a, B(rng) ? b : -b, B(rng) ? c : -c});
    }
    f.nvars = n;
    return f;
}

} // namespace

namespace {

// ---------------------------------------------------------------------------
// §C  A bounded model checker.
//
// A transition system here is: a state packed into `nbits` bits, a
// nondeterministic choice packed into `nin` bits, one initial state, a total
// transition function next(state, choice), and a predicate bad(state).
//
// BMC unrolls the system k steps into propositional logic and asks SAT whether
// any bad state is reachable within k. SAT means there is a counterexample and
// the assignment IS the trace. UNSAT means no bad state is reachable in k
// steps -- a proof, but a bounded one, and the bound is the whole caveat.
//
// ENCODING. Variables x[i][b] for the state at step i, u[i][j] for the choice
// at step i, t[i] a selector meaning "step i is the bad one".
//   init         : unit clauses fixing x[0].
//   transitions  : for every (s, u) pair and every bit b,
//                    (x_i != s) OR (u_i != u) OR x[i+1][b] = next(s,u)_b
//                  which is one clause of nbits+nin+1 literals.
//   bad          : for every GOOD state s, (not t[i]) OR (x_i != s), so a true
//                  t[i] forces step i into a bad state; plus t[0] OR ... OR t[k].
//
// The transition encoding enumerates all 2^(nbits+nin) combinations, which is
// why every machine here is seven bits wide. A real BMC tool encodes the
// transition function as a circuit and never enumerates; that is a different
// amount of code and buys nothing at this size. The clause count is printed
// with every result so the ceiling is visible rather than implied.
// ---------------------------------------------------------------------------

struct Model {
    const char* name = "";
    int nbits = 0, nin = 0;
    std::uint32_t init = 0;
    std::function<std::uint32_t(std::uint32_t, std::uint32_t)> next;
    std::function<bool(std::uint32_t)> bad;
};

struct Unroll {
    Cnf f;
    int k = 0, nb = 0, ni = 0, ubase = 0, tbase = 0;
    int X(int i, int b) const { return 1 + i * nb + b; }
    int U(int i, int j) const { return ubase + i * ni + j; }
    int T(int i) const { return tbase + i; }
};

Unroll encode(const Model& M, int k) {
    Unroll e;
    e.k = k; e.nb = M.nbits; e.ni = M.nin;
    e.ubase = 1 + (k + 1) * e.nb;
    e.tbase = e.ubase + k * e.ni;

    for (int b = 0; b < e.nb; ++b)
        e.f.add({((M.init >> b) & 1u) ? e.X(0, b) : -e.X(0, b)});

    const std::uint32_t ns = 1u << e.nb, nu = 1u << e.ni;
    for (int i = 0; i < k; ++i)
        for (std::uint32_t s = 0; s < ns; ++s)
            for (std::uint32_t u = 0; u < nu; ++u) {
                const std::uint32_t sp = M.next(s, u);
                std::vector<int> pre;
                pre.reserve(static_cast<std::size_t>(e.nb + e.ni + 1));
                for (int b = 0; b < e.nb; ++b)
                    pre.push_back(((s >> b) & 1u) ? -e.X(i, b) : e.X(i, b));
                for (int j = 0; j < e.ni; ++j)
                    pre.push_back(((u >> j) & 1u) ? -e.U(i, j) : e.U(i, j));
                for (int b = 0; b < e.nb; ++b) {
                    std::vector<int> c = pre;
                    c.push_back(((sp >> b) & 1u) ? e.X(i + 1, b) : -e.X(i + 1, b));
                    e.f.add(std::move(c));
                }
            }

    std::vector<int> any;
    for (int i = 0; i <= k; ++i) {
        any.push_back(e.T(i));
        for (std::uint32_t s = 0; s < ns; ++s) {
            if (M.bad(s)) continue;
            std::vector<int> c;
            c.reserve(static_cast<std::size_t>(e.nb + 1));
            c.push_back(-e.T(i));
            for (int b = 0; b < e.nb; ++b)
                c.push_back(((s >> b) & 1u) ? -e.X(i, b) : e.X(i, b));
            e.f.add(std::move(c));
        }
    }
    e.f.add(any);
    e.f.nvars = e.tbase + k;
    return e;
}

struct Bmc {
    bool found = false, unknown = false, trace_valid = false;
    int len = -1;
    std::vector<std::uint32_t> st, in;
    std::size_t clauses = 0;
    long long decisions = 0;
    double ms = 0.0;
};

// Sweep k upward so the counterexample returned is the SHORTEST one, which is
// what makes it readable and what makes it comparable with BFS.
Bmc bmc_shortest(const Model& M, int kmax, long long budget = 40000000LL) {
    Bmc r;
    const Clock::time_point t0 = Clock::now();
    for (int k = 0; k <= kmax; ++k) {
        const Unroll e = encode(M, k);
        r.clauses = e.f.cl.size();
        Dpll d(e.f, budget);
        // The choice variables, in step order. Everything else follows from
        // them by propagation -- see Dpll::decide_first.
        std::vector<int> order;
        for (int i = 0; i < k; ++i)
            for (int j = 0; j < e.ni; ++j) order.push_back(e.U(i, j));
        d.decide_first(std::move(order));
        const Sat s = d.solve();
        r.decisions += d.st.decisions;
        if (s == Sat::Unknown) { r.unknown = true; break; }
        if (s != Sat::True) continue;

        const auto& v = d.value();
        for (int i = 0; i <= k; ++i) {
            std::uint32_t bits = 0;
            for (int b = 0; b < e.nb; ++b) if (v[e.X(i, b)] == 1) bits |= (1u << b);
            r.st.push_back(bits);
        }
        for (int i = 0; i < k; ++i) {
            std::uint32_t bits = 0;
            for (int j = 0; j < e.ni; ++j) if (v[e.U(i, j)] == 1) bits |= (1u << j);
            r.in.push_back(bits);
        }
        // Replay it. A counterexample nobody replayed is a claim, not evidence.
        bool ok = !r.st.empty() && r.st[0] == M.init;
        for (int i = 0; i < k && ok; ++i)
            ok = (M.next(r.st[i], r.in[i]) == r.st[i + 1]);
        bool any_bad = false;
        for (std::uint32_t s2 : r.st) if (M.bad(s2)) any_bad = true;
        r.trace_valid = ok && any_bad;
        r.found = true;
        r.len = k;
        break;
    }
    r.ms = ms_since(t0);
    return r;
}

// Explicit-state ground truth. These machines are 128 states wide, so the whole
// reachable set is cheap and the SAT answer can be CHECKED against it rather
// than believed. It also yields the diameter, which is what turns a bounded
// proof into an unbounded one.
struct Bfs {
    bool found = false;
    int len = -1, diameter = 0;
    std::vector<std::uint32_t> st, in;
    std::size_t reachable = 0, bad_reachable = 0;
};

Bfs bfs_all(const Model& M, int kmax) {
    const std::size_t N = std::size_t(1) << M.nbits;
    std::vector<int> dist(N, -1);
    std::vector<std::uint32_t> par(N, 0), pin(N, 0);
    std::vector<std::uint32_t> q;
    q.push_back(M.init);
    dist[M.init] = 0;
    for (std::size_t h = 0; h < q.size(); ++h) {
        const std::uint32_t s = q[h];
        for (std::uint32_t u = 0; u < (1u << M.nin); ++u) {
            const std::uint32_t t = M.next(s, u);
            if (dist[t] < 0) { dist[t] = dist[s] + 1; par[t] = s; pin[t] = u; q.push_back(t); }
        }
    }
    Bfs r;
    r.reachable = q.size();
    int best = -1;
    std::uint32_t bs = 0;
    for (std::uint32_t s : q) {
        r.diameter = std::max(r.diameter, dist[s]);
        if (!M.bad(s)) continue;
        ++r.bad_reachable;
        if (dist[s] <= kmax && (best < 0 || dist[s] < best)) { best = dist[s]; bs = s; }
    }
    if (best >= 0) {
        r.found = true;
        r.len = best;
        for (std::uint32_t c = bs;; c = par[c]) {
            r.st.push_back(c);
            if (dist[c] == 0) break;
            r.in.push_back(pin[c]);
        }
        std::reverse(r.st.begin(), r.st.end());
        std::reverse(r.in.begin(), r.in.end());
    }
    return r;
}

// The dumb baseline for a model checker: walk the machine at random and hope.
// Returns how many traces were drawn before one visited a bad state, or 0 for
// "gave up". `enabled_only` restricts the draw to choices that actually move
// the machine, which is the strongest fair version of the baseline.
long long random_hunt(const Model& M, int k, std::mt19937& rng, long long cap,
                      bool enabled_only) {
    std::vector<std::uint32_t> opts;
    for (long long trace = 1; trace <= cap; ++trace) {
        std::uint32_t s = M.init;
        if (M.bad(s)) return trace;
        for (int i = 0; i < k; ++i) {
            opts.clear();
            for (std::uint32_t u = 0; u < (1u << M.nin); ++u)
                if (!enabled_only || M.next(s, u) != s) opts.push_back(u);
            if (opts.empty()) break;
            s = M.next(s, opts[rng() % opts.size()]);
            if (M.bad(s)) return trace;
        }
    }
    return 0;
}

} // namespace

namespace {

// ---------------------------------------------------------------------------
// §D  Logos's resolver, as a transition system.
//
// This is the part that has to be argued for, because a model that does not
// correspond to the code proves nothing about the code. Here is the
// correspondence, line by line against src/logos/logos.cpp.
//
//   solve(goals, bind, depth, ...) does exactly three things that change the
//   pair (goals, depth):
//     - a fact unifies with the leading goal   -> solve(rest,          depth-1)
//     - a rule with a one-atom body applies    -> solve(body+rest,     depth-1)
//     - a rule with a two-atom body applies    -> solve(body+rest,     depth-1)
//   and it stops on `if (depth <= 0) return;` with goals still outstanding, or
//   emits an answer on `if (goals.empty())`.
//
//   For the ancestor rule set of tests/logos_test.cpp --
//     anc-base : ancestor(?x,?y) :- parent(?x,?y).
//     anc-step : ancestor(?x,?y) :- ancestor(?x,?z), parent(?z,?y).   [left rec]
//   -- the goal list only ever holds ancestor goals and parent goals, and
//   because anc-step reproduces exactly one ancestor goal while anc-base
//   consumes it, the number of ancestor goals is 1 until anc-base fires and 0
//   afterwards. So the whole state is (a in {0,1}, p, depth).
//
//   a  : 1 bit   -- is there still an ancestor goal outstanding
//   p  : 3 bits  -- how many parent goals are outstanding
//   d  : 3 bits  -- remaining depth budget; the query starts it at 7
//
// WHAT THE MODEL DROPS. Bindings, and therefore whether a goal can actually be
// discharged. It counts moves, it does not check them. That makes it an
// OVER-approximation: every real execution is a path in the model, but not
// every path in the model is a real execution. For a refutation that is the
// dangerous direction, so the four-step counterexample in §7 is written out as
// a concrete goal stack and checked by hand against solve(), and §8 checks the
// model's step counts against the real engine on six different queries.
//
// The 3-bit p also caps outstanding parent goals at 7. Nothing in the results
// below runs into that cap except the deliberate needle in P4, which is exactly
// the cap.
// ---------------------------------------------------------------------------

inline int rb_a(std::uint32_t s) { return static_cast<int>(s & 1u); }
inline int rb_p(std::uint32_t s) { return static_cast<int>((s >> 1) & 7u); }
inline int rb_d(std::uint32_t s) { return static_cast<int>((s >> 4) & 7u); }
inline std::uint32_t rb_mk(int a, int p, int d) {
    return static_cast<std::uint32_t>(a | (p << 1) | (d << 4));
}

enum { MV_FACT = 0, MV_BASE = 1, MV_STEP = 2, MV_NONE = 3 };
const char* rb_move_name(std::uint32_t u) {
    switch (u) {
        case MV_FACT: return "match a parent fact";
        case MV_BASE: return "apply anc-base  (body: 1 atom )";
        case MV_STEP: return "apply anc-step  (body: 2 atoms)";
        default:      return "no move";
    }
}

// `guarded` adds the one line solve() does not have:
//     if (depth < (int)goals.size()) return;
// i.e. refuse to descend into a state whose remaining budget cannot possibly
// discharge the goals already outstanding.
std::uint32_t rb_next(std::uint32_t s, std::uint32_t u, bool guarded) {
    const int a = rb_a(s), p = rb_p(s), d = rb_d(s);
    if (d == 0) return s;                 // solve() returns; the node is done
    int na = a, np = p;
    switch (u) {
        case MV_FACT: if (p == 0) return s; np = p - 1; break;
        case MV_BASE: if (a == 0 || p == 7) return s; na = 0; np = p + 1; break;
        case MV_STEP: if (a == 0 || p == 7) return s; np = p + 1; break;
        default: return s;
    }
    const int nd = d - 1;
    if (guarded && nd < na + np) return s;
    return rb_mk(na, np, nd);
}

Model rb_model(bool guarded, std::function<bool(std::uint32_t)> bad, const char* name) {
    Model M;
    M.name = name;
    M.nbits = 7;
    M.nin = 2;
    M.init = rb_mk(1, 0, 7);              // one ancestor goal, full budget
    M.next = [guarded](std::uint32_t s, std::uint32_t u) { return rb_next(s, u, guarded); };
    M.bad = std::move(bad);
    return M;
}

std::string rb_show(std::uint32_t s) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "a=%d p=%d d=%d  goals=%d  %s",
                  rb_a(s), rb_p(s), rb_d(s), rb_a(s) + rb_p(s),
                  rb_d(s) < rb_a(s) + rb_p(s) ? "<-- budget < goals" : "");
    return buf;
}

// The goal stack the model's counters stand for, in solve()'s own terms.
std::string rb_stack(std::uint32_t s) {
    std::string out = "[";
    if (rb_a(s)) out += "ancestor";
    if (rb_p(s)) {
        if (rb_a(s)) out += ", ";
        out += "parent x" + std::to_string(rb_p(s));
    }
    if (out.size() == 1) out += "empty -- an answer";
    return out + "]";
}

// ---------------------------------------------------------------------------
// §E  The same resolver against a chain of known length.
//
// The RB machine above cannot say how deep a query has to go, because it drops
// the bindings that decide whether anc-base can fire. This second machine adds
// exactly one thing back: r, the remaining distance along the parent chain for
// the outstanding ancestor goal. anc-base can only fire at r == 1, because
// parent(a0, ak) is a fact only when the two are adjacent; anc-step reduces r
// by one and leaves a parent goal behind.
//
// Depth is NOT in this state, because in solve() the depth spent is exactly the
// number of moves made -- so the shortest path to an answer state IS the
// minimum max_depth at which the real engine can answer. That is a falsifiable
// prediction and §8 falsifies or confirms it against the real Engine.
// ---------------------------------------------------------------------------

inline int ch_anc(std::uint32_t s) { return static_cast<int>(s & 1u); }
inline int ch_r(std::uint32_t s)   { return static_cast<int>((s >> 1) & 7u); }
inline int ch_p(std::uint32_t s)   { return static_cast<int>((s >> 4) & 7u); }

std::uint32_t ch_next(std::uint32_t s, std::uint32_t u) {
    int anc = ch_anc(s), r = ch_r(s), p = ch_p(s);
    switch (u) {
        case MV_FACT: if (p == 0) return s; --p; break;
        case MV_BASE: if (!anc || r != 1 || p == 7) return s; anc = 0; r = 0; ++p; break;
        case MV_STEP: if (!anc || r < 2 || p == 7) return s; --r; ++p; break;
        default: return s;
    }
    return static_cast<std::uint32_t>(anc | (r << 1) | (p << 4));
}

Model ch_model(int chain) {
    Model M;
    M.name = "logos chain query";
    M.nbits = 7;
    M.nin = 2;
    M.init = static_cast<std::uint32_t>(1 | (chain << 1));
    M.next = ch_next;
    M.bad = [](std::uint32_t s) { return ch_anc(s) == 0 && ch_p(s) == 0; };  // answer emitted
    return M;
}

// ---------------------------------------------------------------------------
// §F  The real engine.
// ---------------------------------------------------------------------------

Atom A(const std::string& r, const std::string& s, const std::string& o) {
    return Atom{r, Term::parse(s), Term::parse(o)};
}

std::string node(int i) { return "a" + std::to_string(i); }

// The ancestor rule set from tests/logos_test.cpp over a parent chain of the
// given length. `left` selects the dangerous shape -- the recursive goal first.
Engine chain_engine(int chain, bool left) {
    Engine e;
    for (int i = 0; i < chain; ++i) e.fact("parent", node(i), node(i + 1));
    e.rule(Rule{A("ancestor", "?x", "?y"), {A("parent", "?x", "?y")}, "anc-base"});
    if (left)
        e.rule(Rule{A("ancestor", "?x", "?y"),
                    {A("ancestor", "?x", "?z"), A("parent", "?z", "?y")}, "anc-step"});
    else
        e.rule(Rule{A("ancestor", "?x", "?y"),
                    {A("parent", "?x", "?z"), A("ancestor", "?z", "?y")}, "anc-step"});
    return e;
}

// Smallest max_depth at which the real resolver answers, or -1 within the sweep.
int real_min_depth(int chain, bool left, int cap) {
    const Engine e = chain_engine(chain, left);
    const Atom g = A("ancestor", node(0), node(chain));
    for (int d = 1; d <= cap; ++d) if (e.holds(g, d)) return d;
    return -1;
}

} // namespace

int main() {
    // Fully buffered stdout hides where a long run is; this bench prints little
    // and takes minutes, so the tradeoff is the wrong way round by default.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const Clock::time_point t_all = Clock::now();
    std::printf("VERIFY -- a SAT solver and a bounded model checker, pointed at Logos\n");
    std::printf("=====================================================================\n");

    // =======================================================================
    // §1  IS THE SOLVER RIGHT? EVERY ANSWER CHECKED, BOTH DIRECTIONS.
    //
    // A solver that is wrong is worse than no solver, because everything
    // downstream inherits the error silently. Every SAT answer here is checked
    // by evaluating the returned assignment against every clause. Every UNSAT
    // answer is checked against exhaustive enumeration of all 2^12 assignments.
    // The two columns must agree on every single instance; the mismatch column
    // is the whole point of the table.
    // =======================================================================
    {
        std::printf("\n\n§1  DPLL AGAINST EXHAUSTIVE ENUMERATION (n=12, every answer checked)\n");
        std::printf("    ratio | inst | dpll SAT | brute SAT | mismatch | bad models | med dec\n");
        std::printf("    ------+------+----------+-----------+----------+------------+--------\n");
        std::size_t total_mismatch = 0, total_bad = 0;
        for (const double ratio : {2.0, 3.0, 4.0, 4.26, 5.0, 6.0}) {
            const int n = 12, m = static_cast<int>(std::lround(ratio * n));
            std::mt19937 rng(0xC0FFEEu + static_cast<unsigned>(ratio * 100));
            std::size_t inst = 200, dsat = 0, bsat = 0, mism = 0, bad = 0;
            std::vector<double> dec;
            for (std::size_t i = 0; i < inst; ++i) {
                const Cnf f = random_3sat(n, m, rng);
                Dpll d(f, 100000000LL);
                const Sat s = d.solve();
                dec.push_back(static_cast<double>(d.st.decisions));
                const bool bs = brute_force_sat(f);
                if (s == Sat::True)  ++dsat;
                if (bs) ++bsat;
                if ((s == Sat::True) != bs) ++mism;
                if (s == Sat::True && !model_satisfies(f, d.value())) ++bad;
            }
            total_mismatch += mism;
            total_bad += bad;
            std::printf("    %5.2f | %4zu | %8zu | %9zu | %8zu | %10zu | %6.0f\n",
                        ratio, inst, dsat, bsat, mism, bad, median(dec));
        }
        require(total_mismatch == 0, "DPLL and brute force agree on every instance");
        require(total_bad == 0, "every SAT answer's assignment actually satisfies the formula");
        std::printf("\n    mismatch and bad-model columns are zero or the rest of this file\n"
                    "    is worthless. 1200 instances, both directions checked.\n");
    }

    // =======================================================================
    // §2  THE PHASE TRANSITION.
    //
    // Random 3-SAT flips from almost-always-satisfiable to almost-never at a
    // clause/variable ratio near 4.26, and the hard instances sit at the
    // crossing. This is the standard joint sanity check on generator and
    // solver: a broken generator moves the crossing, a broken solver moves the
    // satisfiable fraction. Nothing here was tuned to produce it.
    // =======================================================================
    {
        std::printf("\n\n§2  THE PHASE TRANSITION (n=60, 150 instances per ratio)\n");
        std::printf("    ratio | clauses | %%SAT  | 95%% CI           | med dec | med ms | bad models\n");
        std::printf("    ------+---------+-------+------------------+---------+--------+-----------\n");
        const int n = 60;
        double prev_ratio = 0.0, prev_frac = 1.0, cross = -1.0;
        std::size_t bad_total = 0;
        for (int step = 8; step <= 24; ++step) {
            const double ratio = step * 0.25;
            const int m = static_cast<int>(std::lround(ratio * n));
            std::mt19937 rng(0x5EEDu + static_cast<unsigned>(step));
            const std::size_t inst = 150;
            std::size_t sat = 0, bad = 0;
            std::vector<double> dec, tms;
            for (std::size_t i = 0; i < inst; ++i) {
                const Cnf f = random_3sat(n, m, rng);
                const Clock::time_point t0 = Clock::now();
                Dpll d(f, 100000000LL);
                const Sat s = d.solve();
                tms.push_back(ms_since(t0));
                dec.push_back(static_cast<double>(d.st.decisions));
                if (s == Sat::True) {
                    ++sat;
                    if (!model_satisfies(f, d.value())) ++bad;
                }
            }
            bad_total += bad;
            const double frac = static_cast<double>(sat) / inst;
            const auto ci = wilson(sat, inst);
            std::printf("    %5.2f | %7d | %5.1f | [%5.1f%%, %5.1f%%] | %7.0f | %6.2f | %10zu\n",
                        ratio, m, 100.0 * frac, ci.first, ci.second,
                        median(dec), median(tms), bad);
            if (cross < 0 && prev_ratio > 0 && prev_frac >= 0.5 && frac < 0.5)
                cross = prev_ratio + (prev_frac - 0.5) / (prev_frac - frac) * 0.25;
            prev_ratio = ratio;
            prev_frac = frac;
        }
        require(bad_total == 0, "every SAT answer in the sweep is a verified model");
        if (cross > 0)
            std::printf("\n    50%% crossing interpolates to ratio %.2f; the literature value is\n"
                        "    4.26 and it drifts up at small n, which n=60 is.\n", cross);
        else
            std::printf("\n    no 50%% crossing inside the swept range -- that would be a bug.\n");
        std::printf("    The decisions column peaks at the crossing, which is the second\n"
                    "    half of the check: both easy regions are easy for different reasons.\n");
    }

    // =======================================================================
    // §3  COST AGAINST SIZE, AT THE HARD RATIO.
    //
    // Held at 4.26 so the difficulty per variable is constant and only n moves.
    // The last column is the multiplier over the previous row; if the search is
    // exponential it should settle to a constant well above 1 rather than drift
    // toward it. Sweep stops when a size costs more than 3 s at the median,
    // which is a property of this DPLL, not of SAT.
    // =======================================================================
    {
        std::printf("\n\n§3  DECISIONS AND TIME AGAINST SIZE (ratio 4.26, 21 instances, 9 above n=140)\n");
        std::printf("      n | clauses | %%SAT | med decisions | x prev | med ms  | x prev\n");
        std::printf("    ----+---------+------+---------------+--------+---------+-------\n");
        double last_dec = 0, last_ms = 0;
        std::size_t unknowns = 0, bad = 0;
        for (int n = 20; n <= 280; n += 20) {
            const int m = static_cast<int>(std::lround(4.26 * n));
            std::mt19937 rng(0xA11CEu + static_cast<unsigned>(n));
            const std::size_t inst = n <= 140 ? 21u : 9u;   // the tail costs seconds each
            std::size_t sat = 0;
            std::vector<double> dec, tms;
            for (std::size_t i = 0; i < inst; ++i) {
                const Cnf f = random_3sat(n, m, rng);
                const Clock::time_point t0 = Clock::now();
                Dpll d(f, 60000000LL);
                const Sat s = d.solve();
                tms.push_back(ms_since(t0));
                dec.push_back(static_cast<double>(d.st.decisions));
                if (s == Sat::Unknown) ++unknowns;
                if (s == Sat::True) { ++sat; if (!model_satisfies(f, d.value())) ++bad; }
            }
            const double md = median(dec), mt = median(tms);
            std::printf("    %3d | %7d | %4.0f | %13.0f | %6s | %7.2f | %6s\n",
                        n, m, 100.0 * sat / inst, md,
                        last_dec > 0 ? (std::to_string(md / last_dec).substr(0, 5)).c_str() : "  -- ",
                        mt,
                        last_ms > 0.01 ? (std::to_string(mt / last_ms).substr(0, 5)).c_str() : "  -- ");
            last_dec = md;
            last_ms = mt;
            if (mt > 1500.0) { std::printf("    (stopped: median above 1.5 s)\n"); break; }
        }
        require(bad == 0, "every SAT answer in the size sweep is a verified model");
        std::printf("\n    %zu instances hit the decision budget and are reported as unknown.\n", unknowns);
        std::printf("    A per-row multiplier holding near a constant is what exponential\n"
                    "    looks like on a table; twenty more variables costs a fixed factor,\n"
                    "    not a fixed amount. The %%SAT column wanders because 4.26 sits on the\n"
                    "    crossing and 21 instances is a small sample of a coin flip.\n");
    }

    // =======================================================================
    // §4  THE DUMB BASELINE FOR SAT: DRAW ASSIGNMENTS AT RANDOM.
    //
    // The argument for a solver is not that it is clever, it is that the
    // obvious thing does not work at all. Instances are chosen that DPLL solved
    // and whose model was verified, so a solution certainly exists; random
    // sampling then gets a fixed budget to find one. The last column is how
    // close it ever got, which is the interesting part: it gets within a clause
    // or two and stays there forever.
    // =======================================================================
    {
        std::printf("\n\n§4  RANDOM ASSIGNMENT SAMPLING AGAINST DPLL (ratio 4.26, satisfiable instances)\n");
        std::printf("      n | inst | samples each | found | dpll found | best miss | dpll med dec\n");
        std::printf("    ----+------+--------------+-------+------------+-----------+-------------\n");
        for (int n = 15; n <= 45; n += 10) {
            const int m = static_cast<int>(std::lround(4.26 * n));
            std::mt19937 rng(0xBEEFu + static_cast<unsigned>(n));
            const long long samples = 50000;
            std::size_t inst = 0, found = 0;
            int best_miss = 1 << 30;
            std::vector<double> dec;
            std::vector<std::int8_t> v(static_cast<std::size_t>(n) + 1, 0);
            for (int tries = 0; tries < 200 && inst < 5; ++tries) {
                const Cnf f = random_3sat(n, m, rng);
                Dpll d(f, 100000000LL);
                if (d.solve() != Sat::True) continue;
                if (!model_satisfies(f, d.value())) { require(false, "verified model"); continue; }
                ++inst;
                dec.push_back(static_cast<double>(d.st.decisions));
                bool hit = false;
                int miss = 1 << 30;
                for (long long s = 0; s < samples && !hit; ++s) {
                    for (int b = 1; b <= n; ++b) v[b] = (rng() & 1u) ? 1 : -1;
                    int unsat = 0;
                    for (const auto& c : f.cl) {
                        bool ok = false;
                        for (int l : c) {
                            const std::int8_t x = v[std::abs(l)];
                            if ((l > 0 && x == 1) || (l < 0 && x == -1)) { ok = true; break; }
                        }
                        if (!ok) ++unsat;
                    }
                    miss = std::min(miss, unsat);
                    if (unsat == 0) hit = true;
                }
                if (hit) ++found;
                best_miss = std::min(best_miss, miss);
            }
            std::printf("    %3d | %4zu | %12lld | %5zu | %10zu | %9d | %12.0f\n",
                        n, inst, samples, found, inst, best_miss, median(dec));
        }
        std::printf("\n    'best miss' is the fewest clauses left unsatisfied by any of the\n"
                    "    50,000 draws, across all instances at that size. A solution exists\n"
                    "    in every one of these instances and DPLL produced it; sampling gets\n"
                    "    close and never arrives, because 'close' is not a thing SAT has.\n");
    }

    // =======================================================================
    // §5-§7  THE MODEL CHECKER, POINTED AT LOGOS'S RESOLVER.
    // =======================================================================
    struct Prop {
        const char* label;
        bool guarded;
        std::function<bool(std::uint32_t)> bad;
        bool expect_refuted;
    };
    const int KMAX = 7;   // the machine spends one depth unit per move from a budget of 7,
                          // so 7 is exactly its diameter -- no reachable state is deeper

    std::vector<Prop> props;
    props.push_back({"P1  depth >= |goals|  -- necessary for any answer", false,
                     [](std::uint32_t s) { return rb_d(s) < rb_a(s) + rb_p(s); }, true});
    props.push_back({"P2  depth + |goals| <= 8  -- the potential never rises", false,
                     [](std::uint32_t s) { return rb_d(s) + rb_a(s) + rb_p(s) > 8; }, false});
    props.push_back({"P3  no answer is ever emitted", false,
                     [](std::uint32_t s) { return rb_a(s) == 0 && rb_p(s) == 0; }, true});
    props.push_back({"P4  the goal stack never reaches 8", false,
                     [](std::uint32_t s) { return rb_a(s) + rb_p(s) >= 8; }, true});
    props.push_back({"P5  depth >= |goals|  -- WITH the one-line guard", true,
                     [](std::uint32_t s) { return rb_d(s) < rb_a(s) + rb_p(s); }, false});
    props.push_back({"P6  no answer is ever emitted, WITH the guard", true,
                     [](std::uint32_t s) { return rb_a(s) == 0 && rb_p(s) == 0; }, true});

    {
        std::printf("\n\n§5  BOUNDED MODEL CHECKING THE RESOLVER'S BUDGET (k = %d)\n", KMAX);
        std::printf("    Every SAT verdict is cross-checked against breadth-first search over\n"
                    "    the whole 128-state space, and every trace is replayed through the\n"
                    "    transition function from the initial state before it is believed.\n\n");
        std::printf("    property                                                | verdict  | len | BFS len | clauses | ms    | trace\n");
        std::printf("    --------------------------------------------------------+----------+-----+---------+---------+-------+------\n");
        for (const Prop& p : props) {
            const Model M = rb_model(p.guarded, p.bad, p.label);
            const Bmc b = bmc_shortest(M, KMAX);
            const Bfs g = bfs_all(M, KMAX);
            require(!b.unknown, "the SAT solver finished inside its decision budget");
            if (!b.unknown) {
                require(b.found == g.found, "BMC and BFS agree on whether a counterexample exists");
                require(b.found == p.expect_refuted, "the verdict is the one the analysis predicts");
            }
            if (b.found) {
                require(b.len == g.len, "BMC's shortest counterexample matches BFS's");
                require(b.trace_valid, "the returned trace replays from the initial state");
            }
            std::printf("    %-55s | %-8s | %3s | %7s | %7zu | %5.1f | %s\n",
                        p.label,
                        b.unknown ? "UNKNOWN" : (b.found ? "REFUTED" : "PROVED"),
                        b.found ? std::to_string(b.len).c_str() : " --",
                        g.found ? std::to_string(g.len).c_str() : "     --",
                        b.clauses, b.ms,
                        b.found ? (b.trace_valid ? "replays" : "INVALID") : "  --");
        }

        const Model m_un = rb_model(false, [](std::uint32_t s) { return rb_d(s) < rb_a(s) + rb_p(s); }, "");
        const Model m_gd = rb_model(true,  [](std::uint32_t s) { return rb_d(s) < rb_a(s) + rb_p(s); }, "");
        const Bfs f_un = bfs_all(m_un, KMAX), f_gd = bfs_all(m_gd, KMAX);
        const Model a_un = rb_model(false, [](std::uint32_t s) { return rb_a(s) == 0 && rb_p(s) == 0; }, "");
        const Model a_gd = rb_model(true,  [](std::uint32_t s) { return rb_a(s) == 0 && rb_p(s) == 0; }, "");
        const Bfs an_un = bfs_all(a_un, KMAX), an_gd = bfs_all(a_gd, KMAX);

        std::printf("\n    PROVED here means proved to depth %d. The reachable set closes at\n"
                    "    diameter %d unguarded and %d guarded, both within %d, so for these two\n"
                    "    machines the bounded proofs are in fact unbounded -- which is a fact\n"
                    "    about 128-state machines and will not hold for anything larger.\n",
                    KMAX, f_un.diameter, f_gd.diameter, KMAX);

        std::printf("\n    WHAT THE GUARD WOULD COST AND SAVE, over the full reachable set:\n");
        std::printf("      states reachable, as solve() is now      : %zu\n", f_un.reachable);
        std::printf("      of those, provably answer-free (d<|goals|): %zu  (%.0f%%)\n",
                    f_un.bad_reachable,
                    100.0 * static_cast<double>(f_un.bad_reachable) / static_cast<double>(f_un.reachable));
        std::printf("      states reachable with the guard          : %zu\n", f_gd.reachable);
        std::printf("      answer states reachable, now / guarded   : %zu / %zu\n",
                    an_un.bad_reachable, an_gd.bad_reachable);
        std::printf("      shortest proof, now / guarded            : %d / %d steps\n",
                    an_un.len, an_gd.len);
        require(an_un.bad_reachable == an_gd.bad_reachable,
                "the guard loses no answer state");
        require(an_un.len == an_gd.len, "the guard does not lengthen the shortest proof");
        std::printf("\n    The guard removes states and no answers. That is the whole case for\n"
                    "    it, and it is a claim about the MODEL -- see §10.\n");
    }

    // =======================================================================
    // §6  ONE COUNTEREXAMPLE, IN FULL.
    //
    // A checker that only ever says 'ok' is doing nothing. This is the trace
    // the solver returned for P1, replayed step by step, with the goal stack
    // that each state stands for written out in solve()'s own terms.
    // =======================================================================
    {
        std::printf("\n\n§6  THE COUNTEREXAMPLE TRACE FOR P1, REPLAYED\n");
        const Model M = rb_model(false, [](std::uint32_t s) { return rb_d(s) < rb_a(s) + rb_p(s); },
                                 "P1");
        const Bmc b = bmc_shortest(M, KMAX);
        if (!b.found) {
            std::printf("    P1 was not refuted -- nothing to print, and that would be a bug.\n");
            require(false, "P1 is refuted");
        } else {
            std::printf("    query: ancestor(a0, ak) with max_depth 7, rules anc-base / anc-step\n\n");
            std::printf("    step | move                            | goal stack                      | depth\n");
            std::printf("    -----+---------------------------------+---------------------------------+------\n");
            std::uint32_t cur = M.init;
            std::printf("       0 | %-31s | %-31s | %5d\n", "(initial call)",
                        rb_stack(cur).c_str(), rb_d(cur));
            for (std::size_t i = 0; i < b.in.size(); ++i) {
                const std::uint32_t nxt = M.next(cur, b.in[i]);
                require(nxt == b.st[i + 1], "each step of the printed trace replays");
                cur = nxt;
                std::printf("    %4zu | %-31s | %-31s | %5d%s\n", i + 1,
                            rb_move_name(b.in[i]), rb_stack(cur).c_str(), rb_d(cur),
                            M.bad(cur) ? "   <-- VIOLATION" : "");
            }
            std::printf("\n    At the violating state the remaining budget is %d and %d goals are\n"
                        "    outstanding. Discharging a goal costs at least one unit of depth, so\n"
                        "    no descendant of this node can reach the empty goal list, so no\n"
                        "    descendant can produce an answer. solve() has no test for this and\n"
                        "    keeps expanding until depth runs out.\n",
                        rb_d(cur), rb_a(cur) + rb_p(cur));
            std::printf("\n    Checked by hand against src/logos/logos.cpp. Call the slack\n"
                        "    depth - |goals|; it starts at 7 - 1 = 6, and every move spends one\n"
                        "    unit of depth, so:\n"
                        "      fact match  clears one goal       -> slack unchanged\n"
                        "      anc-base    one goal becomes one  -> slack falls by 1\n"
                        "      anc-step    one goal becomes two  -> slack falls by 2\n"
                        "    Nothing raises it and solve() has no move that recovers it, so the\n"
                        "    region is absorbing: once entered, every descendant is in it too.\n"
                        "    Four moves is the shortest way negative whichever mix of anc-step\n"
                        "    and anc-base gets there.\n");
        }
    }

    // =======================================================================
    // §7  THE MODEL AGAINST THE REAL RESOLVER.
    //
    // Everything above is a claim about a 128-state machine. This is the only
    // section that can make it a claim about logos.cpp. The chain machine
    // predicts the minimum max_depth at which the real engine can answer
    // ancestor(a0, ak); the real engine is then asked, and the two columns are
    // either equal or the model is wrong.
    //
    // The prediction is not a fit. It falls out of solve() spending exactly one
    // depth unit per resolution step and refusing to continue at depth 0 with
    // goals outstanding, so max_depth must be at least the number of steps in
    // the shortest proof. Nothing here was adjusted to make the columns match.
    // =======================================================================
    {
        std::printf("\n\n§7  PREDICTED MINIMUM DEPTH AGAINST THE REAL ENGINE\n");
        std::printf("    chain | BFS shortest | SAT-BMC shortest | real (left rec) | real (right rec)\n");
        std::printf("    ------+--------------+------------------+-----------------+-----------------\n");
        for (int chain = 1; chain <= 6; ++chain) {
            const Model M = ch_model(chain);
            const Bfs g = bfs_all(M, 2 * chain + 2);
            // The SAT encoding costs 4^k paths to refute, so it is run only
            // where that is affordable; BFS carries the rest and the two agree
            // wherever both ran.
            const bool run_sat = chain <= 4;
            Bmc b;
            if (run_sat) b = bmc_shortest(M, 2 * chain + 2);
            const int rl = real_min_depth(chain, true,  2 * chain + 6);
            const int rr = real_min_depth(chain, false, 2 * chain + 6);
            std::printf("    %5d | %12d | %16s | %15d | %16d\n", chain, g.len,
                        run_sat ? std::to_string(b.len).c_str() : "     (not run)",
                        rl, rr);
            require(g.found, "the chain machine reaches an answer state");
            if (run_sat) require(b.found && b.len == g.len, "SAT-BMC and BFS agree on the chain machine");
            require(rl == g.len, "the model's predicted minimum depth is the engine's actual one");
        }
        std::printf("\n    The prediction is 2k and the engine agrees at every k, for both the\n"
                    "    left- and right-recursive rule sets. The depth bound is therefore not\n"
                    "    a depth in the tree the way a reader would assume -- it is a budget of\n"
                    "    resolution STEPS, and a k-step ancestor costs 2k of them. Asking for\n"
                    "    ancestor at max_depth 8, the library default, answers to k=4 and\n"
                    "    silently answers nothing beyond it.\n");
    }

    // =======================================================================
    // §8  WHAT IT COSTS IN THE REAL ENGINE.
    //
    // The model says the resolver enters regions where no answer can exist. It
    // cannot say what that costs, because it counts moves and not nodes. What
    // CAN be measured from outside logos.cpp is the total cost of exhausting a
    // depth bound, and that is what the doomed region is a fraction of.
    //
    // The query is chosen to FAIL, because a failing query is the one that
    // explores the whole bound. The graph is six nodes with out-degree two and
    // cycles -- i -> i+1 and i -> i+2 mod 6 -- which is the smallest thing that
    // makes an unbound subgoal branch. On the parent CHAIN of §7 every subject
    // stays bound and the cost is polynomial; the exponential needs a choice.
    // =======================================================================
    {
        std::printf("\n\n§8  TIME TO EXHAUST THE DEPTH BOUND ON A FAILING QUERY\n");
        std::printf("    graph: 6 nodes, edges i->i+1 and i->i+2 (mod 6), 12 facts, out-degree 2\n");
        std::printf("    query: path(n0, sink) -- 'sink' appears in no fact, so it must fail\n\n");
        std::printf("    max_depth | left-recursive ms | x prev | right-recursive ms | x prev\n");
        std::printf("    ----------+-------------------+--------+--------------------+-------\n");

        auto build = [](bool left) {
            Engine e;
            for (int i = 0; i < 6; ++i) {
                e.fact("edge", "n" + std::to_string(i), "n" + std::to_string((i + 1) % 6));
                e.fact("edge", "n" + std::to_string(i), "n" + std::to_string((i + 2) % 6));
            }
            e.rule(Rule{A("path", "?x", "?y"), {A("edge", "?x", "?y")}, "path-base"});
            if (left)
                e.rule(Rule{A("path", "?x", "?y"),
                            {A("path", "?x", "?z"), A("edge", "?z", "?y")}, "path-step"});
            else
                e.rule(Rule{A("path", "?x", "?y"),
                            {A("edge", "?x", "?z"), A("path", "?z", "?y")}, "path-step"});
            return e;
        };
        const Engine el = build(true), er = build(false);
        const Atom goal = A("path", "n0", "sink");
        double pl = 0, pr = 0;
        bool stop_l = false, stop_r = false;
        for (int d = 2; d <= 34 && !(stop_l && stop_r); ++d) {
            double ml = -1, mr = -1;
            if (!stop_l) {
                const Clock::time_point t0 = Clock::now();
                const bool h = el.holds(goal, d);
                ml = ms_since(t0);
                require(!h, "the failing query really does fail");
                if (ml > 2000.0) stop_l = true;
            }
            if (!stop_r) {
                const Clock::time_point t0 = Clock::now();
                const bool h = er.holds(goal, d);
                mr = ms_since(t0);
                require(!h, "the failing query really does fail");
                if (mr > 2000.0) stop_r = true;
            }
            char lb[24] = "  (stopped)", rb2[24] = "  (stopped)", lx[12] = "   --", rx[12] = "   --";
            if (ml >= 0) std::snprintf(lb, sizeof lb, "%10.2f", ml);
            if (mr >= 0) std::snprintf(rb2, sizeof rb2, "%10.2f", mr);
            if (ml >= 0 && pl > 0.05) std::snprintf(lx, sizeof lx, "%5.2f", ml / pl);
            if (mr >= 0 && pr > 0.05) std::snprintf(rx, sizeof rx, "%5.2f", mr / pr);
            std::printf("    %9d | %17s | %6s | %18s | %6s\n", d, lb, lx, rb2, rx);
            if (ml > 0) pl = ml;
            if (mr > 0) pr = mr;
        }
        std::printf("\n    Both shapes grow by a roughly constant factor per unit of depth, which\n"
                    "    is what makes the depth bound load-bearing rather than cosmetic: the\n"
                    "    header is right that it guarantees termination, and the price of that\n"
                    "    guarantee is that raising it by one multiplies the work.\n");
        std::printf("\n    A NON-FINDING, recorded because I expected the opposite. The left-\n"
                    "    and right-recursive rule sets cost the same here to within noise.\n"
                    "    Both bodies have two atoms, so both grow the goal stack at the same\n"
                    "    rate; the left/right choice decides which subgoal gets a bound\n"
                    "    argument first, not how large a region the depth bound has to cover.\n"
                    "    The logos header's warning about left recursion is about termination,\n"
                    "    and termination is not what separates these two columns.\n");
        std::printf("\n    The doomed region sits inside these numbers and this harness CANNOT\n"
                    "    separate it out. Cutting it needs the guard inside solve(), and this\n"
                    "    bench owns one file which is not that one. What is measured here is\n"
                    "    the size of the thing the guard would cut into, not the cut.\n");
    }

    // =======================================================================
    // §9  THE DUMB BASELINE FOR THE CHECKER: SIMULATE AT RANDOM.
    //
    // The argument for a model checker is the number in the 'traces' column.
    // Two properties are hunted: P1, whose violation is four moves from the
    // start and which random simulation finds immediately, and P4, whose only
    // witness is the single input sequence anc-step seven times in a row.
    // Reporting only P4 would be cheating; both are here.
    // =======================================================================
    {
        std::printf("\n\n§9  RANDOM SIMULATION AGAINST THE CHECKER\n");
        std::printf("    property | witness           | median traces (uniform) | (enabled moves only) | gave up | BMC ms\n");
        std::printf("    ---------+-------------------+-------------------------+----------------------+---------+-------\n");
        struct Hunt { const char* tag; const char* why; std::function<bool(std::uint32_t)> bad; int trials; long long cap; };
        std::vector<Hunt> hunts;
        hunts.push_back({"P1", "4 moves, common", [](std::uint32_t s) { return rb_d(s) < rb_a(s) + rb_p(s); }, 400, 200000});
        hunts.push_back({"P4", "1 sequence of 7 ", [](std::uint32_t s) { return rb_a(s) + rb_p(s) >= 8; }, 120, 400000});
        for (const Hunt& h : hunts) {
            const Model M = rb_model(false, h.bad, h.tag);
            const Bmc b = bmc_shortest(M, KMAX);
            std::vector<double> uni, ena;
            std::size_t gaveup = 0;
            std::mt19937 rng(0xD1CEu);
            for (int t = 0; t < h.trials; ++t) {
                const long long a = random_hunt(M, KMAX, rng, h.cap, false);
                const long long c = random_hunt(M, KMAX, rng, h.cap, true);
                if (a == 0 || c == 0) ++gaveup;
                uni.push_back(static_cast<double>(a == 0 ? h.cap : a));
                ena.push_back(static_cast<double>(c == 0 ? h.cap : c));
            }
            std::printf("    %-8s | %-17s | %23.0f | %20.0f | %7zu | %6.1f\n",
                        h.tag, h.why, median(uni), median(ena), gaveup, b.ms);
        }
        std::printf("\n    P1 is a bad advertisement for model checking and it is in the table\n"
                    "    anyway: a bug four moves deep with a one-in-four move is found by\n"
                    "    guessing. P4 is the honest case. Its only witness is anc-step applied\n"
                    "    seven times with nothing in between, which is one path out of 4^7;\n"
                    "    the checker returns it from a single query. That ratio -- not the\n"
                    "    milliseconds -- is the argument for having a checker.\n");
        std::printf("\n    Both columns run against the MODEL, not the engine, so the comparison\n"
                    "    is fair in the only way it can be: the same machine, one searched\n"
                    "    exhaustively and one sampled.\n");
    }

    // =======================================================================
    // §10  WHAT THIS HARNESS CANNOT SEE.
    // =======================================================================
    std::printf("\n\n§10  WHAT THIS CANNOT SEE\n");
    std::printf("    - The resolver machine is a MODEL of solve(), not solve(). It reproduces\n"
                "      the depth arithmetic and the goal-stack arithmetic and drops the\n"
                "      bindings, which makes it an over-approximation: every real execution\n"
                "      is a path in it, not every path in it is a real execution. §7 is the\n"
                "      only check that the correspondence holds, and it checks step counts on\n"
                "      six queries -- not the whole of solve().\n");
    std::printf("    - The state counts in §5 are counts of MODEL states. They are not a\n"
                "      measured saving and no saving was measured, because the guard has to\n"
                "      go inside src/logos/logos.cpp and this bench owns bench/verify_bench.cpp.\n");
    std::printf("    - PROVED means proved to depth k. It is unconditional here only because\n"
                "      BFS shows these machines' reachable sets close inside k. Any machine\n"
                "      big enough to be interesting will not do that, and then the word\n"
                "      PROVED means considerably less than it looks.\n");
    std::printf("    - The BMC encoding enumerates all 2^(bits+choices) transitions, which\n"
                "      caps the state at seven bits. A circuit encoding removes that cap and\n"
                "      is not written here.\n");
    std::printf("    - The SAT solver is DPLL. On the UNSAT side it must enumerate the input\n"
                "      tree, so the k it can reach is small; §7 runs the SAT checker only to\n"
                "      chain 4 for that reason and leans on BFS beyond it. A CDCL solver\n"
                "      would move that line, not remove it.\n");
    std::printf("    - Nothing here says the invariants CHOSEN are the ones worth checking.\n"
                "      Picking the property is the part no tool does, and it is where the\n"
                "      value of formal verification in a tree like this actually sits.\n");

    std::printf("\n\nran in %.1f s\n", ms_since(t_all) / 1000.0);
    if (checks_failed == 0) std::printf("ALL SELF-CHECKS PASS\n");
    else                    std::printf("%d SELF-CHECK FAILURE(S)\n", checks_failed);
    return checks_failed == 0 ? 0 : 1;
}
