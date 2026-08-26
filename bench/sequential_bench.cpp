// CAN KHORA LEARN ANYTHING WHOSE PAYOFF ARRIVES LATER THAN THE NEXT TICK?
//
// A capability audit of this tree found exactly one reinforcement learner in it:
// khora::telos::Valuer, a UCB1 CONTEXTUAL BANDIT. It estimates E[r | context,
// action] from immediate reward and picks the arm with the highest confidence
// bound. telos.hpp says so itself, in the last paragraph:
//
//     "there is no bootstrapping and no discounting, so no temporal credit
//      assignment. An act whose payoff arrives three acts later is invisible
//      to it."
//
// Grep confirms it: no Q-learning, no TD error, no eligibility trace, no value
// iteration, no policy gradient, no transition model anywhere in src/. The
// Volition (include/khora/volition/volition.hpp) wires the bandit in as the act
// chooser with the dominant drive as context and a BINARY yield as reward. So
// Khora's entire capacity to learn from consequence is: one step, one reward,
// no future.
//
// That sentence is easy to write and easy to wave at. This bench makes it a
// number. It builds a real MDP -- an environment where the state MOVES and the
// reward for a good decision may not land for eight more decisions -- solves it
// exactly, and then puts the bandit in it beside two temporal-difference
// learners and chance.
//
// WHY A CLIFF-WALK LAYOUT, specifically. The grid below has two separable
// competences packed into it:
//
//   TRAP AVOIDANCE is a ONE-STEP problem. Standing next to the cliff, the
//   action that falls in returns -100 immediately. A bandit can learn this,
//   and the prediction is that it will learn it perfectly.
//
//   GOAL SEEKING is a MULTI-STEP problem. From nine squares away, every action
//   returns the same step cost. The information that distinguishes them lives
//   eight transitions in the future. A bandit cannot represent this, and the
//   prediction is that it will not learn it at all.
//
// Reporting one aggregate policy score over both would blur exactly the line
// this bench exists to draw, so they are counted separately.
//
// AND A CONSTANT-ACTION CONTROL, because the first version of this bench scored
// the bandit at 100% trap avoidance and that number was worthless. The bandit's
// learned policy turns out to be "always go up" -- 96% of its states are exact
// numeric ties and Valuer::best() breaks a tie by taking the lowest arm index --
// and a policy that always goes up never steps into a cliff that is south of it.
// The ALWAYS-UP row is in every table so that any score a constant policy gets
// for free is visible next to the score being claimed.
//
// THE REFERENCE IS VALUE ITERATION ON THE KNOWN MODEL, which makes every number
// here measurable instead of relative. V*(start) is the exact best achievable
// discounted return; a uniform-random policy's value is computed exactly too, by
// policy evaluation rather than by sampling. Every method is then reported as
// the fraction of the gap between chance and optimal that it closed.
//
// WHAT THIS HARNESS CANNOT SEE, stated before the results rather than after:
//
//   * The MDP is deterministic, tabular, fully observed and has 32 states. TD
//     methods have every structural advantage here. Nothing in this file says
//     anything about function approximation, partial observability, or scale.
//   * The bandit is handed the FULL STATE as its context -- 32 contexts. The
//     Volition gives it the dominant drive, about six contexts. This is a
//     GENEROUS setting for the bandit, not a rigged one. It fails anyway, and
//     it fails for a structural reason that more context cannot fix.
//   * Value iteration is handed the transition model. Q-learning and SARSA are
//     not. The gap between them is therefore not a fair sample-efficiency
//     comparison; VI is a reference, not a rival.
//   * Policy match counts STATES, not visits. A method can be right about 90%
//     of the grid and still never reach the goal. That is why greedy return is
//     reported beside it -- it is the metric that cannot be gamed by being
//     correct in corners nobody walks through.
//   * Nothing here shows that Khora's real acts form a Markov chain, or that
//     "state" is even observable in the Volition's loop. This measures what TD
//     buys WHEN the problem is sequential. Whether Khora's problems are
//     sequential is a separate question this file does not answer.

#include "khora/telos/telos.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

namespace {

// --- PLUMBING ---------------------------------------------------------------

// xorshift64, same generator the rest of the tree's benches use, so a seed here
// means the same thing it means there.
struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}
    std::uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    double        unit() { return static_cast<double>(next() >> 11) / 9007199254740992.0; }
    int           below(int n) { return static_cast<int>(next() % static_cast<std::uint64_t>(n)); }
};

// 95% Wilson interval on a proportion, as a percentage pair. Policy match is
// counted over (seed x state) pairs and the counts are in the hundreds, so the
// difference between two methods can be a couple of dozen events. A bare
// percentage invites reading that as a result.
std::pair<double, double> wilson(std::size_t hits, std::size_t n) {
    if (n == 0) return {0.0, 0.0};
    const double z = 1.96, dn = static_cast<double>(n);
    const double ph = static_cast<double>(hits) / dn;
    const double d  = 1.0 + z * z / dn;
    const double c  = ph + z * z / (2.0 * dn);
    const double m  = z * std::sqrt(ph * (1.0 - ph) / dn + z * z / (4.0 * dn * dn));
    return {100.0 * (c - m) / d, 100.0 * (c + m) / d};
}

// --- THE MDP ----------------------------------------------------------------
//
//   row 0   . . . . . . . .
//   row 1   . . . . . . . .
//   row 2   . . . . . . . .     <- the optimal lane, one square above the cliff
//   row 3   S T T T T T T G
//
// Deterministic moves; a move into a wall stays put and still pays the step
// cost. Traps and the goal are terminal. Reward is a function of the square
// ENTERED, which is what makes the goal reward delayed rather than emitted at
// the moment of the decision that earned it.
constexpr int kRows = 4, kCols = 8, kA = 4, kNS = kRows * kCols;
const char* const kActName[kA] = {"up", "right", "down", "left"};

struct Grid {
    double step  = -1.0;
    double goal  =  10.0;
    double trap  = -100.0;
    double gamma =  0.95;
    std::array<char, kNS> cell{};
    int start = 3 * kCols + 0;
};

Grid make_grid(double step_cost, double gamma) {
    Grid g;
    g.step  = step_cost;
    g.gamma = gamma;
    g.cell.fill('.');
    for (int c = 1; c <= 6; ++c) g.cell[3 * kCols + c] = 'T';
    g.cell[3 * kCols + 7] = 'G';
    return g;
}

bool terminal(const Grid& g, int s) { return g.cell[s] != '.'; }

int step_to(int s, int a) {
    int r = s / kCols, c = s % kCols;
    if      (a == 0 && r > 0)         --r;
    else if (a == 1 && c < kCols - 1) ++c;
    else if (a == 2 && r < kRows - 1) ++r;
    else if (a == 3 && c > 0)         --c;
    return r * kCols + c;
}

double reward_of(const Grid& g, int entered) {
    if (g.cell[entered] == 'G') return g.goal;
    if (g.cell[entered] == 'T') return g.trap;
    return g.step;
}

// A state from which SOME action falls into a trap. Trap avoidance is only a
// question in these; scoring it over the whole grid would dilute it with
// squares where every action is safe.
bool hazard_adjacent(const Grid& g, int s) {
    if (terminal(g, s)) return false;
    for (int a = 0; a < kA; ++a) if (g.cell[step_to(s, a)] == 'T') return true;
    return false;
}

// --- VALUE ITERATION: THE THING THAT MAKES THIS MEASURABLE -------------------
//
// Given the transition model, V* and the full optimal action SET per state.
// The SET matters: ties are common (two ways round an obstacle can be exactly
// as good), and scoring a method wrong for picking the other one would be
// measuring tie-breaking rather than learning.
struct Solution {
    std::vector<double>              V   = std::vector<double>(kNS, 0.0);
    std::vector<std::array<double, kA>> Q = std::vector<std::array<double, kA>>(kNS);
    std::vector<std::array<bool, kA>>   opt = std::vector<std::array<bool, kA>>(kNS);
    std::vector<int>                 pi  = std::vector<int>(kNS, 0);
    double                           mean_opt_actions = 0.0;
};

Solution value_iteration(const Grid& g) {
    Solution s;
    for (int it = 0; it < 200000; ++it) {
        double delta = 0.0;
        for (int st = 0; st < kNS; ++st) {
            if (terminal(g, st)) { s.V[st] = 0.0; continue; }
            double best = -1e300;
            for (int a = 0; a < kA; ++a) {
                const int n = step_to(st, a);
                best = std::max(best, reward_of(g, n) + g.gamma * s.V[n]);
            }
            delta = std::max(delta, std::fabs(best - s.V[st]));
            s.V[st] = best;
        }
        if (delta < 1e-12) break;
    }
    std::size_t nonterm = 0, opts = 0;
    for (int st = 0; st < kNS; ++st) {
        double best = -1e300;
        for (int a = 0; a < kA; ++a) {
            const int n = step_to(st, a);
            s.Q[st][a] = reward_of(g, n) + g.gamma * s.V[n];
            best = std::max(best, s.Q[st][a]);
        }
        for (int a = 0; a < kA; ++a) {
            s.opt[st][a] = s.Q[st][a] >= best - 1e-9;
            if (!terminal(g, st) && s.opt[st][a]) ++opts;
        }
        s.pi[st] = static_cast<int>(std::max_element(s.Q[st].begin(), s.Q[st].end()) - s.Q[st].begin());
        if (!terminal(g, st)) ++nonterm;
    }
    s.mean_opt_actions = nonterm ? static_cast<double>(opts) / static_cast<double>(nonterm) : 0.0;
    return s;
}

// Exact value of a deterministic policy, by iterative policy evaluation. Used
// instead of Monte-Carlo rollout so the reported greedy return carries no
// sampling noise and no episode-truncation artefact: a policy that walks into a
// wall forever correctly scores step/(1-gamma) rather than "whatever 100 steps
// of it cost".
std::vector<double> evaluate(const Grid& g, const std::vector<int>& pi) {
    std::vector<double> V(kNS, 0.0);
    for (int it = 0; it < 200000; ++it) {
        double delta = 0.0;
        for (int st = 0; st < kNS; ++st) {
            if (terminal(g, st)) continue;
            const int    n = step_to(st, pi[st]);
            const double v = reward_of(g, n) + g.gamma * V[n];
            delta = std::max(delta, std::fabs(v - V[st]));
            V[st] = v;
        }
        if (delta < 1e-12) break;
    }
    return V;
}

double evaluate_uniform(const Grid& g) {
    std::vector<double> V(kNS, 0.0);
    for (int it = 0; it < 200000; ++it) {
        double delta = 0.0;
        for (int st = 0; st < kNS; ++st) {
            if (terminal(g, st)) continue;
            double v = 0.0;
            for (int a = 0; a < kA; ++a) {
                const int n = step_to(st, a);
                v += 0.25 * (reward_of(g, n) + g.gamma * V[n]);
            }
            delta = std::max(delta, std::fabs(v - V[st]));
            V[st] = v;
        }
        if (delta < 1e-12) break;
    }
    return V[g.start];
}

bool reaches_goal(const Grid& g, const std::vector<int>& pi) {
    int s = g.start;
    for (int t = 0; t < 4 * kNS; ++t) {     // longer than any simple path; a loop trips the cap
        if (g.cell[s] == 'G') return true;
        if (g.cell[s] == 'T') return false;
        s = step_to(s, pi[s]);
    }
    return false;
}

// --- THE LEARNERS -----------------------------------------------------------

enum class Method { QLearn, Sarsa, Bandit, BanditScaled, AlwaysUp, Random };

constexpr Method kMethods[] = {Method::QLearn, Method::Sarsa, Method::Bandit,
                               Method::BanditScaled, Method::AlwaysUp, Method::Random};
const char* const kNames[]  = {"Q-learning", "SARSA", "telos bandit",
                               "telos scaled", "always up", "random"};
constexpr int kNM = 6;
constexpr int kBanditIdx = 2, kQIdx = 0;

// ARGMAX WITH A RANDOM TIE-BREAK, by reservoir sampling over the tied set.
// Taking the lowest index instead is not neutral: with a zero step cost every
// Q starts tied at zero, so an index tie-break makes the epsilon-greedy walk a
// biased march in one compass direction that never finds the goal. That is a
// property of the tie-break, not of Q-learning, and measuring it as if it were
// the latter would be a harness bug reported as a result.
int argmax_rand(const std::array<double, kA>& q, Rng& rng) {
    double best = q[0];
    int    n = 1, pick = 0;
    for (int a = 1; a < kA; ++a) {
        if (q[a] > best + 1e-12)      { best = q[a]; n = 1; pick = a; }
        else if (q[a] > best - 1e-12) { ++n; if (rng.below(n) == 0) pick = a; }
    }
    return pick;
}

struct Cfg {
    int    episodes  = 2000;
    int    window    = 200;
    int    max_steps = 100;    // grid diameter is 11; a wanderer hits this, a learner does not
    double alpha     = 0.10;
    double eps       = 0.10;
    double ucb_c     = 1.0;
};

struct Run {
    std::vector<double> curve;          // mean discounted return per training window
    std::vector<int>    pi = std::vector<int>(kNS, 0);
    std::size_t         tied = 0;       // non-terminal states where the greedy pick was a numeric tie
    std::size_t         steps = 0, informative = 0;   // transitions seen / transitions with r != 0
};

// One training loop for all five methods. They differ only in how an action is
// chosen and what is updated; four near-identical loops would be four places for
// the episode bookkeeping to drift apart.
Run train(const Grid& g, Method m, const Cfg& cfg, std::uint64_t seed) {
    Rng rng(seed);
    std::vector<std::array<double, kA>> Q(kNS);
    for (auto& row : Q) row.fill(0.0);

    // The bandit gets one context per state and one arm per move -- the most
    // generous encoding of this problem it can be given.
    khora::telos::Valuer bandit(kNS, kA);

    // UCB1's bound assumes rewards in [0,1]; these run from -100 to +10, so the
    // exploration term is negligible against the reward scale and the bandit
    // stops exploring almost at once. That is a real consequence of using the
    // module as it stands, so it is measured as-is -- and measured again with
    // rewards affinely rescaled into [0,1], which changes exploration but not
    // the greedy policy (argmax is invariant under a positive affine map). If
    // the scaled row also fails, scale was not the problem.
    const double lo = std::min({g.trap, g.step, g.goal});
    const double hi = std::max({g.trap, g.step, g.goal});
    const double span = (hi > lo) ? (hi - lo) : 1.0;

    auto pick = [&](int s) -> int {
        switch (m) {
        case Method::Random:       return rng.below(kA);
        case Method::AlwaysUp:     return 0;
        case Method::Bandit:
        case Method::BanditScaled: return static_cast<int>(bandit.choose(static_cast<std::size_t>(s), cfg.ucb_c));
        default: break;
        }
        if (rng.unit() < cfg.eps) return rng.below(kA);
        return argmax_rand(Q[s], rng);
    };

    const int nwin = std::max(1, cfg.episodes / cfg.window);
    Run out;
    out.curve.assign(static_cast<std::size_t>(nwin), 0.0);

    for (int ep = 0; ep < cfg.episodes; ++ep) {
        int    s    = g.start;
        int    a    = pick(s);
        double ret  = 0.0, disc = 1.0;
        for (int t = 0; t < cfg.max_steps; ++t) {
            const int    n = step_to(s, a);
            const double r = reward_of(g, n);
            const bool   done = terminal(g, n);
            ret += disc * r;
            disc *= g.gamma;
            ++out.steps;
            if (r != 0.0) ++out.informative;

            if (m == Method::QLearn) {
                // OFF-POLICY: bootstrap from the best next action, not the one
                // actually taken. This is what lets it learn the optimal path
                // while behaving epsilon-greedily.
                double mx = 0.0;
                if (!done) { mx = Q[n][0]; for (int b = 1; b < kA; ++b) mx = std::max(mx, Q[n][b]); }
                Q[s][a] += cfg.alpha * (r + g.gamma * mx - Q[s][a]);
            } else if (m == Method::Bandit) {
                bandit.observe(static_cast<std::size_t>(s), static_cast<std::size_t>(a), r);
            } else if (m == Method::BanditScaled) {
                bandit.observe(static_cast<std::size_t>(s), static_cast<std::size_t>(a), (r - lo) / span);
            }

            const int na = done ? 0 : pick(n);

            if (m == Method::Sarsa) {
                // ON-POLICY: bootstrap from the action the behaviour policy will
                // actually take, exploration included. It therefore values the
                // risk of an epsilon-slip into the cliff, and prefers a lane
                // further from it.
                const double q2 = done ? 0.0 : Q[n][na];
                Q[s][a] += cfg.alpha * (r + g.gamma * q2 - Q[s][a]);
            }

            if (done) break;
            s = n;
            a = na;
        }
        out.curve[static_cast<std::size_t>(std::min(nwin - 1, ep / cfg.window))] += ret;
    }
    for (double& w : out.curve) w /= static_cast<double>(cfg.window);

    // The greedy policy -- what the learner would do if it had to stop now.
    for (int s = 0; s < kNS; ++s) {
        if (m == Method::Random)   { out.pi[s] = rng.below(kA); continue; }
        if (m == Method::AlwaysUp) { out.pi[s] = 0; continue; }
        if (m == Method::Bandit || m == Method::BanditScaled) {
            out.pi[s] = static_cast<int>(bandit.best(static_cast<std::size_t>(s)));
            if (terminal(g, s)) continue;
            // HOW OFTEN IS THE BANDIT'S ANSWER JUST TIE-BREAKING? In a state
            // where every action returns the same step cost, all four means
            // coincide and Valuer::best() returns the lowest index. Counting
            // this separates "the bandit chose" from "the loop order chose".
            double b1 = -1e300, b2 = -1e300;
            for (int a = 0; a < kA; ++a) {
                const auto e = bandit.estimate(static_cast<std::size_t>(s), static_cast<std::size_t>(a));
                if (e.count == 0) continue;
                if (e.mean > b1)      { b2 = b1; b1 = e.mean; }
                else if (e.mean > b2) { b2 = e.mean; }
            }
            if (b2 > -1e299 && std::fabs(b1 - b2) < 1e-9) ++out.tied;
            continue;
        }
        out.pi[s] = argmax_rand(Q[s], rng);
    }
    return out;
}

// --- AGGREGATION ------------------------------------------------------------

struct Agg {
    std::vector<double> curve;
    std::size_t match = 0,  match_n = 0;      // greedy action inside the optimal set
    std::size_t safe  = 0,  safe_n  = 0;      // greedy action does not step into a trap
    std::size_t goals = 0,  seeds   = 0;      // greedy policy actually reaches the goal
    std::size_t tied  = 0;
    std::size_t steps = 0,  informative = 0;
    double      ret   = 0.0;                  // mean over seeds of V^pi(start)
};

std::array<Agg, kNM> run_all(const Grid& g, const Solution& opt, const Cfg& cfg, int seeds) {
    std::array<Agg, kNM> agg{};
    for (int mi = 0; mi < kNM; ++mi) {
        Agg& A = agg[static_cast<std::size_t>(mi)];
        A.curve.assign(static_cast<std::size_t>(std::max(1, cfg.episodes / cfg.window)), 0.0);
        for (int k = 0; k < seeds; ++k) {
            // Same seed sequence for every method, so the methods face the same
            // luck rather than being separated by it.
            const Run r = train(g, kMethods[mi], cfg, 1000003ull * static_cast<std::uint64_t>(k + 1) + 17ull);
            for (std::size_t w = 0; w < A.curve.size(); ++w) A.curve[w] += r.curve[w];
            for (int s = 0; s < kNS; ++s) {
                if (terminal(g, s)) continue;
                ++A.match_n;
                if (opt.opt[s][r.pi[s]]) ++A.match;
                if (hazard_adjacent(g, s)) {
                    ++A.safe_n;
                    if (g.cell[step_to(s, r.pi[s])] != 'T') ++A.safe;
                }
            }
            A.ret += evaluate(g, r.pi)[g.start];
            if (reaches_goal(g, r.pi)) ++A.goals;
            ++A.seeds;
            A.tied += r.tied;
            A.steps += r.steps;
            A.informative += r.informative;
        }
        for (double& w : A.curve) w /= static_cast<double>(seeds);
        A.ret /= static_cast<double>(seeds);
    }
    return agg;
}

void print_curves(const std::array<Agg, kNM>& agg, const Cfg& cfg, double vstar, double vrand) {
    std::printf("  mean DISCOUNTED return per episode, by training window (behaviour policy,\n"
                "  exploration included -- not the greedy policy):\n\n");
    std::printf("    episodes   ");
    for (int mi = 0; mi < kNM; ++mi) std::printf("| %12s ", kNames[mi]);
    std::printf("\n    -----------");
    for (int mi = 0; mi < kNM; ++mi) std::printf("+--------------");
    std::printf("\n");
    for (std::size_t w = 0; w < agg[0].curve.size(); ++w) {
        std::printf("    %4d-%-6d", static_cast<int>(w) * cfg.window,
                    (static_cast<int>(w) + 1) * cfg.window - 1);
        for (int mi = 0; mi < kNM; ++mi) std::printf("| %12.2f ", agg[static_cast<std::size_t>(mi)].curve[w]);
        std::printf("\n");
    }
    std::printf("\n    optimal V*(start) = %.3f    uniform-random value = %.3f\n", vstar, vrand);
}

void print_final(const std::array<Agg, kNM>& agg, double vstar, double vrand) {
    std::printf("\n  greedy policy after training (exact policy evaluation, no rollout noise):\n\n");
    std::printf("    method         | V^pi(start) | gap closed | policy match       |    95%% CI      | reaches goal\n");
    std::printf("    ---------------+-------------+------------+--------------------+----------------+--------------\n");
    for (int mi = 0; mi < kNM; ++mi) {
        const Agg& A = agg[static_cast<std::size_t>(mi)];
        const double gap = (vstar - vrand) != 0.0 ? 100.0 * (A.ret - vrand) / (vstar - vrand) : 0.0;
        const auto ci = wilson(A.match, A.match_n);
        std::printf("    %-14s | %11.3f | %9.1f%% | %5zu/%-5zu %5.1f%% | [%5.1f%%,%6.1f%%] | %2zu/%-2zu %5.1f%%\n",
                    kNames[mi], A.ret, gap, A.match, A.match_n,
                    A.match_n ? 100.0 * static_cast<double>(A.match) / static_cast<double>(A.match_n) : 0.0,
                    ci.first, ci.second, A.goals, A.seeds,
                    A.seeds ? 100.0 * static_cast<double>(A.goals) / static_cast<double>(A.seeds) : 0.0);
    }
    std::printf("\n    gap closed = (V^pi - V^random) / (V* - V^random). 100%% is optimal, 0%% is chance.\n"
                "    policy match counts (seed x non-terminal state) pairs where the greedy\n"
                "    action is IN the optimal action set, so a genuine tie is never scored wrong.\n");

    // THE SPLIT THAT IS THE WHOLE POINT.
    std::printf("\n  the same policies, split by whether the decision is one-step or multi-step:\n\n");
    std::printf("    method         | trap avoidance (1 step) |    95%% CI      | tie-broken states\n");
    std::printf("    ---------------+-------------------------+----------------+------------------\n");
    for (int mi = 0; mi < kNM; ++mi) {
        const Agg& A = agg[static_cast<std::size_t>(mi)];
        const auto ci = wilson(A.safe, A.safe_n);
        std::printf("    %-14s | %5zu/%-5zu %11.1f%% | [%5.1f%%,%6.1f%%] | %5zu/%-5zu %5.1f%%\n",
                    kNames[mi], A.safe, A.safe_n,
                    A.safe_n ? 100.0 * static_cast<double>(A.safe) / static_cast<double>(A.safe_n) : 0.0,
                    ci.first, ci.second, A.tied, A.match_n,
                    A.match_n ? 100.0 * static_cast<double>(A.tied) / static_cast<double>(A.match_n) : 0.0);
    }
    std::printf("\n    trap avoidance is scored ONLY on squares from which some action falls in --\n"
                "    the states where the question exists. It is a one-step question and needs no\n"
                "    lookahead. tie-broken counts states where the bandit's top two arm means are\n"
                "    numerically equal, so its answer came from Valuer::best()'s lowest-index\n"
                "    rule, not from evidence. READ THE ALWAYS-UP ROW BEFORE READING ANY OTHER:\n"
                "    every trap in this grid is south, so a policy that never goes south scores\n"
                "    100%% trap avoidance without having learned anything at all.\n");
}

// THE POLICIES THEMSELVES, because a percentage does not show WHERE a method is
// wrong and this one is wrong in a shape rather than at random.
void print_two_policies(const Grid& g,
                        const std::vector<int>& a, const char* la,
                        const std::vector<int>& b, const char* lb) {
    const char arrow[kA] = {'^', '>', 'v', '<'};
    auto cellc = [&](const std::vector<int>& p, int s) -> char {
        if (g.cell[s] == 'T') return '#';
        if (g.cell[s] == 'G') return 'G';
        return arrow[p[s]];
    };
    std::printf("\n      %-22s   %s\n", la, lb);
    for (int r = 0; r < kRows; ++r) {
        std::printf("      ");
        for (int c = 0; c < kCols; ++c) std::printf("%c ", cellc(a, r * kCols + c));
        std::printf("       ");
        for (int c = 0; c < kCols; ++c) std::printf("%c ", cellc(b, r * kCols + c));
        std::printf("\n");
    }
}

int failures = 0;
void check(bool cond, const char* what) {
    std::printf("  %s: %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond) ++failures;
}

} // namespace

int main() {
    std::printf("SEQUENTIAL DECISIONS -- what a contextual bandit cannot learn\n");
    std::printf("=============================================================\n\n");

    const Cfg cfg;
    const int kSeeds = 20, kSweepSeeds = 10;

    {
        const Grid g = make_grid(-1.0, 0.95);
        std::printf("  the grid (S start, T trap, G goal; deterministic; walls bounce):\n\n");
        for (int r = 0; r < kRows; ++r) {
            std::printf("      ");
            for (int c = 0; c < kCols; ++c) {
                const int s = r * kCols + c;
                std::printf("%c ", s == g.start ? 'S' : g.cell[s]);
            }
            std::printf("\n");
        }
        std::printf("\n      %d states, %d actions (%s/%s/%s/%s), %d non-terminal\n",
                    kNS, kA, kActName[0], kActName[1], kActName[2], kActName[3],
                    kNS - 7);
        std::printf("      the shortest safe route is 9 moves: up, right x7, down\n");
    }

    // --- SELF-CHECK: is the MDP and its solver actually right? ---------------
    //
    // Everything below is a comparison against value iteration, so if value
    // iteration is wrong every number is wrong in a way that looks like a
    // result. The closed form for the 9-move route is checkable by hand.
    {
        const Grid g = make_grid(-1.0, 0.95);
        const Solution s = value_iteration(g);
        double want = 0.0, d = 1.0;
        for (int i = 0; i < 8; ++i) { want += d * g.step; d *= g.gamma; }
        want += d * g.goal;
        std::printf("\n  harness self-check:\n");
        check(std::fabs(s.V[g.start] - want) < 1e-6,
              "V*(start) equals the hand-computed 9-move route");
        check(reaches_goal(g, s.pi), "the optimal policy reaches the goal");
        check(std::fabs(evaluate(g, s.pi)[g.start] - s.V[g.start]) < 1e-6,
              "policy evaluation of the optimal policy reproduces V*");
    }

    // === PART 1: DENSE REWARD (step cost every move) =========================
    {
        const Grid g = make_grid(-1.0, 0.95);
        const Solution opt = value_iteration(g);
        const double vrand = evaluate_uniform(g);
        std::printf("\n\n  === PART 1: DENSE REWARD ===\n");
        std::printf("  step %.2f, goal %+.1f, trap %+.1f, gamma %.2f, %d episodes x %d seeds,\n"
                    "  alpha %.2f, epsilon %.2f, UCB c %.1f. Mean optimal actions per state: %.2f/4.\n\n",
                    g.step, g.goal, g.trap, g.gamma, cfg.episodes, kSeeds,
                    cfg.alpha, cfg.eps, cfg.ucb_c, opt.mean_opt_actions);
        const auto agg = run_all(g, opt, cfg, kSeeds);
        print_curves(agg, cfg, opt.V[g.start], vrand);
        print_final(agg, opt.V[g.start], vrand);
        print_two_policies(g, opt.pi, "value iteration (optimal)",
                           train(g, Method::Bandit, cfg, 1000020ull).pi, "telos bandit (seed 0)");
        std::printf("\n      The bandit is right on exactly the squares where the answer is one\n"
                    "      step away, and nowhere else. # is a trap, G the goal.\n");

        std::printf("\n    every move pays -1, so the bandit DOES see a reward on every transition\n"
                    "    (%.1f%% of %zu bandit transitions were non-zero). It is not starved of\n"
                    "    signal here; the signal simply does not distinguish the actions.\n",
                    agg[kBanditIdx].steps ? 100.0 * static_cast<double>(agg[kBanditIdx].informative) / static_cast<double>(agg[kBanditIdx].steps) : 0.0,
                    agg[kBanditIdx].steps);
    }

    // === PART 2: DELAYED REWARD (reward only at a terminal square) ===========
    //
    // Step cost zero. Now every non-terminal transition returns exactly 0, and
    // the only information in the environment arrives at the goal or in the
    // cliff. This is the case telos.hpp names: "an act whose payoff arrives
    // three acts later is invisible to it".
    {
        const Grid g = make_grid(0.0, 0.95);
        const Solution opt = value_iteration(g);
        const double vrand = evaluate_uniform(g);
        std::printf("\n\n  === PART 2: DELAYED REWARD ===\n");
        std::printf("  step %.2f (no per-move signal at all), goal %+.1f, trap %+.1f, gamma %.2f.\n"
                    "  Reaching the goal sooner is better only because of the discount; without\n"
                    "  gamma < 1 every route would tie and the optimal policy would be undefined.\n"
                    "  Mean optimal actions per state: %.2f/4.\n\n",
                    g.step, g.goal, g.trap, g.gamma, opt.mean_opt_actions);
        const auto agg = run_all(g, opt, cfg, kSeeds);
        print_curves(agg, cfg, opt.V[g.start], vrand);
        print_final(agg, opt.V[g.start], vrand);
        print_two_policies(g, opt.pi, "value iteration (optimal)",
                           train(g, Method::Bandit, cfg, 1000020ull).pi, "telos bandit (seed 0)");

        std::printf("\n    transitions carrying ANY reward: bandit %.2f%% of %zu, Q-learning %.2f%% of %zu.\n",
                    agg[kBanditIdx].steps ? 100.0 * static_cast<double>(agg[kBanditIdx].informative) / static_cast<double>(agg[kBanditIdx].steps) : 0.0,
                    agg[kBanditIdx].steps,
                    agg[kQIdx].steps ? 100.0 * static_cast<double>(agg[kQIdx].informative) / static_cast<double>(agg[kQIdx].steps) : 0.0,
                    agg[kQIdx].steps);
        std::printf("    Those two rates are NOT comparable as evidence -- Q-learning's episodes\n"
                    "    end in nine moves once it has learned, so its terminal reward is a larger\n"
                    "    share of a much shorter stream, while the bandit wanders for the full 100.\n"
                    "    What matters is that in this variant only a TERMINAL transition carries any\n"
                    "    reward at all, and the decision that earned it was taken eight moves\n"
                    "    earlier. Q-learning BOOTSTRAPS: it treats its own estimate of the next\n"
                    "    state as if it were reward, which drags the goal's value backwards along\n"
                    "    the path one move per visit. The bandit has nowhere to put that value --\n"
                    "    its table is indexed by (context, action) and has no slot for a successor.\n");
    }

    // === PART 3: DISCOUNT SENSITIVITY ========================================
    //
    // A result that holds at one gamma is not a result. gamma also happens to
    // be the exact dial between "this is a bandit problem" and "this is an MDP":
    // at gamma = 0 the optimal policy IS argmax of immediate reward, which is
    // precisely what telos::Valuer computes.
    {
        std::printf("\n\n  === PART 3: SENSITIVITY TO THE DISCOUNT FACTOR ===\n");
        std::printf("  dense reward (step -1), %d seeds. gamma = 0 makes the MDP a bandit by\n"
                    "  definition, which is the honest place to look for the bandit's ceiling.\n\n",
                    kSweepSeeds);
        std::printf("    gamma | |A*|/4 |  span  |");
        for (int mi = 0; mi < kNM; ++mi) std::printf(" %12s |", kNames[mi]);
        std::printf("\n    ------+-------+--------+");
        for (int mi = 0; mi < kNM; ++mi) std::printf("--------------+");
        std::printf("\n");
        for (const double gam : {0.0, 0.50, 0.80, 0.90, 0.95, 0.99}) {
            const Grid g = make_grid(-1.0, gam);
            const Solution opt = value_iteration(g);
            const double vrand = evaluate_uniform(g);
            const auto agg = run_all(g, opt, cfg, kSweepSeeds);
            std::printf("    %5.2f | %5.2f | %6.2f |", gam, opt.mean_opt_actions,
                        opt.V[g.start] - vrand);
            for (int mi = 0; mi < kNM; ++mi) {
                const Agg& A = agg[static_cast<std::size_t>(mi)];
                const double denom = opt.V[g.start] - vrand;
                const double gap = std::fabs(denom) > 1e-9 ? 100.0 * (A.ret - vrand) / denom : 100.0;
                std::printf(" %4.0f%% / %4.0f%% |",
                            100.0 * static_cast<double>(A.match) / static_cast<double>(A.match_n), gap);
            }
            std::printf("\n");
        }
        std::printf("\n    cells are POLICY-MATCH %% / GAP-CLOSED %%, over %d seeds x 25 states.\n"
                    "\n"
                    "    BOTH COLUMNS HAVE A BLIND SPOT AND THEY ARE DIFFERENT ONES, which is why\n"
                    "    both are printed. |A*|/4 is the mean size of the optimal action set: when\n"
                    "    it is large the MATCH metric saturates -- at gamma 0 the average state has\n"
                    "    3.6 of its 4 actions optimal and even uniform random scores 91%%. GAP has\n"
                    "    the opposite failure: at gamma 0.50 the effective horizon is about two\n"
                    "    moves, so anything that does not fall off the cliff in the next two moves\n"
                    "    is nearly optimal, and the bandit reads 100%% GAP while reading 8%% MATCH.\n"
                    "    SPAN (V* - V^random) is printed so that saturation cannot be mistaken for\n"
                    "    a small denominator: it is 34.31 at gamma 0.50, not small at all.\n"
                    "\n"
                    "    gamma = 0 is not an incidental corner. It is the definition of a bandit\n"
                    "    problem -- the optimal policy IS argmax of immediate reward -- and the\n"
                    "    bandit is exactly optimal there. It collapses to 8%% the moment gamma\n"
                    "    leaves zero and stays at 8%% across the whole range. That flatness is the\n"
                    "    result: this is not a method that degrades with horizon, it is a method\n"
                    "    that does not represent horizon at all. gamma = 1.0 is omitted: with no\n"
                    "    discount, policy evaluation of a looping policy does not converge.\n",
                    kSweepSeeds);
    }

    // === PART 4: STEP-COST SENSITIVITY =======================================
    {
        std::printf("\n\n  === PART 4: SENSITIVITY TO THE STEP COST ===\n");
        std::printf("  gamma 0.95, %d seeds. Step cost 0 is the delayed variant; more negative\n"
                    "  makes wandering expensive and shortens the optimal route's advantage.\n\n",
                    kSweepSeeds);
        std::printf("    step  |");
        for (int mi = 0; mi < kNM; ++mi) std::printf(" %12s |", kNames[mi]);
        std::printf("\n    ------+");
        for (int mi = 0; mi < kNM; ++mi) std::printf("--------------+");
        std::printf("\n");
        for (const double sc : {0.0, -0.1, -1.0, -5.0}) {
            const Grid g = make_grid(sc, 0.95);
            const Solution opt = value_iteration(g);
            const double vrand = evaluate_uniform(g);
            const auto agg = run_all(g, opt, cfg, kSweepSeeds);
            std::printf("    %5.1f |", sc);
            for (int mi = 0; mi < kNM; ++mi) {
                const Agg& A = agg[static_cast<std::size_t>(mi)];
                const double gap = 100.0 * (A.ret - vrand) / (opt.V[g.start] - vrand);
                std::printf(" %10.1f%% |", gap);
            }
            std::printf("\n");
        }
        std::printf("\n    cells are gap closed between the uniform-random value and V*, both\n"
                    "    recomputed for that step cost. 100%% is optimal, 0%% is chance, and a\n"
                    "    negative number means worse than acting at random.\n");
    }

    std::printf("\n\n  === WHAT THIS HARNESS CANNOT SEE ===\n"
                "    * The MDP is deterministic, tabular, fully observed, 32 states. TD has\n"
                "      every structural advantage. Nothing here transfers to approximation.\n"
                "    * The bandit was given the FULL STATE as context -- 32 contexts. The\n"
                "      Volition gives it the dominant drive, about six. This setting is\n"
                "      generous to the bandit, and it still cannot represent the problem.\n"
                "    * Value iteration was handed the transition model; the TD learners were\n"
                "      not. VI is the reference, not a rival, and the comparison says nothing\n"
                "      about what learning a model would cost.\n"
                "    * Policy match counts states, not visits. Greedy return is the check on\n"
                "      it: a method can be right in corners nobody walks through.\n"
                "    * Whether Khora's ACTS form a Markov chain over an observable state is\n"
                "      not tested here and is not obvious. This measures what temporal credit\n"
                "      assignment buys when the problem is sequential -- not that Khora's are.\n");

    std::printf("\n%s\n", failures == 0 ? "SELF-CHECK: ALL PASS" : "SELF-CHECK: FAILURES");
    return failures == 0 ? 0 : 1;
}
