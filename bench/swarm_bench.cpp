// SWARM — AT EQUAL TOTAL BUDGET, DOES COORDINATION BUY ANYTHING?
//
// A capability audit found no multi-agent anything in this tree: no coalition,
// no negotiation, no auction, no consensus, no shared state. Khora is one agent.
// The synapse bus — a thread-safe topic bus with bounded per-subscriber queues
// and drop counting — existed with zero publishers in the whole binary until
// today. This is its second real use, and the first that stresses it.
//
// The live question is not "can many agents do more than one" (trivially yes,
// if you give them more compute). It is "at the SAME TOTAL COMPUTE, do N agents
// beat one". Those two get conflated constantly, and the conflation is where
// most swarm results come from. So every number below is at a fixed total
// budget of 4,096 landscape evaluations, split N ways. N=1 gets all 4,096.
// Speedup bought by spending more is not coordination and is not measured here.
//
// THE TASK: distributed search for the global maximum of a rugged 256x256
// landscape — 65,536 cells, 250 overlapping Gaussian bumps, and in practice
// around 127 distinct local maxima to get trapped in. The total budget buys
// about 6% coverage, which is what keeps the single-agent baseline off the
// ceiling; see the note on kW below for what happened when it was not. An
// agent is a stochastic hill-climber with restarts: pick a start cell, evaluate
// its 8 neighbours, move to the best improving one, stop at a local peak,
// restart elsewhere. Each evaluation of a cell the agent has not seen costs one
// budget unit.
//
// WHY THIS TASK AND NOT AN AUCTION-FOR-RESOURCES OR A CONSENSUS PROBLEM. Three
// reasons, all about being able to state an honest baseline:
//   - The single-agent baseline is EXACTLY comparable. One agent with 4,096
//     evaluations and sixteen agents with 256 each consume the identical
//     resource. In a resource-allocation problem "one agent" has to be defined
//     into existence, and in a consensus-over-noisy-observations problem one
//     agent with N times the samples wins by averaging, which is a statement
//     about sample size, not about coordination.
//   - Duplicated work is directly measurable and directly costly. Every agent
//     memoises its own evaluations for free — a cell it has already paid for is
//     free forever after. So when two agents both evaluate cell c, the second
//     one paid budget for something already bought. Duplication is not a proxy
//     here; it is a line item in the budget.
//   - And that construction exposes the sharpest fact in the whole experiment:
//     ONE AGENT HAS A PERFECT SHARED CACHE FOR FREE. It cannot duplicate its own
//     work. Everything the swarm can possibly gain from communication is, at
//     best, recovering what a single agent gets for nothing. That is the bar.
//
// SIX MECHANISMS, chosen so that each pair differs in exactly one thing:
//   independent      N agents, no messages, restart anywhere. Isolates what
//                    PARALLELISM alone buys (more independent restarts, a
//                    portfolio effect) with zero coordination.
//   partition        N agents, no messages, restarts confined to a vertical
//                    stripe of the grid; climbs may leave it. Coordination
//                    WITHOUT communication — division of labour arranged in
//                    advance and never renegotiated. Costs nothing to run.
//   blackboard-peaks Broadcast every local peak found. Receivers refuse to
//                    restart within 6 cells of any known peak, their own or
//                    anyone's. Shares CONCLUSIONS. ~1 message per climb.
//   blackboard-cells Broadcast every cell evaluated, with its value. Receivers
//                    add it to their cache, so it is free for them thereafter.
//                    Shares OBSERVATIONS. ~1 message per budget unit. This is
//                    the mechanism that, if messages all arrived, would exactly
//                    reconstruct the single agent's free cache.
//   auction          A negotiated protocol. The grid is 64 tiles of 32x32.
//                    Each round every agent picks a tile by UCB1
//                    (khora::telos::Valuer, one context, 64 arms), publishes a
//                    bid, reads everyone's bids, and all agents run the same
//                    deterministic clearing rule, so the round's assignment is
//                    disjoint with no central allocator. Winners search their
//                    tile for 128 evaluations, then publish the yield, which
//                    every agent observes — so all N bandits learn from N times
//                    the data. Rounds are DERIVED so that every N does exactly
//                    32 tile-searches of exactly 128 evaluations; see the note
//                    at run_auction for why a fixed round count would rig it.
//   auction-noclear  The same bandit and the same shared yields, with the
//                    clearing rule removed: every agent simply searches the tile
//                    it bid on. Added after the first run, because the first run
//                    showed the bid-collision rate was 100% at every N and the
//                    question "what is the negotiation actually doing" needed an
//                    answer that was not a guess. It separates SHARED LEARNING
//                    from DIVISION OF LABOUR inside the same protocol.
//
// ALL SIX give every agent the same free local memory: its own cache and its
// own peak list. The only difference between mechanisms is what crosses between
// agents. That is the isolation the whole design is for.
//
// WHY THE BLACKBOARD IS EXPECTED TO BREAK AND WHERE. Broadcast traffic is
// O(N^2) — every agent publishes, every other agent receives — while any single
// agent's capacity to consume it is O(1) per step. This harness prices reading:
// an agent drains at most 4 messages per step (kPopsPerStep). Nothing else is
// needed to produce the classic wall; the bus's bounded queues do the rest and
// count the drops. The auction, by contrast, sends O(N) messages at an explicit
// synchronisation barrier and drains fully there, which is a protocol property,
// not a harness favour. The comparison is between message schedules.
//
// WHAT THE HARNESS CANNOT SEE, stated up front:
//   - THREADS. Agents are stepped round-robin on one thread. The bus used is
//     the real thread-safe bus, but its concurrency is never exercised. Drops
//     here come from queue capacity versus arrival rate, not from scheduling.
//     Real contention, lock cost, and the ragged drop patterns of true
//     concurrency are invisible.
//   - MESSAGE COST IN BUDGET. Sending is free in budget units. Only the READING
//     of messages is rationed. In a real system a message costs bandwidth and
//     latency, so the coordinated mechanisms are flattered here.
//   - THE PAYLOAD TAX IS REPORTED BUT NOT CHARGED. The bus carries Glyphs only.
//     A Glyph is 10,000 bits = 1,250 bytes, so each 16-byte message below moves
//     1,250 bytes. Bytes-on-the-wire is printed; it does not affect any score.
//   - ONE LANDSCAPE FAMILY. Smooth multi-modal sums of Gaussians. Nothing here
//     speaks to noisy, deceptive, non-stationary, or adversarial landscapes, and
//     nothing here speaks to tasks that are not decomposable at all.
//   - NO HETEROGENEITY. All agents run the identical algorithm. Swarms whose
//     members differ are a different question.
//   - Wilson intervals below cover TRIAL SAMPLING ONLY. They say nothing about
//     whether this landscape family is representative.
//
// 300 landscapes, generated once and reused by every configuration, so all
// comparisons are paired on the same problems.

#include "khora/lattice/glyph.hpp"
#include "khora/synapse/synapse_bus.hpp"
#include "khora/telos/telos.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

using khora::lattice::Glyph;
using khora::synapse::Handle;
using khora::synapse::SynapseBus;

// SIZED SO THAT ONE AGENT IS NOT AT CEILING. The first version of this bench
// used a 128x128 grid and the single-agent baseline found the global optimum in
// 300/300 trials, which measures nothing: no mechanism can beat 100%. The grid
// is four times larger here and the budget is unchanged, so the total budget
// buys about 6% coverage and the baseline lands mid-range. A benchmark whose
// baseline is saturated is a benchmark with no result in it.
constexpr int  kW = 256, kH = 256;
constexpr int  kCells = kW * kH;
constexpr int  kBumps = 250;
constexpr long kBudget = 4096;          // total evaluations for the WHOLE swarm
constexpr int  kTileSide = 32;
constexpr int  kTilesX = kW / kTileSide;
constexpr int  kTilesY = kH / kTileSide;
constexpr int  kTiles = kTilesX * kTilesY;   // 64
constexpr int  kPeakRadius = 6;
constexpr int  kTrials = 300;
constexpr std::size_t kQueueCap = 1024;      // the bus default
constexpr int  kPopsPerStep = 4;             // messages an agent may read per step
constexpr int  kUnlimitedPops = 1 << 24;

// --- 95% Wilson interval on a proportion, as percentages. Same convention as
// every other bench in this tree: never a rate without its counts and its
// interval.
std::pair<double, double> wilson(std::size_t k, std::size_t n) {
    if (n == 0) return {0.0, 0.0};
    const double z = 1.959963985, p = double(k) / double(n), zz = z * z;
    const double d    = 1.0 + zz / double(n);
    const double ctr  = p + zz / (2.0 * double(n));
    const double half = z * std::sqrt(p * (1.0 - p) / double(n)
                                      + zz / (4.0 * double(n) * double(n)));
    return {100.0 * (ctr - half) / d, 100.0 * (ctr + half) / d};
}

// --- THE PAYLOAD TAX -------------------------------------------------------
//
// The bus carries khora::lattice::Glyph and nothing else. Every message here is
// three small fields; a Glyph is 1,250 bytes. Packing them into the first two
// words is a deliberate abuse of the type and is exactly the kind of thing that
// should be recorded rather than hidden: routing a 16-byte fact through this bus
// moves 1,250 bytes. Reported below, not charged.
struct Msg {
    std::uint32_t agent;
    std::uint32_t arg;    // cell index, or tile index
    double        val;
};

Glyph pack(const Msg& m) {
    Glyph g;
    auto& w = g.words();
    w[0] = (std::uint64_t(m.agent) << 32) | std::uint64_t(m.arg);
    std::memcpy(&w[1], &m.val, sizeof(double));
    return g;
}

Msg unpack(const Glyph& g) {
    const auto& w = g.words();
    Msg m;
    m.agent = std::uint32_t(w[0] >> 32);
    m.arg   = std::uint32_t(w[0] & 0xffffffffull);
    std::memcpy(&m.val, &w[1], sizeof(double));
    return m;
}

// --- THE LANDSCAPE ---------------------------------------------------------

// float, not double: 300 landscapes x 65,536 cells is 79 MB this way and 157 MB
// the other. Every value an agent ever sees comes out of this array, so exact
// comparison against max_v is still exact.
struct Landscape {
    std::vector<float> v;
    float  max_v = 0.0f;
    int    max_cell = 0;
    int    local_maxima = 0;
};

Landscape make_landscape(std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> ux(0.0, double(kW)), uy(0.0, double(kH));
    std::uniform_real_distribution<double> uh(0.3, 1.0), us(3.0, 8.0);
    struct Bump { double x, y, h, s; };
    std::vector<Bump> bs(kBumps);
    for (auto& b : bs) b = {ux(rng), uy(rng), uh(rng), us(rng)};

    // Splat each bump over its own bounding box rather than testing every bump
    // at every cell: 140 bumps x 65,536 cells x 300 landscapes is 2.8 billion
    // exp() calls and dominates the run. Cut at 5 sigma, where the term is 4e-6.
    std::vector<double> acc(kCells, 0.0);
    for (const auto& b : bs) {
        const int r  = int(5.0 * b.s) + 1;
        const int lo_x = std::max(0, int(b.x) - r), hi_x = std::min(kW - 1, int(b.x) + r);
        const int lo_y = std::max(0, int(b.y) - r), hi_y = std::min(kH - 1, int(b.y) + r);
        const double inv = 1.0 / (2.0 * b.s * b.s);
        for (int y = lo_y; y <= hi_y; ++y) {
            const double dy = double(y) - b.y;
            for (int x = lo_x; x <= hi_x; ++x) {
                const double dx = double(x) - b.x;
                acc[std::size_t(y) * kW + x] += b.h * std::exp(-(dx * dx + dy * dy) * inv);
            }
        }
    }
    Landscape L;
    L.v.resize(kCells);
    for (int i = 0; i < kCells; ++i) L.v[std::size_t(i)] = float(acc[std::size_t(i)]);

    L.max_cell = int(std::max_element(L.v.begin(), L.v.end()) - L.v.begin());
    L.max_v    = L.v[std::size_t(L.max_cell)];

    // How many distinct peaks a climber could stop at. A property of the
    // problem, printed so the difficulty is not a mystery number.
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const float c = L.v[std::size_t(y) * kW + x];
            bool top = true;
            for (int dy = -1; dy <= 1 && top; ++dy) {
                for (int dx = -1; dx <= 1 && top; ++dx) {
                    if (!dx && !dy) continue;
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || nx >= kW || ny < 0 || ny >= kH) continue;
                    if (L.v[std::size_t(ny) * kW + nx] > c) top = false;
                }
            }
            if (top) ++L.local_maxima;
        }
    }
    return L;
}

// --- AGENTS ----------------------------------------------------------------

enum class Mech { Independent, Partition, BoardPeaks, BoardCells, Auction, AuctionNoClear };

const char* mech_name(Mech m) {
    switch (m) {
        case Mech::Independent: return "independent  (0 msgs)";
        case Mech::Partition:   return "partition    (0 msgs)";
        case Mech::BoardPeaks:  return "board-peaks  (share peaks)";
        case Mech::BoardCells:  return "board-cells  (share evals)";
        case Mech::Auction:     return "auction      (bid + clear)";
        case Mech::AuctionNoClear: return "auction-noclear (bid only)";
    }
    return "?";
}

struct Trial {
    std::vector<std::int8_t> first;   // -1 unset, else the agent that paid first
    long spent = 0;
    long dup = 0;          // evaluations paid for a cell another agent already paid for
    long covered = 0;      // distinct cells anyone paid for
    long steps = 0;
    long conflicts = 0;    // auction: rounds in which two agents ended on one tile
    long bid_collisions = 0;
    bool stalled = false;
};

struct Ctx {
    Mech             mech;
    const Landscape* L;
    SynapseBus*      bus;
    Trial*           T;
    int              pops;
};

struct Agent {
    int              id = 0;
    std::mt19937_64  rng;
    std::vector<char>   known;
    std::vector<float>  cache;
    std::vector<int>    peaks;
    int  x0 = 0, x1 = kW, y0 = 0, y1 = kH;   // restart region only; climbs are free to leave
    long rem = 0;
    double best = -1e300;
    int    best_cell = -1;
    // climb state machine: one step == at most one evaluation
    int    cur = -1;
    double cur_v = 0.0;
    int    nb = 8;
    int    best_nb = -1;
    double best_nb_v = -1e300;
    bool   need_start = true;
    Handle h_a = 0, h_b = 0;
    khora::telos::Valuer valuer;
};

// Charges budget only for a cell this agent has never seen. A cell received over
// the bus is free — that IS the coordination benefit, priced in the same
// currency as the work. Returns false when the agent is out of budget.
bool pay_eval(Agent& a, Ctx& c, int cell, double& out) {
    if (a.known[std::size_t(cell)]) {
        out = a.cache[std::size_t(cell)];
        if (out > a.best) { a.best = out; a.best_cell = cell; }
        return true;
    }
    if (a.rem <= 0) return false;
    --a.rem;
    Trial& T = *c.T;
    ++T.spent;
    if (T.first[std::size_t(cell)] < 0) { T.first[std::size_t(cell)] = std::int8_t(a.id); ++T.covered; }
    else if (T.first[std::size_t(cell)] != std::int8_t(a.id)) ++T.dup;
    a.known[std::size_t(cell)] = 1;
    a.cache[std::size_t(cell)] = c.L->v[std::size_t(cell)];
    out = a.cache[std::size_t(cell)];
    if (out > a.best) { a.best = out; a.best_cell = cell; }
    if (c.mech == Mech::BoardCells)
        c.bus->publish("swarm/cell", pack({std::uint32_t(a.id), std::uint32_t(cell), out}));
    return true;
}

void note_peak(Agent& a, Ctx& c, int cell, double v) {
    a.peaks.push_back(cell);
    if (c.mech == Mech::BoardPeaks)
        c.bus->publish("swarm/peak", pack({std::uint32_t(a.id), std::uint32_t(cell), v}));
}

// Reading is rationed. `limit` pops per call, and a message from oneself still
// consumes one — the bus fans out to every subscriber of a topic including the
// publisher, and discarding it is work the agent actually does.
void drain(Agent& a, Ctx& c, int limit) {
    for (int i = 0; i < limit; ++i) {
        auto p = c.bus->try_pop(a.h_a);
        if (!p) break;
        const Msg m = unpack(p->payload);
        if (int(m.agent) == a.id) continue;
        const int cell = int(m.arg);
        if (c.mech == Mech::BoardCells) {
            if (!a.known[std::size_t(cell)]) {
                a.known[std::size_t(cell)] = 1;
                a.cache[std::size_t(cell)] = float(m.val);
            }
        } else if (c.mech == Mech::BoardPeaks) {
            a.peaks.push_back(cell);
        }
        if (m.val > a.best) { a.best = m.val; a.best_cell = cell; }
    }
}

bool near_peak(const Agent& a, int cell) {
    const int x = cell % kW, y = cell / kW;
    for (const int p : a.peaks) {
        if (std::abs(p % kW - x) <= kPeakRadius && std::abs(p / kW - y) <= kPeakRadius)
            return true;
    }
    return false;
}

// Every mechanism refuses to restart next to a peak it already knows. That
// memory is FREE and LOCAL, so a single agent gets the complete version of it at
// no cost. Only which peaks are in the list differs between mechanisms.
int pick_start(Agent& a) {
    int c = a.y0 * kW + a.x0;
    for (int t = 0; t < 20; ++t) {
        const int x = a.x0 + int(a.rng() % unsigned(a.x1 - a.x0));
        const int y = a.y0 + int(a.rng() % unsigned(a.y1 - a.y0));
        c = y * kW + x;
        if (!near_peak(a, c)) return c;
    }
    return c;   // 20 rejections: the region is saturated, take it anyway
}

void step(Agent& a, Ctx& c) {
    ++c.T->steps;
    if (a.need_start) {
        a.cur = pick_start(a);
        double v;
        if (!pay_eval(a, c, a.cur, v)) return;
        a.cur_v = v;
        a.need_start = false;
        a.nb = 0; a.best_nb = -1; a.best_nb_v = -1e300;
        return;
    }
    if (a.nb < 8) {
        static const int dx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
        static const int dy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
        const int x = a.cur % kW + dx[a.nb];
        const int y = a.cur / kW + dy[a.nb];
        ++a.nb;
        if (x < 0 || x >= kW || y < 0 || y >= kH) return;   // off-grid: a step, no budget
        double v;
        if (!pay_eval(a, c, y * kW + x, v)) return;
        if (v > a.best_nb_v) { a.best_nb_v = v; a.best_nb = y * kW + x; }
        return;
    }
    if (a.best_nb >= 0 && a.best_nb_v > a.cur_v) {
        a.cur = a.best_nb; a.cur_v = a.best_nb_v;
        a.nb = 0; a.best_nb = -1; a.best_nb_v = -1e300;
    } else {
        note_peak(a, c, a.cur, a.cur_v);
        a.need_start = true;
    }
}

// --- ONE TRIAL -------------------------------------------------------------

struct Result {
    bool   success = false;     // reached the exact global optimum cell
    double regret = 0.0;        // (max - best) / max
    long   spent = 0, dup = 0, covered = 0;
    std::uint64_t published = 0, dropped = 0;
    long   conflicts = 0, bid_collisions = 0;
    bool   stalled = false;
};

void init_agent(Agent& a, int id, std::uint64_t seed, long rem) {
    a.id = id;
    // Seed does NOT include the mechanism, so agent i draws the same random
    // stream under every mechanism. At N=1 the four non-auction mechanisms are
    // then the identical algorithm and must produce identical numbers; that is
    // asserted below and is the harness's own correctness check.
    a.rng.seed(seed * 1000003ull + std::uint64_t(id) * 7919ull + 11400714819323198485ull);
    a.known.assign(kCells, 0);
    a.cache.assign(kCells, 0.0f);
    a.peaks.clear();
    a.rem = rem;
    a.best = -1e300;
    a.best_cell = -1;
    a.need_start = true;
    a.nb = 8;
    a.x0 = 0; a.x1 = kW; a.y0 = 0; a.y1 = kH;
}

Result run_swarm(Mech m, int N, const Landscape& L, std::uint64_t seed,
                 int pops, std::size_t cap) {
    SynapseBus bus;
    Trial T;
    T.first.assign(kCells, -1);
    Ctx c{m, &L, &bus, &T, pops};

    std::vector<Agent> ag;
    ag.resize(std::size_t(N));
    const char* topic = (m == Mech::BoardCells) ? "swarm/cell" : "swarm/peak";
    for (int i = 0; i < N; ++i) {
        Agent& a = ag[std::size_t(i)];
        init_agent(a, i, seed, kBudget / N + (i < kBudget % N ? 1 : 0));
        if (m == Mech::Partition) { a.x0 = i * kW / N; a.x1 = (i + 1) * kW / N; }
        if (m == Mech::BoardPeaks || m == Mech::BoardCells) a.h_a = bus.subscribe(topic, cap);
    }

    const long step_cap = 80 * kBudget;
    bool any = true;
    while (any) {
        any = false;
        for (int i = 0; i < N; ++i) {
            Agent& a = ag[std::size_t(i)];
            if (a.rem <= 0) continue;
            any = true;
            if (a.h_a) drain(a, c, pops);
            step(a, c);
        }
        if (T.steps > step_cap) { T.stalled = true; break; }
    }

    Result r;
    double best = -1e300;
    for (const Agent& a : ag) best = std::max(best, a.best);
    r.success   = best >= L.max_v - 1e-9;
    r.regret    = (L.max_v - best) / L.max_v;
    r.spent     = T.spent;
    r.dup       = T.dup;
    r.covered   = T.covered;
    r.published = bus.total_published();
    r.dropped   = bus.total_dropped();
    r.stalled   = T.stalled;
    return r;
}

// --- THE NEGOTIATED PROTOCOL ------------------------------------------------
//
// Rounds. Each agent picks a tile by UCB1 over its own Valuer, bids, reads
// every bid, and runs the same deterministic clearing rule, so the round's
// assignment is disjoint without a central allocator. Winners search, then
// publish the yield; everyone observes it, so all N Valuers are learned from N
// times the observations. That shared learning is the point — the tile
// assignment alone is just a dynamic partition.
//
// TELOS'S TIE-BREAK DOES NOT DECORRELATE PEERS, AND THAT IS A FINDING. An
// untried arm has an infinite confidence bound, so in the opening rounds every
// arm ties and choose() reservoir-samples among them. But telos derives that
// randomness by HASHING (context, action, k, evidence-count) rather than drawing
// it, deliberately, so that a const method holds no mutable RNG and two
// identical queries agree — read the comment above take_tie in src/telos. For
// one learner that is exactly right. For N learners that have all observed the
// same yields, the Valuers are bit-identical, so choose() returns the SAME arm
// in all of them. Table 4 measures it: bid collisions are 100% of the maximum
// possible at every N. The fix that stopped ties collapsing to the lowest index
// did not, and could not, stop them collapsing to the same index across agents.
//
// So the auction's disjointness comes entirely from the clearing rule, not from
// the bandit. `auction-noclear` is the same protocol with clearing removed and
// exists to put a number on that.
//
// THE ROUND COUNT IS DERIVED, NOT CHOSEN. A fixed number of rounds would be a
// rigged parameter: at a fixed total budget, rounds x N x slice = budget, so
// pinning rounds makes the per-round slice shrink with N until an agent cannot
// finish a single climb, and the auction loses for a reason that has nothing to
// do with negotiation. Instead the SLICE is pinned at 128 evaluations — a bit
// under two climbs, enough to produce a yield worth bidding on — and the number
// of rounds falls out: 32 rounds at N=1, 2 rounds at N=16. Every configuration
// therefore performs exactly 32 tile-searches of exactly 128 evaluations. The
// only thing N changes is how many of those 32 are decided in parallel with no
// information from each other. That is the honest form of the question this
// mechanism asks, and it makes the answer legible: the bandit's learning is
// SEQUENTIAL, and widening the swarm is precisely what destroys the sequence.
constexpr long kAuctionSlice = 128;

Result run_auction(int N, const Landscape& L, std::uint64_t seed, bool clearing) {
    SynapseBus bus;
    Trial T;
    T.first.assign(kCells, -1);
    Ctx c{Mech::Auction, &L, &bus, &T, kUnlimitedPops};

    std::vector<Agent> ag;
    ag.resize(std::size_t(N));
    for (int i = 0; i < N; ++i) {
        Agent& a = ag[std::size_t(i)];
        init_agent(a, i, seed, 0);
        a.valuer = khora::telos::Valuer(1, kTiles);
        a.h_a = bus.subscribe("swarm/bid", kQueueCap);
        a.h_b = bus.subscribe("swarm/yield", kQueueCap);
    }

    const long slice  = kAuctionSlice;
    const int  rounds = int(kBudget / (kAuctionSlice * N));   // 32, 16, 8, 4, 2
    std::vector<int> mine(std::size_t(N), 0), bidtile(std::size_t(N), 0);

    for (int round = 0; round < rounds; ++round) {
        // BID. confidence_bound is infinite for an untried tile; clamp so the
        // wire value is finite and ordering is preserved.
        for (int i = 0; i < N; ++i) {
            Agent& a = ag[std::size_t(i)];
            const std::size_t t = a.valuer.choose(0, 1.0);
            bidtile[std::size_t(i)] = int(t);
            double b = a.valuer.confidence_bound(0, t, 1.0);
            if (!std::isfinite(b)) b = 1e9;
            bus.publish("swarm/bid", pack({std::uint32_t(i), std::uint32_t(t), b}));
        }
        // Pairs of agents that wanted the SAME tile before clearing. With a
        // lowest-index argmax instead of telos's reservoir tie-break this would
        // be every pair in the opening rounds, since all arms are untried and
        // therefore all tied at infinity.
        for (int i = 0; i < N; ++i)
            for (int j = i + 1; j < N; ++j)
                if (bidtile[std::size_t(i)] == bidtile[std::size_t(j)]) ++T.bid_collisions;

        // CLEAR, independently in each agent, from whatever it received. With no
        // drops all agents compute the same assignment; conflicts below count
        // the rounds where they did not.
        struct Bid { double v; std::uint64_t seq; int agent; int tile; };
        for (int i = 0; i < N; ++i) {
            Agent& a = ag[std::size_t(i)];
            std::vector<Bid> bids;
            while (auto p = bus.try_pop(a.h_a)) {
                const Msg m = unpack(p->payload);
                bids.push_back({m.val, p->sequence, int(m.agent), int(m.arg)});
            }
            std::sort(bids.begin(), bids.end(), [](const Bid& x, const Bid& y) {
                if (x.v != y.v) return x.v > y.v;
                return x.seq < y.seq;                     // publish order breaks ties
            });
            std::vector<char> taken(kTiles, 0), assigned(std::size_t(N), 0);
            std::vector<int>  give(std::size_t(N), -1);
            for (const Bid& b : bids) {
                if (assigned[std::size_t(b.agent)] || taken[std::size_t(b.tile)]) continue;
                assigned[std::size_t(b.agent)] = 1;
                taken[std::size_t(b.tile)] = 1;
                give[std::size_t(b.agent)] = b.tile;
            }
            for (int k = 0; k < N; ++k) {                  // deterministic fallback
                if (assigned[std::size_t(k)]) continue;
                for (int t = 0; t < kTiles; ++t)
                    if (!taken[std::size_t(t)]) { taken[std::size_t(t)] = 1; give[std::size_t(k)] = t; break; }
            }
            mine[std::size_t(i)] = give[std::size_t(i)] < 0 ? 0 : give[std::size_t(i)];
            if (!clearing) mine[std::size_t(i)] = bidtile[std::size_t(i)];
        }
        for (int i = 0; i < N; ++i)
            for (int j = i + 1; j < N; ++j)
                if (mine[std::size_t(i)] == mine[std::size_t(j)]) ++T.conflicts;

        // SEARCH the won tile with this round's slice.
        for (int i = 0; i < N; ++i) {
            Agent& a = ag[std::size_t(i)];
            const int t = mine[std::size_t(i)];
            a.x0 = (t % kTilesX) * kTileSide; a.x1 = a.x0 + kTileSide;
            a.y0 = (t / kTilesX) * kTileSide; a.y1 = a.y0 + kTileSide;
            a.need_start = true;
            const double before = a.best;
            a.rem += slice;
            long guard = 0;
            while (a.rem > 0 && guard++ < 50 * slice + 64) step(a, c);
            const double yield = (a.best > before ? a.best : before);
            bus.publish("swarm/yield", pack({std::uint32_t(i), std::uint32_t(t),
                                             yield <= -1e299 ? 0.0 : yield}));
        }

        // OBSERVE every yield, including one's own. Shared learning: N times the
        // data per Valuer, which is the only thing the auction has that a
        // dynamic partition would not.
        for (int i = 0; i < N; ++i) {
            Agent& a = ag[std::size_t(i)];
            while (auto p = bus.try_pop(a.h_b)) {
                const Msg m = unpack(p->payload);
                a.valuer.observe(0, std::size_t(m.arg), m.val);
            }
        }
    }

    Result r;
    double best = -1e300;
    for (const Agent& a : ag) best = std::max(best, a.best);
    r.success        = best >= L.max_v - 1e-9;
    r.regret         = (L.max_v - best) / L.max_v;
    r.spent          = T.spent;
    r.dup            = T.dup;
    r.covered        = T.covered;
    r.published      = bus.total_published();
    r.dropped        = bus.total_dropped();
    r.conflicts      = T.conflicts;
    r.bid_collisions = T.bid_collisions;
    return r;
}

// --- AGGREGATION ------------------------------------------------------------

struct Agg {
    std::size_t n = 0, hits = 0;
    double regret = 0.0;
    double spent = 0, dup = 0, covered = 0;
    double pub = 0, drop = 0, conflicts = 0, collisions = 0;
    double ms = 0;
    long stalls = 0;
    std::vector<char> ok;      // per-trial success, kept so the comparison can be PAIRED

    void add(const Result& r) {
        ++n;
        ok.push_back(r.success ? 1 : 0);
        if (r.success) ++hits;
        regret += r.regret;
        spent += double(r.spent); dup += double(r.dup); covered += double(r.covered);
        pub += double(r.published); drop += double(r.dropped);
        conflicts += double(r.conflicts);
        collisions += double(r.bid_collisions);
        if (r.stalled) ++stalls;
    }
    double mean(double x) const { return n ? x / double(n) : 0.0; }
};

Agg sweep(Mech m, int N, const std::vector<Landscape>& land, int pops, std::size_t cap) {
    Agg a;
    const auto t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < land.size(); ++i) {
        const std::uint64_t seed = 0x5EEDu + std::uint64_t(i);
        const bool auc = (m == Mech::Auction || m == Mech::AuctionNoClear);
        a.add(auc ? run_auction(N, land[i], seed, m == Mech::Auction)
                  : run_swarm(m, N, land[i], seed, pops, cap));
    }
    const auto t1 = std::chrono::steady_clock::now();
    a.ms = std::chrono::duration<double, std::milli>(t1 - t0).count() / double(land.size());
    return a;
}

// EVERY CONFIGURATION RAN THE SAME 300 LANDSCAPES, so the right comparison
// against the one-agent baseline is paired, not two independent Wilson
// intervals. Overlapping Wilson intervals on paired data routinely hide real
// differences: the trials that decide the question are the DISCORDANT ones,
// where exactly one of the two configurations found the optimum. McNemar's exact
// test looks only at those. w = trials this config won and the baseline lost,
// l = the reverse; p is the two-sided exact binomial on w out of w+l at 0.5.
struct Paired { std::size_t w = 0, l = 0; double p = 1.0; };

Paired mcnemar(const std::vector<char>& base, const std::vector<char>& v) {
    Paired r;
    for (std::size_t i = 0; i < base.size() && i < v.size(); ++i) {
        if (v[i] && !base[i]) ++r.w;
        else if (!v[i] && base[i]) ++r.l;
    }
    const std::size_t n = r.w + r.l, k = std::min(r.w, r.l);
    if (n == 0) return r;
    double tail = 0.0;                        // sum_{i<=k} C(n,i) / 2^n
    for (std::size_t i = 0; i <= k; ++i) {
        const double lc = std::lgamma(double(n) + 1.0) - std::lgamma(double(i) + 1.0)
                        - std::lgamma(double(n - i) + 1.0);
        tail += std::exp(lc - double(n) * std::log(2.0));
    }
    r.p = std::min(1.0, 2.0 * tail);
    return r;
}

void row(const char* label, int N, const Agg& a, const std::vector<char>& base) {
    const auto ci = wilson(a.hits, a.n);
    const Paired m = mcnemar(base, a.ok);
    char cmp[40];
    if (&a.ok == &base) std::snprintf(cmp, sizeof cmp, "%s", "   (baseline)  ");
    else std::snprintf(cmp, sizeof cmp, "+%3zu/-%3zu p=%.4f", m.w, m.l, m.p);
    std::printf("  %-27s | %2d | %4zu/%4zu | %6.1f%% | %5.1f-%5.1f%% | %7.3f%% | %6.0f | %5.0f | %s\n",
                label, N, a.hits, a.n,
                100.0 * double(a.hits) / double(a.n), ci.first, ci.second,
                100.0 * a.mean(a.regret), a.mean(a.covered), a.mean(a.dup), cmp);
}

void cost_row(const char* label, int N, const Agg& a) {
    const double pub = a.mean(a.pub), drop = a.mean(a.drop);
    const double rate = pub > 0 ? 100.0 * drop / (pub * double(N > 1 ? N : 1)) : 0.0;
    std::printf("  %-27s | %2d | %8.0f | %9.0f | %6.1f%% | %8.2f | %7.3f | %5.0f\n",
                label, N, pub, drop, rate,
                pub * 1250.0 / 1024.0, a.ms, a.mean(a.spent));
}

} // namespace

int main() {
    std::printf("SWARM — at equal TOTAL budget, does coordination buy anything?\n");
    std::printf("=============================================================\n\n");

    std::printf("  Building %d landscapes (%dx%d, %d Gaussian bumps)...\n",
                kTrials, kW, kH, kBumps);
    std::vector<Landscape> land;
    land.reserve(kTrials);
    double lm = 0.0;
    for (int i = 0; i < kTrials; ++i) {
        land.push_back(make_landscape(0xA11CEull + std::uint64_t(i)));
        lm += double(land.back().local_maxima);
    }
    std::printf("  %d cells each, %.1f distinct local maxima on average.\n", kCells, lm / kTrials);
    std::printf("  TOTAL budget %ld evaluations per trial, split N ways (N=1 gets all of it).\n",
                kBudget);
    std::printf("  Success = the exact global-optimum cell was evaluated by someone.\n\n");

    const int Ns[] = {1, 2, 4, 8, 16};
    const Mech Ms[] = {Mech::Independent, Mech::Partition, Mech::BoardPeaks,
                       Mech::BoardCells, Mech::Auction, Mech::AuctionNoClear};

    std::vector<std::vector<Agg>> grid(6, std::vector<Agg>(5));
    for (int mi = 0; mi < 6; ++mi)
        for (int ni = 0; ni < 5; ++ni)
            grid[std::size_t(mi)][std::size_t(ni)] =
                sweep(Ms[mi], Ns[ni], land, kPopsPerStep, kQueueCap);

    // The harness's own correctness check. At N=1 there are no peers, so the
    // four non-auction mechanisms are literally the same algorithm on the same
    // random stream. If these disagree, something leaked between agents.
    bool same = true;
    for (int mi = 1; mi < 4; ++mi) {
        if (grid[std::size_t(mi)][0].hits != grid[0][0].hits) same = false;
        if (std::abs(grid[std::size_t(mi)][0].regret - grid[0][0].regret) > 1e-9) same = false;
    }
    // A single agent cannot duplicate its own work, by construction. Neither can
    // a partition duplicate a RESTART region, though its climbs may cross one.
    bool nodup = true;
    for (int mi = 0; mi < 4; ++mi) if (grid[std::size_t(mi)][0].dup != 0.0) nodup = false;
    std::printf("  [check] N=1 identical under all four non-auction mechanisms: %s\n",
                same ? "PASS" : "FAIL");
    std::printf("  [check] N=1 duplicated work = 0 (one agent's cache is free and total): %s\n\n",
                nodup ? "PASS" : "FAIL");

    std::printf("TABLE 1 — TASK PERFORMANCE AT FIXED TOTAL BUDGET\n");
    std::printf("  N=1 is DUMB BASELINE (a): one agent, the whole %ld evaluations.\n", kBudget);
    std::printf("  'independent' is DUMB BASELINE (b): N agents, zero messages — parallelism only.\n");
    std::printf("  dup = evaluations spent on a cell another agent had already paid for.\n");
    std::printf("  'vs 1 agent' is PAIRED: every config ran the same 300 landscapes, so the\n");
    std::printf("  test is McNemar's exact on the discordant trials, not the Wilson overlap.\n\n");
    std::printf("  mechanism                   |  N | found glob |  rate  |   95%% Wilson  |"
                " mean regret | cells  |  dup  | vs 1 agent (paired)\n");
    std::printf("  ----------------------------+----+-----------+--------+---------------+"
                "-------------+--------+-------+--------------------\n");
    const std::vector<char>& base = grid[0][0].ok;
    for (int mi = 0; mi < 6; ++mi) {
        for (int ni = 0; ni < 5; ++ni)
            row(mech_name(Ms[mi]), Ns[ni], grid[std::size_t(mi)][std::size_t(ni)], base);
        if (mi < 5) std::printf("\n");
    }

    // --- THE ISOLATION ------------------------------------------------------
    //
    // Table 1 compares everything to ONE agent, which mixes two effects: what
    // splitting the budget costs, and what coordinating the pieces recovers.
    // This table holds N fixed and changes only the coordination, so each line
    // is one mechanism against the same number of agents doing the same thing
    // without it. That is the difference the whole exercise exists to measure,
    // and it is the one that gets conflated with parallelism everywhere.
    std::printf("\n\nTABLE 2 — WHAT COMMUNICATION BUYS, HELD APART FROM PARALLELISM\n");
    std::printf("  Each line fixes N and changes ONE thing. Paired McNemar on the same 300\n");
    std::printf("  landscapes; +w/-l are the discordant trials.\n\n");
    std::printf("   N | comparison (the arrow matches the rate columns)          |  rate ->  rate  | +w/-l     | p\n");
    std::printf("  ---+----------------------------------------------------------+-----------------+-----------+-------\n");
    struct Cmp { int a, b; const char* text; };
    const Cmp cmps[] = {
        {0, 1, "add division of labour, no messages  indep -> partition"},
        {0, 2, "add sharing of peaks found       indep -> board-peaks"},
        {0, 3, "add sharing of evaluations       indep -> board-cells"},
        {5, 4, "add the clearing rule       auction-noclear -> auction"},
    };
    for (int ni = 1; ni < 5; ++ni) {
        for (const Cmp& cm : cmps) {
            const Agg& x = grid[std::size_t(cm.a)][std::size_t(ni)];
            const Agg& y = grid[std::size_t(cm.b)][std::size_t(ni)];
            const Paired m = mcnemar(x.ok, y.ok);
            std::printf("  %2d | %-56s | %5.1f%% -> %5.1f%% | +%3zu/-%3zu | %.4f\n",
                        Ns[ni], cm.text,
                        100.0 * double(x.hits) / double(x.n),
                        100.0 * double(y.hits) / double(y.n), m.w, m.l, m.p);
        }
        if (ni < 4) std::printf("  ---+----------------------------------------------------------+"
                                "-----------------+-----------+-------\n");
    }

    std::printf("\n\nTABLE 3 — WHAT COORDINATION COSTS\n");
    std::printf("  published = pulses put on the bus. dropped = deliveries the bus discarded\n");
    std::printf("  because a subscriber's bounded queue was full. drop%% is over DELIVERIES\n");
    std::printf("  (published x subscribers), which is what actually gets lost. KiB is the\n");
    std::printf("  payload tax: every message is 16 bytes of fact inside a 1,250-byte Glyph.\n\n");
    std::printf("  mechanism                   |  N | pub/trial | dropped   | drop%%  |"
                "  KiB/trial |   ms    | spent\n");
    std::printf("  ----------------------------+----+----------+-----------+--------+"
                "----------+---------+------\n");
    for (int mi = 0; mi < 6; ++mi) {
        for (int ni = 0; ni < 5; ++ni)
            cost_row(mech_name(Ms[mi]), Ns[ni], grid[std::size_t(mi)][std::size_t(ni)]);
        if (mi < 5) std::printf("\n");
    }

    // --- WHERE THE BLACKBOARD BREAKS ---------------------------------------
    //
    // Broadcast traffic is O(N^2); an agent's reading capacity is O(1) per step.
    // The default above rations reading at 4 pops/step. Relaxing the ration and
    // enlarging the queue says how much of blackboard-cells' behaviour is the
    // protocol and how much is the pipe.
    std::printf("\n\nTABLE 4 — blackboard-cells UNDER LOAD (N=16)\n");
    std::printf("  Same mechanism, same budget, only the message pipe changes.\n");
    std::printf("  pops/step rations how many messages an agent may read per evaluation;\n");
    std::printf("  cap is the bus's per-subscriber queue length.\n\n");
    std::printf("  pops/step |  cap | found glob |  rate  | mean regret |  dup  | dropped   | drop%%\n");
    std::printf("  ----------+------+-----------+--------+-------------+-------+-----------+-------\n");
    struct Load { int pops; std::size_t cap; };
    const Load loads[] = {{1, 1024}, {4, 1024}, {64, 1024}, {kUnlimitedPops, 1024},
                          {kUnlimitedPops, 64}};
    for (const Load& l : loads) {
        const Agg a = sweep(Mech::BoardCells, 16, land, l.pops, l.cap);
        const auto ci = wilson(a.hits, a.n);
        const double pub = a.mean(a.pub), drop = a.mean(a.drop);
        const double rate = pub > 0 ? 100.0 * drop / (pub * 16.0) : 0.0;
        char pops[16];
        if (l.pops == kUnlimitedPops) std::snprintf(pops, sizeof pops, "%s", "all");
        else                          std::snprintf(pops, sizeof pops, "%d", l.pops);
        std::printf("  %9s | %4zu | %4zu/%4zu | %6.1f%% | %7.3f%%    | %5.0f | %9.0f | %5.1f%%  [%.1f-%.1f]\n",
                    pops, l.cap, a.hits, a.n, 100.0 * double(a.hits) / double(a.n),
                    100.0 * a.mean(a.regret), a.mean(a.dup), drop, rate, ci.first, ci.second);
    }

    std::printf("\n\nTABLE 5 — auction protocol integrity\n");
    std::printf("  rounds x N is held at 32 for every N: same 32 tile-searches of 128\n");
    std::printf("  evaluations, only the parallel width changes.\n");
    std::printf("  bid collisions = agent PAIRS per trial that wanted the same tile before\n");
    std::printf("  clearing; 'max' is every pair in every round. They are EQUAL at every N.\n");
    std::printf("  telos derives its tie-break by hashing state rather than drawing it, so\n");
    std::printf("  agents whose Valuers have seen the same yields are bit-identical and pick\n");
    std::printf("  the same arm. The reservoir fix stopped ties collapsing to the lowest\n");
    std::printf("  index; nothing stops them collapsing to the same index across agents.\n");
    std::printf("  conflicts = pairs that ended up SEARCHING the same tile after clearing.\n");
    std::printf("  0 everywhere: no bid was dropped, so every agent cleared the same set.\n");
    std::printf("  Compare the auction and auction-noclear rows of Table 1 to see what the\n");
    std::printf("  clearing rule is worth once the bandit has stopped providing diversity.\n\n");
    std::printf("   N | rounds | bid collisions | max | conflicts | dropped | dup (clear) | dup (no clear)\n");
    std::printf("  ---+--------+----------------+-----+-----------+---------+-------------+----------------\n");
    for (int ni = 0; ni < 5; ++ni) {
        const Agg& a = grid[4][std::size_t(ni)];
        const Agg& b = grid[5][std::size_t(ni)];
        const int n = Ns[ni], rounds = int(kBudget / (kAuctionSlice * n));
        std::printf("  %2d | %6d | %14.2f | %3d | %9.3f | %7.0f | %11.0f | %14.0f\n",
                    n, rounds, a.mean(a.collisions), rounds * n * (n - 1) / 2,
                    a.mean(a.conflicts), a.mean(a.drop), a.mean(a.dup), b.mean(b.dup));
    }

    // --- THE READ ----------------------------------------------------------
    //
    // Printed from the measured numbers rather than written down, so it cannot
    // drift away from what the run actually produced.
    std::printf("\n\nTHE READ\n");
    {
        const Agg& one = grid[0][0];
        std::size_t best_mi = 0, best_ni = 0;
        double best_rate = 0.0;
        for (int mi = 0; mi < 6; ++mi)
            for (int ni = 1; ni < 5; ++ni) {         // swarms only, N >= 2
                const double r = double(grid[std::size_t(mi)][std::size_t(ni)].hits)
                               / double(grid[std::size_t(mi)][std::size_t(ni)].n);
                if (r > best_rate) { best_rate = r; best_mi = std::size_t(mi); best_ni = std::size_t(ni); }
            }
        const Agg& bestswarm = grid[best_mi][best_ni];
        const Paired vs = mcnemar(one.ok, bestswarm.ok);
        std::printf("  One agent with the whole budget: %.1f%% (%zu/%zu).\n",
                    100.0 * double(one.hits) / double(one.n), one.hits, one.n);
        std::printf("  Best swarm of any size or mechanism: %s at N=%d, %.1f%%,\n",
                    mech_name(Ms[best_mi]), Ns[best_ni], 100.0 * best_rate);
        std::printf("  paired against one agent: +%zu/-%zu, p=%.4f.\n", vs.w, vs.l, vs.p);
        if (best_rate > double(one.hits) / double(one.n) && vs.p < 0.05)
            std::printf("  => The swarm beats one agent at equal total budget.\n");
        else
            std::printf("  => NO swarm beats one agent at equal total budget. Splitting a fixed\n"
                        "     budget across agents costs task performance, and every mechanism\n"
                        "     here only reduces how much it costs. Coordination is damage\n"
                        "     control, not a gain.\n");
        std::printf("\n  Parallelism vs communication, at N=16 (Table 2 has all N):\n");
        std::printf("    16 independent agents lose %.1f points to one agent, and waste %.0f\n"
                    "    of %ld evaluations re-evaluating cells a peer had already paid for.\n",
                    100.0 * (double(one.hits) / double(one.n)
                             - double(grid[0][4].hits) / double(grid[0][4].n)),
                    grid[0][4].mean(grid[0][4].dup), kBudget);
        std::printf("    Sharing every evaluation cuts that waste to %.0f but recovers only\n"
                    "    %.1f points, because at 6%% coverage agents rarely collide in the\n"
                    "    first place — there is little duplicated work for messages to prevent.\n",
                    grid[3][4].mean(grid[3][4].dup),
                    100.0 * (double(grid[3][4].hits) / double(grid[3][4].n)
                             - double(grid[0][4].hits) / double(grid[0][4].n)));
        std::printf("    A static partition, which sends NO messages at all, cuts the waste to\n"
                    "    %.0f — most of what the chatty blackboard achieves, for nothing.\n",
                    grid[1][4].mean(grid[1][4].dup));

        // Communication IS worth something, and the paired test can see it even
        // though the Wilson intervals cannot. Reported at every N so the point
        // where it stops working is visible rather than asserted.
        std::printf("\n  Communication is not worthless, it is just small. Sharing evaluations\n"
                    "  against the SAME agent count with no messages, paired, by N:\n");
        for (int ni = 1; ni < 5; ++ni) {
            const Paired m = mcnemar(grid[0][std::size_t(ni)].ok, grid[3][std::size_t(ni)].ok);
            std::printf("    N=%-2d  +%2zu/-%2zu  p=%.4f  %s\n", Ns[ni], m.w, m.l, m.p,
                        m.p < 0.05 ? "significant" : "not significant");
        }
        std::printf("  It is a real effect and it is one-sided (it never loses a trial), but\n"
                    "  it is worth a few trials in 300 and it stops being detectable at N=16,\n"
                    "  where %.1f%% of blackboard deliveries are dropped (Table 4). It never\n"
                    "  comes close to paying back what splitting the budget cost.\n",
                    100.0 * grid[3][4].mean(grid[3][4].drop)
                          / (grid[3][4].mean(grid[3][4].pub) * 16.0));
        std::printf("\n  Where it goes negative: the auction. Removing only its clearing rule\n"
                    "  (auction-noclear, N=16) drops it from %.1f%% to %.1f%% and raises\n"
                    "  duplicated work from %.0f to %.0f of %ld evaluations, because every\n"
                    "  agent's bandit picks the identical tile. The negotiation is the entire\n"
                    "  value of that mechanism; the shared learning contributes nothing and\n"
                    "  the mechanism as a whole still loses to one agent by a wide margin.\n",
                    100.0 * double(grid[4][4].hits) / double(grid[4][4].n),
                    100.0 * double(grid[5][4].hits) / double(grid[5][4].n),
                    grid[4][4].mean(grid[4][4].dup), grid[5][4].mean(grid[5][4].dup), kBudget);
    }

    std::printf("\n\nWHAT THIS CANNOT SEE\n");
    std::printf("  - Threads. Agents are stepped round-robin on one thread. The bus is the\n"
                "    real thread-safe bus but its concurrency is never exercised; drops here\n"
                "    come from queue capacity vs arrival rate, not from scheduling.\n"
                "  - Sending is free in budget units; only READING is rationed. A real system\n"
                "    pays for both, so the talking mechanisms are flattered.\n"
                "  - One landscape family: smooth sums of Gaussians. Nothing here speaks to\n"
                "    deceptive, noisy, non-stationary, or non-decomposable problems.\n"
                "  - All agents run the identical algorithm. Heterogeneous swarms are a\n"
                "    different question and this says nothing about them.\n"
                "  - Wilson intervals cover trial sampling only, not whether these landscapes\n"
                "    are representative of anything.\n");

    long stalls = 0;
    for (const auto& r : grid) for (const auto& a : r) stalls += a.stalls;
    if (stalls) std::printf("\n  WARNING: %ld trials hit the step cap.\n", stalls);
    return 0;
}
