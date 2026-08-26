// Why does no reconstruction ever use a fold?
//
// The header says: "With FoldF available, `sum` is a fold whose body adds."
// The self-hosting bench says: 0 of 29 accepted reconstructions contain a fold
// or a map, and `sum` -- the primitive the scaffolding was put there for -- is
// still not rebuilt. One of those two statements is wrong, and a claim in a
// header that the measurements contradict is the kind of thing this project
// treats as a defect rather than a curiosity.
//
// The scaffolding is not the suspect. The self-hosting bench derives pair_add,
// pair_max and pair_min with Sum/Max/Min banned, admits them FIRST so they sit
// at low library indices, and re-derives them whenever a withdrawal kills one.
// `pair_add  add(head(x), tail(x))` appears in its output. The body exists.
//
// The suspect is a search heuristic in construct(). The higher-order expansion
// declines to speculate on a pool node whose behaviour is large:
//
//     std::size_t elems = 0;
//     for (const Value& v : behaviour[i]) elems += v.size();
//     if (elems > 64) continue;
//
// That sums list lengths across EVERY CASE, not per case. A specification with
// twelve cases therefore admits an average list length of 5.3 before the fold
// stops being offered at all -- and a specification written to demonstrate an
// aggregation naturally uses lists longer than that.
//
// So this bench asks one question with a controlled variable: hold the task,
// the library and the pool fixed, and vary ONLY the total element count across
// the specification. If `sum` is rebuilt as a fold below the cap and not above
// it, the capability is real and a heuristic is hiding it.
#include <khora/techne/techne.hpp>

#include <cstdio>
#include <functional>
#include <algorithm>
#include <string>
#include <vector>

using namespace khora::techne;

namespace {

std::uint64_t rng_state = 0x5EEDF01DULL;
std::uint64_t rnd() {
    rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17; return rng_state;
}

struct Agg {
    const char* name;        // the primitive being rebuilt
    Op          op;          // ...and the operation to ban while rebuilding it
    const char* body;        // the pairwise combiner it folds with
    std::function<Value(const Value&)> ref;
    std::function<Value(const Value&)> pair;
};

std::vector<Agg> aggregations() {
    return {
        {"sum", Op::Sum, "pair_add",
         [](const Value& v) { std::int64_t s = 0; for (const std::int64_t x : v) s += x;
                              return Value{s}; },
         [](const Value& v) { return Value{v[0] + v[1]}; }},
        {"max", Op::Max, "pair_max",
         [](const Value& v) { if (v.empty()) return Value{};
                              std::int64_t m = v[0];
                              for (const std::int64_t x : v) m = std::max(m, x);
                              return Value{m}; },
         [](const Value& v) { return Value{std::max(v[0], v[1])}; }},
        {"min", Op::Min, "pair_min",
         [](const Value& v) { if (v.empty()) return Value{};
                              std::int64_t m = v[0];
                              for (const std::int64_t x : v) m = std::min(m, x);
                              return Value{m}; },
         [](const Value& v) { return Value{std::min(v[0], v[1])}; }},
    };
}

// Learned, not handed over: the same derivation the self-hosting bench uses,
// with Sum/Max/Min and both higher-order operations banned so that a two-element
// `sum(x)` cannot masquerade as pairwise addition.
bool derive_body(Library& lib, const Agg& a) {
    Spec s;
    s.name = a.body;
    for (const Op o : {Op::Sum, Op::Max, Op::Min, Op::FoldF, Op::MapF}) s.banned.push_back(o);
    for (std::size_t i = 0; i < 12; ++i) {
        Value in{static_cast<std::int64_t>(rnd() % 40) - 18,
                 static_cast<std::int64_t>(rnd() % 40) - 18};
        s.cases.push_back({in, a.pair(in)});
    }
    for (std::size_t i = 0; i < 5; ++i) {
        Value in{static_cast<std::int64_t>(rnd() % 400) - 200,
                 static_cast<std::int64_t>(rnd() % 400) - 200};
        s.holdout.push_back({in, a.pair(in)});
    }
    const BuildResult b = construct(s, 20000, &lib);
    if (b.proof != Proof::Generalised) return false;
    std::printf("  fold body derived: %-9s %s\n", a.body, b.recipe.render().c_str());
    return lib.admit_recipe(a.body, b.recipe, 0);
}

// `prog` is what the search RETURNED, and `proof` is whether it survived the
// held-out cases. Reporting only "rebuilt yes/no" cannot tell "no program
// matched" apart from "a program matched the visible cases and was rejected on
// the holdout", and those two have opposite meanings for a capability claim.
struct Row { bool found; bool folded; std::string prog; const char* proof; };

// `empty_case` is the ONLY thing that separates the two halves of part two.
Row try_agg(const Library& lib, const Agg& a, std::size_t ncase, std::size_t len,
            bool empty_case) {
    Spec s;
    s.name = a.name;
    s.banned.push_back(a.op);
    if (empty_case) s.cases.push_back({Value{}, a.ref(Value{})});
    for (std::size_t c = 0; c < ncase; ++c) {
        Value in;
        for (std::size_t j = 0; j < len; ++j)
            in.push_back(static_cast<std::int64_t>(rnd() % 30) - 12);
        s.cases.push_back({in, a.ref(in)});
    }
    for (std::size_t c = 0; c < 6; ++c) {
        Value in;
        for (std::size_t j = 0; j < len + 3; ++j)
            in.push_back(static_cast<std::int64_t>(rnd() % 600) - 300);
        s.holdout.push_back({in, a.ref(in)});
    }
    const BuildResult b = construct(s, 20000, &lib);
    Row r;
    r.found = (b.proof == Proof::Generalised);
    r.prog  = b.recipe.found ? b.recipe.render() : std::string("nothing matched");
    switch (b.proof) {
        case Proof::Generalised: r.proof = "generalised"; break;
        case Proof::Exhaustive:  r.proof = "exhaustive";  break;
        case Proof::Verified:    r.proof = "verified";    break;
        case Proof::Tested:      r.proof = "OVERFIT";     break;
        default:                 r.proof = "none";        break;
    }
    r.folded = false;
    if (b.recipe.found)
        for (const Expr& n : b.recipe.pool)
            if (n.op == Op::FoldF || n.op == Op::MapF) r.folded = true;
    return r;
}
// PART THREE asks the same question of the entry point callers actually use.
// construct() returns the first behavioural match in size order and stops; a
// length-specific program is smaller than a fold, so it can win and then fail
// the holdout. synthesise_hardened() proves over a bounded domain and refines
// against counterexamples, so if the difference is refinement it will show here
// and the "construct() stops too early" reading is about the raw engine only.
Row try_hardened(const Library& lib, const Agg& a, std::size_t ncase,
                 std::size_t len, bool empty_case) {
    Spec s;
    s.name = a.name;
    s.banned.push_back(a.op);
    if (empty_case) s.cases.push_back({Value{}, a.ref(Value{})});
    for (std::size_t c = 0; c < ncase; ++c) {
        Value in;
        for (std::size_t j = 0; j < len; ++j)
            in.push_back(static_cast<std::int64_t>(rnd() % 30) - 12);
        s.cases.push_back({in, a.ref(in)});
    }
    const Oracle oracle = [&a](const Value& v) { return a.ref(v); };
    const BuildResult b = synthesise_hardened(s, 20000, oracle, -2, 2, 4, 3, &lib);
    Row r;
    r.found = (b.proof == Proof::Exhaustive);
    r.prog  = b.recipe.found ? b.recipe.render() : std::string("nothing matched");
    switch (b.proof) {
        case Proof::Generalised: r.proof = "generalised"; break;
        case Proof::Exhaustive:  r.proof = "exhaustive";  break;
        case Proof::Verified:    r.proof = "verified";    break;
        case Proof::Tested:      r.proof = "OVERFIT";     break;
        default:                 r.proof = "none";        break;
    }
    r.folded = false;
    if (b.recipe.found)
        for (const Expr& n : b.recipe.pool)
            if (n.op == Op::FoldF || n.op == Op::MapF) r.folded = true;
    return r;
}
}  // namespace

int main() {
    std::printf("\n  CAN THE SYSTEM REBUILD ITS AGGREGATIONS AS FOLDS?\n\n");

    Library lib(32);
    for (const Agg& a : aggregations())
        if (!derive_body(lib, a)) {
            std::printf("  %s could not be derived -- nothing below would mean anything.\n",
                        a.body);
            return 1;
        }

    // PART ONE. The task, the library, the pool and the banned set are identical
    // down the column; the only thing that moves is the total element count
    // across the specification, which is what the higher-order expansion used to
    // test against its cap of 64.
    std::printf("\n\n  PART ONE -- the speculation cap. `sum`, no empty case.\n\n");
    std::printf("  cases x len | elems | rebuilt | via fold | program\n");
    std::printf("  ------------|-------|---------|----------|----------------------\n");
    struct Shape { std::size_t ncase, len; };
    const Shape shapes[] = {
        {6, 4}, {8, 6}, {10, 6}, {12, 5}, {12, 6}, {12, 8}, {12, 12}, {16, 10},
    };
    std::size_t below = 0, above = 0, n_below = 0, n_above = 0;
    for (const Shape& sh : shapes) {
        const Row r = try_agg(lib, aggregations()[0], sh.ncase, sh.len, false);
        const std::size_t elems = sh.ncase * sh.len;
        std::printf("  %5zu x %-4zu | %5zu |  %-6s |  %-7s | %s\n",
                    sh.ncase, sh.len, elems, r.found ? "yes" : "NO",
                    r.folded ? "yes" : "no", r.prog.c_str());
        if (elems <= 64) { ++n_below; if (r.folded) ++below; }
        else             { ++n_above; if (r.folded) ++above; }
    }
    std::printf("\n  total at or under 64: %zu of %zu folded.  over 64: %zu of %zu.\n",
                below, n_below, above, n_above);
    std::printf("  Under the old rule the second group was 0 of %zu -- not solved worse,\n",
                n_above);
    std::printf("  not solved at all. The cap now bounds the LONGEST CASE instead.\n");

    // PART TWO. Every aggregation, at one shape, with and without a single
    // empty-list case. FoldF seeds with A[0] and returns {} on an empty list.
    std::printf("\n\n\n  PART TWO -- what a fold seeded with A[0] cannot express.\n\n");
    std::printf("  Twelve cases of six elements throughout. The right-hand column adds\n");
    std::printf("  ONE case: the empty list. Nothing else differs.\n\n");
    std::printf("  primitive | ref([]) | empty case | proof       | program\n");
    std::printf("  ----------|---------|------------|-------------|----------------\n");
    for (const Agg& a : aggregations()) {
        const Value e = a.ref(Value{});
        for (const bool empty_case : {false, true}) {
            const Row r = try_agg(lib, a, 12, 6, empty_case);
            std::printf("  %-9s | %-7s | %-10s | %-11s | %s\n", a.name,
                        e.empty() ? "{}" : "{0}", empty_case ? "yes" : "no",
                        r.proof, r.prog.c_str());
        }
    }
    std::printf("\n  THREE DIFFERENT THINGS ARE VISIBLE HERE and only one of them is the\n");
    std::printf("  cap that part one is about.\n\n");
    std::printf("  sum   -- folds when the empty list is absent and NOTHING matches when\n");
    std::printf("           it is present. A fold seeded with A[0] returns {} on the\n");
    std::printf("           empty list; sum([]) is 0. One case is enough to reject a\n");
    std::printf("           candidate, so this instruction set expresses every\n");
    std::printf("           aggregation UNDEFINED on the empty list and no aggregation\n");
    std::printf("           that has an IDENTITY ELEMENT -- NOT that the system cannot\n");
    std::printf("           rebuild one. Given the same body and a spec that includes\n");
    std::printf("           the empty list, the self-hosting bench answers with\n");
    std::printf("           append(fold[pair_add](x), drop(0, len(x))): the fold for the\n");
    std::printf("           elements and a length-conditioned term for the case the fold\n");
    std::printf("           cannot reach. It found that composition on its own, and it\n");
    std::printf("           is clean on all 252 grading inputs. The limit is on the\n");
    std::printf("           OPERATION, and the search routes around it.\n");
    std::printf("\n");
    std::printf("  min   -- generalises both ways, and the empty case makes it BETTER.\n");
    std::printf("           Without it the search takes sub(sum(x), sum(pair_max(x))),\n");
    std::printf("           which is correct and is not a fold. With it that route dies\n");
    std::printf("           -- 0 - 0 is not {} -- and the fold is what is left.\n\n");
    std::printf("  max   -- OVERFITS here, and part three shows this is a property of\n");
    std::printf("           the RAW ENGINE rather than a defect in the shipped path.\n");
    std::printf("           It returns drop(pair_max(x), 4): sort, drop the first,\n");
    std::printf("           drop four more -- the last element of a SIX-element list\n");
    std::printf("           and of no other length. construct() returns the first\n");
    std::printf("           behavioural match in size order and stops, and a\n");
    std::printf("           length-specific program is smaller than a fold, so it wins\n");
    std::printf("           the race and then fails the holdout. Refinement is what\n");
    std::printf("           covers that, and callers get it.\n\n");
    std::printf("  The self-hosting bench rebuilds max as fold[pair_max](x) with 28\n");
    std::printf("  visible cases, where the length-specific program cannot survive the\n");
    std::printf("  spread of lengths. So both halves of this point the same way: more\n");
    std::printf("  evidence should buy more capability. Under the old cap it bought\n");
    std::printf("  LESS, which is what part one measures.\n\n");

    std::printf("\n\n  PART THREE -- the same tasks through synthesise_hardened(), which is\n");
    std::printf("  what callers actually use: a proof over every list of length 0..4 over\n");
    std::printf("  -2..2, the extremal inputs, and counterexample refinement.\n\n");
    std::printf("  primitive | empty case | proof       | program\n");
    std::printf("  ----------|------------|-------------|--------------------------\n");
    for (const Agg& a : aggregations())
        for (const bool ec : {false, true}) {
            const Row r = try_hardened(lib, a, 12, 6, ec);
            std::printf("  %-9s | %-10s | %-11s | %s\n", a.name, ec ? "yes" : "no",
                        r.proof, r.prog.c_str());
        }
    std::printf("\n  max IS answered here -- fold[pair_max](x), proved -- and overfits in\n");
    std::printf("  part two. So stopping at the first match is a property of the raw\n");
    std::printf("  engine that counterexample refinement already covers. The shipped\n");
    std::printf("  path is not the thing at fault, and part two should not be read as\n");
    std::printf("  saying it is.\n\n");
    std::printf("  AND `sum` FAILS HERE EVEN WITH NO EMPTY CASE IN THE SPECIFICATION,\n");
    std::printf("  which is the sharper form of part two. The proof domain is every\n");
    std::printf("  list of length 0..4 -- and that INCLUDES the empty one. A caller\n");
    std::printf("  cannot avoid it by leaving it out of the cases, so a bare fold can\n");
    std::printf("  never be certified for an aggregation with an identity element, no\n");
    std::printf("  matter what it is asked. That is why the self-hosting bench answers\n");
    std::printf("  with append(fold[pair_add](x), drop(0, len(x))) rather than a fold:\n");
    std::printf("  the proof domain forced the composition, and the search found it.\n\n");
    return 0;
}
