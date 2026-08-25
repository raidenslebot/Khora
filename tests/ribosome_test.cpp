// Ribosome test — the properties an evolvable substrate has to have.
//
// This module claims something specific: that a byte tape can be mutated and
// recombined freely and still always be a running program, and that selection
// over such tapes can find a composition of Khora's primitives that nobody
// wrote. Both halves are testable, and the first one is where genetic
// programming usually cheats -- tree representations produce invalid offspring
// that need repair, and the repair rule is a human prior hidden inside the
// search.

#include "khora/ribosome/ribosome.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace khora::ribosome;
using khora::lattice::Glyph;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("  FAIL: %s\n", what); ++failures; }
    else         std::printf("  ok  : %s\n", what);
}

std::uint64_t rs = 0xA11FEULL;
std::uint64_t rnd() {
    std::uint64_t z = (rs += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

} // namespace

int main() {
    std::printf("Ribosome test\n");

    // --- THE DECODER IS TOTAL ------------------------------------------------
    //
    // Every byte string is a program. Not "most" -- every one. If this is false
    // then mutation is not a closed operation and the search needs a repair
    // rule, which is exactly the human prior this design exists to avoid.
    {
        bool all_ran = true;
        const Vm vm;
        for (int trial = 0; trial < 500; ++trial) {
            const std::size_t len = 1 + (rnd() % 24);
            Genome g = Genome::random(len, rnd());
            const auto code = g.decode();
            if (code.size() != len) { all_ran = false; break; }
            const Glyph out = vm.run(g, Glyph::random(rnd()));
            if (out.popcount() > khora::lattice::kGlyphBits) { all_ran = false; break; }
        }
        check(all_ran, "500 random byte tapes all decode and run -- decode is total");
    }

    // --- MUTATION AND CROSSOVER ARE CLOSED ----------------------------------
    {
        const Vm vm;
        Genome g = Genome::random(8, 12345);
        bool ok = true;
        for (int i = 0; i < 500 && ok; ++i) {
            g = g.replicate(rnd(), 0.05);
            if (g.bytes() % 4 != 0) ok = false;                // reading frame held
            if (g.codons() > 0) (void)vm.run(g, Glyph::random(7));
        }
        check(ok, "500 successive replications keep the reading frame intact");
        check(g.codons() > 0, "and the lineage does not mutate itself out of existence");
    }
    {
        const Vm vm;
        bool ok = true;
        for (int i = 0; i < 200 && ok; ++i) {
            Genome a = Genome::random(4 + (rnd() % 8), rnd());
            Genome b = Genome::random(4 + (rnd() % 8), rnd());
            Genome c = Genome::cross(a, b, rnd());
            if (c.bytes() % 4 != 0) ok = false;
            if (c.codons() == 0) ok = false;
            (void)vm.run(c, Glyph::random(3));
        }
        check(ok, "crossover of any two genomes yields a runnable offspring");
    }

    // --- THE VM IS DETERMINISTIC --------------------------------------------
    //
    // Selection is meaningless if the same genome scores differently on
    // re-evaluation.
    {
        const Vm vm;
        Genome g = Genome::random(10, 999);
        const Glyph in = Glyph::random(4242);
        check(vm.run(g, in) == vm.run(g, in), "the same genome on the same input is deterministic");
    }

    // --- ROLE VECTORS ARE STABLE ACROSS ORGANISMS ---------------------------
    //
    // Crossover splices a fragment from one lineage into another. If ROLE[k]
    // meant something different in each genome, that splice would be
    // transferring a symbol with a different referent and heredity would be
    // meaningless.
    {
        check(Vm::role(17) == Vm::role(17), "ROLE[k] is the same vector every time it is read");
        check(Vm::role(17) != Vm::role(18), "and different roles are different vectors");
    }

    // --- HOW MUCH OF A GENOME IS ACTUALLY ALIVE ------------------------------
    //
    // An adversarial audit measured this and it was the deepest problem in the
    // module. With a fixed output register, over 200,000 random 5-codon genomes:
    // mean 1.309 live instructions, 27% pure identity, only 53% producing output
    // that depends on the input at all. The behaviourally distinct space
    // collapsed to roughly 384 programs -- enumerable exhaustively in seconds,
    // which makes calling the search "evolution" dishonest.
    //
    // Reading out the LAST register written instead makes the final instruction
    // live by construction, and liveness propagates back through its sources.
    // This measures the effect rather than assuming it.
    {
        const std::size_t kTrials = 50000;
        std::size_t total_live = 0, identity = 0;
        for (std::size_t t = 0; t < kTrials; ++t) {
            const Genome g = Genome::random(5, rnd());
            const std::size_t e = g.effective_length();
            total_live += e;
            if (e == 0) ++identity;
        }
        const double mean_live = static_cast<double>(total_live) / kTrials;
        std::printf("  random 5-codon genomes: mean %.3f live instructions, %.1f%% pure identity\n",
                    mean_live, 100.0 * identity / static_cast<double>(kTrials));
        check(mean_live > 1.9,
              "most of a genome is expressed, not discarded (fixed-r0 measured 1.309)");
        check(100.0 * identity / static_cast<double>(kTrials) < 12.0,
              "and few genomes are pure identity (fixed-r0 measured 27%)");
    }

    // --- A FLAT LANDSCAPE, AND WHY IT IS FLAT --------------------------------
    //
    // This is kept as a test because it is the finding that redesigned the
    // instruction set, and a module that quietly deleted its own negative
    // result would be lying about how it got here.
    //
    // The relation is to = bind(from, ROLE[57]) -- ONE instruction, well inside
    // what the machine can express. Selection still cannot find it, because in
    // a 10,000-bit XOR space every wrong role is orthogonal to the right one.
    // Wrong-by-a-hair and wrong-entirely score identically at zero. There is no
    // surface to climb, and more compute does not create one.
    {
        std::vector<Assay> train, held;
        for (int i = 0; i < 32; ++i) {
            Assay a;
            a.from = Glyph::random(1000 + i);
            a.to   = khora::lattice::bind(a.from, Vm::role(57));
            (i < 24 ? train : held).push_back(a);
        }
        ChamberConfig cfg;
        cfg.population = 128;
        cfg.genome_codons = 4;
        Chamber ch(cfg, nullptr, 20260824);
        for (int gen = 0; gen < 60; ++gen) (void)ch.step(train);
        const double after = ch.evaluate(ch.best().genome, held);
        std::printf("  orthogonal-role relation: evolved to %.3f of a possible 1.0\n", after);
        check(after < 0.2,
              "a relation with no gradient stays unfound -- the negative result, kept");
    }

    // --- SELECTION FINDS A RELATION IT CAN SENSE -----------------------------
    //
    // Same search, same budget, one difference: the relation now lives in the
    // environment graph rather than in an orthogonal constant. Graph adjacency
    // is graded -- getting three pairs out of twenty right scores 0.15, and
    // that is a foothold. This is what the senses bought.
    {
        Codebook cb;
        for (int i = 0; i < 64; ++i) cb.add("item" + std::to_string(i), Glyph::random(500 + i));
        // Every item points at the one after it. A single `assoc` expresses it.
        for (std::size_t i = 0; i < 64; ++i) cb.link(i, (i + 1) % 64);

        std::vector<Assay> train, held;
        for (std::size_t i = 0; i < 64; ++i) {
            Assay a;
            a.from = cb.at(i);
            a.to   = cb.at((i + 1) % 64);
            a.to_index = (i + 1) % 64;
            (i < 48 ? train : held).push_back(a);
        }

        ChamberConfig cfg;
        cfg.population = 128;
        cfg.genome_codons = 4;
        Chamber ch(cfg, &cb, 20260824);

        const double before = ch.evaluate(Genome::random(4, 1), held);
        for (int gen = 0; gen < 60; ++gen) (void)ch.step(train);
        const double after = ch.evaluate(ch.best().genome, held);

        std::printf("  graph relation: random %.3f -> evolved %.3f on HELD-OUT pairs (%zu births)\n",
                    before, after, ch.births());
        check(after > 0.9, "selection recovers a relation it can sense");
        check(after > before + 0.5, "and that is not where an unselected genome starts");

        std::printf("  what it found:\n%s", ch.best().genome.disassemble().c_str());
    }

    std::printf("\n");
    if (failures == 0) std::printf("ALL PASS\n");
    else               std::printf("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
