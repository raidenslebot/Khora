#include "khora/lattice/glyph.hpp"

#include <bit>
#include <cstring>

namespace khora::lattice {
namespace {

// SplitMix64 — fast deterministic PRNG, seed-stable across platforms.
inline std::uint64_t splitmix64(std::uint64_t& s) noexcept {
    std::uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

constexpr std::size_t kTailBits  = kGlyphBits % 64;
constexpr std::size_t kFullWords = kGlyphBits / 64;
constexpr std::uint64_t kTailMask =
    (kTailBits == 0) ? ~std::uint64_t(0) : ((std::uint64_t(1) << kTailBits) - 1);

inline void mask_tail(Glyph::Storage& s) noexcept {
    if constexpr (kTailBits != 0) {
        s[kFullWords] &= kTailMask;
    }
}

} // namespace

Glyph::Glyph() noexcept = default;

Glyph::Glyph(const Storage& storage) noexcept : storage_(storage) {
    mask_tail(storage_);
}

Glyph Glyph::zero() noexcept { return Glyph{}; }

Glyph Glyph::random(std::uint64_t seed) noexcept {
    Glyph g;
    std::uint64_t s = (seed != 0) ? seed : 0xCAFEBABEDEADBEEFULL;
    for (auto& w : g.storage_) {
        w = splitmix64(s);
    }
    mask_tail(g.storage_);
    return g;
}

Glyph Glyph::sparse(std::uint64_t seed, std::size_t active_bits) noexcept {
    Glyph g;
    if (active_bits == 0) return g;
    if (active_bits > kGlyphBits) active_bits = kGlyphBits;

    std::uint64_t s = (seed != 0) ? seed : 0xCAFEBABEDEADBEEFULL;
    std::size_t placed = 0;
    while (placed < active_bits) {
        const std::uint64_t r = splitmix64(s);
        const std::size_t i = static_cast<std::size_t>(r % kGlyphBits);
        if (!g.bit(i)) {
            g.set_bit(i);
            ++placed;
        }
    }
    return g;
}

Glyph Glyph::from_hash(std::string_view sv) noexcept {
    // FNV-1a → 64-bit seed → splitmix expansion.
    std::uint64_t h = 0xCBF29CE484222325ULL;
    for (unsigned char c : sv) {
        h ^= static_cast<std::uint64_t>(c);
        h *= 0x100000001B3ULL;
    }
    return Glyph::random(h);
}

Glyph& Glyph::xor_with(const Glyph& other) noexcept {
    for (std::size_t i = 0; i < kGlyphWords; ++i) storage_[i] ^= other.storage_[i];
    return *this;
}

Glyph& Glyph::and_with(const Glyph& other) noexcept {
    for (std::size_t i = 0; i < kGlyphWords; ++i) storage_[i] &= other.storage_[i];
    return *this;
}

Glyph& Glyph::or_with(const Glyph& other) noexcept {
    for (std::size_t i = 0; i < kGlyphWords; ++i) storage_[i] |= other.storage_[i];
    mask_tail(storage_);
    return *this;
}

Glyph& Glyph::permute_inplace(int shift) noexcept {
    if (shift == 0) return *this;
    int s = shift % static_cast<int>(kGlyphBits);
    if (s < 0) s += static_cast<int>(kGlyphBits);

    Glyph out;
    // Simple per-bit cyclic shift. Correct, not yet fast.
    for (std::size_t i = 0; i < kGlyphBits; ++i) {
        if (bit(i)) {
            const std::size_t j = (i + static_cast<std::size_t>(s)) % kGlyphBits;
            out.set_bit(j);
        }
    }
    storage_ = out.storage_;
    return *this;
}

void Glyph::clear() noexcept { storage_.fill(0); }

bool Glyph::bit(std::size_t i) const noexcept {
    return ((storage_[i >> 6] >> (i & 63)) & 1ULL) != 0;
}
void Glyph::set_bit(std::size_t i) noexcept {
    storage_[i >> 6] |= (std::uint64_t(1) << (i & 63));
}
void Glyph::clear_bit(std::size_t i) noexcept {
    storage_[i >> 6] &= ~(std::uint64_t(1) << (i & 63));
}
void Glyph::flip_bit(std::size_t i) noexcept {
    storage_[i >> 6] ^= (std::uint64_t(1) << (i & 63));
}

std::size_t Glyph::popcount() const noexcept {
    std::size_t c = 0;
    for (auto w : storage_) c += static_cast<std::size_t>(std::popcount(w));
    return c;
}

std::size_t Glyph::hamming(const Glyph& other) const noexcept {
    std::size_t c = 0;
    for (std::size_t i = 0; i < kGlyphWords; ++i) {
        c += static_cast<std::size_t>(std::popcount(storage_[i] ^ other.storage_[i]));
    }
    return c;
}

double Glyph::similarity(const Glyph& other) const noexcept {
    return 1.0 - 2.0 * static_cast<double>(hamming(other)) / static_cast<double>(kGlyphBits);
}

double Glyph::density() const noexcept {
    return static_cast<double>(popcount()) / static_cast<double>(kGlyphBits);
}

bool Glyph::operator==(const Glyph& other) const noexcept {
    return std::memcmp(storage_.data(), other.storage_.data(), sizeof(Storage)) == 0;
}

// --- Free functions ---

Glyph bind(const Glyph& a, const Glyph& b) noexcept {
    Glyph out = a;
    out.xor_with(b);
    return out;
}

Glyph bundle(std::span<const Glyph> xs) {
    Glyph out;
    if (xs.empty()) return out;

    // Count how many of the inputs have each bit set; output bit = 1
    // iff at least (n + 1) / 2 inputs have it set. For odd n this is
    // strict majority; for even n it biases slightly toward density,
    // which preserves similarity-to-constituents better than dropping ties.
    std::vector<std::uint16_t> counts(kGlyphBits, 0);
    for (const auto& g : xs) {
        for (std::size_t i = 0; i < kGlyphBits; ++i) {
            if (g.bit(i)) ++counts[i];
        }
    }
    const std::size_t n = xs.size();
    const std::uint16_t threshold = static_cast<std::uint16_t>((n + 1) / 2);
    for (std::size_t i = 0; i < kGlyphBits; ++i) {
        if (counts[i] >= threshold) out.set_bit(i);
    }
    return out;
}

Glyph bundle(std::initializer_list<Glyph> xs) {
    const std::vector<Glyph> v(xs.begin(), xs.end());
    return bundle(std::span<const Glyph>{v.data(), v.size()});
}

Glyph permute(const Glyph& g, int shift) noexcept {
    Glyph out = g;
    out.permute_inplace(shift);
    return out;
}

} // namespace khora::lattice
