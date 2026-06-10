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

namespace khora::plexus { class Plexus; }   // associative graph (hub-proof kin)

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

// GENESIS — the open-ended invention of NEW concepts. Not prediction, not retrieval:
// Khora finds a coherent cluster of concepts whose SHARED concept has no name yet — the
// centroid sits in a real GAP, far from every existing named concept — and that unnamed
// thing IS an invention. Accepted inventions are kept and COMPOUND (they become new
// concepts to combine), so the conceptual universe expands itself with no ceiling. The
// drive is novelty x coherence, an objective with no maximum — a different kind of mind.
struct Genesis {
    std::vector<std::string> from;            // the constituent concepts that forged it
    std::vector<std::string> near;            // the nearest existing concepts (its locale)
    double                   novelty   = 0.0; // 1 - similarity to the nearest NAMED concept
    double                   coherence = 0.0; // mutual similarity of the constituents
    bool                     genuine   = false; // novel AND coherent => a real invention
    khora::lattice::Glyph    glyph;           // the invented concept's vector
};

// A single emergent thought from NON-LINEAR contemplation — a concept the parallel threads
// converged on, with how strongly and across how many distinct MODES of thought (convergence
// across modes is emergence: the same concept found by association AND by tower-leap AND by
// chaotic collision is a deep one).
struct Emergence {
    std::string name;
    double      score = 0.0;
    int         modes = 0;   // distinct cognitive modes that reached it
};

// A CASCADE of thought — recursive non-linear cognition. Each step is itself a multi-mode
// convergence (a contemplate); the strongest emergent thought (steered by a chaos dial
// between order and entropy) becomes the seed of the next. The trajectory either COLLAPSES
// into a stable attractor (it returns to a concept it has already thought — an insight has
// crystallised) or stays generatively chaotic. Recursive instability resolving into action.
struct Cascade {
    std::vector<std::string> chain;       // the train of thought, in order
    bool                     collapsed = false;  // reached a stable attractor (a loop)
    std::string              attractor;   // the concept it collapsed onto (if it did)
    int                      novelty = 0; // distinct concepts visited before collapse
    double                   emergence = 0.0;  // mean cross-mode convergence along the chain
};

// THE TRANSMUTATION — chaos forged into permanent capability. Khora leaps into genuine entropy
// (a concept with ZERO link to the theme) and FIGHTS BACK to coherence by finding the hidden
// third concept that bridges them — the beauty their tension reveals. Leaps that forge a TRUE
// bridge are kept; the rest are honest nothing (failure → fuel). Committed, the verified
// bridge is reinforced into the graph, so chaos becomes lasting structure that compounds —
// pennies of entropy into oceans of capability. The `yield` is how often chaos turns to beauty.
struct Transmutation {
    std::string              theme;
    int                      leaps = 0;     // chaotic leaps into the void attempted
    int                      forged = 0;    // leaps that found a TRUE bridge (entropy -> beauty)
    int                      written = 0;   // bridges reinforced into the graph (if committed)
    double                   yield = 0.0;   // forged / leaps — the fight-out-of-entropy rate
    std::vector<std::string> bridges;       // the novel bridging concepts discovered
};

// Recursive abstraction — the combinatorial engine of exponential cognition.
// Khora chunks a cluster of kindred concepts into ONE higher-order concept,
// then abstracts over THOSE, and over those — a rising tower. Where flat
// concepts grow linearly, a hierarchy of abstractions grows combinatorially:
// every new abstraction multiplies what can be composed at the next level.
// The tower persists, so it compounds across Khora's whole existence.
struct Abstraction {
    std::string                name;
    khora::lattice::Glyph      glyph;
    int                        level = 1;     // 1 = over words; 2 = over abstractions; ...
    double                     coherence = 0.0;  // how tightly its members cohere
    std::vector<std::string>   members;
};

// A grounded, structured account of a single concept — Khora answering "what is
// X?" from the clean structure it learned, NOT by free generation (which drifts
// to the corpus's heaviest sequences). Every field is read straight off the
// Plexus and the abstraction tower, so it is correct and verifiable.
struct Insight {
    std::string                subject;    // ('concept' is a C++20 keyword)
    bool                       known = false;
    std::vector<std::string>   defines;   // strongest PMI kin — what the subject is about
    std::string                kind;       // the abstraction it belongs to (its category)
    std::vector<std::string>   kindred;    // its siblings under that category
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

    // Wire in the Plexus (associative graph). When set, the Spire forms
    // abstractions from hub-proof PMI kin and judges coherence by mutual
    // information, instead of the density-fouled Hamming field. Optional —
    // without it, abstraction falls back to the substrate field.
    void set_plexus(khora::plexus::Plexus* p) { plexus_ = p; }

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

    // --- Reasoning: goal-directed inference over the clean structure ---
    //
    // A reasoned PATH connecting two concepts — a chain of genuine associations
    // found by beam search that heads TOWARD the goal (an affinity-to-goal
    // heuristic), not free association. Every step is a real Plexus PMI edge, so
    // the chain is grounded and verifiable; this is inference, not retrieval.
    // Returns the chain start..goal, the closest-approach path if no full
    // connection is found within max_depth, or empty if either concept is unknown.
    // This is the first faculty that THINKS TOWARD an answer rather than wandering.
    std::vector<std::string> infer_path(const std::string& start,
                                        const std::string& goal,
                                        std::size_t max_depth = 7) const;

    // The goal-pull heuristic weight of infer_path — a TUNABLE knob (was a
    // compile-time constant). The Yield loop sweeps it and keeps the value that
    // measurably maximises inference success: the first parameter set by
    // downstream outcome, not by hand.
    void   set_infer_goal_pull(double g) { infer_goal_pull_ = g; }
    double infer_goal_pull() const noexcept { return infer_goal_pull_; }

    // CLOSED-LOOP MEASUREMENT — an OBJECTIVE score of inference, the missing
    // success signal. Samples `n` genuine 2-hop concept pairs (A relates to C
    // only through some bridge B, with NO direct A–C edge), runs infer_path on
    // each, and returns the fraction that reach the goal. The gold ("these ARE
    // connectable") comes from the graph's own structure — no labels, not
    // circular. This is the number self-improvement can finally optimise toward;
    // it must DROP if the knowledge graph is degraded (the anti-fake test).
    double benchmark_inference(std::size_t n, std::uint64_t seed = 0,
                               std::size_t max_depth = 5) const;

    // OBJECTIVE self-measurement of the ABSTRACTION faculty's calibration. A sound
    // abstraction faculty should judge a real content concept's neighbourhood as
    // COHERENT (it can seed a genuine abstraction) and a diffuse / function word as
    // INCOHERENT. This samples concepts, classifies each by whether seed_coherence_
    // clears a fixed bar, and returns the accuracy against the content/function ground
    // truth. The coherence scale calibrates exactly this separation, so it is a real,
    // scale-sensitive fitness number — the closed loop reaching a third faculty.
    double benchmark_abstraction(std::size_t n, std::uint64_t seed = 0) const;

    // REAL, HELD-OUT predictive fitness — the keystone. Given a token sequence Khora was
    // NOT trained on, mask each known content word and predict it from its content-word
    // neighbours (aggregating the Plexus's vote), then return top-k accuracy against the
    // TRUE word. Unlike the graph-internal benchmarks this is EXTERNAL and non-circular:
    // it measures whether Khora's knowledge GENERALISES to predict text it has not seen.
    // The number that, once self-improvement climbs it, aims the whole machine at real
    // capability rather than a proxy.
    double benchmark_prediction(const std::vector<std::string>& heldout,
                                std::size_t topk = 5) const;

    // Held-out next-word prediction via the CORTEX (the purpose-built predictive column),
    // not the PMI graph: for each content word, take the left-context glyphs, ask the
    // column for the most plausible next glyphs (the next-values of the nearest learned
    // contexts), decode each to a word, and return the mean reciprocal rank of the TRUE
    // next word. This tests the substrate's actual LEARNED (context -> next) transitions —
    // a real predictive mechanism rather than co-occurrence counting.
    double benchmark_next_word(const std::vector<std::string>& heldout) const;

    // GENESIS — invent a NEW concept: find a coherent cluster whose shared concept is
    // unnamed (its centroid sits in a gap, far from every existing named concept), and
    // return that invention with its novelty/coherence. Const — it forges and judges;
    // accepting + compounding is a separate, deliberate step.
    Genesis invent(std::uint64_t seed) const;

    // Open-ended FERTILITY (the drive with no ceiling): over n attempts, the fraction of
    // GENUINE inventions and their mean novelty x coherence. This measures how richly
    // Khora's conceptual universe can EXPAND — a different objective than prediction.
    double  benchmark_invention(std::size_t n, std::uint64_t seed = 0) const;

    // ASCEND THE TOWER — drive the recursive abstraction upward: level by level, form
    // higher-order abstractions over the existing ones, each COHERENCE-GATED (grounded to
    // real corpus-word leaves) so the tower rises without degenerating into blobs. This is
    // the combinatorial, no-ceiling growth the Spire was built for — Khora building concepts
    // over its own concepts, relentlessly. Returns {abstractions formed, highest level reached}.
    std::pair<int,int> ascend_tower(double min_coherence = 0.40, int max_new = 40);

    // PREDICTIVE LEARNING (lever 2) — the loop that actually MOVES the prediction number.
    // Read training text; for each content word, predict it from its neighbours; and on a
    // PREDICTION ERROR, strengthen the context->word links that would have made it right.
    // This is error-driven (discriminative), not mere co-occurrence counting: reinforce()
    // raises only the joint count, which lifts the pair's PMI, making that word more
    // predictable FROM that context specifically. Returns the number of corrective updates.
    std::size_t learn_predictively(const std::vector<std::string>& tokens,
                                   std::uint32_t reinforce_by = 2);

    // Answer "what is X?" from structure: the concept's defining kin, the
    // abstraction it belongs to (its kind), and its kindred under that category.
    // Grounded and correct where free generation drifts. Read-only.
    Insight explain(const std::string& subject) const;

    // --- Autopoiesis: knowledge that generates knowledge ---
    //
    // ONE step of verified self-learning, seeded at `seed`. Khora looks for a
    // concept that MANY of the seed's kin independently point to (a transitive
    // relation reached by multiple bridges) yet which the seed is NOT already
    // directly linked to — a genuine DISCOVERY — and, if the consensus clears a
    // bar (verification, to keep out echo-chamber noise), writes that link back
    // into the Plexus via reinforce(). The graph thereby grows beyond the corpus
    // from Khora's own reasoning, and compounds. MUTATES — caller holds the
    // unique lock. Returns a description of the discovery, or empty if none/unverified.
    std::string distill_knowledge(const std::string& seed);

    // Compose a short utterance steered toward `topic` — Khora putting its
    // thought into words, generated by chaining the cortex's learned
    // transitions and steering each step toward the topic. Associative, not
    // reasoned, but in its own voice. Empty if it has not learned enough.
    std::string utter(const std::string& topic, std::size_t n = 14);

    // Form a higher-order abstraction from a seed concept (a learned word OR
    // an existing abstraction): bind it with its nearest kin into one new
    // concept, one level higher. Returns its name. Empty if it can't form one.
    // Returns the new abstraction's name, or empty if it cohered below
    // `min_coherence` (Khora refusing a weak unification — a self-set bar).
    std::string form_abstraction(const std::string& seed, std::size_t k = 4,
                                 double min_coherence = 0.0);
    // Parallel abstraction scouting — the Furnace's core-user. Samples many
    // candidate seeds and computes each one's plexus-cluster coherence ACROSS
    // `threads` cores, returning the most coherent (>= min_coherence). This is
    // pure read-only work over the const Plexus + concept set, so it is safe to
    // run wide while the caller holds a SHARED lock (writers excluded). The
    // returned seeds are then formed (under a unique lock) by the caller. Lets
    // Khora burn the idle cores searching for its next good abstraction.
    std::vector<std::pair<std::string, double>>
    scout_abstractions(std::size_t samples, unsigned threads,
                       double min_coherence = 0.0) const;

    std::size_t abstraction_count() const noexcept { return abstractions_.size(); }
    int         abstraction_depth() const noexcept;            // highest level reached

    // Open-ended CONCEPTUAL RICHNESS — the sum over the abstraction tower of level x
    // coherence. It rewards a tower that is TALL (high levels), BROAD (many abstractions),
    // and SOUND (coherent), and it has no maximum: building one more coherent higher-order
    // abstraction always raises it. The native, no-ceiling fitness — concepts over concepts,
    // forever — that this substrate genuinely supports (unlike next-word prediction).
    double      tower_richness() const;

    // PRUNE the tower back to genuine structure: recompute each abstraction's HONEST
    // (word-grounded) coherence and remove those that fall below `bar`, iterating until
    // stable. Cleans out self-similar depth-stacking that only LOOKED coherent because the
    // old grounding stopped before reaching words. Returns {removed, surviving depth}.
    std::pair<int,int> prune_tower(double bar = 0.45);
    std::vector<std::string> abstraction_names(std::size_t n) const;  // recent, with levels
    // A name to abstract from next: usually a hot preoccupation, but every
    // few calls an existing abstraction — so the tower keeps rising.
    std::string abstraction_seed(std::uint64_t n) const;
    void save_abstractions(const std::filesystem::path& path) const;
    void load_abstractions(const std::filesystem::path& path);

    // Compose a response grounded in a whole question rather than one topic:
    // seed the generation with the question's content concepts and steer it
    // toward their combined meaning. Retrieval-grounded associative
    // generation — Khora answering from what it knows, in its own voice.
    // (Honest scope: grounded + fluent, but not yet step-by-step reasoning.)
    std::string respond(const std::string& question, std::size_t n = 20);

    // Collide two concepts and report what their superposition evokes — the
    // emergent idea neither parent contains. Chaos turned to creation. If a
    // or b is empty, Khora picks distant concepts itself (`seed` varies it).
    Synthesis synthesize(const std::string& a, const std::string& b, std::uint64_t seed = 0);

    // NON-LINEAR COGNITION (the vision's section V, high priority). On a seed, spawn many
    // PARALLEL threads across distinct modes of thought — flat association, ascent/leaps
    // through the abstraction tower into distant domains, and chaotic collision — let them
    // COMPETE (by strength) and COMBINE (a concept reached by SEVERAL modes is boosted), and
    // COLLAPSE into the thoughts that emerged from the WHOLE. Meaning from the field of
    // competing threads, not a single linear walk. Uses the machine's cores; reads only.
    std::vector<Emergence> contemplate(const std::string& seed, std::size_t threads = 16);

    // RECURSIVE non-linear cognition — a CASCADE of thought. From a seed, contemplate; let the
    // strongest emergent thought become the next seed; repeat. `chaos` in [0,1] dials between
    // order (always follow the deepest convergent thought) and entropy (leap to a less-obvious
    // one) — the chaos-master turning instability into trajectory. The cascade halts when it
    // returns to a concept already thought (it has COLLAPSED into an attractor — an insight) or
    // after `max_steps`. Pure cognition; bounded.
    Cascade cascade(const std::string& seed, std::size_t max_steps = 10, double chaos = 0.2);

    // THE TRANSMUTATION — turn entropy into permanent capability. Take `leaps` chaotic jumps to
    // concepts with NO connection to `theme`, and for each, find the hidden bridge that links
    // them (a true Plexus-routed bridge, tied to BOTH). When `commit`, reinforce each verified
    // bridge into the graph so the discovery LASTS and compounds. Returns the yield (how often
    // chaos forged beauty) and the novel bridges. Mutating only when committing.
    Transmutation transmute(const std::string& theme, std::size_t leaps = 16,
                            bool commit = false, std::uint64_t seed = 0);

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

    // CURIOSITY — the gap detector that directs open-ended learning. Among the
    // concepts Khora keeps thinking about (its attractors), the one it understands
    // LEAST (thinnest associative structure, or wholly unknown) is its frontier:
    // "I keep returning to X but I don't really grasp it." Returns that concept as
    // a topic to go and learn — the self-directed half of the exponential loop.
    // (Non-const: it ensures the content field is built so the filter is live.)
    std::string curiosity_topic();

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
    // Core generation: chain the cortex's candidate continuations, decoded via
    // the GPU lexicon field, steering each step toward `target`. Shared by
    // utter() and respond().
    std::string generate_(std::vector<khora::lattice::Glyph> ctx,
                          const khora::lattice::Glyph& target,
                          const std::vector<std::string>& steer_words,
                          std::size_t n, double steer = 0.8);
    // Topic pull for generation: how strongly a candidate word associates with
    // the steer words, by Plexus mutual information (squashed to [0,1)). Boosts
    // on-topic CONTENT words without penalising grammatical function words (whose
    // affinity is ~0, so they keep their natural cortex rank). 0 if no Plexus.
    double plexus_steer_(const std::string& w,
                         const std::vector<std::string>& targets) const;
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
    std::vector<Abstraction>        abstractions_;   // the rising tower
    // O(1) name -> abstraction-index, so grounding the tower is not an O(N) scan per node
    // (rebuilt lazily when the tower grows; single-threaded under the cognition lock).
    mutable std::unordered_map<std::string, std::size_t> abs_index_;
    mutable std::size_t             abs_index_n_ = static_cast<std::size_t>(-1);
    khora::plexus::Plexus*          plexus_ = nullptr;  // associative graph (hub-proof kin)
    std::size_t                     abstraction_seq_ = 0;
    std::size_t                    indexed_vocab_ = static_cast<std::size_t>(-1);
    std::size_t                    indexed_abstractions_ = 0;  // rebuild field as the tower grows

    // Glyph + level of any concept name (a learned word = level 0, or an
    // existing abstraction = its level).
    khora::lattice::Glyph concept_glyph_any_(const std::string& name, int& level) const;

    // Hub-proof abstraction: select members from the Plexus's PMI associates
    // and judge coherence by mutual information. Used when plexus_ knows the
    // seed; returns empty if it cannot (too few representable kin, or the
    // cluster cohered below min_coherence).
    std::string form_abstraction_plexus_(const std::string& seed, std::size_t k,
                                         double min_coherence);

    // Coherent TOWER-RISING: abstract over abstractions. An abstraction has no
    // Plexus node, so its meaning is taken from its members ground down to their
    // corpus-word leaves; two abstractions are kin when their grounded leaves
    // mutually associate. This lets level >= 2 of the Spire rise on the same
    // clean PMI structure as level 1 — the combinatorial engine compounding.
    std::string form_abstraction_over_abstractions_(const std::string& seed,
                                                    std::size_t k, double min_coherence);
    // Expand a concept name to its grounded corpus-word leaves (recursively
    // through the abstraction tree; a word is its own leaf). Bounded.
    void ground_concept_(const std::string& name,
                         std::unordered_set<std::string>& out, int depth) const;
    // Recursive worker for ground_concept_ — traces an abstraction all the way down to
    // REAL corpus words (no shallow depth cap), cycle-safe via a visited set, bounded by
    // the leaf cap. This is what makes deep-tower coherence HONEST (word-grounded), so the
    // ascent's gate can correctly refuse abstractions that don't truly cohere.
    void ground_into_(const std::string& name, std::unordered_set<std::string>& out,
                      std::unordered_set<std::string>& visited) const;
    // Recompute an abstraction's HONEST (word-grounded) coherence from its members.
    double honest_coherence_(const Abstraction& a) const;
    // Mean Plexus affinity across two grounded leaf sets — how related two
    // abstractions (or a word and an abstraction) are, through the corpus.
    double leafset_affinity_(const std::unordered_set<std::string>& a,
                             const std::unordered_set<std::string>& b) const;
    // Read-only coherence a word seed WOULD yield as an abstraction (its top
    // PMI kin's mean pairwise affinity, squashed). No mutation — the Furnace's
    // parallel scout uses this to rank candidates across cores.
    double seed_coherence_(const std::string& seed) const;

    std::size_t resonance_k_            = 5;
    double      infer_goal_pull_        = 1.5;   // tunable by measured yield
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
