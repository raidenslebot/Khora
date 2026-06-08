// Cortex demo — train a PredictiveColumn on a repeating symbolic sequence
// and print the learning curve. This is real online learning: the column
// starts with zero knowledge, accumulates associations as it observes the
// stream, and its recent-accuracy climbs from ~0 toward 1.0.

#include "khora/cortex/predictive_column.hpp"

#include <cstdio>
#include <string>
#include <vector>

using khora::lattice::Glyph;
using khora::cortex::PredictiveColumn;

namespace {
Glyph glyph_for(char c) {
    char s[2] = { c, '\0' };
    return Glyph::from_hash(s);
}
}

int main() {
    PredictiveColumn col(3);

    const std::string corpus_unit = "the quick brown fox jumps over the lazy dog ";
    constexpr int CYCLES = 50;

    std::printf("Cortex demo — training column on a repeating English phrase.\n");
    std::printf("  context_window = 3   cycles = %d   chars/cycle = %zu\n\n",
                CYCLES, corpus_unit.size());
    std::printf("  step | sim     | err   | novel | associations | recent_acc\n");
    std::printf("  -----+---------+-------+-------+--------------+-----------\n");

    int step_no = 0;
    int report_every = static_cast<int>(corpus_unit.size());  // one line per cycle

    for (int cycle = 0; cycle < CYCLES; ++cycle) {
        for (char c : corpus_unit) {
            auto r = col.step(glyph_for(c));
            ++step_no;
            if (step_no % report_every == 0) {
                std::printf("  %4d | %+0.4f | %5zu | %5s | %12zu | %0.4f\n",
                            step_no, r.similarity, r.prediction_error,
                            r.novel_context ? "yes" : "no",
                            col.associations(), col.recent_accuracy());
            }
        }
    }

    const double final_acc = col.recent_accuracy();
    std::printf("\nFinal recent_accuracy: %.4f (%zu observations, %zu associations)\n",
                final_acc, col.observations(), col.associations());
    if (final_acc > 0.85) {
        std::printf("PASS  Cortex learned the repeating pattern (similarity > 0.85).\n");
        return 0;
    } else {
        std::printf("FAIL  Cortex did not converge (similarity %.4f).\n", final_acc);
        return 1;
    }
}
