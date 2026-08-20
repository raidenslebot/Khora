#pragma once

// The Plexus — Khora's associative graph memory.
//
// The hub problem haunts every distributional substrate: loud words ("the",
// "of", "is") keep company with everything, so under any overlap metric they
// sit near everything, and every train of thought collapses into them. Three
// honest attempts to fix it inside the binary hypervector substrate failed —
// strip the common bits (there were none concentrated), force fixed density
// (surfaced random rare words), normalise by cosine (function words DO overlap
// everything, so cosine rewarded them). The fault was never the metric. It was
// that the binary glyph THREW AWAY the frequency information the cure needs.
//
// The Plexus keeps that information. It is an explicit weighted graph: each
// word a node, each co-occurrence an edge, every raw count preserved. Affinity
// is not overlap but POINTWISE MUTUAL INFORMATION —
//
//     PMI(a,b) = log2[ P(a,b) / (P(a) * P(b)) ]
//
// — the co-occurrence of a and b measured AGAINST the chance they would meet
// at random. A hub has enormous P(a), so its own loudness divides straight out
// of every edge it owns: "the" near "justice" scores ~0, while "injustice"
// near "justice" blazes. This is the degree-normalisation the failed tweaks
// were groping toward, in its principled form — and it is the same quantity
// modern word embeddings implicitly factorise (Levy & Goldberg, 2014). Context
// is smoothed (P(b)^0.75) to blunt PMI's bias toward the rare, and flimsy
// single-meeting edges are floored away as noise.
//
// Out of this falls SHARP association: the true kin of a concept, hubs gone.
// That clean structure is the fuel the Spire (recursive abstraction) starves
// for, the field chaos churns, the ground cognition walks. Solve the hub
// problem here and abstraction, chaos, cognition, generation all unlock at
// once — they all drink from this one well.
//
// Pure standard C++. No LLM, no external dependency. Memory is bounded: each
// node keeps only its strongest `max_degree` associates (confidence-weighted
// PMI), so the whole graph fits in tens of megabytes for a 35k vocabulary.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace khora::plexus {

class Plexus {
public:
    Plexus();

    // Observe a token sequence, tallying every co-occurrence within +/- window
    // and every node's frequency. Returns the number of co-occurrence events
    // recorded. Unlike the Lexicon, the Plexus does NOT subsample loud words —
    // PMI divides their loudness out analytically, so it wants the full count.
    std::size_t observe(const std::vector<std::string>& tokens,
                        std::size_t window = 3);

    // Affinity: positive pointwise mutual information between two words, with
    // context smoothing. 0.0 if unrelated, unseen, or below the noise floor.
    // This is the hub-proof similarity — a word's loudness cannot inflate it.
    double affinity(std::string_view a, std::string_view b) const;

    // The k strongest associates of a word, by affinity (descending). These
    // are SHARP: frequency hubs are suppressed by the mathematics, not by any
    // hand-tuned stop-list. The heart of the cure.
    std::vector<std::pair<std::string, double>>
    associates(std::string_view word, std::size_t k = 8) const;

    // REINFORCE — the autopoietic write-back. Strengthen the a<->b connection by
    // `add` (raising the joint count, hence PMI), as if Khora had observed it.
    // This is how reasoning becomes knowledge: a verified discovery (a relation
    // Khora reasoned and corroborated) is written back so the graph GROWS BEYOND
    // the corpus and compounds. Marginal occurrences are deliberately NOT bumped —
    // raising only the joint count is exactly what lifts the mutual information.
    // Use only on VERIFIED discoveries; unverified write-back is an echo chamber.
    void          reinforce(const std::string& a, const std::string& b,
                            std::uint32_t add);
    std::uint64_t reinforcements() const noexcept { return reinforcements_; }

    // Topology access. The Plexus is a learned graph, and the only way to know
    // whether its STRUCTURE resembles a real neural network -- clustering, path
    // length, degree distribution, hubs -- is to be able to walk it. Node ids
    // are dense in [0, vocabulary_size).
    std::string_view node_name(std::size_t id) const { return word_[id]; }
    // (neighbour id, co-occurrence count) for one node.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> neighbours(std::size_t id) const;
    std::size_t   degree(std::size_t id) const { return adj_[id].size(); }

    // Inspectors.
    std::size_t   vocabulary_size() const noexcept { return word_.size(); }
    std::uint64_t total_tokens()    const noexcept { return total_tokens_; }
    std::uint64_t edge_count()      const noexcept;
    std::uint32_t occurrences(std::string_view word) const;
    bool          has(std::string_view word) const;

    // Z — the partition function of the smoothed context distribution: the sum
    // of occ^alpha over the whole vocabulary. PMI divides the smoothed context
    // term by this so that P(context) is an actual distribution. Exposed
    // because it is the one quantity whose definition, if it drifts, silently
    // rescales every affinity in the graph.
    double        smoothed_context_z() const noexcept { return smoothed_ctx_z_; }
    static double context_smoothing_exponent() noexcept;

    // Memory bound: maximum associates stored per node. Lowering it sheds
    // edges on the next prune; raising it lets nodes keep more kin.
    void        set_max_degree(std::size_t d) { max_degree_ = (d ? d : 1); }
    std::size_t max_degree() const noexcept   { return max_degree_; }

    // Merge another graph into this one — co-occurrence is an additive,
    // commutative monoid, so summing partial graphs built over disjoint slices
    // of the corpus equals the serial result. This is what lets the forge build
    // the graph across all cores: each thread weaves a thread-local Plexus over
    // its slice, then they are absorbed into one. Prune AFTER all absorbs.
    void absorb(const Plexus& other);
    // Prune every over-capacity node back to max_degree (call once after merge).
    void prune_all();

    // Persistence — a single compact binary file <prefix>.plexus.
    void save(const std::filesystem::path& prefix) const;
    void load(const std::filesystem::path& prefix);

private:
    std::uint32_t intern_(const std::string& w);     // get-or-create node id
    std::int64_t  lookup_(std::string_view w) const; // node id or -1
    double        ppmi_(std::uint32_t a, std::uint32_t b,
                        std::uint32_t cab) const;
    void          prune_(std::uint32_t node);
    void          recompute_smoothed_context_();     // after any change to occ_

    std::unordered_map<std::string, std::uint32_t>              ids_;   // word -> id
    std::vector<std::string>                                    word_;  // id -> word
    std::vector<std::uint32_t>                                  occ_;   // id -> frequency
    std::vector<std::unordered_map<std::uint32_t, std::uint32_t>> adj_; // id -> (id -> cooc)
    std::uint64_t total_tokens_   = 0;   // N — corpus length
    std::uint64_t total_cooc_     = 0;   // W — total co-occurrence weight
    // Z — the partition function of the smoothed context distribution,
    // sum over all words of occ^alpha. PMI needs a normalised P(context);
    // without this the smoothing subtracts a constant from every score.
    double        smoothed_ctx_z_ = 0.0;
    std::uint64_t reinforcements_ = 0;   // verified discoveries written back (autopoiesis)
    std::size_t   max_degree_     = 160;
};

} // namespace khora::plexus
