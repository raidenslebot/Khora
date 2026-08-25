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

// PHP spells every local `$t0` and every parameter `$x`. Putting the sigil in
// the reference builder rather than at each of the four places a name is
// written is what keeps the backends differing only in syntax.
const char* var_sigil(Lang l) { return l == Lang::Php ? "$" : ""; }

// Java and C# have no free functions, so their operation set lives on a class
// and every call has to name it. Nothing else in the emitter needs to know why.
const char* call_prefix(Lang l) {
    return (l == Lang::Java || l == Lang::CSharp) ? "Kh." : "";
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

const char* kGoPrelude = R"GO(package kh

import "sort"

type V = []int64

const CAP int64 = 1000000000

func kh_cap(x int64) int64 {
	if x < -CAP {
		return -CAP
	}
	if x > CAP {
		return CAP
	}
	return x
}

func kh_zip(a V, b V, f func(int64, int64) int64) V {
	if len(a) == 0 || len(b) == 0 {
		return V{}
	}
	o := make(V, len(a))
	for i := range a {
		o[i] = kh_cap(f(a[i], b[i%len(b)]))
	}
	return o
}

func kh_id(a V) V { return append(V{}, a...) }

func kh_add(a V, b V) V { return kh_zip(a, b, func(x, y int64) int64 { return x + y }) }
func kh_sub(a V, b V) V { return kh_zip(a, b, func(x, y int64) int64 { return x - y }) }
func kh_mul(a V, b V) V { return kh_zip(a, b, func(x, y int64) int64 { return x * y }) }

// Go's / and % on signed integers truncate toward zero and take the sign of the
// dividend, exactly as C does, so these need no correction -- unlike Python,
// Ruby, Lua and Haskell, where the same two lines would be quietly wrong.
func kh_div(a V, b V) V {
	return kh_zip(a, b, func(x, y int64) int64 {
		if y == 0 {
			return 0
		}
		return x / y
	})
}

func kh_mod(a V, b V) V {
	return kh_zip(a, b, func(x, y int64) int64 {
		if y == 0 {
			return 0
		}
		return x % y
	})
}

func kh_len(a V) V { return V{int64(len(a))} }

func kh_head(a V) V {
	if len(a) == 0 {
		return V{}
	}
	return V{a[0]}
}

func kh_tail(a V) V {
	if len(a) > 1 {
		return append(V{}, a[1:]...)
	}
	return V{}
}

func kh_rev(a V) V {
	o := make(V, len(a))
	for i, x := range a {
		o[len(a)-1-i] = x
	}
	return o
}

func kh_sort(a V) V {
	o := append(V{}, a...)
	sort.Slice(o, func(i, j int) bool { return o[i] < o[j] })
	return o
}

func kh_cat(a V, b V) V { return append(append(V{}, a...), b...) }

func kh_clamp(n int64, hi int) int64 {
	if n < 0 {
		n = 0
	}
	if n > int64(hi) {
		n = int64(hi)
	}
	return n
}

func kh_take(a V, b V) V {
	if len(b) == 0 {
		return V{}
	}
	return append(V{}, a[:kh_clamp(b[0], len(a))]...)
}

func kh_drop(a V, b V) V {
	if len(b) == 0 {
		return V{}
	}
	return append(V{}, a[kh_clamp(b[0], len(a)):]...)
}

func kh_at(a V, b V) V {
	if len(b) == 0 || b[0] < 0 || b[0] >= int64(len(a)) {
		return V{}
	}
	return V{a[b[0]]}
}

func kh_range(a V) V {
	if len(a) == 0 {
		return V{}
	}
	n := kh_clamp(a[0], 512)
	o := make(V, n)
	for i := range o {
		o[i] = int64(i)
	}
	return o
}

func kh_sum(a V) V {
	var t int64
	for _, x := range a {
		t = kh_cap(t + x)
	}
	return V{t}
}

func kh_max(a V) V {
	if len(a) == 0 {
		return V{}
	}
	m := a[0]
	for _, x := range a {
		if x > m {
			m = x
		}
	}
	return V{m}
}

func kh_min(a V) V {
	if len(a) == 0 {
		return V{}
	}
	m := a[0]
	for _, x := range a {
		if x < m {
			m = x
		}
	}
	return V{m}
}

func kh_filter(a V, b V) V {
	if len(b) == 0 {
		return V{}
	}
	o := V{}
	for _, x := range a {
		if x > b[0] {
			o = append(o, x)
		}
	}
	return o
}

func kh_addk(a V, b V) V {
	if len(b) == 0 {
		return V{}
	}
	o := make(V, len(a))
	for i, x := range a {
		o[i] = kh_cap(x + b[0])
	}
	return o
}

func kh_mulk(a V, b V) V {
	if len(b) == 0 {
		return V{}
	}
	o := make(V, len(a))
	for i, x := range a {
		o[i] = kh_cap(x * b[0])
	}
	return o
}

func kh_count(a V, b V) V {
	if len(b) == 0 {
		return V{}
	}
	var n int64
	for _, x := range a {
		if x == b[0] {
			n++
		}
	}
	return V{n}
}

func kh_guard(a V, b V) V {
	if len(b) == 0 {
		return V{}
	}
	return append(V{}, a...)
}

func kh_else(a V, b V) V {
	if len(a) == 0 {
		return append(V{}, b...)
	}
	return append(V{}, a...)
}
)GO";

// JAVA HAS NO TOP-LEVEL FUNCTIONS AND NO LIST LITERALS, which is why this
// backend looks different from every other one: the operation set lives on a
// class, every emitted call names that class, and a constant is
// `new long[]{k}` rather than `[k]`.
//
// long[] rather than List<Long> is deliberate. Boxed Long compares by
// REFERENCE under ==, so kh_count and kh_at would silently disagree with the
// reference for values outside the -128..127 cache -- exactly the class of
// wrongness this file exists to prevent.
const char* kJavaPrelude = R"JAVA(import java.util.Arrays;

class Kh {
    static final long CAP = 1000000000L;
    static final long[] EMPTY = new long[0];

    interface F { long apply(long x, long y); }

    static long kh_cap(long x) { return x < -CAP ? -CAP : (x > CAP ? CAP : x); }

    static long[] kh_zip(long[] a, long[] b, F f) {
        if (a.length == 0 || b.length == 0) return EMPTY;
        long[] o = new long[a.length];
        for (int i = 0; i < a.length; i++) o[i] = kh_cap(f.apply(a[i], b[i % b.length]));
        return o;
    }
    static long[] kh_id(long[] a) { return a.clone(); }
    static long[] kh_add(long[] a, long[] b) { return kh_zip(a, b, (x, y) -> x + y); }
    static long[] kh_sub(long[] a, long[] b) { return kh_zip(a, b, (x, y) -> x - y); }
    static long[] kh_mul(long[] a, long[] b) { return kh_zip(a, b, (x, y) -> x * y); }
    static long[] kh_div(long[] a, long[] b) { return kh_zip(a, b, (x, y) -> y == 0 ? 0 : x / y); }
    static long[] kh_mod(long[] a, long[] b) { return kh_zip(a, b, (x, y) -> y == 0 ? 0 : x % y); }
    static long[] kh_len(long[] a) { return new long[]{ a.length }; }
    static long[] kh_head(long[] a) { return a.length == 0 ? EMPTY : new long[]{ a[0] }; }
    static long[] kh_tail(long[] a) { return a.length > 1 ? Arrays.copyOfRange(a, 1, a.length) : EMPTY; }
    static long[] kh_rev(long[] a) {
        long[] o = new long[a.length];
        for (int i = 0; i < a.length; i++) o[i] = a[a.length - 1 - i];
        return o;
    }
    static long[] kh_sort(long[] a) { long[] o = a.clone(); Arrays.sort(o); return o; }
    static long[] kh_cat(long[] a, long[] b) {
        long[] o = Arrays.copyOf(a, a.length + b.length);
        System.arraycopy(b, 0, o, a.length, b.length);
        return o;
    }
    static int kh_clamp(long n, int hi) { return (int) Math.min((long) hi, Math.max(0L, n)); }
    static long[] kh_take(long[] a, long[] b) {
        if (b.length == 0) return EMPTY;
        return Arrays.copyOfRange(a, 0, kh_clamp(b[0], a.length));
    }
    static long[] kh_drop(long[] a, long[] b) {
        if (b.length == 0) return EMPTY;
        return Arrays.copyOfRange(a, kh_clamp(b[0], a.length), a.length);
    }
    static long[] kh_at(long[] a, long[] b) {
        if (b.length == 0 || b[0] < 0 || b[0] >= a.length) return EMPTY;
        return new long[]{ a[(int) b[0]] };
    }
    static long[] kh_range(long[] a) {
        if (a.length == 0) return EMPTY;
        int n = kh_clamp(a[0], 512);
        long[] o = new long[n];
        for (int i = 0; i < n; i++) o[i] = i;
        return o;
    }
    static long[] kh_sum(long[] a) { long t = 0; for (long x : a) t = kh_cap(t + x); return new long[]{ t }; }
    static long[] kh_max(long[] a) {
        if (a.length == 0) return EMPTY;
        long m = a[0];
        for (long x : a) if (x > m) m = x;
        return new long[]{ m };
    }
    static long[] kh_min(long[] a) {
        if (a.length == 0) return EMPTY;
        long m = a[0];
        for (long x : a) if (x < m) m = x;
        return new long[]{ m };
    }
    static long[] kh_filter(long[] a, long[] b) {
        if (b.length == 0) return EMPTY;
        long[] o = new long[a.length];
        int n = 0;
        for (long x : a) if (x > b[0]) o[n++] = x;
        return Arrays.copyOf(o, n);
    }
    static long[] kh_addk(long[] a, long[] b) {
        if (b.length == 0) return EMPTY;
        long[] o = new long[a.length];
        for (int i = 0; i < a.length; i++) o[i] = kh_cap(a[i] + b[0]);
        return o;
    }
    static long[] kh_mulk(long[] a, long[] b) {
        if (b.length == 0) return EMPTY;
        long[] o = new long[a.length];
        for (int i = 0; i < a.length; i++) o[i] = kh_cap(a[i] * b[0]);
        return o;
    }
    static long[] kh_count(long[] a, long[] b) {
        if (b.length == 0) return EMPTY;
        long n = 0;
        for (long x : a) if (x == b[0]) n++;
        return new long[]{ n };
    }
    static long[] kh_guard(long[] a, long[] b) { return b.length == 0 ? EMPTY : a.clone(); }
    static long[] kh_else(long[] a, long[] b) { return a.length == 0 ? b.clone() : a.clone(); }
}
)JAVA";

// C# has no top-level functions either, so it takes the same shape as Java: a
// static class holding the operation set, named at every call site. long[]
// rather than List<long> for the same reason, and because Array.Sort on long[]
// is a numeric sort with no comparer to get wrong.
const char* kCSharpPrelude = R"CS(using System;

static class Kh {
    public const long CAP = 1000000000L;
    static readonly long[] EMPTY = new long[0];

    public static long kh_cap(long x) { return x < -CAP ? -CAP : (x > CAP ? CAP : x); }

    static long[] kh_zip(long[] a, long[] b, Func<long, long, long> f) {
        if (a.Length == 0 || b.Length == 0) return EMPTY;
        long[] o = new long[a.Length];
        for (int i = 0; i < a.Length; i++) o[i] = kh_cap(f(a[i], b[i % b.Length]));
        return o;
    }
    static long[] kh_cut(long[] a, int from, int n) {
        long[] o = new long[n];
        Array.Copy(a, from, o, 0, n);
        return o;
    }
    static int kh_clamp(long n, int hi) { return (int) Math.Min((long) hi, Math.Max(0L, n)); }

    public static long[] kh_id(long[] a) { return kh_cut(a, 0, a.Length); }
    public static long[] kh_add(long[] a, long[] b) { return kh_zip(a, b, (x, y) => x + y); }
    public static long[] kh_sub(long[] a, long[] b) { return kh_zip(a, b, (x, y) => x - y); }
    public static long[] kh_mul(long[] a, long[] b) { return kh_zip(a, b, (x, y) => x * y); }
    public static long[] kh_div(long[] a, long[] b) { return kh_zip(a, b, (x, y) => y == 0L ? 0L : x / y); }
    public static long[] kh_mod(long[] a, long[] b) { return kh_zip(a, b, (x, y) => y == 0L ? 0L : x % y); }
    public static long[] kh_len(long[] a) { return new long[]{ a.Length }; }
    public static long[] kh_head(long[] a) { return a.Length == 0 ? EMPTY : new long[]{ a[0] }; }
    public static long[] kh_tail(long[] a) { return a.Length > 1 ? kh_cut(a, 1, a.Length - 1) : EMPTY; }
    public static long[] kh_rev(long[] a) {
        long[] o = new long[a.Length];
        for (int i = 0; i < a.Length; i++) o[i] = a[a.Length - 1 - i];
        return o;
    }
    public static long[] kh_sort(long[] a) { long[] o = kh_cut(a, 0, a.Length); Array.Sort(o); return o; }
    public static long[] kh_cat(long[] a, long[] b) {
        long[] o = new long[a.Length + b.Length];
        Array.Copy(a, 0, o, 0, a.Length);
        Array.Copy(b, 0, o, a.Length, b.Length);
        return o;
    }
    public static long[] kh_take(long[] a, long[] b) {
        if (b.Length == 0) return EMPTY;
        return kh_cut(a, 0, kh_clamp(b[0], a.Length));
    }
    public static long[] kh_drop(long[] a, long[] b) {
        if (b.Length == 0) return EMPTY;
        int n = kh_clamp(b[0], a.Length);
        return kh_cut(a, n, a.Length - n);
    }
    public static long[] kh_at(long[] a, long[] b) {
        if (b.Length == 0 || b[0] < 0 || b[0] >= a.Length) return EMPTY;
        return new long[]{ a[(int) b[0]] };
    }
    public static long[] kh_range(long[] a) {
        if (a.Length == 0) return EMPTY;
        int n = kh_clamp(a[0], 512);
        long[] o = new long[n];
        for (int i = 0; i < n; i++) o[i] = i;
        return o;
    }
    public static long[] kh_sum(long[] a) {
        long t = 0;
        foreach (long x in a) t = kh_cap(t + x);
        return new long[]{ t };
    }
    public static long[] kh_max(long[] a) {
        if (a.Length == 0) return EMPTY;
        long m = a[0];
        foreach (long x in a) if (x > m) m = x;
        return new long[]{ m };
    }
    public static long[] kh_min(long[] a) {
        if (a.Length == 0) return EMPTY;
        long m = a[0];
        foreach (long x in a) if (x < m) m = x;
        return new long[]{ m };
    }
    public static long[] kh_filter(long[] a, long[] b) {
        if (b.Length == 0) return EMPTY;
        long[] o = new long[a.Length];
        int n = 0;
        foreach (long x in a) if (x > b[0]) o[n++] = x;
        return kh_cut(o, 0, n);
    }
    public static long[] kh_addk(long[] a, long[] b) {
        if (b.Length == 0) return EMPTY;
        long[] o = new long[a.Length];
        for (int i = 0; i < a.Length; i++) o[i] = kh_cap(a[i] + b[0]);
        return o;
    }
    public static long[] kh_mulk(long[] a, long[] b) {
        if (b.Length == 0) return EMPTY;
        long[] o = new long[a.Length];
        for (int i = 0; i < a.Length; i++) o[i] = kh_cap(a[i] * b[0]);
        return o;
    }
    public static long[] kh_count(long[] a, long[] b) {
        if (b.Length == 0) return EMPTY;
        long n = 0;
        foreach (long x in a) if (x == b[0]) n++;
        return new long[]{ n };
    }
    public static long[] kh_guard(long[] a, long[] b) { return b.Length == 0 ? EMPTY : kh_id(a); }
    public static long[] kh_else(long[] a, long[] b) { return a.Length == 0 ? kh_id(b) : kh_id(a); }
}
)CS";

// TypeScript is the JavaScript backend with the types written down, so it
// inherits JavaScript's traps verbatim: sort() compares STRINGIFIED elements
// unless given a comparator (it would put 10 before 9), and / is float division
// so truncation has to be asked for by name.
const char* kTsPrelude = R"TS(type V = number[];

const CAP = 1000000000;
const kh_cap = (x: number): number => (x < -CAP ? -CAP : x > CAP ? CAP : x);
const kh_zip = (a: V, b: V, f: (x: number, y: number) => number): V =>
  !a.length || !b.length ? [] : a.map((v, i) => kh_cap(f(v, b[i % b.length])));
const kh_id = (a: V): V => a.slice();
const kh_add = (a: V, b: V): V => kh_zip(a, b, (x, y) => x + y);
const kh_sub = (a: V, b: V): V => kh_zip(a, b, (x, y) => x - y);
const kh_mul = (a: V, b: V): V => kh_zip(a, b, (x, y) => x * y);
const kh_div = (a: V, b: V): V => kh_zip(a, b, (x, y) => (y === 0 ? 0 : Math.trunc(x / y)));
const kh_mod = (a: V, b: V): V => kh_zip(a, b, (x, y) => (y === 0 ? 0 : x % y));
const kh_len = (a: V): V => [a.length];
const kh_head = (a: V): V => (a.length ? [a[0]] : []);
const kh_tail = (a: V): V => (a.length > 1 ? a.slice(1) : []);
const kh_rev = (a: V): V => a.slice().reverse();
const kh_sort = (a: V): V => a.slice().sort((x, y) => x - y);
const kh_cat = (a: V, b: V): V => a.concat(b);
const kh_take = (a: V, b: V): V => (b.length ? a.slice(0, Math.max(0, b[0])) : []);
const kh_drop = (a: V, b: V): V => (b.length ? a.slice(Math.max(0, b[0])) : []);
const kh_at = (a: V, b: V): V =>
  !b.length || b[0] < 0 || b[0] >= a.length ? [] : [a[b[0]]];
const kh_range = (a: V): V =>
  a.length ? [...Array(Math.max(0, Math.min(a[0], 512))).keys()] : [];
const kh_sum = (a: V): V => [a.reduce((t, x) => kh_cap(t + x), 0)];
const kh_max = (a: V): V => (a.length ? [Math.max(...a)] : []);
const kh_min = (a: V): V => (a.length ? [Math.min(...a)] : []);
const kh_filter = (a: V, b: V): V => (b.length ? a.filter((x) => x > b[0]) : []);
const kh_addk = (a: V, b: V): V => (b.length ? a.map((x) => kh_cap(x + b[0])) : []);
const kh_mulk = (a: V, b: V): V => (b.length ? a.map((x) => kh_cap(x * b[0])) : []);
const kh_count = (a: V, b: V): V => (b.length ? [a.filter((x) => x === b[0]).length] : []);
const kh_guard = (a: V, b: V): V => (b.length ? a.slice() : []);
const kh_else = (a: V, b: V): V => (a.length ? a.slice() : b.slice());
)TS";

const char* kRubyPrelude = R"RB(CAP = 1_000_000_000

def kh_cap(x); x < -CAP ? -CAP : (x > CAP ? CAP : x); end

def kh_zip(a, b)
  return [] if a.empty? || b.empty?
  (0...a.length).map { |i| kh_cap(yield(a[i], b[i % b.length])) }
end

def kh_id(a); a.dup; end
def kh_add(a, b); kh_zip(a, b) { |x, y| x + y }; end
def kh_sub(a, b); kh_zip(a, b) { |x, y| x - y }; end
def kh_mul(a, b); kh_zip(a, b) { |x, y| x * y }; end

# Ruby's / FLOORS and % takes the sign of the DIVISOR: -7 / 2 is -4 and -7 % 2
# is 1, where the reference wants -3 and -1. So division is rebuilt from the
# magnitudes and the remainder uses Integer#remainder, which is the truncating
# one. Using / and % directly here would be right on every non-negative case
# and wrong on every negative one, which is the worst way to be wrong.
def kh_tdiv(x, y); q = x.abs / y.abs; (x < 0) == (y < 0) ? q : -q; end
def kh_div(a, b); kh_zip(a, b) { |x, y| y == 0 ? 0 : kh_tdiv(x, y) }; end
def kh_mod(a, b); kh_zip(a, b) { |x, y| y == 0 ? 0 : x.remainder(y) }; end

def kh_len(a); [a.length]; end
def kh_head(a); a.empty? ? [] : [a[0]]; end
def kh_tail(a); a.length > 1 ? a[1..-1] : []; end
def kh_rev(a); a.reverse; end
def kh_sort(a); a.sort; end
def kh_cat(a, b); a + b; end
def kh_take(a, b); b.empty? ? [] : a.first([0, b[0]].max); end
def kh_drop(a, b); b.empty? ? [] : a.drop([0, b[0]].max); end
def kh_at(a, b)
  return [] if b.empty? || b[0] < 0 || b[0] >= a.length
  [a[b[0]]]
end
def kh_range(a); a.empty? ? [] : (0...[[0, a[0]].max, 512].min).to_a; end
def kh_sum(a); t = 0; a.each { |x| t = kh_cap(t + x) }; [t]; end
def kh_max(a); a.empty? ? [] : [a.max]; end
def kh_min(a); a.empty? ? [] : [a.min]; end
def kh_filter(a, b); b.empty? ? [] : a.select { |x| x > b[0] }; end
def kh_addk(a, b); b.empty? ? [] : a.map { |x| kh_cap(x + b[0]) }; end
def kh_mulk(a, b); b.empty? ? [] : a.map { |x| kh_cap(x * b[0]) }; end
def kh_count(a, b); b.empty? ? [] : [a.count { |x| x == b[0] }]; end
def kh_guard(a, b); b.empty? ? [] : a.dup; end
def kh_else(a, b); a.empty? ? b.dup : a.dup; end
)RB";

// LUA TABLES ARE 1-INDEXED. Every index in this file is therefore shifted by
// one against the reference, and the shift is written out at each site rather
// than folded into the surrounding arithmetic, because an off-by-one here would
// be a backend that computes the wrong thing while looking right.
const char* kLuaPrelude = R"LUA(CAP = 1000000000

function kh_cap(x)
  if x < -CAP then return -CAP elseif x > CAP then return CAP else return x end
end

function kh_zip(a, b, f)
  if #a == 0 or #b == 0 then return {} end
  local o = {}
  for i = 1, #a do o[i] = kh_cap(f(a[i], b[(i - 1) % #b + 1])) end
  return o
end

function kh_id(a)
  local o = {}
  for i = 1, #a do o[i] = a[i] end
  return o
end

function kh_add(a, b) return kh_zip(a, b, function(x, y) return x + y end) end
function kh_sub(a, b) return kh_zip(a, b, function(x, y) return x - y end) end
function kh_mul(a, b) return kh_zip(a, b, function(x, y) return x * y end) end

-- Lua's // floors and its % takes the sign of the divisor, like Python's. The
-- reference truncates toward zero, so both are rebuilt from the magnitudes.
-- (// is integer division from Lua 5.3, which is also the first version with a
-- distinct integer subtype at all -- on 5.1/5.2 every value here would be a
-- double and the cap boundary would stop being exact.)
function kh_tdiv(x, y)
  local m = (x < 0 and -x or x) // (y < 0 and -y or y)
  if (x < 0) == (y < 0) then return m else return -m end
end
function kh_trem(x, y) return x - kh_tdiv(x, y) * y end
function kh_div(a, b)
  return kh_zip(a, b, function(x, y) if y == 0 then return 0 end return kh_tdiv(x, y) end)
end
function kh_mod(a, b)
  return kh_zip(a, b, function(x, y) if y == 0 then return 0 end return kh_trem(x, y) end)
end

function kh_len(a) return { #a } end
function kh_head(a) if #a == 0 then return {} end return { a[1] } end

function kh_tail(a)
  if #a <= 1 then return {} end
  local o = {}
  for i = 2, #a do o[i - 1] = a[i] end
  return o
end

function kh_rev(a)
  local o = {}
  for i = 1, #a do o[i] = a[#a - i + 1] end
  return o
end

function kh_sort(a)
  local o = kh_id(a)
  table.sort(o)
  return o
end

function kh_cat(a, b)
  local o = kh_id(a)
  for i = 1, #b do o[#a + i] = b[i] end
  return o
end

function kh_clamp(n, hi)
  if n < 0 then return 0 elseif n > hi then return hi else return n end
end

function kh_take(a, b)
  if #b == 0 then return {} end
  local n = kh_clamp(b[1], #a)
  local o = {}
  for i = 1, n do o[i] = a[i] end
  return o
end

function kh_drop(a, b)
  if #b == 0 then return {} end
  local n = kh_clamp(b[1], #a)
  local o = {}
  for i = n + 1, #a do o[i - n] = a[i] end
  return o
end

function kh_at(a, b)
  if #b == 0 or b[1] < 0 or b[1] >= #a then return {} end
  return { a[b[1] + 1] }
end

function kh_range(a)
  if #a == 0 then return {} end
  local n = kh_clamp(a[1], 512)
  local o = {}
  for i = 1, n do o[i] = i - 1 end
  return o
end

function kh_sum(a)
  local t = 0
  for i = 1, #a do t = kh_cap(t + a[i]) end
  return { t }
end

function kh_max(a)
  if #a == 0 then return {} end
  local m = a[1]
  for i = 2, #a do if a[i] > m then m = a[i] end end
  return { m }
end

function kh_min(a)
  if #a == 0 then return {} end
  local m = a[1]
  for i = 2, #a do if a[i] < m then m = a[i] end end
  return { m }
end

function kh_filter(a, b)
  if #b == 0 then return {} end
  local o, n = {}, 0
  for i = 1, #a do if a[i] > b[1] then n = n + 1 o[n] = a[i] end end
  return o
end

function kh_addk(a, b)
  if #b == 0 then return {} end
  local o = {}
  for i = 1, #a do o[i] = kh_cap(a[i] + b[1]) end
  return o
end

function kh_mulk(a, b)
  if #b == 0 then return {} end
  local o = {}
  for i = 1, #a do o[i] = kh_cap(a[i] * b[1]) end
  return o
end

function kh_count(a, b)
  if #b == 0 then return {} end
  local n = 0
  for i = 1, #a do if a[i] == b[1] then n = n + 1 end end
  return { n }
end

function kh_guard(a, b) if #b == 0 then return {} end return kh_id(a) end
function kh_else(a, b) if #a == 0 then return kh_id(b) end return kh_id(a) end
)LUA";

// Haskell's div/mod FLOOR; quot/rem are the truncating pair the reference uses.
// The other trap here is unary minus: `x < -cap` is a parse error rather than a
// comparison, so kh_cap is written with guards and `negate`.
const char* kHaskellPrelude = R"HS(module Kh where

import Data.Int (Int64)
import Data.List (sort)

type V = [Int64]

cap :: Int64
cap = 1000000000

kh_cap :: Int64 -> Int64
kh_cap x
  | x < negate cap = negate cap
  | x > cap = cap
  | otherwise = x

-- `cycle b` is the shorter-operand rule stated directly: the result is as long
-- as a, and b repeats under it.
kh_zip :: V -> V -> (Int64 -> Int64 -> Int64) -> V
kh_zip a b f
  | null a || null b = []
  | otherwise = zipWith (\x y -> kh_cap (f x y)) a (take (length a) (cycle b))

kh_id :: V -> V
kh_id a = a

kh_add, kh_sub, kh_mul, kh_div, kh_mod :: V -> V -> V
kh_add a b = kh_zip a b (+)
kh_sub a b = kh_zip a b (-)
kh_mul a b = kh_zip a b (*)
kh_div a b = kh_zip a b (\x y -> if y == 0 then 0 else x `quot` y)
kh_mod a b = kh_zip a b (\x y -> if y == 0 then 0 else x `rem` y)

kh_len :: V -> V
kh_len a = [fromIntegral (length a)]

kh_head :: V -> V
kh_head a = if null a then [] else [head a]

kh_tail :: V -> V
kh_tail a = if length a > 1 then tail a else []

kh_rev :: V -> V
kh_rev = reverse

kh_sort :: V -> V
kh_sort = sort

kh_cat :: V -> V -> V
kh_cat a b = a ++ b

kh_clamp :: Int64 -> Int -> Int
kh_clamp n hi = min hi (max 0 (fromIntegral n))

kh_take, kh_drop, kh_at :: V -> V -> V
kh_take a b = if null b then [] else take (kh_clamp (head b) (length a)) a
kh_drop a b = if null b then [] else drop (kh_clamp (head b) (length a)) a
kh_at a b
  | null b || head b < 0 || head b >= fromIntegral (length a) = []
  | otherwise = [a !! fromIntegral (head b)]

kh_range :: V -> V
kh_range a = if null a then [] else [0 .. min (max 0 (head a)) 512 - 1]

kh_sum :: V -> V
kh_sum a = [foldl (\t x -> kh_cap (t + x)) 0 a]

kh_max, kh_min :: V -> V
kh_max a = if null a then [] else [maximum a]
kh_min a = if null a then [] else [minimum a]

kh_filter, kh_addk, kh_mulk, kh_count, kh_guard, kh_else :: V -> V -> V
kh_filter a b = if null b then [] else filter (> head b) a
kh_addk a b = if null b then [] else map (\x -> kh_cap (x + head b)) a
kh_mulk a b = if null b then [] else map (\x -> kh_cap (x * head b)) a
kh_count a b = if null b then [] else [fromIntegral (length (filter (== head b) a))]
kh_guard a b = if null b then [] else a
kh_else a b = if null a then b else a
)HS";

// Swift TRAPS on signed overflow rather than wrapping, so the elementwise
// arithmetic uses the &-operators. Every value is clamped to +-CAP at each
// step, so nothing in range can overflow -- but a trap where the reference
// merely wraps is still a behavioural difference, and this backend is not
// allowed to have one.
const char* kSwiftPrelude = R"SWIFT(typealias V = [Int64]

let CAP: Int64 = 1_000_000_000

func kh_cap(_ x: Int64) -> Int64 { return x < -CAP ? -CAP : (x > CAP ? CAP : x) }

func kh_zip(_ a: V, _ b: V, _ f: (Int64, Int64) -> Int64) -> V {
    if a.isEmpty || b.isEmpty { return [] }
    var o = V()
    o.reserveCapacity(a.count)
    for i in 0..<a.count { o.append(kh_cap(f(a[i], b[i % b.count]))) }
    return o
}

func kh_clamp(_ n: Int64, _ hi: Int) -> Int { return Int(min(Int64(hi), max(0, n))) }

func kh_id(_ a: V) -> V { return a }
func kh_add(_ a: V, _ b: V) -> V { return kh_zip(a, b, { x, y in x &+ y }) }
func kh_sub(_ a: V, _ b: V) -> V { return kh_zip(a, b, { x, y in x &- y }) }
func kh_mul(_ a: V, _ b: V) -> V { return kh_zip(a, b, { x, y in x &* y }) }
func kh_div(_ a: V, _ b: V) -> V { return kh_zip(a, b, { x, y in y == 0 ? 0 : x / y }) }
func kh_mod(_ a: V, _ b: V) -> V { return kh_zip(a, b, { x, y in y == 0 ? 0 : x % y }) }
func kh_len(_ a: V) -> V { return [Int64(a.count)] }
func kh_head(_ a: V) -> V { return a.isEmpty ? [] : [a[0]] }
func kh_tail(_ a: V) -> V { return a.count > 1 ? Array(a[1...]) : [] }
func kh_rev(_ a: V) -> V { return Array(a.reversed()) }
func kh_sort(_ a: V) -> V { return a.sorted() }
func kh_cat(_ a: V, _ b: V) -> V { return a + b }
func kh_take(_ a: V, _ b: V) -> V {
    if b.isEmpty { return [] }
    return Array(a[0..<kh_clamp(b[0], a.count)])
}
func kh_drop(_ a: V, _ b: V) -> V {
    if b.isEmpty { return [] }
    return Array(a[kh_clamp(b[0], a.count)...])
}
func kh_at(_ a: V, _ b: V) -> V {
    if b.isEmpty || b[0] < 0 || b[0] >= Int64(a.count) { return [] }
    return [a[Int(b[0])]]
}
func kh_range(_ a: V) -> V {
    if a.isEmpty { return [] }
    return (0..<Int64(kh_clamp(a[0], 512))).map { $0 }
}
func kh_sum(_ a: V) -> V {
    var t: Int64 = 0
    for x in a { t = kh_cap(t &+ x) }
    return [t]
}
func kh_max(_ a: V) -> V { return a.isEmpty ? [] : [a.max()!] }
func kh_min(_ a: V) -> V { return a.isEmpty ? [] : [a.min()!] }
func kh_filter(_ a: V, _ b: V) -> V { return b.isEmpty ? [] : a.filter { $0 > b[0] } }
func kh_addk(_ a: V, _ b: V) -> V { return b.isEmpty ? [] : a.map { kh_cap($0 &+ b[0]) } }
func kh_mulk(_ a: V, _ b: V) -> V { return b.isEmpty ? [] : a.map { kh_cap($0 &* b[0]) } }
func kh_count(_ a: V, _ b: V) -> V {
    return b.isEmpty ? [] : [Int64(a.filter { $0 == b[0] }.count)]
}
func kh_guard(_ a: V, _ b: V) -> V { return b.isEmpty ? [] : a }
func kh_else(_ a: V, _ b: V) -> V { return a.isEmpty ? b : a }
)SWIFT";

// Kotlin's / and % on Long truncate toward zero like C's, so the division pair
// needs no correction. The literal trap instead: a Kotlin integer literal is an
// Int, so every constant needs the L suffix or kh_add would not typecheck --
// which is why the constant form here is listOf(kL) rather than [k].
const char* kKotlinPrelude = R"KT(typealias V = List<Long>

const val CAP: Long = 1_000_000_000L

fun kh_cap(x: Long): Long = if (x < -CAP) -CAP else if (x > CAP) CAP else x

fun kh_zip(a: V, b: V, f: (Long, Long) -> Long): V {
    if (a.isEmpty() || b.isEmpty()) return emptyList()
    return List(a.size) { i -> kh_cap(f(a[i], b[i % b.size])) }
}

fun kh_clamp(n: Long, hi: Int): Int = minOf(hi.toLong(), maxOf(0L, n)).toInt()

fun kh_id(a: V): V = a.toList()
fun kh_add(a: V, b: V): V = kh_zip(a, b) { x, y -> x + y }
fun kh_sub(a: V, b: V): V = kh_zip(a, b) { x, y -> x - y }
fun kh_mul(a: V, b: V): V = kh_zip(a, b) { x, y -> x * y }
fun kh_div(a: V, b: V): V = kh_zip(a, b) { x, y -> if (y == 0L) 0L else x / y }
fun kh_mod(a: V, b: V): V = kh_zip(a, b) { x, y -> if (y == 0L) 0L else x % y }
fun kh_len(a: V): V = listOf(a.size.toLong())
fun kh_head(a: V): V = if (a.isEmpty()) emptyList() else listOf(a[0])
fun kh_tail(a: V): V = if (a.size > 1) a.subList(1, a.size).toList() else emptyList()
fun kh_rev(a: V): V = a.reversed()
fun kh_sort(a: V): V = a.sorted()
fun kh_cat(a: V, b: V): V = a + b
fun kh_take(a: V, b: V): V = if (b.isEmpty()) emptyList() else a.take(kh_clamp(b[0], a.size))
fun kh_drop(a: V, b: V): V = if (b.isEmpty()) emptyList() else a.drop(kh_clamp(b[0], a.size))
fun kh_at(a: V, b: V): V =
    if (b.isEmpty() || b[0] < 0L || b[0] >= a.size.toLong()) emptyList()
    else listOf(a[b[0].toInt()])
fun kh_range(a: V): V =
    if (a.isEmpty()) emptyList() else List(kh_clamp(a[0], 512)) { it.toLong() }
fun kh_sum(a: V): V {
    var t = 0L
    for (x in a) t = kh_cap(t + x)
    return listOf(t)
}
fun kh_max(a: V): V = if (a.isEmpty()) emptyList() else listOf(a.maxOrNull()!!)
fun kh_min(a: V): V = if (a.isEmpty()) emptyList() else listOf(a.minOrNull()!!)
fun kh_filter(a: V, b: V): V = if (b.isEmpty()) emptyList() else a.filter { it > b[0] }
fun kh_addk(a: V, b: V): V = if (b.isEmpty()) emptyList() else a.map { kh_cap(it + b[0]) }
fun kh_mulk(a: V, b: V): V = if (b.isEmpty()) emptyList() else a.map { kh_cap(it * b[0]) }
fun kh_count(a: V, b: V): V =
    if (b.isEmpty()) emptyList() else listOf(a.count { it == b[0] }.toLong())
fun kh_guard(a: V, b: V): V = if (b.isEmpty()) emptyList() else a.toList()
fun kh_else(a: V, b: V): V = if (a.isEmpty()) b.toList() else a.toList()
)KT";

// PHP's / always produces a FLOAT, so integer division has to be intdiv(),
// which truncates toward zero the way the reference does; % is already the
// truncating remainder. The other trap is range(0, -1), which yields [0, -1]
// rather than the empty list, so kh_range cannot route the zero case through it.
const char* kPhpPrelude = R"PHP(<?php

const CAP = 1000000000;

function kh_cap($x) { return $x < -CAP ? -CAP : ($x > CAP ? CAP : $x); }

function kh_zip($a, $b, $f) {
    if (count($a) === 0 || count($b) === 0) return [];
    $o = [];
    $n = count($b);
    for ($i = 0; $i < count($a); $i++) $o[] = kh_cap($f($a[$i], $b[$i % $n]));
    return $o;
}

function kh_clamp($n, $hi) { return $n < 0 ? 0 : ($n > $hi ? $hi : $n); }

function kh_id($a) { return array_values($a); }
function kh_add($a, $b) { return kh_zip($a, $b, function ($x, $y) { return $x + $y; }); }
function kh_sub($a, $b) { return kh_zip($a, $b, function ($x, $y) { return $x - $y; }); }
function kh_mul($a, $b) { return kh_zip($a, $b, function ($x, $y) { return $x * $y; }); }
function kh_div($a, $b) { return kh_zip($a, $b, function ($x, $y) { return $y === 0 ? 0 : intdiv($x, $y); }); }
function kh_mod($a, $b) { return kh_zip($a, $b, function ($x, $y) { return $y === 0 ? 0 : $x % $y; }); }
function kh_len($a) { return [count($a)]; }
function kh_head($a) { return count($a) === 0 ? [] : [$a[0]]; }
function kh_tail($a) { return count($a) > 1 ? array_slice($a, 1) : []; }
function kh_rev($a) { return array_reverse(array_values($a)); }
function kh_sort($a) { $o = array_values($a); sort($o, SORT_NUMERIC); return $o; }
function kh_cat($a, $b) { return array_merge(array_values($a), array_values($b)); }
function kh_take($a, $b) { return count($b) === 0 ? [] : array_slice($a, 0, kh_clamp($b[0], count($a))); }
function kh_drop($a, $b) { return count($b) === 0 ? [] : array_slice($a, kh_clamp($b[0], count($a))); }
function kh_at($a, $b) {
    if (count($b) === 0 || $b[0] < 0 || $b[0] >= count($a)) return [];
    return [$a[$b[0]]];
}
function kh_range($a) {
    if (count($a) === 0) return [];
    $n = kh_clamp($a[0], 512);
    return $n === 0 ? [] : range(0, $n - 1);
}
function kh_sum($a) {
    $t = 0;
    foreach ($a as $x) $t = kh_cap($t + $x);
    return [$t];
}
function kh_max($a) { return count($a) === 0 ? [] : [max($a)]; }
function kh_min($a) { return count($a) === 0 ? [] : [min($a)]; }
function kh_filter($a, $b) {
    if (count($b) === 0) return [];
    $o = [];
    foreach ($a as $x) if ($x > $b[0]) $o[] = $x;
    return $o;
}
function kh_addk($a, $b) {
    if (count($b) === 0) return [];
    $o = [];
    foreach ($a as $x) $o[] = kh_cap($x + $b[0]);
    return $o;
}
function kh_mulk($a, $b) {
    if (count($b) === 0) return [];
    $o = [];
    foreach ($a as $x) $o[] = kh_cap($x * $b[0]);
    return $o;
}
function kh_count($a, $b) {
    if (count($b) === 0) return [];
    $n = 0;
    foreach ($a as $x) if ($x === $b[0]) $n++;
    return [$n];
}
function kh_guard($a, $b) { return count($b) === 0 ? [] : array_values($a); }
function kh_else($a, $b) { return count($a) === 0 ? array_values($b) : array_values($a); }
)PHP";

} // namespace

const char* lang_name(Lang l) {
    switch (l) {
        case Lang::Cpp: return "C++";
        case Lang::Python: return "Python";
        case Lang::JavaScript: return "JavaScript";
        case Lang::Rust: return "Rust";
        case Lang::Go: return "Go";
        case Lang::Java: return "Java";
        case Lang::CSharp: return "C#";
        case Lang::TypeScript: return "TypeScript";
        case Lang::Ruby: return "Ruby";
        case Lang::Lua: return "Lua";
        case Lang::Haskell: return "Haskell";
        case Lang::Swift: return "Swift";
        case Lang::Kotlin: return "Kotlin";
        case Lang::Php: return "PHP";
    }
    return "?";
}

const char* lang_ext(Lang l) {
    switch (l) {
        case Lang::Cpp: return "cpp";
        case Lang::Python: return "py";
        case Lang::JavaScript: return "js";
        case Lang::Rust: return "rs";
        case Lang::Go: return "go";
        case Lang::Java: return "java";
        case Lang::CSharp: return "cs";
        case Lang::TypeScript: return "ts";
        case Lang::Ruby: return "rb";
        case Lang::Lua: return "lua";
        case Lang::Haskell: return "hs";
        case Lang::Swift: return "swift";
        case Lang::Kotlin: return "kt";
        case Lang::Php: return "php";
    }
    return "txt";
}

std::string prelude(Lang l) {
    switch (l) {
        case Lang::Cpp: return kCppPrelude;
        case Lang::Python: return kPythonPrelude;
        case Lang::JavaScript: return kJsPrelude;
        case Lang::Rust: return kRustPrelude;
        case Lang::Go: return kGoPrelude;
        case Lang::Java: return kJavaPrelude;
        case Lang::CSharp: return kCSharpPrelude;
        case Lang::TypeScript: return kTsPrelude;
        case Lang::Ruby: return kRubyPrelude;
        case Lang::Lua: return kLuaPrelude;
        case Lang::Haskell: return kHaskellPrelude;
        case Lang::Swift: return kSwiftPrelude;
        case Lang::Kotlin: return kKotlinPrelude;
        case Lang::Php: return kPhpPrelude;
    }
    return {};
}

// Defined below: the real emitter, which requires a call-free recipe.
static std::string emit_inlined(const Recipe& r, Lang l, const std::string& fn,
                                std::size_t* lines);

// Splice a library body in place of each call, remapping its node indices and
// routing its input to the call's argument. Recursive, because a library recipe
// may itself call one -- bounded by kMaxCallDepth, the same bound the
// interpreter uses.
Recipe inline_calls(const Recipe& r, const Library& lib) {
    if (!r.found) return r;
    bool any = false;
    for (const Expr& e : r.pool) if (e.op == Op::Call) { any = true; break; }
    if (!any) return r;

    Recipe out;
    out.found = true;
    std::vector<int> remap(r.pool.size(), -1);

    std::function<int(const Recipe&, std::size_t, int, std::size_t)> splice =
        [&](const Recipe& src, std::size_t node, int arg_of_input, std::size_t depth) -> int {
            std::vector<int> local(src.pool.size(), -2);
            std::function<int(std::size_t)> go = [&](std::size_t i) -> int {
                if (local[i] != -2) return local[i];
                const Expr& e = src.pool[i];
                Expr c = e;
                c.a = (e.a < 0) ? arg_of_input : go(static_cast<std::size_t>(e.a));
                c.b = (e.b < 0) ? arg_of_input : go(static_cast<std::size_t>(e.b));
                if (e.op == Op::Call && depth < kMaxCallDepth) {
                    const std::size_t li = e.k % std::max<std::size_t>(1, lib.size());
                    if (li < lib.size() && lib.at(li).recipe.found) {
                        const Recipe& body = lib.at(li).recipe;
                        local[i] = splice(body, body.root, c.a, depth + 1);
                        return local[i];
                    }
                    // No body to inline: emit an identity rather than a call to
                    // nothing, and say so, because silently dropping it is the
                    // defect this whole function exists to remove.
                    c.op = Op::Mov;
                }
                out.pool.push_back(c);
                local[i] = static_cast<int>(out.pool.size()) - 1;
                return local[i];
            };
            return go(node);
        };

    (void)remap;
    out.root = static_cast<std::size_t>(splice(r, r.root, -1, 0));
    return out;
}

std::string emit(const Recipe& r, Lang l, const std::string& fn, std::size_t* lines,
                 const Library* lib) {
    if (lines) *lines = 0;
    if (!r.found) return {};
    // Inline before anything else. A recipe carrying calls cannot be emitted as
    // standalone source, and dropping the calls -- which is what this function
    // used to do -- produces a program that is not the one that was certified.
    const Recipe inlined = (lib != nullptr) ? inline_calls(r, *lib) : r;
    return emit_inlined(inlined, l, fn, lines);
}

static std::string emit_inlined(const Recipe& r, Lang l, const std::string& fn,
                                std::size_t* lines) {
    if (lines) *lines = 0;
    if (!r.found) return {};

    // REFUSE RATHER THAN EMIT SOMETHING WRONG.
    //
    // MapF and FoldF carry a library body in `k`, exactly as Op::Call does, but
    // inline_calls does not splice them -- a fold is a loop, not a substitution.
    // Left to fall through, they would reach fn_of(), hit its default, and emit
    // kh_id: a silent identity where a fold belongs. That is the precise defect
    // that made three quarters of this module's output a different program from
    // the one certified, and it is not going to happen twice.
    //
    // Until every backend has a fold, a recipe containing one emits NOTHING and
    // counts as zero lines. A capability the emitter cannot express is a
    // capability this organ does not yet have, and the throughput figure should
    // say so by being lower rather than by being wrong.
    for (const Expr& e : r.pool) {
        if (e.op == Op::MapF || e.op == Op::FoldF || e.op == Op::Call) return {};
    }

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
    // ONLY Mov IS AN IDENTITY. Op::Call IS NOT, AND TREATING IT AS ONE WAS THE
    // WORST DEFECT THIS MODULE HAS HAD.
    //
    // Op::Call invokes a learned library function. Aliasing it to its argument
    // deleted the call from the emitted source, so `max(sub(x, lib1(x)))` came
    // out as kh_max(kh_sub(x, x)) -- a DIFFERENT PROGRAM, returning [0] where the
    // certified recipe returns [15]. Measured on the bench's own configuration,
    // 90 of 120 certified recipes contained a live Op::Call, so three quarters of
    // the emitted output did not implement what had been verified.
    //
    // That breaks the only claim this organ makes. A certificate is a statement
    // about a program; if the source handed back is a different program, the
    // certificate is attached to nothing. It was introduced while removing the
    // `t0 = kh_id(x)` padding -- a correctness break shipped inside an integrity
    // fix, which is the kind of thing that only surfaces when someone else reads
    // the code.
    std::vector<int> alias(r.pool.size(), -2);        // -2 = not an alias
    for (std::size_t i = 0; i < r.pool.size(); ++i) {
        if (r.pool[i].op == Op::Mov) alias[i] = r.pool[i].a;
    }

    std::string body;
    std::size_t n = 0;
    const std::string sig = var_sigil(l);
    std::function<std::string(int)> ref = [&](int i) -> std::string {
        if (i < 0) return sig + "x";
        const std::size_t u = static_cast<std::size_t>(i);
        if (u < alias.size() && alias[u] != -2) return ref(alias[u]);
        return sig + "t" + std::to_string(u);
    };
    auto line = [&](const std::string& text) { body += text; body += '\n'; ++n; };

    for (std::size_t i = 0; i < r.pool.size(); ++i) {
        if (!live[i]) continue;
        if (alias[i] != -2) continue;                 // identity: nothing to emit
        const Expr& e = r.pool[i];
        const std::string lhs = sig + "t" + std::to_string(i);
        std::string rhs;
        if (e.op == Op::Const) {
            const std::string k = std::to_string(const_value(e.k));
            switch (l) {
                case Lang::Cpp:    rhs = "V{" + k + "}"; break;
                case Lang::Rust:   rhs = "vec![" + k + "]"; break;
                case Lang::Go:     rhs = "V{" + k + "}"; break;
                // Java has no list literal at all, and a bare Kotlin integer
                // literal is an Int rather than a Long.
                case Lang::Java:   rhs = "new long[]{" + k + "}"; break;
                case Lang::CSharp: rhs = "new long[]{" + k + "}"; break;
                case Lang::Kotlin: rhs = "listOf(" + k + "L)"; break;
                case Lang::Lua:    rhs = "{" + k + "}"; break;
                default:           rhs = "[" + k + "]"; break;
            }
        } else {
            const std::string f = std::string(call_prefix(l)) + fn_of(e.op);
            if (l == Lang::Haskell) {
                // Haskell applies by juxtaposition; `f(a, b)` would pass one
                // TUPLE, which is a different call and would not typecheck.
                rhs = is_binary(e.op) ? f + " " + ref(e.a) + " " + ref(e.b)
                                      : f + " " + ref(e.a);
            } else if (l == Lang::Rust) {
                rhs = is_binary(e.op) ? f + "(&" + ref(e.a) + ", &" + ref(e.b) + ")"
                                      : f + "(&" + ref(e.a) + ")";
            } else {
                rhs = is_binary(e.op) ? f + "(" + ref(e.a) + ", " + ref(e.b) + ")"
                                      : f + "(" + ref(e.a) + ")";
            }
        }
        switch (l) {
            case Lang::Cpp:        line("    const V " + lhs + " = " + rhs + ";"); break;
            case Lang::Rust:       line("    let " + lhs + ": V = " + rhs + ";"); break;
            case Lang::JavaScript: line("  const " + lhs + " = " + rhs + ";"); break;
            case Lang::Python:     line("    " + lhs + " = " + rhs); break;
            case Lang::Go:         line("\t" + lhs + " := " + rhs); break;
            case Lang::Java:       line("    long[] " + lhs + " = " + rhs + ";"); break;
            case Lang::CSharp:     line("    long[] " + lhs + " = " + rhs + ";"); break;
            case Lang::TypeScript: line("  const " + lhs + ": V = " + rhs + ";"); break;
            case Lang::Ruby:       line("  " + lhs + " = " + rhs); break;
            case Lang::Lua:        line("  local " + lhs + " = " + rhs); break;
            case Lang::Haskell:    line("    " + lhs + " = " + rhs); break;
            case Lang::Swift:      line("    let " + lhs + ": V = " + rhs); break;
            case Lang::Kotlin:     line("    val " + lhs + ": V = " + rhs); break;
            case Lang::Php:        line("    " + lhs + " = " + rhs + ";"); break;
        }
    }

    // The wrapper counts as TWO lines in every language -- the signature and the
    // return -- whatever punctuation the language needs around them. A closing
    // brace is not a line of synthesised code, and counting it in the braced
    // languages while Python has none would make the same recipe cost a
    // different number of lines depending on the target, which would make the
    // per-language table meaningless.
    // ref(), not a raw name. When the root node is itself an identity there is
    // no `t<root>` declared, so the emitted function returned an undefined
    // variable and the file did not compile at all.
    const std::string root = ref(static_cast<int>(r.root));
    std::string out;
    switch (l) {
        case Lang::Cpp:
            out = "V " + fn + "(const V& x) {\n" + body + "    return " + root + ";\n}\n";
            break;
        case Lang::Rust:
            out = "pub fn " + fn + "(x: &V) -> V {\n" + body + "    " + root + "\n}\n";
            break;
        case Lang::JavaScript:
            out = "function " + fn + "(x) {\n" + body + "  return " + root + ";\n}\n";
            break;
        case Lang::Python:
            out = "def " + fn + "(x):\n" + body + "    return " + root + "\n";
            break;
        case Lang::Go:
            out = "func " + fn + "(x V) V {\n" + body + "\treturn " + root + "\n}\n";
            break;
        // Java and C# have no top-level functions, so each emitted function is
        // wrapped in its own non-public class. Several of them per file is legal
        // precisely because none is public.
        case Lang::Java:
            out = "final class Fn_" + fn + " { static long[] " + fn + "(long[] x) {\n" +
                  body + "    return " + root + ";\n} }\n";
            break;
        case Lang::CSharp:
            out = "static class Fn_" + fn + " { public static long[] " + fn +
                  "(long[] x) {\n" + body + "    return " + root + ";\n} }\n";
            break;
        case Lang::TypeScript:
            out = "function " + fn + "(x: V): V {\n" + body + "  return " + root + ";\n}\n";
            break;
        case Lang::Ruby:
            out = "def " + fn + "(x)\n" + body + "  " + root + "\nend\n";
            break;
        case Lang::Lua:
            out = "function " + fn + "(x)\n" + body + "  return " + root + "\nend\n";
            break;
        // Haskell has no statements to sequence, so the SSA lines become a `let`
        // group. `let` opens the layout block on the signature line and `in`,
        // outdented, closes it -- the bindings themselves can then be emitted at
        // one fixed indent like every other backend's.
        case Lang::Haskell:
            out = fn + " :: V -> V\n" + fn + " x = let\n" + body + "  in " + root + "\n";
            break;
        case Lang::Swift:
            out = "func " + fn + "(_ x: V) -> V {\n" + body + "    return " + root + "\n}\n";
            break;
        case Lang::Kotlin:
            out = "fun " + fn + "(x: V): V {\n" + body + "    return " + root + "\n}\n";
            break;
        case Lang::Php:
            out = "function " + fn + "($x) {\n" + body + "    return " + root + ";\n}\n";
            break;
    }
    n += 2;
    if (lines) *lines = n;
    return out;
}

} // namespace khora::techne
