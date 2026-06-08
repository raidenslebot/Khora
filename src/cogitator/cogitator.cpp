#include "khora/cogitator/cogitator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
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

Glyph Cogitator::token_glyph_(const std::string& tok) const {
    const Glyph g = lex_.context_glyph(tok);            // distributional meaning
    return g.popcount() > 0 ? g : lex_.glyph_for(tok);  // structural fallback if unlearned
}

Glyph Cogitator::encode_(const std::vector<std::string>& tokens) const {
    if (tokens.empty()) return Glyph::zero();
    std::vector<Glyph> gs;
    gs.reserve(tokens.size());
    for (const auto& t : tokens) if (is_content_(t)) gs.push_back(token_glyph_(t));
    if (gs.empty())  // all function words — fall back to the whole stimulus
        for (const auto& t : tokens) gs.push_back(token_glyph_(t));
    return bundle(std::span<const Glyph>{gs.data(), gs.size()});
}

Glyph Cogitator::gestalt_(const Glyph& probe,
                          const std::vector<LatticeMatch>& res) const {
    std::vector<Glyph> ingredients;
    ingredients.reserve(1 + res.size());
    if (probe.popcount() > 0) ingredients.push_back(probe);
    for (const auto& m : res) {
        const Glyph g = recall_(m.label);
        if (g.popcount() > 0) ingredients.push_back(g);
    }
    if (ingredients.empty()) return Glyph::zero();
    return bundle(std::span<const Glyph>{ingredients.data(), ingredients.size()});
}

// (Re)index the resonance field from the Lexicon whenever the vocabulary
// has changed. The field is every learned word's glyph_for — cognition's
// probe (a bundle of glyph_for tokens) lives in the same space, so the
// match is exact. Rebuilds are lazy: one per vocabulary change, never
// mid-deliberation.
void Cogitator::ensure_field_() {
    const std::size_t v = lex_.vocabulary_size();
    if (v == indexed_vocab_) return;

    // Resonate over CONTENT words only — the salient vocabulary, with the
    // ubiquitous function words already filtered out — so cognition lands on
    // meaning, not connective tissue. Fall back to the whole field for a
    // small lexicon.
    std::vector<std::pair<std::string, Glyph>> entries;
    content_.clear();
    {
        const auto salient = lex_.salient_tokens(200000, 3);
        entries.reserve(salient.size());
        for (const auto& w : salient) entries.emplace_back(w, lex_.context_glyph(w));
        if (entries.size() < 50) {
            entries = lex_.context_field();   // tiny lexicon: keep content_ empty
        } else {
            content_.insert(salient.begin(), salient.end());
        }
    }
    field_.build(entries);

    // Demote any residual distributional hubs (centrality outliers, mean+2σ)
    // so thought resonates with content, not connective tissue. The surviving
    // labels become the clean concept set the Volition seeds thought from.
    bool demoted = false;
    if (entries.size() > 64) {
        const auto deg = field_.centrality(10);
        if (deg.size() == entries.size()) {
            double mean = 0.0;
            for (auto dd : deg) mean += dd;
            mean /= static_cast<double>(deg.size());
            double var = 0.0;
            for (auto dd : deg) { const double e = dd - mean; var += e * e; }
            var /= static_cast<double>(deg.size());
            const double cut = mean + 2.0 * std::sqrt(var);
            std::vector<std::pair<std::string, Glyph>> clean;
            clean.reserve(entries.size());
            for (std::size_t i = 0; i < entries.size(); ++i)
                if (deg[i] <= cut) clean.push_back(entries[i]);   // copy: keep entries intact
            if (clean.size() >= 2 && clean.size() < entries.size()) {
                field_.build(clean);
                concepts_.clear();
                concepts_.reserve(clean.size());
                for (auto& e : clean) concepts_.push_back(e.first);
                demoted = true;
            }
        }
    }
    if (!demoted) {
        concepts_.clear();
        concepts_.reserve(entries.size());
        for (auto& e : entries) concepts_.push_back(e.first);
    }
    indexed_vocab_ = v;
}

// Single-probe resonance: the Lexicon field if populated, else the
// provisional memory_. Callers on the linear path (think) only.
std::vector<LatticeMatch> Cogitator::resonate_(const Glyph& probe, std::size_t k) const {
    if (field_.size() > 0) return field_.query(probe, k);
    if (memory_.size() > 0) return memory_.query(probe, k);
    return {};
}

// Batched resonance: every probe in one shot. One GPU dispatch over the
// field when active, so a deliberation's facets never contend for the
// device. CPU/memory fallback preserves behaviour with no field.
std::vector<std::vector<LatticeMatch>> Cogitator::resonate_batch_(
    const std::vector<Glyph>& probes, std::size_t k) const {
    if (field_.size() > 0) return field_.query_batch(probes, k);
    std::vector<std::vector<LatticeMatch>> out;
    out.reserve(probes.size());
    for (const auto& p : probes)
        out.push_back(memory_.size() > 0 ? memory_.query(p, k) : std::vector<LatticeMatch>{});
    return out;
}

// Recover the glyph behind a resonance label: a coined concept from memory_,
// or a learned word's glyph from the Lexicon. Zero if neither knows it.
Glyph Cogitator::recall_(const std::string& label) const {
    if (auto g = memory_.recall(label)) return *g;
    if (lex_.has(label)) return lex_.glyph_for(label);
    return Glyph::zero();
}

std::string Cogitator::wandering_seed(std::uint64_t n) {
    ensure_field_();
    if (concepts_.empty()) return std::string{};
    // concepts_ is exposure-ordered; the head is still function-word-heavy
    // even after hub demotion, so skip it and wander the content body.
    const std::size_t skip = std::min<std::size_t>(concepts_.size() / 2, 60);
    const std::size_t span = (concepts_.size() > skip) ? concepts_.size() - skip
                                                       : concepts_.size();
    const std::size_t base = (concepts_.size() > skip) ? skip : 0;
    return concepts_[base + (n % span)];
}

std::string Cogitator::focused_seed(std::uint64_t n) {
    if (attractors_.empty()) return wandering_seed(n);
    const auto top = top_attractors(8);
    if (top.empty()) return wandering_seed(n);
    return top[n % top.size()].first;
}

std::string Cogitator::utter(const std::string& topic, std::size_t n) {
    ensure_field_();
    if (n == 0) return {};
    const Glyph topicG = lex_.glyph_for(topic);

    // Decoder over the full glyph_for field (any generated token can resolve).
    maelstrom::Resonator dec(256);
    dec.build(lex_.semantic_field());

    std::vector<Glyph> ctx;
    ctx.push_back(topicG);
    std::string out, last;
    for (std::size_t s = 0; s < n; ++s) {
        const auto cands = cortex_.predict_candidates(ctx, 6);
        if (cands.empty()) break;
        double best = -1e9; std::string word; Glyph wg; int rank = 0;
        for (const auto& cg : cands) {
            const auto d = dec.query(cg, 1);
            ++rank;
            if (d.empty()) continue;
            const std::string w = d.front().label;
            const Glyph wgl = lex_.glyph_for(w);
            const double score = (1.0 - 0.12 * (rank - 1)) + 0.8 * wgl.similarity(topicG);
            if (w != last && score > best) { best = score; word = w; wg = wgl; }
        }
        if (word.empty()) break;
        if (!out.empty()) out += ' ';
        out += word; last = word;
        ctx.push_back(wg);
        if (ctx.size() > 8) ctx.erase(ctx.begin());
    }
    return out;
}

Synthesis Cogitator::synthesize(const std::string& a_in, const std::string& b_in, std::uint64_t seed) {
    ensure_field_();
    Synthesis s;
    std::string a = a_in, b = b_in;

    // Unspecified parents -> Khora picks distant concepts itself (pure chaos:
    // the more distant the collision, the more entropy to turn into beauty).
    if ((a.empty() || b.empty()) && !concepts_.empty()) {
        if (a.empty()) a = wandering_seed(seed);
        if (b.empty()) {
            const Glyph ga0 = token_glyph_(a);
            double worst = 2.0;
            std::string far;
            for (int t = 0; t < 16; ++t) {
                const std::string cand = wandering_seed(seed * 2654435761ull + static_cast<std::uint64_t>(t) * 7 + 1);
                if (cand.empty() || cand == a) continue;
                const double sim = ga0.similarity(token_glyph_(cand));
                if (sim < worst) { worst = sim; far = cand; }   // most distant wins
            }
            b = far.empty() ? wandering_seed(seed + 1) : far;
        }
    }
    s.a = a; s.b = b;
    if (a.empty() || b.empty()) return s;

    const Glyph ga = token_glyph_(a);
    const Glyph gb = token_glyph_(b);
    if (ga.popcount() == 0 || gb.popcount() == 0) return s;
    s.tension = 1.0 - ga.similarity(gb);

    // Superpose the two into a chimera, then see what concept that collision
    // evokes that is NEITHER parent — the idea forged from their tension.
    const Glyph chimera = bundle({ga, gb});
    auto hits = (field_.size() > 0) ? field_.query(chimera, 8) : memory_.query(chimera, 8);
    for (auto& h : hits) {
        if (h.label == a || h.label == b) continue;
        s.emergent.push_back(h);
        if (s.emergent.size() >= 4) break;
    }
    // What chaos forges is itself a place thought has landed.
    if (!s.emergent.empty()) note_attractor_(s.emergent.front().label);
    return s;
}

void Cogitator::note_attractor_(const std::string& label) {
    if (label.empty()) return;
    // Skip Khora's own provisional trace concepts — only real, learned
    // concepts count as preoccupations.
    if (label.rfind("deliberation_", 0) == 0 || label.rfind("hypothesis_", 0) == 0) return;
    ++attractors_[label];
}

void Cogitator::save_attractors(const std::filesystem::path& path) const {
    namespace fs = std::filesystem;
    if (path.has_parent_path()) fs::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::trunc);
    if (!f) return;
    for (const auto& [name, count] : attractors_)
        if (!name.empty()) f << count << ' ' << name << '\n';
}

void Cogitator::load_attractors(const std::filesystem::path& path) {
    std::ifstream f(path);
    if (!f) return;
    std::uint32_t count = 0;
    std::string   name;
    while (f >> count >> name)
        if (!name.empty()) attractors_[name] += count;
}

std::vector<std::pair<std::string, std::uint32_t>> Cogitator::top_attractors(std::size_t n) const {
    std::vector<std::pair<std::string, std::uint32_t>> v(attractors_.begin(), attractors_.end());
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });
    if (v.size() > n) v.resize(n);
    return v;
}

Thought Cogitator::think(std::string_view stimulus) {
    Thought t;
    t.stimulus = std::string(stimulus);
    ++thoughts_;
    ensure_field_();

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
        if (t.probe.popcount() > 0) {
            t.resonances = resonate_(t.probe, resonance_k_);
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
            const auto fm = resonate_(tg, 1);
            if (!fm.empty() && fm.front().similarity > novelty_threshold_ * 0.5) {
                const Glyph g = recall_(fm.front().label);
                if (g.popcount() > 0) fragments.push_back(g);
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

// The breadth each lens resonates with.
static std::size_t lens_k(Lens lens, std::size_t base) {
    if (lens == Lens::Broad)   return std::max<std::size_t>(base, 8);
    if (lens == Lens::Focused) return 1;
    if (lens == Lens::Curious) return std::max<std::size_t>(base, 4);
    return base;
}

// Build the lens-shaped probe — different facets literally look at
// different aspects of the stimulus. No resonance here; that is batched.
Glyph Cogitator::facet_probe_(const std::vector<std::string>& tokens, Lens lens,
                              std::uint64_t entropy_seed) const {
    std::vector<Glyph> parts;
    parts.reserve(tokens.size());
    const std::size_t n = tokens.size();
    switch (lens) {
        case Lens::Leading: {
            const std::size_t half = (n + 1) / 2;
            for (std::size_t i = 0; i < half; ++i)
                if (is_content_(tokens[i])) parts.push_back(token_glyph_(tokens[i]));
            break;
        }
        case Lens::Trailing: {
            const std::size_t start = n / 2;
            for (std::size_t i = start; i < n; ++i)
                if (is_content_(tokens[i])) parts.push_back(token_glyph_(tokens[i]));
            break;
        }
        default:
            for (const auto& tok : tokens)
                if (is_content_(tok)) parts.push_back(token_glyph_(tok));
            break;
    }
    // If the lens's slice held only function words, fall back to its tokens.
    if (parts.empty()) {
        if (lens == Lens::Leading) {
            const std::size_t half = (n + 1) / 2;
            for (std::size_t i = 0; i < half; ++i) parts.push_back(token_glyph_(tokens[i]));
        } else if (lens == Lens::Trailing) {
            for (std::size_t i = n / 2; i < n; ++i) parts.push_back(token_glyph_(tokens[i]));
        } else {
            for (const auto& tok : tokens) parts.push_back(token_glyph_(tok));
        }
    }
    Glyph probe = parts.empty() ? Glyph::zero()
                                : bundle(std::span<const Glyph>{parts.data(), parts.size()});

    // The chaotic lens injects entropy — it explores a perturbed nearby
    // region of the manifold, the engine's way of courting the unexpected.
    if (lens == Lens::Chaotic && probe.popcount() > 0) {
        std::uint64_t s = entropy_seed;
        const std::size_t flips = khora::lattice::kGlyphBits / 50;  // ~2% perturbation
        for (std::size_t i = 0; i < flips; ++i) {
            s = s * 6364136223846793005ULL + 1442695040888963407ULL;
            probe.flip_bit(static_cast<std::size_t>(s % khora::lattice::kGlyphBits));
        }
    }

    // The associative lens follows the cortex's forward projection instead
    // of the literal stimulus — thinking about what tends to come next.
    if (lens == Lens::Associative) {
        const Glyph proj = cortex_.predict();
        if (proj.popcount() > 0)
            return (probe.popcount() > 0) ? bundle({probe, proj}) : proj;
    }
    return probe;
}

// Finish a facet given its already-resolved resonances (computed in one
// batched dispatch). Pure post-processing — runs concurrently across
// facets with no device contention.
Facet Cogitator::finish_facet_(Lens lens, const Glyph& query_probe,
                               std::vector<LatticeMatch> resonances) const {
    Facet f;
    f.lens       = lens;
    f.probe      = query_probe;
    f.resonances = std::move(resonances);

    // Choose this facet's candidate.
    std::size_t pick = 0;
    if (lens == Lens::Curious && f.resonances.size() > 1) {
        // Deliberately chase a non-obvious alternative (second-best) — the
        // facet that questions the obvious answer.
        pick = 1;
    }
    if (!f.resonances.empty()) {
        f.confidence = f.resonances.front().similarity;       // confidence = best available
        const auto& chosen = f.resonances[std::min(pick, f.resonances.size() - 1)];
        f.label     = chosen.label;
        f.candidate = recall_(chosen.label);
        if (f.candidate.popcount() == 0) f.candidate = query_probe;
    } else {
        f.candidate  = query_probe;
        f.confidence = 0.0;
    }
    f.novel = f.confidence < novelty_threshold_;

    // Score this facet through the drives — each lens flatters a different
    // drive, so the Soma's current mood tilts the contest.
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
    ensure_field_();

    // 1. Build every lens's probe (serial, cheap). Each facet looks at a
    //    different aspect of the stimulus.
    std::vector<Glyph>       probes(n_lenses);
    std::vector<std::size_t> ks(n_lenses);
    for (std::size_t i = 0; i < n_lenses; ++i) {
        const Lens lens = static_cast<Lens>(i);
        const std::uint64_t seed =
            0xC0FFEEULL ^ (static_cast<std::uint64_t>(deliberations_) << 20) ^ (i * 0x9E3779B9ULL);
        probes[i] = facet_probe_(d.tokens, lens, seed);
        ks[i]     = lens_k(lens, resonance_k_);
    }

    // 2. Resonate ALL facets in a single batched dispatch — one GPU call,
    //    no device contention, instead of eight concurrent ones. Each facet
    //    then keeps its own lens-specific breadth from the shared result.
    std::size_t kmax = 1;
    for (auto k : ks) kmax = std::max(kmax, k);
    const auto batched = resonate_batch_(probes, kmax);

    // 3. Finish the facets CONCURRENTLY — pure post-processing (candidate
    //    choice, drive valence), read-only and device-free, so the chorus
    //    still thinks at once.
    std::vector<std::future<Facet>> futures;
    futures.reserve(n_lenses);
    for (std::size_t i = 0; i < n_lenses; ++i) {
        const Lens lens = static_cast<Lens>(i);
        std::vector<LatticeMatch> res =
            (i < batched.size()) ? batched[i] : std::vector<LatticeMatch>{};
        if (res.size() > ks[i]) res.resize(ks[i]);   // lens-specific breadth
        futures.push_back(std::async(std::launch::async,
            [this, lens, p = probes[i], r = std::move(res)]() mutable {
                return finish_facet_(lens, p, std::move(r));
            }));
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
    note_attractor_(d.chosen_label);

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
    note_attractor_(r.conclusion);
    return r;
}

} // namespace khora::cogitator
