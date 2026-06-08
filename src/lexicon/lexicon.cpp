#include "khora/lexicon/lexicon.hpp"

#include "khora/lattice/lattice.hpp"
#include "khora/lattice/persistence.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace khora::lexicon {

using khora::lattice::Glyph;
using khora::lattice::bind;
using khora::lattice::bundle;
using khora::lattice::position_glyph;
using khora::lattice::kGlyphBits;

namespace {

inline std::uint64_t splitmix64(std::uint64_t& s) noexcept {
    std::uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

inline char to_lower(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

std::uint64_t fnv1a(std::string_view s) {
    std::uint64_t h = 0xCBF29CE484222325ULL;
    for (unsigned char c : s) { h ^= c; h *= 0x100000001B3ULL; }
    return h;
}

Glyph trigram_glyph(std::string_view tri) {
    return Glyph::random(fnv1a(tri));
}

std::string normalise(std::string_view raw) {
    std::string norm = "^";
    norm.reserve(raw.size() + 2);
    for (char c : raw) {
        if (std::isalnum(static_cast<unsigned char>(c))) norm.push_back(to_lower(c));
    }
    if (norm.size() == 1) return {};
    norm.push_back('$');
    return norm;
}

// Random Indexing parameters: K +/- pairs of nonzeros per index vector.
constexpr int kIndexPairs = 12;

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
            // Bind each trigram to its position slot (word-parallel XOR),
            // marking order far more cheaply than cyclic permutation.
            primitives.push_back(bind(trigram_glyph(norm.substr(i, 3)),
                                      position_glyph(i)));
        }
    }
    return bundle(std::span<const Glyph>{primitives.data(), primitives.size()});
}

std::vector<std::string> tokenize(std::string_view text) {
    std::vector<std::string> out;
    std::string current;
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c))) current.push_back(to_lower(c));
        else if (!current.empty()) { out.push_back(std::move(current)); current.clear(); }
    }
    if (!current.empty()) out.push_back(std::move(current));
    return out;
}

Lexicon::Lexicon() = default;

Glyph Lexicon::baseline_for(std::string_view token) { return encode_token(token); }

void Lexicon::add_index_(std::vector<std::int32_t>& acc,
                         std::string_view source_token, int sign) {
    // The source token's sparse ternary index vector: K positions at +sign,
    // K at -sign, chosen deterministically from its hash.
    std::uint64_t state = fnv1a(source_token);
    for (int k = 0; k < kIndexPairs; ++k) {
        const std::size_t p_plus  = static_cast<std::size_t>(splitmix64(state) % kGlyphBits);
        const std::size_t p_minus = static_cast<std::size_t>(splitmix64(state) % kGlyphBits);
        acc[p_plus]  += sign;
        acc[p_minus] -= sign;
    }
}

Lexicon::Context& Lexicon::touch_(const std::string& token) {
    auto it = ctx_.find(token);
    if (it != ctx_.end()) return it->second;
    Context c;
    c.acc.assign(kGlyphBits, 0);
    auto [ins, _ok] = ctx_.emplace(token, std::move(c));
    return ins->second;
}

Glyph Lexicon::binarise_(const Context& c) const {
    Glyph g;
    for (std::size_t i = 0; i < kGlyphBits; ++i) {
        if (c.acc[i] > 0) g.set_bit(i);
    }
    return g;
}

Glyph Lexicon::glyph_for(std::string_view token) const {
    const Glyph base = encode_token(token);
    auto it = ctx_.find(std::string{token});
    if (it == ctx_.end() || it->second.obs == 0) return base;
    return bundle({base, binarise_(it->second)});
}

bool Lexicon::has(std::string_view token) const {
    auto it = ctx_.find(std::string{token});
    return it != ctx_.end() && it->second.obs > 0;
}

std::uint32_t Lexicon::exposures_for(std::string_view token) const {
    auto it = ctx_.find(std::string{token});
    return (it == ctx_.end()) ? 0 : it->second.obs;
}

std::size_t Lexicon::expose_sequence(const std::vector<std::string>& tokens,
                                     std::size_t window) {
    if (window == 0) return 0;
    std::size_t pairs = 0;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        Context& focus = touch_(tokens[i]);
        const std::size_t lo = (i > window) ? (i - window) : 0;
        const std::size_t hi = std::min(tokens.size(), i + window + 1);
        for (std::size_t j = lo; j < hi; ++j) {
            if (j == i) continue;
            add_index_(focus.acc, tokens[j], +1);  // ~K increments, not ~N bits
            ++pairs;
        }
        ++focus.obs;
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

void Lexicon::save(const std::filesystem::path& prefix) const {
    namespace fs = std::filesystem;
    if (prefix.has_parent_path()) fs::create_directories(prefix.parent_path());

    // Binarised context glyphs go into a Lattice (compact, reuses .klat).
    khora::lattice::Lattice sem;
    for (const auto& [tok, c] : ctx_) {
        if (c.obs == 0) continue;
        sem.store(tok, binarise_(c));
    }
    auto sem_path = prefix; sem_path += ".sem.klat";
    khora::lattice::save(sem, sem_path);

    // Observation counts alongside.
    auto obs_path = prefix; obs_path += ".lexobs";
    std::ofstream os(obs_path, std::ios::trunc);
    for (const auto& [tok, c] : ctx_) {
        if (c.obs == 0) continue;
        std::string safe = tok;
        for (char& ch : safe) if (ch == '\t' || ch == '\n') ch = ' ';
        os << safe << '\t' << c.obs << '\n';
    }
}

void Lexicon::load(const std::filesystem::path& prefix) {
    namespace fs = std::filesystem;
    auto sem_path = prefix; sem_path += ".sem.klat";
    if (!fs::exists(sem_path)) return;

    khora::lattice::Lattice sem = khora::lattice::load(sem_path);

    // Observation counts.
    std::unordered_map<std::string, std::uint32_t> obs;
    auto obs_path = prefix; obs_path += ".lexobs";
    std::ifstream is(obs_path);
    std::string line;
    while (std::getline(is, line)) {
        const std::size_t tab = line.rfind('\t');
        if (tab == std::string::npos) continue;
        try { obs[line.substr(0, tab)] = static_cast<std::uint32_t>(std::stoul(line.substr(tab + 1))); }
        catch (...) {}
    }

    ctx_.clear();
    total_obs_ = 0;
    for (const auto& [tok, g] : sem) {
        Context c;
        c.acc.assign(kGlyphBits, 0);
        // Seed the accumulator from the stored signs so continued learning
        // builds on prior evidence rather than starting cold.
        const std::uint32_t o = obs.count(tok) ? obs[tok] : 1;
        const std::int32_t mag = static_cast<std::int32_t>(std::min<std::uint32_t>(o, 32));
        for (std::size_t i = 0; i < kGlyphBits; ++i) c.acc[i] = g.bit(i) ? mag : -mag;
        c.obs = o;
        total_obs_ += o;
        ctx_.emplace(tok, std::move(c));
    }
}

} // namespace khora::lexicon
