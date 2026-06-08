#include "khora/cogitator/cogitator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <future>
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

// ----------------------- non-linear cognition: the Prism -----------------------

const char* lens_name(Lens l) noexcept {
    switch (l) {
        case Lens::Holistic:    return "holistic";
        case Lens::Leading:     return "leading";
        case Lens::Trailing:    return "trailing";
        case Lens::Broad:       return "broad";
        case Lens::Focused:     return "focused";
        case Lens::Curious:     return "curious";
        case Lens::Associative: return "associative";
        case Lens::Chaotic:     return "chaotic";
        default:                return "?";
    }
}

Facet Cogitator::explore_facet_(const std::vector<std::string>& tokens, Lens lens,
                                std::uint64_t entropy_seed) const {
    Facet f;
    f.lens = lens;

    // 1. Build a probe shaped by the lens — different facets literally
    //    look at different aspects of the stimulus.
    std::vector<Glyph> parts;
    parts.reserve(tokens.size());
    const std::size_t n = tokens.size();
    switch (lens) {
        case Lens::Leading: {
            const std::size_t half = (n + 1) / 2;
            for (std::size_t i = 0; i < half; ++i) parts.push_back(lex_.glyph_for(tokens[i]));
            break;
        }
        case Lens::Trailing: {
            const std::size_t start = n / 2;
            for (std::size_t i = start; i < n; ++i) parts.push_back(lex_.glyph_for(tokens[i]));
            break;
        }
        default:
            for (const auto& tok : tokens) parts.push_back(lex_.glyph_for(tok));
            break;
    }
    f.probe = parts.empty() ? Glyph::zero()
                            : bundle(std::span<const Glyph>{parts.data(), parts.size()});

    // The chaotic lens injects entropy — it explores a perturbed nearby
    // region of the manifold, the engine's way of courting the unexpected.
    if (lens == Lens::Chaotic && f.probe.popcount() > 0) {
        std::uint64_t s = entropy_seed;
        const std::size_t flips = khora::lattice::kGlyphBits / 50;  // ~2% perturbation
        for (std::size_t i = 0; i < flips; ++i) {
            s = s * 6364136223846793005ULL + 1442695040888963407ULL;
            f.probe.flip_bit(static_cast<std::size_t>(s % khora::lattice::kGlyphBits));
        }
    }

    // The associative lens follows the cortex's forward projection instead
    // of the literal stimulus — thinking about what tends to come next.
    Glyph query_probe = f.probe;
    if (lens == Lens::Associative) {
        const Glyph proj = cortex_.predict();
        if (proj.popcount() > 0) {
            query_probe = (f.probe.popcount() > 0) ? bundle({f.probe, proj}) : proj;
        }
    }

    // 2. Resonate with a lens-specific breadth.
    std::size_t k = resonance_k_;
    if (lens == Lens::Broad)   k = std::max<std::size_t>(resonance_k_, 8);
    if (lens == Lens::Focused) k = 1;
    if (lens == Lens::Curious) k = std::max<std::size_t>(resonance_k_, 4);

    if (memory_.size() > 0 && query_probe.popcount() > 0) {
        f.resonances = memory_.query(query_probe, k);
    }

    // 3. Choose this facet's candidate.
    std::size_t pick = 0;
    if (lens == Lens::Curious && f.resonances.size() > 1) {
        // Deliberately chase a non-obvious alternative (second-best) — the
        // facet that questions the obvious answer.
        pick = 1;
    }
    if (!f.resonances.empty()) {
        f.confidence = f.resonances.front().similarity;       // confidence = best available
        const auto& chosen = f.resonances[std::min(pick, f.resonances.size() - 1)];
        f.label = chosen.label;
        if (auto g = memory_.recall(chosen.label)) f.candidate = *g;
        else                                       f.candidate = query_probe;
    } else {
        f.candidate  = query_probe;
        f.confidence = 0.0;
    }
    f.novel = f.confidence < novelty_threshold_;

    // 4. Score this facet through the drives — each lens flatters a
    //    different drive, so the Soma's current mood tilts the contest.
    Affinity a{};
    a.per_drive[static_cast<std::size_t>(Drive::Curiosity)] =
        (lens == Lens::Curious || lens == Lens::Chaotic) ? 1.0 : (f.novel ? 0.6 : 0.2);
    a.per_drive[static_cast<std::size_t>(Drive::Mastery)]          = f.confidence;
    a.per_drive[static_cast<std::size_t>(Drive::Efficiency)] =
        (lens == Lens::Focused) ? 0.5 : (lens == Lens::Broad ? -0.2 : 0.0);
    a.per_drive[static_cast<std::size_t>(Drive::OperatorAffinity)] = 1.0;
    f.valence = soma_.evaluate(a);
    return f;
}

Deliberation Cogitator::deliberate(std::string_view stimulus, std::size_t facets) {
    Deliberation d;
    d.stimulus = std::string(stimulus);
    d.tokens   = khora::lexicon::tokenize(stimulus);
    ++deliberations_;

    const std::size_t n_lenses = std::min<std::size_t>(
        facets, static_cast<std::size_t>(Lens::_Count));
    if (n_lenses == 0) return d;

    // Spawn the facets to explore CONCURRENTLY. Exploration is read-only
    // over memory / lexicon / cortex, so genuine parallelism is safe — the
    // chorus thinks at once, not in turn.
    std::vector<std::future<Facet>> futures;
    futures.reserve(n_lenses);
    for (std::size_t i = 0; i < n_lenses; ++i) {
        const Lens lens = static_cast<Lens>(i);
        const std::uint64_t seed =
            0xC0FFEEULL ^ (static_cast<std::uint64_t>(deliberations_) << 20) ^ (i * 0x9E3779B9ULL);
        futures.push_back(std::async(std::launch::async,
            [this, &d, lens, seed] { return explore_facet_(d.tokens, lens, seed); }));
    }
    d.facets.reserve(n_lenses);
    for (auto& fut : futures) d.facets.push_back(fut.get());

    // Arbitrate: the facet with the highest drive-weighted valence wins.
    d.winner = 0;
    for (std::size_t i = 1; i < d.facets.size(); ++i) {
        if (d.facets[i].valence > d.facets[d.winner].valence) d.winner = static_cast<int>(i);
    }

    // Coherence: mean pairwise similarity of the facet candidates — how
    // much the chorus agrees. Entropy: spread of valences — the chaos in
    // the contest.
    {
        double sim_sum = 0.0; std::size_t pairs = 0;
        for (std::size_t i = 0; i < d.facets.size(); ++i)
            for (std::size_t j = i + 1; j < d.facets.size(); ++j) {
                sim_sum += d.facets[i].candidate.similarity(d.facets[j].candidate);
                ++pairs;
            }
        d.coherence = pairs ? sim_sum / static_cast<double>(pairs) : 1.0;

        double mean_v = 0.0;
        for (const auto& f : d.facets) mean_v += f.valence;
        mean_v /= static_cast<double>(d.facets.size());
        double var = 0.0;
        for (const auto& f : d.facets) var += (f.valence - mean_v) * (f.valence - mean_v);
        d.entropy = std::sqrt(var / static_cast<double>(d.facets.size()));
    }

    // Collapse: bundle the coalition of facets that AGREE with the winner
    // (candidate similarity above threshold). Concord reinforces; dissent
    // is dropped. Meaning emerges from the coherent whole.
    const Glyph& win_cand = d.facets[d.winner].candidate;
    std::vector<Glyph> coalition;
    for (const auto& f : d.facets) {
        if (f.candidate.popcount() == 0) continue;
        if (&f == &d.facets[d.winner] || f.candidate.similarity(win_cand) > 0.3)
            coalition.push_back(f.candidate);
    }
    d.collapsed = coalition.empty() ? win_cand
        : bundle(std::span<const Glyph>{coalition.data(), coalition.size()});
    d.chosen_label = d.facets[d.winner].novel ? std::string{} : d.facets[d.winner].label;

    // Consolidate the collapsed thought into memory + cortex (serial; the
    // exploration was read-only, the learning happens once, here).
    if (consolidate_hypotheses_ && d.collapsed.popcount() > 0) {
        const std::string label = "deliberation_" + std::to_string(hypothesis_seq_++);
        memory_.store(label, d.collapsed);
        if (learn_from_thoughts_) cortex_.step(d.collapsed);
        d.learned = true;
    }
    return d;
}

namespace {
bool is_trace(const std::string& label) {
    return label.rfind("deliberation_", 0) == 0 || label.rfind("hypothesis_", 0) == 0;
}

// The concept a deliberation lands on — the strongest real resonance
// across all facets, skipping transient trace concepts and any concept in
// `exclude`. Excluding the whole visited set forces the train onward into
// fresh territory rather than collapsing onto a central hub in two hops.
std::string landed_concept(const Deliberation& d,
                           const std::vector<std::string>& exclude) {
    double best = -2.0; std::string best_label;
    for (const auto& f : d.facets) {
        for (const auto& m : f.resonances) {
            if (is_trace(m.label)) continue;
            if (std::find(exclude.begin(), exclude.end(), m.label) != exclude.end()) continue;
            if (m.similarity > best) { best = m.similarity; best_label = m.label; }
        }
    }
    return best_label;
}
} // namespace

Rumination Cogitator::ruminate(std::string_view stimulus, std::size_t max_depth) {
    Rumination r;
    r.seed = std::string(stimulus);
    std::string current = r.seed;
    std::vector<std::string> visited;

    r.train.push_back(current);      // the seed is the first stop
    for (std::size_t depth = 0; depth < max_depth; ++depth) {
        Deliberation d = deliberate(current);

        // Hop to the strongest concept the train has NOT yet visited, so it
        // explores fresh territory each step instead of orbiting a hub.
        const std::string fresh = landed_concept(d, visited);
        if (!fresh.empty()) {
            r.train.push_back(fresh);
            visited.push_back(fresh);
            current = fresh;
            r.chain.push_back(std::move(d));
            continue;
        }

        // No unvisited concept resonates — the neighbourhood is exhausted.
        // The strongest concept overall is the attractor the train keeps
        // returning to: that recurring pull is the conclusion.
        const std::string attractor = landed_concept(d, {});
        r.chain.push_back(std::move(d));
        if (!attractor.empty()) {
            r.converged  = true;
            r.conclusion = attractor;
        }
        break;
    }

    if (r.conclusion.empty() && !r.train.empty()) r.conclusion = r.train.back();
    return r;
}

} // namespace khora::cogitator
