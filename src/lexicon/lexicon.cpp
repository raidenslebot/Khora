#include "khora/lexicon/lexicon.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <climits>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace khora::lexicon {

using khora::lattice::Glyph;
using khora::lattice::bundle;
using khora::lattice::permute;
using khora::lattice::kGlyphBits;

namespace {

inline char to_lower(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

// Deterministic primitive glyph for a single trigram via FNV-1a -> SplitMix64.
Glyph trigram_glyph(std::string_view tri) {
    std::uint64_t h = 0xCBF29CE484222325ULL;  // FNV offset basis
    for (unsigned char c : tri) {
        h ^= c;
        h *= 0x100000001B3ULL;                 // FNV prime
    }
    return Glyph::random(h);
}

std::string normalise(std::string_view raw) {
    std::string norm = "^";
    norm.reserve(raw.size() + 2);
    for (char c : raw) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            norm.push_back(to_lower(c));
        }
    }
    if (norm.size() == 1) return {};  // no alnum content
    norm.push_back('$');
    return norm;
}

} // namespace

Glyph encode_token(std::string_view raw) {
    const std::string norm = normalise(raw);
    if (norm.empty()) return Glyph::zero();

    std::vector<Glyph> primitives;
    if (norm.size() < 3) {
        primitives.push_back(trigram_glyph(norm));
    } else {
        primitives.reserve(norm.size() - 2);
        for (std::size_t i = 0; i + 3 <= norm.size(); ++i) {
            primitives.push_back(
                permute(trigram_glyph(norm.substr(i, 3)),
                        static_cast<int>(i * 7))
            );
        }
    }
    return bundle(std::span<const Glyph>{primitives.data(), primitives.size()});
}

std::vector<std::string> tokenize(std::string_view text) {
    std::vector<std::string> out;
    std::string current;
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            current.push_back(to_lower(c));
        } else if (!current.empty()) {
            out.push_back(std::move(current));
            current.clear();
        }
    }
    if (!current.empty()) out.push_back(std::move(current));
    return out;
}

Lexicon::Lexicon() = default;

Glyph Lexicon::baseline_for(std::string_view token) {
    return encode_token(token);
}

Lexicon::Evidence& Lexicon::touch_(const std::string& token) {
    auto it = evidence_.find(token);
    if (it != evidence_.end()) return it->second;
    Evidence e;
    e.votes.assign(kGlyphBits, 0);
    e.observations = 0;
    auto [ins, _ok] = evidence_.emplace(token, std::move(e));
    return ins->second;
}

Glyph Lexicon::context_glyph_(const Evidence& e) const {
    Glyph g;
    if (e.observations == 0) return g;
    // Threshold at half of observation count -- bit set iff observed in
    // at least half of cooccurrences.
    const std::uint32_t threshold = e.observations / 2;
    for (std::size_t i = 0; i < kGlyphBits; ++i) {
        if (e.votes[i] > threshold) g.set_bit(i);
    }
    return g;
}

Glyph Lexicon::glyph_for(std::string_view token) const {
    const Glyph base = encode_token(token);
    auto it = evidence_.find(std::string{token});
    if (it == evidence_.end() || it->second.observations == 0) return base;
    const Glyph ctx = context_glyph_(it->second);
    return bundle({base, ctx});
}

bool Lexicon::has(std::string_view token) const {
    auto it = evidence_.find(std::string{token});
    return it != evidence_.end() && it->second.observations > 0;
}

std::uint32_t Lexicon::exposures_for(std::string_view token) const {
    auto it = evidence_.find(std::string{token});
    return (it == evidence_.end()) ? 0 : it->second.observations;
}

std::size_t Lexicon::expose_sequence(const std::vector<std::string>& tokens,
                                     std::size_t window) {
    std::size_t pairs = 0;
    if (window == 0) return 0;

    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const auto& a_tok = tokens[i];
        Evidence& a_ev = touch_(a_tok);

        const std::size_t lo = (i > window) ? (i - window) : 0;
        const std::size_t hi = std::min(tokens.size(), i + window + 1);

        for (std::size_t j = lo; j < hi; ++j) {
            if (j == i) continue;
            const Glyph b = encode_token(tokens[j]);
            // Add one vote to every bit b has set. std::countr_zero gives
            // us the index of the lowest set bit in each word; we mask it
            // off and repeat. Portable across MSVC/GCC/Clang.
            const auto& bw = b.words();
            for (std::size_t wi = 0; wi < bw.size(); ++wi) {
                std::uint64_t w = bw[wi];
                while (w) {
                    const std::size_t local_bit =
                        static_cast<std::size_t>(std::countr_zero(w));
                    const std::size_t bit_idx = wi * 64 + local_bit;
                    if (bit_idx < kGlyphBits &&
                        a_ev.votes[bit_idx] < UINT16_MAX) {
                        ++a_ev.votes[bit_idx];
                    }
                    w &= (w - 1);  // clear lowest set bit
                }
            }
            ++a_ev.observations;
            ++pairs;
        }
    }
    total_obs_ += pairs;
    return pairs;
}

std::size_t Lexicon::expose_text(std::string_view text, std::size_t window) {
    return expose_sequence(tokenize(text), window);
}

double Lexicon::similarity(std::string_view a, std::string_view b) const {
    return glyph_for(a).similarity(glyph_for(b));
}

} // namespace khora::lexicon
