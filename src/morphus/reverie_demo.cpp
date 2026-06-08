// Reverie demo — train a cortex briefly, then run dream cycles.
// Demonstrates the three core subsystems composing into emergent imagination.

#include "khora/reverie/reverie_loom.hpp"

#include <cstdio>
#include <string>

using khora::lattice::Glyph;
using khora::lattice::Lattice;
using khora::cortex::PredictiveColumn;
using khora::soma::SomaNexus;
using khora::soma::Drive;
using khora::reverie::ReverieLoom;

namespace {
Glyph glyph_for(char c) {
    char s[2] = { c, '\0' };
    return Glyph::from_hash(s);
}
}

int main() {
    Lattice memory;
    PredictiveColumn cortex(3);
    SomaNexus soma;

    // 1. Build a memory of 200 random concept-glyphs.
    for (int i = 0; i < 200; ++i) {
        memory.store("concept_" + std::to_string(i), Glyph::random(0x2000 + i));
    }

    // 2. Train the cortex briefly on a small repeating sequence so it
    //    has a non-trivial prediction state during dreaming.
    const std::string train = "the quick brown fox ";
    for (int cycle = 0; cycle < 20; ++cycle) {
        for (char c : train) cortex.step(glyph_for(c));
    }

    // 3. Bias the soma to favour curious / masterful drives.
    soma.stimulate(Drive::Curiosity, +0.3);
    soma.stimulate(Drive::Mastery,   +0.2);

    // 4. Configure and run the Reverie Loom.
    ReverieLoom loom(memory, cortex, soma);
    loom.set_perturbation_bits(120);          // ~1.2% bit flips
    loom.set_satisfaction_threshold(0.4);     // moderate

    std::printf("Reverie Loom demo\n");
    std::printf("  memory size       : %zu glyphs\n", memory.size());
    std::printf("  cortex associations: %zu\n", cortex.associations());
    std::printf("  cortex recent_acc : %.4f\n", cortex.recent_accuracy());
    std::printf("\n");

    constexpr std::size_t N_CYCLES = 1000;
    double sum_sat = 0.0;
    double sum_fam = 0.0;
    for (std::size_t i = 0; i < N_CYCLES; ++i) {
        const auto s = loom.dream_once();
        sum_sat += s.satisfaction;
        sum_fam += s.familiarity;
    }

    std::printf("  cycles            : %zu\n", loom.cycles());
    std::printf("  dreams retained   : %zu  (%.1f%%)\n",
                loom.retained(),
                100.0 * static_cast<double>(loom.retained()) / static_cast<double>(loom.cycles()));
    std::printf("  mean satisfaction : %.4f\n", sum_sat / static_cast<double>(N_CYCLES));
    std::printf("  mean familiarity  : %.4f\n", sum_fam / static_cast<double>(N_CYCLES));
    std::printf("  dream lattice size: %zu\n", loom.dreams().size());

    // Probe: how similar are the first few retained dreams to the original memories?
    if (loom.dreams().size() >= 3) {
        std::printf("\n  Sample dream similarities to nearest source memory:\n");
        std::size_t shown = 0;
        for (const auto& [label, dream_glyph] : loom.dreams()) {
            const auto matches = memory.query(dream_glyph, 1);
            if (!matches.empty()) {
                std::printf("    %s  vs  %s  sim=%+.4f\n",
                            label.c_str(), matches[0].label.c_str(), matches[0].similarity);
            }
            if (++shown >= 5) break;
        }
    }
    return 0;
}
