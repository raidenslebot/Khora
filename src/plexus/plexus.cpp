#include "khora/plexus/plexus.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace khora::plexus {

namespace {

// Co-occurrences seen fewer than this many times are treated as noise at query
// time: a couple of chance meetings of two rare words is not an association.
constexpr std::uint32_t kMinCoocQuery = 3;

// Content filter for surfaced associates: a word in the ubiquitous
// high-frequency tail IS a function word (this is the definition, not a
// hand-list). Such words form real syntactic collocations ("knowledge OF")
// but carry no semantic kinship, so they are excluded from associates() —
// the same principle the Lexicon's salience filter uses. Affinity math is
// untouched; only the surfaced/selected kin are filtered.
constexpr double kStopFraction = 0.006;  // > ~0.6% of all tokens = function word

// Context-distribution smoothing exponent (Levy & Goldberg, 2014): raising the
// context probability to 0.75 lifts rare contexts, blunting PMI's notorious
// bias toward low-frequency pairs.
constexpr double kContextSmoothing = 0.75;

// A node's adjacency is pruned back to max_degree once it grows past this
// multiple of it — amortising the prune cost while bounding memory.
constexpr std::size_t kPruneTriggerFactor = 2;

} // namespace

Plexus::Plexus() = default;

std::uint32_t Plexus::intern_(const std::string& w) {
    auto it = ids_.find(w);
    if (it != ids_.end()) return it->second;
    const std::uint32_t id = static_cast<std::uint32_t>(word_.size());
    ids_.emplace(w, id);
    word_.push_back(w);
    occ_.push_back(0);
    adj_.emplace_back();
    return id;
}

std::int64_t Plexus::lookup_(std::string_view w) const {
    auto it = ids_.find(std::string{w});
    return (it == ids_.end()) ? -1 : static_cast<std::int64_t>(it->second);
}

double Plexus::ppmi_(std::uint32_t a, std::uint32_t b, std::uint32_t cab) const {
    if (cab == 0) return 0.0;
    const double N = static_cast<double>(total_tokens_ ? total_tokens_ : 1);
    const double W = static_cast<double>(total_cooc_   ? total_cooc_   : 1);
    const double ca = static_cast<double>(occ_[a] ? occ_[a] : 1);
    const double cb = static_cast<double>(occ_[b] ? occ_[b] : 1);

    // P(a,b) measured against the chance they would meet at random, P(a)*P(b),
    // with the context term b smoothed. The hub's loudness lives in ca/cb and
    // divides straight out.
    const double p_ab = static_cast<double>(cab) / W;
    const double p_a  = ca / N;
    const double p_b  = std::pow(cb, kContextSmoothing) / std::pow(N, kContextSmoothing);
    const double pmi  = std::log2(p_ab / (p_a * p_b));
    return pmi > 0.0 ? pmi : 0.0;
}

void Plexus::prune_(std::uint32_t node) {
    auto& edges = adj_[node];
    if (edges.size() <= max_degree_) return;

    // Rank surviving edges by confidence-weighted affinity: ppmi * log2(1+cooc).
    // This evicts BOTH the loud-but-meaningless (low ppmi) and the
    // flimsy-but-rare (low evidence), keeping a node's genuine strong kin.
    std::vector<std::pair<double, std::pair<std::uint32_t, std::uint32_t>>> scored;
    scored.reserve(edges.size());
    for (const auto& [nb, c] : edges) {
        const double s = ppmi_(node, nb, c) * std::log2(1.0 + static_cast<double>(c));
        scored.emplace_back(s, std::pair<std::uint32_t, std::uint32_t>{nb, c});
    }
    std::nth_element(scored.begin(), scored.begin() + max_degree_, scored.end(),
                     [](const auto& x, const auto& y) { return x.first > y.first; });
    scored.resize(max_degree_);

    std::unordered_map<std::uint32_t, std::uint32_t> kept;
    kept.reserve(max_degree_ * 2);
    for (const auto& [s, e] : scored) kept.emplace(e.first, e.second);
    edges.swap(kept);
}

std::size_t Plexus::observe(const std::vector<std::string>& tokens,
                            std::size_t window) {
    if (window == 0 || tokens.empty()) return 0;

    // Intern every token and count its frequency (the denominator of PMI).
    std::vector<std::uint32_t> ids;
    ids.reserve(tokens.size());
    for (const auto& t : tokens) {
        const std::uint32_t id = intern_(t);
        ++occ_[id];
        ids.push_back(id);
    }
    total_tokens_ += tokens.size();

    // Tally co-occurrence within the window. Edges are accrued in both
    // directions over the corpus, so the graph is effectively symmetric.
    std::size_t events = 0;
    const std::size_t n = ids.size();
    const std::size_t trigger = max_degree_ * kPruneTriggerFactor;
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint32_t fi = ids[i];
        auto& edges = adj_[fi];
        const std::size_t lo = (i > window) ? (i - window) : 0;
        const std::size_t hi = std::min(n, i + window + 1);
        for (std::size_t j = lo; j < hi; ++j) {
            if (j == i) continue;
            ++edges[ids[j]];
            ++total_cooc_;
            ++events;
        }
        if (edges.size() > trigger) prune_(fi);
    }
    return events;
}

double Plexus::affinity(std::string_view a, std::string_view b) const {
    const std::int64_t ia = lookup_(a);
    const std::int64_t ib = lookup_(b);
    if (ia < 0 || ib < 0) return 0.0;
    const auto& edges = adj_[static_cast<std::size_t>(ia)];
    auto it = edges.find(static_cast<std::uint32_t>(ib));
    if (it == edges.end() || it->second < kMinCoocQuery) return 0.0;
    return ppmi_(static_cast<std::uint32_t>(ia), static_cast<std::uint32_t>(ib),
                 it->second);
}

std::vector<std::pair<std::string, double>>
Plexus::associates(std::string_view word, std::size_t k) const {
    std::vector<std::pair<std::string, double>> out;
    const std::int64_t ia = lookup_(word);
    if (ia < 0 || k == 0) return out;

    const auto a = static_cast<std::uint32_t>(ia);
    // Rank by CONFIDENCE-WEIGHTED PMI: ppmi * log2(1+cooc). Pure PPMI over-rewards
    // rare single-meeting pairs (its well-known low-frequency bias); weighting by
    // evidence demotes that noise while leaving genuinely surprising, well-attested
    // kin on top. We still REPORT the pure PMI (interpretable bits of association).
    struct Cand { double rank; double ppmi; std::uint32_t nb; };
    std::vector<Cand> scored;
    scored.reserve(adj_[a].size());
    // The function-word filter needs enough corpus for frequency stats to mean
    // anything; below that (e.g. unit tests) it is disabled so tiny vocabularies
    // stay intact.
    const double stop_occ = (total_tokens_ >= 10000)
        ? static_cast<double>(total_tokens_) * kStopFraction
        : static_cast<double>(total_tokens_) + 1.0;   // unreachable => no filtering
    for (const auto& [nb, c] : adj_[a]) {
        if (c < kMinCoocQuery) continue;
        if (static_cast<double>(occ_[nb]) > stop_occ) continue;  // function word — no semantic kinship
        const double p = ppmi_(a, nb, c);
        if (p > 0.0) scored.push_back({ p * std::log2(1.0 + static_cast<double>(c)), p, nb });
    }
    if (scored.empty()) return out;

    const std::size_t keep = std::min(k, scored.size());
    std::partial_sort(scored.begin(), scored.begin() + keep, scored.end(),
                      [](const Cand& x, const Cand& y) { return x.rank > y.rank; });
    out.reserve(keep);
    for (std::size_t i = 0; i < keep; ++i)
        out.emplace_back(word_[scored[i].nb], scored[i].ppmi);
    return out;
}

void Plexus::absorb(const Plexus& other) {
    if (other.word_.empty()) return;
    // Map other's node ids into this graph (interning new words), summing
    // occurrences, then sum every edge's co-occurrence count.
    std::vector<std::uint32_t> remap(other.word_.size());
    for (std::uint32_t oid = 0; oid < other.word_.size(); ++oid) {
        const std::uint32_t mid = intern_(other.word_[oid]);
        remap[oid] = mid;
        occ_[mid] += other.occ_[oid];
    }
    for (std::uint32_t oid = 0; oid < other.adj_.size(); ++oid) {
        auto& dst = adj_[remap[oid]];
        for (const auto& [onb, c] : other.adj_[oid]) dst[remap[onb]] += c;
    }
    total_tokens_ += other.total_tokens_;
    total_cooc_   += other.total_cooc_;
}

void Plexus::prune_all() {
    for (std::uint32_t i = 0; i < adj_.size(); ++i)
        if (adj_[i].size() > max_degree_) prune_(i);
}

std::uint64_t Plexus::edge_count() const noexcept {
    std::uint64_t e = 0;
    for (const auto& m : adj_) e += m.size();
    return e;
}

std::uint32_t Plexus::occurrences(std::string_view word) const {
    const std::int64_t i = lookup_(word);
    return (i < 0) ? 0u : occ_[static_cast<std::size_t>(i)];
}

bool Plexus::has(std::string_view word) const { return lookup_(word) >= 0; }

// ---- Persistence: a single compact binary file -----------------------------
//
// Layout (little-endian native):
//   magic "KPLX", u32 version
//   u64 total_tokens, u64 total_cooc, u32 vocab
//   per node: u16 word_len, bytes word, u32 occ
//   per node: u32 degree, then degree * (u32 neighbour_id, u32 cooc)

namespace {
template <typename T> void wr(std::ofstream& os, const T& v) {
    os.write(reinterpret_cast<const char*>(&v), sizeof(T));
}
template <typename T> T rd(std::ifstream& is) {
    T v{}; is.read(reinterpret_cast<char*>(&v), sizeof(T)); return v;
}
} // namespace

void Plexus::save(const std::filesystem::path& prefix) const {
    namespace fs = std::filesystem;
    if (prefix.has_parent_path()) fs::create_directories(prefix.parent_path());
    auto path = prefix; path += ".plexus";
    std::ofstream os(path, std::ios::binary | std::ios::trunc);
    if (!os) return;

    os.write("KPLX", 4);
    wr<std::uint32_t>(os, 1u);
    wr<std::uint64_t>(os, total_tokens_);
    wr<std::uint64_t>(os, total_cooc_);
    const std::uint32_t n = static_cast<std::uint32_t>(word_.size());
    wr<std::uint32_t>(os, n);

    for (std::uint32_t i = 0; i < n; ++i) {
        const std::uint16_t len =
            static_cast<std::uint16_t>(std::min<std::size_t>(word_[i].size(), 0xFFFF));
        wr<std::uint16_t>(os, len);
        os.write(word_[i].data(), len);
        wr<std::uint32_t>(os, occ_[i]);
    }
    for (std::uint32_t i = 0; i < n; ++i) {
        wr<std::uint32_t>(os, static_cast<std::uint32_t>(adj_[i].size()));
        for (const auto& [nb, c] : adj_[i]) { wr<std::uint32_t>(os, nb); wr<std::uint32_t>(os, c); }
    }
}

void Plexus::load(const std::filesystem::path& prefix) {
    namespace fs = std::filesystem;
    auto path = prefix; path += ".plexus";
    if (!fs::exists(path)) return;
    std::ifstream is(path, std::ios::binary);
    if (!is) return;

    char magic[4] = {0, 0, 0, 0};
    is.read(magic, 4);
    if (std::memcmp(magic, "KPLX", 4) != 0) return;
    (void)rd<std::uint32_t>(is);  // version

    ids_.clear(); word_.clear(); occ_.clear(); adj_.clear();

    total_tokens_ = rd<std::uint64_t>(is);
    total_cooc_   = rd<std::uint64_t>(is);
    const std::uint32_t n = rd<std::uint32_t>(is);

    word_.reserve(n); occ_.reserve(n); adj_.reserve(n); ids_.reserve(n * 2);
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::uint16_t len = rd<std::uint16_t>(is);
        std::string w(len, '\0');
        if (len) is.read(w.data(), len);
        const std::uint32_t o = rd<std::uint32_t>(is);
        ids_.emplace(w, i);
        word_.push_back(std::move(w));
        occ_.push_back(o);
        adj_.emplace_back();
    }
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::uint32_t deg = rd<std::uint32_t>(is);
        auto& edges = adj_[i];
        edges.reserve(deg * 2);
        for (std::uint32_t e = 0; e < deg; ++e) {
            const std::uint32_t nb = rd<std::uint32_t>(is);
            const std::uint32_t c  = rd<std::uint32_t>(is);
            if (is) edges.emplace(nb, c);
        }
    }
}

} // namespace khora::plexus
