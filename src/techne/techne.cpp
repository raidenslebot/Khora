#include "khora/techne/techne.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <functional>
#include <numeric>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace khora::techne {
namespace {

// splitmix64. NOTE: seeds are used raw, never forced odd.
//
// An earlier `seed | 1ULL` here silently mapped seeds 1000 and 1001 to the same
// stream, so Program::random(3, 1000) and Program::random(3, 1001) were the
// SAME PROGRAM. It surfaced as a library refusing half its admissions as
// duplicates, but the real damage was upstream: it halved the diversity of every
// randomly initialised population in this module. splitmix64 has no requirement
// that its seed be odd.
inline std::uint64_t splitmix(std::uint64_t& s) noexcept {
    std::uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

inline double unit(std::uint64_t& s) noexcept {
    return static_cast<double>(splitmix(s) >> 11) / 9007199254740992.0;
}

// EVERY VALUE IS CAPPED, and that is what makes the arithmetic total rather
// than carefully-case-analysed.
//
// The first version used hand-written saturating add and multiply. It had
// undefined behaviour in it -- negating INT64_MIN to implement subtraction --
// and the test process died at once. Case analysis over the full 64-bit range
// is exactly the kind of thing that looks right and is not.
//
// Capping magnitudes at 1e9 makes a sum at most 2e9 and a product at most 1e18,
// both comfortably inside int64. No overflow is possible, so no check is
// needed, and there is no case to get wrong. Results are re-capped, so the cap
// is a closed interval under every operation.
inline constexpr std::int64_t kValueCap = 1'000'000'000;

inline std::int64_t cap(std::int64_t x) noexcept {
    return x < -kValueCap ? -kValueCap : (x > kValueCap ? kValueCap : x);
}
inline std::int64_t sat_add(std::int64_t a, std::int64_t b) noexcept { return cap(a + b); }
inline std::int64_t sat_sub(std::int64_t a, std::int64_t b) noexcept { return cap(a - b); }
inline std::int64_t sat_mul(std::int64_t a, std::int64_t b) noexcept { return cap(a * b); }

inline void clamp_len(Value& v) { if (v.size() > kMaxListLen) v.resize(kMaxListLen); }

// Elementwise with the shorter operand cycling. Cycling rather than truncating
// means a scalar operand broadcasts over a list for free, which is the common
// case and would otherwise need a separate opcode.
template <typename F>
Value zip(const Value& a, const Value& b, F f) {
    if (a.empty() || b.empty()) return {};
    Value out;
    out.reserve(a.size());
    for (std::size_t i = 0; i < a.size(); ++i) out.push_back(f(a[i], b[i % b.size()]));
    return out;
}

const char* op_name(Op o) {
    switch (o) {
        case Op::Nop: return "nop";      case Op::Mov: return "mov";
        case Op::Const: return "const";  case Op::Add: return "add";
        case Op::Sub: return "sub";      case Op::Mul: return "mul";
        case Op::Div: return "div";      case Op::Mod: return "mod";
        case Op::Len: return "len";      case Op::Head: return "head";
        case Op::Tail: return "tail";    case Op::Rev: return "rev";
        case Op::Sort: return "sort";    case Op::Append: return "append";
        case Op::Take: return "take";    case Op::Drop: return "drop";
        case Op::Index: return "index";  case Op::Range: return "range";
        case Op::Sum: return "sum";      case Op::Max: return "max";
        case Op::Min: return "min";      case Op::Filter: return "filter";
        case Op::MapAdd: return "mapadd";case Op::MapMul: return "mapmul";
        case Op::Count: return "count";  case Op::Call: return "call";
        case Op::Arg:   return "arg";
        case Op::Scan:  return "scan";
        case Op::Guard: return "guard";  case Op::Else: return "else";
        case Op::MapF: return "mapf";    case Op::FoldF: return "foldf";
        case Op::Gt: return "gt";        case Op::Member: return "member";
        case Op::Until: return "until";  case Op::Delta: return "delta";
        default: return "?";
    }
}

// Which opcodes read a second register operand. Needed by liveness.
inline bool binary(Op o) {
    switch (o) {
        case Op::Add: case Op::Sub: case Op::Mul: case Op::Div: case Op::Mod:
        case Op::Append: case Op::Take: case Op::Drop: case Op::Index:
        case Op::Filter: case Op::MapAdd: case Op::MapMul: case Op::Count:
        case Op::Guard: case Op::Else:
        case Op::Gt: case Op::Member: case Op::Until:
            return true;
        default: return false;
    }
}

// The constants a program can name. Small integers dominate real code, and a
// full 8-bit literal range would spend most of the search on constants nobody
// needs.
inline std::int64_t const_of(std::uint8_t b) {
    static const std::array<std::int64_t, 16> k{0, 1, 2, 3, 4, 5, 6, 7,
                                                8, 9, 10, -1, -2, 100, 1000, 2};
    return k[b % k.size()];
}

} // namespace

// ---------------------------------------------------------------------------
// Program
// ---------------------------------------------------------------------------

Program Program::random(std::size_t instrs, std::uint64_t seed) {
    std::vector<std::uint8_t> t(instrs * 4);
    std::uint64_t s = seed;
    for (auto& b : t) b = static_cast<std::uint8_t>(splitmix(s) & 0xFF);
    return Program(std::move(t));
}

std::vector<Instr> Program::decode() const {
    std::vector<Instr> out;
    out.reserve(tape_.size() / 4);
    for (std::size_t i = 0; i + 3 < tape_.size(); i += 4) {
        Instr c;
        // Scaled rather than wrapped: a plain modulo leaves the tail of the enum
        // under-represented, and in the sibling organ the under-represented tail
        // was exactly the set of opcodes that carried a gradient.
        c.op  = static_cast<Op>((static_cast<std::uint32_t>(tape_[i]) *
                                 static_cast<std::uint32_t>(Op::kCount)) >> 8);
        c.dst = tape_[i + 1] % kRegisters;
        c.a   = tape_[i + 2] % kRegisters;
        c.b   = tape_[i + 3];
        out.push_back(c);
    }
    return out;
}

Program Program::mutate(std::uint64_t seed, double rate) const {
    std::uint64_t s = seed;
    std::vector<std::uint8_t> t;
    t.reserve(tape_.size() + 4);
    for (const std::uint8_t b : tape_) {
        t.push_back(unit(s) < rate ? static_cast<std::uint8_t>(splitmix(s) & 0xFF) : b);
    }
    // Whole-instruction indels keep the reading frame. Substitution alone can
    // never change program LENGTH, which would fix the shape of every solution
    // the search can reach.
    if (unit(s) < rate * 0.15 && t.size() >= 8) {
        const std::size_t at = (splitmix(s) % (t.size() / 4)) * 4;
        t.erase(t.begin() + static_cast<std::ptrdiff_t>(at),
                t.begin() + static_cast<std::ptrdiff_t>(at + 4));
    } else if (unit(s) < rate * 0.15 && t.size() < 128) {
        const std::size_t at = (splitmix(s) % (t.size() / 4 + 1)) * 4;
        std::array<std::uint8_t, 4> c{};
        for (auto& x : c) x = static_cast<std::uint8_t>(splitmix(s) & 0xFF);
        t.insert(t.begin() + static_cast<std::ptrdiff_t>(at), c.begin(), c.end());
    }
    return Program(std::move(t));
}

Program Program::cross(const Program& x, const Program& y, std::uint64_t seed) {
    std::uint64_t s = seed;
    if (x.length() == 0) return y;
    if (y.length() == 0) return x;
    const std::size_t a = (splitmix(s) % x.length()) * 4;
    const std::size_t b = (splitmix(s) % y.length()) * 4;
    std::vector<std::uint8_t> t;
    t.insert(t.end(), x.tape().begin(), x.tape().begin() + static_cast<std::ptrdiff_t>(a));
    t.insert(t.end(), y.tape().begin() + static_cast<std::ptrdiff_t>(b), y.tape().end());
    if (t.empty()) t = x.tape();
    return Program(std::move(t));
}

std::size_t Program::output_register() const {
    std::size_t out = 0;
    for (const Instr& c : decode()) if (c.op != Op::Nop) out = c.dst;
    return out;
}

std::vector<bool> Program::live_mask() const {
    const auto code = decode();
    std::vector<bool> live(code.size(), false);
    std::array<bool, kRegisters> reg{};
    reg[output_register()] = true;
    for (std::size_t k = code.size(); k-- > 0;) {
        const Instr& c = code[k];
        if (c.op == Op::Nop || !reg[c.dst]) continue;
        live[k] = true;
        const bool reads_dst = (c.a == c.dst) ||
                               (binary(c.op) && (c.b % kRegisters) == c.dst);
        if (!reads_dst) reg[c.dst] = false;
        if (c.op != Op::Const) reg[c.a] = true;
        if (binary(c.op)) reg[c.b % kRegisters] = true;
    }
    return live;
}

std::size_t Program::effective_length() const {
    std::size_t n = 0;
    for (const bool b : live_mask()) if (b) ++n;
    return n;
}

std::string Program::disassemble() const {
    std::string out;
    char line[128];
    const auto live = live_mask();
    std::size_t i = 0;
    for (const Instr& c : decode()) {
        switch (c.op) {
            case Op::Const:
                std::snprintf(line, sizeof line, "%2zu  const  r%u <- %lld",
                              i, c.dst, static_cast<long long>(const_of(c.b)));
                break;
            case Op::Call:
                std::snprintf(line, sizeof line, "%2zu  call   r%u <- lib[%u](r%u)",
                              i, c.dst, c.b, c.a);
                break;
            case Op::Nop:
                std::snprintf(line, sizeof line, "%2zu  nop", i);
                break;
            default:
                if (binary(c.op)) {
                    std::snprintf(line, sizeof line, "%2zu  %-6s r%u <- r%u, r%u",
                                  i, op_name(c.op), c.dst, c.a, c.b % kRegisters);
                } else {
                    std::snprintf(line, sizeof line, "%2zu  %-6s r%u <- r%u",
                                  i, op_name(c.op), c.dst, c.a);
                }
                break;
        }
        std::string l(line);
        while (l.size() < 40) l += ' ';
        out += l;
        out += (i < live.size() && live[i]) ? "  <- LIVE" : "  (dead)";
        out.push_back(0x0A);
        ++i;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Library
// ---------------------------------------------------------------------------

bool Library::admit(std::string name, Program body, std::size_t task) {
    for (const auto& it : items_) if (it.body.tape() == body.tape()) return false;
    Learned l;
    l.name = std::move(name);
    l.body = std::move(body);
    l.born = task;
    items_.push_back(std::move(l));
    reorder();
    return true;
}

bool Library::admit_recipe(std::string name, Recipe r, std::size_t task) {
    if (!r.found) return false;

    for (const auto& it : items_) {
        if (it.recipe.found && it.recipe.pool.size() == r.pool.size() &&
            it.recipe.root == r.root) {
            bool same = true;
            for (std::size_t i = 0; i < r.pool.size() && same; ++i) {
                const Expr& a = it.recipe.pool[i];
                const Expr& b = r.pool[i];
                same = (a.op == b.op && a.a == b.a && a.b == b.b && a.k == b.k);
            }
            if (same) return false;
        }
    }
    Learned l;
    l.name = std::move(name);
    l.recipe = std::move(r);
    l.born = task;

    // RECORD WHAT THIS SOLUTION USED. `uses` is the quantity prune() evicts on,
    // and nothing in the tree ever incremented it -- so "utility-based eviction"
    // was sorting a column of zeroes and keeping whatever insertion order
    // happened to be. The header defines uses as "appearances in later certified
    // solutions", and this is the only place a later certified solution arrives.
    //
    // Reachable nodes only: an unreachable node is dead code in the pool, and a
    // call it contains never runs, so counting it would credit an entry for work
    // that does not happen.
    if (!items_.empty()) {
        const Recipe& rr = l.recipe;
        std::vector<bool> live(rr.pool.size(), false);
        std::vector<std::size_t> stack{rr.root};
        while (!stack.empty()) {
            const std::size_t i = stack.back();
            stack.pop_back();
            if (i >= rr.pool.size() || live[i]) continue;
            live[i] = true;
            if (rr.pool[i].a >= 0) stack.push_back(static_cast<std::size_t>(rr.pool[i].a));
            if (rr.pool[i].b >= 0) stack.push_back(static_cast<std::size_t>(rr.pool[i].b));
        }
        for (std::size_t i = 0; i < rr.pool.size(); ++i) {
            if (!live[i]) continue;
            const Expr& e = rr.pool[i];
            if (e.op == Op::Call || e.op == Op::MapF || e.op == Op::FoldF)
                note_use(e.k % items_.size());
        }
    }
    items_.push_back(std::move(l));
    reorder();
    return true;
}

Value Library::call(std::size_t index, const Value& arg, std::size_t depth) const {
    if (index >= items_.size() || depth >= kMaxCallDepth) return {};
    const Learned& l = items_[index];
    // A recipe takes precedence: it is the form the engine that actually solves
    // things produces. Depth is threaded through both paths.
    if (l.recipe.found) return l.recipe.apply(arg, this, depth);
    return run(l.body, arg, this, depth);
}

void Library::reorder() {
    order_.resize(items_.size());
    for (std::size_t i = 0; i < order_.size(); ++i) order_[i] = i;
    std::stable_sort(order_.begin(), order_.end(), [&](std::size_t x, std::size_t y) {
        // NOTE WHAT IS NOT HERE: `uses`. See the header. Age alone, measured.
        if (items_[x].uses != items_[y].uses) return items_[x].uses > items_[y].uses;
        // Ties break OLDEST first, not newest, because body depth is body cost:
        // a recent entry is a deep recipe and every speculative call runs it once
        // per case. Age is a free proxy for cheapness, and only a real use count
        // may promote past it.
        //
        // WHICH OF uses AND age SHOULD DOMINATE IS UNMEASURED, and saying so is
        // better than the alternative. Deciding it needs a benchmark that both
        // exceeds the library budget and holds its task set fixed, and neither
        // benchmark here is that: techne_bench fixes its curriculum but admits
        // too few entries to ever prune, while ascent_bench prunes constantly and
        // BUILDS ITS NEXT TIER OUT OF WHAT IT JUST SOLVED, so any engine change
        // hands it a different problem set and the totals stop being comparable.
        // I read four such runs as a 2x slowdown before checking that against a
        // fixed task set, where the same two engines came out byte-identical.
        return items_[x].born < items_[y].born;
    });
}

std::size_t Library::prune() {
    if (items_.size() <= budget_) return 0;

    // THIS USED TO SORT items_ IN PLACE AND TRUNCATE, WHICH IS TWO BUGS.
    //
    // First, a stored recipe names its callee by INDEX in Expr::k and evaluates
    // through items_[k] later. Permuting the store silently changes what every
    // already-certified library program computes. It had not fired only because
    // uses was never incremented, so every sort key was equal and stable_sort
    // left the order alone -- dormant for exactly the reason the call-depth bug
    // was dormant, and armed the moment the counter started moving.
    //
    // Second, with all keys equal, truncation dropped the TAIL: the newest
    // entries, learned at the deepest tier reached, which in an ascent are
    // precisely the ones the next tier needs.
    //
    // So: choose survivors by utility, keep them in their ORIGINAL relative
    // order, and rewrite every surviving recipe's indices through the remap.
    const std::vector<std::size_t> ord = order_;
    std::vector<bool> keep;

    // A kept entry whose body calls a dropped entry would dangle. Closing the
    // keep set under references fixes that, and I built it twice: first without
    // a cap, which let the library grow past its budget forever -- the exact
    // unbounded-growth failure this class exists to prevent, reintroduced by the
    // fix for a different bug -- then with a greedy cap.
    //
    // The closure is unnecessary either way. AN ENTRY CAN ONLY CALL ENTRIES THAT
    // ALREADY EXISTED WHEN IT WAS BUILT, so after compaction every callee has a
    // LOWER index than its caller, and an age-ordered prefix is therefore already
    // closed under references for free.
    //
    // The remap below is what makes that safe. Without it a stale k wraps through
    // the modulo in apply_op and silently resolves to whichever function now sits
    // in that slot -- which is what the old prune did, sorting items_ in place
    // and truncating. That had never fired only because nothing incremented
    // `uses`, so every sort key was equal and stable_sort left the order alone:
    // dormant for exactly the reason the call-depth bug was dormant, and armed
    // the moment the counter started moving. Truncation also dropped the TAIL,
    // the newest entries, which in an ascent are the deepest reached.
    keep.assign(items_.size(), false);
    for (std::size_t n = 0; n < budget_ && n < ord.size(); ++n) keep[ord[n]] = true;

    const std::size_t was = items_.size();
    std::vector<std::size_t> remap(was, 0);
    std::vector<Learned> kept;
    kept.reserve(was);
    for (std::size_t i = 0; i < was; ++i) {
        if (!keep[i]) continue;
        remap[i] = kept.size();
        kept.push_back(std::move(items_[i]));
    }
    for (Learned& l : kept) {
        for (Expr& e : l.recipe.pool) {
            if (e.op != Op::Call && e.op != Op::MapF && e.op != Op::FoldF) continue;
            e.k = static_cast<std::uint8_t>(remap[e.k % was]);
        }
    }
    const std::size_t dropped = was - kept.size();
    items_ = std::move(kept);
    evicted_ += dropped;
    reorder();
    return dropped;
}

// ---------------------------------------------------------------------------
// Execution
// ---------------------------------------------------------------------------

Value run(const Program& p, const Value& input, const Library* lib, std::size_t depth) {
    std::array<Value, kRegisters> r;
    // Cap on entry too, so a caller cannot hand in a value that breaks the
    // invariant the arithmetic relies on.
    r[0] = input;
    for (auto& x : r[0]) x = cap(x);
    // The other registers start empty rather than as copies of the input: a
    // register file pre-loaded with the input hands the search free identity
    // programs, and an identity that scores well on some cases is a local
    // optimum with a moat around it.
    for (std::size_t i = 1; i < kRegisters; ++i) r[i].clear();

    const auto code = p.decode();
    for (const Instr& c : code) {
        const Value& A = r[c.a];
        const Value& B = r[c.b % kRegisters];
        Value out;
        switch (c.op) {
            case Op::Nop:   continue;
            case Op::Mov:   out = A; break;
            case Op::Const: out = Value{const_of(c.b)}; break;
            case Op::Add:   out = zip(A, B, [](auto x, auto y) { return sat_add(x, y); }); break;
            case Op::Sub:   out = zip(A, B, [](auto x, auto y) { return sat_sub(x, y); }); break;
            case Op::Mul:   out = zip(A, B, [](auto x, auto y) { return sat_mul(x, y); }); break;
            case Op::Div:   out = zip(A, B, [](auto x, auto y) {
                                return y == 0 ? std::int64_t{0} : x / y; }); break;
            case Op::Mod:   out = zip(A, B, [](auto x, auto y) {
                                return y == 0 ? std::int64_t{0} : x % y; }); break;
            case Op::Len:   out = Value{static_cast<std::int64_t>(A.size())}; break;
            case Op::Head:  if (!A.empty()) out = Value{A.front()}; break;
            case Op::Tail:  if (A.size() > 1) out.assign(A.begin() + 1, A.end()); break;
            case Op::Rev:   out.assign(A.rbegin(), A.rend()); break;
            case Op::Sort:  out = A; std::sort(out.begin(), out.end()); break;
            case Op::Append: out = A; out.insert(out.end(), B.begin(), B.end()); break;
            case Op::Take: {
                if (B.empty()) break;
                const std::int64_t n = std::max<std::int64_t>(0, B[0]);
                out.assign(A.begin(),
                           A.begin() + static_cast<std::ptrdiff_t>(
                               std::min<std::size_t>(A.size(), static_cast<std::size_t>(n))));
                break;
            }
            case Op::Drop: {
                if (B.empty()) break;
                const std::int64_t n = std::max<std::int64_t>(0, B[0]);
                const std::size_t d = std::min<std::size_t>(A.size(), static_cast<std::size_t>(n));
                out.assign(A.begin() + static_cast<std::ptrdiff_t>(d), A.end());
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
                const std::int64_t n = std::min<std::int64_t>(
                    std::max<std::int64_t>(0, A[0]), static_cast<std::int64_t>(kMaxListLen));
                out.resize(static_cast<std::size_t>(n));
                std::iota(out.begin(), out.end(), std::int64_t{0});
                break;
            }
            case Op::Sum: {
                std::int64_t s = 0;
                for (const auto x : A) s = sat_add(s, x);
                out = Value{s};
                break;
            }
            case Op::Max: if (!A.empty()) out = Value{*std::max_element(A.begin(), A.end())}; break;
            case Op::Min: if (!A.empty()) out = Value{*std::min_element(A.begin(), A.end())}; break;
            case Op::Filter: {
                if (B.empty()) break;
                for (const auto x : A) if (x > B[0]) out.push_back(x);
                break;
            }
            case Op::MapAdd: {
                if (B.empty()) break;
                for (const auto x : A) out.push_back(sat_add(x, B[0]));
                break;
            }
            case Op::MapMul: {
                if (B.empty()) break;
                for (const auto x : A) out.push_back(sat_mul(x, B[0]));
                break;
            }
            case Op::Count: {
                if (B.empty()) break;
                std::int64_t n = 0;
                for (const auto x : A) if (x == B[0]) ++n;
                out = Value{n};
                break;
            }
            case Op::Guard: if (!B.empty()) out = A; break;
            case Op::Else:  out = A.empty() ? B : A; break;
            case Op::Gt: {
                if (B.empty()) break;
                for (const auto x : A) out.push_back(x > B[0] ? 1 : 0);
                break;
            }
            case Op::Member: {
                if (B.empty()) break;
                // O(|a| + |b|), not O(|a| x |b|). Scanning all of B for every
                // element of A is applied to every PAIR of pool entries inside the
                // search, and with two 512-element lists that is 262,144 comparisons
                // per candidate per case -- which took the throughput benchmark from
                // finishing in seconds to not finishing at all. A quadratic operation
                // in a hot loop is not a slow program, it is a different one.
                //
                // A hash set costs more than a scan for a handful of elements, so the
                // scan is kept where it wins.
                if (B.size() <= 16) {
                    for (const auto x : A) {
                        out.push_back(std::find(B.begin(), B.end(), x) != B.end() ? 1 : 0);
                    }
                } else {
                    const std::unordered_set<std::int64_t> seen(B.begin(), B.end());
                    for (const auto x : A) out.push_back(seen.count(x) ? 1 : 0);
                }
                break;
            }
            case Op::Until: {
                if (B.empty()) break;
                for (const auto x : A) { if (x == B[0]) break; out.push_back(x); }
                break;
            }
            case Op::Delta: {
                for (std::size_t i = 1; i < A.size(); ++i) out.push_back(cap(A[i] - A[i - 1]));
                break;
            case Op::Scan: { std::int64_t acc = 0;
                for (const auto x : A) { acc = cap(acc + x); out.push_back(acc); } } break;
            }
        case Op::MapF: {
                if (lib == nullptr || lib->size() == 0) break;
                const std::size_t li = c.b % lib->size();
                for (const auto x : A) {
                    const Value r1 = lib->call(li, Value{x}, depth + 1);
                    out.insert(out.end(), r1.begin(), r1.end());
                    if (out.size() > kMaxListLen) break;
                }
                break;
            }
            case Op::FoldF: {
                if (lib == nullptr || lib->size() == 0 || A.empty()) break;
                if (depth > 0) break;                 // see MapF: no nesting
                const std::size_t li = c.b % lib->size();
                Value acc{A[0]};
                for (std::size_t i = 1; i < A.size(); ++i) {
                    // The body receives the running value and the next element
                    // as a two-element list, which is how a one-argument machine
                    // expresses a binary operation with no second input channel.
                    Value pair = acc;
                    pair.push_back(A[i]);
                    acc = lib->call(li, pair, depth + 1);
                    if (acc.empty()) break;
                }
                out = acc;
                break;
            }
            case Op::Call: {
                if (lib == nullptr || lib->size() == 0) break;
                out = lib->call(c.b % lib->size(), A, depth + 1);
                break;
            }
            default: break;
        }
        clamp_len(out);
        r[c.dst] = std::move(out);
    }
    return r[p.output_register()];
}

// ---------------------------------------------------------------------------
// Scoring
// ---------------------------------------------------------------------------

double score(const Program& p, const std::vector<Case>& cases, const Library* lib) {
    if (cases.empty()) return 0.0;
    std::size_t exact = 0;
    double partial = 0.0;
    for (const Case& c : cases) {
        const Value got = run(p, c.in, lib);
        if (got == c.out) { ++exact; partial += 1.0; continue; }
        // PARTIAL CREDIT, and it is not decoration. A previous organ in this
        // repo could not find a ONE-INSTRUCTION solution in 2,048 births because
        // every wrong answer scored identically to every other wrong answer:
        // a flat surface with a single invisible needle. Element-level agreement
        // gives the surface a slope.
        const std::size_t n = std::max(got.size(), c.out.size());
        if (n == 0) { partial += 1.0; continue; }
        std::size_t same = 0;
        for (std::size_t i = 0; i < std::min(got.size(), c.out.size()); ++i) {
            if (got[i] == c.out[i]) ++same;
        }
        partial += static_cast<double>(same) / static_cast<double>(n);
    }
    const double m = static_cast<double>(cases.size());
    // Exact matches dominate by three orders of magnitude, so a program that
    // solves one case outright always outranks one that is merely close on all
    // of them.
    return static_cast<double>(exact) / m + 0.001 * (partial / m);
}

namespace {

std::size_t exact_count(const Program& p, const std::vector<Case>& cases, const Library* lib) {
    std::size_t n = 0;
    for (const Case& c : cases) if (run(p, c.in, lib) == c.out) ++n;
    return n;
}

Solution certify(const Spec& spec, Program p, const Library* lib, std::size_t tried) {
    Solution s;
    s.program = std::move(p);
    s.candidates_tried = tried;
    s.cases_total = spec.cases.size();
    s.holdout_total = spec.holdout.size();
    s.cases_passed = exact_count(s.program, spec.cases, lib);
    s.holdout_passed = exact_count(s.program, spec.holdout, lib);

    // THE CONTRACT. Nothing is certified unless every visible case passes, and
    // "Generalised" additionally requires every held-out case -- cases the
    // search never saw and could not have fitted. A program that passes the
    // visible cases and fails the held-out ones is memorisation, and it is
    // reported as Tested rather than dressed up.
    if (s.cases_total > 0 && s.cases_passed == s.cases_total) {
        s.proof = (s.holdout_total == 0 || s.holdout_passed == s.holdout_total)
                      ? Proof::Generalised : Proof::Tested;
    } else {
        s.proof = Proof::None;
    }
    return s;
}

} // namespace

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

Solution synthesise(const Spec& spec, SearchConfig cfg, Library* lib) {
    std::uint64_t rng = cfg.seed;
    struct Ind { Program p; double f = -1.0; };

    std::vector<Ind> pop;
    pop.reserve(cfg.population);
    for (std::size_t i = 0; i < cfg.population; ++i) {
        Ind x;
        x.p = Program::random(cfg.program_len, splitmix(rng));
        x.f = score(x.p, spec.cases, lib);
        pop.push_back(std::move(x));
    }
    std::size_t tried = cfg.population;

    Ind best = pop.front();
    for (const Ind& x : pop) if (x.f > best.f) best = x;

    const double solved = 1.0 + 0.001 - 1e-9;   // every case exact
    for (std::size_t g = 0; g < cfg.generations && best.f < solved; ++g) {
        const std::size_t replace = std::max<std::size_t>(1, pop.size() / 4);
        for (std::size_t k = 0; k < replace; ++k) {
            auto pick = [&]() {
                std::size_t b = splitmix(rng) % pop.size();
                for (std::size_t j = 1; j < cfg.tournament; ++j) {
                    const std::size_t c = splitmix(rng) % pop.size();
                    if (pop[c].f > pop[b].f) b = c;
                }
                return b;
            };
            std::size_t worst = splitmix(rng) % pop.size();
            for (std::size_t j = 1; j < cfg.tournament; ++j) {
                const std::size_t c = splitmix(rng) % pop.size();
                if (pop[c].f < pop[worst].f) worst = c;
            }
            Program child = (unit(rng) < cfg.crossover)
                ? Program::cross(pop[pick()].p, pop[pick()].p, splitmix(rng))
                : pop[pick()].p;
            child = child.mutate(splitmix(rng), cfg.mutation_rate);
            pop[worst].p = std::move(child);
            pop[worst].f = score(pop[worst].p, spec.cases, lib);
            ++tried;
            if (pop[worst].f > best.f) best = pop[worst];
        }
        // ELITISM. Without it the champion survives only as a reporting
        // artefact: it is never re-inserted, and a quarter of the population is
        // displaced every round.
        auto w = std::min_element(pop.begin(), pop.end(),
                                  [](const Ind& a, const Ind& b) { return a.f < b.f; });
        *w = best;
    }

    return certify(spec, best.p, lib, tried);
}

Solution enumerate(const Spec& spec, std::size_t max_len, std::size_t budget,
                   Library* lib) {
    // THE DUMB BASELINE, and in this repo it has won often enough not to be a
    // formality -- a thirty-line trigram table beat a temporal memory, and a
    // one-line graph heuristic tied an evolved operator. Systematic enumeration
    // of short programs is what a synthesiser has to beat to justify itself.
    std::uint64_t rng = 0xC0DEC0DEULL;
    Solution best;
    best.cases_total = spec.cases.size();
    double best_score = -1.0;
    Program best_p;

    for (std::size_t tried = 0; tried < budget; ++tried) {
        const std::size_t len = 1 + (tried % max_len);
        Program p = Program::random(len, splitmix(rng));
        const double f = score(p, spec.cases, lib);
        if (f > best_score) { best_score = f; best_p = p; }
        if (f >= 1.0) {
            return certify(spec, std::move(p), lib, tried + 1);
        }
    }
    return certify(spec, std::move(best_p), lib, budget);
}

// ---------------------------------------------------------------------------
// Bottom-up construction
// ---------------------------------------------------------------------------

namespace {

// The operations worth composing. Nop and Mov are excluded: they add a node
// without adding a behaviour, and this pool is indexed by behaviour.
const std::vector<Op>& unary_ops() {
    static const std::vector<Op> v{Op::Len, Op::Head, Op::Tail, Op::Rev, Op::Sort,
                                   Op::Range, Op::Sum, Op::Max, Op::Min, Op::Delta,
                                   Op::Scan};
    return v;
}
const std::vector<Op>& binary_ops() {
    static const std::vector<Op> v{Op::Add, Op::Sub, Op::Mul, Op::Div, Op::Mod,
                                   Op::Append, Op::Take, Op::Drop, Op::Index,
                                   Op::Filter, Op::MapAdd, Op::MapMul, Op::Count,
                                   Op::Guard, Op::Else,
                                   Op::Gt, Op::Member, Op::Until};
    return v;
}

// One operation, sharing the helpers the tape machine uses, so a constructed
// program and an evolved one cannot disagree about what an opcode means.
Value apply_op(Op op, const Value& A, const Value& B, std::uint8_t k,
               const Library* lib, std::size_t depth) {
    Value out;
    switch (op) {
        case Op::Const: out = Value{const_of(k)}; break;
        case Op::Add:   out = zip(A, B, [](auto x, auto y) { return sat_add(x, y); }); break;
        case Op::Sub:   out = zip(A, B, [](auto x, auto y) { return sat_sub(x, y); }); break;
        case Op::Mul:   out = zip(A, B, [](auto x, auto y) { return sat_mul(x, y); }); break;
        case Op::Div:   out = zip(A, B, [](auto x, auto y) { return y == 0 ? std::int64_t{0} : x / y; }); break;
        case Op::Mod:   out = zip(A, B, [](auto x, auto y) { return y == 0 ? std::int64_t{0} : x % y; }); break;
        case Op::Len:   out = Value{static_cast<std::int64_t>(A.size())}; break;
        case Op::Head:  if (!A.empty()) out = Value{A.front()}; break;
        case Op::Tail:  if (A.size() > 1) out.assign(A.begin() + 1, A.end()); break;
        case Op::Rev:   out.assign(A.rbegin(), A.rend()); break;
        case Op::Sort:  out = A; std::sort(out.begin(), out.end()); break;
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
        case Op::Sum: { std::int64_t t = 0; for (const auto x : A) t = sat_add(t, x); out = Value{t}; break; }
        case Op::Max: if (!A.empty()) out = Value{*std::max_element(A.begin(), A.end())}; break;
        case Op::Min: if (!A.empty()) out = Value{*std::min_element(A.begin(), A.end())}; break;
        case Op::Filter: { if (B.empty()) break; for (const auto x : A) if (x > B[0]) out.push_back(x); break; }
        case Op::MapAdd: { if (B.empty()) break; for (const auto x : A) out.push_back(sat_add(x, B[0])); break; }
        case Op::MapMul: { if (B.empty()) break; for (const auto x : A) out.push_back(sat_mul(x, B[0])); break; }
        case Op::Count: { if (B.empty()) break; std::int64_t n = 0; for (const auto x : A) if (x == B[0]) ++n; out = Value{n}; break; }
        case Op::Guard: if (!B.empty()) out = A; break;
        case Op::Else:  out = A.empty() ? B : A; break;
        case Op::Gt: {
            if (B.empty()) break;
            for (const auto x : A) out.push_back(x > B[0] ? 1 : 0);
            break;
        }
        case Op::Member: {
            if (B.empty()) break;
            // O(|a| + |b|), not O(|a| x |b|). Scanning all of B for every
            // element of A is applied to every PAIR of pool entries inside the
            // search, and with two 512-element lists that is 262,144 comparisons
            // per candidate per case -- which took the throughput benchmark from
            // finishing in seconds to not finishing at all. A quadratic operation
            // in a hot loop is not a slow program, it is a different one.
            //
            // A hash set costs more than a scan for a handful of elements, so the
            // scan is kept where it wins.
            if (B.size() <= 16) {
                for (const auto x : A) {
                    out.push_back(std::find(B.begin(), B.end(), x) != B.end() ? 1 : 0);
                }
            } else {
                const std::unordered_set<std::int64_t> seen(B.begin(), B.end());
                for (const auto x : A) out.push_back(seen.count(x) ? 1 : 0);
            }
            break;
        }
        case Op::Until: {
            if (B.empty()) break;
            for (const auto x : A) { if (x == B[0]) break; out.push_back(x); }
            break;
        }
        case Op::Delta: {
            for (std::size_t i = 1; i < A.size(); ++i) out.push_back(cap(A[i] - A[i - 1]));
            break;
        }
        case Op::Scan: {
            std::int64_t acc = 0;
            for (const auto x : A) { acc = cap(acc + x); out.push_back(acc); }
            break;
        }
        case Op::MapF: {
            if (lib == nullptr || lib->size() == 0) break;
                // NO NESTING. A fold invokes its body once per element, so a
                // fold whose body folds is quadratic in list length, and three
                // permitted levels makes 64 elements cost 262,144 evaluations
                // for one call. Speculation was bounded; the cost of CALLING an
                // admitted body was not, and one such entry made every later
                // search that touched it unaffordable -- measured, the isolated
                // arm stopped returning at 300 tasks. One level keeps a library
                // call O(elements), which is a bound that can be stated.
                if (depth > 0) break;
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
            if (depth > 0) break;                     // see MapF: no nesting
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
        case Op::Call:  if (lib && lib->size()) out = lib->call(k % lib->size(), A, depth + 1); break;
        // A leaf: resolved by whoever holds the arguments, never here.
        case Op::Arg:   out = A; break;
        default: out = A; break;
    }
    clamp_len(out);
    return out;
}

// A BEHAVIOUR is the concatenation of a candidate's outputs over every visible
// case. Two candidates with the same behaviour are indistinguishable to the
// specification, so only the first is kept -- and because the pool is grown in
// size order, the first is also the smallest.
//
// A 128-BIT FINGERPRINT, not the decimal digits it replaces.
//
// The string form allocated a fresh std::string per candidate and retained one
// per pool entry -- 695,697 candidates on this suite, each with a heap
// allocation and a key kept for the lifetime of the search. RAM is the binding
// constraint on how deep the pool can go, and spending it on keys rather than on
// behaviours is the wrong trade. Two 64-bit lanes put a collision at a million
// entries near 1e-27, far below the rate at which anything else here is wrong.
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

Sig signature(const std::vector<Value>& outs) {
    Sig s;
    for (const Value& v : outs) {
        s.feed(0xF17E5ULL);
        for (const auto x : v) s.feed(static_cast<std::uint64_t>(x));
        s.feed(static_cast<std::uint64_t>(v.size()));
    }
    return s;
}

} // namespace

Value Recipe::apply(const Value& in, const Library* lib, std::size_t depth) const {
    return apply_n(std::vector<Value>{in}, lib, depth);
}

std::size_t Recipe::arity() const {
    if (!found || pool.empty()) return 1;
    std::size_t n = 1;
    std::vector<bool> live(pool.size(), false);
    std::vector<std::size_t> stack{root};
    while (!stack.empty()) {
        const std::size_t i = stack.back();
        stack.pop_back();
        if (i >= pool.size() || live[i]) continue;
        live[i] = true;
        if (pool[i].op == Op::Arg) n = std::max<std::size_t>(n, pool[i].k + 1u);
        if (pool[i].a >= 0) stack.push_back(static_cast<std::size_t>(pool[i].a));
        if (pool[i].b >= 0) stack.push_back(static_cast<std::size_t>(pool[i].b));
    }
    return n;
}

Value Recipe::apply_n(const std::vector<Value>& args, const Library* lib,
                      std::size_t depth) const {
    static const Value kNone;
    const Value& first = args.empty() ? kNone : args[0];
    if (!found || pool.empty()) return first;
    std::vector<Value> vals(pool.size());
    for (std::size_t i = 0; i < pool.size(); ++i) {
        const Expr& e = pool[i];
        // Op::Arg is a LEAF and must be read before the operands are, because it
        // has none. An argument the caller did not supply reads as empty rather
        // than out of bounds -- a recipe of arity 2 applied to one value is a
        // caller error, not a crash.
        if (e.op == Op::Arg) {
            vals[i] = (e.k < args.size()) ? args[e.k] : kNone;
            continue;
        }
        const Value& A = (e.a < 0) ? first : vals[static_cast<std::size_t>(e.a)];
        const Value& B = (e.b < 0) ? first : vals[static_cast<std::size_t>(e.b)];
        // A mined literal is carried on the node, not selectable by index, so it
        // has to be read here or the recipe evaluates to a different constant
        // than the one the search chose.
        vals[i] = (e.op == Op::Const && e.has_lit) ? Value{e.lit}
                                                   : apply_op(e.op, A, B, e.k, lib, depth);
    }
    return vals[root];
}

Recipe Recipe::compact() const {
    if (!found || pool.empty()) return *this;
    std::vector<bool> live(pool.size(), false);
    std::vector<std::size_t> stack{root};
    while (!stack.empty()) {
        const std::size_t i = stack.back();
        stack.pop_back();
        if (i >= pool.size() || live[i]) continue;
        live[i] = true;
        if (pool[i].a >= 0) stack.push_back(static_cast<std::size_t>(pool[i].a));
        if (pool[i].b >= 0) stack.push_back(static_cast<std::size_t>(pool[i].b));
    }
    Recipe out;
    out.found = found;
    std::vector<std::size_t> remap(pool.size(), 0);
    out.pool.reserve(pool.size());
    for (std::size_t i = 0; i < pool.size(); ++i) {
        if (!live[i]) continue;
        remap[i] = out.pool.size();
        Expr e = pool[i];
        if (e.a >= 0) e.a = static_cast<int>(remap[static_cast<std::size_t>(e.a)]);
        if (e.b >= 0) e.b = static_cast<int>(remap[static_cast<std::size_t>(e.b)]);
        out.pool.push_back(e);
    }
    out.root = remap[root];
    return out;
}

std::size_t Recipe::size() const {
    if (!found) return 0;
    std::vector<bool> used(pool.size(), false);
    std::vector<std::size_t> stack{root};
    while (!stack.empty()) {
        const std::size_t i = stack.back();
        stack.pop_back();
        if (i >= pool.size() || used[i]) continue;
        used[i] = true;
        if (pool[i].a >= 0) stack.push_back(static_cast<std::size_t>(pool[i].a));
        if (pool[i].b >= 0) stack.push_back(static_cast<std::size_t>(pool[i].b));
    }
    std::size_t n = 0;
    for (const bool b : used) if (b) ++n;
    return n;
}

std::string Recipe::render() const {
    if (!found) return "(none)";
    std::function<std::string(int)> go = [&](int i) -> std::string {
        if (i < 0) return "x";
        const Expr& e = pool[static_cast<std::size_t>(i)];
        if (e.op == Op::Const) return std::to_string(e.has_lit ? e.lit : const_of(e.k));
        if (e.op == Op::Arg)   return "x" + std::to_string(e.k);
        if (e.op == Op::Call)  return "lib" + std::to_string(e.k) + "(" + go(e.a) + ")";
        // A fold is meaningless without naming its BODY. `foldf(x, x)` printed
        // its operand twice and said nothing about which learned function was
        // being folded, which makes the most interesting results this module
        // produces unreadable.
        if (e.op == Op::MapF)  return "map[lib" + std::to_string(e.k) + "](" + go(e.a) + ")";
        if (e.op == Op::FoldF) return "fold[lib" + std::to_string(e.k) + "](" + go(e.a) + ")";
        if (e.op == Op::Mov)   return go(e.a);
        const std::string nm = op_name(e.op);
        for (const Op u : unary_ops()) if (u == e.op) return nm + "(" + go(e.a) + ")";
        return nm + "(" + go(e.a) + ", " + go(e.b) + ")";
    };
    return go(static_cast<int>(root));
}

BuildResult solve_one(const Spec& spec, std::size_t max_pool, const Library* lib) {
    const bool have_lib = (lib != nullptr && lib->size() > 0);
    BuildResult fwd, bid, bare;
    std::thread t1([&] { fwd  = construct(spec, max_pool, lib); });
    std::thread t2([&] { bid  = construct_bidir(spec, max_pool, lib); });
    std::thread t3;
    if (have_lib) t3 = std::thread([&] { bare = construct(spec, max_pool, nullptr); });
    t1.join(); t2.join();
    if (t3.joinable()) t3.join();

    // FIXED PREFERENCE, never "whoever finished first". Forward before
    // bidirectional because it is the cheaper and more common answer, and both
    // before the no-library fallback so the library keeps whatever credit it has
    // earned. Ordering by arrival would make the result depend on the scheduler,
    // which is the one thing a certificate may not do.
    if (fwd.proof  == Proof::Generalised) return fwd;
    if (bid.proof  == Proof::Generalised) return bid;
    if (bare.proof == Proof::Generalised) return bare;
    BuildResult best = std::move(fwd);
    if (bid.cases_passed  > best.cases_passed) best = std::move(bid);
    if (bare.cases_passed > best.cases_passed) best = std::move(bare);
    return best;
}

BuildResult construct_best(const Spec& spec, std::size_t max_pool, const Library* lib,
                           bool mine_constants) {
    BuildResult with = construct(spec, max_pool, lib, mine_constants);
    if (with.proof == Proof::Generalised || lib == nullptr || lib->size() == 0) return with;
    BuildResult bare = construct(spec, max_pool, nullptr, mine_constants);
    if (bare.proof == Proof::Generalised) return bare;
    // Neither generalised: keep whichever got further, so the fallback cannot
    // lose information either.
    return (bare.cases_passed > with.cases_passed) ? bare : with;
}

BuildResult construct(const Spec& spec, std::size_t max_pool, const Library* lib,
                      bool mine_constants) {
    BuildResult r;
    r.cases_total = spec.cases.size();
    r.holdout_total = spec.holdout.size();
    if (spec.cases.empty()) return r;

    const std::size_t ncase = spec.cases.size();
    std::vector<Value> target;
    target.reserve(ncase);
    for (const Case& c : spec.cases) target.push_back(c.out);
    const Sig want = signature(target);

    std::vector<Expr> pool;
    std::vector<std::vector<Value>> behaviour;
    // Sig -> POOL INDEX, not just a set. Knowing WHERE a behaviour lives is what
    // lets the closing pass below turn "does this behaviour exist" into "here is
    // the node that computes it".
    std::unordered_map<Sig, std::size_t, SigHash> seen;
    int found_at = -1;

    auto consider = [&](const Expr& e, std::vector<Value> outs) {
        ++r.nodes_considered;
        const Sig sig = signature(outs);
        if (!seen.emplace(sig, pool.size()).second) return;  // observationally equivalent
        const bool is_target = (sig == want);
        pool.push_back(e);
        behaviour.push_back(std::move(outs));
        if (is_target && found_at < 0) found_at = static_cast<int>(pool.size()) - 1;
    };

    // Level 0: the input, the constants, and every library primitive.
    //
    // Seeding the library HERE is the point of the whole exercise. A learned
    // primitive is available from the first level rather than having to be
    // stumbled upon by a mutation operator -- which, measured, never happened
    // once in twenty tasks.
    {
        std::vector<Value> ident;
        ident.reserve(ncase);
        for (const Case& c : spec.cases) ident.push_back(c.in);
        Expr e; e.op = Op::Mov; e.a = -1;
        consider(e, std::move(ident));
    }
    // EVERY OTHER ARGUMENT IS ALSO A LEVEL-0 TERM. Without this the search can
    // only ever reach the first one, which is what made every program this
    // system could write a function of exactly one thing.
    {
        std::size_t arity = 1;
        for (const Case& c : spec.cases) arity = std::max(arity, c.arity());
        for (std::size_t k = 1; k < arity && found_at < 0; ++k) {
            std::vector<Value> outs(ncase);
            for (std::size_t c = 0; c < ncase; ++c) {
                outs[c] = k < spec.cases[c].arity() ? spec.cases[c].arg(k) : Value{};
            }
            Expr e; e.op = Op::Arg; e.k = static_cast<std::uint8_t>(k);
            consider(e, std::move(outs));
        }
    }
    for (std::uint8_t k = 0; k < 16 && found_at < 0; ++k) {
        std::vector<Value> outs(ncase);
        for (std::size_t c = 0; c < ncase; ++c) outs[c] = apply_op(Op::Const, {}, {}, k, lib, 0);
        Expr e; e.op = Op::Const; e.k = k;
        consider(e, std::move(outs));
    }
    // CONSTANTS MINED FROM THE SPECIFICATION ITSELF.
    //
    // The fixed table holds {0..10, -1, -2, 100, 1000}, which is a set I chose
    // and which has no relationship to any particular problem. Measured
    // consequence: the space character 32 is not nameable, so every text task
    // needing it had to BUILD it -- count_spaces and upper_lower both found
    // mul(4, 8), and first_word found until(x, mul(4, 8)). That worked. It stops
    // working the moment a task needs several such constants: count_vowels needs
    // the set {97, 101, 105, 111, 117}, which is five unnameable values plus four
    // appends, and no pool reaches it.
    //
    // The values a problem needs are almost always sitting in the problem. Every
    // distinct value appearing in the cases -- inputs as well as outputs -- is a
    // candidate the specification itself nominated, which is a far better prior
    // than my taste in round numbers. Capped, and ordered by how often they
    // occur, so a wide input alphabet cannot flood level 0.
    if (mine_constants) {
        std::unordered_map<std::int64_t, std::size_t> freq;
        for (const Case& c : spec.cases) {
            for (const auto v : c.in)  ++freq[v];
            for (const auto v : c.out) ++freq[v];
        }
        // AND FROM THE RELATIONSHIP BETWEEN INPUT AND OUTPUT, not only from the
        // values themselves.
        //
        // Measured regression that forced this: upper_lower subtracts 32 from
        // every character, and its inputs are pure lowercase, so 32 appears
        // NOWHERE in the data. Mining values alone therefore could not supply it
        // -- and worse, the 24 character codes it did mine crowded level 0 and
        // displaced the mul(4, 8) route that used to work. The task went from
        // solved to unsolved, which is a regression I caused.
        //
        // A constant is very often the DIFFERENCE or the RATIO between what went
        // in and what came out, and neither is visible in either list on its own.
        // Mining aligned differences gives -32 directly; mining exact ratios
        // gives the multiplier in a scaling task.
        for (const Case& c : spec.cases) {
            const std::size_t n = std::min(c.in.size(), c.out.size());
            for (std::size_t i = 0; i < n; ++i) {
                freq[cap(c.out[i] - c.in[i])] += 2;          // weighted above raw values
                if (c.in[i] != 0 && c.out[i] % c.in[i] == 0) freq[c.out[i] / c.in[i]] += 2;
            }
            // Length relationships too: a take or drop count is usually one of
            // these and is otherwise unnameable.
            freq[static_cast<std::int64_t>(c.out.size())] += 1;
            if (c.in.size() >= c.out.size()) {
                freq[static_cast<std::int64_t>(c.in.size() - c.out.size())] += 1;
            }
        }

        std::vector<std::pair<std::int64_t, std::size_t>> mined(freq.begin(), freq.end());
        std::sort(mined.begin(), mined.end(), [](const auto& a, const auto& b) {
            if (a.second != b.second) return a.second > b.second;
            return a.first < b.first;              // deterministic on ties
        });
        // EIGHT, NOT TWENTY-FOUR, AND THE REASON IS QUADRATIC.
        //
        // A binary level costs level0^2 x ops. At 24 mined constants level 0 goes
        // from 17 entries to 41 and that level goes from ~5,200 candidates to
        // ~30,000 -- six times the work, which took a throughput run from
        // finishing in minutes to not finishing. At 8 it is 25 entries and 2.1x,
        // which is a price worth paying.
        //
        // I first tried making mining conditional instead: cheap attempt, then a
        // mined retry on failure. That was WORSE. Roughly 45% of tasks fail, so
        // nearly half the workload paid for both attempts -- 7x rather than 6x.
        // Cheap-first only wins when the cheap case is the common case, and here
        // it is not.
        //
        // Eight is enough because the mined list is ordered by frequency with
        // input/output RELATIONSHIPS weighted above raw values, and it is the
        // relationships that carry the answers: -32 for upper_lower, 32 for
        // strip_spaces and first_word. Those sit at the top, not the tail.
        std::size_t taken = 0;
        for (const auto& [val, n] : mined) {
            if (taken >= 8) break;
            (void)n;
            bool already = false;
            for (std::uint8_t k = 0; k < 16; ++k) if (const_of(k) == val) { already = true; break; }
            if (already) continue;
            std::vector<Value> outs(ncase, Value{val});
            Expr e; e.op = Op::Const; e.k = 0; e.lit = val; e.has_lit = true;
            consider(e, std::move(outs));
            ++taken;
        }
    }
    if (lib != nullptr) {
        for (std::size_t li = 0; li < lib->size() && found_at < 0; ++li) {
            std::vector<Value> outs(ncase);
            for (std::size_t c = 0; c < ncase; ++c) {
                outs[c] = apply_op(Op::Call, spec.cases[c].in, {},
                                   static_cast<std::uint8_t>(li), lib, 0);
            }
            Expr e; e.op = Op::Call; e.a = -1; e.k = static_cast<std::uint8_t>(li);
            consider(e, std::move(outs));
        }
    }
    (void)0;

    // Grow by size. Every pool entry is already the smallest expression with its
    // behaviour, so composing over the pool composes over BEHAVIOURS rather than
    // over syntax -- which is what collapses the combinatorial explosion.
    std::size_t frontier = 0;
    while (found_at < 0 && pool.size() < max_pool) {
        const std::size_t start = frontier;
        const std::size_t end = pool.size();
        if (start >= end) break;
        frontier = end;

        for (std::size_t i = start; i < end && found_at < 0 && pool.size() < max_pool; ++i) {
            for (const Op op : unary_ops()) {
                if (!spec.allows(op)) continue;
                std::vector<Value> outs(ncase);
                for (std::size_t c = 0; c < ncase; ++c) {
                    outs[c] = apply_op(op, behaviour[i][c], {}, 0, lib, 0);
                }
                Expr e; e.op = op; e.a = static_cast<int>(i);
                consider(e, std::move(outs));
                if (found_at >= 0) break;
            }
        }
        for (std::size_t i = 0; i < end && found_at < 0 && pool.size() < max_pool; ++i) {
            for (std::size_t j = 0; j < end && found_at < 0 && pool.size() < max_pool; ++j) {
                if (i < start && j < start) continue;   // this pair already combined
                for (const Op op : binary_ops()) {
                    if (!spec.allows(op)) continue;
                    std::vector<Value> outs(ncase);
                    for (std::size_t c = 0; c < ncase; ++c) {
                        outs[c] = apply_op(op, behaviour[i][c], behaviour[j][c], 0, lib, 0);
                    }
                    Expr e; e.op = op; e.a = static_cast<int>(i); e.b = static_cast<int>(j);
                    consider(e, std::move(outs));
                    if (found_at >= 0) break;
                }
            }
        }

        // THE CLOSING PASS: solve for the operand instead of enumerating it.
        //
        // The binary sweep tries every PAIR, which is end^2 x |ops| and is what
        // puts operation-depth 4 out of reach -- a 200,000-node pool fills before
        // the level completes. But for an invertible operation the second operand
        // is DETERMINED by the target. If the answer is mul(a, b), then b is
        // target / a; there is nothing to search for.
        //
        // So for each node already in the pool, compute the operand the target
        // would require and look it up. That is O(pool) per operation instead of
        // O(pool^2), and it reaches one level deeper than the sweep that produced
        // the pool -- which is exactly the level that was missing. Measured: the
        // wall was tasks needing six operation nodes, like
        // mul(x, mapadd(range(len(x)), 1)), where every piece is cheap and only
        // the final pairing is expensive.
        //
        // EVERY HIT IS VERIFIED BY APPLYING THE OPERATION. The inverses below are
        // exact only when the shapes line up, and zip CYCLES its shorter operand,
        // so a candidate that looks right by construction can still be wrong.
        // Recomputing forward costs one op per hit and removes the whole class.
        if (found_at < 0) {
            const std::size_t close_end = pool.size();
            for (std::size_t i = 0; i < close_end && found_at < 0; ++i) {
                for (const Op op : {Op::Add, Op::Sub, Op::Mul, Op::Append}) {
                    if (!spec.allows(op)) continue;
                    std::vector<Value> need(ncase);
                    bool ok = true;
                    for (std::size_t c = 0; c < ncase && ok; ++c) {
                        const Value& A = behaviour[i][c];
                        const Value& T = target[c];
                        Value& B = need[c];
                        switch (op) {
                            case Op::Add:                       // b = t - a
                                if (A.size() != T.size()) { ok = false; break; }
                                for (std::size_t k = 0; k < T.size(); ++k) B.push_back(T[k] - A[k]);
                                break;
                            case Op::Sub:                       // b = a - t
                                if (A.size() != T.size()) { ok = false; break; }
                                for (std::size_t k = 0; k < T.size(); ++k) B.push_back(A[k] - T[k]);
                                break;
                            case Op::Mul:                       // b = t / a, exactly
                                if (A.size() != T.size()) { ok = false; break; }
                                for (std::size_t k = 0; k < T.size() && ok; ++k) {
                                    if (A[k] == 0) { if (T[k] != 0) ok = false; else B.push_back(0); }
                                    else if (T[k] % A[k] != 0) ok = false;
                                    else B.push_back(T[k] / A[k]);
                                }
                                break;
                            case Op::Append:                    // b = t with a's prefix removed
                                if (T.size() < A.size()) { ok = false; break; }
                                for (std::size_t k = 0; k < A.size() && ok; ++k)
                                    if (T[k] != A[k]) ok = false;
                                if (ok) B.assign(T.begin() + static_cast<std::ptrdiff_t>(A.size()), T.end());
                                break;
                            default: ok = false; break;
                        }
                    }
                    if (!ok) continue;
                    const auto at = seen.find(signature(need));
                    if (at == seen.end()) continue;
                    const std::size_t j = at->second;
                    // Verify forward before believing it.
                    bool same = true;
                    std::vector<Value> outs(ncase);
                    for (std::size_t c = 0; c < ncase && same; ++c) {
                        outs[c] = apply_op(op, behaviour[i][c], behaviour[j][c], 0, lib, 0);
                        if (outs[c] != target[c]) same = false;
                    }
                    if (!same) continue;
                    Expr e; e.op = op; e.a = static_cast<int>(i); e.b = static_cast<int>(j);
                    consider(e, std::move(outs));
                }
            }
        }

        // HIGHER-ORDER LAST, and this is a scheduling fix rather than a policy
        // change.
        //
        // Expanding folds and maps FIRST put up to 256 x 2 x 8 = 4,096 candidates
        // into the pool before a single ordinary operation was tried, and against
        // a 3,000-behaviour cap that meant the cheap, common answers never got
        // space. Measured: the parallel arm certified 80 of 300 while a SINGLE
        // THREAD certified 184, because the shared library filled faster and so
        // the flood arrived sooner. A search that spends its budget on the rarest
        // shape first is not exploring, it is queueing badly.

        // HIGHER-ORDER EXPANSION. MapF and FoldF are unary in shape but carry a
        // library index, so each needs one candidate per library ENTRY rather
        // than one candidate per operation. That is why they get their own loop,
        // and why the capability only appears once the system has learned
        // something worth folding with -- with an empty library there is no body
        // to supply and the loop does nothing.
        //
        // BOUNDED, because the unbounded version was unusable. Two operations
        // times every library entry, applied to every pool node, each invocation
        // running a whole library recipe over every case -- at a 40,000 pool with
        // a 32-entry library that is millions of nested evaluations per task, and
        // the benchmark simply stopped returning.
        //
        // The bound is not arbitrary. fold(f, x) applied to the raw input or to
        // something one step from it is the shape that occurs; folding over a
        // deeply transformed list is rare and can be reached the other way, by
        // learning the transformation as a library entry first. Capping the
        // bodies at the most recent few costs the same way and for the same
        // reason.
        const std::size_t kHigherOrderNodes = 256;
        const std::size_t kHigherOrderBodies = 8;
        // A CALL COSTS ONE BODY EVALUATION PER CASE. A map or fold costs one per
        // ELEMENT per case -- up to 64x more by the operand cap just below. They
        // shared a bound anyway, which priced the cheap one as if it were the
        // expensive one: with a 32-entry budget and a limit of 8, TWENTY-FOUR OF
        // EVERY THIRTY-TWO LEARNED ENTRIES WERE UNREACHABLE. Admitted, counted in
        // the reported size, competing for budget in prune(), and impossible to
        // name from a Call.
        //
        // It is nevertheless still 8, not 64. Raising it made the ascent finish
        // four tiers instead of fifteen -- but see the note on prune() below: the
        // ascent regenerates its curriculum from its own results, so that is an
        // observation about one run and NOT a measurement of this constant. The
        // conservative value stays until an instrument exists that can price it.
        const std::size_t kCallBodies = 8;
        // A LIBRARY CALL ON AN INTERMEDIATE NODE, not only on the raw input.
        //
        // Op::Call was seeded at level 0 with a == -1 and nowhere else, so it
        // could only ever be applied to the argument. That makes
        // lib_j(lib_i(x)) -- a composition of two learned functions --
        // UNREACHABLE, which is precisely the mechanism that is supposed to make
        // a deep task cheap once its parts are known.
        //
        // It is the reason the ascent stalled: tiers 6 to 13 verified 1 to 3 of
        // 20 with a full 32-entry library, because the library could contribute
        // at most one call to any answer. A library you can only call once is a
        // set of shortcuts, not a vocabulary.
        //
        // Bounded and placed after the ordinary operations for the same reason
        // the higher-order expansion is: candidates that cost more should not get
        // the pool before candidates that cost less.
        static const std::vector<std::size_t> kNoBodies;
        const std::vector<std::size_t>& body_order =
            lib != nullptr ? lib->search_order() : kNoBodies;
        if (lib != nullptr && lib->size() > 0) {
            const std::size_t call_nodes = std::min(end, kHigherOrderNodes);
            const std::size_t call_bodies = std::min(body_order.size(), kCallBodies);
            for (std::size_t i = start; i < call_nodes && found_at < 0 && pool.size() < max_pool; ++i) {
                for (std::size_t oi = 0; oi < call_bodies && found_at < 0; ++oi) {
                    const std::size_t li = body_order[oi];
                    std::vector<Value> outs(ncase);
                    for (std::size_t c = 0; c < ncase; ++c) {
                        outs[c] = apply_op(Op::Call, behaviour[i][c], {},
                                           static_cast<std::uint8_t>(li), lib, 0);
                    }
                    Expr e; e.op = Op::Call; e.a = static_cast<int>(i);
                    e.k = static_cast<std::uint8_t>(li);
                    consider(e, std::move(outs));
                }
            }
        }

        if (lib != nullptr && lib->size() > 0) {
            const std::size_t node_lim = std::min(end, kHigherOrderNodes);
            const std::size_t body_lim = std::min(lib->size(), kHigherOrderBodies);
            for (std::size_t i = start; i < node_lim && found_at < 0 && pool.size() < max_pool; ++i) {
                // SKIP LARGE OPERANDS. A fold or map invokes its body ONCE PER
                // ELEMENT, per case, per candidate -- so applying one to a
                // 512-element list built by Range costs thousands of nested
                // recipe evaluations for a single candidate, and the benchmark
                // stopped returning at all.
                //
                // This is a search heuristic, not a change of semantics: the
                // operations still fold over any length when they appear in a
                // finished program. It only declines to SPECULATE on operands
                // large enough to make speculation cost more than it can return.
                std::size_t elems = 0;
                for (const Value& v : behaviour[i]) elems += v.size();
                if (elems > 64) continue;

                for (const Op hop : {Op::MapF, Op::FoldF}) {
                    if (!spec.allows(hop)) continue;
                    // INSERTION ORDER HERE, deliberately, unlike the Call loop.
                    // A fold invokes its body once per element, so body COST is
                    // what dominates, and older entries were learned from
                    // shallower tiers and are cheaper to run. Ordering these by
                    // utility instead was measured: identical verified counts at
                    // every tier and roughly 3x the time, because it fed the
                    // per-element loop the deepest recipes in the library.
                    for (std::size_t li = 0; li < body_lim && found_at < 0; ++li) {
                        std::vector<Value> outs(ncase);
                        for (std::size_t c = 0; c < ncase; ++c) {
                            outs[c] = apply_op(hop, behaviour[i][c], {},
                                               static_cast<std::uint8_t>(li), lib, 0);
                        }
                        Expr e; e.op = hop; e.a = static_cast<int>(i);
                        e.k = static_cast<std::uint8_t>(li);
                        consider(e, std::move(outs));
                    }
                }
            }
        }

    }

    r.distinct_behaviours = pool.size();
    if (found_at < 0) return r;

    r.recipe.pool = std::move(pool);
    r.recipe.root = static_cast<std::size_t>(found_at);
    r.recipe.found = true;
    // COMPACT BEFORE ANYTHING ELSE TOUCHES IT. Everything downstream -- the case
    // checks just below, verification, the library, emission -- applies this
    // recipe, and until now each application walked the entire search pool.
    r.recipe = r.recipe.compact();

    for (const Case& c : spec.cases)   if (r.recipe.apply_n(c.args(), lib) == c.out) ++r.cases_passed;
    for (const Case& c : spec.holdout) if (r.recipe.apply_n(c.args(), lib) == c.out) ++r.holdout_passed;
    if (r.cases_passed == r.cases_total) {
        r.proof = (r.holdout_total == 0 || r.holdout_passed == r.holdout_total)
                      ? Proof::Generalised : Proof::Tested;
    }
    return r;
}

} // namespace khora::techne
