// Bidirectional synthesis — meeting in the middle.
//
// The forward search is the one this project already had: from the input,
// compose operations, keep one representative per distinct behaviour. Its cost
// is exponential in depth and no pool budget changes that.
//
// The backward search is new. It starts at the TARGET and asks, for each
// operation, what an operand would have had to be for that operation to have
// produced the target. Many operations answer that exactly:
//
//     G = rev(A)          =>  A = rev(G)
//     G = add(A, F)       =>  A = sub(G, F)      for any known F
//     G = sub(A, F)       =>  A = add(G, F)
//     G = mapadd(A, k)    =>  A = mapadd(G, -k)
//     G = mapmul(A, k)    =>  A = G / k          where k divides every element
//     G = tail(A)         =>  A = anything with G as its tail  (not unique)
//     G = sort(A)         =>  A is any permutation of G         (not unique)
//
// The first five are unique preimages. The last two are not, and are excluded --
// a non-unique inverse generates goals that are usually wrong and floods the
// pool, which is the failure mode that makes naive backward search useless.
//
// A GOAL therefore carries two things: the behaviour something must produce, and
// the WRAPPER that turns such a thing into the target. When a forward behaviour
// matches a goal's behaviour, the answer is the wrapper applied to the forward
// expression, and wrappers chain, so a goal at backward depth 2 carries two.
//
// EVERYTHING ASSEMBLED IS RE-CHECKED. Inverses are computed on the sampled
// behaviour vectors, and a behaviour vector is a finite observation. Two
// expressions agreeing on every case can still differ elsewhere, and an inverse
// can be valid on the cases and invalid in general. Re-running the assembled
// candidate against every case before returning it means an unsound step costs
// time and can never cost correctness.

#include "khora/techne/techne.hpp"

#include <algorithm>
#include <functional>
#include <numeric>
#include <unordered_map>
#include <vector>

namespace khora::techne {
namespace {

constexpr std::int64_t kCap = 1000000000;
inline std::int64_t cap(std::int64_t x) noexcept {
    return x < -kCap ? -kCap : (x > kCap ? kCap : x);
}

std::int64_t const_at(std::uint8_t b) {
    static const std::int64_t k[16] = {0, 1, 2, 3, 4, 5, 6, 7,
                                       8, 9, 10, -1, -2, 100, 1000, 2};
    return k[b % 16];
}

// A behaviour fingerprint over every case. Two candidates with the same
// fingerprint are indistinguishable to the specification.
struct Sig {
    std::uint64_t a = 0xcbf29ce484222325ULL, b = 0x9e3779b97f4a7c15ULL;
    bool operator==(const Sig& o) const noexcept { return a == o.a && b == o.b; }
    void feed(std::uint64_t x) noexcept {
        a = (a ^ x) * 0x100000001b3ULL;
        b = b + x + 0x9e3779b97f4a7c15ULL;
        b = (b ^ (b >> 29)) * 0xbf58476d1ce4e5b9ULL;
    }
};
struct SigHash {
    std::size_t operator()(const Sig& s) const noexcept {
        return static_cast<std::size_t>(s.a ^ (s.b << 1));
    }
};

Sig sign(const std::vector<Value>& outs) {
    Sig s;
    for (const Value& v : outs) {
        s.feed(0xF17E5ULL);
        for (const auto x : v) s.feed(static_cast<std::uint64_t>(x));
        s.feed(static_cast<std::uint64_t>(v.size()));
    }
    return s;
}

// The forward half. Deliberately a small, self-contained re-implementation
// rather than a call into construct(): this search needs the behaviour VECTORS
// kept for inversion, and construct keeps only fingerprints.
Value fwd_op(Op op, const Value& A, const Value& B, std::uint8_t k,
             const Library* lib, std::size_t depth) {
    Value out;
    auto zip = [&](auto f) {
        if (A.empty() || B.empty()) return;
        out.reserve(A.size());
        for (std::size_t i = 0; i < A.size(); ++i) out.push_back(cap(f(A[i], B[i % B.size()])));
    };
    switch (op) {
        case Op::Const: out = Value{const_at(k)}; break;
        case Op::Add: zip([](auto x, auto y) { return x + y; }); break;
        case Op::Sub: zip([](auto x, auto y) { return x - y; }); break;
        case Op::Mul: zip([](auto x, auto y) { return x * y; }); break;
        case Op::Div: zip([](auto x, auto y) { return y == 0 ? std::int64_t{0} : x / y; }); break;
        case Op::Mod: zip([](auto x, auto y) { return y == 0 ? std::int64_t{0} : x % y; }); break;
        case Op::Len: out = Value{static_cast<std::int64_t>(A.size())}; break;
        case Op::Head: if (!A.empty()) out = Value{A.front()}; break;
        case Op::Tail: if (A.size() > 1) out.assign(A.begin() + 1, A.end()); break;
        case Op::Rev: out.assign(A.rbegin(), A.rend()); break;
        case Op::Sort: out = A; std::sort(out.begin(), out.end()); break;
        case Op::Append: out = A; out.insert(out.end(), B.begin(), B.end()); break;
        case Op::Take: {
            if (B.empty()) break;
            const std::size_t n = static_cast<std::size_t>(std::max<std::int64_t>(0, B[0]));
            out.assign(A.begin(), A.begin() + static_cast<std::ptrdiff_t>(std::min(A.size(), n)));
            break;
        }
        case Op::Drop: {
            if (B.empty()) break;
            const std::size_t n = std::min(A.size(),
                static_cast<std::size_t>(std::max<std::int64_t>(0, B[0])));
            out.assign(A.begin() + static_cast<std::ptrdiff_t>(n), A.end());
            break;
        }
        case Op::Index: {
            if (B.empty() || B[0] < 0) break;
            const std::size_t i = static_cast<std::size_t>(B[0]);
            if (i < A.size()) out = Value{A[i]};
            break;
        }
        case Op::Range: {
            if (A.empty()) break;
            const std::int64_t n = std::min<std::int64_t>(std::max<std::int64_t>(0, A[0]),
                                                          static_cast<std::int64_t>(kMaxListLen));
            out.resize(static_cast<std::size_t>(n));
            std::iota(out.begin(), out.end(), std::int64_t{0});
            break;
        }
        case Op::Sum: { std::int64_t t = 0; for (const auto x : A) t = cap(t + x); out = Value{t}; break; }
        case Op::Max: if (!A.empty()) out = Value{*std::max_element(A.begin(), A.end())}; break;
        case Op::Min: if (!A.empty()) out = Value{*std::min_element(A.begin(), A.end())}; break;
        case Op::Filter: { if (B.empty()) break; for (const auto x : A) if (x > B[0]) out.push_back(x); break; }
        case Op::MapAdd: { if (B.empty()) break; for (const auto x : A) out.push_back(cap(x + B[0])); break; }
        case Op::MapMul: { if (B.empty()) break; for (const auto x : A) out.push_back(cap(x * B[0])); break; }
        case Op::Count: { if (B.empty()) break; std::int64_t n = 0; for (const auto x : A) if (x == B[0]) ++n; out = Value{n}; break; }
        case Op::Guard: if (!B.empty()) out = A; break;
        case Op::Else: out = A.empty() ? B : A; break;
        case Op::MapF: {
            if (lib == nullptr || lib->size() == 0) break;
            const std::size_t li = k % lib->size();
            for (const auto x : A) {
                const Value r1 = lib->call(li, Value{x}, depth + 1);
                out.insert(out.end(), r1.begin(), r1.end());
                if (out.size() > kMaxListLen) break;
            }
            break;
        }
        case Op::FoldF: {
            if (lib == nullptr || lib->size() == 0 || A.empty()) break;
            const std::size_t li = k % lib->size();
            Value acc{A[0]};
            for (std::size_t i = 1; i < A.size(); ++i) {
                // The body receives the running value and the next element as a
                // two-element list, which is how a one-argument machine expresses
                // a binary operation without a second input channel.
                Value pair = acc;
                pair.push_back(A[i]);
                acc = lib->call(li, pair, depth + 1);
                if (acc.empty()) break;
            }
            out = acc;
            break;
        }
        case Op::Call: if (lib && lib->size()) out = lib->call(k % lib->size(), A, depth + 1); break;
        default: out = A; break;
    }
    if (out.size() > kMaxListLen) out.resize(kMaxListLen);
    return out;
}

const std::vector<Op>& fwd_unary() {
    static const std::vector<Op> v{Op::Len, Op::Head, Op::Tail, Op::Rev, Op::Sort,
                                   Op::Range, Op::Sum, Op::Max, Op::Min};
    return v;
}
const std::vector<Op>& fwd_binary() {
    static const std::vector<Op> v{Op::Add, Op::Sub, Op::Mul, Op::Div, Op::Mod,
                                   Op::Append, Op::Take, Op::Drop, Op::Index,
                                   Op::Filter, Op::MapAdd, Op::MapMul, Op::Count,
                                   Op::Guard, Op::Else};
    return v;
}

// ---------------------------------------------------------------------------
// Inverses. Each returns false when the preimage is not unique or not defined
// on this value, which is the honest answer far more often than not.
// ---------------------------------------------------------------------------

bool inv_rev(const Value& g, Value& pre) {
    pre.assign(g.rbegin(), g.rend());
    return true;                                  // rev is its own inverse
}

bool inv_mapadd(const Value& g, std::int64_t k, Value& pre) {
    // Exact unless the forward direction saturated at the cap, which would have
    // destroyed information. Refusing at the boundary keeps the inverse sound.
    pre.clear();
    pre.reserve(g.size());
    for (const auto x : g) {
        if (x >= kCap || x <= -kCap) return false;
        pre.push_back(x - k);
    }
    return true;
}

bool inv_mapmul(const Value& g, std::int64_t k, Value& pre) {
    if (k == 0) return false;                     // every preimage maps to zero
    pre.clear();
    pre.reserve(g.size());
    for (const auto x : g) {
        if (x >= kCap || x <= -kCap) return false;
        if (x % k != 0) return false;             // no integer preimage
        pre.push_back(x / k);
    }
    return true;
}

// G = add(A, F) => A = sub(G, F), elementwise with F cycling, and only when the
// shapes are consistent with the forward rule -- add returns a list the length
// of its FIRST operand, so |G| must equal |A|, and F must be non-empty.
bool inv_add_left(const Value& g, const Value& f, Value& pre) {
    if (g.empty() || f.empty()) return false;
    pre.clear();
    pre.reserve(g.size());
    for (std::size_t i = 0; i < g.size(); ++i) {
        const std::int64_t x = g[i];
        if (x >= kCap || x <= -kCap) return false;
        pre.push_back(x - f[i % f.size()]);
    }
    return true;
}

bool inv_sub_left(const Value& g, const Value& f, Value& pre) {
    if (g.empty() || f.empty()) return false;
    pre.clear();
    pre.reserve(g.size());
    for (std::size_t i = 0; i < g.size(); ++i) {
        const std::int64_t x = g[i];
        if (x >= kCap || x <= -kCap) return false;
        pre.push_back(x + f[i % f.size()]);
    }
    return true;
}

// A goal: a behaviour something must produce, plus how to turn such a thing into
// the target. `op` is the wrapper, `operand` indexes the FORWARD pool for binary
// wrappers (-1 when unary or constant-valued), `k` carries the constant.
struct Goal {
    std::vector<Value> want;      // per case
    Sig sig;
    Op  op = Op::Mov;             // wrapper to apply to a matching expression
    int operand = -1;             // forward pool index, or -1
    std::uint8_t k = 0;
    int parent = -1;              // previous goal in the chain, -1 = the target
};

} // namespace

BuildResult construct_bidir(const Spec& spec, std::size_t max_pool, const Library* lib) {
    BuildResult r;
    r.cases_total = spec.cases.size();
    r.holdout_total = spec.holdout.size();
    if (spec.cases.empty()) return r;

    const std::size_t ncase = spec.cases.size();

    // ---- forward pool -------------------------------------------------------
    std::vector<Expr> expr;
    std::vector<std::vector<Value>> beh;
    std::unordered_map<Sig, std::size_t, SigHash> fwd_index;

    std::vector<Value> target;
    target.reserve(ncase);
    for (const Case& c : spec.cases) target.push_back(c.out);
    const Sig want = sign(target);

    int found_fwd = -1;                 // forward pool entry equal to the target
    int found_goal = -1;                // goal it satisfied, -1 if direct hit

    auto add_fwd = [&](const Expr& e, std::vector<Value> outs) -> int {
        ++r.nodes_considered;
        const Sig s = sign(outs);
        auto it = fwd_index.find(s);
        if (it != fwd_index.end()) return static_cast<int>(it->second);
        expr.push_back(e);
        beh.push_back(std::move(outs));
        fwd_index.emplace(s, expr.size() - 1);
        if (s == want && found_fwd < 0) found_fwd = static_cast<int>(expr.size()) - 1;
        return static_cast<int>(expr.size()) - 1;
    };

    {
        std::vector<Value> ident;
        ident.reserve(ncase);
        for (const Case& c : spec.cases) ident.push_back(c.in);
        Expr e; e.op = Op::Mov; e.a = -1;
        add_fwd(e, std::move(ident));
    }
    for (std::uint8_t k = 0; k < 16; ++k) {
        std::vector<Value> outs(ncase, Value{const_at(k)});
        Expr e; e.op = Op::Const; e.k = k;
        add_fwd(e, std::move(outs));
    }
    if (lib != nullptr) {
        for (std::size_t li = 0; li < lib->size(); ++li) {
            std::vector<Value> outs(ncase);
            for (std::size_t c = 0; c < ncase; ++c) {
                outs[c] = fwd_op(Op::Call, spec.cases[c].in, {},
                                 static_cast<std::uint8_t>(li), lib, 0);
            }
            Expr e; e.op = Op::Call; e.a = -1; e.k = static_cast<std::uint8_t>(li);
            add_fwd(e, std::move(outs));
        }
    }

    // ---- goal pool ----------------------------------------------------------
    std::vector<Goal> goals;
    std::unordered_map<Sig, std::size_t, SigHash> goal_index;

    auto add_goal = [&](Goal g) {
        g.sig = sign(g.want);
        if (!goal_index.emplace(g.sig, goals.size()).second) return;
        goals.push_back(std::move(g));
    };

    {
        Goal g;
        g.want = target;
        g.op = Op::Mov;
        g.parent = -1;
        add_goal(std::move(g));
    }

    // Meet-in-the-middle: alternate one level of each, checking for an
    // intersection after every expansion. The check is a hash lookup, so it
    // costs nothing relative to the expansion that produced it.
    auto meet = [&]() -> bool {
        if (found_fwd >= 0) return true;
        for (std::size_t gi = 0; gi < goals.size(); ++gi) {
            auto it = fwd_index.find(goals[gi].sig);
            if (it == fwd_index.end()) continue;
            if (goals[gi].parent == -1 && goals[gi].op == Op::Mov) {
                found_fwd = static_cast<int>(it->second);
                found_goal = -1;
                return true;
            }
            found_fwd = static_cast<int>(it->second);
            found_goal = static_cast<int>(gi);
            return true;
        }
        return false;
    };

    std::size_t fwd_frontier = 0, goal_frontier = 0;
    const std::size_t goal_cap = std::max<std::size_t>(64, max_pool / 4);

    for (std::size_t level = 0; level < 6 && !meet(); ++level) {
        // ---- one forward level ---------------------------------------------
        {
            const std::size_t lo = fwd_frontier, hi = expr.size();
            fwd_frontier = hi;
            for (std::size_t i = lo; i < hi && expr.size() < max_pool && found_fwd < 0; ++i) {
                for (const Op op : fwd_unary()) {
                    if (!spec.allows(op)) continue;
                    std::vector<Value> outs(ncase);
                    for (std::size_t c = 0; c < ncase; ++c) {
                        outs[c] = fwd_op(op, beh[i][c], {}, 0, lib, 0);
                    }
                    Expr e; e.op = op; e.a = static_cast<int>(i);
                    add_fwd(e, std::move(outs));
                }
            }
            for (std::size_t i = 0; i < hi && expr.size() < max_pool && found_fwd < 0; ++i) {
                for (std::size_t j = 0; j < hi && expr.size() < max_pool && found_fwd < 0; ++j) {
                    if (i < lo && j < lo) continue;
                    for (const Op op : fwd_binary()) {
                        if (!spec.allows(op)) continue;
                        std::vector<Value> outs(ncase);
                        for (std::size_t c = 0; c < ncase; ++c) {
                            outs[c] = fwd_op(op, beh[i][c], beh[j][c], 0, lib, 0);
                        }
                        Expr e; e.op = op; e.a = static_cast<int>(i); e.b = static_cast<int>(j);
                        add_fwd(e, std::move(outs));
                    }
                }
            }
        }
        if (meet()) break;

        // ---- one backward level ---------------------------------------------
        //
        // Only unique inverses are expanded. A non-unique one (sort, tail,
        // filter) would generate goals that are usually wrong and would flood
        // the pool -- which is exactly why naive backward search has a bad
        // reputation and why this one is restricted.
        {
            const std::size_t lo = goal_frontier, hi = goals.size();
            goal_frontier = hi;
            for (std::size_t gi = lo; gi < hi && goals.size() < goal_cap; ++gi) {
                // A COPY, not a reference. `push` below appends to `goals`, and
                // the first reallocation would invalidate a reference into it --
                // a use-after-free that crashes before a single line of output
                // is flushed, which is exactly how it presented.
                const std::vector<Value> G = goals[gi].want;

                auto push = [&](Op wrap, int operand, std::uint8_t k,
                                std::vector<Value> pre) {
                    Goal g;
                    g.want = std::move(pre);
                    g.op = wrap;
                    g.operand = operand;
                    g.k = k;
                    g.parent = static_cast<int>(gi);
                    add_goal(std::move(g));
                };

                // rev: its own inverse, always exact.
                if (spec.allows(Op::Rev)) {
                    std::vector<Value> pre(ncase);
                    bool ok = true;
                    for (std::size_t c = 0; c < ncase && ok; ++c) ok = inv_rev(G[c], pre[c]);
                    if (ok) push(Op::Rev, -1, 0, std::move(pre));
                }

                // mapadd / mapmul against each constant.
                for (std::uint8_t k = 0; k < 16 && goals.size() < goal_cap; ++k) {
                    const std::int64_t kv = const_at(k);
                    if (spec.allows(Op::MapAdd) && kv != 0) {
                        std::vector<Value> pre(ncase);
                        bool ok = true;
                        for (std::size_t c = 0; c < ncase && ok; ++c) {
                            ok = inv_mapadd(G[c], kv, pre[c]);
                        }
                        if (ok) push(Op::MapAdd, -1, k, std::move(pre));
                    }
                    if (spec.allows(Op::MapMul) && kv != 0 && kv != 1) {
                        std::vector<Value> pre(ncase);
                        bool ok = true;
                        for (std::size_t c = 0; c < ncase && ok; ++c) {
                            ok = inv_mapmul(G[c], kv, pre[c]);
                        }
                        if (ok) push(Op::MapMul, -1, k, std::move(pre));
                    }
                }

                // add / sub against forward expressions already built. This is
                // where the two halves genuinely cooperate: the backward step
                // uses what the forward step has found.
                const std::size_t operands = std::min<std::size_t>(expr.size(), 64);
                for (std::size_t f = 0; f < operands && goals.size() < goal_cap; ++f) {
                    if (spec.allows(Op::Add)) {
                        std::vector<Value> pre(ncase);
                        bool ok = true;
                        for (std::size_t c = 0; c < ncase && ok; ++c) {
                            ok = inv_add_left(G[c], beh[f][c], pre[c]);
                        }
                        if (ok) push(Op::Add, static_cast<int>(f), 0, std::move(pre));
                    }
                    if (spec.allows(Op::Sub)) {
                        std::vector<Value> pre(ncase);
                        bool ok = true;
                        for (std::size_t c = 0; c < ncase && ok; ++c) {
                            ok = inv_sub_left(G[c], beh[f][c], pre[c]);
                        }
                        if (ok) push(Op::Sub, static_cast<int>(f), 0, std::move(pre));
                    }
                }
            }
        }
    }

    r.distinct_behaviours = expr.size();
    if (!meet()) return r;

    // ---- assemble -----------------------------------------------------------
    //
    // Walk the goal chain outward from the meeting point, wrapping the forward
    // expression once per goal. The chain is short by construction: it is the
    // backward half of the depth.
    Recipe rec;
    rec.pool = expr;
    std::size_t root = static_cast<std::size_t>(found_fwd);
    for (int gi = found_goal; gi >= 0; gi = goals[static_cast<std::size_t>(gi)].parent) {
        const Goal& g = goals[static_cast<std::size_t>(gi)];
        if (g.op == Op::Mov) continue;
        Expr e;
        e.op = g.op;
        e.a = static_cast<int>(root);
        e.b = g.operand;
        e.k = g.k;
        // A unary wrapper carrying a constant needs that constant as a real
        // operand, because MapAdd and MapMul read it from register b.
        if ((g.op == Op::MapAdd || g.op == Op::MapMul) && g.operand < 0) {
            Expr kc; kc.op = Op::Const; kc.k = g.k;
            rec.pool.push_back(kc);
            e.b = static_cast<int>(rec.pool.size()) - 1;
        }
        rec.pool.push_back(e);
        root = rec.pool.size() - 1;
    }
    rec.root = root;
    rec.found = true;

    // ---- and re-check, because an inverse can be sound on the sample and
    // wrong in general -----------------------------------------------------
    for (const Case& c : spec.cases)   if (rec.apply(c.in, lib) == c.out) ++r.cases_passed;
    for (const Case& c : spec.holdout) if (rec.apply(c.in, lib) == c.out) ++r.holdout_passed;

    r.recipe = std::move(rec);
    if (r.cases_passed == r.cases_total) {
        r.proof = (r.holdout_total == 0 || r.holdout_passed == r.holdout_total)
                      ? Proof::Generalised : Proof::Tested;
    }
    return r;
}

} // namespace khora::techne
