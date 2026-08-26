// CAN THIS SYSTEM PLAY A GAME? -- ADVERSARIAL SEARCH, MEASURED
//
// An audit of the tree found no adversarial search anywhere in Khora: no
// minimax, no alpha-beta, no MCTS, no game tree, no notion of an opponent. The
// Ligature has a beam-search planner (plan_to) that chains backward over CAUSES
// scored by the weakest link, and the Lattice has similarity search, and Techne
// searches program space -- but every one of those searches a space that does
// not push back. Nothing in ninety-odd tools models a second agent choosing the
// worst continuation for you. That is not a missing optimisation; it is a
// missing category. Most of the field's landmark results are adversarial search
// results, and Khora had zero lines of it.
//
// So this bench builds it from nothing, self-contained in one file, and prices
// it. It is a BENCH, not an organ: nothing here is wired into Khora. It shows
// that the capability can be written and measured. It does not show the system
// can use one.
//
// THE GAME: Connect-4 on a reduced board. Chosen over Nim, the other obvious
// candidate, for two reasons. Nim has a closed-form optimal strategy (XOR the
// heaps), which makes "optimal play" free but also makes the game a lookup
// table -- there is no positional judgement for a heuristic to be good or bad
// at, so the mandatory greedy baseline would be either perfect or nonsense.
// Connect-4 has a natural one-ply heuristic (win if you can, block if you must,
// else take the centre) that is genuinely mediocre, which is what a baseline
// should be.
//
// TWO BOARD SIZES, and the reason is the third measurement:
//
//   6 wide x 5 high (30 cells) carries the head-to-head play, the sweeps, and
//   the cost table. It is NOT solvable here -- the legal position count is of
//   order 10^9 -- so every number on this board is strength RELATIVE to the
//   other agents in the table, never against truth.
//
//   5 wide x 4 high (20 cells) is solved exactly, by memoised negamax into a
//   directly-addressed table. The position key packs into W*(H+1) = 25 bits, so
//   the whole transposition table is a flat std::vector<int8_t> of 2^25 entries
//   (33 MB) with no hashing at all. That gives a perfect player, and it gives
//   the measurement that actually matters: over a COMMON set of sampled
//   positions, what fraction of the time does each agent choose a move that
//   preserves the game-theoretic value. Win rate against a weak opponent
//   saturates; optimal-move agreement does not.
//
// WHAT IT FOUND, at the defaults and reproduced at three times the sample:
//
//   Win rate against random SATURATES. Every agent above uniform random scores
//   99-100% against it, including the one-ply heuristic. That table is worth
//   printing only to show it is worthless as a discriminator.
//
//   Both searches beat the greedy baseline decisively, so neither is decoration.
//   Alpha-beta d=2 already scores 99.2% against it at a cost of 28 nodes and
//   3 us a move. MCTS needs 1000 rollouts and 380 us to match that.
//
//   ALPHA-BETA IS NOT MONOTONE IN DEPTH. Against exact play on the solved board
//   the draw rate over depths 2/4/6/8/12 runs 27.7%, 32.3%, 18.7%, 36.0%, 54.3%
//   -- depth 6 is worse than depth 2. It reproduced at 100 and at 300 games and
//   it is not noise. The d=8 and d=12 control rows converge toward exact play,
//   which is what says the SEARCH is right and the EVALUATION is wrong: a window
//   holding three of my stones scores 16 whether or not the fourth cell can
//   actually be reached, and a deeper search optimises harder against that lie.
//   No weight was touched in response. Tuning the eval until depth 6 looks good
//   would have deleted the finding.
//
//   THE RANKING IS NOT TRANSITIVE. Depth 6 is the strongest alpha-beta against
//   10000-rollout MCTS and the weakest against the greedy heuristic and against
//   exact play. Which depth is "best" depends entirely on the opponent, and any
//   single head-to-head table implies an ordering it cannot support.
//
//   ONE CELL OF THE SWEEP IS NOT A STRENGTH RESULT. uct-10000 with greedy
//   rollouts against alpha-beta d=6 draws 83 of 100 games, where the same MCTS
//   against d=2 and d=4 is decisive three quarters of the time and the same d=6
//   against uniform-rollout MCTS is decisive 93% of the time. The draw rate is a
//   property of the PAIR, not of either agent, and a score of 56.5% built out of
//   83 draws does not mean what a score of 75.5% built out of 60 wins means.
//
// WHAT THIS HARNESS CANNOT SEE is listed in full at the bottom of the output,
// where it can be read next to the numbers. The short version: the 6x5 numbers
// have no ground truth, the minimax evaluation function is a hand-written knob,
// the UCT constant is fixed at its textbook value and never swept, and node
// counts are not the same unit across methods.

#include <algorithm>
#include <bit>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kMaxW = 8;

// --- THE GAME ---------------------------------------------------------------
//
// Standard Connect-4 bitboard, with runtime dimensions so the same code runs
// the 6x5 arena and the 5x4 solved board. Each column occupies H+1 bits: H
// playable rows plus one sentinel row that is never filled. The sentinel is
// what stops a vertical or diagonal run from wrapping from the top of one
// column into the bottom of the next, which is why the whole win test is four
// shift-and-mask pairs with no bounds checking anywhere.
struct C4 {
    int           W = 6, H = 5;
    std::uint64_t pos  = 0;   // stones of the side to move
    std::uint64_t mask = 0;   // stones of both sides
    std::uint64_t bottom = 0; // one bit at the base of each column
    int           moves = 0;

    C4(int w, int h) : W(w), H(h) {
        for (int c = 0; c < W; ++c) bottom |= 1ull << (c * (H + 1));
    }

    std::uint64_t col_mask(int c) const { return ((1ull << H) - 1) << (c * (H + 1)); }
    std::uint64_t top_bit(int c)  const { return 1ull << (c * (H + 1) + H - 1); }
    bool          can_play(int c) const { return (mask & top_bit(c)) == 0; }

    // The bit a stone dropped in column c would land on. Adding the column's
    // base bit to the mask carries up through the contiguous run of stones in
    // that column and lands exactly on the first empty cell; the carry cannot
    // escape the column because the sentinel row absorbs it.
    std::uint64_t drop(int c) const {
        return (mask + (1ull << (c * (H + 1)))) & col_mask(c);
    }

    std::uint64_t opp()  const { return pos ^ mask; }
    bool          full() const { return moves == W * H; }

    void play(int c) { const std::uint64_t m = drop(c); pos ^= mask; mask |= m; ++moves; }

    bool aligned(std::uint64_t p) const {
        const int d[4] = {1, H, H + 1, H + 2};   // vertical, two diagonals, horizontal
        for (int i = 0; i < 4; ++i) {
            const std::uint64_t m = p & (p >> d[i]);
            if (m & (m >> (2 * d[i]))) return true;
        }
        return false;
    }
    bool wins_with(int c) const { return aligned(pos | drop(c)); }

    // The key used by the exact solver. position + mask + bottom is the classic
    // Connect-4 encoding: it is injective over legal positions and, because the
    // carries stay inside their columns, it fits in exactly W*(H+1) bits.
    std::uint64_t key() const { return pos + mask + bottom; }
};

struct Moves { int c[kMaxW]; int n = 0; };

Moves legal(const C4& g) {
    Moves m;
    for (int c = 0; c < g.W; ++c) if (g.can_play(c)) m.c[m.n++] = c;
    return m;
}

// How far column c is from the centre, doubled so odd and even widths share
// one formula. Used for the greedy baseline's tiebreak and for move ordering.
int centre_dist(int c, int W) { return std::abs(2 * c - (W - 1)); }

// --- WHAT EACH MOVE COST ----------------------------------------------------
//
// nodes and nanoseconds are accumulated per agent so that strength can be
// divided by price. The two node counters are NOT the same unit and the report
// says so: a minimax node is one negamax call (move generation, a win scan, and
// possibly a static evaluation); an MCTS node is one position touched during
// selection or rollout, which is far cheaper. They are comparable as orders of
// magnitude and not as a ratio.
struct Stats {
    std::uint64_t nodes = 0;
    std::uint64_t ns    = 0;
    std::uint64_t plies = 0;
    void add_time(Clock::time_point t0) {
        ns += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count());
        ++plies;
    }
};

using Agent = std::function<int(const C4&, std::mt19937&, Stats&)>;
struct Player { std::string name; Agent fn; };

// --- BASELINE 1: UNIFORM RANDOM ---------------------------------------------
int random_move(const C4& g, std::mt19937& rng) {
    const Moves m = legal(g);
    return m.c[rng() % static_cast<unsigned>(m.n)];
}

// --- BASELINE 2: GREEDY ONE PLY ---------------------------------------------
//
// Win now if a move wins now; else block a move that would win for the opponent
// if they were to move; else take the most central legal column, breaking ties
// at random. This is one ply of lookahead and no more -- it will happily play a
// move that hands the opponent a win on the square above, which is exactly the
// failure that a real search is supposed to fix. That is the point of having
// it: it is the bar that alpha-beta and MCTS have to clear to have earned their
// node counts.
int greedy_move(const C4& g, std::mt19937& rng) {
    const Moves m = legal(g);
    int cand[kMaxW], n = 0;

    for (int i = 0; i < m.n; ++i) if (g.wins_with(m.c[i])) cand[n++] = m.c[i];
    if (n) return cand[rng() % static_cast<unsigned>(n)];

    C4 as_opp = g;                      // same board, opponent hypothetically to move
    as_opp.pos = g.opp();
    for (int i = 0; i < m.n; ++i) if (as_opp.wins_with(m.c[i])) cand[n++] = m.c[i];
    if (n) return cand[rng() % static_cast<unsigned>(n)];

    int best = INT_MAX;
    for (int i = 0; i < m.n; ++i) {
        const int d = centre_dist(m.c[i], g.W);
        if (d < best) { best = d; n = 0; cand[n++] = m.c[i]; }
        else if (d == best) cand[n++] = m.c[i];
    }
    return cand[rng() % static_cast<unsigned>(n)];
}

// --- MINIMAX WITH ALPHA-BETA ------------------------------------------------
//
// Negamax, fail-soft, with a depth limit and a static evaluation at the
// horizon. Three deliberate choices, each of which shows up in the numbers:
//
//   The immediate-win scan runs BEFORE the depth cutoff, so a depth-d agent
//   sees its own wins d+1 plies out and the opponent's d plies out. Without it
//   a depth-2 agent would walk into losses that a one-ply heuristic catches.
//
//   Terminal wins score kWin - moves, so a faster win outranks a slower one and
//   a later loss outranks an earlier one. Otherwise a won position can be
//   shuffled indefinitely because every winning line scores the same.
//
//   The ROOT searches every child with a full window rather than narrowing
//   alpha as it goes. Fail-soft alpha-beta returns bounds, not values, for
//   children that fail low, so ties cannot be identified from a narrowed
//   search -- and without random tiebreaking two deterministic agents replay
//   the same two games N times and the whole match is one sample. A real
//   engine would narrow the root and be roughly twice as fast; the node counts
//   below include that cost and are honest about it.
struct Minimax {
    static constexpr int kWin = 100000;
    static constexpr int kInf = 1 << 20;

    int                        depth;
    std::vector<std::uint64_t> win_windows;   // every 4-in-a-row cell set on this board
    std::uint64_t              centre = 0;    // the one or two central columns
    std::vector<int>           order;         // columns, centre outward

    Minimax(int W, int H, int d) : depth(d) {
        auto bit = [&](int c, int r) { return 1ull << (c * (H + 1) + r); };
        for (int c = 0; c < W; ++c) {
            for (int r = 0; r < H; ++r) {
                if (r + 3 < H)               win_windows.push_back(bit(c, r) | bit(c, r + 1) | bit(c, r + 2) | bit(c, r + 3));
                if (c + 3 < W)               win_windows.push_back(bit(c, r) | bit(c + 1, r) | bit(c + 2, r) | bit(c + 3, r));
                if (c + 3 < W && r + 3 < H)  win_windows.push_back(bit(c, r) | bit(c + 1, r + 1) | bit(c + 2, r + 2) | bit(c + 3, r + 3));
                if (c + 3 < W && r - 3 >= 0) win_windows.push_back(bit(c, r) | bit(c + 1, r - 1) | bit(c + 2, r - 2) | bit(c + 3, r - 3));
            }
        }
        const int mind = (W % 2) ? 0 : 1;
        for (int c = 0; c < W; ++c) if (centre_dist(c, W) <= mind) centre |= ((1ull << H) - 1) << (c * (H + 1));
        for (int c = 0; c < W; ++c) order.push_back(c);
        std::stable_sort(order.begin(), order.end(),
                         [&](int a, int b) { return centre_dist(a, W) < centre_dist(b, W); });
    }

    // THE EVALUATION IS A KNOB. A window with stones of both colours in it can
    // never be completed by either, so it is worth nothing; an uncontested
    // window is worth a superlinear amount in the number of stones already in
    // it, and central stones are worth a little on their own because they take
    // part in more windows. The weights 1/4/16 and the centre bonus of 3 were
    // written once and never tuned. Every minimax row in this report moves if
    // they change, and there is no ground truth on the 6x5 board to say which
    // setting is right -- which is precisely why the 5x4 optimality table
    // exists.
    int evaluate(const C4& g) const {
        static const int wt[4] = {0, 1, 4, 16};
        const std::uint64_t me = g.pos, you = g.opp();
        int s = 0;
        for (const std::uint64_t w : win_windows) {
            const int a = std::popcount(me & w), b = std::popcount(you & w);
            if (a && b) continue;
            s += wt[a] - wt[b];
        }
        s += 3 * (std::popcount(me & centre) - std::popcount(you & centre));
        return s;
    }

    Moves ordered(const C4& g) const {
        Moves m;
        for (const int c : order) if (g.can_play(c)) m.c[m.n++] = c;
        return m;
    }

    int negamax(const C4& g, int d, int a, int b, Stats& st) const {
        ++st.nodes;
        if (g.full()) return 0;
        const Moves m = ordered(g);
        for (int i = 0; i < m.n; ++i) if (g.wins_with(m.c[i])) return kWin - g.moves;
        if (d <= 0) return evaluate(g);
        int best = -kInf;
        for (int i = 0; i < m.n; ++i) {
            C4 ch = g;
            ch.play(m.c[i]);
            const int v = -negamax(ch, d - 1, -b, -a, st);
            if (v > best) best = v;
            if (best > a) a = best;
            if (a >= b) break;
        }
        return best;
    }

    int choose(const C4& g, std::mt19937& rng, Stats& st) const {
        const auto t0 = Clock::now();
        const Moves m = ordered(g);
        int best = INT_MIN, cand[kMaxW], n = 0;
        for (int i = 0; i < m.n; ++i) {
            int v;
            if (g.wins_with(m.c[i])) { ++st.nodes; v = kWin - g.moves; }
            else { C4 ch = g; ch.play(m.c[i]); v = -negamax(ch, depth - 1, -kInf, kInf, st); }
            if (v > best) { best = v; n = 0; cand[n++] = m.c[i]; }
            else if (v == best) cand[n++] = m.c[i];
        }
        st.add_time(t0);
        return cand[rng() % static_cast<unsigned>(n)];
    }
};

// --- MCTS WITH UCT ----------------------------------------------------------
//
// Textbook UCT: descend by argmax(q/n + C*sqrt(ln N / n)), expand one untried
// move, play out, back up. C is sqrt(2), the value the formula is derived for
// when rewards live in [0,1], and it is NOT swept -- see the caveats.
//
// TWO ROLLOUT POLICIES, and the second one is not a tuning trick. Uniform
// random playouts are the honest textbook version, and on Connect-4 they are
// known to be bad: a uniform rollout walks past a win-in-one roughly as often
// as it takes it, so the value estimate at the leaf is mostly noise about
// tactics. Comparing uniform-rollout MCTS against an alpha-beta that carries a
// hand-written evaluation function measures the two rollout policies as much as
// it measures the two search algorithms. So the second variant uses exactly the
// greedy baseline as its playout policy -- the SAME one-ply knowledge the
// mandatory baseline has, no more -- and both variants are reported. If MCTS
// loses, the report should say whether it lost as an algorithm or lost as a
// rollout policy.
//
// Each node stores its own board rather than replaying moves from the root.
// That is 48 bytes a node against a few hundred nanoseconds of replay per
// descent, and the tree never exceeds `budget` nodes.
struct MCTS {
    int    budget;
    bool   greedy_rollout;
    double C = 1.41421356;

    struct Node {
        C4               g;
        int              parent   = -1;
        double           q        = 0;   // from the perspective of whoever MOVED INTO this node
        int              n        = 0;
        bool             terminal = false;
        int              winner   = -1;  // 0, 1, or -1 for a draw
        Moves            untried;
        std::vector<int> kids;
        int              move = -1;
        explicit Node(const C4& s) : g(s) {}
    };

    MCTS(int b, bool greedy) : budget(b), greedy_rollout(greedy) {}

    int rollout(C4 g, std::mt19937& rng, Stats& st) const {
        for (;;) {
            if (g.full()) return -1;
            const int m = greedy_rollout ? greedy_move(g, rng) : random_move(g, rng);
            ++st.nodes;
            if (g.wins_with(m)) return g.moves % 2;
            g.play(m);
        }
    }

    int choose(const C4& root_state, std::mt19937& rng, Stats& st) const {
        const auto t0 = Clock::now();
        std::vector<Node> t;
        t.reserve(static_cast<std::size_t>(budget) + 2);
        t.emplace_back(root_state);
        t[0].untried = legal(root_state);

        for (int it = 0; it < budget; ++it) {
            int v = 0;
            // SELECT. Descend while the node is fully expanded and not terminal.
            while (!t[v].terminal && t[v].untried.n == 0 && !t[v].kids.empty()) {
                ++st.nodes;
                double bestv = -1e300;
                int    bestk = t[v].kids[0];
                const double logN = std::log(static_cast<double>(t[v].n));
                for (const int k : t[v].kids) {
                    const double u = t[k].q / t[k].n + C * std::sqrt(logN / t[k].n);
                    if (u > bestv) { bestv = u; bestk = k; }
                }
                v = bestk;
            }
            // EXPAND one untried move.
            if (!t[v].terminal && t[v].untried.n > 0) {
                const int pick = static_cast<int>(rng() % static_cast<unsigned>(t[v].untried.n));
                const int mv   = t[v].untried.c[pick];
                t[v].untried.c[pick] = t[v].untried.c[--t[v].untried.n];
                const bool won = t[v].g.wins_with(mv);
                const int  mover = t[v].g.moves % 2;
                C4 ch = t[v].g;
                ch.play(mv);
                t.emplace_back(ch);
                const int id = static_cast<int>(t.size()) - 1;
                t[id].parent = v;
                t[id].move   = mv;
                if (won)            { t[id].terminal = true;  t[id].winner = mover; }
                else if (ch.full()) { t[id].terminal = true;  t[id].winner = -1; }
                else                { t[id].untried  = legal(ch); }
                t[v].kids.push_back(id);
                v = id;
                ++st.nodes;
            }
            // PLAY OUT and BACK UP. q is stored from the point of view of the
            // player who moved into each node, which is the player choosing
            // among that node's siblings one level up -- so the UCT formula can
            // read q/n directly with no sign juggling.
            const int winner = t[v].terminal ? t[v].winner : rollout(t[v].g, rng, st);
            const double s0 = (winner < 0) ? 0.5 : (winner == 0 ? 1.0 : 0.0);
            for (int u = v; u != -1; u = t[u].parent) {
                ++t[u].n;
                const int mover = (t[u].g.moves + 1) % 2;
                t[u].q += (mover == 0) ? s0 : 1.0 - s0;
            }
        }

        // ROBUST CHILD: most visits, not best mean. A child with three visits
        // and a 100% mean is noise; the visit count is the quantity UCT
        // actually concentrated.
        int cand[kMaxW], n = 0, bestn = -1;
        for (const int k : t[0].kids) {
            if (t[k].n > bestn) { bestn = t[k].n; n = 0; cand[n++] = t[k].move; }
            else if (t[k].n == bestn && n < kMaxW) cand[n++] = t[k].move;
        }
        st.add_time(t0);
        if (n == 0) return random_move(root_state, rng);   // budget 0, cannot happen here
        return cand[rng() % static_cast<unsigned>(n)];
    }
};

// --- THE EXACT SOLVER -------------------------------------------------------
//
// Win/draw/loss only, no distance-to-mate. That is the right notion for
// "optimal move": a move is optimal if it does not throw away the
// game-theoretic result. A won position played WDL-optimally still terminates
// in a win, because Connect-4 is finite and every move fills a cell.
//
// The table is directly addressed, not hashed. On 5x4 the key occupies 25 bits,
// so 2^25 int8_t entries -- 33 MB -- cover every possible key with no
// collisions, no probing, and no hash function. This is the whole reason the
// solved board is 5 wide: at 6x4 the key needs 30 bits (1 GB) and at 6x5 it
// needs 36.
struct Solver {
    std::vector<std::int8_t> tt;   // 0 unknown, otherwise value + 2
    std::uint64_t            solved = 0;

    Solver(int W, int H) : tt(std::size_t(1) << (W * (H + 1)), 0) {}

    int solve(const C4& g) {
        const std::uint64_t k = g.key();
        if (tt[k]) return tt[k] - 2;
        int best;
        if (g.full()) {
            best = 0;
        } else {
            best = -1;
            const Moves m = legal(g);
            for (int i = 0; i < m.n && best < 1; ++i) {
                if (g.wins_with(m.c[i])) { best = 1; break; }
                C4 ch = g;
                ch.play(m.c[i]);
                best = std::max(best, -solve(ch));
            }
        }
        tt[k] = static_cast<std::int8_t>(best + 2);
        ++solved;
        return best;
    }

    // The exact value of each legal move, from the mover's point of view.
    void move_values(const C4& g, int* out, Moves& m) {
        m = legal(g);
        for (int i = 0; i < m.n; ++i) {
            if (g.wins_with(m.c[i])) { out[i] = 1; continue; }
            C4 ch = g;
            ch.play(m.c[i]);
            out[i] = -solve(ch);
        }
    }
};

// --- STATISTICS -------------------------------------------------------------
//
// The same 95% Wilson interval the rest of the benches use. A game result is
// trinomial, and (wins + draws/2)/n is therefore not a binomial proportion and
// gets no interval; the two intervals printed beside it -- on the win share and
// on the loss share -- are proper binomial ones and bound the outcomes that
// decide the match.
std::pair<double, double> wilson(std::size_t hits, std::size_t n) {
    if (n == 0) return {0.0, 0.0};
    const double z = 1.96, ph = static_cast<double>(hits) / static_cast<double>(n);
    const double d = 1.0 + z * z / static_cast<double>(n);
    const double c = ph + z * z / (2.0 * static_cast<double>(n));
    const double m = z * std::sqrt(ph * (1.0 - ph) / static_cast<double>(n)
                                   + z * z / (4.0 * static_cast<double>(n) * static_cast<double>(n)));
    return {std::max(0.0, 100.0 * (c - m) / d), std::min(100.0, 100.0 * (c + m) / d)};
}

// --- PLAYING THE GAMES ------------------------------------------------------
//
// A bench that does not alternate the first move measures the first-move
// advantage. Game g has agent A moving first when g is even. Every result is
// recorded from A's point of view.
//
// Every agent randomises its tiebreaks and every game gets its own seed, so a
// match between two deterministic-looking agents is N distinct games rather
// than one game repeated N times. This is not decoration: without it the entire
// minimax-vs-greedy row would be two samples.
struct Match { std::size_t w = 0, d = 0, l = 0; Stats a, b; };

Match play_match(int W, int H, const Agent& A, const Agent& B,
                 int games, std::uint64_t seed) {
    Match r;
    for (int gi = 0; gi < games; ++gi) {
        std::mt19937 rng(static_cast<std::uint32_t>(seed + 7919ull * static_cast<std::uint64_t>(gi)));
        C4 g(W, H);
        const int a_first = (gi % 2 == 0) ? 0 : 1;   // which side-to-move index is A
        int result = 0;                              // +1 A wins, -1 B wins, 0 draw
        for (;;) {
            if (g.full()) break;
            const bool a_turn = (g.moves % 2) == a_first;
            const int  mv     = a_turn ? A(g, rng, r.a) : B(g, rng, r.b);
            if (g.wins_with(mv)) { result = a_turn ? 1 : -1; break; }
            g.play(mv);
        }
        if (result > 0) ++r.w; else if (result < 0) ++r.l; else ++r.d;
    }
    return r;
}

void print_match_row(const char* label, const Match& m, int games) {
    const std::size_t n = static_cast<std::size_t>(games);
    const auto cw = wilson(m.w, n);
    const auto cl = wilson(m.l, n);
    std::printf("  %-22s | %4zu %4zu %4zu | %6.1f%% | %5.1f%% [%5.1f,%5.1f] | %5.1f%% [%5.1f,%5.1f]\n",
                label, m.w, m.d, m.l,
                100.0 * (static_cast<double>(m.w) + 0.5 * static_cast<double>(m.d)) / static_cast<double>(n),
                100.0 * static_cast<double>(m.w) / static_cast<double>(n), cw.first, cw.second,
                100.0 * static_cast<double>(m.l) / static_cast<double>(n), cl.first, cl.second);
}

void print_match_header() {
    std::printf("  agent                  |    W    D    L |  score | win%%    95%% CI       | loss%%   95%% CI\n");
    std::printf("  -----------------------+----------------+--------+----------------------+---------------------\n");
}

// --- A COMMON SET OF POSITIONS ----------------------------------------------
//
// Agreement rates are only comparable if every agent is asked about the same
// positions. Sampling each agent's OWN games would give each of them a
// different exam -- a strong agent reaches different positions than a weak one,
// and the difference in difficulty would be folded into the score.
//
// Positions come from epsilon-greedy self-play: mostly the one-ply heuristic,
// with a fixed chance of a uniform move, so the set is varied without being
// absurd. Terminal positions and positions with a single legal move are
// dropped -- there is no choice to get right in either. Deduplicated by key.
std::vector<C4> sample_positions(int W, int H, std::size_t want, std::uint64_t seed, double eps) {
    std::vector<C4> out;
    std::unordered_set<std::uint64_t> seen;
    std::mt19937 rng(static_cast<std::uint32_t>(seed));
    std::uniform_real_distribution<double> u(0.0, 1.0);
    for (int game = 0; out.size() < want && game < 100000; ++game) {
        C4 g(W, H);
        for (;;) {
            if (g.full()) break;
            const Moves m = legal(g);
            if (m.n >= 2 && seen.insert(g.key()).second && out.size() < want) out.push_back(g);
            const int mv = (u(rng) < eps) ? random_move(g, rng) : greedy_move(g, rng);
            if (g.wins_with(mv)) break;
            g.play(mv);
        }
    }
    return out;
}

// --- WHAT EACH MOVE COSTS ---------------------------------------------------
void cost_table(const std::vector<C4>& ps, const std::vector<Player>& players, std::uint64_t seed) {
    std::printf("  agent                  | positions |    nodes/move |   us/move | nodes/us\n");
    std::printf("  -----------------------+-----------+---------------+-----------+---------\n");
    for (const auto& p : players) {
        Stats st;
        std::mt19937 rng(static_cast<std::uint32_t>(seed));
        for (const auto& g : ps) (void)p.fn(g, rng, st);
        const double n = static_cast<double>(ps.size());
        const double us = static_cast<double>(st.ns) / 1000.0 / n;
        std::printf("  %-22s | %9zu | %13.1f | %9.1f | %7.2f\n",
                    p.name.c_str(), ps.size(), static_cast<double>(st.nodes) / n, us,
                    us > 0.0 ? (static_cast<double>(st.nodes) / n) / us : 0.0);
    }
}

} // namespace

int main(int argc, char** argv) {
    const int games  = (argc > 1) ? std::atoi(argv[1]) : 100;
    const int sweepn = (argc > 2) ? std::atoi(argv[2]) : 50;
    const int nposs  = (argc > 3) ? std::atoi(argv[3]) : 200;

    const int AW = 6, AH = 5;   // the arena, unsolved
    const int SW = 5, SH = 4;   // the solved board

    std::printf("ADVERSARIAL SEARCH, BUILT AND PRICED\n");
    std::printf("  arena       : Connect-4 %dx%d (%d cells), NOT solved -- order 10^9 legal positions\n",
                AW, AH, AW * AH);
    std::printf("  solved board: Connect-4 %dx%d (%d cells), solved exactly below\n", SW, SH, SW * SH);
    std::printf("  %d games per head-to-head, %d per sweep cell, %d sampled positions, first move alternated\n",
                games, sweepn, nposs);

    // Agents on the arena. Depths 2/4/6 and budgets 100/1000/10000 as specified.
    auto mk_mm = [&](int W, int H, int d) {
        Minimax m(W, H, d);
        return Agent([m](const C4& g, std::mt19937& r, Stats& s) { return m.choose(g, r, s); });
    };
    auto mk_uct = [](int b, bool greedy) {
        MCTS m(b, greedy);
        return Agent([m](const C4& g, std::mt19937& r, Stats& s) { return m.choose(g, r, s); });
    };
    const Agent rnd = [](const C4& g, std::mt19937& r, Stats& s) {
        const auto t0 = Clock::now();
        s.nodes += static_cast<std::uint64_t>(legal(g).n);
        const int m = random_move(g, r);
        s.add_time(t0);
        return m;
    };
    const Agent grd = [](const C4& g, std::mt19937& r, Stats& s) {
        const auto t0 = Clock::now();
        s.nodes += static_cast<std::uint64_t>(2 * legal(g).n);
        const int m = greedy_move(g, r);
        s.add_time(t0);
        return m;
    };

    std::vector<Player> arena = {
        {"random",              rnd},
        {"greedy 1-ply",        grd},
        {"alphabeta d=2",       mk_mm(AW, AH, 2)},
        {"alphabeta d=4",       mk_mm(AW, AH, 4)},
        {"alphabeta d=6",       mk_mm(AW, AH, 6)},
        {"uct 100 uniform",     mk_uct(100, false)},
        {"uct 1000 uniform",    mk_uct(1000, false)},
        {"uct 10000 uniform",   mk_uct(10000, false)},
        {"uct 1000 greedy-ro",  mk_uct(1000, true)},
        {"uct 10000 greedy-ro", mk_uct(10000, true)},
    };

    // === 1. AGAINST THE TWO MANDATORY BASELINES =============================
    std::printf("\n=== 1a. EVERY AGENT vs UNIFORM RANDOM (Connect-4 %dx%d) ===\n", AW, AH);
    print_match_header();
    for (const auto& p : arena)
        print_match_row(p.name.c_str(), play_match(AW, AH, p.fn, rnd, games, 1000), games);
    std::printf("  every agent above the first saturates. That is the whole problem with\n"
                "  measuring strength by win rate against a weak opponent, and the reason\n"
                "  section 3 exists.\n");

    std::printf("\n=== 1b. EVERY AGENT vs THE GREEDY ONE-PLY HEURISTIC (Connect-4 %dx%d) ===\n", AW, AH);
    print_match_header();
    for (const auto& p : arena)
        print_match_row(p.name.c_str(), play_match(AW, AH, p.fn, grd, games, 2000), games);
    std::printf("  (greedy vs greedy is on the second row as a sanity check: two copies of\n"
                "   the same agent alternating the first move should score near 50%%.)\n");

    // === 2. MCTS vs MINIMAX, SWEPT =========================================
    std::printf("\n=== 2. MCTS vs ALPHA-BETA, HEAD TO HEAD, SWEPT (Connect-4 %dx%d) ===\n", AW, AH);
    std::printf("  results are from the MCTS side. %d games per cell.\n", sweepn);
    print_match_header();
    for (const bool gro : {false, true}) {
        for (const int b : {100, 1000, 10000}) {
            for (const int d : {2, 4, 6}) {
                char lbl[64];
                std::snprintf(lbl, sizeof lbl, "uct %d%s v d=%d", b, gro ? "g" : "u", d);
                print_match_row(lbl, play_match(AW, AH, mk_uct(b, gro), mk_mm(AW, AH, d),
                                                sweepn, 3000ull + static_cast<std::uint64_t>(b) * 31 + static_cast<std::uint64_t>(d)),
                                sweepn);
            }
        }
        if (!gro) std::printf("  -- uniform rollouts above, greedy rollouts below --\n");
    }
    std::printf("  read the three depths across each budget: the ordering CHANGES with the\n"
                "  opponent. d=6 is the strongest alpha-beta against 10000-rollout MCTS and\n"
                "  the weakest against the greedy baseline and against exact play (3b). This\n"
                "  table cannot be collapsed into a ranking.\n"
                "  and read the W/D/L, not the score: the uct-10000-greedy vs d=6 cell is\n"
                "  mostly draws while the same MCTS against d=2 and d=4 is mostly decisive.\n"
                "  A draw-heavy score and a win-heavy score of the same value are not the\n"
                "  same result -- the draw rate belongs to the pairing, not to either agent.\n");

    // === 4. THE PRICE (numbered as specified; printed here because the ======
    //        position set it uses is the same one section 3 needs) ===========
    std::printf("\n=== 4a. COST PER MOVE ON THE ARENA (%dx%d), same sampled positions for all ===\n", AW, AH);
    {
        const std::vector<C4> ps = sample_positions(AW, AH, static_cast<std::size_t>(nposs), 4242, 0.35);
        cost_table(ps, arena, 555);
        std::printf("  a minimax node is one negamax call (move generation, a win scan, sometimes\n"
                    "  a static eval); an MCTS node is one position touched in selection or\n"
                    "  rollout. The units are NOT the same -- read the columns down, not across.\n");
    }

    // === 3. AGAINST OPTIMAL PLAY ============================================
    std::printf("\n=== 3. AGAINST EXACT PLAY ON A SOLVED BOARD (Connect-4 %dx%d) ===\n", SW, SH);
    {
        Solver sv(SW, SH);
        const auto t0 = Clock::now();
        const C4 empty(SW, SH);
        const int value = sv.solve(empty);
        const double solve_s = std::chrono::duration<double>(Clock::now() - t0).count();
        std::printf("  exact game value from the empty board: %s  (%llu positions stored, %.2f s)\n",
                    value > 0 ? "FIRST PLAYER WINS" : (value < 0 ? "SECOND PLAYER WINS" : "DRAW"),
                    static_cast<unsigned long long>(sv.solved), solve_s);
        std::printf("  fewer than the board's total legal positions: a position proven won is\n"
                    "  abandoned as soon as one winning move is found, so its siblings stay unvisited.\n");

        const Agent perfect = [&sv](const C4& g, std::mt19937& r, Stats& s) {
            const auto t = Clock::now();
            Moves m; int v[kMaxW];
            const std::uint64_t before = sv.solved;
            sv.move_values(g, v, m);
            int best = -2, cand[kMaxW], n = 0;
            for (int i = 0; i < m.n; ++i) {
                if (v[i] > best) { best = v[i]; n = 0; cand[n++] = m.c[i]; }
                else if (v[i] == best) cand[n++] = m.c[i];
            }
            s.nodes += sv.solved - before;
            s.add_time(t);
            return cand[r() % static_cast<unsigned>(n)];
        };

        // d=8 and d=12 are not in the specified sweep; they are a CONTROL on the
        // alpha-beta implementation. If the search is correct, agreement with
        // exact play must rise with depth and converge on 100% as the horizon
        // stops mattering. A depth series that plateaus below 100%, or wobbles,
        // is a bug report about the search rather than a fact about the game.
        std::vector<Player> small = {
            {"random",              rnd},
            {"greedy 1-ply",        grd},
            {"alphabeta d=2",       mk_mm(SW, SH, 2)},
            {"alphabeta d=4",       mk_mm(SW, SH, 4)},
            {"alphabeta d=6",       mk_mm(SW, SH, 6)},
            {"alphabeta d=8  ctrl", mk_mm(SW, SH, 8)},
            {"alphabeta d=12 ctrl", mk_mm(SW, SH, 12)},
            {"uct 100 uniform",     mk_uct(100, false)},
            {"uct 1000 uniform",    mk_uct(1000, false)},
            {"uct 10000 uniform",   mk_uct(10000, false)},
            {"uct 1000 greedy-ro",  mk_uct(1000, true)},
            {"uct 10000 greedy-ro", mk_uct(10000, true)},
            {"EXACT (perfect)",     perfect},
        };

        // --- 3a. OPTIMAL-MOVE AGREEMENT ------------------------------------
        //
        // The measurement win rate cannot give. A move is OPTIMAL if its exact
        // win/draw/loss value equals the position's -- if it does not throw the
        // result away. Reported three ways, because the first is inflated by
        // construction: in most positions of a drawn game nearly every move is
        // fine, so "agreement over all positions" flatters everybody. The
        // DISCRIMINATING subset -- positions where at least one legal move is a
        // mistake -- is the real number. The blunder column is the fraction of
        // positions where the agent moved into a proven LOSS from a position
        // that was not already lost.
        const std::vector<C4> ps = sample_positions(SW, SH, static_cast<std::size_t>(nposs), 8484, 0.35);
        std::size_t discriminating = 0;
        std::vector<std::vector<int>> vals(ps.size());
        std::vector<Moves>            mvs(ps.size());
        std::vector<int>              pv(ps.size());
        for (std::size_t i = 0; i < ps.size(); ++i) {
            int v[kMaxW];
            sv.move_values(ps[i], v, mvs[i]);
            vals[i].assign(v, v + mvs[i].n);
            pv[i] = *std::max_element(vals[i].begin(), vals[i].end());
            if (std::count(vals[i].begin(), vals[i].end(), pv[i]) < mvs[i].n) ++discriminating;
        }
        std::printf("\n  --- 3a. OPTIMAL-MOVE AGREEMENT over %zu sampled positions ---\n", ps.size());
        std::printf("  %zu of them are DISCRIMINATING (at least one legal move loses value)\n", discriminating);
        std::printf("  agent                  | optimal/all   | optimal/discriminating  95%% CI      | blunders to a loss\n");
        std::printf("  -----------------------+---------------+-------------------------------------+-------------------\n");
        for (const auto& p : small) {
            std::mt19937 rng(90210);
            Stats st;
            std::size_t ok_all = 0, ok_disc = 0, disc = 0, blunder = 0, blunder_pool = 0;
            for (std::size_t i = 0; i < ps.size(); ++i) {
                const int mv = p.fn(ps[i], rng, st);
                int chosen = -2;
                for (int j = 0; j < mvs[i].n; ++j) if (mvs[i].c[j] == mv) chosen = vals[i][j];
                const bool hard = std::count(vals[i].begin(), vals[i].end(), pv[i]) < mvs[i].n;
                if (chosen == pv[i]) ++ok_all;
                if (hard) { ++disc; if (chosen == pv[i]) ++ok_disc; }
                if (pv[i] > -1) { ++blunder_pool; if (chosen == -1) ++blunder; }
            }
            const auto ci = wilson(ok_disc, disc);
            std::printf("  %-22s | %5zu %6.1f%% | %5zu %6.1f%%  [%5.1f, %5.1f]        | %5zu / %-5zu %5.1f%%\n",
                        p.name.c_str(), ok_all,
                        ps.empty() ? 0.0 : 100.0 * static_cast<double>(ok_all) / static_cast<double>(ps.size()),
                        ok_disc, disc ? 100.0 * static_cast<double>(ok_disc) / static_cast<double>(disc) : 0.0,
                        ci.first, ci.second, blunder, blunder_pool,
                        blunder_pool ? 100.0 * static_cast<double>(blunder) / static_cast<double>(blunder_pool) : 0.0);
        }
        std::printf("  the EXACT row is the control: it must read 100%% and zero blunders, and if\n"
                    "  it does not the harness is broken rather than the agent.\n");

        // --- 3b. HEAD TO HEAD AGAINST THE PERFECT PLAYER --------------------
        std::printf("\n  --- 3b. vs THE PERFECT PLAYER, %d games, first move alternated ---\n", games);
        print_match_header();
        for (const auto& p : small)
            print_match_row(p.name.c_str(), play_match(SW, SH, p.fn, perfect, games, 6000), games);
        std::printf("  the perfect player is win/draw/loss optimal, not fastest-win optimal: in a\n"
                    "  drawn position it plays ANY drawing move and makes no attempt to set traps.\n"
                    "  It cannot lose, and it only wins when the opponent throws the draw away.\n"
                    "  Nobody wins a game, which is correct: %dx%d is a draw, so a win column of\n"
                    "  zero everywhere is a check on the solver rather than a result about the agents.\n"
                    "  READ THE ALPHA-BETA DEPTHS DOWN. The draw rate is not monotone in depth --\n"
                    "  d=6 sits below d=2. The d=8 and d=12 control rows keep climbing, so the\n"
                    "  search converges on exact play and the dip belongs to the evaluation\n"
                    "  function, which scores a three-in-a-window at 16 without checking whether\n"
                    "  the fourth cell can be reached. Nothing was retuned in response.\n"
                    "  Note also that 3a and 3b disagree about d=6: it matches d=4 for per-position\n"
                    "  agreement and loses far more games. Agreement is measured on positions from\n"
                    "  epsilon-greedy self-play; these games are the positions a PERFECT opponent\n"
                    "  steers into, which are not the same distribution.\n", SW, SH);

        // --- 4b. THE PRICE ON THE SOLVED BOARD -----------------------------
        std::printf("\n  --- 4b. COST PER MOVE ON THE SOLVED BOARD (%dx%d) ---\n", SW, SH);
        cost_table(ps, small, 777);
        std::printf("  the EXACT row's node count is positions NEWLY solved, so it collapses after\n"
                    "  the table is warm -- the honest price of exact play is the %.2f s solve above,\n"
                    "  paid once, for a board 10 cells smaller than the arena.\n", solve_s);
    }

    // === WHAT THIS HARNESS CANNOT SEE =======================================
    std::printf("\n=== WHAT THIS HARNESS CANNOT SEE ===\n");
    std::printf(
        "  1. The %dx%d arena is NOT solved. Every number in sections 1, 2 and 4a is\n"
        "     strength relative to the other agents in the same table. None of them is\n"
        "     measured against truth, and a table of weak agents beating each other\n"
        "     tells you nothing about how any of them would do against real play.\n"
        "  2. The minimax evaluation function is hand-written and was never tuned.\n"
        "     Window weights 1/4/16 and a centre bonus of 3, chosen once. Every\n"
        "     alpha-beta row moves if they change and there is no ground truth on the\n"
        "     arena to say which setting is right. Section 3 is the only place where\n"
        "     an evaluation can be checked against the actual value of a position.\n"
        "  3. The UCT exploration constant is fixed at sqrt(2) and is not swept. It is\n"
        "     the textbook value for rewards in [0,1] and it is still a knob.\n"
        "  4. Node counts are not one unit. A minimax node is a negamax call; an MCTS\n"
        "     node is a position touched in selection or rollout; the EXACT row counts\n"
        "     positions newly written to the solve table. Compare down a column.\n"
        "  5. Wall time is single-threaded, one machine, one build. The nodes/us column\n"
        "     is the only part that travels.\n"
        "  6. Optimal-move agreement is measured on positions sampled from\n"
        "     epsilon-greedy self-play. A different sampler gives a different set and a\n"
        "     different level. The set is COMMON to every agent, so the ordering is\n"
        "     fair; the absolute percentage is a property of the sample.\n"
        "  7. Game outcomes are trinomial, so the score column has no interval. The two\n"
        "     Wilson intervals bound the win share and the loss share separately.\n"
        "  8. NONE OF THIS IS WIRED INTO KHORA. It is one bench file that links\n"
        "     khora::lattice and does not call it. It establishes that adversarial\n"
        "     search can be written and measured in this tree; it does not give the\n"
        "     system an opponent model, and the Ligature's beam planner still searches\n"
        "     a space that never pushes back.\n", AW, AH);
    return 0;
}
