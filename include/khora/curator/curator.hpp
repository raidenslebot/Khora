#pragma once

// The Curator — Khora's autonomous knowledge director.
//
// Khora decides for itself what to learn. The Curator surveys its liquid
// knowledge (the Reservoir) and its absorption state, then takes the next
// most valuable knowledge action without being told:
//
//   STUDY   : a tome it holds but has not yet absorbed -> learn it.
//   FORAGE  : a topic it has no material on -> acquire a source.
//   DEEPEN  : everything held is studied -> acquire more breadth/depth.
//   IDLE    : the seed catalogue is exhausted (for now).
//
// This closes the loop: detect need -> acquire -> absorb -> seek next.
// The same study faculty is exposed as a reusable function so the runtime
// and the Curator share one implementation.

#include "khora/cortex/predictive_column.hpp"
#include "khora/lattice/lattice.hpp"
#include "khora/lexicon/lexicon.hpp"
#include "khora/reservoir/aqueduct.hpp"
#include "khora/reservoir/reservoir.hpp"

#include <cstddef>
#include <string>

namespace khora::curator {

struct StudyOutcome {
    bool         ok = false;
    std::string  title;
    std::size_t  tokens = 0;
    std::size_t  vocab_before = 0;
    std::size_t  vocab_after = 0;
    double       acc_before = 0.0;
    double       acc_after = 0.0;
    double       yield = 0.0;
    double       mastery = 0.0;
    std::size_t  cooccurrences = 0;
    std::string  error;
};

// Read a tome and absorb it into the live Lexicon + Cortex, crediting the
// learning back to the Reservoir. Shared by the Curator and the runtime.
// If `concept_space` is non-null, the most salient words learned are
// promoted into it as thinkable concepts (so cognition can resonate over
// studied vocabulary, not just hand-memorized concepts).
StudyOutcome study_tome(khora::reservoir::Reservoir& pool,
                        khora::lexicon::Lexicon& lex,
                        khora::cortex::PredictiveColumn& cortex,
                        const std::string& title,
                        std::size_t max_tokens = 200000,
                        khora::lattice::Lattice* concept_space = nullptr);

struct Decision {
    enum Kind { Study, Forage, Deepen, Idle } kind = Idle;
    std::string topic;
    std::string title;
    std::string rationale;
};

class Curator {
public:
    Curator(khora::reservoir::Reservoir& pool,
            khora::reservoir::Aqueduct&  aqueduct,
            khora::lexicon::Lexicon&     lex,
            khora::cortex::PredictiveColumn& cortex,
            khora::lattice::Lattice*     concept_space = nullptr);

    // Decide the next knowledge action without executing it.
    Decision decide() const;

    // Decide and execute one knowledge action. Returns a human-readable
    // account of what Khora chose to do and what came of it.
    std::string act(std::size_t study_tokens = 60000);

    std::size_t studies()  const noexcept { return studies_; }
    std::size_t forages()  const noexcept { return forages_; }

    void set_mastery_target(double t) { mastery_target_ = t; }

private:
    khora::reservoir::Reservoir&      pool_;
    khora::reservoir::Aqueduct&       aqueduct_;
    khora::lexicon::Lexicon&          lex_;
    khora::cortex::PredictiveColumn&  cortex_;
    khora::lattice::Lattice*          concept_space_ = nullptr;

    double      mastery_target_ = 0.6;
    std::size_t studies_ = 0;
    std::size_t forages_ = 0;
};

} // namespace khora::curator
