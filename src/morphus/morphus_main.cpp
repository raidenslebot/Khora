// Morphus demo — proves the Morphic Lattice has real signal.
//
// Populates a 1,000-glyph lattice, bundles three known glyphs into a
// blind probe, and asserts that the lattice's top-3 nearest-neighbour
// matches are exactly those three. This is the substrate working
// end-to-end. If this fails, nothing built on top will work.

#include "khora/lattice/lattice.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

int main() {
    using namespace khora::lattice;

    Lattice mem;
    constexpr std::size_t N = 1000;
    for (std::size_t i = 0; i < N; ++i) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "concept_%zu", i);
        mem.store(buf, Glyph::random(0x1000 + i));
    }
    std::printf("Morphic Lattice populated: %zu glyphs (%zu bits each).\n",
                mem.size(), kGlyphBits);

    const Glyph a = mem.recall("concept_42").value();
    const Glyph b = mem.recall("concept_137").value();
    const Glyph c = mem.recall("concept_823").value();

    const Glyph probe = bundle({a, b, c});
    std::printf("\nBundled probe density: %.4f  (expected ~0.5)\n", probe.density());
    std::printf("Probe similarity to constituents:\n");
    std::printf("  concept_42  : %+.4f\n", probe.similarity(a));
    std::printf("  concept_137 : %+.4f\n", probe.similarity(b));
    std::printf("  concept_823 : %+.4f\n", probe.similarity(c));

    const auto matches = mem.query(probe, 5);
    std::printf("\nTop-5 Lattice matches for the bundled probe:\n");
    for (std::size_t i = 0; i < matches.size(); ++i) {
        std::printf("  %zu. %-15s  hamming=%-5zu  sim=%+.4f\n",
                    i + 1, matches[i].label.c_str(),
                    matches[i].hamming, matches[i].similarity);
    }

    std::vector<std::string> expected = {"concept_42", "concept_137", "concept_823"};
    std::vector<std::string> got;
    for (std::size_t i = 0; i < 3 && i < matches.size(); ++i) {
        got.push_back(matches[i].label);
    }
    std::sort(expected.begin(), expected.end());
    std::sort(got.begin(), got.end());
    const bool ok = (expected == got);

    std::printf("\n%s\n", ok
        ? "PASS  Morphic Lattice recovered the bundled constituents from a blind probe."
        : "FAIL  Lattice did not recover constituents.");
    return ok ? 0 : 1;
}
