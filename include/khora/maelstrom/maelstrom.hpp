#pragma once

// The Maelstrom — Khora's GPU parallel-resonance engine.
//
// A vortex of concurrent GPU threads that answers k-nearest-neighbour
// hamming queries across a charged glyph database resident in VRAM.
// The Morphic Lattice's content-addressable recall is embarrassingly
// parallel — one independent popcount per stored glyph — so it maps
// perfectly onto the thousands of lanes of a modern GPU.
//
// Substrate: pure Direct3D 11 DirectCompute. No CUDA, no toolkit, no
// added runtime dependency — d3d11.dll and d3dcompiler_47.dll ship with
// every Windows install. On a machine with no compute-capable GPU the
// Maelstrom simply never ignites; the CPU lattice remains the ground truth.
//
// This is an ACCELERATOR, never a requirement. lattice::Lattice::query()
// stays the canonical (and only) correctness oracle; the Maelstrom must
// return bit-identical hamming distances or it is wrong.

#include "khora/lattice/glyph.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace khora::maelstrom {

// One neighbour from a resonance query: an index into the charged
// database and its hamming distance to the probe.
struct Neighbour {
    std::uint32_t index;
    std::uint32_t hamming;
};

// Identity + capability of the GPU the Maelstrom bound to.
struct DeviceInfo {
    bool        available = false;
    std::string adapter;        // e.g. "NVIDIA GeForce RTX 2070 SUPER"
    std::size_t vram_mb   = 0;  // dedicated video memory
    std::string feature;        // D3D feature level, e.g. "11_1"
    std::string note;           // populated with the reason when unavailable
};

class Maelstrom {
public:
    Maelstrom();
    ~Maelstrom();
    Maelstrom(const Maelstrom&)            = delete;
    Maelstrom& operator=(const Maelstrom&) = delete;
    Maelstrom(Maelstrom&&) noexcept;
    Maelstrom& operator=(Maelstrom&&) noexcept;

    // Bind a compute-capable GPU and compile the resonance kernel. Safe on
    // a machine without one — returns false, ready() stays false, and
    // device().note explains why. Idempotent.
    bool ignite();
    bool ready() const noexcept;
    const DeviceInfo& device() const noexcept;

    // Upload a glyph database into VRAM. Replaces any prior charge. The
    // glyphs keep their index (0..n-1); resonate() returns those indices.
    // Returns false if not ready or the upload failed.
    bool charge(const std::vector<lattice::Glyph>& glyphs);
    std::size_t charged() const noexcept;          // glyphs resident in VRAM
    std::size_t vram_bytes() const noexcept;        // bytes the DB occupies

    // k-nearest by hamming over the charged database, ascending. Returns up
    // to k neighbours; empty if not ready or nothing charged.
    std::vector<Neighbour> resonate(const lattice::Glyph& probe,
                                    std::size_t k = 5) const;

    // Compute the raw hamming distance to every charged glyph (index order).
    // Primarily a verification hook — lets a caller check the GPU against
    // the CPU popcount bit-for-bit. Empty if not ready/charged.
    std::vector<std::uint32_t> hamming_all(const lattice::Glyph& probe) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace khora::maelstrom
