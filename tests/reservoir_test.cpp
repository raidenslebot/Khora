// Verification for the Reservoir: codec losslessness, distillation
// artifact removal, and capacity-capped value-based eviction.

#include "khora/reservoir/aqueduct.hpp"
#include "khora/reservoir/codec.hpp"
#include "khora/reservoir/distill.hpp"
#include "khora/reservoir/reservoir.hpp"

#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace {
int g_total = 0;
int g_failed = 0;
}

#define EXPECT(cond, msg) do { \
    ++g_total; \
    if (!(cond)) { ++g_failed; std::fprintf(stderr, "FAIL: %s  (%s:%d)\n", (msg), __FILE__, __LINE__); } \
} while (0)

using namespace khora::reservoir;

static std::vector<std::uint8_t> bytes_of(const std::string& s) {
    return std::vector<std::uint8_t>(s.begin(), s.end());
}

int main() {
    // ---- codec: lossless on every shape of data ----
    {
        EXPECT(codec::verify_roundtrip({}), "codec roundtrip: empty");
        EXPECT(codec::verify_roundtrip(bytes_of("a")), "codec roundtrip: single byte");
        EXPECT(codec::verify_roundtrip(bytes_of(std::string(5000, 'x'))),
               "codec roundtrip: long run");

        std::string rep;
        for (int i = 0; i < 2000; ++i) rep += "abcde";
        EXPECT(codec::verify_roundtrip(bytes_of(rep)), "codec roundtrip: repetitive");
        // Repetitive data must actually compress.
        EXPECT(codec::compress(bytes_of(rep)).size() < rep.size() / 2,
               "codec compresses repetitive data >2x");

        std::string prose =
            "It was the best of times, it was the worst of times, it was the age "
            "of wisdom, it was the age of foolishness, it was the epoch of belief, "
            "it was the epoch of incredulity. ";
        std::string book;
        for (int i = 0; i < 500; ++i) book += prose;
        EXPECT(codec::verify_roundtrip(bytes_of(book)), "codec roundtrip: prose");
        EXPECT(codec::compress(bytes_of(book)).size() < book.size(),
               "codec compresses prose");

        std::mt19937 rng(12345);
        std::vector<std::uint8_t> rnd(8000);
        for (auto& b : rnd) b = static_cast<std::uint8_t>(rng() & 0xFF);
        EXPECT(codec::verify_roundtrip(rnd), "codec roundtrip: random bytes");
    }

    // ---- distillation: artifacts removed ----
    {
        std::string raw =
            "The Project Gutenberg eBook of Test\r\n"
            "Some license boilerplate here.\r\n"
            "*** START OF THE PROJECT GUTENBERG EBOOK TEST ***\r\n"
            "\r\n\r\n\r\n"
            "Real <b>content</b> begins &amp; continues.\r\n"
            "A second line.\r\n\r\n\r\n\r\n"
            "Third paragraph after many blanks.\r\n"
            "*** END OF THE PROJECT GUTENBERG EBOOK TEST ***\r\n"
            "More license junk we do not want.\r\n";
        DistillStats st;
        const std::string clean = distill(raw, &st);

        EXPECT(st.stripped_gutenberg_header, "distill strips gutenberg header");
        EXPECT(st.stripped_gutenberg_footer, "distill strips gutenberg footer");
        EXPECT(clean.find("license boilerplate") == std::string::npos,
               "distill removes pre-start license text");
        EXPECT(clean.find("license junk") == std::string::npos,
               "distill removes post-end license text");
        EXPECT(clean.find("<b>") == std::string::npos, "distill removes html tags");
        EXPECT(clean.find("content") != std::string::npos, "distill keeps real content");
        EXPECT(clean.find('&') != std::string::npos && clean.find("&amp;") == std::string::npos,
               "distill decodes &amp; entity");
        EXPECT(clean.find('\r') == std::string::npos, "distill removes carriage returns");
        EXPECT(clean.find("\n\n\n") == std::string::npos, "distill collapses blank-line runs");
    }

    // ---- reservoir: admit, read-back exact, eviction, persistence ----
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "khora_reservoir_test";
        fs::remove_all(dir);

        // ~1.5 KB of low-redundancy distinct text per tome; cap holds ~2,
        // so admitting five genuinely forces eviction.
        auto make_text = [](char tag) {
            std::mt19937 rng(static_cast<unsigned>(tag) * 2654435761u);
            std::string s = "tome "; s += tag; s += '\n';
            while (s.size() < 1500) {
                const int len = 3 + static_cast<int>(rng() % 7);
                for (int k = 0; k < len; ++k)
                    s += static_cast<char>('a' + static_cast<int>(rng() % 26));
                s += (rng() % 8 == 0) ? '\n' : ' ';
            }
            return s;
        };
        {
            Reservoir res(dir, /*cap=*/4000);

            const auto rA = res.admit("A", "test", "urlA", make_text('A'));
            EXPECT(rA.ok, "admit A ok");
            EXPECT(rA.verified_lossless, "admit A verified lossless");

            // A is fully mastered and taught little -> lowest keep value.
            res.record_learning("A", /*yield*/0.0, /*mastery*/1.0);

            res.admit("B", "test", "urlB", make_text('B'));
            res.admit("C", "test", "urlC", make_text('C'));
            res.admit("D", "test", "urlD", make_text('D'));
            res.admit("E", "test", "urlE", make_text('E'));

            EXPECT(res.total_stored_bytes() <= res.cap_bytes(), "reservoir respects cap");
            EXPECT(!res.has("A"), "mastered low-value tome A evicted first");
            EXPECT(res.has("E"), "most recent tome E retained");

            // Read-back of a surviving tome equals its distilled text.
            const auto back = res.read("E");
            EXPECT(back.has_value(), "read E");
            if (back) {
                const std::string distilled = distill(make_text('E'));
                EXPECT(*back == distilled, "read-back equals distilled text exactly");
            }
        }

        // Persistence: reconstruct from the same dir.
        {
            Reservoir res2(dir, 6000);
            EXPECT(res2.count() >= 1, "catalog reloaded after reconstruction");
            EXPECT(res2.has("E"), "tome E survives reload");
            EXPECT(res2.read("E").has_value(), "tome E readable after reload");
        }
        fs::remove_all(dir);
    }

    // ---- aqueduct: seed catalog present & well-formed ----
    {
        const auto& seeds = seed_catalog();
        EXPECT(seeds.size() >= 10, "seed catalog has a real set of sources");
        bool all_https = true;
        for (const auto& s : seeds) {
            if (s.url.rfind("https://", 0) != 0) all_https = false;
            if (s.title.empty() || s.topic.empty()) all_https = false;
        }
        EXPECT(all_https, "all seeds are https with title+topic");
    }

    std::printf("\nReservoir tests: %d/%d passed (%d failed).\n",
                g_total - g_failed, g_total, g_failed);
    return g_failed == 0 ? 0 : 1;
}
