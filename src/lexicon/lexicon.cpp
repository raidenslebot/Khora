#include "khora/lexicon/lexicon.hpp"

#include "khora/lattice/lattice.hpp"
#include "khora/lattice/persistence.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <random>
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

std::vector<std::vector<std::string>> tokenize_sentences(std::string_view text) {
    std::vector<std::vector<std::string>> out;
    std::vector<std::string> sentence;
    std::string current;
    int newlines = 0;

    const auto end_word = [&]() {
        if (!current.empty()) { sentence.push_back(std::move(current)); current.clear(); }
    };
    const auto end_sentence = [&]() {
        end_word();
        if (!sentence.empty()) { out.push_back(std::move(sentence)); sentence.clear(); }
    };

    for (const char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            current.push_back(to_lower(c));
            newlines = 0;
        } else {
            end_word();
            if (c == '.' || c == '!' || c == '?' || c == ';' || c == ':') {
                end_sentence();
                newlines = 0;
            } else if (c == '\n') {
                // A blank line is a paragraph break, and prose that ends a
                // paragraph without punctuation is common in these texts --
                // headings, verse, list items.
                if (++newlines >= 2) { end_sentence(); newlines = 0; }
            } else if (c != '\r' && c != ' ' && c != '\t') {
                newlines = 0;
            }
        }
    }
    end_sentence();
    return out;
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
                         std::string_view source_token, int weight) {
    // The source token's sparse ternary index vector: K positions at
    // +weight, K at -weight, chosen deterministically from its hash.
    std::uint64_t state = fnv1a(source_token);
    for (int k = 0; k < kIndexPairs; ++k) {
        const std::size_t p_plus  = static_cast<std::size_t>(splitmix64(state) % kGlyphBits);
        const std::size_t p_minus = static_cast<std::size_t>(splitmix64(state) % kGlyphBits);
        acc[p_plus]  += weight;
        acc[p_minus] -= weight;
    }
}

int Lexicon::weight_for_(const std::string& token) const {
    // Inverse-frequency weight: rare, meaningful neighbours carry more
    // signal than ubiquitous function words. idf = log(total / freq),
    // scaled and clamped to a small positive integer range.
    auto it = freq_.find(token);
    const double f = (it == freq_.end()) ? 1.0 : static_cast<double>(it->second);
    const double total = static_cast<double>(total_tokens_ > 0 ? total_tokens_ : 1);
    const double idf = std::log((total + 1.0) / (f + 1.0));
    int w = static_cast<int>(idf * 1.5 + 0.5);
    if (w < 1)  w = 1;
    if (w > 16) w = 16;
    return w;
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

std::vector<std::pair<std::string, Glyph>> Lexicon::semantic_field() const {
    std::vector<std::pair<std::string, Glyph>> out;
    out.reserve(ctx_.size());
    for (const auto& [tok, c] : ctx_) {
        if (c.obs == 0) continue;
        out.emplace_back(tok, glyph_for(tok));
    }
    return out;
}

Glyph Lexicon::context_glyph(std::string_view token) const {
    auto it = ctx_.find(std::string{token});
    if (it == ctx_.end() || it->second.obs == 0) return Glyph::zero();
    return binarise_(it->second);
}

std::vector<std::pair<std::string, Glyph>> Lexicon::context_field() const {
    std::vector<std::pair<std::string, Glyph>> out;
    out.reserve(ctx_.size());
    for (const auto& [tok, c] : ctx_) {
        if (c.obs == 0) continue;
        out.emplace_back(tok, binarise_(c));
    }
    return out;
}

std::uint32_t Lexicon::exposures_for(std::string_view token) const {
    auto it = ctx_.find(std::string{token});
    return (it == ctx_.end()) ? 0 : it->second.obs;
}

std::size_t Lexicon::prune(std::size_t target) {
    if (ctx_.size() <= target) return 0;
    std::vector<std::pair<std::uint32_t, std::string>> v;
    v.reserve(ctx_.size());
    for (const auto& [tok, c] : ctx_) v.emplace_back(c.obs, tok);
    // Keep the `target` most-exposed words; drop the rest (their heavy
    // accumulators are freed; their small frequency counts are retained).
    std::sort(v.begin(), v.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    std::size_t removed = 0;
    for (std::size_t i = target; i < v.size(); ++i) { ctx_.erase(v[i].second); ++removed; }
    return removed;
}

std::vector<std::string> Lexicon::salient_tokens(std::size_t max_tokens,
                                                 std::uint32_t min_exposure) const {
    std::vector<std::pair<std::string, std::uint32_t>> cands;
    cands.reserve(ctx_.size());
    for (const auto& [tok, c] : ctx_) {
        if (c.obs < min_exposure) continue;
        // Length >= 3: two-letter tokens ("be","it","of","to") are almost
        // all function words or fragments, and their sparse trigram glyphs
        // act as structural hubs that swallow every train of thought.
        if (tok.size() < 3) continue;
        // Keep only genuine content words: those appearing in under ~1% of
        // positions (high idf). Frequent words score lower and are
        // distributional hubs, so they are excluded from the concept space.
        if (weight_for_(tok) < 8) continue;
        cands.emplace_back(tok, c.obs);
    }
    std::sort(cands.begin(), cands.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    std::vector<std::string> out;
    out.reserve(std::min(max_tokens, cands.size()));
    for (std::size_t i = 0; i < cands.size() && i < max_tokens; ++i)
        out.push_back(cands[i].first);
    return out;
}

std::size_t Lexicon::expose_sequence(const std::vector<std::string>& tokens,
                                     std::size_t window) {
    if (window == 0) return 0;

    // Pass 1: update global frequencies so weights + subsampling reflect
    // the corpus including this sequence.
    for (const auto& t : tokens) ++freq_[t];
    total_tokens_ += tokens.size();

    // Pass 2: subsample — drop ubiquitous words from the stream entirely
    // (word2vec keep-probability). This stops function words like "the"
    // from both polluting and being polluted, so meaning concentrates in
    // content words. Only applied once the corpus is large enough to have
    // meaningful frequency statistics.
    std::vector<const std::string*> stream;
    stream.reserve(tokens.size());
    if (total_tokens_ < 2000) {
        for (const auto& t : tokens) stream.push_back(&t);
    } else {
        std::mt19937 rng(0x5EED ^ static_cast<unsigned>(total_tokens_));
        std::uniform_real_distribution<double> unif(0.0, 1.0);
        constexpr double kSampleT = 1e-3;
        for (const auto& t : tokens) {
            const double z = static_cast<double>(freq_[t]) / static_cast<double>(total_tokens_);
            double keep = 1.0;
            if (z > kSampleT) {
                keep = (std::sqrt(z / kSampleT) + 1.0) * (kSampleT / z);  // word2vec
            }
            if (keep >= 1.0 || unif(rng) < keep) stream.push_back(&t);
        }
    }

    // Pass 3: accumulate context over the subsampled stream, weighting
    // each surviving neighbour by its inverse frequency.
    std::size_t pairs = 0;
    for (std::size_t i = 0; i < stream.size(); ++i) {
        Context& focus = touch_(*stream[i]);
        const std::size_t lo = (i > window) ? (i - window) : 0;
        const std::size_t hi = std::min(stream.size(), i + window + 1);
        for (std::size_t j = lo; j < hi; ++j) {
            if (j == i) continue;
            add_index_(focus.acc, *stream[j], weight_for_(*stream[j]));
            ++pairs;
        }
        ++focus.obs;
    }
    total_obs_ += pairs;

    // Keep the vocabulary's heavy accumulators within the memory cap.
    if (max_vocabulary_ > 0 && ctx_.size() > max_vocabulary_) {
        prune(max_vocabulary_);
    }
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

    // Observation + frequency counts alongside.
    auto obs_path = prefix; obs_path += ".lexobs";
    std::ofstream os(obs_path, std::ios::trunc);
    for (const auto& [tok, c] : ctx_) {
        if (c.obs == 0) continue;
        std::string safe = tok;
        for (char& ch : safe) if (ch == '\t' || ch == '\n') ch = ' ';
        auto fit = freq_.find(tok);
        const std::uint32_t f = (fit == freq_.end()) ? 0u : fit->second;
        os << safe << '\t' << c.obs << '\t' << f << '\n';
    }
}

void Lexicon::load(const std::filesystem::path& prefix) {
    namespace fs = std::filesystem;
    auto sem_path = prefix; sem_path += ".sem.klat";
    if (!fs::exists(sem_path)) return;

    khora::lattice::Lattice sem = khora::lattice::load(sem_path);

    // Observation + frequency counts.  Format: token \t obs [\t freq]
    std::unordered_map<std::string, std::uint32_t> obs;
    freq_.clear();
    total_tokens_ = 0;
    auto obs_path = prefix; obs_path += ".lexobs";
    std::ifstream is(obs_path);
    std::string line;
    while (std::getline(is, line)) {
        const std::size_t t1 = line.find('\t');
        if (t1 == std::string::npos) continue;
        const std::string tok = line.substr(0, t1);
        const std::size_t t2 = line.find('\t', t1 + 1);
        try {
            if (t2 == std::string::npos) {
                obs[tok] = static_cast<std::uint32_t>(std::stoul(line.substr(t1 + 1)));
            } else {
                obs[tok]   = static_cast<std::uint32_t>(std::stoul(line.substr(t1 + 1, t2 - t1 - 1)));
                const auto f = static_cast<std::uint32_t>(std::stoul(line.substr(t2 + 1)));
                freq_[tok]   = f;
                total_tokens_ += f;
            }
        } catch (...) {}
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
