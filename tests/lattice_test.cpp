// Tests for the Morphic Lattice. Dependency-free.

#include "khora/lattice/lattice.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {
int g_total  = 0;
int g_failed = 0;
}

#define EXPECT(cond, msg) do { \
    ++g_total; \
    if (!(cond)) { ++g_failed; std::fprintf(stderr, "FAIL: %s  (%s:%d)\n", (msg), __FILE__, __LINE__); } \
} while (0)

#define EXPECT_NEAR(a, b, tol, msg) do { \
    ++g_total; \
    const double _a = (a); const double _b = (b); const double _t = (tol); \
    if (std::fabs(_a - _b) > _t) { \
        ++g_failed; \
        std::fprintf(stderr, "FAIL: %s  expected %.4f got %.4f tol %.4f  (%s:%d)\n", \
                     (msg), _b, _a, _t, __FILE__, __LINE__); \
    } \
} while (0)

int main() {
    using namespace khora::lattice;

    // Zero glyph has zero popcount.
    {
        const Glyph z = Glyph::zero();
        EXPECT(z.popcount() == 0, "zero glyph popcount");
    }

    // Random glyphs should have density near 0.5.
    {
        const Glyph g = Glyph::random(12345);
        EXPECT_NEAR(g.density(), 0.5, 0.05, "random glyph density ~ 0.5");
    }

    // Sparse glyph respects active_bits exactly.
    {
        const Glyph s = Glyph::sparse(99, 1000);
        EXPECT(s.popcount() == 1000, "sparse glyph popcount matches request");
    }

    // Two independent random glyphs should be nearly orthogonal (sim ~ 0).
    {
        const Glyph a = Glyph::random(1);
        const Glyph b = Glyph::random(2);
        EXPECT_NEAR(a.similarity(b), 0.0, 0.05, "random glyphs orthogonal");
    }

    // Bind (XOR) is self-inverse.
    {
        const Glyph a = Glyph::random(7);
        const Glyph b = Glyph::random(13);
        const Glyph c    = bind(a, b);
        const Glyph back = bind(c, b);
        EXPECT(back == a, "bind is self-inverse");
    }

    // Permute is distance-preserving.
    {
        const Glyph a  = Glyph::random(101);
        const Glyph b  = Glyph::random(202);
        const Glyph ap = permute(a, 137);
        const Glyph bp = permute(b, 137);
        EXPECT(a.hamming(b) == ap.hamming(bp), "permute preserves hamming distance");
    }

    // Bundle preserves similarity to constituents and rejects strangers.
    {
        const Glyph a = Glyph::random(11);
        const Glyph b = Glyph::random(22);
        const Glyph c = Glyph::random(33);
        const Glyph bun = bundle({a, b, c});

        EXPECT(bun.similarity(a) > 0.3, "bundle similar to constituent a");
        EXPECT(bun.similarity(b) > 0.3, "bundle similar to constituent b");
        EXPECT(bun.similarity(c) > 0.3, "bundle similar to constituent c");

        const Glyph stranger = Glyph::random(44);
        EXPECT(bun.similarity(stranger) < 0.1, "bundle dissimilar to stranger");
    }

    // from_hash is deterministic and distinguishes inputs.
    {
        const Glyph h1 = Glyph::from_hash("morphus");
        const Glyph h2 = Glyph::from_hash("morphus");
        EXPECT(h1 == h2, "from_hash deterministic for equal inputs");
        const Glyph h3 = Glyph::from_hash("khora");
        EXPECT(!(h1 == h3), "from_hash distinguishes different inputs");
    }

    // Lattice store / recall / contains.
    {
        Lattice L;
        L.store("alpha", Glyph::random(1));
        L.store("beta",  Glyph::random(2));
        EXPECT(L.size() == 2, "lattice size after two stores");
        EXPECT(L.contains("alpha"), "contains alpha");
        EXPECT(L.recall("alpha").has_value(), "recall alpha");
        EXPECT(!L.recall("missing").has_value(), "recall missing returns nullopt");
    }

    // End-to-end: lattice query recovers exact constituents of a bundled probe.
    {
        Lattice L;
        std::vector<Glyph> all;
        for (int i = 0; i < 200; ++i) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "g%d", i);
            const Glyph g = Glyph::random(0x500 + i);
            L.store(buf, g);
            all.push_back(g);
        }
        const Glyph probe = bundle({all[5], all[77], all[150]});
        const auto matches = L.query(probe, 3);
        EXPECT(matches.size() == 3, "query returns k matches");

        std::vector<std::string> got = { matches[0].label, matches[1].label, matches[2].label };
        std::sort(got.begin(), got.end());
        std::vector<std::string> want = { "g150", "g5", "g77" };
        std::sort(want.begin(), want.end());
        EXPECT(got == want, "query recovers exact bundled constituents");
    }

    std::printf("\nLattice tests: %d/%d passed (%d failed).\n",
                g_total - g_failed, g_total, g_failed);
    return g_failed == 0 ? 0 : 1;
}
