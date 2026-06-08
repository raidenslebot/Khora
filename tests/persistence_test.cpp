// Tests for Lattice persistence.

#include "khora/lattice/persistence.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
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

int main() {
    using namespace khora::lattice;
    namespace fs = std::filesystem;

    const fs::path tmpdir = fs::temp_directory_path() / "khora_persist_test";
    fs::create_directories(tmpdir);

    // 1. Empty round-trip.
    {
        const auto p = tmpdir / "empty.klat";
        Lattice L;
        const auto stats = save(L, p);
        EXPECT(stats.glyph_count == 0, "save reports zero for empty lattice");
        EXPECT(stats.bytes_written > 0, "empty save still writes header/footer");
        const Lattice loaded = load(p);
        EXPECT(loaded.size() == 0, "loaded empty lattice has size 0");
    }

    // 2. Full round-trip: 500 glyphs, all preserved bit-exact.
    {
        const auto p = tmpdir / "full.klat";
        Lattice L;
        for (int i = 0; i < 500; ++i) {
            char buf[16]; std::snprintf(buf, sizeof(buf), "g%d", i);
            L.store(buf, Glyph::random(0x42 + i));
        }
        const auto stats = save(L, p);
        EXPECT(stats.glyph_count == 500, "save reports correct glyph count");

        const Lattice loaded = load(p);
        EXPECT(loaded.size() == 500, "loaded lattice has same size");

        bool all_match = true;
        for (int i = 0; i < 500; ++i) {
            char buf[16]; std::snprintf(buf, sizeof(buf), "g%d", i);
            const auto a = L.recall(buf);
            const auto b = loaded.recall(buf);
            if (!a.has_value() || !b.has_value() || !(*a == *b)) {
                all_match = false; break;
            }
        }
        EXPECT(all_match, "all glyphs bit-identical after round-trip");

        // Query equivalence.
        const Glyph probe = bundle({
            L.recall("g7").value(), L.recall("g123").value(), L.recall("g456").value()
        });
        const auto orig_matches = L.query(probe, 3);
        const auto load_matches = loaded.query(probe, 3);
        EXPECT(orig_matches.size() == load_matches.size(), "query result size identical");
        bool all_same = true;
        for (std::size_t i = 0; i < orig_matches.size(); ++i) {
            if (orig_matches[i].label   != load_matches[i].label
             || orig_matches[i].hamming != load_matches[i].hamming) {
                all_same = false; break;
            }
        }
        EXPECT(all_same, "query results identical after round-trip");
    }

    // 3. Bad header magic -> throws PersistError.
    {
        const auto p = tmpdir / "bad_magic.klat";
        {
            std::ofstream os(p, std::ios::binary);
            os.write("NOTAKHORA", 9);
            for (int i = 0; i < 100; ++i) os.put('\0');
        }
        bool threw = false;
        try { (void)load(p); }
        catch (const PersistError&) { threw = true; }
        EXPECT(threw, "bad header magic throws PersistError");
    }

    // 4. Truncated file -> throws PersistError.
    {
        const auto good  = tmpdir / "full.klat";
        const auto trunc = tmpdir / "truncated.klat";
        {
            std::ifstream src(good, std::ios::binary);
            std::ofstream dst(trunc, std::ios::binary);
            std::vector<char> buf(100);
            src.read(buf.data(), static_cast<std::streamsize>(buf.size()));
            dst.write(buf.data(), src.gcount());
        }
        bool threw = false;
        try { (void)load(trunc); }
        catch (const PersistError&) { threw = true; }
        EXPECT(threw, "truncated file throws PersistError");
    }

    // 5. Long labels with non-ASCII bytes survive.
    {
        const auto p = tmpdir / "labels.klat";
        Lattice L;
        const std::string odd_label = "concept/with-special_chars.\xc3\xa9.\xe7\x88\xb1";
        L.store(odd_label, Glyph::random(99));
        save(L, p);
        const Lattice loaded = load(p);
        EXPECT(loaded.contains(odd_label), "non-ASCII label round-trips");
    }

    std::printf("\nPersistence tests: %d/%d passed (%d failed).\n",
                g_total - g_failed, g_total, g_failed);
    return g_failed == 0 ? 0 : 1;
}
