#include "khora/lattice/sdr.hpp"

#include <algorithm>
#include <bit>
#include <cstring>

namespace khora::lattice {
namespace {

// Same generator as glyph.cpp, so seeds behave consistently across substrates.
inline std::uint64_t splitmix64(std::uint64_t& s) noexcept {
    std::uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

constexpr std::uint8_t kIndexMask = static_cast<std::uint8_t>(kSdrBlockSize - 1);

// --- the dense -> sparse projection -----------------------------------------
//
// Each of the kSdrBits output units samples kProjSamples fixed positions of the
// dense Glyph and scores itself by how many of them are set; the winner of each
// block becomes that block's active index. Sampling rather than a full dense
// matrix is the point: it is a few hundred KB instead of 200 MB, and it is what
// makes the projection similarity-preserving without being similarity-copying.
//
// A granule cell in dentate gyrus receives only a few thousand of the many
// millions of entorhinal axons available to it. Sparse sampling IS the
// biological arrangement, not an approximation of it.
constexpr std::size_t kProjSamples = 16;

struct Projection {
    // kSdrBits x kProjSamples positions into the dense glyph.
    std::vector<std::uint16_t> taps;

    Projection() : taps(kSdrBits * kProjSamples) {
        std::uint64_t s = 0x5EED0F7A11A17E55ULL;
        for (auto& t : taps) {
            t = static_cast<std::uint16_t>(splitmix64(s) % kGlyphBits);
        }
    }
};

const Projection& projection() {
    static const Projection p;   // thread-safe static init
    return p;
}

} // namespace

// --- Sdr ---------------------------------------------------------------------

Sdr::Sdr() noexcept = default;

Sdr::Sdr(const Storage& s) noexcept : blocks_(s) {
    for (auto& b : blocks_) b &= kIndexMask;
}

void Sdr::set_index(std::size_t block, std::uint8_t i) noexcept {
    blocks_[block] = static_cast<std::uint8_t>(i & kIndexMask);
}

std::size_t Sdr::bit(std::size_t block) const noexcept {
    return block * kSdrBlockSize + blocks_[block];
}

Sdr Sdr::random(std::uint64_t seed) noexcept {
    Sdr out;
    std::uint64_t s = (seed != 0) ? seed : 0xCAFEBABEDEADBEEFULL;
    for (std::size_t b = 0; b < kSdrBlocks; ++b) {
        out.blocks_[b] = static_cast<std::uint8_t>(splitmix64(s) & kIndexMask);
    }
    return out;
}

Sdr Sdr::from_hash(std::string_view str) noexcept {
    std::uint64_t h = 0xCBF29CE484222325ULL;
    for (const unsigned char c : str) { h ^= c; h *= 0x100000001B3ULL; }
    return random(h);
}

std::size_t Sdr::overlap(const Sdr& other) const noexcept {
    std::size_t n = 0;
    for (std::size_t b = 0; b < kSdrBlocks; ++b) {
        n += (blocks_[b] == other.blocks_[b]) ? 1u : 0u;
    }
    return n;
}

double Sdr::similarity(const Sdr& other) const noexcept {
    return static_cast<double>(overlap(other)) / static_cast<double>(kSdrBlocks);
}

// --- algebra -----------------------------------------------------------------

Sdr bind(const Sdr& a, const Sdr& b) noexcept {
    Sdr out;
    auto& o = out.blocks();
    const auto& x = a.blocks();
    const auto& y = b.blocks();
    for (std::size_t i = 0; i < kSdrBlocks; ++i) {
        o[i] = static_cast<std::uint8_t>((x[i] + y[i]) & kIndexMask);
    }
    return out;
}

Sdr unbind(const Sdr& bound, const Sdr& b) noexcept {
    Sdr out;
    auto& o = out.blocks();
    const auto& x = bound.blocks();
    const auto& y = b.blocks();
    for (std::size_t i = 0; i < kSdrBlocks; ++i) {
        o[i] = static_cast<std::uint8_t>((x[i] - y[i]) & kIndexMask);
    }
    return out;
}

Sdr permute(const Sdr& a, int shift) noexcept {
    Sdr out;
    auto& o = out.blocks();
    const auto& x = a.blocks();
    const int B = static_cast<int>(kSdrBlocks);
    int s = shift % B;
    if (s < 0) s += B;
    for (std::size_t i = 0; i < kSdrBlocks; ++i) {
        o[(i + static_cast<std::size_t>(s)) % kSdrBlocks] = x[i];
    }
    return out;
}

// --- Trace -------------------------------------------------------------------

Trace::Trace() : counts_(kSdrBits, 0) {}

void Trace::add(const Sdr& s) noexcept { add(s, 1); }

void Trace::add(const Sdr& s, std::int16_t weight) noexcept {
    for (std::size_t b = 0; b < kSdrBlocks; ++b) {
        counts_[b * kSdrBlockSize + s.index(b)] += weight;
    }
    ++contributors_;
}

void Trace::clear() noexcept {
    std::fill(counts_.begin(), counts_.end(), std::int16_t{0});
    contributors_ = 0;
}

Sdr Trace::binarise() const noexcept {
    Sdr out;
    for (std::size_t b = 0; b < kSdrBlocks; ++b) {
        const std::int16_t* base = counts_.data() + b * kSdrBlockSize;
        std::size_t best = 0;
        std::int16_t best_v = base[0];
        for (std::size_t i = 1; i < kSdrBlockSize; ++i) {
            if (base[i] > best_v) { best_v = base[i]; best = i; }
        }
        out.set_index(b, static_cast<std::uint8_t>(best));
    }
    return out;
}

double Trace::sharpness() const noexcept {
    if (contributors_ == 0) return 0.0;
    double total = 0.0;
    for (std::size_t b = 0; b < kSdrBlocks; ++b) {
        const std::int16_t* base = counts_.data() + b * kSdrBlockSize;
        std::int16_t best = base[0];
        for (std::size_t i = 1; i < kSdrBlockSize; ++i) best = std::max(best, base[i]);
        total += static_cast<double>(best);
    }
    return total / (static_cast<double>(kSdrBlocks) * static_cast<double>(contributors_));
}

Sdr bundle(std::span<const Sdr> xs) {
    Trace t;
    for (const auto& s : xs) t.add(s);
    return t.binarise();
}

Sdr bundle(std::initializer_list<Sdr> xs) {
    return bundle(std::span<const Sdr>{xs.begin(), xs.size()});
}

// --- SdrUnion ----------------------------------------------------------------

void SdrUnion::add(const Sdr& s) noexcept {
    for (std::size_t b = 0; b < kSdrBlocks; ++b) {
        mask_[b] |= (1ULL << s.index(b));
    }
    ++members_;
}

void SdrUnion::add_position(std::size_t block, std::uint8_t index) noexcept {
    mask_[block] |= (1ULL << (index & (kSdrBlockSize - 1)));
}

void SdrUnion::clear() noexcept {
    mask_.fill(0);
    members_ = 0;
}

std::size_t SdrUnion::overlap(const Sdr& s) const noexcept {
    std::size_t n = 0;
    for (std::size_t b = 0; b < kSdrBlocks; ++b) {
        n += ((mask_[b] >> s.index(b)) & 1ULL) ? 1u : 0u;
    }
    return n;
}

std::size_t SdrUnion::active() const noexcept {
    std::size_t n = 0;
    for (const auto m : mask_) n += static_cast<std::size_t>(std::popcount(m));
    return n;
}

double SdrUnion::density() const noexcept {
    return static_cast<double>(active()) / static_cast<double>(kSdrBits);
}

// --- the bridge --------------------------------------------------------------

Sdr project(const Glyph& g) noexcept {
    const auto& taps = projection().taps;
    const auto& w = g.words();

    Sdr out;
    for (std::size_t b = 0; b < kSdrBlocks; ++b) {
        std::size_t best = 0;
        int best_score = -1;
        for (std::size_t i = 0; i < kSdrBlockSize; ++i) {
            const std::size_t unit = b * kSdrBlockSize + i;
            const std::uint16_t* t = taps.data() + unit * kProjSamples;
            int score = 0;
            for (std::size_t k = 0; k < kProjSamples; ++k) {
                const std::size_t pos = t[k];
                score += static_cast<int>((w[pos >> 6] >> (pos & 63)) & 1ULL);
            }
            if (score > best_score) { best_score = score; best = i; }
        }
        out.set_index(b, static_cast<std::uint8_t>(best));
    }
    return out;
}

// --- Segment -----------------------------------------------------------------

Segment Segment::learn(const Sdr& s, std::uint64_t seed, std::size_t n) noexcept {
    Segment seg;
    if (n > kSynapses) n = kSynapses;
    std::uint64_t r = (seed != 0) ? seed : 0x51E6E27ULL;

    // Sample blocks WITHOUT replacement: two synapses onto the same block would
    // be perfectly redundant, since a block has exactly one active index, and
    // would silently weaken the threshold.
    std::array<bool, kSdrBlocks> taken{};
    std::size_t placed = 0;
    std::size_t guard = 0;
    while (placed < n && guard < kSdrBlocks * 8) {
        ++guard;
        const std::size_t b = static_cast<std::size_t>(splitmix64(r) % kSdrBlocks);
        if (taken[b]) continue;
        taken[b] = true;
        seg.block[placed] = static_cast<std::uint8_t>(b);
        seg.index[placed] = s.index(b);
        ++placed;
    }
    seg.count = static_cast<std::uint8_t>(placed);
    return seg;
}

std::size_t Segment::agreement(const Sdr& s) const noexcept {
    std::size_t n = 0;
    for (std::size_t i = 0; i < count; ++i) {
        n += (s.index(block[i]) == index[i]) ? 1u : 0u;
    }
    return n;
}

bool Segment::matches(const Sdr& s, std::uint8_t theta) const noexcept {
    return agreement(s) >= theta;
}

std::size_t Segment::agreement(const SdrUnion& u) const noexcept {
    std::size_t n = 0;
    for (std::size_t i = 0; i < count; ++i) {
        n += u.contains(block[i], index[i]) ? 1u : 0u;
    }
    return n;
}

bool Segment::matches(const SdrUnion& u, std::uint8_t theta) const noexcept {
    return agreement(u) >= theta;
}

} // namespace khora::lattice
