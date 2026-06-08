// Tests for the Stratiform Cortex's PredictiveColumn.

#include "khora/cortex/predictive_column.hpp"

#include <cstdio>
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

using khora::lattice::Glyph;
using khora::cortex::PredictiveColumn;

namespace {
// Map a character to a stable random Glyph for symbolic-sequence tests.
Glyph glyph_for(char c) {
    char s[2] = { c, '\0' };
    return Glyph::from_hash(s);
}
} // namespace

int main() {
    // 1. Cold-start step on an empty column.
    {
        PredictiveColumn col(3);
        auto r = col.step(glyph_for('a'));
        EXPECT(r.novel_context, "first input has novel context");
        EXPECT(col.observations() == 1, "observations advances");
        EXPECT(col.associations() == 0, "no associations stored from first input alone");
    }

    // 2. Two-step pattern: after "ab" once, predicting 'b' after 'a' on
    //    the second exposure should be very accurate.
    {
        PredictiveColumn col(2);
        const auto a = glyph_for('a');
        const auto b = glyph_for('b');
        col.step(a);   // observation 1
        col.step(b);   // learn (ctx={a} -> b)
        col.step(a);   // observation 3, context={a,b}-ish
        auto r = col.step(b);  // should predict b from ctx={a}+something
        // After learning once, accuracy on a familiar transition should be very high.
        EXPECT(r.similarity > 0.9, "after one ab exposure, b after a predicted accurately");
    }

    // 3. Repeating 5-glyph loop: after training, recent_accuracy must be high.
    {
        PredictiveColumn col(2);
        const std::string loop = "abcde";
        std::vector<Glyph> seq;
        for (char c : loop) seq.push_back(glyph_for(c));

        for (int cycle = 0; cycle < 40; ++cycle) {
            for (const auto& g : seq) col.step(g);
        }
        const double acc = col.recent_accuracy();
        EXPECT(acc > 0.9, "after 40 cycles of abcde, recent accuracy > 0.9");
    }

    // 4. Novelty detection: after training, a stretch of *context* drawn
    //    from outside the training distribution flags novel_context.
    //    Note: novel_context is about the column's recent-context window,
    //    not the latest single input — one foreign glyph still sees a
    //    familiar prior context. Feed several in a row to shift context.
    {
        PredictiveColumn col(2);
        const std::string loop = "abcabc";
        for (int cycle = 0; cycle < 20; ++cycle) {
            for (char c : loop) col.step(glyph_for(c));
        }
        col.step(glyph_for('X'));            // context still familiar
        col.step(glyph_for('Y'));            // context now {a-or-b-or-c, X} — unfamiliar
        auto r = col.step(glyph_for('Z'));   // context now {X, Y} — definitely unseen
        EXPECT(r.novel_context, "out-of-distribution context flagged as novel");
    }

    // 5. Predict-without-learn is stable: calling predict() twice returns same glyph.
    {
        PredictiveColumn col(2);
        for (int i = 0; i < 10; ++i) col.step(glyph_for('a' + (i % 5)));
        auto p1 = col.predict();
        auto p2 = col.predict();
        EXPECT(p1 == p2, "predict() is pure / repeatable");
        EXPECT(col.observations() == 10, "predict() does not advance observations");
    }

    // 6. Predict-without-learn returns zero on a cold column.
    {
        PredictiveColumn col(2);
        auto p = col.predict();
        EXPECT(p == Glyph::zero(), "cold predict returns zero glyph");
    }

    std::printf("\nCortex tests: %d/%d passed (%d failed).\n",
                g_total - g_failed, g_total, g_failed);
    return g_failed == 0 ? 0 : 1;
}
