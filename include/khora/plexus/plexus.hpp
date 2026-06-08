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

    // Inspectors.
    std::size_t   vocabulary_size() const noexcept { return word_.size(); }
    std::uint64_t total_tokens()    const noexcept { return total_tokens_; }
    std::uint64_t edge_count()      const noexcept;
    std::uint32_t occurrences(std::string_view word) const;
    bool          has(std::string_view word) const;

    // Memory bound: maximum associates stored per node. Lowering it sheds
    // edges on the next prune; raising it lets nodes keep more kin.
    void        set_max_degree(std::size_t d) { max_degree_ = (d ? d : 1); }
    std::size_t max_degree() const noexcept   { return max_degree_; }

    // Persistence — a single compact binary file <prefix>.plexus.
    void save(const std::filesystem::path& prefix) const;
    void load(const std::filesystem::path& prefix);

private:
    std::uint32_t intern_(const std::string& w);     // get-or-create node id
    std::int64_t  lookup_(std::string_view w) const; // node id or -1
    double        ppmi_(std::uint32_t a, std::uint32_t b,
                        std::uint32_t cab) const;
    void          prune_(std::uint32_t node);

    std::unordered_map<std::string, std::uint32_t>              ids_;   // word -> id
    std::vector<std::string>                                    word_;  // id -> word
    std::vector<std::uint32_t>                                  occ_;   // id -> frequency
    std::vector<std::unordered_map<std::uint32_t, std::uint32_t>> adj_; // id -> (id -> cooc)
    std::uint64_t total_tokens_ = 0;   // N — corpus length
    std::uint64_t total_cooc_   = 0;   // W — total co-occurrence weight
    std::size_t   max_degree_   = 160;
};

} // namespace khora::plexus
