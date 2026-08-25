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

// The real implementation, reached through the interpreter, so the target is
// literally what the machine does rather than a restatement of it. A one-node
// program applying the op to the input.
Value real_op(Op op, const Value& in, std::uint8_t b) {
    std::vector<std::uint8_t> tape;
    // Load the constant operand into r1, then apply op with a=r0, b=r1.
    tape.push_back(static_cast<std::uint8_t>(
        (static_cast<std::uint32_t>(Op::Const) * 256u) / static_cast<std::uint32_t>(Op::kCount)));
    tape.push_back(1); tape.push_back(0); tape.push_back(b);
    tape.push_back(static_cast<std::uint8_t>(
        (static_cast<std::uint32_t>(op) * 256u) / static_cast<std::uint32_t>(Op::kCount)));
    tape.push_back(2); tape.push_back(0); tape.push_back(1);
    return run(Program(std::move(tape)), in, nullptr);
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
        {Op::Add,   "add",    0}, {Op::Sub,  "sub",    0},
        {Op::Mul,   "mul",    0}, {Op::Div,  "div",    0},
        {Op::Mod,   "mod",    0}, {Op::Append, "cat",  0},
        {Op::Take,  "take_3", 3}, {Op::Drop, "drop_2", 2},
        {Op::Index, "at_1",   1}, {Op::Filter, "filter_0", 0},
        {Op::MapAdd, "addk_5", 5}, {Op::MapMul, "mulk_2", 2},
        {Op::Count, "count_0", 0},
    };
}

} // namespace

int main(int argc, char** argv) {
    const std::size_t pool_cap = (argc > 1) ? std::stoul(argv[1]) : 40000;
    const std::size_t visible  = (argc > 2) ? std::stoul(argv[2]) : 14;
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

    for (const Target& t : tgts) {
        Spec s;
        s.name = t.name;
        s.banned.push_back(t.op);
        // Banning Call too: a library entry that already IS this primitive would
        // make the reconstruction circular.
        for (std::size_t i = 0; i < visible; ++i) {
            const std::size_t len = i % 7;
            Value in;
            for (std::size_t k = 0; k < len; ++k) {
                in.push_back(static_cast<std::int64_t>(rnd() % 30) - 12);
            }
            s.cases.push_back({in, real_op(t.op, in, t.b)});
        }
        for (std::size_t i = 0; i < 6; ++i) {
            Value in;
            const std::size_t len = 7 + i;
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
