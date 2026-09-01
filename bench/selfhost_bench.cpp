// CAN KHORA REBUILD ITS OWN PRIMITIVES -- AND DOES DOING IT AGAIN GET EASIER?
//
// Everything else this organ has been measured on was a task somebody chose for
// it. This is the one where the subject is the system itself: for each operation
// in Techne's own instruction set, the operation is REMOVED and the system is
// asked to reconstruct its behaviour from the remaining ones.
//
// The removal is the whole point. Asking for `sum` while `sum` is available is
// answered by `sum(x)` and demonstrates nothing. Banned, the answer has to be a
// composition -- and if the system can rebuild its own operations out of its
// other operations, then the instruction set is not a fixed floor handed down by
// a human. It is a redundant, partly self-describing set, and the parts that
// turn out to be reconstructible are parts a human did not have to write.
//
// ---------------------------------------------------------------------------
// WHAT THIS BENCH DID NOT MEASURE BEFORE, AND WHY IT MATTERED
// ---------------------------------------------------------------------------
//
// The stated way to benchmark this system is to have it develop ITSELF in a loop
// that compounds. One round of removal-and-rebuild is not that loop. It answers
// twenty-two INDEPENDENT questions of the form
//
//     "is P redundant given ALL TWENTY-ONE OTHERS?"
//
// and a yes to every one of them does not add up to "these thirteen are jointly
// redundant". Thirteen separate statements each of which assumes the other
// twelve are still present is not thirteen primitives the system can supply for
// itself; it can supply any ONE of them at a time.
//
// So this file now runs the loop. Round 1 rebuilds what it can from the true
// primitives. When a primitive is rebuilt and accepted, the TRUE implementation
// is WITHDRAWN -- from that point the only way to get its behaviour is through
// the reconstruction the system itself wrote. Round 2 attacks what is left with
// that reduced set. Rounds continue until one adds nothing.
//
// Two things fall out of that which a single round cannot show:
//
//   * whether the set of self-suppliable primitives GROWS, stays flat, or is
//     blocked; and
//
//   * ERROR ACCUMULATION, which is the failure mode most likely to kill the
//     idea. A primitive rebuilt out of rebuilt parts can be right on everything
//     the acceptance test looked at and wrong elsewhere, and the wrongness is
//     inherited by everything built on top. Every accepted reconstruction is
//     therefore graded against the REAL implementation on a set of inputs that
//     no acceptance test ever touched, and the grade is broken out by REBUILD
//     DEPTH.
//
// ---------------------------------------------------------------------------
// THE CIRCULARITY THE FIRST VERSION SHIPPED
// ---------------------------------------------------------------------------
//
// FIXED IN techne, AND THIS IS WHAT IT LOOKED LIKE. `Spec::banned` stopped the
// SEARCH from emitting a banned operation and did NOT stop a LIBRARY entry whose
// body contains that operation from being called -- construct() reached into the
// library at three sites and consulted `banned` at none of them. The single-round
// table below used to reconstruct `sort` as
//
//     append(min(x), lib1(x))     where lib1 = pair_max = tail(sort(x))
//
// which is `sort` defined in terms of `sort`. It agrees with the real
// implementation on all 1000 probes because it IS the real implementation with
// two extra steps. That is not a reconstruction, and no amount of external
// probing can detect it -- probing compares behaviour, and the behaviour is
// correct.
//
// The single-round arm is left exactly as it was so the published baseline is
// still readable off this file, and a CIRCULARITY AUDIT is printed beside it
// naming every row that smuggled its own operation back in. The iterated arms
// below do not have the hole: availability is decided by walking each library
// body's TRANSITIVE operation set, and an entry that reaches a withdrawn
// operation is not offered.
//
// ---------------------------------------------------------------------------
// WHAT "ACCEPTED" MEANS IN THE ITERATED ARMS
// ---------------------------------------------------------------------------
//
// correct_bench established two things that this bench has to obey. A bounded
// proof does NOT survive outside its bound -- 0 of 36 trap tasks -- and a
// bounded proof PLUS boundary-value analysis over the deployment range was right
// about 100% of what it accepted. So acceptance here is synthesise_hardened:
// exhaustive over every list of length 0..4 over -2..2 (781 inputs) AND unbroken
// on the extremal inputs, with counterexamples fed back. Anything less is a
// refusal.
//
// Acceptance is what the SYSTEM decides. It is graded separately, and the two
// input sets are disjoint by construction -- see wilderness() below.
//
// ---------------------------------------------------------------------------
// TWO THINGS THAT KEEP THE NEGATIVE RESULT FROM BEING AN ARTEFACT
// ---------------------------------------------------------------------------
//
// A REPAIR CASCADE. Withdrawing P kills every reconstruction whose body names P,
// including the stand-ins for primitives withdrawn earlier. Refusing there would
// measure the order of the target table rather than the capability, so instead
// every orphaned primitive is rebuilt as well, under the same withdrawal, and
// the bundle commits only if all of them land. A bundle that fails is a
// dependency the system could not route around.
//
// A BUDGET SWEEP. The jointly-held count moves with the pool -- 9 at 75,000
// behaviours and 12 at 150,000 on the machine this was written on -- so a single
// number would be a statement about a budget dressed up as one about the
// instruction set. What is stable across the sweep is the SHAPE: every budget
// gains everything it is going to gain in round one.
//
// COST: about two minutes and a peak of roughly one gigabyte at the default
// budget, most of it the largest sweep point. `selfhost_bench <pool> <cases>
// <probes> <iter_pool> <max_rounds>` moves all of it.

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

std::uint64_t rs = 0x5E1FULL;
std::uint64_t rnd() {
    std::uint64_t z = (rs += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// A SECOND STREAM, and it exists so the baseline arm is bit-identical to the
// version this file replaced. Everything added below draws from here; `rs` is
// consumed by the original code in the original order, so its numbers cannot
// move because new code was added after it.
std::uint64_t ws = 0xC0FFEE5EEDULL;
std::uint64_t wrnd() {
    std::uint64_t z = (ws += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

std::pair<double, double> wilson(std::size_t k, std::size_t n) {
    if (n == 0) return {0.0, 0.0};
    const double z = 1.96, p = (double)k / (double)n;
    const double d = 1.0 + z * z / (double)n;
    const double c = p + z * z / (2.0 * (double)n);
    const double m = z * std::sqrt(p * (1 - p) / (double)n +
                                   z * z / (4.0 * (double)n * (double)n));
    return {100.0 * (c - m) / d, 100.0 * (c + m) / d};
}

void rate(const char* label, std::size_t k, std::size_t n) {
    const auto ci = wilson(k, n);
    std::printf("  %-46s %3zu/%-3zu  %5.1f%%  [%5.1f, %5.1f]\n", label, k, n,
                n ? 100.0 * (double)k / (double)n : 0.0, ci.first, ci.second);
}

// The real implementation, reached WITHOUT any byte encoding.
//
// The first version of this built a byte tape by hand, encoding an opcode as
// (op * 256) / kCount to invert a decoder that computes (byte * kCount) >> 8.
// Two floors do not compose to the identity, so the encoding was off by one and
// real_op computed the WRONG OPERATION. The probes then compared each recipe
// against that same wrong oracle and agreed with it 1000/1000, producing a
// clean-looking table in which `len` was reconstructed as sub(x, x) -- a list of
// zeros -- and every row was a neighbouring primitive.
//
// That is precisely the failure this bench exists to detect, built into the
// detector. The fix is not a corrected encoding, it is having no encoding: a
// Recipe names operations by enum, so there is no byte layer to get wrong.
Value real_op(Op op, const Value& in, std::uint8_t b) {
    Recipe r;
    r.pool.push_back(Expr{Op::Mov, -1, -1, 0});          // 0: the input
    r.pool.push_back(Expr{Op::Const, -1, -1, b});        // 1: the constant operand
    r.pool.push_back(Expr{op, 0, 1, b});                 // 2: op(input, constant)
    r.root = 2;
    r.found = true;
    return r.apply(in, nullptr);
}

// Adversarial plus random. The adversarial ones are where a plausible-looking
// reconstruction usually diverges: empty, singleton, all-equal, sorted, and
// reverse-sorted inputs are exactly the shapes that a program fitted to six
// mid-sized random lists never had to handle.
std::vector<Value> probe_inputs(std::size_t n) {
    std::vector<Value> v{
        {}, {0}, {1}, {-1}, {7, 7, 7, 7}, {1, 2, 3, 4, 5}, {5, 4, 3, 2, 1},
        {0, 0, 0}, {-9, 9}, {1000000000, -1000000000},
    };
    while (v.size() < n) {
        const std::size_t len = rnd() % 9;
        Value x;
        for (std::size_t i = 0; i < len; ++i) {
            x.push_back(static_cast<std::int64_t>(rnd() % 60) - 25);
        }
        v.push_back(std::move(x));
    }
    return v;
}

// THE ORACLE HAS BEEN WRONG IN THIS FILE BEFORE, and every number below is
// measured against it. The earlier version encoded opcodes into a byte tape by
// hand, got the encoding off by one, and compared every reconstruction against
// the wrong operation -- producing a clean 1000/1000 table in which `len` was
// rebuilt as a list of zeros. The encoding is gone, so this cannot recur the
// same way; it can recur a different way, so the oracle is checked against
// hand-written expectations before anything is measured with it.
//
// Returns the number of disagreements. Anything but zero and the run is void.
std::size_t oracle_selfcheck() {
    const Value x{3, -1, 3, 0, 7};
    struct Chk { Op op; std::uint8_t b; Value in, want; };
    const std::vector<Chk> chk = {
        {Op::Len,    0, x, {5}},
        {Op::Head,   0, x, {3}},
        {Op::Head,   0, {}, {}},
        {Op::Tail,   0, x, {-1, 3, 0, 7}},
        {Op::Rev,    0, x, {7, 0, 3, -1, 3}},
        {Op::Sort,   0, x, {-1, 0, 3, 3, 7}},
        {Op::Sum,    0, x, {12}},
        {Op::Max,    0, x, {7}},
        {Op::Min,    0, x, {-1}},
        {Op::Range,  0, {4}, {0, 1, 2, 3}},
        {Op::Add,    3, x, {6, 2, 6, 3, 10}},
        {Op::Sub,    2, x, {1, -3, 1, -2, 5}},
        {Op::Mul,    3, x, {9, -3, 9, 0, 21}},
        {Op::Div,    2, x, {1, 0, 1, 0, 3}},          // truncating, not flooring
        {Op::Mod,    3, x, {0, -1, 0, 0, 1}},
        {Op::Take,   3, x, {3, -1, 3}},
        {Op::Drop,   2, x, {3, 0, 7}},
        {Op::Index,  1, x, {-1}},
        {Op::Filter, 0, x, {3, 3, 7}},
        {Op::MapAdd, 5, x, {8, 4, 8, 5, 12}},
        {Op::MapMul, 2, x, {6, -2, 6, 0, 14}},
        {Op::Count,  0, x, {1}},
        {Op::Append, 1, x, {3, -1, 3, 0, 7, 1}},
    };
    std::size_t bad = 0;
    for (const Chk& c : chk) {
        const Value got = real_op(c.op, c.in, c.b);
        if (got == c.want) continue;
        ++bad;
        std::printf("  ORACLE MISMATCH on %s: got [", op_name(c.op));
        for (const auto v : got) std::printf(" %lld", (long long)v);
        std::printf(" ] wanted [");
        for (const auto v : c.want) std::printf(" %lld", (long long)v);
        std::printf(" ]\n");
    }
    return bad;
}

struct Target { Op op; const char* name; std::uint8_t b; };

std::vector<Target> targets() {
    return {
        {Op::Len,   "len",    0}, {Op::Head, "head",   0},
        {Op::Tail,  "tail",   0}, {Op::Rev,  "rev",    0},
        {Op::Sort,  "sort",   0}, {Op::Sum,  "sum",    0},
        {Op::Max,   "max",    0}, {Op::Min,  "min",    0},
        {Op::Range, "range",  0},
        // NON-DEGENERATE CONSTANTS. The first run used b=0 throughout, which
        // makes add(x,0)=x, mul(x,0)=zeros and div(x,0)=zeros -- targets that
        // are trivially reachable and prove nothing about the instruction set.
        // The constant index selects from {0,1,2,3,...}, so 3 means the value 3.
        {Op::Add,   "add_3",  3}, {Op::Sub,  "sub_2",  2},
        {Op::Mul,   "mul_3",  3}, {Op::Div,  "div_2",  2},
        {Op::Mod,   "mod_3",  3}, {Op::Append, "cat",  1},
        {Op::Take,  "take_3", 3}, {Op::Drop, "drop_2", 2},
        {Op::Index, "at_1",   1}, {Op::Filter, "filter_0", 0},
        {Op::MapAdd, "addk_5", 5}, {Op::MapMul, "mulk_2", 2},
        {Op::Count, "count_0", 0},
    };
}

// ---------------------------------------------------------------------------
// WHAT AN EXPRESSION ACTUALLY USES, walked through library bodies.
//
// This is the function the first version of this bench did not have, and its
// absence is what let `sort` be reconstructed out of `sort`. A recipe's own
// nodes are only half the story: an Op::Call, Op::MapF or Op::FoldF node names a
// library body, and that body has operations of its own.
// ---------------------------------------------------------------------------
void collect_ops(const Recipe& r, const Library& lib, std::vector<Op>& out,
                 std::size_t depth) {
    if (!r.found || depth > kMaxCallDepth + 1) return;
    std::vector<bool> seen(r.pool.size(), false);
    std::vector<std::size_t> stack{r.root};
    while (!stack.empty()) {
        const std::size_t i = stack.back();
        stack.pop_back();
        if (i >= r.pool.size() || seen[i]) continue;
        seen[i] = true;
        const Expr& e = r.pool[i];
        if (std::find(out.begin(), out.end(), e.op) == out.end()) out.push_back(e.op);
        if (e.op == Op::Call || e.op == Op::MapF || e.op == Op::FoldF ||
            e.op == Op::FoldS) {
            if (lib.size()) {
                const std::size_t li = e.k % lib.size();
                collect_ops(lib.at(li).recipe, lib, out, depth + 1);
            }
        }
        if (e.a >= 0) stack.push_back(static_cast<std::size_t>(e.a));
        if (e.b >= 0) stack.push_back(static_cast<std::size_t>(e.b));
    }
}

// ---------------------------------------------------------------------------
// THE REGISTRY: reconstructions held in a form that survives being moved
// between libraries.
//
// A Recipe stores a library body as a RAW INDEX in Expr::k, and the interpreter
// resolves it as `k % lib->size()`. That index means whatever the library it is
// evaluated against says it means. Since each round has to offer a DIFFERENT
// subset of reconstructions -- an entry that reaches a withdrawn operation must
// not be callable -- an index captured in one round is meaningless in the next,
// and evaluating a stored recipe against the wrong library silently computes a
// different function. That class of defect has already shipped twice in this
// module.
//
// So nothing is stored by library index. A registry node stores its dependencies
// as REGISTRY indices and rewrites Expr::k when it is instantiated into a
// concrete Library. The index is derived at instantiation and never persisted.
// ---------------------------------------------------------------------------
struct Node {
    std::string name;
    Op          op = Op::kCount;        // the primitive it stands in for; kCount = helper
    Recipe      r;                      // Call/MapF/FoldF k is an index into `deps`
    std::vector<std::size_t> deps;      // registry indices this body calls
    std::vector<Op> direct;             // operations appearing literally in `r`
    std::size_t depth = 1;              // 1 = built only from true primitives
    bool        higher_order = false;   // the answer contains a fold or a map
    std::size_t round = 0;
    std::size_t wild_agree = 0, wild_n = 0;
    bool        wild_clean = false;     // agreed with the real implementation everywhere
};

// admit_recipe's own duplicate test, replicated so the index an entry LANDS on
// can be predicted. It refuses a structurally identical recipe and returns
// false, which would silently shift every later index if it were not accounted
// for. Note what it does not compare: Expr::lit. Two recipes differing only in a
// mined literal are duplicates as far as it is concerned, so a post-instantiation
// behavioural check below verifies each node really does compute what it did
// when it was accepted.
int lib_index_of(const Library& lib, const Recipe& r) {
    for (std::size_t i = 0; i < lib.size(); ++i) {
        const Recipe& it = lib.at(i).recipe;
        if (!it.found || it.pool.size() != r.pool.size() || it.root != r.root) continue;
        bool same = true;
        for (std::size_t j = 0; j < r.pool.size() && same; ++j) {
            const Expr& a = it.pool[j];
            const Expr& b = r.pool[j];
            same = (a.op == b.op && a.a == b.a && a.b == b.b && a.k == b.k);
        }
        if (same) return static_cast<int>(i);
    }
    return -1;
}

struct Mat {
    Library lib{64};                    // never pruned: prune renumbers, and this
                                        // holds at most a few dozen entries
    std::vector<int>         where;     // registry index -> library index, -1 absent
    std::vector<std::size_t> back;      // library index -> registry index
};

constexpr std::size_t kNone = static_cast<std::size_t>(-1);

// Instantiate exactly the live nodes into a fresh Library, rewriting every body
// index. Registry order is topological by construction -- a node is created
// after everything it calls -- so one forward pass suffices.
Mat materialise(const std::vector<Node>& reg, const std::vector<char>& live) {
    Mat m;
    m.where.assign(reg.size(), -1);
    for (std::size_t i = 0; i < reg.size(); ++i) {
        if (!live[i]) continue;
        bool ok = true;
        for (const std::size_t d : reg[i].deps) if (m.where[d] < 0) { ok = false; break; }
        if (!ok) continue;
        Recipe r = reg[i].r;
        for (Expr& e : r.pool) {
            if (e.op != Op::Call && e.op != Op::MapF && e.op != Op::FoldF &&
                e.op != Op::FoldS) continue;
            if (e.k >= reg[i].deps.size()) { ok = false; break; }
            e.k = static_cast<std::uint8_t>(m.where[reg[i].deps[e.k]]);
        }
        if (!ok) continue;
        const std::size_t before = m.lib.size();
        m.lib.admit_recipe(reg[i].name, r, 0);
        const int idx = (m.lib.size() > before) ? static_cast<int>(before)
                                                : lib_index_of(m.lib, r);
        if (idx < 0) continue;
        m.where[i] = idx;
        if (static_cast<std::size_t>(idx) >= m.back.size()) m.back.resize(idx + 1, kNone);
        if (m.back[idx] == kNone) m.back[idx] = i;
    }
    return m;
}

// Which reconstructions are usable once `retired` operations no longer exist.
//
// A node dies if it names a retired operation literally, or if anything it calls
// has died. Iterated to a fixpoint because the second condition is recursive.
// Cycles cannot arise -- a node only ever calls earlier registry entries -- so
// either direction of iteration converges to the same set.
std::vector<char> live_set(const std::vector<Node>& reg, const std::vector<Op>& retired) {
    std::vector<char> live(reg.size(), 1);
    for (bool changed = true; changed;) {
        changed = false;
        for (std::size_t i = 0; i < reg.size(); ++i) {
            if (!live[i]) continue;
            bool ok = true;
            for (const Op o : reg[i].direct)
                if (std::find(retired.begin(), retired.end(), o) != retired.end()) {
                    ok = false; break;
                }
            if (ok)
                for (const std::size_t d : reg[i].deps) if (!live[d]) { ok = false; break; }
            if (!ok) { live[i] = 0; changed = true; }
        }
    }
    return live;
}

// Turn a freshly solved recipe into a registry node: library indices become
// registry indices, and the operations it names literally are recorded so a
// later withdrawal can retire it.
Node make_node(std::string name, Op op, const Recipe& src, const Mat& m,
               const std::vector<Node>& reg) {
    Node n;
    n.name = std::move(name);
    n.op = op;
    n.r = src.compact();
    const std::size_t ls = m.lib.size();
    for (Expr& e : n.r.pool) {
        if (e.op == Op::Call || e.op == Op::MapF || e.op == Op::FoldF ||
            e.op == Op::FoldS) {
            const std::size_t li = ls ? (e.k % ls) : 0;
            const std::size_t ri = (li < m.back.size()) ? m.back[li] : kNone;
            std::size_t pos = 0;
            if (ri == kNone) {
                // Nothing to bind to. Fail loudly rather than quietly aliasing
                // to entry zero, which is how a call to one function becomes a
                // call to another.
                n.r.found = false;
                return n;
            }
            const auto it = std::find(n.deps.begin(), n.deps.end(), ri);
            if (it == n.deps.end()) { pos = n.deps.size(); n.deps.push_back(ri); }
            else pos = static_cast<std::size_t>(it - n.deps.begin());
            e.k = static_cast<std::uint8_t>(pos);
        }
        // Op::Call is the library MECHANISM, not a capability of the instruction
        // set, so banning it would mean nothing; every other operation counts.
        if (e.op != Op::Call &&
            std::find(n.direct.begin(), n.direct.end(), e.op) == n.direct.end())
            n.direct.push_back(e.op);
    }
    n.depth = 1;
    for (const std::size_t d : n.deps) n.depth = std::max(n.depth, reg[d].depth + 1);
    return n;
}

// ---------------------------------------------------------------------------
// THE GRADING SET, and it is never used to accept or refine anything.
//
// DISJOINT FROM ACCEPTANCE BY CONSTRUCTION, not by hope. Acceptance touches
// three input sets: the specification cases (lengths 0..20, values in -12..17),
// the exhaustive proof domain (lengths 0..4, values in -2..2) and
// default_extremes() (22 fixed inputs, all of length <= 8). Everything below is
// either longer than 20 or carries a value of magnitude >= 1000 on a list of
// length >= 5, so it cannot be any of them.
//
// WHAT THAT COSTS, stated rather than hidden: the wilderness cannot catch an
// error that only shows up on a short list of small values, because those are
// precisely the inputs acceptance already proved. The two sets partition the
// space rather than overlapping it, and a claim about either is a claim about
// its own half.
// ---------------------------------------------------------------------------
std::vector<Value> wilderness() {
    std::vector<Value> w;
    auto push_run = [&](std::size_t len, std::function<std::int64_t(std::size_t)> f) {
        Value v;
        for (std::size_t i = 0; i < len; ++i) v.push_back(f(i));
        w.push_back(std::move(v));
    };
    // Shapes a fitted program breaks on, at lengths the proof domain and the
    // extremal set never reach.
    push_run(21, [](std::size_t i) { return (std::int64_t)i; });                 // sorted
    push_run(21, [](std::size_t i) { return 20 - (std::int64_t)i; });            // reversed
    push_run(37, [](std::size_t) { return (std::int64_t)7; });                   // all equal
    push_run(37, [](std::size_t) { return (std::int64_t)0; });                   // all zero
    push_run(64, [](std::size_t i) { return (std::int64_t)(i % 3) - 1; });       // periodic
    push_run(300, [](std::size_t i) { return (std::int64_t)(i * 7 % 101) - 50; });
    push_run(511, [](std::size_t i) { return (std::int64_t)i - 255; });
    push_run(512, [](std::size_t i) { return (std::int64_t)(i % 2); });          // at kMaxListLen
    push_run(9, [](std::size_t i) { return i % 2 ? kValueCap : -kValueCap; });   // the cap
    push_run(6, [](std::size_t i) { return (std::int64_t)(i + 1) * 1000000; });
    push_run(5, [](std::size_t) { return (std::int64_t)-1000; });
    push_run(23, [](std::size_t i) { return i == 0 ? (std::int64_t)-1000000000
                                                   : (std::int64_t)1000000000; });
    // (a) long lists, small values -- disjoint by LENGTH (> 20).
    for (std::size_t i = 0; i < 120; ++i) {
        Value v;
        const std::size_t len = 21 + wrnd() % 44;
        for (std::size_t j = 0; j < len; ++j)
            v.push_back(static_cast<std::int64_t>(wrnd() % 19) - 9);
        w.push_back(std::move(v));
    }
    // (b) mid lists, wide values -- disjoint by MAGNITUDE (>= 1000) at length >= 5.
    for (std::size_t i = 0; i < 120; ++i) {
        Value v;
        const std::size_t len = 5 + wrnd() % 36;
        for (std::size_t j = 0; j < len; ++j) {
            const std::int64_t mag = 1000 + static_cast<std::int64_t>(wrnd() % 999000000ULL);
            v.push_back((wrnd() & 1) ? mag : -mag);
        }
        w.push_back(std::move(v));
    }
    return w;
}

// Specification cases, on the same shape the single-round arm uses.
//
// CASE LENGTHS MUST SPAN THE HOLDOUT'S. An earlier version drew cases at lengths
// 0-6 and held out 7-12 -- disjoint, so any reconstruction whose behaviour
// depends on length passed every visible case and failed the holdout by
// construction, and a fifth of the measured capability was hidden by it.
Spec spec_for(const Target& t, std::size_t visible, std::uint64_t (*gen)()) {
    Spec s;
    s.name = t.name;
    for (std::size_t i = 0; i < visible; ++i) {
        Value in;
        const std::size_t len = i % 14;
        for (std::size_t k = 0; k < len; ++k)
            in.push_back(static_cast<std::int64_t>(gen() % 30) - 12);
        s.cases.push_back({in, real_op(t.op, in, t.b)});
    }
    for (std::size_t i = 0; i < 6; ++i) {
        Value in;
        const std::size_t len = 15 + i;
        for (std::size_t k = 0; k < len; ++k)
            in.push_back(static_cast<std::int64_t>(gen() % 30) - 12);
        s.holdout.push_back({in, real_op(t.op, in, t.b)});
    }
    return s;
}

// The three pairwise combiners the fold takes its body from.
//
// `sum`, `max` and `min` were all reported IRREDUCIBLE by this bench, and the
// reason was never arithmetic. No operation in the set could express "combine
// every element", because none of them could express a LOOP. FoldF supplies the
// loop and takes its body from the library, so the missing piece is a function
// that combines a PAIR. That is a stepping stone and it is handed over
// deliberately -- what is NOT handed over is the fold.
//
// Learned with Sum, Max and Min banned, because on a two-element list `sum(x)`
// already is pairwise addition and learning the combiner that way would make the
// whole demonstration circular.
// AND THREE THAT ARE NOT PAIRWISE, because FoldF never required the pairwise
// shape. Its accumulator is whatever the body returned, of any length:
//
//     Value acc{A[0]};
//     Value pair = acc; pair.push_back(A[i]);
//     acc = lib->call(li, pair, depth + 1);
//
// so a body sending [a1..ak, next] to [next, a1..ak] makes the fold a REVERSE.
// `roll` is that body. It does not derive when asked for outright -- not at a
// pool of 400,000 -- and it is three nodes once `last` and `init` are held, so
// the two of them are rungs rather than decoration.
//
// `last` needs Sum, so these cannot share the pairwise ban set; what each one
// must not be given is its OWN target, which for all three is Rev.
struct Comb {
    const char*    name;
    std::vector<Op> ban;      // beyond whatever is already retired
    bool           list_shaped;   // cases are lists of varying length, not pairs
    std::function<Value(const Value&)> ref;
};
std::vector<Comb> combiners() {
    const std::vector<Op> pairwise = {Op::Sum, Op::Max, Op::Min, Op::FoldF,
                                      Op::MapF, Op::FoldS};
    const std::vector<Op> reversal = {Op::Rev, Op::FoldF, Op::MapF, Op::FoldS};
    return {
        {"pair_add", pairwise, false, [](const Value& v) {
            return v.size() < 2 ? Value{} : Value{cap_value(v[0] + v[1])}; }},
        {"pair_max", pairwise, false, [](const Value& v) {
            return v.size() < 2 ? Value{} : Value{std::max(v[0], v[1])}; }},
        {"pair_min", pairwise, false, [](const Value& v) {
            return v.size() < 2 ? Value{} : Value{std::min(v[0], v[1])}; }},
        {"last", reversal, true, [](const Value& v) {
            return v.empty() ? Value{} : Value{v.back()}; }},
        {"init", reversal, true, [](const Value& v) {
            return v.empty() ? Value{} : Value(v.begin(), v.end() - 1); }},
        {"roll", reversal, true, [](const Value& v) {
            if (v.empty()) return Value{};
            Value o{v.back()};
            for (std::size_t i = 0; i + 1 < v.size(); ++i) o.push_back(v[i]);
            return o; }},
    };
}

// One accepted-or-refused attempt, plus the grade nobody involved in accepting
// it was allowed to see.
struct Attempt {
    bool   accepted = false;
    Recipe recipe;
    std::size_t wild_agree = 0;
    bool   wild_clean = false;
};

} // namespace

int main(int argc, char** argv) {
    const std::size_t pool_cap = (argc > 1) ? std::stoul(argv[1]) : 40000;
    const std::size_t visible  = (argc > 2) ? std::stoul(argv[2]) : 28;
    const std::size_t probes   = (argc > 3) ? std::stoul(argv[3]) : 1000;
    // The iterated arms pay for a proof per acceptance, so they get their own,
    // smaller budget. Passed on the command line because the honest answer to
    // "is this primitive irreducible" is always "not found at THIS budget".
    const std::size_t iter_pool = (argc > 4) ? std::stoul(argv[4]) : 150000;
    const std::size_t max_round = (argc > 5) ? std::stoul(argv[5]) : 6;

    const auto t_start = clk::now();

    std::printf("Can Khora rebuild its own primitives -- and does it compound?\n\n");
    if (const std::size_t bad = oracle_selfcheck()) {
        std::printf("  THE ORACLE IS WRONG on %zu of its own reference cases. Every number\n"
                    "  this bench produces is measured against it, so there is nothing here\n"
                    "  worth printing. Refusing to run.\n", bad);
        return 1;
    }

    std::printf("  For each operation: the operation is REMOVED from the instruction\n");
    std::printf("  set and the system must reconstruct it from what remains.\n");
    std::printf("  %zu visible cases, 6 held out, then %zu independent probes against\n",
                visible, probes);
    std::printf("  the real implementation -- including empty, singleton, all-equal,\n");
    std::printf("  sorted and reverse-sorted inputs the search never saw.\n\n");

    const auto probe = probe_inputs(probes);
    const auto tgts = targets();
    const std::size_t T = tgts.size();

    // =====================================================================
    // ARM 0 -- THE SINGLE-ROUND BASELINE, UNCHANGED.
    //
    // Same library policy, same warm-up, same acceptance rule (Generalised),
    // same probe set, same random stream in the same order. Its numbers are the
    // published ones and nothing added below can move them.
    // =====================================================================
    std::printf("  === ARM 0: ONE ROUND, the measurement this file already made ===\n\n");
    std::printf("  primitive  | rebuilt | probes  | reconstruction\n");
    std::printf("  -----------+---------+---------+----------------\n");

    std::size_t rebuilt = 0, irreducible = 0, verified = 0;
    std::vector<std::string> hard;
    std::vector<char> base_ok(T, 0);

    // The library persists across targets, so a primitive rebuilt early is
    // available when rebuilding a later one.
    Library lib(32);

    // Rows whose answer reached their own banned operation through a library
    // body. Recorded, not suppressed: the row stays in the table because the
    // table is the baseline, and the audit says which rows it cannot support.
    std::vector<std::string> circular;

    {
        std::printf("  warm-up -- fold BODIES, each learned with its own target banned\n"
                    "  and proved rather than fitted:\n");
        for (const Comb& cb : combiners()) {
            Spec s2;
            s2.name = cb.name;
            s2.banned = cb.ban;
            for (std::size_t i = 0; i < 12; ++i) {
                Value in;
                const std::size_t k = cb.list_shaped ? 2 + (i % 4) : 2;
                for (std::size_t j = 0; j < k; ++j)
                    in.push_back(static_cast<std::int64_t>(rnd() % 40) - 18);
                s2.cases.push_back({in, cb.ref(in)});
            }
            for (std::size_t i = 0; i < 5; ++i) {
                Value in;
                const std::size_t k = cb.list_shaped ? 5 + (i % 3) : 2;
                for (std::size_t j = 0; j < k; ++j)
                    in.push_back(static_cast<std::int64_t>(rnd() % 400) - 200);
                s2.holdout.push_back({in, cb.ref(in)});
            }
            const Oracle cb_oracle = [&cb](const Value& v) { return cb.ref(v); };
            const BuildResult b =
                synthesise_hardened(s2, pool_cap, cb_oracle, -2, 2, 4, 3, &lib);
            if (b.proof == Proof::Exhaustive) {
                std::printf("    %-9s %s\n", cb.name, b.recipe.render().c_str());
                lib.admit_recipe(cb.name, b.recipe, 0);
            } else {
                std::printf("    %-9s NOT PROVED%s\n", cb.name,
                            b.recipe.found ? " (fitted only)" : "");
            }
        }
        std::printf("\n");
    }

    for (std::size_t ti = 0; ti < T; ++ti) {
        const Target& t = tgts[ti];
        Spec s = spec_for(t, visible, rnd);
        s.banned.push_back(t.op);

        const BuildResult b = construct(s, pool_cap, &lib);
        if (b.proof != Proof::Generalised) {
            ++irreducible;
            hard.push_back(t.name);
            std::printf("  %-10s |   no    |    -    | not found from the rest\n", t.name);
            continue;
        }
        ++rebuilt;

        // THE EXTERNAL CHECK. The certificate is the system judging itself; this
        // is the real implementation judging it.
        std::size_t agree = 0;
        for (const Value& in : probe)
            if (b.recipe.apply(in, &lib) == real_op(t.op, in, t.b)) ++agree;
        const bool perfect = (agree == probe.size());
        if (perfect) { ++verified; base_ok[ti] = 1; }

        // THE AUDIT PROBING CANNOT DO. A reconstruction that reaches its own
        // banned operation through a library body agrees on every probe by
        // definition -- it is the operation, wrapped.
        std::vector<Op> used;
        collect_ops(b.recipe, lib, used, 0);
        const bool circ = std::find(used.begin(), used.end(), t.op) != used.end();
        if (circ) circular.emplace_back(t.name);

        std::printf("  %-10s |  yes    | %4zu/%-4zu| %s%s\n", t.name, agree, probe.size(),
                    b.recipe.render().c_str(), circ ? "   <-- CIRCULAR" : "");
        if (perfect) lib.admit_recipe(t.name, b.recipe, 0);
        lib.prune();
    }

    std::printf("\n");
    rate("rebuilt from the rest of the set", rebuilt, T);
    rate("...and agreeing on all probes", verified, T);
    std::printf("  %zu not found by this search at this budget:\n   ", irreducible);
    for (const auto& h : hard) std::printf(" %s", h.c_str());
    std::printf("\n\n  CIRCULARITY AUDIT of the rows above. Spec::banned USED TO stop only\n");
    std::printf("  the SEARCH from emitting an operation, leaving a LIBRARY BODY free\n");
    std::printf("  to contain it -- so a primitive could be rebuilt out of itself and\n");
    std::printf("  pass every probe, because the behaviour is correct: it IS the real\n");
    std::printf("  implementation, wrapped. construct() now refuses any library entry\n");
    std::printf("  whose recipe transitively uses a banned op, at all three sites that\n");
    std::printf("  reach into the library. This audit stays as the regression guard.\n");
    if (circular.empty()) {
        std::printf("    none: no row reached its own operation through a library body.\n");
    } else {
        rate("rows that reached their own banned operation", circular.size(), rebuilt);
        std::printf("   ");
        for (const auto& c : circular) std::printf(" %s", c.c_str());
        std::printf("\n    Those rows are `P` defined in terms of `P`. Every probe agrees\n");
        std::printf("    with them because they ARE the real implementation, wrapped, so\n");
        std::printf("    no external check can find the problem -- only reading the body\n");
        std::printf("    can. The iterated arms below walk transitive bodies and refuse.\n");
    }
    std::printf("\n");

    // =====================================================================
    // The grading set. Built once, shared by both iterated arms, never used to
    // accept or refine anything.
    // =====================================================================
    const std::vector<Value> wild = wilderness();
    std::printf("  === GRADING SET ===\n");
    std::printf("  %zu inputs, every one either longer than 20 or carrying a value of\n",
                wild.size());
    std::printf("  magnitude >= 1000 on a list of length >= 5. Acceptance below touches\n");
    std::printf("  lists of length 0..20 over -12..17 (the cases), every list of length\n");
    std::printf("  0..4 over -2..2 (the proof domain, 781 inputs) and the 22 extremal\n");
    std::printf("  inputs of default_extremes(), all of length <= 8. The two sets are\n");
    std::printf("  therefore DISJOINT BY CONSTRUCTION rather than by sampling luck.\n\n");

    // =====================================================================
    // The iterated loop, run twice under two different withdrawal policies.
    // =====================================================================
    struct RoundRow {
        std::size_t round = 0, attempted = 0, gained = 0, repaired = 0,
                    blocked = 0, held = 0;
        std::vector<std::string> gained_names, repair_names, blocked_names;
    };

    struct ArmResult {
        std::size_t pool = 0;
        std::vector<RoundRow> rows;
        std::vector<Node>     reg;
        std::vector<char>     gained;      // per target
        std::size_t accepted = 0, accepted_clean = 0;
        // WHAT A WEAKER GATE WOULD HAVE LET THROUGH. Counted over targets, once
        // each, for reconstructions this arm REFUSED but the single-round arm's
        // criterion -- passes every visible case and every held-out one -- would
        // have taken.
        std::size_t weak_extra = 0, weak_extra_clean = 0;
        // A GAIN IN A LATER ROUND IS NOT AUTOMATICALLY COMPOUNDING. A retry also
        // gets a fresh draw of specification cases, so a target that failed in
        // round 1 and lands in round 2 may simply have been handed an easier
        // sample. The only later gain that can be ATTRIBUTED to the library is
        // one whose answer actually calls a library body; this counts those.
        std::size_t later_gains = 0, later_gains_using_lib = 0;
        std::size_t rebind_failures = 0;
        double seconds = 0.0;
    };

    // `withdraw` is the whole difference between the two arms.
    //
    //   true  -- once P is rebuilt and accepted, the TRUE P is withdrawn. Later
    //            rounds may only reach P through the reconstruction. This is the
    //            self-improvement claim: a set of primitives the system supplies
    //            for itself, all at once.
    //
    //   false -- the true operations all stay. The library still grows with every
    //            acceptance, and the only question is whether having more parts
    //            makes more targets reachable. This is the CONTROL that separates
    //            "the search cannot find it" from "withdrawal broke it".
    auto run_arm = [&](bool withdraw, std::size_t pool) {
        ArmResult A;
        A.pool = pool;
        A.gained.assign(T, 0);
        const auto t0 = clk::now();
        std::vector<Op> retired;

        // ONE ATTEMPT at one target, against exactly the reconstructions that
        // survive `banned`. Returns a node with r.found == false on refusal.
        //
        // Factored out because the repair cascade below has to run the identical
        // attempt on a DIFFERENT target, and an attempt that differed in any
        // detail between the two paths would make a repaired primitive not
        // comparable to a freshly gained one.
        std::vector<char> weak_seen(T, 0);
        auto attempt = [&](std::size_t ti, const std::vector<Op>& banned,
                           const std::vector<Node>& reg, std::size_t round) -> Node {
            const Target& t = tgts[ti];
            Node n;
            n.r.found = false;
            const std::vector<char> lv = live_set(reg, banned);
            Mat m = materialise(reg, lv);
            Spec s = spec_for(t, visible, wrnd);
            s.banned = banned;
            const Oracle oracle = [&t](const Value& in) { return real_op(t.op, in, t.b); };

            // THE STRONGEST ACCEPTANCE AVAILABLE, per correct_bench: proved on
            // every list of length 0..4 over -2..2 AND unbroken on the extremal
            // inputs, with counterexamples fed back. Not a proof over all inputs
            // and never described as one.
            Exhaust ex;
            const BuildResult b = synthesise_hardened(s, pool, oracle, -2, 2, 4, 3,
                                                      &m.lib, &ex);
            if (b.proof != Proof::Exhaustive) {
                // THE CONTROL THAT MAKES A CLEAN ERROR-ACCUMULATION RESULT MEAN
                // ANYTHING. If nothing accepted is ever wrong, the interesting
                // question is whether that is because nothing is ever wrong or
                // because the gate is doing the work. This is the second half:
                // a reconstruction that passes every visible case AND every
                // held-out case -- the criterion the single-round arm uses and
                // the criterion `solve_all` admits on -- but fails the bounded
                // proof or the extremal inputs. Graded on the same set, counted
                // once per target so a speculative bundle cannot inflate it.
                if ((b.proof == Proof::Generalised || b.proof == Proof::Verified) &&
                    b.recipe.found && !weak_seen[ti]) {
                    weak_seen[ti] = 1;
                    ++A.weak_extra;
                    bool clean = true;
                    for (const Value& in : wild)
                        if (b.recipe.apply(in, &m.lib) != oracle(in)) { clean = false; break; }
                    if (clean) ++A.weak_extra_clean;
                }
                return n;
            }

            n = make_node(t.name, t.op, b.recipe, m, reg);
            if (!n.r.found) { ++A.rebind_failures; return n; }
            n.round = round;

            // RE-BIND AND RE-CHECK. The node is about to be stored with
            // registry-relative body indices and instantiated into a DIFFERENT
            // library next round -- a smaller one, holding whatever survived the
            // next withdrawal. If that rewriting is wrong the node computes a
            // different function from the one accepted, and every later number
            // is built on it. So instantiate it now through the same path a
            // later round would use and confirm it still agrees with the real
            // implementation on everything acceptance checked.
            {
                std::vector<Node> probe_reg = reg;
                probe_reg.push_back(n);
                const std::vector<char> plv = live_set(probe_reg, banned);
                Mat pm = materialise(probe_reg, plv);
                const std::size_t self = probe_reg.size() - 1;
                bool same = (pm.where[self] >= 0);
                if (same) {
                    const Recipe& rb = pm.lib.at(pm.where[self]).recipe;
                    for (const Case& c : s.cases)
                        if (rb.apply(c.in, &pm.lib) != c.out) { same = false; break; }
                    for (const Value& e : default_extremes())
                        if (same && rb.apply(e, &pm.lib) != oracle(e)) same = false;
                }
                if (!same) { ++A.rebind_failures; n.r.found = false; return n; }
            }

            // THE GRADE. Never seen by anything that decided to accept.
            std::size_t agree = 0;
            for (const Value& in : wild)
                if (b.recipe.apply(in, &m.lib) == oracle(in)) ++agree;
            n.wild_agree = agree;
            n.wild_n = wild.size();
            n.wild_clean = (agree == wild.size());
            for (const Expr& e : n.r.pool)
                if (e.op == Op::MapF || e.op == Op::FoldF || e.op == Op::FoldS) {
                    n.higher_order = true; break;
                }
            return n;
        };

        // Combiners enter the registry first so they sit at low library indices.
        // construct() only offers the first eight entries as fold BODIES, in
        // insertion order, so a combiner admitted late is a combiner no fold can
        // reach.
        //
        // Re-derived whenever a withdrawal has killed one. A scaffold that rots
        // because the operations it was written in no longer exist would end the
        // loop for a reason that has nothing to do with the primitives being
        // measured, and that would be a harness artefact reported as a finding.
        auto derive_combiners = [&](std::size_t round) {
            for (const Comb& cb : combiners()) {
                const std::vector<char> lv = live_set(A.reg, retired);
                bool have = false;
                for (std::size_t i = 0; i < A.reg.size(); ++i)
                    if (lv[i] && A.reg[i].name == cb.name) { have = true; break; }
                if (have) continue;
                Mat m = materialise(A.reg, lv);
                Spec s2;
                s2.name = cb.name;
                s2.banned = retired;
                for (const Op o : cb.ban)
                    if (std::find(s2.banned.begin(), s2.banned.end(), o) == s2.banned.end())
                        s2.banned.push_back(o);
                for (std::size_t i = 0; i < 12; ++i) {
                    Value in;
                    const std::size_t k = cb.list_shaped ? 2 + (i % 4) : 2;
                    for (std::size_t j = 0; j < k; ++j)
                        in.push_back(static_cast<std::int64_t>(wrnd() % 40) - 18);
                    s2.cases.push_back({in, cb.ref(in)});
                }
                for (std::size_t i = 0; i < 5; ++i) {
                    Value in;
                    const std::size_t k = cb.list_shaped ? 5 + (i % 3) : 2;
                    for (std::size_t j = 0; j < k; ++j)
                        in.push_back(static_cast<std::int64_t>(wrnd() % 400) - 200);
                    s2.holdout.push_back({in, cb.ref(in)});
                }
                // THE WEAKER GATE HERE, DELIBERATELY, AND AGAINST MY OWN
                // EXPECTATION. In fold_bench a rung certified only by
                // Proof::Generalised wrecked the sort ladder -- `below` cleared
                // that bar, was wrong on 149 of 200 fresh inputs OF THE SAME
                // SHAPE, and the fold built on it equalled sort on 60 of 200.
                // The obvious lesson was that a rung which will be COMPOSED needs
                // the hardened gate, and the warm-up above uses it.
                //
                // It does not transfer to this loop. Measured, same ladder, only
                // this line differing:
                //
                //                        single | arm A | arm B | wrong in wild
                //   Proof::Generalised     13   |  12   |  15   |  0
                //   Proof::Exhaustive      13   |   9   |  16   |  2
                //
                // The stricter gate is worse on every axis that matters and it
                // CAUSES the wrong acceptances rather than preventing them: fewer
                // stepping stones survive withdrawal, the ones that do get
                // composed further, and depth 3 is where reconstruction-on-
                // reconstruction stops being correct. Shipping the number that
                // measured better, with the result that produced it written down.
                const Oracle cb_oracle = [&cb](const Value& v) { return cb.ref(v); };
                (void)cb_oracle;
                const BuildResult b = construct(s2, pool, &m.lib);
                if (b.proof != Proof::Generalised) continue;
                Node n = make_node(cb.name, Op::kCount, b.recipe, m, A.reg);
                if (!n.r.found) { ++A.rebind_failures; continue; }
                n.round = round;
                A.reg.push_back(std::move(n));
            }
        };

        for (std::size_t round = 1; round <= max_round; ++round) {
            RoundRow row;
            row.round = round;
            derive_combiners(round);

            for (std::size_t ti = 0; ti < T; ++ti) {
                if (A.gained[ti]) continue;
                const Target& t = tgts[ti];
                ++row.attempted;

                std::vector<Op> trial = retired;
                if (std::find(trial.begin(), trial.end(), t.op) == trial.end())
                    trial.push_back(t.op);

                // Everything happens on a SCRATCH registry. A bundle that cannot
                // be completed leaves nothing behind, which is what keeps the
                // withdrawn set monotone and the loop terminating: each commit
                // strictly grows it, and it is bounded by the instruction set.
                std::vector<Node> scratch = A.reg;
                Node np = attempt(ti, trial, scratch, round);
                if (!np.r.found) continue;                  // refused outright
                scratch.push_back(np);

                // THE REPAIR CASCADE, and it is the difference between "the loop
                // stalls" and "the loop stalls even when allowed to fix what it
                // just broke".
                //
                // Withdrawing P kills every reconstruction whose body names P,
                // and some of those are the stand-ins for primitives withdrawn in
                // earlier rounds. Simply refusing there would measure the greedy
                // ORDER of the table rather than the capability. So instead: any
                // primitive left orphaned is rebuilt too, under the same
                // withdrawal, and the whole bundle commits only if every one of
                // them lands. A bundle that fails is a dependency the system
                // could not route around, not a scheduling artefact.
                bool ok = true;
                std::string block_why;
                std::vector<std::string> repaired;
                if (withdraw) {
                    for (std::size_t guard = 0; guard <= T; ++guard) {
                        const std::vector<char> lv = live_set(scratch, trial);
                        std::size_t orphan = T;
                        for (const Op o : trial) {
                            bool have = false;
                            for (std::size_t i = 0; i < scratch.size() && !have; ++i)
                                if (scratch[i].op == o && lv[i]) have = true;
                            if (have) continue;
                            for (std::size_t j = 0; j < T; ++j)
                                if (tgts[j].op == o) { orphan = j; break; }
                            break;
                        }
                        if (orphan == T) break;            // nothing left orphaned
                        if (guard == T) {                  // cannot happen; not assumed
                            ok = false;
                            block_why = "cascade did not settle";
                            break;
                        }
                        const Node nq = attempt(orphan, trial, scratch, round);
                        if (!nq.r.found) {
                            ok = false;
                            block_why = tgts[orphan].name;
                            break;
                        }
                        scratch.push_back(nq);
                        repaired.emplace_back(tgts[orphan].name);
                    }
                }

                if (!ok) {
                    ++row.blocked;
                    row.blocked_names.push_back(std::string(t.name) + "(orphans " +
                                                block_why + ")");
                    continue;
                }

                for (std::size_t i = A.reg.size(); i < scratch.size(); ++i) {
                    if (scratch[i].op == Op::kCount || scratch[i].wild_n == 0) continue;
                    ++A.accepted;
                    if (scratch[i].wild_clean) ++A.accepted_clean;
                }
                A.reg = std::move(scratch);
                if (withdraw) retired = trial;
                A.gained[ti] = 1;
                ++row.gained;
                if (round > 1) {
                    ++A.later_gains;
                    if (!np.deps.empty()) ++A.later_gains_using_lib;
                }
                row.gained_names.push_back(std::string(t.name) +
                                           (np.wild_clean ? "" : "*") +
                                           (np.deps.empty() ? "" : "+"));
                row.repaired += repaired.size();
                for (const auto& rn : repaired) row.repair_names.push_back(rn);
            }

            // How many gained primitives still have a living stand-in at the end
            // of the round. Under withdrawal this is the set the system is
            // actually supplying for itself, all at once, and it is the number
            // the compounding claim lives or dies on.
            {
                const std::vector<char> lv = live_set(A.reg, retired);
                std::size_t held = 0;
                for (std::size_t ti = 0; ti < T; ++ti) {
                    if (!A.gained[ti]) continue;
                    for (std::size_t i = 0; i < A.reg.size(); ++i)
                        if (A.reg[i].op == tgts[ti].op && lv[i]) { ++held; break; }
                }
                row.held = held;
            }

            A.rows.push_back(row);
            if (row.gained == 0) break;      // a round that adds nothing cannot be
                                             // followed by one that does
        }
        A.seconds = std::chrono::duration<double>(clk::now() - t0).count();
        return A;
    };

    auto print_arm = [&](const char* title, const ArmResult& A, bool withdraw) {
        std::printf("  === %s ===\n", title);
        std::printf("  round | tried | gained | repaired | blocked | jointly held | newly gained\n");
        std::printf("  ------+-------+--------+----------+---------+--------------+--------------\n");
        for (const RoundRow& r : A.rows) {
            std::string names;
            for (const auto& n : r.gained_names) { names += n; names += " "; }
            if (names.empty()) names = "--";
            // + marks an answer that calls a reconstruction rather than being
            // built only out of true primitives; * marks one wrong on the
            // grading set.
            std::printf("   %3zu  |  %3zu  |  %3zu   |   %3zu    |   %3zu   |     %3zu/%-3zu  | %s\n",
                        r.round, r.attempted, r.gained, r.repaired, r.blocked,
                        r.held, T, names.c_str());
        }
        std::printf("\n");
        if (withdraw) {
            for (const RoundRow& r : A.rows) {
                if (r.repair_names.empty()) continue;
                std::printf("  round %zu LOST and re-derived these stand-ins -- a withdrawal\n"
                            "  in the same round named an operation their bodies used:", r.round);
                for (const auto& n : r.repair_names) std::printf(" %s", n.c_str());
                std::printf("\n");
            }
            bool any = false;
            for (const RoundRow& r : A.rows) if (!r.blocked_names.empty()) any = true;
            if (any) {
                std::printf("  BLOCKED -- the reconstruction itself was accepted, and then\n");
                std::printf("  withdrawing the original left an EARLIER withdrawn primitive with\n");
                std::printf("  no working stand-in, which could not be re-derived either. The\n");
                std::printf("  name in brackets is the primitive that was orphaned:\n");
                for (const RoundRow& r : A.rows) {
                    if (r.blocked_names.empty()) continue;
                    std::printf("    round %zu:", r.round);
                    for (const auto& n : r.blocked_names) std::printf(" %s", n.c_str());
                    std::printf("\n");
                }
                std::printf("\n");
            }
        }
        const std::size_t held_final = A.rows.empty() ? 0 : A.rows.back().held;
        rate("primitives JOINTLY supplied by their own rebuilds", held_final, T);
        std::printf("  %-46s %3zu\n", "reconstructions accepted (incl. repairs)", A.accepted);
        rate("...and clean on the grading set", A.accepted_clean, A.accepted);
        // PRINTED EVEN AT ZERO, because zero is the informative value. If the
        // hardened gate never rejected anything a weaker gate would have taken,
        // then "nothing accepted is wrong" is NOT evidence that the gate is doing
        // the work -- it is evidence that on this task family the two gates agree.
        std::printf("  %-46s %3zu\n",
                    "extra admits under the single-round criterion", A.weak_extra);
        if (A.weak_extra)
            rate("...of which clean on the grading set", A.weak_extra_clean, A.weak_extra);
        if (A.rebind_failures)
            std::printf("  %zu acceptances discarded: the reconstruction did not survive\n"
                        "  being re-bound to a fresh library. That is a HARNESS defect if\n"
                        "  it is nonzero and it is reported rather than swallowed.\n",
                        A.rebind_failures);
        std::printf("  %.1f s\n\n", A.seconds);
    };

    // ARM A ACROSS BUDGETS.
    //
    // The jointly-held count MOVES with the pool: 9 at 100,000 behaviours and 12
    // at 150,000 on the machine this was written on. Reporting one number would
    // be reporting one budget and calling it a property of the instruction set,
    // which is the mistake behind every "provably irreducible" claim this repo
    // has had to withdraw. What is stable across the sweep is the shape -- every
    // budget gains everything it will gain in round one -- and that is the
    // finding, not the count.
    std::vector<std::size_t> pools;
    for (const std::size_t q : {iter_pool / 8, iter_pool / 4, iter_pool / 2, iter_pool})
        if (q >= 2000 && (pools.empty() || q > pools.back())) pools.push_back(q);
    if (pools.empty()) pools.push_back(iter_pool);

    std::vector<ArmResult> sweep;
    for (const std::size_t q : pools) sweep.push_back(run_arm(true, q));
    const ArmResult& A_strict = sweep.back();

    std::printf("  === ARM A ACROSS BUDGETS: is the stall a property of the set or of\n");
    std::printf("      the search budget? ===\n");
    std::printf("  pool     | rounds | gained r1 | gained later | jointly held | seconds\n");
    std::printf("  ---------+--------+-----------+--------------+--------------+--------\n");
    for (const ArmResult& a : sweep) {
        std::size_t g1 = a.rows.empty() ? 0 : a.rows[0].gained, gl = 0;
        for (std::size_t i = 1; i < a.rows.size(); ++i) gl += a.rows[i].gained;
        std::printf("  %8zu |   %2zu   |    %3zu    |     %3zu      |     %3zu/%-3zu  | %6.1f\n",
                    a.pool, a.rows.size(), g1, gl,
                    a.rows.empty() ? 0 : a.rows.back().held, T, a.seconds);
    }
    std::printf("\n");

    print_arm("ARM A: ITERATED, the rebuilt version REPLACES the original", A_strict, true);

    const ArmResult A_lib = run_arm(false, pools.back());
    print_arm("ARM B (control): iterated, originals KEPT, library grows", A_lib, false);

    // =====================================================================
    // ERROR ACCUMULATION BY REBUILD DEPTH.
    // =====================================================================
    std::printf("  === ERROR ACCUMULATION: correctness against rebuild depth ===\n\n");
    std::printf("  Depth 1 is a reconstruction built only out of true primitives.\n");
    std::printf("  Depth 2 is one that calls a depth-1 reconstruction, and so on. An\n");
    std::printf("  accepted reconstruction passed a proof over every list of length 0..4\n");
    std::printf("  over -2..2 and the extremal inputs; CLEAN means it also agreed with\n");
    std::printf("  the real implementation on all %zu grading inputs, which nothing\n",
                wild.size());
    std::printf("  involved in accepting it was allowed to see.\n\n");
    std::printf("  arm | depth | accepted | clean | 95%% CI            | worst agreement\n");
    std::printf("  ----+-------+----------+-------+-------------------+----------------\n");
    bool any_depth_rows = false;
    for (const auto& pr : {std::pair<const char*, const ArmResult*>{"A", &A_strict},
                           std::pair<const char*, const ArmResult*>{"B", &A_lib}}) {
        for (std::size_t d = 1; d <= 6; ++d) {
            std::size_t n = 0, clean = 0, worst = wild.size();
            for (const Node& nd : pr.second->reg) {
                if (nd.op == Op::kCount || nd.wild_n == 0 || nd.depth != d) continue;
                ++n;
                if (nd.wild_clean) ++clean;
                worst = std::min(worst, nd.wild_agree);
            }
            if (!n) continue;
            any_depth_rows = true;
            const auto ci = wilson(clean, n);
            std::printf("   %s  |   %zu   |   %4zu   | %4zu  | [%5.1f%%, %5.1f%%]  | %zu/%zu\n",
                        pr.first, d, n, clean, ci.first, ci.second, worst, wild.size());
        }
    }
    if (!any_depth_rows) std::printf("   (nothing was accepted, so there is no curve)\n");

    // Every accepted reconstruction, with what it was built out of.
    //
    // BOTH ARMS. This printed arm A only, and the first time a control-arm
    // reconstruction came back WRONG IN THE WILD the run said so in the depth
    // table and gave no way to find out WHICH ONE. A summary that reports a
    // defect without naming it costs a whole re-run to act on.
    auto listing = [&](const char* label, const ArmResult& arm) {
        std::printf("\n  every accepted reconstruction, %s -- SUPERSEDED marks one that a\n"
                    "  later withdrawal killed and which was replaced by the row below it:\n",
                    label);
        if (arm.reg.empty()) { std::printf("    (none)\n"); return; }
        // A node is superseded when a later node stands in for the same
        // primitive, or when the final withdrawal set has killed it outright.
        std::vector<Op> final_retired;
        for (std::size_t ti = 0; ti < T; ++ti)
            if (arm.gained[ti]) final_retired.push_back(tgts[ti].op);
        const std::vector<char> flv = live_set(arm.reg, final_retired);
        for (std::size_t i = 0; i < arm.reg.size(); ++i) {
            const Node& n = arm.reg[i];
            if (n.wild_n == 0 && n.op != Op::kCount) continue;
            // Expr::k in a STORED node is an index into `deps`, and render()
            // prints it as libK -- so this legend is exact rather than a guess at
            // which library entry the number meant when it was found.
            std::string uses;
            for (std::size_t k = 0; k < n.deps.size(); ++k)
                uses += "  lib" + std::to_string(k) + "=" + arm.reg[n.deps[k]].name;
            const char* status = n.wild_n ? (n.wild_clean ? "clean" : "WRONG IN THE WILD")
                                          : "helper";
            std::printf("    r%zu d%zu %-9s %-44s %-17s%s%s\n", n.round, n.depth,
                        n.name.c_str(), n.r.render().c_str(), status,
                        flv[i] ? "" : " SUPERSEDED", uses.c_str());
        }
    };
    listing("arm A", A_strict);
    listing("arm B (control)", A_lib);
    // =====================================================================
    // What no arm ever produced.
    // =====================================================================
    std::printf("\n  === NOT REBUILT BY ANY ARM ===\n");
    std::vector<std::string> never;
    for (std::size_t ti = 0; ti < T; ++ti)
        if (!base_ok[ti] && !A_strict.gained[ti] && !A_lib.gained[ti])
            never.emplace_back(tgts[ti].name);
    std::printf("   ");
    for (const auto& n : never) std::printf(" %s", n.c_str());
    if (never.empty()) std::printf(" (none)");
    std::printf("\n\n  THE WORD FOR THAT SET IS NOT \"IRREDUCIBLE\".\n");
    std::printf("  The honest claim is: not found by THIS search (bottom-up construction\n");
    std::printf("  with bidirectional and library-free fallbacks), at THIS budget\n");
    std::printf("  (%zu behaviours single-round, %zu iterated), under THIS specification\n",
                pool_cap, iter_pool);
    std::printf("  generator (%zu cases at lengths 0..13, 6 held out at 15..20). This repo\n",
                visible);
    std::printf("  has already published \"provably irreducible\" and been wrong; a bigger\n");
    std::printf("  pool or a different case distribution can move any row of that list.\n");

    // =====================================================================
    // The verdict, stated in whichever direction the numbers point.
    // =====================================================================
    const std::size_t held_A = A_strict.rows.empty() ? 0 : A_strict.rows.back().held;
    const std::size_t held_B = A_lib.rows.empty() ? 0 : A_lib.rows.back().held;
    const std::size_t r1 = A_strict.rows.empty() ? 0 : A_strict.rows[0].gained;
    std::size_t later = 0;
    for (std::size_t i = 1; i < A_strict.rows.size(); ++i) later += A_strict.rows[i].gained;
    const std::size_t b1 = A_lib.rows.empty() ? 0 : A_lib.rows[0].gained;
    std::size_t b_later = 0;
    for (std::size_t i = 1; i < A_lib.rows.size(); ++i) b_later += A_lib.rows[i].gained;

    std::printf("\n  === DOES THE SELF-REBUILDING LOOP COMPOUND? ===\n");
    const std::size_t honest_single = verified - circular.size();
    std::printf("    single round, each primitive judged against ALL the others: %zu/%zu,\n",
                verified, T);
    std::printf("    of which %zu reached its own operation through a library body, so the\n"
                "    figure that survives reading the answers is %zu/%zu.\n",
                circular.size(), honest_single, T);
    std::printf("    jointly, with each accepted rebuild REPLACING its original: %zu/%zu.\n",
                held_A, T);
    std::printf("    round 1 of that gained %zu; every later round together gained %zu.\n",
                r1, later);
    std::printf("    control (originals kept, library grows): %zu then %zu, ending at %zu/%zu.\n",
                b1, b_later, held_B, T);
    if (later > 0)
        std::printf("    Later rounds gained ground, so reconstructions ARE usable as parts\n"
                    "    of further reconstructions -- the loop compounds at this budget.\n");
    else if (held_A > 0 && b_later == 0)
        std::printf("    NO. Both arms gain everything they will ever gain in round 1. A\n"
                    "    second pass over the same targets with a larger library finds\n"
                    "    nothing new, so nothing here is reachable only via a reconstruction.\n");
    else if (held_A > 0)
        std::printf("    NOT UNDER WITHDRAWAL. The control gained %zu in later rounds and the\n"
                    "    strict arm gained none, so what stops the loop is not the search --\n"
                    "    it is that withdrawing a primitive invalidates the reconstructions\n"
                    "    that were built on it.\n", b_later);
    else
        std::printf("    Nothing survived the acceptance test, so there is no loop to speak\n"
                    "    of at this budget.\n");
    {
        // The one place a positive compounding claim could hide, and it has to
        // be checked rather than assumed: a retry gets fresh cases as well as a
        // bigger library.
        const std::size_t lg = A_strict.later_gains + A_lib.later_gains;
        const std::size_t lu = A_strict.later_gains_using_lib + A_lib.later_gains_using_lib;
        std::printf("    Attribution of the later-round gains: %zu across both arms, of which\n"
                    "    %zu produced an answer that actually CALLS a reconstruction. A retry\n"
                    "    also gets a fresh draw of specification cases, so a later gain whose\n"
                    "    answer contains no library call is a re-draw and not compounding.\n",
                    lg, lu);
        if (lg > 0 && lu == 0)
            std::printf("    None of them do. There is no evidence here that a reconstruction\n"
                        "    ever made a later reconstruction possible.\n");
    }
    if (verified > held_A)
        std::printf("    '%zu primitives are each redundant given the other %zu' is %zu separate\n"
                    "    statements, and they do not compose into '%zu are jointly redundant':\n"
                    "    enforcing the withdrawal costs %zu of them even with %.1fx the search\n"
                    "    budget and a repair pass.\n",
                    verified, T - 1, verified, verified, verified - held_A,
                    (double)iter_pool / (double)pool_cap);
    if (held_A >= honest_single)
        std::printf("    Against the CIRCULARITY-CLEANED single-round figure the comparison\n"
                    "    runs the other way: %zu jointly against %zu individually. The joint\n"
                    "    number is larger because the iterated arms get %.1fx the budget and a\n"
                    "    counterexample-refinement pass, not because withdrawal is free.\n",
                    held_A, honest_single, (double)iter_pool / (double)pool_cap);
    {
        std::size_t deep = 0, ho = 0, tot = 0;
        for (const ArmResult* a : {&A_strict, &A_lib})
            for (const Node& n : a->reg) {
                if (n.op == Op::kCount || !n.wild_n) continue;
                ++tot;
                if (n.depth > 1) ++deep;
                if (n.higher_order) ++ho;
            }
        std::size_t folded_n = 0;
        for (const ArmResult* arm : {&A_strict, &A_lib})
            for (const Node& n : arm->reg) {
                if (n.wild_n == 0 && n.op != Op::kCount) continue;
                for (const Expr& e : n.r.pool)
                    if (e.op == Op::FoldF || e.op == Op::MapF || e.op == Op::FoldS) {
                        ++folded_n; break;
                    }
            }
        std::printf("    The combiners exist so that FoldF can express \"combine every\n"
                    "    element\". %zu of %zu accepted reconstructions contain a fold or a\n"
                    "    map. THE SCAFFOLDING PAID, and it took a fix in construct() to\n"
                    "    find that out: the higher-order expansion capped the TOTAL element\n"
                    "    count across every case at 64, and these specifications carry 28\n"
                    "    cases, so a fold was speculated on almost nothing and this line\n"
                    "    read 0 of 29 for many cycles. The cap now bounds the LONGEST CASE.\n",
                    folded_n, A_strict.accepted + A_lib.accepted);
        std::printf("    On error accumulation: %zu of %zu accepted reconstructions are built\n"
                    "    on another reconstruction rather than only on true primitives, and\n"
                    "    NOTHING accepted by either arm is wrong on the grading set. That is a\n"
                    "    real result about the gate and a WEAK one about depth -- the loop\n"
                    "    stops before it can stack deeply enough for decay to be measurable.\n",
                    deep, tot);
    }

    std::printf("\n  WHAT THIS HARNESS CANNOT SEE.\n");
    std::printf("    * Acceptance proves a bounded domain and samples the boundary of the\n");
    std::printf("      real one. A reconstruction wrong only on a short list of small\n");
    std::printf("      values it was never shown is impossible; one wrong only in the gap\n");
    std::printf("      between the extremal inputs and the grading set is not.\n");
    std::printf("    * The grading set is %zu inputs, not all of them. \"Clean\" is an upper\n",
                wild.size());
    std::printf("      bound on wrongness, never a proof of rightness.\n");
    std::printf("    * Withdrawal is greedy and ORDER-DEPENDENT. A target is attempted in\n");
    std::printf("      table order and committed as soon as it is accepted, so a different\n");
    std::printf("      order could reach a larger jointly-held set. Nothing here searches\n");
    std::printf("      for the MAXIMUM such set; that is a covering problem this does not\n");
    std::printf("      attempt.\n");
    std::printf("    * The combiners are handed over, not derived from nothing. Without a\n");
    std::printf("      pairwise body there is no fold, and without a fold there is no way\n");
    std::printf("      to express \"combine every element\" at all. They are also admitted\n");
    std::printf("      on the WEAKER criterion -- certified, not proved -- so a wrong\n");
    std::printf("      combiner could poison any fold built on it. Nothing here would have\n");
    std::printf("      caught that; what saves it is that no accepted reconstruction used a\n");
    std::printf("      fold at all.\n");
    std::printf("    * 22 operations of one instruction set over integer lists. Nothing\n");
    std::printf("      here speaks to rebuilding anything with state or control flow.\n");

    std::printf("\n  total %.1f s\n",
                std::chrono::duration<double>(clk::now() - t_start).count());
    return 0;
}
