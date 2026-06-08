#pragma once

// The Lodestone — Khora's gauge of its own physical substrate.
//
// Khora measures the machine it lives in (CPU glyph throughput, real
// parallel speedup, RAM, disk write speed) and derives an operating
// profile that scales its cognitive complexity to the hardware. "The
// only limit is physics" — so Khora learns where the physics sit and
// fills the space they leave.

#include <cstddef>
#include <cstdint>
#include <string>

namespace khora::lodestone {

struct HardwareProfile {
    // Measured
    unsigned       threads          = 1;
    double         bind_mops        = 0.0;   // million bind ops/sec, 1 thread
    double         hamming_mops     = 0.0;   // million hamming ops/sec, 1 thread
    double         parallel_speedup = 1.0;   // aggregate N-thread vs 1-thread
    std::uint64_t  ram_total_mb     = 0;
    std::uint64_t  ram_avail_mb     = 0;
    double         disk_write_mbps  = 0.0;

    // Derived operating parameters
    std::size_t    recommended_facets        = 8;
    std::size_t    recommended_assoc_cap     = 200000;
    std::size_t    recommended_study_tokens  = 60000;
    int            recommended_reverie_ms    = 100;
    int            recommended_whetstone_ms  = 250;

    std::string summary() const;
};

// Run the benchmarks against the current machine and derive the profile.
// `scratch_dir` is where the disk benchmark writes a temporary file.
HardwareProfile gauge(const std::string& scratch_dir = ".");

} // namespace khora::lodestone
