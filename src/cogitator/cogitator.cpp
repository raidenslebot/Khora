#include "khora/cogitator/cogitator.hpp"

#include <span>
#include <string>
#include <vector>

namespace khora::cogitator {

using khora::lattice::Glyph;
using khora::lattice::LatticeMatch;
using khora::lattice::bundle;
using khora::soma::Affinity;
using khora::soma::Drive;

Cogitator::Cogitator(khora::lexicon::Lexicon&         lex,
                     khora::lattice::Lattice&         memory,
                     khora::cortex::PredictiveColumn& cortex,
                     khora::soma::SomaNexus&          soma)
    : lex_(lex), memory_(memory), cortex_(cortex), soma_(soma) {}

Glyph Cogitator::encode_(const std::vector<std::string>& tokens) const {
    if (tokens.empty()) return Glyph::zero();
    std::vector<Glyph> gs;
    gs.reserve(tokens.size());
    for (const auto& t : tokens) gs.push_back(lex_.glyph_for(t));
    return bundle(std::span<const Glyph>{gs.data(), gs.size()});
}

Glyph Cogitator::gestalt_(const Glyph& probe,
                          const std::vector<LatticeMatch>& res) const {
    std::vector<Glyph> ingredients;
    ingredients.reserve(1 + res.size());
    if (probe.popcount() > 0) ingredients.push_back(probe);
    for (const auto& m : res) {
        if (auto g = memory_.recall(m.label)) ingredients.push_back(*g);
    }
    if (ingredients.empty()) return Glyph::zero();
    return bundle(std::span<const Glyph>{ingredients.data(), ingredients.size()});
}

Thought Cogitator::think(std::string_view stimulus) {
    Thought t;
    t.stimulus = std::string(stimulus);
    ++thoughts_;

    // ENCODE
    t.tokens = khora::lexicon::tokenize(stimulus);
    t.probe  = encode_(t.tokens);

    // The resolve loop. Each pass either resolves the thought or learns
    // something and tries again. Failure is fuel, never a terminus.
    for (std::size_t attempt = 0; attempt < max_attempts_; ++attempt) {
        ++t.attempts;
        ++total_attempts_;

        // RESONATE
        t.resonances.clear();
        if (memory_.size() > 0 && t.probe.popcount() > 0) {
            t.resonances = memory_.query(t.probe, resonance_k_);
        }
        t.confidence = t.resonances.empty() ? 0.0 : t.resonances.front().similarity;
        t.gestalt    = gestalt_(t.probe, t.resonances);

        // PROJECT — every thought is also experience for the cortex.
        if (learn_from_thoughts_ && t.gestalt.popcount() > 0) {
            cortex_.step(t.gestalt);
        }
        t.projection = cortex_.predict();

        // Did it resolve?
        if (t.confidence >= novelty_threshold_ && !t.resonances.empty()) {
            t.novel        = false;
            t.chosen_label = t.resonances.front().label;
            break;
        }

        // --- NOVELTY: failure becomes the trigger to learn. ---
        // a. Curiosity spikes — the drive that turns not-knowing into seeking.
        soma_.stimulate(Drive::Curiosity, +0.15);

        // b. DECOMPOSE: resonate each token alone, gather the best fragments
        //    Khora *does* partially recognise, even when the whole is alien.
        std::vector<Glyph> fragments;
        fragments.reserve(t.tokens.size() + 2);
        if (t.probe.popcount() > 0) fragments.push_back(t.probe);
        for (const auto& tok : t.tokens) {
            const Glyph tg = lex_.glyph_for(tok);
            if (memory_.size() > 0) {
                const auto fm = memory_.query(tg, 1);
                if (!fm.empty() && fm.front().similarity > novelty_threshold_ * 0.5) {
                    if (auto g = memory_.recall(fm.front().label)) fragments.push_back(*g);
                }
            }
            fragments.push_back(tg);
        }
        // Fold in the cortex's forward projection as an imaginative ingredient.
        if (t.projection.popcount() > 0) fragments.push_back(t.projection);

        // c. SYNTHESIZE a hypothesis from the partial knowledge.
        t.hypothesis = fragments.empty()
            ? t.probe
            : bundle(std::span<const Glyph>{fragments.data(), fragments.size()});

        // d. CONSOLIDATE: the hypothesis becomes a provisional memory and
        //    cortex experience. Khora now knows more than it did a moment ago.
        if (consolidate_hypotheses_ && t.hypothesis.popcount() > 0) {
            const std::string label = "hypothesis_" + std::to_string(hypothesis_seq_++);
            memory_.store(label, t.hypothesis);
            cortex_.step(t.hypothesis);
            ++hypotheses_formed_;
            t.learned_this_cycle = true;
        }

        // e. Enrich the probe toward the hypothesis so the next RESONATE
        //    pass searches from a more-informed position. (Failure changed us.)
        if (t.hypothesis.popcount() > 0) {
            t.probe = bundle({t.probe, t.hypothesis});
        }
        // loop: re-attempt
    }

    if (t.confidence < novelty_threshold_) {
        t.novel = true;
        ++novel_count_;
    }

    // EVALUATE — the Soma judges the whole act through its drive lattice.
    Affinity a{};
    a.per_drive[static_cast<std::size_t>(Drive::Curiosity)]        = t.novel ? 1.0 : 0.3;
    a.per_drive[static_cast<std::size_t>(Drive::Mastery)]          = t.confidence;
    a.per_drive[static_cast<std::size_t>(Drive::Preservation)]     = -0.05 * static_cast<double>(t.attempts);
    a.per_drive[static_cast<std::size_t>(Drive::Efficiency)]       = -0.05 * static_cast<double>(t.attempts);
    a.per_drive[static_cast<std::size_t>(Drive::OperatorAffinity)] = 1.0;
    t.valence = soma_.evaluate(a);

    return t;
}

} // namespace khora::cogitator
