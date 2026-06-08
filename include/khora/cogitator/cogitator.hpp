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
#include "khora/maelstrom/maelstrom.hpp"
#include "khora/soma/soma_nexus.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
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

// --- Non-linear cognition: the Prism ---
//
// A stimulus does not travel one path. It refracts into parallel Facets,
// each viewing the problem through a different Lens. The Facets explore
// concurrently (real threads), then compete; the Soma arbitrates by
// drive-valence; the coherent coalition collapses into a single thought.
// Meaning emerges from the whole chorus, not a linear chain.

enum class Lens : std::uint8_t {
    Holistic,     // the whole stimulus, balanced
    Leading,      // weight the front of the stimulus
    Trailing,     // weight the tail of the stimulus
    Broad,        // cast a wide resonance net (high k)
    Focused,      // the single sharpest match (k = 1)
    Curious,      // deliberately chase a non-obvious alternative
    Associative,  // follow the cortex's forward projection
    Chaotic,      // perturb the probe with entropy and explore nearby
    _Count
};

const char* lens_name(Lens l) noexcept;

struct Facet {
    Lens                                       lens = Lens::Holistic;
    khora::lattice::Glyph                      probe;
    khora::lattice::Glyph                      candidate;    // this facet's answer-glyph
    std::vector<khora::lattice::LatticeMatch>  resonances;
    double                                     confidence = 0.0;
    double                                     valence = 0.0;  // soma's drive-weighted score
    bool                                       novel = true;
    std::string                                label;          // top resonance label, if any
};

struct Deliberation {
    std::string                stimulus;
    std::vector<std::string>   tokens;
    std::vector<Facet>         facets;        // the parallel explorations
    int                        winner = -1;   // index of the arbitrated winner
    double                     coherence = 0.0; // how much the facets agreed (0..1)
    double                     entropy = 0.0;   // spread of valences (chaos in the chorus)
    khora::lattice::Glyph      collapsed;     // the coherent coalition, bundled
    std::string                chosen_label;  // winner's answer, or empty
    bool                       learned = false;
};

// A train of thought — recursive deliberation. Each collapsed thought
// becomes the next stimulus, so cognition hops through concept-space
// until it settles into an attractor (a concept it keeps returning to)
// or exhausts its depth. Associative, non-linear, self-driven.
struct Rumination {
    std::string                seed;
    std::vector<Deliberation>  chain;
    std::vector<std::string>   train;       // the concepts traversed, in order
    bool                       converged = false;  // settled into an attractor
    std::string                conclusion; // the attractor, or last concept reached
};

// Chaotic synthesis — entropy into beauty. Two distant concepts are
// superposed into a chimera glyph; what the chimera resonates with (that is
// neither parent) is the emergent idea their collision forged. High tension
// (distant parents) + a coherent child = creativity out of chaos.
struct Synthesis {
    std::string                                a, b;
    double                                     tension = 0.0;  // 1 - sim(a,b)
    std::vector<khora::lattice::LatticeMatch>  emergent;       // the forged concept(s)
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

    // One act of thought — runs the full (linear) resolve loop.
    Thought think(std::string_view stimulus);

    // Non-linear cognition: refract the stimulus into parallel Facets,
    // let them compete, and collapse the coherent coalition. `facets`
    // caps how many lenses to spawn (default: all of them).
    Deliberation deliberate(std::string_view stimulus,
                            std::size_t facets = static_cast<std::size_t>(Lens::_Count));

    // Recursive deliberation: a train of thought that hops from concept to
    // concept until it settles into an attractor or reaches max_depth.
    Rumination ruminate(std::string_view stimulus, std::size_t max_depth = 6);

    // Collide two concepts and report what their superposition evokes — the
    // emergent idea neither parent contains. Chaos turned to creation. If a
    // or b is empty, Khora picks distant concepts itself (`seed` varies it).
    Synthesis synthesize(const std::string& a, const std::string& b, std::uint64_t seed = 0);

    // A clean concept to think about, drawn from the centrality-pruned
    // content field (not the function-word-heavy raw vocabulary). Lets the
    // Volition seed autonomous thought with real concepts. Empty if nothing
    // has been learned yet. `n` rotates deterministically through the set.
    std::string wandering_seed(std::uint64_t n);

    // A concept to DEEPEN rather than discover: one of Khora's current
    // preoccupations (top attractors), so thought can dwell on and develop
    // its own themes. Falls back to wandering_seed before any have formed.
    // Exploration (wandering) + focus (this) is Khora's attention dynamic.
    std::string focused_seed(std::uint64_t n);

    // The concepts Khora's own thought keeps converging on — its emergent
    // preoccupations, the attractors a mind develops as it ruminates. Ranked
    // by how often deliberation/rumination has landed there.
    std::vector<std::pair<std::string, std::uint32_t>> top_attractors(std::size_t n = 10) const;

    // Persist / restore Khora's preoccupations so its inner life continues
    // across restarts — the same developing mind each run, not a fresh one.
    void save_attractors(const std::filesystem::path& path) const;
    void load_attractors(const std::filesystem::path& path);

    // Stats
    std::size_t thoughts_completed() const noexcept { return thoughts_; }
    std::size_t novel_thoughts()     const noexcept { return novel_count_; }
    std::size_t hypotheses_formed()  const noexcept { return hypotheses_formed_; }
    std::size_t total_attempts()     const noexcept { return total_attempts_; }
    std::size_t deliberations()      const noexcept { return deliberations_; }

private:
    khora::lattice::Glyph encode_(const std::vector<std::string>& tokens) const;
    khora::lattice::Glyph gestalt_(const khora::lattice::Glyph& probe,
                                   const std::vector<khora::lattice::LatticeMatch>& res) const;

    // Build a lens-shaped probe (the "view" each facet takes), and finish a
    // facet given its already-resolved resonances — split so a deliberation
    // can resonate all its facets in ONE batched call before they finish
    // concurrently (one GPU dispatch, not eight; no shared-context hazard).
    khora::lattice::Glyph facet_probe_(const std::vector<std::string>& tokens,
                                       Lens lens, std::uint64_t entropy_seed) const;
    Facet finish_facet_(Lens lens, const khora::lattice::Glyph& query_probe,
                        std::vector<khora::lattice::LatticeMatch> resonances) const;

    // Resonance over Khora's knowledge. The primary field is the Lexicon's
    // whole learned vocabulary (indexed in the Resonator, GPU-accelerated at
    // scale); memory_ holds the provisional concepts cognition itself coins.
    void ensure_field_();
    // A token's glyph for cognition: its pure distributional context glyph
    // (so resonance follows meaning, not spelling), or the structural
    // baseline as a fallback when Khora hasn't learned the word yet.
    khora::lattice::Glyph token_glyph_(const std::string& tok) const;
    // Is this token a content word (in the salient set), or — when the set
    // is empty (tiny lexicon) — treat everything as content.
    bool is_content_(const std::string& tok) const {
        return content_.empty() || content_.count(tok) > 0;
    }
    std::vector<khora::lattice::LatticeMatch> resonate_(const khora::lattice::Glyph& probe,
                                                        std::size_t k) const;
    std::vector<std::vector<khora::lattice::LatticeMatch>> resonate_batch_(
        const std::vector<khora::lattice::Glyph>& probes, std::size_t k) const;
    khora::lattice::Glyph recall_(const std::string& label) const;
    void note_attractor_(const std::string& label);  // record a concept thought landed on

    khora::lexicon::Lexicon&         lex_;
    khora::lattice::Lattice&         memory_;
    khora::cortex::PredictiveColumn& cortex_;
    khora::soma::SomaNexus&          soma_;

    maelstrom::Resonator           field_{1024};   // GPU crossover for cognition
    std::unordered_set<std::string> content_;       // salient content words
    std::vector<std::string>        concepts_;      // clean concepts for seeding thought
    std::unordered_map<std::string, std::uint32_t> attractors_;  // emergent preoccupations
    std::size_t                    indexed_vocab_ = static_cast<std::size_t>(-1);

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
    std::size_t deliberations_     = 0;
};

} // namespace khora::cogitator
