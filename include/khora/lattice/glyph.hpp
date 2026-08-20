#pragma once

// The Glyph — atomic unit of the Morphic Lattice.
//
// A DENSE binary hypervector, ~50% of bits set, representing a unit of meaning.
// It is dense on purpose: XOR binding is exact and its inverse is noiseless,
// which is what makes the relational algebra work. For anything needing
// subsampled matching, unions, or pattern separation, see khora/lattice/sdr.hpp
// -- a dense code cannot do those at any dimension.
//
// Closed under the algebra { bind (XOR), bundle (majority sum), permute (cyclic shift) }.

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>
#include <vector>

namespace khora::lattice {

inline constexpr std::size_t kGlyphBits  = 10000;
inline constexpr std::size_t kGlyphWords = (kGlyphBits + 63) / 64;

class Glyph {
public:
    using Word    = std::uint64_t;
    using Storage = std::array<Word, kGlyphWords>;

    Glyph() noexcept;
    explicit Glyph(const Storage& storage) noexcept;

    // Factories
    static Glyph zero() noexcept;
    static Glyph random(std::uint64_t seed) noexcept;
    static Glyph from_hash(std::string_view s) noexcept;

    // Mutating operations
    Glyph& xor_with(const Glyph& other) noexcept;
    Glyph& and_with(const Glyph& other) noexcept;
    Glyph& or_with(const Glyph& other) noexcept;
    Glyph& permute_inplace(int shift) noexcept;
    void   clear() noexcept;

    // Bit-level access
    bool bit(std::size_t i) const noexcept;
    void set_bit(std::size_t i) noexcept;
    void clear_bit(std::size_t i) noexcept;
    void flip_bit(std::size_t i) noexcept;

    // Queries
    std::size_t popcount() const noexcept;
    std::size_t hamming(const Glyph& other) const noexcept;
    double      similarity(const Glyph& other) const noexcept; // range [-1, 1]
    double      density() const noexcept;                       // popcount / N

    // Word-level access (for SIMD acceleration paths)
    const Storage& words() const noexcept { return storage_; }
    Storage&       words()       noexcept { return storage_; }

    bool operator==(const Glyph& other) const noexcept;
    bool operator!=(const Glyph& other) const noexcept { return !(*this == other); }

private:
    Storage storage_{};
};

// Free-function operators (return new glyphs)
Glyph bind(const Glyph& a, const Glyph& b) noexcept;
Glyph bundle(std::span<const Glyph> xs);
Glyph bundle(std::initializer_list<Glyph> xs);
Glyph permute(const Glyph& g, int shift) noexcept;

// A deterministic, cached family of orthogonal "position" glyphs. Binding
// a value with position_glyph(k) marks it as occupying slot k — a
// word-parallel (XOR) alternative to cyclic permutation for encoding
// order/position, distance-preserving and far cheaper than permute().
const Glyph& position_glyph(std::size_t k);

} // namespace khora::lattice
