#pragma once

// RETINA — images into the same substrate everything else already lives in.
//
// A capability audit of this tree found perception entirely absent: not one
// reference to a pixel, a bitmap, a camera or a colour channel anywhere in
// `src/` or `include/`. Every "image" in the codebase meant the process's own
// executable image. Khora reads, reasons, plans, acts on the machine and
// rewrites its own binary, and it cannot see.
//
// It does not need a new substrate to. The Lattice is a 10,000-bit dense
// hypervector algebra closed under bind (XOR), bundle (majority) and permute,
// and hyperdimensional computing has a standard, well-founded encoding for
// spatial data in exactly that algebra. So an image becomes a Glyph, and every
// faculty already built on Glyphs -- associative memory, the Plexus graph,
// nearest-neighbour recall, the GPU hamming search -- works on it unchanged.
// That is the point of doing it this way rather than bolting on a tensor stack:
// one modality added, and the entire existing machine can use it.
//
// HOW AN IMAGE BECOMES A GLYPH
//
//   glyph(image) = bundle over cells of  bind( place(cell), level(intensity) )
//
// Two constructions carry the meaning, and both matter:
//
//   LEVEL vectors are graded, not orthogonal. Intensity 100 and intensity 104
//   must encode similarly or nothing is robust to lighting or noise; intensity 0
//   and 255 must encode near-orthogonally or nothing discriminates. Level k is
//   built by flipping a further D/(2L) bits of level k-1, so similarity falls
//   linearly with intensity distance and the two ends are uncorrelated. Random
//   independent level vectors -- the obvious thing -- would make brightness a
//   categorical variable and throw away the fact that it is ordered.
//
//   PLACE vectors are orthogonal per CELL. `grid` sets how many cells the image
//   is pooled into before encoding, and POOLING TURNS OUT TO BE A PESSIMISATION
//   -- which is worth stating plainly because this header argued the opposite
//   twice before the test was run.
//
//   The first claim was that pooling buys tolerance to small translations. It
//   does not: a ten-pixel square shifted one pixel encodes at 0.880 similarity
//   per-pixel and 0.779 under a 4x4 grid. The second claim was that it buys
//   noise robustness instead. It does not do that either: under 10% pixel noise,
//   0.794 per-pixel against 0.759 coarse. And on the task itself, accuracy rises
//   the whole way as the grid gets finer --
//
//       grid  4x4  55.8%      grid 16x16  64.2%
//       grid  8x8  52.9%      grid 32x32  75.4%
//
//   -- so the default is one cell per pixel and coarser grids exist only for
//   images large enough that 10,000 bits cannot carry them. Averaging an 8x8
//   block destroys exactly the shape information that tells a ring from a
//   square, which is obvious in hindsight and was not obvious in advance.
//
// WHAT THIS IS NOT. There is no convolution, no learned filter, no gradient.
// This is a fixed encoder, and a fixed encoder has a ceiling that a learned one
// does not. It is measured against chance and against a nearest-neighbour
// baseline in retina_test rather than asserted, because a perception module that
// nobody has separated from luck is not a perception module.

#include "khora/lattice/glyph.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace khora::retina {

// A greyscale image. Row-major, one byte per pixel, no padding.
struct Image {
    std::size_t               width  = 0;
    std::size_t               height = 0;
    std::vector<std::uint8_t> pixels;

    bool valid() const noexcept {
        return width > 0 && height > 0 && pixels.size() == width * height;
    }
    std::uint8_t at(std::size_t x, std::size_t y) const noexcept {
        return pixels[y * width + x];
    }
};

// The encoder holds its level and place families, which are deterministic given
// the seed and must be the SAME family for any two glyphs that will be compared.
// Two Retinas with different seeds produce incomparable glyphs, which is why the
// seed is explicit rather than hidden.
class Retina {
public:
    // `grid` is the pooling resolution: the image is reduced to grid x grid
    // cells before encoding. `levels` is how finely intensity is quantised.
    explicit Retina(std::size_t grid = 32, std::size_t levels = 16,
                    std::uint64_t seed = 0x5E77A11AULL);

    khora::lattice::Glyph encode(const Image& img) const;

    // The mean intensity of each pooled cell, which is what encode() actually
    // sees. Exposed because a perception bug is nearly always in the pooling
    // rather than in the algebra, and a test that cannot look at this is
    // guessing.
    std::vector<std::uint8_t> pool(const Image& img) const;

    std::size_t grid()   const noexcept { return grid_; }
    std::size_t levels() const noexcept { return levels_; }

    // The level family, for tests that need to check the grading directly.
    const khora::lattice::Glyph& level(std::size_t k) const;

private:
    std::size_t grid_;
    std::size_t levels_;
    std::vector<khora::lattice::Glyph> level_;   // graded: adjacent levels are similar
    std::vector<khora::lattice::Glyph> place_;   // orthogonal: one per cell
};

// A nearest-prototype classifier over encoded images. Each class is the bundle
// of its examples -- the standard HDC one-shot classifier -- which means
// training is a majority vote and costs one pass, with no gradient anywhere.
class Recogniser {
public:
    void learn(const khora::lattice::Glyph& g, int label);

    // The best label and its similarity, or {-1, -1.0} if nothing was learned.
    std::pair<int, double> classify(const khora::lattice::Glyph& g) const;

    std::size_t classes() const noexcept { return protos_.size(); }

private:
    struct Proto {
        int                                label = 0;
        std::vector<khora::lattice::Glyph> seen;
        mutable khora::lattice::Glyph      cached;
        mutable bool                       dirty = true;
    };
    std::vector<Proto> protos_;
};

} // namespace khora::retina
