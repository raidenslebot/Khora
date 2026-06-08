#pragma once

// Verified-lossless byte codec for the Reservoir.
//
// LZSS (LZ77 + sliding window) with a hash-chain match finder. The
// decompressor is trivially correct; the compressor only ever emits
// valid tokens, so correctness is guaranteed and ratio is best-effort.
//
// The Reservoir NEVER trusts a compressed Tome it has not round-tripped:
// compress() is always paired with a decompress()+compare at the call
// site, and a mismatch falls back to raw storage. "Zero artifacts" is an
// enforced invariant, not an aspiration.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace khora::reservoir::codec {

// Compress bytes with LZSS. Returns the compressed stream (no container
// header — the Reservoir wraps it).
std::vector<std::uint8_t> compress(const std::vector<std::uint8_t>& input);

// Decompress an LZSS stream produced by compress(). `original_size` is
// the known decompressed length (the Reservoir stores it in the Tome
// header).
std::vector<std::uint8_t> decompress(const std::vector<std::uint8_t>& packed,
                                     std::size_t original_size);

// Convenience: compress, immediately decompress, and confirm the result
// equals the input. Returns true iff the round-trip is bit-identical.
bool verify_roundtrip(const std::vector<std::uint8_t>& input);

} // namespace khora::reservoir::codec
