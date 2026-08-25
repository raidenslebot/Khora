// Emission — a recipe becomes real source in a real language.
//
// Every backend here defines the SAME operation set with the SAME semantics as
// the interpreter in techne.cpp. That is not a convention, it is a correctness
// requirement: emitted code that behaves differently from the code that was
// certified means the certificate is a lie. The value cap, the empty-list
// results, the zero-guard on division and the cycling shorter operand are all
// reproduced in each target rather than approximated.
//
// Emission is in static-single-assignment form: one operation per line, each
// naming its inputs. That is what a person would write, it is what makes the
// output reviewable, and it is the only definition under which "lines of code"
// is a meaningful count rather than a formatting choice.

#include "khora/techne/techne.hpp"

#include <functional>
#include <string>
#include <vector>

namespace khora::techne {
namespace {

std::int64_t const_value(std::uint8_t b) {
    static const std::int64_t k[16] = {0, 1, 2, 3, 4, 5, 6, 7,
                                       8, 9, 10, -1, -2, 100, 1000, 2};
    return k[b % 16];
}

// One call per operation, named identically in every language so the backends
// differ only in syntax and never in meaning.
const char* fn_of(Op op) {
    switch (op) {
        case Op::Add: return "kh_add";      case Op::Sub: return "kh_sub";
        case Op::Mul: return "kh_mul";      case Op::Div: return "kh_div";
        case Op::Mod: return "kh_mod";      case Op::Len: return "kh_len";
        case Op::Head: return "kh_head";    case Op::Tail: return "kh_tail";
        case Op::Rev: return "kh_rev";      case Op::Sort: return "kh_sort";
        case Op::Append: return "kh_cat";   case Op::Take: return "kh_take";
        case Op::Drop: return "kh_drop";    case Op::Index: return "kh_at";
        case Op::Range: return "kh_range";  case Op::Sum: return "kh_sum";
        case Op::Max: return "kh_max";      case Op::Min: return "kh_min";
        case Op::Filter: return "kh_filter";case Op::MapAdd: return "kh_addk";
        case Op::MapMul: return "kh_mulk";  case Op::Count: return "kh_count";
        case Op::Guard: return "kh_guard";  case Op::Else: return "kh_else";
        default: return "kh_id";
    }
}

bool is_binary(Op op) {
    switch (op) {
        case Op::Add: case Op::Sub: case Op::Mul: case Op::Div: case Op::Mod:
        case Op::Append: case Op::Take: case Op::Drop: case Op::Index:
        case Op::Filter: case Op::MapAdd: case Op::MapMul: case Op::Count:
        case Op::Guard: case Op::Else:
            return true;
        default: return false;
    }
}

const char* kPythonPrelude = R"PY(CAP = 10**9
def kh_cap(x): return -CAP if x < -CAP else (CAP if x > CAP else x)
def kh_zip(a, b, f):
    if not a or not b: return []
    return [kh_cap(f(a[i], b[i % len(b)])) for i in range(len(a))]
def kh_id(a): return list(a)
def kh_add(a, b): return kh_zip(a, b, lambda x, y: x + y)
def kh_sub(a, b): return kh_zip(a, b, lambda x, y: x - y)
def kh_mul(a, b): return kh_zip(a, b, lambda x, y: x * y)
def kh_div(a, b): return kh_zip(a, b, lambda x, y: 0 if y == 0 else int(x / y))
def kh_mod(a, b): return kh_zip(a, b, lambda x, y: 0 if y == 0 else x - int(x / y) * y)
def kh_len(a): return [len(a)]
def kh_head(a): return [a[0]] if a else []
def kh_tail(a): return list(a[1:]) if len(a) > 1 else []
def kh_rev(a): return list(reversed(a))
def kh_sort(a): return sorted(a)
def kh_cat(a, b): return list(a) + list(b)
def kh_take(a, b): return list(a[:max(0, b[0])]) if b else []
def kh_drop(a, b): return list(a[max(0, b[0]):]) if b else []
def kh_at(a, b):
    if not b or b[0] < 0 or b[0] >= len(a): return []
    return [a[b[0]]]
def kh_range(a): return list(range(max(0, min(a[0], 512)))) if a else []
def kh_sum(a):
    t = 0
    for x in a: t = kh_cap(t + x)
    return [t]
def kh_max(a): return [max(a)] if a else []
def kh_min(a): return [min(a)] if a else []
def kh_filter(a, b): return [x for x in a if x > b[0]] if b else []
def kh_addk(a, b): return [kh_cap(x + b[0]) for x in a] if b else []
def kh_mulk(a, b): return [kh_cap(x * b[0]) for x in a] if b else []
def kh_count(a, b): return [sum(1 for x in a if x == b[0])] if b else []
def kh_guard(a, b): return list(a) if b else []
def kh_else(a, b): return list(a) if a else list(b)
)PY";

const char* kCppPrelude = R"CPP(#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

using V = std::vector<std::int64_t>;
static constexpr std::int64_t CAP = 1000000000;
static inline std::int64_t kh_cap(std::int64_t x) {
    return x < -CAP ? -CAP : (x > CAP ? CAP : x);
}
template <class F> static V kh_zip(const V& a, const V& b, F f) {
    if (a.empty() || b.empty()) return {};
    V o; o.reserve(a.size());
    for (std::size_t i = 0; i < a.size(); ++i) o.push_back(kh_cap(f(a[i], b[i % b.size()])));
    return o;
}
static V kh_id(const V& a) { return a; }
static V kh_add(const V& a, const V& b) { return kh_zip(a, b, [](auto x, auto y) { return x + y; }); }
static V kh_sub(const V& a, const V& b) { return kh_zip(a, b, [](auto x, auto y) { return x - y; }); }
static V kh_mul(const V& a, const V& b) { return kh_zip(a, b, [](auto x, auto y) { return x * y; }); }
static V kh_div(const V& a, const V& b) { return kh_zip(a, b, [](auto x, auto y) { return y == 0 ? 0 : x / y; }); }
static V kh_mod(const V& a, const V& b) { return kh_zip(a, b, [](auto x, auto y) { return y == 0 ? 0 : x % y; }); }
static V kh_len(const V& a) { return { (std::int64_t)a.size() }; }
static V kh_head(const V& a) { return a.empty() ? V{} : V{ a.front() }; }
static V kh_tail(const V& a) { return a.size() > 1 ? V(a.begin() + 1, a.end()) : V{}; }
static V kh_rev(const V& a) { return V(a.rbegin(), a.rend()); }
static V kh_sort(const V& a) { V o = a; std::sort(o.begin(), o.end()); return o; }
static V kh_cat(const V& a, const V& b) { V o = a; o.insert(o.end(), b.begin(), b.end()); return o; }
static V kh_take(const V& a, const V& b) {
    if (b.empty()) return {};
    std::size_t n = (std::size_t)std::max<std::int64_t>(0, b[0]);
    return V(a.begin(), a.begin() + (std::ptrdiff_t)std::min(a.size(), n));
}
static V kh_drop(const V& a, const V& b) {
    if (b.empty()) return {};
    std::size_t n = std::min(a.size(), (std::size_t)std::max<std::int64_t>(0, b[0]));
    return V(a.begin() + (std::ptrdiff_t)n, a.end());
}
static V kh_at(const V& a, const V& b) {
    if (b.empty() || b[0] < 0 || (std::size_t)b[0] >= a.size()) return {};
    return { a[(std::size_t)b[0]] };
}
static V kh_range(const V& a) {
    if (a.empty()) return {};
    std::int64_t n = std::min<std::int64_t>(std::max<std::int64_t>(0, a[0]), 512);
    V o((std::size_t)n); std::iota(o.begin(), o.end(), (std::int64_t)0); return o;
}
static V kh_sum(const V& a) { std::int64_t t = 0; for (auto x : a) t = kh_cap(t + x); return { t }; }
static V kh_max(const V& a) { return a.empty() ? V{} : V{ *std::max_element(a.begin(), a.end()) }; }
static V kh_min(const V& a) { return a.empty() ? V{} : V{ *std::min_element(a.begin(), a.end()) }; }
static V kh_filter(const V& a, const V& b) { if (b.empty()) return {}; V o; for (auto x : a) if (x > b[0]) o.push_back(x); return o; }
static V kh_addk(const V& a, const V& b) { if (b.empty()) return {}; V o; for (auto x : a) o.push_back(kh_cap(x + b[0])); return o; }
static V kh_mulk(const V& a, const V& b) { if (b.empty()) return {}; V o; for (auto x : a) o.push_back(kh_cap(x * b[0])); return o; }
static V kh_count(const V& a, const V& b) { if (b.empty()) return {}; std::int64_t n = 0; for (auto x : a) if (x == b[0]) ++n; return { n }; }
static V kh_guard(const V& a, const V& b) { return b.empty() ? V{} : a; }
static V kh_else(const V& a, const V& b) { return a.empty() ? b : a; }
)CPP";

const char* kJsPrelude = R"JS(const CAP = 1000000000;
const kh_cap = x => x < -CAP ? -CAP : (x > CAP ? CAP : x);
const kh_zip = (a, b, f) => (!a.length || !b.length) ? []
  : a.map((v, i) => kh_cap(f(v, b[i % b.length])));
const kh_id = a => a.slice();
const kh_add = (a, b) => kh_zip(a, b, (x, y) => x + y);
const kh_sub = (a, b) => kh_zip(a, b, (x, y) => x - y);
const kh_mul = (a, b) => kh_zip(a, b, (x, y) => x * y);
const kh_div = (a, b) => kh_zip(a, b, (x, y) => y === 0 ? 0 : Math.trunc(x / y));
const kh_mod = (a, b) => kh_zip(a, b, (x, y) => y === 0 ? 0 : x % y);
const kh_len = a => [a.length];
const kh_head = a => a.length ? [a[0]] : [];
const kh_tail = a => a.length > 1 ? a.slice(1) : [];
const kh_rev = a => a.slice().reverse();
const kh_sort = a => a.slice().sort((x, y) => x - y);
const kh_cat = (a, b) => a.concat(b);
const kh_take = (a, b) => b.length ? a.slice(0, Math.max(0, b[0])) : [];
const kh_drop = (a, b) => b.length ? a.slice(Math.max(0, b[0])) : [];
const kh_at = (a, b) => (!b.length || b[0] < 0 || b[0] >= a.length) ? [] : [a[b[0]]];
const kh_range = a => a.length ? [...Array(Math.max(0, Math.min(a[0], 512))).keys()] : [];
const kh_sum = a => [a.reduce((t, x) => kh_cap(t + x), 0)];
const kh_max = a => a.length ? [Math.max(...a)] : [];
const kh_min = a => a.length ? [Math.min(...a)] : [];
const kh_filter = (a, b) => b.length ? a.filter(x => x > b[0]) : [];
const kh_addk = (a, b) => b.length ? a.map(x => kh_cap(x + b[0])) : [];
const kh_mulk = (a, b) => b.length ? a.map(x => kh_cap(x * b[0])) : [];
const kh_count = (a, b) => b.length ? [a.filter(x => x === b[0]).length] : [];
const kh_guard = (a, b) => b.length ? a.slice() : [];
const kh_else = (a, b) => a.length ? a.slice() : b.slice();
)JS";

const char* kRustPrelude = R"RS(pub type V = Vec<i64>;
const CAP: i64 = 1_000_000_000;
fn kh_cap(x: i64) -> i64 { if x < -CAP { -CAP } else if x > CAP { CAP } else { x } }
fn kh_zip(a: &V, b: &V, f: impl Fn(i64, i64) -> i64) -> V {
    if a.is_empty() || b.is_empty() { return vec![]; }
    (0..a.len()).map(|i| kh_cap(f(a[i], b[i % b.len()]))).collect()
}
pub fn kh_id(a: &V) -> V { a.clone() }
pub fn kh_add(a: &V, b: &V) -> V { kh_zip(a, b, |x, y| x.saturating_add(y)) }
pub fn kh_sub(a: &V, b: &V) -> V { kh_zip(a, b, |x, y| x.saturating_sub(y)) }
pub fn kh_mul(a: &V, b: &V) -> V { kh_zip(a, b, |x, y| x.saturating_mul(y)) }
pub fn kh_div(a: &V, b: &V) -> V { kh_zip(a, b, |x, y| if y == 0 { 0 } else { x / y }) }
pub fn kh_mod(a: &V, b: &V) -> V { kh_zip(a, b, |x, y| if y == 0 { 0 } else { x % y }) }
pub fn kh_len(a: &V) -> V { vec![a.len() as i64] }
pub fn kh_head(a: &V) -> V { if a.is_empty() { vec![] } else { vec![a[0]] } }
pub fn kh_tail(a: &V) -> V { if a.len() > 1 { a[1..].to_vec() } else { vec![] } }
pub fn kh_rev(a: &V) -> V { let mut o = a.clone(); o.reverse(); o }
pub fn kh_sort(a: &V) -> V { let mut o = a.clone(); o.sort(); o }
pub fn kh_cat(a: &V, b: &V) -> V { let mut o = a.clone(); o.extend(b.iter()); o }
pub fn kh_take(a: &V, b: &V) -> V {
    if b.is_empty() { return vec![]; }
    let n = (b[0].max(0) as usize).min(a.len()); a[..n].to_vec()
}
pub fn kh_drop(a: &V, b: &V) -> V {
    if b.is_empty() { return vec![]; }
    let n = (b[0].max(0) as usize).min(a.len()); a[n..].to_vec()
}
pub fn kh_at(a: &V, b: &V) -> V {
    if b.is_empty() || b[0] < 0 || b[0] as usize >= a.len() { return vec![]; }
    vec![a[b[0] as usize]]
}
pub fn kh_range(a: &V) -> V {
    if a.is_empty() { return vec![]; }
    (0..a[0].max(0).min(512)).collect()
}
pub fn kh_sum(a: &V) -> V { let mut t = 0i64; for x in a { t = kh_cap(t + x); } vec![t] }
pub fn kh_max(a: &V) -> V { a.iter().max().map_or(vec![], |m| vec![*m]) }
pub fn kh_min(a: &V) -> V { a.iter().min().map_or(vec![], |m| vec![*m]) }
pub fn kh_filter(a: &V, b: &V) -> V {
    if b.is_empty() { return vec![]; }
    a.iter().cloned().filter(|x| *x > b[0]).collect()
}
pub fn kh_addk(a: &V, b: &V) -> V {
    if b.is_empty() { return vec![]; }
    a.iter().map(|x| kh_cap(x + b[0])).collect()
}
pub fn kh_mulk(a: &V, b: &V) -> V {
    if b.is_empty() { return vec![]; }
    a.iter().map(|x| kh_cap(x * b[0])).collect()
}
pub fn kh_count(a: &V, b: &V) -> V {
    if b.is_empty() { return vec![]; }
    vec![a.iter().filter(|x| **x == b[0]).count() as i64]
}
pub fn kh_guard(a: &V, b: &V) -> V { if b.is_empty() { vec![] } else { a.clone() } }
pub fn kh_else(a: &V, b: &V) -> V { if a.is_empty() { b.clone() } else { a.clone() } }
)RS";

} // namespace

const char* lang_name(Lang l) {
    switch (l) {
        case Lang::Cpp: return "C++";
        case Lang::Python: return "Python";
        case Lang::JavaScript: return "JavaScript";
        case Lang::Rust: return "Rust";
    }
    return "?";
}

const char* lang_ext(Lang l) {
    switch (l) {
        case Lang::Cpp: return "cpp";
        case Lang::Python: return "py";
        case Lang::JavaScript: return "js";
        case Lang::Rust: return "rs";
    }
    return "txt";
}

std::string prelude(Lang l) {
    switch (l) {
        case Lang::Cpp: return kCppPrelude;
        case Lang::Python: return kPythonPrelude;
        case Lang::JavaScript: return kJsPrelude;
        case Lang::Rust: return kRustPrelude;
    }
    return {};
}

std::string emit(const Recipe& r, Lang l, const std::string& fn, std::size_t* lines) {
    if (lines) *lines = 0;
    if (!r.found) return {};

    // Only the nodes the root actually reaches. Emitting the whole pool would
    // pad the output with dead code, and a line count inflated by dead code is
    // the sort of number that makes a throughput claim meaningless.
    std::vector<bool> live(r.pool.size(), false);
    {
        std::vector<std::size_t> stack{r.root};
        while (!stack.empty()) {
            const std::size_t i = stack.back();
            stack.pop_back();
            if (i >= r.pool.size() || live[i]) continue;
            live[i] = true;
            if (r.pool[i].a >= 0) stack.push_back(static_cast<std::size_t>(r.pool[i].a));
            if (r.pool[i].b >= 0) stack.push_back(static_cast<std::size_t>(r.pool[i].b));
        }
    }

    // AN IDENTITY NODE IS NOT A LINE.
    //
    // The level-0 pool entry is Mov(input), so almost every emitted function
    // opened with `t0 = kh_id(x)` and then used t0 everywhere. That is a copy of
    // the argument, it does nothing, and counting it inflated the throughput
    // figure by one line per function -- against my own rule that dead code must
    // not pad the count. References to an identity node now resolve straight to
    // the argument and the node is not emitted at all. This makes the reported
    // rate LOWER, which is the direction integrity moves in.
    std::vector<int> alias(r.pool.size(), -2);        // -2 = not an alias
    for (std::size_t i = 0; i < r.pool.size(); ++i) {
        if (r.pool[i].op == Op::Mov || r.pool[i].op == Op::Call) alias[i] = r.pool[i].a;
    }

    std::string body;
    std::size_t n = 0;
    std::function<std::string(int)> ref = [&](int i) -> std::string {
        if (i < 0) return "x";
        const std::size_t u = static_cast<std::size_t>(i);
        if (u < alias.size() && alias[u] != -2) return ref(alias[u]);
        return "t" + std::to_string(u);
    };
    auto line = [&](const std::string& text) { body += text; body += '\n'; ++n; };

    for (std::size_t i = 0; i < r.pool.size(); ++i) {
        if (!live[i]) continue;
        if (alias[i] != -2) continue;                 // identity: nothing to emit
        const Expr& e = r.pool[i];
        const std::string lhs = "t" + std::to_string(i);
        std::string rhs;
        if (e.op == Op::Const) {
            const std::string k = std::to_string(const_value(e.k));
            switch (l) {
                case Lang::Cpp:  rhs = "V{" + k + "}"; break;
                case Lang::Rust: rhs = "vec![" + k + "]"; break;
                default:         rhs = "[" + k + "]"; break;
            }
        } else if (is_binary(e.op)) {
            rhs = (l == Lang::Rust)
                ? std::string(fn_of(e.op)) + "(&" + ref(e.a) + ", &" + ref(e.b) + ")"
                : std::string(fn_of(e.op)) + "(" + ref(e.a) + ", " + ref(e.b) + ")";
        } else {
            rhs = (l == Lang::Rust)
                ? std::string(fn_of(e.op)) + "(&" + ref(e.a) + ")"
                : std::string(fn_of(e.op)) + "(" + ref(e.a) + ")";
        }
        switch (l) {
            case Lang::Cpp:        line("    const V " + lhs + " = " + rhs + ";"); break;
            case Lang::Rust:       line("    let " + lhs + ": V = " + rhs + ";"); break;
            case Lang::JavaScript: line("  const " + lhs + " = " + rhs + ";"); break;
            case Lang::Python:     line("    " + lhs + " = " + rhs); break;
        }
    }

    std::string out;
    switch (l) {
        case Lang::Cpp:
            out = "V " + fn + "(const V& x) {\n" + body + "    return t" +
                  std::to_string(r.root) + ";\n}\n";
            n += 2;
            break;
        case Lang::Rust:
            out = "pub fn " + fn + "(x: &V) -> V {\n" + body + "    t" +
                  std::to_string(r.root) + "\n}\n";
            n += 2;
            break;
        case Lang::JavaScript:
            out = "function " + fn + "(x) {\n" + body + "  return t" +
                  std::to_string(r.root) + ";\n}\n";
            n += 2;
            break;
        case Lang::Python:
            out = "def " + fn + "(x):\n" + body + "    return t" +
                  std::to_string(r.root) + "\n";
            n += 2;
            break;
    }
    if (lines) *lines = n;
    return out;
}

} // namespace khora::techne
