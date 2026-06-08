#pragma once

// Reverie Loom — Khora's offline simulation / consolidation subsystem.
//
// When the system isn't busy serving the operator, the Reverie Loom
// picks pairs of stored memories, perturbs them, bundles them into
// "dreams" (synthetic glyphs that weren't directly observed), asks
// the Cortex how familiar each dream feels, and asks the Soma Nexus
// whether the dream is satisfying given current drive state. Satisfying
// dreams are retained in a separate dream lattice.
//
// Composition: Lattice + Cortex + Soma → emergent imagination.

#include "khora/cortex/predictive_column.hpp"
#include "khora/lattice/lattice.hpp"
#include "khora/soma/soma_nexus.hpp"

#include <cstdint>

namespace khora::reverie {

struct DreamSample {
    khora::lattice::Glyph seed_a;       // first perturbed memory
    khora::lattice::Glyph seed_b;       // second perturbed memory
    khora::lattice::Glyph dream;        // bundled synthesis
    double                familiarity;  // cosine-sim of dream vs cortex prediction
    double                satisfaction; // soma evaluate(dream_affinity)
    bool                  retained;
};

class ReverieLoom {
public:
    ReverieLoom(khora::lattice::Lattice&             memory,
                khora::cortex::PredictiveColumn&     cortex,
                khora::soma::SomaNexus&              soma,
                std::uint64_t                        seed = 0xDEADC0DEFEEDFACEULL);

    // Tuning
    void set_perturbation_bits(std::size_t bits)        { perturbation_bits_ = bits; }
    void set_satisfaction_threshold(double t)           { threshold_ = t; }
    void set_dream_affinity(const khora::soma::Affinity& a) { dream_affinity_ = a; }

    // When consolidation is on, every retained dream is also fed into
    // the cortex via cortex.step(dream). Closes the loop: dreams become
    // training signal. Default: off.
    void set_consolidation(bool on) { consolidate_ = on; }
    bool consolidation() const noexcept { return consolidate_; }

    // Run one dream cycle.
    DreamSample dream_once();

    // Run N cycles. Returns count retained.
    std::size_t dream_n(std::size_t n);

    // Access
    const khora::lattice::Lattice& dreams() const noexcept { return dreams_; }

    // Stats
    std::size_t cycles()         const noexcept { return cycles_; }
    std::size_t retained()       const noexcept { return retained_count_; }
    std::size_t consolidations() const noexcept { return consolidations_done_; }

private:
    khora::lattice::Lattice&             memory_;
    khora::cortex::PredictiveColumn&     cortex_;
    khora::soma::SomaNexus&              soma_;
    khora::lattice::Lattice              dreams_;

    std::uint64_t                        rng_state_;
    std::size_t                          perturbation_bits_ = 100;
    double                               threshold_         = 0.5;
    khora::soma::Affinity                dream_affinity_;

    std::size_t                          cycles_              = 0;
    std::size_t                          retained_count_      = 0;
    std::size_t                          consolidations_done_ = 0;
    bool                                 consolidate_         = false;
};

} // namespace khora::reverie
