// Throughput benchmark for Morphic Lattice primitives.

#include "khora/lattice/lattice.hpp"
#include "khora/lattice/sdr.hpp"

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <span>
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
    // Bundle is the substrate's hottest primitive after bind — the cortex,
    // lexicon and cogitator all call it several times per cycle, at arities
    // that hit three different code paths (the n==2 and n==3 word-parallel
    // cases, and the generic bit-plane vote). All three are measured.
    {
        std::vector<Glyph> pool;
        pool.reserve(16);
        for (int i = 0; i < 16; ++i) pool.push_back(Glyph::random(0x900 + i));

        for (const std::size_t arity : {std::size_t{2}, std::size_t{3},
                                        std::size_t{8}, std::size_t{16}}) {
            const int n = (arity <= 3) ? 200'000 : 50'000;
            const std::span<const Glyph> xs{pool.data(), arity};
            Glyph c;
            const double ms = time_ms([&](int){ c = bundle(xs); }, n);
            std::printf("  bundle x%-2zu      : %8.2f Mops/s  (%.2f ms / %d iters)  density %.3f\n",
                        arity, n / ms / 1000.0, ms, n, c.density());
        }
    }
    // The sparse substrate, against the dense one it sits beside. Sdr::overlap
    // is 256 byte compares; Glyph::hamming is 157 XOR+POPCNT. The sparse path
    // has to be competitive or the substrate decision costs throughput to buy
    // selectivity, and that trade has to be visible rather than assumed.
    {
        const Sdr sa = Sdr::random(1), sb = Sdr::random(2);
        {
            const int n = 1'000'000;
            volatile std::size_t sink = 0;
            const double ms = time_ms([&](int){ sink += sa.overlap(sb); }, n);
            std::printf("  sdr overlap     : %8.2f Mops/s  (%.2f ms / %d iters)\n",
                        n / ms / 1000.0, ms, n);
        }
        {
            const int n = 1'000'000;
            Sdr c;
            const double ms = time_ms([&](int){ c = bind(sa, sb); }, n);
            std::printf("  sdr bind        : %8.2f Mops/s  (%.2f ms / %d iters)\n",
                        n / ms / 1000.0, ms, n);
        }
        {
            // The operation the whole sparse substrate exists for: a 24-synapse
            // subsampled match. It touches 24 bytes, not 16,384 bits.
            const Segment seg = Segment::learn(sa, 0x2468);
            const int n = 1'000'000;
            volatile std::size_t sink = 0;
            const double ms = time_ms([&](int){ sink += seg.agreement(sb); }, n);
            std::printf("  sdr segment     : %8.2f Mops/s  (%.2f ms / %d iters)\n",
                        n / ms / 1000.0, ms, n);
        }
        {
            SdrUnion u;
            for (int i = 0; i < 8; ++i) u.add(Sdr::random(0x700 + i));
            const Segment seg = Segment::learn(sa, 0x2468);
            const int n = 1'000'000;
            volatile std::size_t sink = 0;
            const double ms = time_ms([&](int){ sink += seg.agreement(u); }, n);
            std::printf("  sdr seg v union : %8.2f Mops/s  (%.2f ms / %d iters)\n",
                        n / ms / 1000.0, ms, n);
        }
        {
            const Glyph g = Glyph::random(3);
            const int n = 100'000;
            Sdr c;
            const double ms = time_ms([&](int){ c = project(g); }, n);
            std::printf("  glyph->sdr proj : %8.2f Kops/s  (%.2f ms / %d iters)\n",
                        n / ms, ms, n);
        }
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
