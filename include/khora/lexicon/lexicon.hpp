#pragma once

// The Lexicon — Khora's semantic encoding layer.
//
// Two-layer encoding:
//
// 1. Structural baseline (deterministic, no training)
//    Each token's glyph is the bundle of its position-permuted char
//    trigrams, with sentinels '^' and '$'. "cat" and "cats" share
//    two of three trigrams (^ca, cat) and so share ~37% of bits.
//    Typo "instal" stays ~52% similar to "install". This handles
//    English nuance and typos for free, no training.
//
// 2. Cooccurrence evidence (accumulator, online learning)
//    Each token maintains a vector of per-bit "vote" counters. When
//    the token cooccurs with a neighbour within a window, every bit
//    set in the neighbour's structural glyph contributes one vote to
//    the token's accumulator. Reading the token's glyph thresholds
//    those votes against the token's total exposure count to recover
//    a "context glyph", which is bundled with the structural baseline.
//    Words that share contexts drift toward similar glyphs --
//    real distributional semantics on the substrate, no LLM.

#include "khora/lattice/glyph.hpp"

#include <cstddef>
#include <cstdint>
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

    // Get the current glyph for a token. If the token has no exposure
    // history, returns the pure structural baseline. Otherwise returns
    // a bundle of (structural baseline, context glyph).
    khora::lattice::Glyph glyph_for(std::string_view token) const;

    // Pure structural baseline (no exposure influence).
    static khora::lattice::Glyph baseline_for(std::string_view token);

    // Observe a sequence of tokens, accruing cooccurrence evidence
    // within +/- window neighbours. Returns the number of token-pair
    // observations recorded.
    std::size_t expose_sequence(const std::vector<std::string>& tokens,
                                std::size_t window = 3);

    // Convenience: tokenize text then expose.
    std::size_t expose_text(std::string_view text, std::size_t window = 3);

    // Semantic similarity between two tokens through the lexicon.
    double similarity(std::string_view a, std::string_view b) const;

    // Inspectors
    std::size_t vocabulary_size()  const noexcept { return evidence_.size(); }
    std::size_t total_observations() const noexcept { return total_obs_; }
    bool        has(std::string_view token) const;
    std::uint32_t exposures_for(std::string_view token) const;

private:
    struct Evidence {
        // One counter per bit position. Saturates at u16 max but in
        // practice we never approach that under normal training.
        std::vector<std::uint16_t> votes;
        std::uint32_t              observations = 0;
    };

    Evidence& touch_(const std::string& token);
    khora::lattice::Glyph context_glyph_(const Evidence& e) const;

    std::unordered_map<std::string, Evidence> evidence_;
    std::size_t                               total_obs_ = 0;
};

} // namespace khora::lexicon
