// DOES SPLITTING THE TARGET'S OWN OUTPUT REACH WHAT THE FLAT SEARCH CANNOT?
//
// Bottom-up construction is exponential in depth and the pool cap truncates it.
// Measured in fold_bench: `roll`, which sends [a1..ak, next] to [next, a1..ak],
// does not derive at a pool of 400,000 -- and it is three nodes once `last` and
// `init` are library entries, because a library entry costs one node however
// deep it is. Written flat it is nine.
//
// What made that work was KNOWING TO ASK for last and init, which I did by hand.
// synthesise_split asks the target instead: its output is a list, the dominant
// way to build a list is to append two of them, so cut every case's output at the
// same place and pose the two halves as their own specifications. Cutting roll
// after one element IS last and init.
//
// THE CONTROL THAT MATTERS IS WORK, NOT POOL. Splitting runs up to nine searches
// where the flat arm runs one, so beating the flat arm at an equal pool proves
// nothing -- it just spent more. The third arm gives the flat search NINE TIMES
// THE POOL, so it may do the same total work in one search, and the comparison is
// then about the shape of the search rather than the size of the budget.
//
// Nothing is accepted because a half was solved. Both halves go into a local
// library and the ORIGINAL specification goes back through the ordinary hardened
// search, which proves the combination the same way it proves anything.
#include <khora/techne/techne.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace khora::techne;

namespace {

std::uint64_t st = 0x51D3F00DULL;
std::uint64_t rnd() { st ^= st << 13; st ^= st >> 7; st ^= st << 17; return st; }

using Fn = std::function<Value(const Value&)>;

// The halves. Each is several nodes with the full operation set -- there is no
// single opcode for any of them -- so a target built from two of them is deep
// enough that reaching it flat is the question.
Value f_last(const Value& v) { return v.empty() ? Value{} : Value{v.back()}; }
Value f_init(const Value& v) {
    return v.empty() ? Value{} : Value(v.begin(), v.end() - 1);
}
Value f_span(const Value& v) {
    if (v.empty()) return Value{};
    const auto mm = std::minmax_element(v.begin(), v.end());
    return Value{*mm.second - *mm.first};
}
Value f_sortd(const Value& v) { Value o = v; std::sort(o.begin(), o.end()); return o; }
Value f_delta(const Value& v) {
    Value o;
    for (std::size_t i = 1; i < v.size(); ++i) o.push_back(v[i] - v[i - 1]);
    return o;
}

struct Target { const char* name; Fn a; Fn b; };

// Every target is a CONCATENATION, which is the structure the split is for. Said
// plainly rather than buried: this bench measures the shapes the mechanism
// addresses, and says nothing about any other shape.
std::vector<Target> targets() {
    return {
        {"roll",       f_last,  f_init},    // the one fold_bench could not derive
        {"last_span",  f_last,  f_span},
        {"span_init",  f_span,  f_init},
        {"span_sort",  f_span,  f_sortd},
        {"last_delta", f_last,  f_delta},
        {"span_delta", f_span,  f_delta},
    };
}

Spec spec_for(const Target& t) {
    Spec s;
    s.name = t.name;
    for (std::size_t i = 0; i < 14; ++i) {
        Value in;
        for (std::size_t j = 0; j < 2 + (i % 5); ++j)
            in.push_back(static_cast<std::int64_t>(rnd() % 24) - 10);
        Value out = t.a(in);
        const Value tail = t.b(in);
        out.insert(out.end(), tail.begin(), tail.end());
        s.cases.push_back({in, out});
    }
    return s;
}

Oracle oracle_for(const Target& t) {
    return [t](const Value& in) {
        Value out = t.a(in);
        const Value tail = t.b(in);
        out.insert(out.end(), tail.begin(), tail.end());
        return out;
    };
}

struct Row { bool proved; double secs; std::string prog; };

Row run(const Spec& s, const Oracle& o, std::size_t pool, bool split) {
    const auto t0 = std::chrono::steady_clock::now();
    const BuildResult b = split
        ? synthesise_split(s, pool, o, -2, 2, 4, 3, nullptr, 2)
        : synthesise_hardened(s, pool, o, -2, 2, 4, 3, nullptr);
    const auto t1 = std::chrono::steady_clock::now();
    Row r;
    r.proved = (b.proof == Proof::Exhaustive);
    r.secs = std::chrono::duration<double>(t1 - t0).count();
    r.prog = b.recipe.found ? b.recipe.render() : std::string("--");
    return r;
}

}  // namespace

int main(int argc, char** argv) {
    const std::size_t pool = (argc > 1) ? std::stoul(argv[1]) : 30000;

    std::printf("\n  SPLITTING THE TARGET'S OWN OUTPUT\n\n");
    std::printf("  Every target below is a CONCATENATION of two functions, each of\n");
    std::printf("  which takes several nodes to express -- so written flat the answer\n");
    std::printf("  is deep, and bottom-up search is exponential in depth.\n\n");
    std::printf("  Three arms. `flat` is the ordinary hardened search at a pool of\n");
    std::printf("  %zu. `split` cuts each case's output at the same place, solves the\n", pool);
    std::printf("  two halves as their own specifications, admits them, and puts the\n");
    std::printf("  ORIGINAL specification back through the ordinary search. `flat x9`\n");
    std::printf("  gives the flat arm nine times the pool, because splitting runs up to\n");
    std::printf("  nine searches and beating a smaller budget proves nothing.\n\n");

    std::printf("  target     | flat | flat x9 | split | split answer\n");
    std::printf("  -----------+------+---------+-------+---------------------------\n");

    std::size_t nf = 0, nb = 0, ns = 0;
    double tf = 0, tb = 0, ts = 0;
    for (const Target& t : targets()) {
        const Spec s = spec_for(t);
        const Oracle o = oracle_for(t);
        const Row f = run(s, o, pool, false);
        const Row g = run(s, o, pool * 9, false);
        const Row h = run(s, o, pool, true);
        nf += f.proved; nb += g.proved; ns += h.proved;
        tf += f.secs;   tb += g.secs;   ts += h.secs;
        std::printf("  %-10s | %-4s | %-7s | %-5s | %s\n", t.name,
                    f.proved ? "yes" : "no", g.proved ? "yes" : "no",
                    h.proved ? "yes" : "no",
                    h.proved ? h.prog.c_str() : (f.proved ? f.prog.c_str() : "--"));
    }

    std::printf("\n  proved: flat %zu/%zu, flat x9 %zu/%zu, split %zu/%zu\n",
                nf, targets().size(), nb, targets().size(), ns, targets().size());
    std::printf("  seconds: flat %.1f, flat x9 %.1f, split %.1f\n", tf, tb, ts);
    std::printf("\n  A split answer is proved by the same gate as any other: a proof\n");
    std::printf("  over every list of length 0..4 over -2..2, the extremal inputs, and\n");
    std::printf("  counterexample refinement. Solving a half is a search hint and is\n");
    std::printf("  not evidence about the whole.\n\n");
    return 0;
}
