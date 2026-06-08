#pragma once

// The Morphic Cogitator — Khora's recursive thought cycle.
//
// Cognition here is not a single forward pass. It is a *resolve loop*
// built on one principle: there is no such thing as failure, only the
// trigger for the next attempt. When a thought fails to resonate with
// anything Khora knows, the Cogitator does not surrender — it:
//
//   1. ENCODE     tokenize -> bundle the stimulus into a probe glyph
//   2. RESONATE   fire the K nearest memories in parallel
//   3. if a resonance is strong enough -> CHOOSE it, the cycle resolves
//   4. otherwise (novelty):
//        a. spike Curiosity in the Soma (failure feeds drive)
//        b. DECOMPOSE the stimulus into its tokens, resonate each alone
//        c. SYNTHESIZE a hypothesis = bundle(probe, best fragments,
//           cortex projection) -- a guess assembled from partial knowledge
//        d. CONSOLIDATE: store the hypothesis as a provisional memory and
//           step the cortex on it -- Khora now knows something it didn't
//        e. RE-ATTEMPT resonance against the enriched memory
//      repeat until confident or max attempts reached.
//
// Even at the attempt cap the Cogitator never returns "no answer": it
// returns its strongest hypothesis and leaves Curiosity elevated so the
// background Reverie keeps working the problem. Every act of thought
// leaves Khora having learned.
//
// Composes Lexicon + Lattice + Cortex + Soma into one act of cognition.
// No LLM. The substrate resonating with, and extending, itself.

#include "khora/cortex/predictive_column.hpp"
#include "khora/lattice/lattice.hpp"
#include "khora/lexicon/lexicon.hpp"
#include "khora/soma/soma_nexus.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace khora::cogitator {

struct Thought {
    std::string                                stimulus;
    std::vector<std::string>                   tokens;
    khora::lattice::Glyph                      probe;             // bundled token glyphs
    std::vector<khora::lattice::LatticeMatch>  resonances;        // final-attempt K-NN
    khora::lattice::Glyph                      gestalt;           // probe + firing memories
    khora::lattice::Glyph                      hypothesis;        // synthesized guess
    khora::lattice::Glyph                      projection;        // cortex forecast
    std::size_t                                attempts = 0;      // resolve-loop iterations
    double                                     confidence = 0.0;  // best resonance sim
    double                                     valence = 0.0;     // soma evaluation
    bool                                       novel = true;      // never crossed threshold
    bool                                       learned_this_cycle = false; // consolidated a hypothesis
    std::string                                chosen_label;      // resolved answer, or empty
};

class Cogitator {
public:
    Cogitator(khora::lexicon::Lexicon&         lex,
              khora::lattice::Lattice&         memory,
              khora::cortex::PredictiveColumn& cortex,
              khora::soma::SomaNexus&          soma);

    // Tuning
    void set_resonance_k(std::size_t k)        { resonance_k_ = (k == 0 ? 1 : k); }
    void set_novelty_threshold(double t)       { novelty_threshold_ = t; }
    void set_max_resolve_attempts(std::size_t n) { max_attempts_ = (n == 0 ? 1 : n); }
    void set_learn_from_thoughts(bool b)       { learn_from_thoughts_ = b; }
    void set_consolidate_hypotheses(bool b)    { consolidate_hypotheses_ = b; }

    std::size_t resonance_k()         const noexcept { return resonance_k_; }
    double      novelty_threshold()   const noexcept { return novelty_threshold_; }
    std::size_t max_resolve_attempts()const noexcept { return max_attempts_; }
    bool        learn_from_thoughts() const noexcept { return learn_from_thoughts_; }

    // One act of thought — runs the full resolve loop.
    Thought think(std::string_view stimulus);

    // Stats
    std::size_t thoughts_completed() const noexcept { return thoughts_; }
    std::size_t novel_thoughts()     const noexcept { return novel_count_; }
    std::size_t hypotheses_formed()  const noexcept { return hypotheses_formed_; }
    std::size_t total_attempts()     const noexcept { return total_attempts_; }

private:
    khora::lattice::Glyph encode_(const std::vector<std::string>& tokens) const;
    khora::lattice::Glyph gestalt_(const khora::lattice::Glyph& probe,
                                   const std::vector<khora::lattice::LatticeMatch>& res) const;

    khora::lexicon::Lexicon&         lex_;
    khora::lattice::Lattice&         memory_;
    khora::cortex::PredictiveColumn& cortex_;
    khora::soma::SomaNexus&          soma_;

    std::size_t resonance_k_            = 5;
    double      novelty_threshold_      = 0.20;
    std::size_t max_attempts_           = 4;
    bool        learn_from_thoughts_    = true;
    bool        consolidate_hypotheses_ = true;

    std::size_t thoughts_          = 0;
    std::size_t novel_count_       = 0;
    std::size_t hypotheses_formed_ = 0;
    std::size_t total_attempts_    = 0;
    std::size_t hypothesis_seq_    = 0;
};

} // namespace khora::cogitator
