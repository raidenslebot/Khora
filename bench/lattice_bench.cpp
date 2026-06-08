// Throughput benchmark for Morphic Lattice primitives.

#include "khora/lattice/lattice.hpp"

#include <chrono>
#include <cstdio>
#include <vector>

using clock_t_ = std::chrono::high_resolution_clock;

template <class F>
double time_ms(F&& fn, int iters) {
    const auto t0 = clock_t_::now();
    for (int i = 0; i < iters; ++i) fn(i);
    const auto t1 = clock_t_::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

int main() {
    using namespace khora::lattice;
    std::printf("Morphic Lattice bench (Glyph bits = %zu)\n\n", kGlyphBits);

    Glyph a = Glyph::random(1);
    Glyph b = Glyph::random(2);

    {
        const int n = 1'000'000;
        volatile std::size_t sink = 0;
        const double ms = time_ms([&](int){ sink += a.popcount(); }, n);
        std::printf("  popcount        : %8.2f Mops/s  (%.2f ms / %d iters)\n",
                    n / ms / 1000.0, ms, n);
    }
    {
        const int n = 1'000'000;
        volatile std::size_t sink = 0;
        const double ms = time_ms([&](int){ sink += a.hamming(b); }, n);
        std::printf("  hamming         : %8.2f Mops/s  (%.2f ms / %d iters)\n",
                    n / ms / 1000.0, ms, n);
    }
    {
        const int n = 1'000'000;
        Glyph c;
        const double ms = time_ms([&](int){ c = bind(a, b); }, n);
        std::printf("  bind (xor)      : %8.2f Mops/s  (%.2f ms / %d iters)\n",
                    n / ms / 1000.0, ms, n);
    }
    {
        Lattice L;
        for (int i = 0; i < 1000; ++i) {
            char buf[16]; std::snprintf(buf, sizeof(buf), "g%d", i);
            L.store(buf, Glyph::random(0x100 + i));
        }
        const int n = 10000;
        const Glyph probe = Glyph::random(0xBEEF);
        volatile std::size_t sink = 0;
        const double ms = time_ms([&](int){ sink += L.query(probe, 5).size(); }, n);
        std::printf("  lattice query   : %8.2f qps      (over 1000 glyphs, %.2f ms / %d queries)\n",
                    n / ms * 1000.0, ms, n);
    }
    return 0;
}
