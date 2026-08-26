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

    // --- A HIGHER-ORDER RECIPE EMITS A UNIT THAT IS THE SAME PROGRAM ---------
    //
    // MapF and FoldF name a library BODY rather than a value, so the thing that
    // has to be checked is not that a fold appears in the source -- it is that
    // the body it names is in the same file, under the index the interpreter
    // would resolve, and defined before the function that folds over it.
    {
        Library lib(8);
        // 0: sum whatever it is handed. As a fold body that is "+", because the
        //    body receives the running value and the next element as a pair.
        Recipe pairsum;
        pairsum.pool.push_back(Expr{Op::Sum, -1, -1, 0});
        pairsum.root = 0; pairsum.found = true;
        lib.admit_recipe("pairsum", pairsum, 0);
        // 1 and 2 stack a further higher-order level each, so a fold over 2
        // reaches depth 3 -- where the interpreter stops and returns nothing.
        Recipe m1;
        m1.pool.push_back(Expr{Op::MapF, -1, -1, 0});
        m1.root = 0; m1.found = true;
        lib.admit_recipe("map_pairsum", m1, 0);
        Recipe m2;
        m2.pool.push_back(Expr{Op::MapF, -1, -1, 1});
        m2.root = 0; m2.found = true;
        lib.admit_recipe("map_map", m2, 0);
        check(lib.size() == 3, "three library bodies admitted");

        Recipe fold;
        fold.pool.push_back(Expr{Op::FoldF, -1, -1, 0});
        fold.root = 0; fold.found = true;

        const Value got = fold.apply(Value{1, 2, 3, 4}, &lib);
        std::printf("  fold of + over [1,2,3,4] -> [%lld]\n",
                    got.empty() ? -1LL : static_cast<long long>(got[0]));
        check(got == Value{10}, "a fold with an adding body sums the list");
        check(fold.apply(Value{}, &lib).empty(), "an empty list folds to nothing");
        check(fold.apply(Value{9}, &lib) == Value{9}, "a singleton folds to itself");

        std::size_t lines = 0;
        const std::string unit = emit_unit(fold, Lang::Python, "f", &lib, &lines);
        std::printf("%s", unit.c_str());
        const std::size_t body = unit.find("def kh_lib0");
        const std::size_t user = unit.find("kh_foldf(x, kh_lib0)");
        check(body != std::string::npos, "the unit carries the library body it folds over");
        check(user != std::string::npos, "and the fold names that body rather than an identity");
        check(body < user, "with the body defined before the function that folds over it");
        check(lines > 0, "and the synthesised lines are counted");

        // emit() hands back one function, which cannot carry a body, so it
        // refuses -- and still emits everything that is not higher order.
        check(emit(fold, Lang::Python, "f", nullptr, &lib).empty(),
              "emit() refuses a fold rather than emitting a dangling call");

        // AT THE DEPTH BOUND, REFUSE. The interpreter yields empty there, and
        // one emitted kh_lib0 cannot be a fold at one depth and empty at another.
        Recipe deep;
        deep.pool.push_back(Expr{Op::FoldF, -1, -1, 2});
        deep.root = 0; deep.found = true;
        check(emit_unit(deep, Lang::Python, "f", &lib).empty(),
              "and a nesting that reaches kMaxCallDepth refuses the whole unit");
    }

    // --- THE FOUR OPERATIONS THE TEXT BENCH NAMED ----------------------------
    //
    // Four string tasks failed there and I claimed each had a specific missing
    // capability behind it. These four exist to test that claim, so they had
    // better do exactly what the claim says -- an operation that is subtly wrong
    // would make the diagnosis untestable rather than tested.
    {
        auto build2 = [](Op op, std::uint8_t k) {
            Recipe r;
            r.pool.push_back(Expr{Op::Mov, -1, -1, 0});      // the input
            r.pool.push_back(Expr{Op::Const, -1, -1, k});    // a constant operand
            r.pool.push_back(Expr{op, 0, 1, 0});
            r.root = 2;
            r.found = true;
            return r;
        };

        const Recipe gt = build2(Op::Gt, 2);                 // > 2
        check(gt.apply(Value{1, 2, 3, 5}, nullptr) == Value{0, 0, 1, 1},
              "Gt gives an elementwise indicator, so a conditional becomes arithmetic");

        const Recipe mem = build2(Op::Member, 3);            // in [3]
        check(mem.apply(Value{1, 3, 5, 3}, nullptr) == Value{0, 1, 0, 1},
              "Member tests set membership");

        const Recipe until = build2(Op::Until, 0);           // up to the first 0
        check(until.apply(Value{7, 8, 0, 9}, nullptr) == Value{7, 8},
              "Until stops at the delimiter");
        check(until.apply(Value{7, 8, 9}, nullptr) == Value{7, 8, 9},
              "and returns everything when the delimiter is absent");

        Recipe delta;
        delta.pool.push_back(Expr{Op::Mov, -1, -1, 0});
        delta.pool.push_back(Expr{Op::Delta, 0, -1, 0});
        delta.root = 1;
        delta.found = true;
        check(delta.apply(Value{1, 4, 9}, nullptr) == Value{3, 5},
              "Delta compares neighbouring elements");
        check(delta.apply(Value{5}, nullptr).empty(),
              "and a single element has no neighbours");
        check(delta.apply(Value{}, nullptr).empty(), "nor does an empty list");

        // AND THEY EMIT. These four were refused by the emitter for want of a
        // backend, which meant a recipe using one could be certified and never
        // handed back as source. The failure mode if that regresses is not an
        // empty string, it is fn_of() falling through to kh_id -- so the check
        // is that the right helper is NAMED, not merely that something came out.
        struct { const Recipe* r; const char* want; } named[] = {
            {&gt, "kh_gt"}, {&mem, "kh_member"}, {&until, "kh_until"}, {&delta, "kh_delta"},
        };
        for (const auto& n : named) {
            const std::string py = emit(*n.r, Lang::Python, "f", nullptr, nullptr);
            check(py.find(n.want) != std::string::npos,
                  (std::string("emitted Python names ") + n.want).c_str());
        }
        // Arity, in the one target where getting it wrong is not a syntax error.
        // Haskell applies by juxtaposition, so a binary op emitted as unary
        // silently becomes a partial application rather than a compile error at
        // the call site.
        // Counted, not pattern-matched against particular operand NAMES. The
        // first version looked for the literal "kh_gt x t", which broke the
        // moment Mov stopped being aliased away and the first operand became t0
        // instead of x -- a correctness fix in the emitter failing a test that
        // was only ever checking arity. A test should assert the property it
        // cares about.
        auto haskell_arity = [](const std::string& src, const std::string& fn) {
            const std::size_t at = src.find(fn + " ");
            if (at == std::string::npos) return static_cast<std::size_t>(0);
            std::size_t i = at + fn.size();
            std::size_t operands = 0;
            while (i < src.size() && src[i] != ')' && src[i] != 0x0A) {
                if (src[i] == ' ') { ++i; continue; }
                ++operands;
                while (i < src.size() && src[i] != ' ' && src[i] != ')' && src[i] != 0x0A) ++i;
            }
            return operands;
        };
        check(haskell_arity(emit(gt, Lang::Haskell, "f", nullptr, nullptr), "kh_gt") == 2,
              "Haskell emits Gt with both operands");
        check(haskell_arity(emit(delta, Lang::Haskell, "f", nullptr, nullptr), "kh_delta") == 1,
              "and Delta with one");
    }

    // -- A LIBRARY SURVIVES A ROUND TRIP TO DISK -----------------------------
    //
    // Without this the library is built, filled and discarded at process exit,
    // so nothing about PROGRAMMING compounds across runs. The check is not that
    // the file parses: it is that every entry computes the SAME FUNCTION after
    // reloading, because a serialiser that writes enum ordinals would round-trip
    // cleanly and silently mean something else after one operation was added.
    {
        Library src(16);
        auto unit = [](Op op) {
            Recipe r; r.found = true; r.root = 1;
            Expr in;  in.op = Op::Mov;  in.a = -1;
            Expr e;   e.op = op; e.a = 0;
            r.pool = {in, e};
            return r;
        };
        src.admit_recipe("sort", unit(Op::Sort), 0);
        src.admit_recipe("rev",  unit(Op::Rev),  1);
        {   // one with a literal, a k, and a call, so every field is exercised
            Recipe r; r.found = true; r.root = 3;
            Expr in;  in.op = Op::Mov;   in.a = -1;
            Expr c;   c.op = Op::Const;  c.has_lit = true; c.lit = -7;
            Expr m;   m.op = Op::MapAdd; m.a = 0; m.b = 1;
            Expr k;   k.op = Op::Call;   k.a = 2; k.k = 0;
            r.pool = {in, c, m, k};
            src.admit_recipe("shift-then-sort", r, 2);
        }

        const std::string path = "techne_library_roundtrip.txt";
        check(src.save(path), "a library writes itself to disk");

        Library back(16);
        check(back.load(path), "and reads itself back");
        check(back.size() == src.size(), "with every entry present");

        bool same = true, named = true;
        const Value probes[] = {{}, {3, 1, 2}, {-5, 5}, {7}, {0, 0, 9, 2}};
        for (std::size_t i = 0; i < src.size() && i < back.size(); ++i) {
            if (src.at(i).name != back.at(i).name) named = false;
            for (const Value& v : probes) {
                if (src.at(i).recipe.apply(v, &src) != back.at(i).recipe.apply(v, &back))
                    same = false;
            }
        }
        check(named, "with names intact");
        check(same, "and every entry computing the same function afterwards");
        std::remove(path.c_str());
    }

    // -- FUNCTIONS OF MORE THAN ONE ARGUMENT --------------------------------
    //
    // Every program this system could express used to be a UNARY function of one
    // integer list, because a Case held exactly one input. Most programs a person
    // writes take more than one argument, so this is the difference between
    // "writes list transformations" and "writes programs".
    {
        Spec sp;
        sp.name = "append_two";
        auto add2 = [](Value a, Value b, Value o) {
            return Case(std::move(a), std::vector<Value>{std::move(b)}, std::move(o));
        };
        sp.cases.push_back(add2({1, 2}, {3},       {1, 2, 3}));
        sp.cases.push_back(add2({7},    {8, 9},    {7, 8, 9}));
        sp.cases.push_back(add2({},     {4},       {4}));
        sp.cases.push_back(add2({5, 5}, {},        {5, 5}));
        sp.holdout.push_back(add2({2, 4, 6}, {1, 3}, {2, 4, 6, 1, 3}));

        const BuildResult b = construct(sp, 4000, nullptr);
        check(b.proof == Proof::Generalised,
              "a two-argument function is synthesised and generalises");
        check(b.recipe.arity() == 2, "and the recipe reports arity 2");
        // The held-out case is the one that matters: it was never searched.
        const Value got = b.recipe.apply_n({{2, 4, 6}, {1, 3}}, nullptr);
        check(got == Value({2, 4, 6, 1, 3}), "and it is right on an unseen pair");
        // SECOND ARGUMENT ACTUALLY READ, not ignored. A recipe that quietly
        // dropped it would still pass a case whose answer happens to start with
        // the first argument, so change only the second and require the output
        // to follow.
        const Value other = b.recipe.apply_n({{2, 4, 6}, {9}}, nullptr);
        check(other == Value({2, 4, 6, 9}), "and the second argument is really used");
        std::printf("  two-arg solution: %s\n", b.recipe.render().c_str());
    }

    // -- Eviction must not change what a surviving recipe computes ----------
    //
    // A recipe names its callee by INDEX. prune() used to sort items_ in place
    // and truncate, so after an eviction a stored Call pointed at a different
    // function. It was invisible because uses was never incremented, every sort
    // key was equal, and stable_sort left the order alone. This is the check
    // that would have caught it, and it fails against the old prune.
    {
        Library lib(3);
        auto unit = [](Op op) {
            Recipe r; r.found = true; r.root = 1;
            Expr in;  in.op = Op::Mov;  in.a = -1;
            Expr e;   e.op = op; e.a = 0;
            r.pool = {in, e};
            return r;
        };
        // The caller is admitted EARLY. Eviction keeps the age-ordered prefix,
        // so a caller younger than the cut is dropped on its own merits and the
        // check would pass or fail for reasons unrelated to indices.
        lib.admit_recipe("sort", unit(Op::Sort), 0);
        Recipe caller; caller.found = true; caller.root = 1;
        {
            Expr in; in.op = Op::Mov; in.a = -1;
            Expr c;  c.op = Op::Call; c.a = 0; c.k = 0;   // sort
            caller.pool = {in, c};
        }
        lib.admit_recipe("via-sort", caller, 1);
        lib.admit_recipe("head", unit(Op::Head), 2);
        lib.admit_recipe("rev",  unit(Op::Rev),  3);

        const Value arg{3, 1, 2};
        std::size_t at = lib.size();
        for (std::size_t i = 0; i < lib.size(); ++i)
            if (lib.at(i).name == "via-sort") at = i;
        const Value before = lib.at(at).recipe.apply(arg, &lib, 0);
        check(before == Value({1, 2, 3}), "caller resolves through the library");

        check(lib.prune() > 0, "prune evicts when over budget");
        // The budget is HARD even when survivors reference each other. Closing
        // the keep set under references without a cap let the library grow
        // without limit -- the failure this class exists to prevent.
        check(lib.size() <= 3, "the hard budget holds across cross-references");

        // Find the caller again by name -- its index may legitimately move.
        at = lib.size();
        for (std::size_t i = 0; i < lib.size(); ++i)
            if (lib.at(i).name == "via-sort") at = i;
        check(at < lib.size(), "the caller survived eviction");
        if (at < lib.size())
            check(lib.at(at).recipe.apply(arg, &lib, 0) == before,
                  "and still computes the same function");
    }

    // --- A BOUNDED PROOF IS NOT A PROOF, AND THIS IS THE CASE THAT SHOWS IT ---
    //
    // synthesise_exhaustive checks every input of a stated finite domain, which
    // is a real proof over that domain and nothing more. On a task whose
    // behaviour changes only OUTSIDE the domain it accepts with full confidence
    // and is wrong. Measured over 36 such tasks it accepted 36 and got 0 right,
    // losing to random probing, which at least draws a big number occasionally.
    //
    // synthesise_hardened keeps that pass and adds the extremes of the range the
    // program is actually for. It accepts less and is right about what it
    // accepts. These two checks pin exactly that difference, so a later
    // simplification cannot quietly collapse one into the other.
    {
        // "the fifth element, or nothing" -- invisible below length 5, and the
        // proof domain below stops at length 4.
        auto fifth = [](const Value& v) -> Value {
            return v.size() < 5 ? Value{} : Value{v[4]};
        };
        Spec spec;
        spec.name = "fifth";
        std::uint64_t sd = 20260827;
        auto nxt = [&sd]() { sd ^= sd << 13; sd ^= sd >> 7; sd ^= sd << 17; return sd; };
        for (int k = 0; k < 12; ++k) {
            Value in;
            const std::size_t len = nxt() % 5;          // 0..4, inside the domain
            for (std::size_t j = 0; j < len; ++j)
                in.push_back(static_cast<std::int64_t>(nxt() % 5) - 2);
            Case c(in, fifth(in));
            if (k >= 9) spec.holdout.push_back(c); else spec.cases.push_back(c);
        }
        Oracle oracle = [&fifth](const Value& in) { return fifth(in); };
        const Value outside = {1, 2, 3, 4, 5};          // length 5: outside the domain

        Exhaust ex;
        const BuildResult be = synthesise_exhaustive(spec, 8000, oracle, -2, 2, 4, 6,
                                                     nullptr, &ex);
        const bool e_proved = (be.proof == Proof::Exhaustive);
        const bool e_wrong  = e_proved && be.recipe.apply(outside, nullptr) != fifth(outside);
        std::printf("      exhaustive: proved=%s, and wrong outside the domain=%s\n",
                    e_proved ? "yes" : "no", e_wrong ? "yes" : "no");
        check(e_proved && e_wrong,
              "a bounded proof accepts a program that is wrong outside its bound");

        const BuildResult bh = synthesise_hardened(spec, 8000, oracle, -2, 2, 4, 6,
                                                   nullptr, &ex);
        const bool h_claims = (bh.proof == Proof::Exhaustive);
        const bool h_right  = !h_claims ||
                              bh.recipe.apply(outside, nullptr) == fifth(outside);
        std::printf("      hardened  : claims proof=%s, and right outside=%s\n",
                    h_claims ? "yes" : "no", h_right ? "yes" : "no");
        check(h_right,
              "and the hardened check never claims a proof for one that is not");
    }

    std::printf("\n");
    if (failures == 0) std::printf("ALL PASS\n");
    else               std::printf("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
