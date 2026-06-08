// Demonstrates that Khora's substrate accumulates across process lifetimes.
//
// First invocation (no arg)   : builds and saves a 1,000-glyph lattice.
// Second invocation ("load")  : reloads it and runs a query.
//
// The point: state survives process death. Khora wakes up remembering.

#include "khora/lattice/persistence.hpp"
#include "khora/lattice/lattice.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>

int main(int argc, char** argv) {
    using namespace khora::lattice;
    namespace fs = std::filesystem;

    const fs::path archive = fs::path("data") / "lattice_archive" / "demo.klat";

    const bool load_phase = (argc > 1 && std::strcmp(argv[1], "load") == 0);

    if (!load_phase) {
        Lattice L;
        for (std::size_t i = 0; i < 1000; ++i) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "concept_%zu", i);
            L.store(buf, Glyph::random(0x1000 + i));
        }
        try {
            const auto stats = save(L, archive);
            std::printf("SAVE: %zu glyphs -> %s (%llu bytes)\n",
                        stats.glyph_count,
                        archive.string().c_str(),
                        static_cast<unsigned long long>(stats.bytes_written));
            std::printf("Run with arg 'load' to reload and query.\n");
        } catch (const PersistError& e) {
            std::fprintf(stderr, "SAVE failed: %s\n", e.what());
            return 1;
        }
        return 0;
    }

    if (!fs::exists(archive)) {
        std::fprintf(stderr,
                     "No archive at %s — run without 'load' first.\n",
                     archive.string().c_str());
        return 1;
    }

    Lattice L;
    try {
        L = load(archive);
    } catch (const PersistError& e) {
        std::fprintf(stderr, "LOAD failed: %s\n", e.what());
        return 1;
    }
    std::printf("LOAD: %zu glyphs from %s\n", L.size(), archive.string().c_str());

    const auto a = L.recall("concept_42").value();
    const auto b = L.recall("concept_137").value();
    const auto c = L.recall("concept_823").value();
    const Glyph probe = bundle({a, b, c});

    const auto matches = L.query(probe, 5);
    std::printf("\nTop-5 matches for bundled probe (after disk round-trip):\n");
    for (std::size_t i = 0; i < matches.size(); ++i) {
        std::printf("  %zu. %-15s  hamming=%-5zu  sim=%+.4f\n",
                    i + 1, matches[i].label.c_str(),
                    matches[i].hamming, matches[i].similarity);
    }
    return 0;
}
