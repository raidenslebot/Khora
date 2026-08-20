#pragma once

// The Sdr — Khora's SPARSE substrate, alongside the dense Glyph.
//
// WHY A SECOND CODE EXISTS
//
// A Glyph is ~50% dense, and that is correct for the relational algebra: XOR
// binding is exact, its inverse introduces no noise at all, and the Crucible
// measures structured unbind at 100.00%. Nothing about that path needs fixing.
//
// What a dense code cannot do is SUBSAMPLED MATCHING — storing a small sample
// of a pattern and recognising it from that sample. That is what a dendritic
// segment physically does: ~20 synapses drawn from a much larger active
// population, firing when a threshold of them agree. The false-match rate of
// such a test against unrelated input is P(Binomial(s, density) >= theta). At
// s = 24, theta = 12 and 50% density that is 0.58 — and it stays 0.58 at every
// dimension, from 2,000 bits to 65,536. Widening the vector does not help,
// because a coin-flip bit meets a half-threshold by chance. At 1.5% density the
// same test sits around 1e-17.
//
// Sparsity, not dimensionality, is what makes subsampling work — and subsampling
// is what makes distal context, unions of simultaneous predictions, and
// tolerance of large-scale unit loss possible.
//
// The split is not a compromise; it is the complementary-learning-systems
// boundary. Dentate gyrus runs at 1-4% active and pattern-separates; CA1 runs
// near 40% and overlaps (Chawla et al. 2005, Hippocampus 15:579). Sdr is the
// fast, sparse, separating store. Glyph is the slow, dense, semantic one.
//
// THE CODE
//
// A sparse BLOCK code (Laiho et al. 2015; Frady, Kleyko & Sommer, IEEE TNNLS
// 2023). The n positions are partitioned into B blocks of L, with exactly ONE
// active position per block. Sparsity is therefore structural — it cannot drift,
// and no inhibition step is needed to enforce it, because the block constraint
// IS the k-winners-take-all.
//
// This is not a foreign algebra. At L = 2 a block is one bit, (i+j) mod 2 is
// exactly XOR, and per-block argmax is exactly the majority rule — so the dense
// Glyph algebra is the degenerate case of this one, kept as its own optimised
// implementation.

#include "khora/lattice/glyph.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>
#include <vector>

namespace khora::lattice {

// B = 256 blocks of L = 64. 16,384 positions, 256 of them active: 1.5625%.
//
// L = 64 sits near the knee of two opposing pressures. Storage is one byte per
// block (256 B, against 2 KB for the equivalent dense bitset), and (i+j) mod 64
// is a mask rather than a division. Larger blocks are sparser but degrade the
// signal-to-noise of argmax recovery from a bundle; smaller blocks recover
// better but store and compare more.
inline constexpr std::size_t kSdrBlocks    = 256;
inline constexpr std::size_t kSdrBlockSize = 64;
inline constexpr std::size_t kSdrBits      = kSdrBlocks * kSdrBlockSize;  // 16384
inline constexpr std::size_t kSdrActive    = kSdrBlocks;                  // one per block

static_assert((kSdrBlockSize & (kSdrBlockSize - 1)) == 0,
              "block size must be a power of two so (i+j) mod L is a mask");

class Sdr {
public:
    // One active index per block. The whole representation is 256 bytes.
    using Storage = std::array<std::uint8_t, kSdrBlocks>;

    Sdr() noexcept;                                   // all blocks at index 0
    explicit Sdr(const Storage& s) noexcept;

    static Sdr random(std::uint64_t seed) noexcept;   // uniform index per block
    static Sdr from_hash(std::string_view s) noexcept;

    // Block access. `index(b)` is which position within block b is active;
    // `bit(b)` is that position in absolute [0, kSdrBits) coordinates.
    std::uint8_t index(std::size_t block) const noexcept { return blocks_[block]; }
    void         set_index(std::size_t block, std::uint8_t i) noexcept;
    std::size_t  bit(std::size_t block) const noexcept;

    // Number of blocks agreeing. This is the similarity measure: two unrelated
    // Sdrs agree on B/L = 4.0 blocks on average (sd 1.98), while identical ones
    // agree on all 256.
    std::size_t overlap(const Sdr& other) const noexcept;
    double      similarity(const Sdr& other) const noexcept;  // overlap / kSdrBlocks

    const Storage& blocks() const noexcept { return blocks_; }
    Storage&       blocks()       noexcept { return blocks_; }

    bool operator==(const Sdr& o) const noexcept { return blocks_ == o.blocks_; }
    bool operator!=(const Sdr& o) const noexcept { return !(*this == o); }

private:
    Storage blocks_{};
};

// Binding is per-block modular addition: exactly invertible, and sparsity is
// preserved by construction rather than by correction. Unlike XOR it is not
// self-inverse, so unbind is a separate operation (subtraction).
//
// It is still COMMUTATIVE, so — exactly as with XOR on the dense code — bind
// alone cannot express a DIRECTED relation. bind(a,b) == bind(b,a), and from
// the result either operand recovers the other, so a chain of bound transitions
// is a set of undirected edges and traversal is a coin flip between successor
// and predecessor. Permute one operand first; see whetstone's transition
// encoding, where that defect was measured and fixed.
Sdr bind(const Sdr& a, const Sdr& b) noexcept;
Sdr unbind(const Sdr& bound, const Sdr& b) noexcept;   // recovers a from bind(a,b)

// Permutation for sequence position, as with Glyph: rotate the block ORDER, so
// the result is still exactly one active element per block.
Sdr permute(const Sdr& a, int shift) noexcept;

// Superposition. A bundle must NEVER be binarised while it is still being
// accumulated: argmax over per-position counts is only meaningful once every
// contributor is in. Trace holds those counts; binarise() commits.
class Trace {
public:
    Trace();

    void add(const Sdr& s) noexcept;
    void add(const Sdr& s, std::int16_t weight) noexcept;
    void clear() noexcept;

    // Per-block argmax. Ties resolve to the lowest index, so this is
    // deterministic — the substrate's claim is reproducible reasoning.
    Sdr binarise() const noexcept;

    std::size_t contributors() const noexcept { return contributors_; }
    // Highest count in a block, averaged over blocks: how sharply the bundle
    // resolves. Falls toward chance as the bundle saturates.
    double sharpness() const noexcept;

private:
    std::vector<std::int16_t> counts_;   // kSdrBits
    std::size_t               contributors_ = 0;
};

Sdr bundle(std::span<const Sdr> xs);
Sdr bundle(std::initializer_list<Sdr> xs);

// A UNION of simultaneously active patterns — which is a different thing from a
// bundle, and the block code cannot express it as one.
//
// Trace::binarise() answers "which single pattern best explains these votes",
// by argmax within each block. That is the right question for cleanup, and the
// wrong one for simultaneity: a block holds one winner, so a union of M
// patterns keeps only about 1/M of each member's blocks and a subsampled
// segment stops recognising any of them. Measured: a segment found its own
// pattern in a union of 4 in 1 trial out of 400.
//
// A union is therefore a SET of active positions, one bit per position — which
// is exactly how a set of simultaneously depolarised cells is represented in
// tissue. Since a block is 64 positions wide, that is precisely one uint64 mask
// per block, and membership is a shift and a test.
//
// This is the representation the false-match mathematics was derived for
// (Ahmad & Hawkins 2016): every member's bits are present, so a member is
// always found; the cost is that density grows with M, and with it the
// false-match rate. That trade is the reason a UNION has a capacity limit at
// all, and the reason the sparse code has room for one where a dense code
// does not.
class SdrUnion {
public:
    using Mask = std::array<std::uint64_t, kSdrBlocks>;
    static_assert(kSdrBlockSize == 64, "one uint64 mask per block assumes L = 64");

    SdrUnion() noexcept = default;

    void add(const Sdr& s) noexcept;
    // Set ONE position. Adding a whole Sdr sets a bit in every block, so a
    // union assembled from individual positions -- a set of predicted columns,
    // say -- has to be built this way instead.
    void add_position(std::size_t block, std::uint8_t index) noexcept;
    void clear() noexcept;

    bool contains(std::size_t block, std::uint8_t index) const noexcept {
        return ((mask_[block] >> index) & 1ULL) != 0;
    }
    // How many of `s`'s blocks are present in the union. Equals kSdrBlocks for
    // any pattern that was added.
    std::size_t overlap(const Sdr& s) const noexcept;

    std::size_t active() const noexcept;    // total positions set
    double      density() const noexcept;   // active / kSdrBits
    std::size_t members() const noexcept { return members_; }

    const Mask& mask() const noexcept { return mask_; }

private:
    Mask        mask_{};
    std::size_t members_ = 0;
};

// THE BRIDGE, one-way by design.
//
// A fixed pseudo-random projection from the dense Glyph space to block scores,
// then per-block argmax. This is the mossy-fibre path: dentate gyrus takes
// entorhinal input and re-codes it four times wider and far sparser, which is
// what makes pattern separation possible.
//
// There is deliberately no Sdr -> Glyph. The two codes carry different
// invariants, and a reverse projection is how a two-substrate design decays
// into two half-correct algebras.
Sdr project(const Glyph& g) noexcept;

// A dendritic SEGMENT: a small subsample of an Sdr, matched at a threshold.
//
// This is the whole reason the sparse code exists. A segment stores kSynapses
// (block, expected index) pairs — never the whole pattern — and reports a match
// when at least `theta` of them agree. Because it never sees the full pattern it
// costs almost nothing to test, it survives large-scale loss of the pattern it
// was trained on, and — the property a dense code cannot give — it stays
// selective against a UNION of many simultaneously active patterns.
struct Segment {
    static constexpr std::size_t kSynapses = 24;
    static constexpr std::uint8_t kDefaultTheta = 12;

    std::array<std::uint8_t, kSynapses> block{};   // which block
    std::array<std::uint8_t, kSynapses> index{};   // the index expected there
    std::uint8_t                        count = 0; // synapses actually populated

    // Sample `n` blocks of `s` and record what it has there.
    static Segment learn(const Sdr& s, std::uint64_t seed,
                         std::size_t n = kSynapses) noexcept;

    std::size_t agreement(const Sdr& s) const noexcept;
    bool        matches(const Sdr& s, std::uint8_t theta = kDefaultTheta) const noexcept;

    // Against a union: the same subsample, asking membership instead of
    // equality. This is the overload that matters — recognising one pattern
    // among many simultaneously active ones is the capability the whole sparse
    // substrate was adopted for.
    std::size_t agreement(const SdrUnion& u) const noexcept;
    bool        matches(const SdrUnion& u, std::uint8_t theta = kDefaultTheta) const noexcept;
};

} // namespace khora::lattice
