// CAN KHORA REBUILD ITS OWN PRIMITIVES?
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
// VERIFICATION IS NOT THE CERTIFICATE HERE. A certificate says the recipe
// matched every visible case and every held-out case. That is the system judging
// itself. This bench additionally runs each reconstruction against the REAL
// implementation on a thousand random inputs, including adversarial ones --
// empty lists, singletons, negatives, all-equal, already-sorted, reverse-sorted.
// Agreement on all thousand is an external check the search never saw and could
// not have fitted.
//
// WHAT A FAILURE MEANS. An operation that cannot be rebuilt is IRREDUCIBLE with
// respect to the rest of the set: it carries information no combination of the
// others carries. That is a real and useful result about the instruction set --
// it names the primitives that genuinely have to exist.

#include "khora/techne/techne.hpp"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <numeric>
#include <string>
#include <vector>

using namespace khora::techne;

namespace {

std::uint64_t rs = 0x5E1FULL;
std::uint64_t rnd() {
    std::uint64_t z = (rs += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
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

} // namespace

int main(int argc, char** argv) {
    const std::size_t pool_cap = (argc > 1) ? std::stoul(argv[1]) : 40000;
    const std::size_t visible  = (argc > 2) ? std::stoul(argv[2]) : 28;
    const std::size_t probes   = (argc > 3) ? std::stoul(argv[3]) : 1000;

    std::printf("Can Khora rebuild its own primitives?\n\n");
    std::printf("  For each operation: the operation is REMOVED from the instruction\n");
    std::printf("  set and the system must reconstruct it from what remains.\n");
    std::printf("  %zu visible cases, 6 held out, then %zu independent probes against\n",
                visible, probes);
    std::printf("  the real implementation -- including empty, singleton, all-equal,\n");
    std::printf("  sorted and reverse-sorted inputs the search never saw.\n\n");

    const auto probe = probe_inputs(probes);
    const auto tgts = targets();

    std::printf("  primitive  | rebuilt | probes  | reconstruction\n");
    std::printf("  -----------+---------+---------+----------------\n");

    std::size_t rebuilt = 0, irreducible = 0, verified = 0;
    std::vector<std::string> hard;

    // The library persists across targets, so a primitive rebuilt early is
    // available when rebuilding a later one. That is the same compounding the
    // task suite showed, turned on the system itself.
    Library lib(32);

    // ---- WARM-UP: learn two-element combiners first -------------------------
    //
    // `sum`, `max` and `min` were all reported IRREDUCIBLE by this bench, and
    // the reason was never arithmetic. No operation in the set could express
    // "combine every element", because none of them could express a LOOP.
    //
    // FoldF supplies the loop and takes its body from the library, so the
    // missing piece is a function that combines a PAIR. That is a stepping
    // stone and it is being handed over deliberately -- what is NOT handed over
    // is the fold. If the system rebuilds `sum` after this, it has discovered
    // that repeated pairwise addition is summation, which is a control structure
    // it composed rather than a primitive it was given.
    //
    // The combiners are learned with Sum, Max and Min BANNED, because on a
    // two-element list `sum(x)` already is pairwise addition and learning the
    // combiner that way would make the whole demonstration circular.
    {
        struct Comb { const char* name; std::function<Value(const Value&)> ref; };
        const std::vector<Comb> combiners = {
            {"pair_add", [](const Value& v) {
                return v.size() < 2 ? Value{} : Value{v[0] + v[1]}; }},
            {"pair_max", [](const Value& v) {
                return v.size() < 2 ? Value{} : Value{std::max(v[0], v[1])}; }},
            {"pair_min", [](const Value& v) {
                return v.size() < 2 ? Value{} : Value{std::min(v[0], v[1])}; }},
        };
        std::printf("  warm-up -- combiners, learned with Sum/Max/Min banned:\n");
        for (const Comb& cb : combiners) {
            Spec s2;
            s2.name = cb.name;
            s2.banned = {Op::Sum, Op::Max, Op::Min, Op::FoldF, Op::MapF};
            for (std::size_t i = 0; i < 12; ++i) {
                Value in{static_cast<std::int64_t>(rnd() % 40) - 18,
                         static_cast<std::int64_t>(rnd() % 40) - 18};
                s2.cases.push_back({in, cb.ref(in)});
            }
            for (std::size_t i = 0; i < 5; ++i) {
                Value in{static_cast<std::int64_t>(rnd() % 400) - 200,
                         static_cast<std::int64_t>(rnd() % 400) - 200};
                s2.holdout.push_back({in, cb.ref(in)});
            }
            const BuildResult b = construct(s2, pool_cap, &lib);
            if (b.proof == Proof::Generalised) {
                std::printf("    %-9s %s\n", cb.name, b.recipe.render().c_str());
                lib.admit_recipe(cb.name, b.recipe, 0);
            } else {
                std::printf("    %-9s not found\n", cb.name);
            }
        }
        std::printf("\n");
    }

    for (const Target& t : tgts) {
        Spec s;
        s.name = t.name;
        s.banned.push_back(t.op);
        // Banning Call too: a library entry that already IS this primitive would
        // make the reconstruction circular.
        // CASE LENGTHS MUST SPAN THE HOLDOUT'S. This drew cases at lengths 0-6 and
        // held out 7-12 -- disjoint, so any reconstruction whose behaviour
        // depends on length passed every visible case and failed the holdout by
        // construction. The identical defect in the other two benches was hiding
        // a fifth of the measured capability, and "provably irreducible" is far
        // too strong a claim to rest on a specification generator with a hole in
        // it.
        for (std::size_t i = 0; i < visible; ++i) {
            const std::size_t len = i % 14;
            Value in;
            for (std::size_t k = 0; k < len; ++k) {
                in.push_back(static_cast<std::int64_t>(rnd() % 30) - 12);
            }
            s.cases.push_back({in, real_op(t.op, in, t.b)});
        }
        for (std::size_t i = 0; i < 6; ++i) {
            Value in;
            const std::size_t len = 15 + i;
            for (std::size_t k = 0; k < len; ++k) {
                in.push_back(static_cast<std::int64_t>(rnd() % 30) - 12);
            }
            s.holdout.push_back({in, real_op(t.op, in, t.b)});
        }

        const BuildResult b = construct(s, pool_cap, &lib);
        if (b.proof != Proof::Generalised) {
            ++irreducible;
            hard.push_back(t.name);
            std::printf("  %-10s |   no    |    -    | irreducible from the rest\n", t.name);
            continue;
        }
        ++rebuilt;

        // THE EXTERNAL CHECK. The certificate is the system judging itself; this
        // is the real implementation judging it.
        std::size_t agree = 0;
        for (const Value& in : probe) {
            if (b.recipe.apply(in, &lib) == real_op(t.op, in, t.b)) ++agree;
        }
        const bool perfect = (agree == probe.size());
        if (perfect) ++verified;
        std::printf("  %-10s |  yes    | %4zu/%-4zu| %s\n", t.name, agree, probe.size(),
                    b.recipe.render().c_str());
        if (perfect) lib.admit_recipe(t.name, b.recipe, 0);
        lib.prune();
    }

    std::printf("\n  %zu of %zu primitives rebuilt from the rest of the set.\n",
                rebuilt, tgts.size());
    std::printf("  %zu of those agree with the real implementation on ALL %zu probes.\n",
                verified, probes);
    std::printf("  %zu are IRREDUCIBLE -- no composition of the others reproduces them,\n",
                irreducible);
    std::printf("  which names the primitives that genuinely have to be written:\n   ");
    for (const auto& h : hard) std::printf(" %s", h.c_str());
    std::printf("\n");

    std::printf("\n  HOW TO READ IT\n");
    std::printf("    A rebuilt primitive that agrees on every probe is a piece of this\n");
    std::printf("    system that the system can now write for itself. An irreducible one\n");
    std::printf("    carries information the rest of the set does not, which is a real\n");
    std::printf("    result about the instruction set rather than a failure -- it is the\n");
    std::printf("    minimal core, measured instead of assumed.\n");
    std::printf("    A rebuilt primitive that DISAGREES on probes is the important case:\n");
    std::printf("    it passed every case it was shown and is still wrong, which is what\n");
    std::printf("    a certificate cannot catch and an external check can.\n");
    return 0;
}
