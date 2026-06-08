#include "khora/lodestone/lodestone.hpp"

#include "khora/lattice/glyph.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <sstream>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace khora::lodestone {

using clock_t_ = std::chrono::high_resolution_clock;
using khora::lattice::Glyph;

namespace {

// Single-thread bind throughput in million-ops/sec. The volatile sink and
// data dependency keep the optimiser from deleting the loop.
double bench_bind(int iters) {
    Glyph a = Glyph::random(1), b = Glyph::random(2), c;
    const auto t0 = clock_t_::now();
    for (int i = 0; i < iters; ++i) {
        c = khora::lattice::bind(a, b);
        a.set_bit(static_cast<std::size_t>(i) % 64);  // perturb to defeat hoisting
    }
    const auto t1 = clock_t_::now();
    volatile std::size_t sink = c.popcount(); (void)sink;
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return ms > 0 ? (iters / ms / 1000.0) : 0.0;
}

double bench_hamming(int iters) {
    Glyph a = Glyph::random(3), b = Glyph::random(4);
    volatile std::size_t sink = 0;
    const auto t0 = clock_t_::now();
    for (int i = 0; i < iters; ++i) { sink += a.hamming(b); a.flip_bit(static_cast<std::size_t>(i) % 128); }
    const auto t1 = clock_t_::now();
    (void)sink;
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return ms > 0 ? (iters / ms / 1000.0) : 0.0;
}

void query_ram(std::uint64_t& total_mb, std::uint64_t& avail_mb) {
#ifdef _WIN32
    MEMORYSTATUSEX ms; ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        total_mb = ms.ullTotalPhys / (1024 * 1024);
        avail_mb = ms.ullAvailPhys / (1024 * 1024);
    }
#else
    total_mb = 0; avail_mb = 0;
#endif
}

double bench_disk(const std::string& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path f = fs::path(dir) / ".lodestone_bench.tmp";
    const std::size_t bytes = 16 * 1024 * 1024;   // 16 MB
    std::vector<char> buf(bytes, 'K');
    const auto t0 = clock_t_::now();
    {
        std::ofstream os(f, std::ios::binary | std::ios::trunc);
        if (!os) return 0.0;
        os.write(buf.data(), static_cast<std::streamsize>(buf.size()));
        os.flush();
    }
    const auto t1 = clock_t_::now();
    fs::remove(f, ec);
    const double s = std::chrono::duration<double>(t1 - t0).count();
    return s > 0 ? (static_cast<double>(bytes) / (1024.0 * 1024.0) / s) : 0.0;
}

template <typename T> T clampv(T v, T lo, T hi) { return std::max(lo, std::min(hi, v)); }

} // namespace

std::string HardwareProfile::summary() const {
    std::ostringstream os;
    os << "Lodestone hardware gauge:\n"
       << "  threads          : " << threads << "\n"
       << "  bind throughput  : " << bind_mops << " Mops/s (1 thread)\n"
       << "  hamming throughput: " << hamming_mops << " Mops/s (1 thread)\n"
       << "  parallel speedup : " << parallel_speedup << "x\n"
       << "  RAM              : " << ram_avail_mb << " / " << ram_total_mb << " MB free\n"
       << "  disk write       : " << disk_write_mbps << " MB/s\n"
       << "  -> facets        : " << recommended_facets << "\n"
       << "  -> assoc cap     : " << recommended_assoc_cap << "\n"
       << "  -> study tokens  : " << recommended_study_tokens << "\n"
       << "  -> reverie/whet  : " << recommended_reverie_ms << "ms / "
       << recommended_whetstone_ms << "ms";
    return os.str();
}

HardwareProfile gauge(const std::string& scratch_dir) {
    HardwareProfile p;
    p.threads = std::max(1u, std::thread::hardware_concurrency());

    constexpr int kIters = 2'000'000;
    p.bind_mops    = bench_bind(kIters);
    p.hamming_mops = bench_hamming(kIters);

    // Parallel speedup: run the bind benchmark on every hardware thread at
    // once and compare aggregate throughput to a single thread.
    {
        const auto t0 = clock_t_::now();
        std::vector<std::future<double>> futs;
        for (unsigned t = 0; t < p.threads; ++t)
            futs.push_back(std::async(std::launch::async, [] { return bench_bind(2'000'000); }));
        double agg = 0.0; for (auto& f : futs) agg += f.get();
        const auto t1 = clock_t_::now();
        (void)t0; (void)t1;
        p.parallel_speedup = (p.bind_mops > 0) ? (agg / p.bind_mops) : 1.0;
    }

    query_ram(p.ram_total_mb, p.ram_avail_mb);
    p.disk_write_mbps = bench_disk(scratch_dir);

    // --- Derive operating parameters from the measurements ---

    // Facets: one per hardware thread, capped at the 8 lenses we have.
    p.recommended_facets = clampv<std::size_t>(p.threads, 4, 8);

    // Association cap: spend up to ~25% of available RAM on cortex
    // associations (each ~2500 bytes: two glyphs).
    if (p.ram_avail_mb > 0) {
        const std::uint64_t budget_bytes = (p.ram_avail_mb * 1024ull * 1024ull) / 4;
        p.recommended_assoc_cap = clampv<std::size_t>(
            static_cast<std::size_t>(budget_bytes / 2500ull), 50000, 2000000);
    }

    // Study budget scales with raw glyph throughput.
    p.recommended_study_tokens = clampv<std::size_t>(
        static_cast<std::size_t>(p.bind_mops * 4000.0), 20000, 500000);

    // Background cadences: faster machines can afford to dream/sharpen more
    // often without starving the operator.
    const double headroom = clampv(p.parallel_speedup / static_cast<double>(p.threads), 0.2, 1.0);
    p.recommended_reverie_ms   = static_cast<int>(clampv(150.0 - headroom * 100.0, 40.0, 200.0));
    p.recommended_whetstone_ms = static_cast<int>(clampv(400.0 - headroom * 250.0, 100.0, 500.0));

    return p;
}

} // namespace khora::lodestone
