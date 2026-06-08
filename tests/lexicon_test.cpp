// Tests for the Lexicon — semantic encoding and cooccurrence drift.

#include "khora/lexicon/lexicon.hpp"

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

using namespace khora::lexicon;

int main() {
    // 1. Empty / trivial inputs do not crash.
    {
        const auto g = encode_token("");
        EXPECT(g == khora::lattice::Glyph::zero(), "empty token gives zero glyph");
        const auto g2 = encode_token("!!!");
        EXPECT(g2 == khora::lattice::Glyph::zero(), "non-alnum-only gives zero glyph");
    }

    // 2. Same word, same glyph (deterministic).
    {
        EXPECT(encode_token("cat") == encode_token("cat"), "deterministic encoding");
        EXPECT(encode_token("Cat") == encode_token("cat"), "case-insensitive");
        EXPECT(encode_token("c-a-t") == encode_token("cat"), "non-alnum stripped");
    }

    // 3. Structural similarity: "cat" / "cats" share many trigrams.
    //    Empirically ~0.37 from majority-bundle math; well above the
    //    ~0.0 baseline of unrelated words.
    {
        const auto s = encode_token("cat").similarity(encode_token("cats"));
        EXPECT(s > 0.25, "cat ~ cats (structural similarity, threshold 0.25)");
    }

    // 4. Typo tolerance: "instal" still close to "install".
    {
        const auto s = encode_token("install").similarity(encode_token("instal"));
        EXPECT(s > 0.4, "install ~ instal (typo tolerance)");
    }

    // 5. Unrelated words are nearly orthogonal.
    {
        const auto s = encode_token("aardvark").similarity(encode_token("zephyr"));
        EXPECT(s < 0.2, "aardvark vs zephyr (orthogonal)");
    }

    // 6. Cooccurrence drift makes neighbours more similar.
    {
        Lexicon lex;
        const double before = lex.similarity("cat", "purr");
        for (int i = 0; i < 50; ++i) {
            lex.expose_text("the cat began to purr softly", 3);
        }
        const double after = lex.similarity("cat", "purr");
        EXPECT(after > before, "cooccurrence increases cat~purr similarity");
        EXPECT(lex.has("cat"), "cat is in lexicon after exposure");
        EXPECT(lex.vocabulary_size() > 0, "vocabulary grew");
    }

    // 7. Cooccurrence does NOT make distant unrelated words similar.
    {
        Lexicon lex;
        for (int i = 0; i < 50; ++i) lex.expose_text("the cat began to purr softly", 3);
        const double s_unrelated = lex.similarity("cat", "zephyr");
        EXPECT(s_unrelated < 0.3, "cat~zephyr still orthogonal after cat~purr training");
    }

    // 8. Tokenize handles punctuation, case, multiple spaces.
    {
        const auto toks = tokenize("Hello,   World! Don't break.");
        EXPECT(toks.size() == 5, "tokenize: 5 tokens (Hello, World, Don, t, break)");
        EXPECT(toks[0] == "hello", "tokenize: lowercased");
        EXPECT(toks[1] == "world", "tokenize: World");
    }

    std::printf("\nLexicon tests: %d/%d passed (%d failed).\n",
                g_total - g_failed, g_total, g_failed);
    return g_failed == 0 ? 0 : 1;
}
