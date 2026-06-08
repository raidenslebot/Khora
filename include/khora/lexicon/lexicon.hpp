#pragma once

// The Lexicon — Khora's semantic encoding layer.
//
// Two-layer encoding, both pure hyperdimensional computing, no LLM:
//
// 1. Structural baseline (deterministic, no training)
//    Each token's glyph is the bundle of its position-permuted char
//    trigrams with '^'/'$' sentinels. "cat" and "cats" share trigrams
//    and so share bits; typos stay close. English nuance for free.
//
// 2. Distributional context via RANDOM INDEXING (online, persistable)
//    Each token has a fixed sparse ternary *index vector* (a handful of
//    +1/-1 positions, deterministic from the token). Each token also
//    accumulates a *context vector*: every time it appears near a
//    neighbour, the neighbour's index vector is added in. Words that
//    keep similar company accumulate similar context vectors, so their
//    binarised glyphs converge — real distributional semantics. Because
//    each index vector is sparse (~K nonzeros), a cooccurrence costs ~K
//    increments instead of ~N bits: study runs orders of magnitude
//    faster, and the binarised context glyph per token persists compactly
//    through the Lattice.

#include "khora/lattice/glyph.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace khora::lexicon {

// Pure structural encoding from char trigrams. Deterministic.
khora::lattice::Glyph encode_token(std::string_view token);

// Lowercase + alnum tokenizer.
std::vector<std::string> tokenize(std::string_view text);

class Lexicon {
public:
    Lexicon();

    // Current glyph for a token: structural baseline alone if unseen,
    // else baseline blended with the binarised context vector.
    khora::lattice::Glyph glyph_for(std::string_view token) const;

    // Pure structural baseline (no exposure influence).
    static khora::lattice::Glyph baseline_for(std::string_view token);

    // Observe a token sequence, accruing random-indexing context within
    // +/- window neighbours. Returns the number of cooccurrence updates.
    std::size_t expose_sequence(const std::vector<std::string>& tokens,
                                std::size_t window = 3);
    std::size_t expose_text(std::string_view text, std::size_t window = 3);

    // Semantic similarity between two tokens through the lexicon.
    double similarity(std::string_view a, std::string_view b) const;

    // Inspectors
    std::size_t   vocabulary_size()  const noexcept { return ctx_.size(); }
    std::size_t   total_observations() const noexcept { return total_obs_; }
    bool          has(std::string_view token) const;
    std::uint32_t exposures_for(std::string_view token) const;

    // The most salient learned tokens: content words (not ubiquitous
    // function words) with enough exposure, ranked by exposure. Used to
    // promote studied vocabulary into Khora's thinkable concept space.
    std::vector<std::string> salient_tokens(std::size_t max_tokens,
                                            std::uint32_t min_exposure = 5) const;

    // Persistence — survives across process restarts. Writes
    //   <prefix>.sem.klat  : token -> binarised context glyph (Lattice)
    //   <prefix>.lexobs     : per-token observation counts
    void save(const std::filesystem::path& prefix) const;
    void load(const std::filesystem::path& prefix);

private:
    struct Context {
        std::vector<std::int32_t> acc;   // D-dim accumulator
        std::uint32_t             obs = 0;
    };

    Context& touch_(const std::string& token);
    static void add_index_(std::vector<std::int32_t>& acc,
                           std::string_view source_token, int weight);
    int  weight_for_(const std::string& token) const;  // inverse-frequency weight
    khora::lattice::Glyph binarise_(const Context& c) const;

    std::unordered_map<std::string, Context>      ctx_;
    std::unordered_map<std::string, std::uint32_t> freq_;   // global token frequency
    std::size_t                                   total_obs_    = 0;
    std::size_t                                   total_tokens_ = 0;
};

} // namespace khora::lexicon
