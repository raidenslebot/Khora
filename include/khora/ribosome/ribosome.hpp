#pragma once

// Ribosome — the organ that BUILDS.
//
// Khora can perceive (Plexus, Lexicon, ContextTree, TemporalMemory), it can act
// (Carapace, 95 tools), and it can contain (Bulwark). Nothing in it can
// CONSTRUCT. Every faculty it has, a human wrote. This is the organ that emits a
// capability that did not previously exist.
//
// WHAT IT ACTUALLY DOES, stated plainly before any of the framing below.
//
// A genome is a tape of bytes. It decodes to a short program over Khora's own
// hyperdimensional primitives -- bind, bundle, permute, cleanup. A population of
// these programs is held under a hard budget, replicated with error, and
// selected on whether the program computes a relation the system has observed
// but cannot yet perform. What survives is a new compound operator, expressed in
// the system's native algebra, that nobody designed.
//
// WHY THAT IS THE INTERESTING TARGET.
//
// Every VSA system in the literature hand-designs its composition. To encode a
// role-filler pair you bind with a role vector; to encode a sequence you permute;
// to encode a set you bundle. Which primitive, in which order, with which role
// vector, is always a human decision. That decision IS the architecture, and it
// has never been searched -- it has only ever been chosen.
//
// So the thing worth evolving is not a better predictor. It is the composition
// itself. Given pairs of words standing in some relation the corpus exhibits,
// find the program that carries the first to the second, and find it in a form
// that GENERALISES to pairs it was never shown.
//
// THE FOUR PILLARS, mapped honestly onto what is actually buildable here.
//
//   DNA data storage      The genome is a linear byte tape and the decoder is
//                         TOTAL -- every possible byte string decodes to a
//                         running program. This is not a convenience. In tree-
//                         based genetic programming, crossover produces invalid
//                         structures that need repair, and the repair is a human
//                         prior smuggled into the search. A total decoder over a
//                         linear tape needs no repair, so mutation and crossover
//                         are closed operations exactly as they are in DNA. Any
//                         mutation yields an organism that is viable or dead,
//                         never one that is malformed.
//
//   Biocomputing          The substrate is not a generic CPU. The registers hold
//                         Glyphs -- 10,000-bit hypervectors -- and the opcodes
//                         are the operations Khora's tissue already performs.
//                         An organism computes IN the representation, not about
//                         it.
//
//   Synthetic genomics    Replication copies the tape with a per-byte error
//                         rate. The genome is the whole organism; there is no
//                         phenotype held separately, so what replicates is
//                         exactly what was selected.
//
//   Directed evolution    Selection is continuous rather than generational, in
//                         the PACE sense: there is one chamber, one fixed
//                         resource budget, and organisms are displaced by
//                         better ones as they arrive rather than at a
//                         generation boundary. An organism that fails the assay
//                         is starved out.
//
// CONTAINMENT, and this is a real claim rather than a gesture at Bulwark. The VM
// has a fixed register file, no memory addressing, no I/O, and a hard
// instruction ceiling derived from the tape length. It cannot loop, allocate,
// or reach anything outside itself. Containment is by construction, which is
// stronger than a sandbox, and it is why this stage does not use Bulwark at all.
// Bulwark becomes necessary at the stage where organisms call Carapace tools and
// touch the world; claiming it now would be claiming a safeguard that is not
// doing any work.
//
// WHAT WOULD MAKE THIS FAIL, named up front. The instruction set is the whole
// design. Too expressive and the search space is hopeless; too narrow and the
// population can only rediscover what was handed to it. The honest test is the
// one in the bench: a hand-designed VSA role vector is the standard answer to
// this problem and is strong. If evolution cannot beat it, this organ is
// ceremony and the module should say so.

#include "khora/lattice/glyph.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace khora::ribosome {

// ---------------------------------------------------------------------------
// The instruction set.
//
// Four bytes per codon: (opcode, dst, a, b). Every field is masked into range on
// decode, so there is no such thing as an invalid codon -- which is what makes
// mutation a closed operation.
// ---------------------------------------------------------------------------
enum class Op : std::uint8_t {
    Nop = 0,
    Copy,      // dst = a
    Bind,      // dst = bind(a, b)            -- XOR, self-inverse, commutative
    Bundle,    // dst = bundle({a, b})        -- majority vote
    Perm,      // dst = permute(a, b - 128)   -- the only directional primitive
    Role,      // dst = bind(a, role[b])      -- bind with one of 256 fixed roles
    And,       // dst = a & b
    Or,        // dst = a | b
    Clean,     // dst = nearest item in the cleanup memory to a
    // ---- SENSES. The organism reaches into the world model. -----------------
    Assoc,     // dst = the b-th associate, in the environment graph, of the
               //       item nearest a
    Neigh,     // dst = bundle of the top (b mod 8) + 1 associates of that item
    Common,    // dst = the item most linked-to by BOTH neighbourhoods, a and b
    Kin,       // dst = the item whose neighbourhood most overlaps a's
    kCount
};

// INTERSECTION WAS AN ARBITRARY OMISSION, NOT A RESTRICTION.
//
// The first instruction set gave neighbourhood UNION (Neigh) and no
// intersection. That is not a principled limit -- intersection is the exact dual
// of union, and both are ordinary graph primitives. Leaving one out silently
// decided which relations were reachable.
//
// Common and Kin close it. Kin in particular is second-order distributional
// similarity: the item whose company most resembles yours. It is a strong
// primitive, so the bench runs it as its OWN BASELINE as well as an opcode. If
// the evolved program merely rediscovers Kin, the baseline scores the same and
// the honest report is "evolution found a primitive it was handed", not "the
// composition works".

// WHY THE SENSES HAD TO BE ADDED, and it was a measured failure rather than a
// design preference.
//
// The first instruction set was closed: bind, bundle, permute, cleanup, and 256
// fixed role vectors. On a synthetic relation expressible in ONE instruction --
// to = bind(from, ROLE[57]) -- selection found nothing at all. 2,048 births,
// held-out similarity 0.030 where 1.0 was available.
//
// The reason is a property of the representation, not of the search. In a
// 10,000-bit XOR space bind(a, ROLE[57]) and bind(a, ROLE[58]) are orthogonal.
// Every wrong role scores exactly zero, indistinguishably from every wrong
// program. The landscape is perfectly flat with a single invisible needle, and
// no amount of selection pressure climbs a flat surface.
//
// That generalises past the synthetic case and is the more important finding: a
// program over random atomic hypervectors CANNOT express a semantic relation,
// because the atoms carry no structure to exploit. Two words that mean similar
// things have orthogonal glyphs. Whatever relates them lives in Khora's graph,
// not in its vectors.
//
// So the organism was a calculator with a closed register file, and no closed
// calculator can find this. It needed to be able to SENSE. Assoc and Neigh read
// the environment, and because graph adjacency is graded rather than orthogonal,
// they are the first opcodes whose outputs carry a gradient at all.

struct Codon {
    Op            op = Op::Nop;
    std::uint8_t  dst = 0;
    std::uint8_t  a   = 0;
    std::uint8_t  b   = 0;
};

// Four, not eight. Every extra register multiplies the size of the needle a
// blind search has to hit -- a one-instruction solution has to name the right
// opcode AND the right destination AND the right source, so the register count
// enters the odds squared. Four still permits composition. This is a search-
// budget decision, not a claim about capacity.
inline constexpr std::size_t kRegisters = 4;

// ---------------------------------------------------------------------------
// Genome: a linear tape. This is the entire organism.
// ---------------------------------------------------------------------------
class Genome {
public:
    Genome() = default;
    explicit Genome(std::vector<std::uint8_t> tape) : tape_(std::move(tape)) {}

    // A random tape of `codons` codons. Random genomes are also the dumb
    // baseline the evolved population has to beat.
    static Genome random(std::size_t codons, std::uint64_t seed);

    // Decode is TOTAL: every byte string is a program.
    std::vector<Codon> decode() const;

    // Replication with error. Point substitutions at `rate` per byte, plus
    // occasional indels, which are what let a genome change LENGTH -- a
    // substitution-only mutation operator can never grow or shrink a program.
    Genome replicate(std::uint64_t seed, double rate) const;

    // Recombination: one crossover point, chosen per parent so that offspring
    // length varies. Both parents keep their own tapes.
    static Genome cross(const Genome& x, const Genome& y, std::uint64_t seed);

    const std::vector<std::uint8_t>& tape() const noexcept { return tape_; }
    std::size_t codons() const noexcept { return tape_.size() / 4; }
    std::size_t bytes()  const noexcept { return tape_.size(); }

    // Human-readable disassembly. The point of evolving a program rather than
    // fitting weights is that the result can be READ, so this is part of the
    // contract, not a debugging aid.
    //
    // Every line is marked live or dead. Only register 0 is the output, so an
    // instruction matters only if its result reaches r0 -- and in practice most
    // do not. Reading an evolved genome WITHOUT that marking is how a champion
    // that ignores its own input gets mistaken for an operator: the first
    // hypernym winner here looked like eight instructions of machinery and was
    // a constant. Liveness is computed by walking backwards from r0.
    std::string disassemble() const;

    // Which instructions actually reach the output, and how many there are.
    std::vector<bool> live_mask() const;
    std::size_t effective_length() const;

private:
    std::vector<std::uint8_t> tape_;
};

// ---------------------------------------------------------------------------
// Cleanup memory: the item codebook.
//
// Iterated binding degrades a hypervector; without a cleanup step a composition
// of more than two or three operations returns noise. Every VSA system has one.
// Here it is also an environment the organism can reach into -- Clean is the one
// opcode whose result depends on what Khora knows rather than only on the
// register file.
// ---------------------------------------------------------------------------
class Codebook {
public:
    void add(std::string name, const lattice::Glyph& g);
    std::size_t size() const noexcept { return items_.size(); }

    // The environment graph: which items an item is related to, in the world
    // model. In the bench this is Plexus adjacency; in the test it is a known
    // synthetic relation. Ribosome does not link Plexus -- the environment is
    // handed in, so the organ stays a substrate rather than a consumer of one
    // particular world model.
    void link(std::size_t from, std::size_t to);
    const std::vector<std::uint32_t>& links(std::size_t i) const;

    // Second-order neighbours: for each item, the item whose neighbourhood most
    // overlaps it. Built once from the reverse index, so it costs
    // O(edges * average in-degree) rather than O(items^2).
    // Class labels, used only for set-valued scoring. The codebook already owns
    // the item table, so it is the natural place to hang them.
    void set_class(std::size_t i, int c);
    int  class_of(std::size_t i) const;

    void precompute_kin();
    std::size_t kin(std::size_t i) const;

    // The item most linked-to by both neighbourhoods, or npos.
    std::size_t common(std::size_t i, std::size_t j) const;

    // Index of the nearest item, or npos when empty.
    //
    // An EXACT hit short-circuits the scan. This is not only an optimisation:
    // Assoc, Neigh and Clean all return codebook glyphs verbatim, and the
    // inputs are codebook glyphs, so in a typical organism most lookups are
    // exact and the linear scan over 10,000-bit vectors is pure waste. Without
    // the short-circuit the bench does not finish.
    std::size_t nearest_index(const lattice::Glyph& q) const;

    // Nearest item by similarity. Returns the input unchanged when empty.
    const lattice::Glyph& nearest(const lattice::Glyph& q) const;
    std::string_view      nearest_name(const lattice::Glyph& q) const;
    const lattice::Glyph& at(std::size_t i) const { return items_[i].second; }
    std::string_view      name_at(std::size_t i) const { return items_[i].first; }

private:
    std::vector<std::pair<std::string, lattice::Glyph>> items_;
    std::vector<std::vector<std::uint32_t>> adj_;
    std::unordered_map<std::uint64_t, std::uint32_t> exact_;  // glyph hash -> slot

    // Memo for the scan. Cleanup is a deterministic function of the query, and
    // a population of related genomes recomputes the same intermediates
    // constantly -- the same input word through the same program prefix, across
    // hundreds of organisms and hundreds of generations. Keyed on a 64-bit
    // digest and capped, so at the cap a collision has probability ~1e-9 and
    // would cost one mis-scored pair, never a structurally wrong result.
    mutable std::unordered_map<std::uint64_t, std::uint32_t> memo_;
    std::vector<std::uint32_t> kin_;
    std::vector<int> class_;
};

// ---------------------------------------------------------------------------
// The VM. Bounded by construction: fixed registers, no addressing, no I/O, and
// at most one pass over the tape.
// ---------------------------------------------------------------------------
class Vm {
public:
    explicit Vm(const Codebook* cleanup = nullptr) : cleanup_(cleanup) {}

    // Run `g` with R0 = input. Returns R0 at halt.
    lattice::Glyph run(const Genome& g, const lattice::Glyph& input) const;

    // The 256 fixed role vectors. Deterministic across runs and across
    // organisms, so a role index means the same thing in every genome -- which
    // is what allows crossover to transfer a discovered role between lineages.
    static const lattice::Glyph& role(std::uint8_t i);

private:
    const Codebook* cleanup_;
};

// ---------------------------------------------------------------------------
// The chamber: a population under a fixed budget, continuously selected.
// ---------------------------------------------------------------------------
struct ChamberConfig {
    std::size_t population   = 256;   // the hard budget -- the chamber's volume
    std::size_t genome_codons = 8;    // initial program length
    double      mutation_rate = 0.02; // per byte, per replication
    double      crossover     = 0.5;  // share of offspring from two parents
    std::size_t tournament    = 4;    // selection pressure

    // Training pairs drawn per fitness evaluation, 0 for all of them. A
    // fluctuating sample is standard in evolutionary computation and it is not
    // only a speed measure: scoring every organism on the identical fixed set
    // rewards operators that happen to suit that set, and resampling makes the
    // pressure be about the relation rather than about the sample. Held-out
    // scoring always uses every pair.
    // MEASURED: 96 was catastrophic. 294 of 300 initial organisms score ZERO
    // hits out of 96 sampled pairs, so their entire fitness is a 1e-6
    // similarity tiebreak of random sign and the tournament is a coin flip.
    // Even the best available operator draws zero hits 81% of the time.
    // Separating the difference this experiment cares about at 2 sigma needs
    // ~34,000 samples; 96 is a 350-fold shortfall. 0 means the full training
    // set, which after the VM was profiled is also the CHEAPER option.
    std::size_t sample = 0;
};

// One (input, target) pair the population is selected against. `to_index` is
// the target's slot in the codebook, which is what accuracy is scored against.
struct Assay {
    lattice::Glyph from;
    lattice::Glyph to;
    std::size_t    to_index = static_cast<std::size_t>(-1);
    std::size_t    from_index = static_cast<std::size_t>(-1);

    // SET-VALUED TARGET. When >= 0, an answer is correct if it lands on ANY
    // item of this class rather than on one designated item.
    //
    // This field exists because its absence made the whole experiment measure
    // the wrong thing. Co-hyponymy is set-valued -- any sibling is right -- but
    // the chamber was selecting on one designated sibling while the bench
    // REPORTED same-category accuracy. An exhaustive scan of all 384
    // one-instruction programs showed the two objectives are anti-correlated
    // over the range that matters: the instruction the chamber preferred scored
    // 1.62% on the reported metric, while the one it rejected scored 3.36%.
    // Selection was working perfectly on a target nobody wanted.
    int            to_class = -1;
};

struct Organism {
    Genome genome;
    double fitness = -1.0;
    std::size_t age = 0;
};

class Chamber {
public:
    explicit Chamber(ChamberConfig cfg, const Codebook* cleanup, std::uint64_t seed);

    // Score every organism against `train` and run one round of continuous
    // replacement. Returns the best fitness seen this round.
    double step(const std::vector<Assay>& train);

    // FITNESS IS CLEANUP ACCURACY, not raw similarity, and the difference is the
    // one that decides whether selection has anything to climb.
    //
    // Mean similarity to the target is a flat signal here: a program that lands
    // on the wrong item scores ~0 whether it was nearly right or entirely
    // random, because distinct hypervectors are orthogonal. Accuracy over a SET
    // of pairs is graded instead -- an operator that carries three pairs out of
    // twenty scores 0.15, and that is a foothold. Partial credit comes from
    // being right about part of the world rather than from being close in a
    // space where closeness does not exist.
    //
    // Similarity is kept as a thousandth-weight tiebreak so that a population
    // which has not yet got a single pair right is not perfectly flat.
    double evaluate(const Genome& g, const std::vector<Assay>& pairs) const;

    const Organism& best() const noexcept { return best_; }
    std::size_t generations() const noexcept { return generations_; }
    std::size_t births() const noexcept { return births_; }

private:
    ChamberConfig cfg_;
    const Codebook* cleanup_;
    std::vector<Organism> pop_;
    Organism best_;
    std::uint64_t rng_;
    std::size_t generations_ = 0;
    std::size_t births_ = 0;
    double best_full_ = -1e9;   // the champion's score on the FULL training set

    std::uint64_t next_rand();
    std::size_t select_parent();
    double evaluate_(const Genome& g, const std::vector<Assay>& pairs,
                     std::size_t sample, std::uint64_t seed) const;
};

} // namespace khora::ribosome
