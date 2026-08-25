#include "khora/retina/retina.hpp"

#include <algorithm>
#include <span>

namespace khora::retina {

using khora::lattice::Glyph;

Retina::Retina(std::size_t grid, std::size_t levels, std::uint64_t seed)
    : grid_(grid == 0 ? 1 : grid), levels_(levels < 2 ? 2 : levels) {
    // THE LEVEL FAMILY IS GRADED, and this is the part that makes the encoder
    // robust rather than brittle. Level 0 is random; each subsequent level flips
    // a further D/(2L) bits of the previous one, so similarity to level 0 falls
    // linearly and level L-1 is uncorrelated with it. Intensity becomes an
    // ordered quantity in the algebra instead of a categorical one, which is
    // what lets a shape survive being a little brighter than the one it is being
    // compared against.
    // Flipping RANDOM bit positions per level does not work, and the test caught
    // it: the positions collide, so a flip undoes an earlier one and the family
    // never spans the range. Measured, level 0 against level 15 came out at 0.25
    // similarity where it must be near zero -- 4,687 random flips over 10,000
    // bits land on only about 3,745 distinct positions.
    //
    // A PERMUTATION fixes it exactly. Take a random ordering of the bit indices
    // and give each level the next disjoint block of it, so level k differs from
    // level 0 in exactly k*D/(2L) bits: similarity falls linearly by
    // construction and the two ends are a full D/2 apart, which is orthogonal.
    std::vector<std::size_t> order(khora::lattice::kGlyphBits);
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::uint64_t s = seed ^ 0x9E3779B97F4A7C15ULL;
    auto nxt = [&s]() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; };
    for (std::size_t i = order.size(); i > 1; --i) {
        std::swap(order[i - 1], order[nxt() % i]);
    }
    const std::size_t per = khora::lattice::kGlyphBits / (2 * (levels_ - 1));
    level_.reserve(levels_);
    level_.push_back(Glyph::random(seed));
    for (std::size_t k = 1; k < levels_; ++k) {
        Glyph g = level_[k - 1];
        const std::size_t lo = (k - 1) * per, hi = std::min(k * per, order.size());
        for (std::size_t i = lo; i < hi; ++i) g.flip_bit(order[i]);
        level_.push_back(g);
    }

    // THE PLACE FAMILY IS ORTHOGONAL, one per pooled cell. Random 10,000-bit
    // vectors are near-orthogonal by construction, which is the property that
    // keeps one cell's contribution from being mistaken for another's.
    place_.reserve(grid_ * grid_);
    for (std::size_t c = 0; c < grid_ * grid_; ++c) {
        place_.push_back(Glyph::random(seed ^ (0xC0FFEEULL * (c + 1))));
    }
}

const Glyph& Retina::level(std::size_t k) const {
    return level_[std::min(k, level_.size() - 1)];
}

std::vector<std::uint8_t> Retina::pool(const Image& img) const {
    std::vector<std::uint8_t> out(grid_ * grid_, 0);
    if (!img.valid()) return out;
    // Area averaging rather than sampling. Sampling one pixel per cell makes the
    // encoding depend on which pixel happened to be sampled, so a single flipped
    // pixel can change a whole cell -- averaging is what makes pooling absorb
    // noise instead of amplifying it.
    for (std::size_t gy = 0; gy < grid_; ++gy) {
        const std::size_t y0 = gy * img.height / grid_;
        const std::size_t y1 = std::max(y0 + 1, (gy + 1) * img.height / grid_);
        for (std::size_t gx = 0; gx < grid_; ++gx) {
            const std::size_t x0 = gx * img.width / grid_;
            const std::size_t x1 = std::max(x0 + 1, (gx + 1) * img.width / grid_);
            std::uint64_t sum = 0, n = 0;
            for (std::size_t y = y0; y < y1 && y < img.height; ++y) {
                for (std::size_t x = x0; x < x1 && x < img.width; ++x) {
                    sum += img.at(x, y);
                    ++n;
                }
            }
            out[gy * grid_ + gx] = n ? static_cast<std::uint8_t>(sum / n) : 0;
        }
    }
    return out;
}

Glyph Retina::encode(const Image& img) const {
    const std::vector<std::uint8_t> cells = pool(img);
    std::vector<Glyph> parts;
    parts.reserve(cells.size());
    for (std::size_t c = 0; c < cells.size(); ++c) {
        const std::size_t k =
            static_cast<std::size_t>(cells[c]) * (levels_ - 1) / 255;
        parts.push_back(bind(place_[c], level_[k]));
    }
    return bundle(std::span<const Glyph>(parts));
}

void Recogniser::learn(const Glyph& g, int label) {
    for (Proto& p : protos_) {
        if (p.label == label) { p.seen.push_back(g); p.dirty = true; return; }
    }
    Proto p;
    p.label = label;
    p.seen.push_back(g);
    p.dirty = true;
    protos_.push_back(std::move(p));
}

std::pair<int, double> Recogniser::classify(const Glyph& g) const {
    int best = -1;
    double best_sim = -2.0;
    for (const Proto& p : protos_) {
        if (p.dirty) {
            // The prototype is the BUNDLE of its examples: a majority vote, one
            // pass, no gradient. That is the whole of training in this scheme,
            // and it is why a class can be learned from a handful of examples.
            p.cached = bundle(std::span<const Glyph>(p.seen));
            p.dirty = false;
        }
        const double s = g.similarity(p.cached);
        if (s > best_sim) { best_sim = s; best = p.label; }
    }
    if (best < 0) return {-1, -1.0};
    return {best, best_sim};
}

} // namespace khora::retina
