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

// Bundling is a MAJORITY vote: a bit is set iff more than half the inputs set
// it. At even arity exactly half the inputs can set a bit, and that tie has to
// be broken without bias — resolving ties toward SET makes bundle a union, so
// superposition can only ever add bits and density climbs with every operand.
//
// The tie is broken by a pseudo-random glyph derived from the inputs themselves.
// That keeps the operation deterministic (the substrate's whole claim is
// reproducible reasoning) and commutative (a superposition has no argument
// order, and XOR-folding the operands is order-independent), while decorrelating
// the tiebreak across different operand sets — a single fixed tiebreak glyph
// would make every even-arity bundle share bits by construction.
namespace {

// ponytail: costs a full glyph of splitmix64 per even-arity bundle, which took
// bundle-x2 from ~31 to ~6.4 Mops/s. Left alone deliberately: lattice query runs
// at 14.6k qps, so it outweighs a bundle by ~45x and is the real bottleneck. If
// bundle ever does become hot, derive the tiebreak word-locally from A[w]^B[w]
// instead of folding the operands into one seed and expanding it.
Glyph tiebreak_for(std::span<const Glyph> xs) noexcept {
    std::uint64_t fold = 0;
    for (const auto& g : xs) {
        for (const auto w : g.words()) fold ^= w;
    }
    std::uint64_t s = fold ^ 0x51ED2701A5B3C7D9ULL;  // avoid the seed-0 special case
    return Glyph::random(splitmix64(s));
}

} // namespace

Glyph bundle(std::span<const Glyph> xs) {
    const std::size_t n = xs.size();
    if (n == 0) return Glyph{};
    if (n == 1) return xs[0];

    Glyph out;
    auto& ow = out.words();

    // Fast word-parallel paths for the two most common arities. Inputs are
    // tail-masked, so word-wise ops keep the tail clean. Both are bit-identical
    // to the generic vote below.
    if (n == 2) {
        // Unanimous bits carry; the rest are ties, decided by the tiebreak.
        const auto& A = xs[0].words();
        const auto& B = xs[1].words();
        const auto& T = tiebreak_for(xs).words();
        for (std::size_t w = 0; w < kGlyphWords; ++w)
            ow[w] = (A[w] & B[w]) | ((A[w] ^ B[w]) & T[w]);
        return out;
    }
    if (n == 3) {
        // Odd arity: a strict majority always exists, no tie is possible.
        const auto& A = xs[0].words();
        const auto& B = xs[1].words();
        const auto& C = xs[2].words();
        for (std::size_t w = 0; w < kGlyphWords; ++w)
            ow[w] = (A[w] & B[w]) | (A[w] & C[w]) | (B[w] & C[w]);
        return out;
    }

    // Generic arity. Counts are held as vertical bit-planes: plane p holds bit p
    // of the per-position count, for all kGlyphBits positions at once. Adding an
    // operand is a ripple-carry increment across the planes, so the whole vote
    // runs 64 positions per instruction instead of one position per branch.
    const std::size_t planes = static_cast<std::size_t>(std::bit_width(n));
    std::vector<std::uint64_t> cnt(planes * kGlyphWords, 0);

    for (const auto& g : xs) {
        const auto& gw = g.words();
        for (std::size_t w = 0; w < kGlyphWords; ++w) {
            std::uint64_t carry = gw[w];
            for (std::size_t p = 0; p < planes && carry; ++p) {
                std::uint64_t& plane = cnt[p * kGlyphWords + w];
                const std::uint64_t next_carry = plane & carry;
                plane ^= carry;
                carry = next_carry;
            }
        }
    }

    // Compare each position's count against half, MSB first: `gt` accumulates
    // positions already known to exceed it, `eq` those still exactly equal.
    const std::size_t half = n / 2;
    const bool even = (n % 2) == 0;
    const Glyph tb = even ? tiebreak_for(xs) : Glyph{};
    const auto& tbw = tb.words();

    for (std::size_t w = 0; w < kGlyphWords; ++w) {
        std::uint64_t gt = 0;
        std::uint64_t eq = ~std::uint64_t(0);
        for (std::size_t p = planes; p-- > 0;) {
            const std::uint64_t bits = cnt[p * kGlyphWords + w];
            const std::uint64_t thr  = ((half >> p) & 1u) ? ~std::uint64_t(0) : 0;
            gt |= eq & bits & ~thr;
            eq &= ~(bits ^ thr);
        }
        // Strictly more than half wins outright. Exactly half is a tie, which
        // only occurs at even arity; at odd arity `eq` marks a minority.
        ow[w] = even ? (gt | (eq & tbw[w])) : gt;
    }
    mask_tail(ow);
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

const Glyph& position_glyph(std::size_t k) {
    // A fixed table of orthogonal position markers, computed once
    // (thread-safe static init). Positions beyond the table wrap.
    static const std::array<Glyph, 256> table = [] {
        std::array<Glyph, 256> t;
        // Slot 0 is the identity (zero) glyph: bind(x, position_glyph(0))
        // == x, so the first/most-recent element is marked by being left
        // unchanged — crisper than perturbing it.
        t[0] = Glyph::zero();
        for (std::size_t i = 1; i < t.size(); ++i) {
            const std::uint64_t seed =
                0xC0FFEE00BADF00D5ULL ^ (0x9E3779B97F4A7C15ULL * (i + 1));
            t[i] = Glyph::random(seed);
        }
        return t;
    }();
    return table[k & 255];
}

} // namespace khora::lattice
