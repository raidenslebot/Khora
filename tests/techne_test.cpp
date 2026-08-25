// Techne test — the properties a code-writing organ has to have before any
// result it produces means anything.
//
// The claim this module makes is narrow and checkable: it either returns a
// program certified against a specification, or it returns nothing. So the tests
// are about the contract, not about how clever the search is.
//
//   1. Execution is TOTAL and BOUNDED. No input makes a program fail to return,
//      no program loops, and memory cannot grow without limit. That is what
//      makes the oracle usable as a fitness function -- a search that can hang
//      cannot be run to completion.
//   2. Mutation and crossover are CLOSED. Every byte string is a program, so
//      there is no repair rule and therefore no human prior hidden in one.
//   3. Nothing is certified that does not pass every case, and a program that
//      passes the visible cases while failing the held-out ones is reported as
//      memorisation rather than as a solution.
//   4. The search actually solves things a dumb enumerator does not.

#include "khora/techne/techne.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace khora::techne;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("  FAIL: %s\n", what); ++failures; }
    else         std::printf("  ok  : %s\n", what);
}

std::uint64_t rs = 0x7EC4E5ULL;
std::uint64_t rnd() {
    std::uint64_t z = (rs += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

Spec make_spec(const char* name,
               const std::vector<Case>& visible,
               const std::vector<Case>& held) {
    Spec s;
    s.name = name;
    s.cases = visible;
    s.holdout = held;
    return s;
}

} // namespace

int main() {
    std::printf("Techne test\n");

    // --- EXECUTION IS TOTAL AND BOUNDED --------------------------------------
    //
    // 2,000 random programs on adversarial inputs -- empty lists, negatives,
    // extremes -- every one of which must return. If any input could hang or
    // trap, the oracle could not be used as a fitness function at all.
    {
        bool ok = true;
        const std::vector<Value> inputs = {
            {}, {0}, {-1}, {INT64_MIN}, {INT64_MAX},
            {5, 3, 1, 4, 2}, {0, 0, 0}, {-7, 7},
        };
        for (int t = 0; t < 2000 && ok; ++t) {
            const Program p = Program::random(1 + (rnd() % 12), rnd());
            for (const Value& in : inputs) {
                const Value out = run(p, in, nullptr);
                if (out.size() > kMaxListLen) { ok = false; break; }
            }
        }
        check(ok, "2,000 random programs return on every adversarial input, bounded");
    }

    // --- MUTATION AND CROSSOVER ARE CLOSED -----------------------------------
    {
        Program p = Program::random(6, 4242);
        bool ok = true;
        for (int i = 0; i < 1000 && ok; ++i) {
            p = p.mutate(rnd(), 0.08);
            if (p.tape().size() % 4 != 0) ok = false;
            if (p.length() > 0) (void)run(p, {1, 2, 3}, nullptr);
        }
        check(ok && p.length() > 0, "1,000 successive mutations stay runnable and in frame");

        for (int i = 0; i < 500 && ok; ++i) {
            const Program a = Program::random(1 + (rnd() % 8), rnd());
            const Program b = Program::random(1 + (rnd() % 8), rnd());
            const Program c = Program::cross(a, b, rnd());
            if (c.tape().size() % 4 != 0 || c.length() == 0) ok = false;
            (void)run(c, {4, 5, 6}, nullptr);
        }
        check(ok, "crossover of any two programs yields a runnable program");
    }

    // --- DETERMINISM ---------------------------------------------------------
    {
        const Program p = Program::random(8, 31337);
        const Value in{9, 2, 7};
        check(run(p, in, nullptr) == run(p, in, nullptr),
              "the same program on the same input is deterministic");
    }

    // --- MOST OF A PROGRAM IS EXPRESSED --------------------------------------
    //
    // The sibling organ measured 1.309 live instructions out of 5 and 27% pure
    // identity when the output register was fixed. Reading out the last write
    // fixed it there and the same choice is made here, so the same measurement
    // has to hold.
    {
        const std::size_t kTrials = 20000;
        std::size_t live = 0, identity = 0;
        for (std::size_t t = 0; t < kTrials; ++t) {
            const Program p = Program::random(6, rnd());
            const std::size_t e = p.effective_length();
            live += e;
            if (e == 0) ++identity;
        }
        const double mean = static_cast<double>(live) / kTrials;
        std::printf("  random 6-instruction programs: %.3f live, %.1f%% pure identity\n",
                    mean, 100.0 * identity / static_cast<double>(kTrials));
        check(mean > 2.0, "most of a program is expressed rather than discarded");
    }

    // --- THE CONTRACT: NOTHING UNCERTIFIED IS RETURNED -----------------------
    //
    // A specification no program in this instruction set can satisfy. The search
    // must come back with proof == None rather than with its best guess dressed
    // as an answer.
    {
        // Requires a value unreachable from the input by any available op.
        std::vector<Case> impossible;
        for (int i = 1; i <= 6; ++i) {
            impossible.push_back({Value{i}, Value{static_cast<std::int64_t>(i) * 7919 + 104729}});
        }
        SearchConfig cfg;
        cfg.population = 120;
        cfg.generations = 40;
        const Solution s = synthesise(make_spec("impossible", impossible, {}), cfg, nullptr);
        std::printf("  impossible spec: %zu/%zu cases, certified=%s\n",
                    s.cases_passed, s.cases_total, s.certified() ? "YES" : "no");
        check(!s.certified(),
              "an unsatisfiable specification returns NOTHING, not a plausible guess");
    }

    // --- IT SOLVES, AND IT GENERALISES ---------------------------------------
    //
    // sum of a list. Visible cases are short; held-out cases are longer and were
    // never scored during the search, so passing them cannot be memorisation.
    {
        std::vector<Case> vis{
            {{1, 2, 3},      {6}},
            {{5},            {5}},
            {{10, 20},       {30}},
            {{0, 0, 0},      {0}},
            {{7, -3},        {4}},
        };
        std::vector<Case> held{
            {{1, 2, 3, 4, 5, 6}, {21}},
            {{100, 200, 300},    {600}},
            {{-5, -5, -5, -5},   {-20}},
        };
        SearchConfig cfg;
        cfg.population = 300;
        cfg.generations = 200;
        cfg.program_len = 4;
        const Solution s = synthesise(make_spec("sum", vis, held), cfg, nullptr);
        std::printf("  sum: %zu/%zu visible, %zu/%zu held out, %zu candidates\n",
                    s.cases_passed, s.cases_total, s.holdout_passed, s.holdout_total,
                    s.candidates_tried);
        check(s.certified(), "it solves 'sum of a list'");
        check(s.proof == Proof::Generalised,
              "and it generalises to held-out cases it was never scored on");
        if (s.certified()) std::printf("%s", s.program.disassemble().c_str());
    }

    // --- THE LIBRARY GROWS, AND IS BOUNDED -----------------------------------
    {
        Library lib(4);
        for (int i = 0; i < 10; ++i) {
            lib.admit("f" + std::to_string(i), Program::random(3, 1000 + i), i);
        }
        check(lib.size() == 10, "certified programs are admitted as new primitives");
        const std::size_t dropped = lib.prune();
        std::printf("  library: 10 admitted, %zu evicted, %zu kept (budget 4)\n",
                    dropped, lib.size());
        check(lib.size() <= 4, "and the library holds a hard budget rather than accreting");
        check(dropped > 0, "eviction actually runs");
    }

    // --- DUPLICATES ARE REFUSED ----------------------------------------------
    {
        Library lib(8);
        const Program p = Program::random(3, 55);
        check(lib.admit("a", p, 0), "a new primitive is admitted");
        check(!lib.admit("b", p, 1), "an identical primitive is refused rather than duplicated");
    }

    // --- THE EMITTED SOURCE IS THE PROGRAM THAT WAS CERTIFIED ----------------
    //
    // This is the claim the whole organ rests on, and it was FALSE for three
    // quarters of results until an external reader found it. Op::Call was
    // treated as an identity alias to its argument, so a library call was
    // deleted from the emitted source: max(sub(x, lib1(x))) came out as
    // kh_max(kh_sub(x, x)) -- a different program, returning [0] where the
    // recipe returns [15]. A certificate attached to a program you do not hand
    // back is attached to nothing.
    {
        Library lib(8);

        // A library function: double every element.
        Recipe dbl;
        dbl.pool.push_back(Expr{Op::Mov, -1, -1, 0});
        dbl.pool.push_back(Expr{Op::Const, -1, -1, 2});
        dbl.pool.push_back(Expr{Op::MapMul, 0, 1, 0});
        dbl.root = 2;
        dbl.found = true;
        check(lib.admit_recipe("double", dbl, 0), "a library function is admitted");

        // A caller that USES it: sum(double(x)).
        Recipe caller;
        caller.pool.push_back(Expr{Op::Mov, -1, -1, 0});
        caller.pool.push_back(Expr{Op::Call, 0, -1, 0});
        caller.pool.push_back(Expr{Op::Sum, 1, -1, 0});
        caller.root = 2;
        caller.found = true;

        const Value in{1, 2, 3, 4};
        const Value want = caller.apply(in, &lib);
        std::printf("  recipe with a library call on [1,2,3,4] -> [%lld]\n",
                    want.empty() ? 0LL : static_cast<long long>(want[0]));
        check(want == Value{20}, "the recipe itself computes sum of doubles");

        const Recipe flat = inline_calls(caller, lib);
        bool has_call = false;
        for (const Expr& e : flat.pool) if (e.op == Op::Call) has_call = true;
        check(!has_call, "inlining removes every library call");
        check(flat.apply(in, nullptr) == want,
              "and the inlined recipe computes exactly the same thing");

        std::size_t lines = 0;
        const std::string src = emit(caller, Lang::Python, "f", &lines, &lib);
        std::printf("%s", src.c_str());
        check(src.find("kh_sum") != std::string::npos,
              "the emitted source contains the outer operation");
        check(src.find("kh_mulk") != std::string::npos,
              "AND the library body, rather than silently dropping the call");
    }

    std::printf("\n");
    if (failures == 0) std::printf("ALL PASS\n");
    else               std::printf("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
