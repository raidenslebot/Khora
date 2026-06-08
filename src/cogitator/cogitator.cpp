#include "khora/cogitator/cogitator.hpp"

#include "khora/plexus/plexus.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <cstdlib>
#include <fstream>
#include <future>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace khora::cogitator {

using khora::lattice::Glyph;
using khora::lattice::LatticeMatch;
using khora::lattice::bundle;
using khora::soma::Affinity;
using khora::soma::Drive;

Cogitator::Cogitator(khora::lexicon::Lexicon&         lex,
                     khora::lattice::Lattice&         memory,
                     khora::cortex::PredictiveColumn& cortex,
                     khora::soma::SomaNexus&          soma)
    : lex_(lex), memory_(memory), cortex_(cortex), soma_(soma) {}

Glyph Cogitator::token_glyph_(const std::string& tok) const {
    const Glyph g = lex_.context_glyph(tok);            // distributional meaning
    return g.popcount() > 0 ? g : lex_.glyph_for(tok);  // structural fallback if unlearned
}

Glyph Cogitator::encode_(const std::vector<std::string>& tokens) const {
    if (tokens.empty()) return Glyph::zero();
    std::vector<Glyph> gs;
    gs.reserve(tokens.size());
    for (const auto& t : tokens) if (is_content_(t)) gs.push_back(token_glyph_(t));
    if (gs.empty())  // all function words — fall back to the whole stimulus
        for (const auto& t : tokens) gs.push_back(token_glyph_(t));
    return bundle(std::span<const Glyph>{gs.data(), gs.size()});
}

Glyph Cogitator::gestalt_(const Glyph& probe,
                          const std::vector<LatticeMatch>& res) const {
    std::vector<Glyph> ingredients;
    ingredients.reserve(1 + res.size());
    if (probe.popcount() > 0) ingredients.push_back(probe);
    for (const auto& m : res) {
        const Glyph g = recall_(m.label);
        if (g.popcount() > 0) ingredients.push_back(g);
    }
    if (ingredients.empty()) return Glyph::zero();
    return bundle(std::span<const Glyph>{ingredients.data(), ingredients.size()});
}

// (Re)index the resonance field from the Lexicon whenever the vocabulary
// has changed. The field is every learned word's glyph_for — cognition's
// probe (a bundle of glyph_for tokens) lives in the same space, so the
// match is exact. Rebuilds are lazy: one per vocabulary change, never
// mid-deliberation.
void Cogitator::ensure_field_() {
    const std::size_t v = lex_.vocabulary_size();
    const std::size_t na = abstractions_.size();
    // Rebuild when the vocabulary changes, or when the tower has grown enough
    // that cognition should resonate over the new abstractions too.
    if (v == indexed_vocab_ && na >= indexed_abstractions_ && na < indexed_abstractions_ + 16)
        return;

    // Resonate over CONTENT words only — the salient vocabulary, with the
    // ubiquitous function words already filtered out — so cognition lands on
    // meaning, not connective tissue. Fall back to the whole field for a
    // small lexicon.
    std::vector<std::pair<std::string, Glyph>> entries;
    content_.clear();
    {
        const auto salient = lex_.salient_tokens(200000, 3);
        entries.reserve(salient.size());
        for (const auto& w : salient) entries.emplace_back(w, lex_.context_glyph(w));
        if (entries.size() < 50) {
            entries = lex_.context_field();   // tiny lexicon: keep content_ empty
        } else {
            content_.insert(salient.begin(), salient.end());
        }
    }
    field_.build(entries);

    // Demote any residual distributional hubs (centrality outliers, mean+2σ)
    // so thought resonates with content, not connective tissue. The surviving
    // labels become the clean concept set the Volition seeds thought from.
    std::vector<std::pair<std::string, Glyph>> final_set;
    bool demoted = false;
    if (entries.size() > 64) {
        const auto deg = field_.centrality(10);
        if (deg.size() == entries.size()) {
            double mean = 0.0;
            for (auto dd : deg) mean += dd;
            mean /= static_cast<double>(deg.size());
            double var = 0.0;
            for (auto dd : deg) { const double e = dd - mean; var += e * e; }
            var /= static_cast<double>(deg.size());
            const double cut = mean + 2.0 * std::sqrt(var);
            std::vector<std::pair<std::string, Glyph>> clean;
            clean.reserve(entries.size());
            for (std::size_t i = 0; i < entries.size(); ++i)
                if (deg[i] <= cut) clean.push_back(entries[i]);
            if (clean.size() >= 2 && clean.size() < entries.size()) {
                final_set = std::move(clean);
                demoted = true;
            }
        }
    }
    if (!demoted) final_set = std::move(entries);

    // The content labels are what the Volition SEEDS thought from (words only).
    concepts_.clear();
    concepts_.reserve(final_set.size());
    for (auto& e : final_set) concepts_.push_back(e.first);

    // Fold the rising tower in: cognition now RESONATES over abstractions too,
    // so thought reaches higher-order concepts and forges still-higher ones —
    // the abstraction loop closing back into cognition.
    for (const auto& a : abstractions_) final_set.emplace_back(a.name, a.glyph);

    field_.build(final_set);
    indexed_vocab_ = v;
    indexed_abstractions_ = na;
}

// Single-probe resonance: the Lexicon field if populated, else the
// provisional memory_. Callers on the linear path (think) only.
std::vector<LatticeMatch> Cogitator::resonate_(const Glyph& probe, std::size_t k) const {
    if (field_.size() > 0) return field_.query(probe, k);
    if (memory_.size() > 0) return memory_.query(probe, k);
    return {};
}

// Batched resonance: every probe in one shot. One GPU dispatch over the
// field when active, so a deliberation's facets never contend for the
// device. CPU/memory fallback preserves behaviour with no field.
std::vector<std::vector<LatticeMatch>> Cogitator::resonate_batch_(
    const std::vector<Glyph>& probes, std::size_t k) const {
    if (field_.size() > 0) return field_.query_batch(probes, k);
    std::vector<std::vector<LatticeMatch>> out;
    out.reserve(probes.size());
    for (const auto& p : probes)
        out.push_back(memory_.size() > 0 ? memory_.query(p, k) : std::vector<LatticeMatch>{});
    return out;
}

// Recover the glyph behind a resonance label: a coined concept from memory_,
// or a learned word's glyph from the Lexicon. Zero if neither knows it.
Glyph Cogitator::recall_(const std::string& label) const {
    if (auto g = memory_.recall(label)) return *g;
    if (lex_.has(label)) return lex_.glyph_for(label);
    return Glyph::zero();
}

std::string Cogitator::wandering_seed(std::uint64_t n) {
    ensure_field_();
    if (concepts_.empty()) return std::string{};
    // concepts_ is exposure-ordered; the head is still function-word-heavy
    // even after hub demotion, so skip it and wander the content body.
    const std::size_t skip = std::min<std::size_t>(concepts_.size() / 2, 60);
    const std::size_t span = (concepts_.size() > skip) ? concepts_.size() - skip
                                                       : concepts_.size();
    const std::size_t base = (concepts_.size() > skip) ? skip : 0;
    return concepts_[base + (n % span)];
}

Glyph Cogitator::concept_glyph_any_(const std::string& name, int& level) const {
    for (const auto& a : abstractions_)
        if (a.name == name) { level = a.level; return a.glyph; }
    level = 0;
    return lex_.context_glyph(name);
}

namespace {
// PMI affinities are unbounded (0 .. ~15); the coherence bar is in [0,1]. This
// scale squashes mean pairwise affinity into [0,1) via aff/(aff+scale): a mean
// affinity equal to the scale maps to 0.5. Tuned to the real corpus so coherent
// clusters land in the self-escalating bar's working range.
constexpr double kPmiCoherenceScale = 2.5;
} // namespace

std::string Cogitator::form_abstraction_plexus_(const std::string& seed, std::size_t k,
                                                double min_coherence) {
    int seed_level = 0;
    const Glyph seedG = concept_glyph_any_(seed, seed_level);
    if (seedG.popcount() == 0) return {};

    // Sharp kin by mutual information — the hubs already divided out. This is
    // the clean structure the Spire starved for on the Hamming field.
    const auto kin = plexus_->associates(seed, k + 6);
    if (kin.size() < 1) return {};

    std::vector<Glyph> parts{ seedG };
    std::vector<std::string> members{ seed };
    for (const auto& [w, aff] : kin) {
        if (members.size() >= k + 1) break;
        if (w == seed) continue;
        int l = 0;
        const Glyph g = concept_glyph_any_(w, l);
        if (g.popcount() == 0) continue;     // must be representable as a concept
        parts.push_back(g);
        members.push_back(w);
    }
    if (members.size() < 2) return {};

    // Coherence by mutual information: mean pairwise PMI affinity across ALL
    // members (not just spokes from the seed), so a tightly inter-associated
    // cluster scores high and a mere star scores low. Squashed into [0,1] so
    // the self-escalating coherence bar transfers unchanged.
    double aff_sum = 0.0; std::size_t pairs = 0;
    for (std::size_t i = 0; i < members.size(); ++i)
        for (std::size_t j = i + 1; j < members.size(); ++j) {
            aff_sum += plexus_->affinity(members[i], members[j]);
            ++pairs;
        }
    const double mean_aff = pairs ? aff_sum / static_cast<double>(pairs) : 0.0;
    const double coh = mean_aff / (mean_aff + kPmiCoherenceScale);
    if (coh < min_coherence) return {};

    Abstraction a;
    a.glyph     = bundle(std::span<const Glyph>{parts.data(), parts.size()});
    a.level     = seed_level + 1;
    a.coherence = coh;
    a.members   = members;
    a.name = "{";
    const std::size_t mn = std::min<std::size_t>(3, members.size());
    for (std::size_t i = 0; i < mn; ++i) { if (i) a.name += "+"; a.name += members[i]; }
    if (members.size() > mn) a.name += "+..";
    a.name += "}#" + std::to_string(abstraction_seq_++);
    abstractions_.push_back(std::move(a));
    return abstractions_.back().name;
}

void Cogitator::ground_concept_(const std::string& name,
                                std::unordered_set<std::string>& out, int depth) const {
    if (depth > 5 || out.size() >= 48) return;
    for (const auto& a : abstractions_) {
        if (a.name == name) {
            for (const auto& m : a.members) ground_concept_(m, out, depth + 1);
            return;
        }
    }
    out.insert(name);  // a corpus word (its own leaf), or an unknown name
}

double Cogitator::leafset_affinity_(const std::unordered_set<std::string>& a,
                                    const std::unordered_set<std::string>& b) const {
    if (!plexus_ || a.empty() || b.empty()) return 0.0;
    // Cluster linkage by the STRONGEST conceptual bridges, not the diluted
    // average over all (mostly unrelated) leaf pairs. Averaging everything would
    // pull cross-level coherence far below the word-level scale, so a single
    // self-escalating bar could never govern the whole tower; the top-k bridges
    // keep it on the same scale as word-word affinity.
    std::vector<double> bridges;
    bridges.reserve(a.size());
    for (const auto& x : a)
        for (const auto& y : b) {
            if (x == y) continue;
            const double v = plexus_->affinity(x, y);
            if (v > 0.0) bridges.push_back(v);
        }
    if (bridges.empty()) return 0.0;
    const std::size_t topn = std::min<std::size_t>(3, bridges.size());
    std::partial_sort(bridges.begin(), bridges.begin() + topn, bridges.end(),
                      [](double x, double y) { return x > y; });
    double s = 0.0;
    for (std::size_t i = 0; i < topn; ++i) s += bridges[i];
    return s / static_cast<double>(topn);
}

double Cogitator::seed_coherence_(const std::string& seed) const {
    if (!plexus_ || !plexus_->has(seed)) return 0.0;
    const auto kin = plexus_->associates(seed, 8);
    if (kin.size() < 2) return 0.0;

    // Build the seed's 2-HOP neighbourhood — its kin, plus each kin's own top
    // kin — and measure the cohesion of that whole region. This is a truer,
    // heavier signal of whether the seed anchors a genuinely coherent concept
    // cluster than the 1-hop star, and it is the substantial parallel work the
    // Furnace burns the idle cores on.
    std::vector<std::string> region{ seed };
    for (const auto& kv : kin) {
        if (region.size() >= 9) break;
        if (kv.first != seed) region.push_back(kv.first);
    }
    const std::size_t inner = region.size();
    for (std::size_t m = 1; m < inner; ++m) {
        if (region.size() >= 28) break;
        for (const auto& kv : plexus_->associates(region[m], 4)) {
            if (region.size() >= 28) break;
            if (std::find(region.begin(), region.end(), kv.first) == region.end())
                region.push_back(kv.first);
        }
    }

    double aff_sum = 0.0; std::size_t pairs = 0;
    for (std::size_t i = 0; i < region.size(); ++i)
        for (std::size_t j = i + 1; j < region.size(); ++j) {
            aff_sum += plexus_->affinity(region[i], region[j]);
            ++pairs;
        }
    const double mean_aff = pairs ? aff_sum / static_cast<double>(pairs) : 0.0;
    return mean_aff / (mean_aff + kPmiCoherenceScale);
}

std::vector<std::pair<std::string, double>>
Cogitator::scout_abstractions(std::size_t samples, unsigned threads,
                              double min_coherence) const {
    if (!plexus_ || concepts_.empty() || samples == 0) return {};
    if (threads < 1)  threads = 1;
    if (threads > 64) threads = 64;
    const std::size_t N = concepts_.size();

    // Each thread scores a disjoint band of sample indices into `out` (no shared
    // writes), reading only the const Plexus + concept set. Pure parallel reads.
    std::vector<std::pair<std::string, double>> out(samples);
    auto work = [this, &out, N](std::size_t lo, std::size_t hi) {
        for (std::size_t s = lo; s < hi; ++s) {
            const std::size_t idx =
                static_cast<std::size_t>((s * 2654435761ull + 1099511628211ull) % N);
            const std::string& seed = concepts_[idx];
            out[s] = { seed, seed_coherence_(seed) };
        }
    };
    std::vector<std::thread> pool;
    pool.reserve(threads);
    const std::size_t chunk = (samples + threads - 1) / threads;
    for (unsigned t = 0; t < threads; ++t) {
        const std::size_t lo = static_cast<std::size_t>(t) * chunk;
        const std::size_t hi = std::min(samples, lo + chunk);
        if (lo >= hi) break;
        pool.emplace_back(work, lo, hi);
    }
    for (auto& th : pool) th.join();

    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    std::vector<std::pair<std::string, double>> top;
    std::unordered_set<std::string> seen;
    for (auto& c : out) {
        if (c.first.empty() || c.second < min_coherence) continue;
        if (!seen.insert(c.first).second) continue;   // dedupe repeated samples
        top.push_back(c);
        if (top.size() >= 8) break;
    }
    return top;
}

std::string Cogitator::form_abstraction_over_abstractions_(const std::string& seed,
                                                           std::size_t k, double min_coherence) {
    const Abstraction* seedA = nullptr;
    for (const auto& a : abstractions_) if (a.name == seed) { seedA = &a; break; }
    if (!seedA) return {};

    std::unordered_set<std::string> seedLeaves;
    ground_concept_(seed, seedLeaves, 0);
    if (seedLeaves.empty()) return {};

    // Rank the OTHER abstractions by grounded cross-affinity to the seed. Each
    // abstraction's meaning is its grounded corpus-word leaves; kinship is how
    // strongly those leaf sets associate in the Plexus.
    struct AK { double aff; std::size_t idx; };
    std::vector<AK> kin;
    std::vector<std::unordered_set<std::string>> leavesOf(abstractions_.size());
    for (std::size_t i = 0; i < abstractions_.size(); ++i) {
        if (abstractions_[i].name == seed) continue;
        ground_concept_(abstractions_[i].name, leavesOf[i], 0);
        const double aff = leafset_affinity_(seedLeaves, leavesOf[i]);
        if (aff > 0.0) kin.push_back({ aff, i });
    }
    if (kin.empty()) return {};
    std::sort(kin.begin(), kin.end(), [](const AK& x, const AK& y) { return x.aff > y.aff; });

    std::vector<Glyph> parts{ seedA->glyph };
    std::vector<std::string> members{ seed };
    std::vector<const std::unordered_set<std::string>*> memberLeaves{ &seedLeaves };
    int maxlvl = seedA->level;
    for (const auto& kk : kin) {
        if (members.size() >= k + 1) break;
        const Abstraction& a = abstractions_[kk.idx];
        parts.push_back(a.glyph);
        members.push_back(a.name);
        memberLeaves.push_back(&leavesOf[kk.idx]);
        maxlvl = std::max(maxlvl, a.level);
    }
    if (members.size() < 2) return {};

    // Coherence: mean pairwise grounded cross-affinity among members, squashed
    // into [0,1] by the same scale as the word-level path, so one bar governs
    // the whole tower.
    double aff_sum = 0.0; std::size_t pairs = 0;
    for (std::size_t i = 0; i < memberLeaves.size(); ++i)
        for (std::size_t j = i + 1; j < memberLeaves.size(); ++j) {
            aff_sum += leafset_affinity_(*memberLeaves[i], *memberLeaves[j]);
            ++pairs;
        }
    const double mean_aff = pairs ? aff_sum / static_cast<double>(pairs) : 0.0;
    const double coh = mean_aff / (mean_aff + kPmiCoherenceScale);
    if (coh < min_coherence) return {};

    Abstraction a;
    a.glyph     = bundle(std::span<const Glyph>{parts.data(), parts.size()});
    a.level     = maxlvl + 1;
    a.coherence = coh;
    a.members   = members;
    a.name = "{";
    const std::size_t mn = std::min<std::size_t>(3, members.size());
    for (std::size_t i = 0; i < mn; ++i) { if (i) a.name += "+"; a.name += members[i]; }
    if (members.size() > mn) a.name += "+..";
    a.name += "}#" + std::to_string(abstraction_seq_++);
    abstractions_.push_back(std::move(a));
    return abstractions_.back().name;
}

std::string Cogitator::form_abstraction(const std::string& seed, std::size_t k, double min_coherence) {
    // Hub-proof path: when the Plexus knows this seed word, it is AUTHORITATIVE —
    // members are its PMI kin and coherence is mutual information. A refusal here
    // is a true refusal; we do NOT fall back to the looser Hamming field (which
    // would readmit the very hub-fouled clusters the bar exists to reject).
    if (plexus_) {
        if (plexus_->has(seed))
            return form_abstraction_plexus_(seed, k, min_coherence);
        // A known abstraction seed has no Plexus node — rise the tower coherently
        // through its grounded member leaves (also authoritative, no c0 fallback).
        for (const auto& a : abstractions_)
            if (a.name == seed)
                return form_abstraction_over_abstractions_(seed, k, min_coherence);
    }

    ensure_field_();
    int seed_level = 0;
    const Glyph seedG = concept_glyph_any_(seed, seed_level);
    if (seedG.popcount() == 0) return {};

    // Candidate kin: nearest learned words (the field) AND existing
    // abstractions — so a TOWER can form, not just flat word-chunks.
    struct Kin { double sim; std::string name; int level; };
    std::vector<Kin> kin;
    for (const auto& m : field_.query(seedG, k + 4)) {
        if (m.label == seed) continue;
        if (!m.label.empty() && m.label[0] == '{') continue;  // words here; abstractions added below
        kin.push_back({ m.similarity, m.label, 0 });
    }
    for (const auto& a : abstractions_) {
        if (a.name == seed) continue;
        kin.push_back({ seedG.similarity(a.glyph), a.name, a.level });
    }
    std::sort(kin.begin(), kin.end(), [](const Kin& a, const Kin& b) { return a.sim > b.sim; });

    std::vector<Glyph> parts{ seedG };
    std::vector<std::string> members{ seed };
    int maxlvl = seed_level;
    for (const auto& c : kin) {
        if (members.size() >= k + 1) break;
        int l2 = 0;
        const Glyph g = concept_glyph_any_(c.name, l2);
        if (g.popcount() == 0) continue;
        parts.push_back(g);
        members.push_back(c.name);
        maxlvl = std::max(maxlvl, l2);
    }
    if (members.size() < 2) return {};

    // Coherence: mean pairwise similarity of the cluster. A loose grab-bag
    // scores low; a genuine unification scores high. Khora refuses weak ones.
    double coh = 0.0; std::size_t pairs = 0;
    for (std::size_t i = 0; i < parts.size(); ++i)
        for (std::size_t j = i + 1; j < parts.size(); ++j) { coh += parts[i].similarity(parts[j]); ++pairs; }
    coh = pairs ? coh / static_cast<double>(pairs) : 0.0;
    if (coh < min_coherence) return {};

    Abstraction a;
    a.glyph     = bundle(std::span<const Glyph>{parts.data(), parts.size()});
    a.level     = maxlvl + 1;
    a.coherence = coh;
    a.members   = members;
    a.name = "{";
    const std::size_t mn = std::min<std::size_t>(3, members.size());
    for (std::size_t i = 0; i < mn; ++i) { if (i) a.name += "+"; a.name += members[i]; }
    if (members.size() > mn) a.name += "+..";
    a.name += "}#" + std::to_string(abstraction_seq_++);
    abstractions_.push_back(std::move(a));
    return abstractions_.back().name;
}

int Cogitator::abstraction_depth() const noexcept {
    int d = 0;
    for (const auto& a : abstractions_) d = std::max(d, a.level);
    return d;
}

std::vector<std::string> Cogitator::abstraction_names(std::size_t n) const {
    std::vector<std::string> out;
    for (auto it = abstractions_.rbegin(); it != abstractions_.rend() && out.size() < n; ++it)
        out.push_back("L" + std::to_string(it->level)
                      + " c" + std::to_string(static_cast<int>(it->coherence * 100 + 0.5))
                      + "  " + it->name);
    return out;
}

std::string Cogitator::abstraction_seed(std::uint64_t n) const {
    // Every 4th step, rise: abstract over an existing abstraction.
    if (!abstractions_.empty() && (n % 4) == 3)
        return abstractions_[static_cast<std::size_t>(n) % abstractions_.size()].name;
    const auto top = top_attractors(8);
    if (!top.empty()) return top[static_cast<std::size_t>(n) % top.size()].first;
    if (!abstractions_.empty()) return abstractions_.back().name;
    return {};
}

void Cogitator::save_abstractions(const std::filesystem::path& path) const {
    namespace fs = std::filesystem;
    if (path.has_parent_path()) fs::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::trunc);
    if (!f) return;
    // level<TAB>name<TAB>member|member|...<TAB>coherence — glyphs re-derived on
    // load; coherence persisted so the tower's measured cohesion survives restarts
    // (an honest spire across lives, not a reset-to-zero display).
    for (const auto& a : abstractions_) {
        f << a.level << '\t' << a.name << '\t';
        for (std::size_t i = 0; i < a.members.size(); ++i) { if (i) f << '|'; f << a.members[i]; }
        f << '\t' << a.coherence << '\n';
    }
}

void Cogitator::load_abstractions(const std::filesystem::path& path) {
    std::ifstream f(path);
    if (!f) return;
    std::string line;
    std::vector<Abstraction> loaded;
    while (std::getline(f, line)) {
        const auto t1 = line.find('\t');
        const auto t2 = (t1 == std::string::npos) ? std::string::npos : line.find('\t', t1 + 1);
        if (t1 == std::string::npos || t2 == std::string::npos) continue;
        Abstraction a;
        a.level = std::atoi(line.substr(0, t1).c_str());
        a.name  = line.substr(t1 + 1, t2 - t1 - 1);
        // Optional 4th field (coherence); old 3-field lines load as coherence 0.
        const auto t3 = line.find('\t', t2 + 1);
        std::string mem = (t3 == std::string::npos) ? line.substr(t2 + 1)
                                                    : line.substr(t2 + 1, t3 - t2 - 1);
        a.coherence = (t3 == std::string::npos) ? 0.0
                                                : std::atof(line.substr(t3 + 1).c_str());
        std::size_t p = 0, q;
        while ((q = mem.find('|', p)) != std::string::npos) { a.members.push_back(mem.substr(p, q - p)); p = q + 1; }
        if (p < mem.size()) a.members.push_back(mem.substr(p));
        loaded.push_back(std::move(a));
    }
    // Re-derive glyphs in level order, so abstractions can reference lower ones.
    std::stable_sort(loaded.begin(), loaded.end(),
                     [](const Abstraction& a, const Abstraction& b) { return a.level < b.level; });
    abstractions_.clear();
    for (auto& a : loaded) {
        std::vector<Glyph> parts;
        for (const auto& m : a.members) {
            int lvl = 0;
            const Glyph g = concept_glyph_any_(m, lvl);   // sees already-loaded lowers
            if (g.popcount() > 0) parts.push_back(g);
        }
        if (parts.empty()) continue;
        a.glyph = bundle(std::span<const Glyph>{parts.data(), parts.size()});
        abstractions_.push_back(std::move(a));
        ++abstraction_seq_;
    }
}

std::string Cogitator::focused_seed(std::uint64_t n) {
    if (attractors_.empty()) return wandering_seed(n);
    const auto top = top_attractors(8);
    if (top.empty()) return wandering_seed(n);
    return top[n % top.size()].first;
}

double Cogitator::plexus_steer_(const std::string& w,
                                const std::vector<std::string>& targets) const {
    if (!plexus_ || targets.empty() || !plexus_->has(w)) return 0.0;
    double best = 0.0;
    for (const auto& t : targets) {
        const double a = plexus_->affinity(w, t);   // PMI, >= 0
        if (a > best) best = a;
    }
    return best / (best + 3.0);   // squash to [0,1): strongest topic link
}

std::string Cogitator::generate_(std::vector<Glyph> ctx, const Glyph& target,
                                 const std::vector<std::string>& steer_words,
                                 std::size_t n, double steer) {
    if (n == 0) return {};
    // Decoder over the full glyph_for field (any generated token can resolve).
    maelstrom::Resonator dec(256);
    dec.build(lex_.semantic_field());
    if (ctx.size() > 8) ctx.erase(ctx.begin(), ctx.end() - 8);
    const bool use_plexus = (plexus_ != nullptr && !steer_words.empty());

    std::string out, last;
    std::deque<std::string> recent;   // anti-repetition window (breaks loops)
    for (std::size_t s = 0; s < n; ++s) {
        const auto cands = cortex_.predict_candidates(ctx, 6);
        if (cands.empty()) break;
        double best = -1e9; std::string word; Glyph wg; int rank = 0;
        for (const auto& cg : cands) {
            const auto d = dec.query(cg, 1);
            ++rank;
            if (d.empty()) continue;
            const std::string w = d.front().label;
            if (w == last) continue;   // never an immediate repeat
            // Hard-skip words in the recent window so generation cannot collapse
            // into "what what what" loops — better to end the thought than spin.
            if (std::find(recent.begin(), recent.end(), w) != recent.end()) continue;
            const Glyph wgl = lex_.glyph_for(w);
            // Grammatical fluency from the cortex rank; topic pull from the
            // Plexus (hub-proof) — boosting on-topic content words without
            // penalising the function words the cortex ranks for grammar. Falls
            // back to glyph similarity only when the Plexus can't steer.
            const double sem = use_plexus ? plexus_steer_(w, steer_words)
                                          : wgl.similarity(target);
            const double score = (1.0 - 0.12 * (rank - 1)) + steer * sem;
            if (score > best) { best = score; word = w; wg = wgl; }
        }
        if (word.empty()) break;
        if (!out.empty()) out += ' ';
        out += word; last = word;
        recent.push_back(word);
        if (recent.size() > 5) recent.pop_front();
        ctx.push_back(wg);
        if (ctx.size() > 8) ctx.erase(ctx.begin());
    }
    return out;
}

std::string Cogitator::utter(const std::string& topic, std::size_t n) {
    ensure_field_();
    const Glyph topicG = lex_.glyph_for(topic);
    return generate_({ topicG }, topicG, { topic }, n);
}

std::string Cogitator::respond(const std::string& question, std::size_t n) {
    ensure_field_();
    // Seed the cortex with the WHOLE question phrase (so different questions
    // start from different contexts, not one weak content word), and steer
    // toward the question's content concepts — its knowledge neighbourhood,
    // pulled by the Plexus so the answer stays on-topic without hub-drift.
    std::vector<Glyph> seed, concepts;
    std::vector<std::string> steer_words;
    for (const auto& t : khora::lexicon::tokenize(question)) {
        seed.push_back(lex_.glyph_for(t));                          // phrase context
        if (lex_.has(t) && is_content_(t)) {
            concepts.push_back(lex_.glyph_for(t));
            steer_words.push_back(t);
        }
    }
    if (concepts.empty())  // fall back to any learned tokens for the target
        for (const auto& t : khora::lexicon::tokenize(question))
            if (lex_.has(t)) { concepts.push_back(lex_.glyph_for(t)); steer_words.push_back(t); }
    if (seed.empty()) return {};
    const Glyph target = concepts.empty()
        ? seed.back()
        : bundle(std::span<const Glyph>{concepts.data(), concepts.size()});
    return generate_(std::move(seed), target, steer_words, n, 1.0);
}

Synthesis Cogitator::synthesize(const std::string& a_in, const std::string& b_in, std::uint64_t seed) {
    ensure_field_();
    Synthesis s;
    std::string a = a_in, b = b_in;

    // Unspecified parents -> Khora picks distant concepts itself (pure chaos:
    // the more distant the collision, the more entropy to turn into beauty).
    if ((a.empty() || b.empty()) && !concepts_.empty()) {
        if (a.empty()) a = wandering_seed(seed);
        if (b.empty()) {
            const Glyph ga0 = token_glyph_(a);
            double worst = 2.0;
            std::string far;
            for (int t = 0; t < 16; ++t) {
                const std::string cand = wandering_seed(seed * 2654435761ull + static_cast<std::uint64_t>(t) * 7 + 1);
                if (cand.empty() || cand == a) continue;
                const double sim = ga0.similarity(token_glyph_(cand));
                if (sim < worst) { worst = sim; far = cand; }   // most distant wins
            }
            b = far.empty() ? wandering_seed(seed + 1) : far;
        }
    }
    s.a = a; s.b = b;
    if (a.empty() || b.empty()) return s;

    const Glyph ga = token_glyph_(a);
    const Glyph gb = token_glyph_(b);
    if (ga.popcount() == 0 || gb.popcount() == 0) return s;
    s.tension = 1.0 - ga.similarity(gb);

    // Plexus-routed collision: the idea forged from colliding a and b is the
    // concept that BRIDGES them — strongly associated with BOTH parents, yet
    // neither. Meaningful chaos: the hidden third their tension reveals, instead
    // of the dense-chimera Hamming drift into function-word hubs. When the Plexus
    // knows both parents it is authoritative: a real bridge, or an honest nothing
    // (distant concepts that share no conceptual link forge nothing) — never a hub.
    if (plexus_ && plexus_->has(a) && plexus_->has(b)) {
        // Gather the union of both parents' kin, with each candidate's affinity to
        // each parent. A TRUE bridge (linked to both) is the richest emergent; if
        // none exists, the strongest combined pull still names a real concept the
        // collision evokes — so chaos forges something meaningful, never a hub and
        // never (for two known content words) pure nothing.
        std::unordered_map<std::string, std::pair<double, double>> cand;  // c -> (affA, affB)
        for (const auto& kin : plexus_->associates(a, 60)) {
            if (kin.first != a && kin.first != b) cand[kin.first].first = kin.second;
        }
        for (const auto& kin : plexus_->associates(b, 60)) {
            if (kin.first != a && kin.first != b) cand[kin.first].second = kin.second;
        }
        struct Em { bool both; double score; std::string label; };
        std::vector<Em> ranked;
        ranked.reserve(cand.size());
        for (auto& [c, af] : cand) {
            double aA = af.first, aB = af.second;
            if (aA == 0.0) aA = plexus_->affinity(a, c);   // fill the cross link
            if (aB == 0.0) aB = plexus_->affinity(b, c);
            const bool both = (aA > 0.0 && aB > 0.0);
            const double score = both ? std::min(aA, aB) : (aA + aB);  // bridge = weaker link
            ranked.push_back({ both, score, c });
        }
        // True bridges first, then strongest pull.
        std::sort(ranked.begin(), ranked.end(), [](const Em& x, const Em& y) {
            if (x.both != y.both) return x.both;
            return x.score > y.score;
        });
        for (std::size_t j = 0; j < ranked.size() && s.emergent.size() < 4; ++j) {
            LatticeMatch m;
            m.label      = ranked[j].label;
            m.hamming    = ranked[j].both ? 0u : 1u;   // mark true bridges (hamming 0)
            m.similarity = ranked[j].score / (ranked[j].score + 2.5);  // squash to 0..1
            s.emergent.push_back(std::move(m));
        }
        if (!s.emergent.empty()) note_attractor_(s.emergent.front().label);
        return s;  // authoritative — real concepts, never Hamming hub-drift
    }

    // Superpose the two into a chimera, then see what concept that collision
    // evokes that is NEITHER parent — the idea forged from their tension. Used
    // for parents the Plexus has not learned.
    const Glyph chimera = bundle({ga, gb});
    auto hits = (field_.size() > 0) ? field_.query(chimera, 8) : memory_.query(chimera, 8);
    for (auto& h : hits) {
        if (h.label == a || h.label == b) continue;
        s.emergent.push_back(h);
        if (s.emergent.size() >= 4) break;
    }
    // What chaos forges is itself a place thought has landed.
    if (!s.emergent.empty()) note_attractor_(s.emergent.front().label);
    return s;
}

void Cogitator::note_attractor_(const std::string& label) {
    if (label.empty()) return;
    // Skip Khora's own provisional trace concepts — only real, learned
    // concepts count as preoccupations.
    if (label.rfind("deliberation_", 0) == 0 || label.rfind("hypothesis_", 0) == 0) return;
    ++attractors_[label];
}

void Cogitator::save_attractors(const std::filesystem::path& path) const {
    namespace fs = std::filesystem;
    if (path.has_parent_path()) fs::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::trunc);
    if (!f) return;
    for (const auto& [name, count] : attractors_)
        if (!name.empty()) f << count << ' ' << name << '\n';
}

void Cogitator::load_attractors(const std::filesystem::path& path) {
    std::ifstream f(path);
    if (!f) return;
    std::uint32_t count = 0;
    std::string   name;
    while (f >> count >> name)
        if (!name.empty()) attractors_[name] += count;
}

std::vector<std::pair<std::string, std::uint32_t>> Cogitator::top_attractors(std::size_t n) const {
    std::vector<std::pair<std::string, std::uint32_t>> v(attractors_.begin(), attractors_.end());
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });
    if (v.size() > n) v.resize(n);
    return v;
}

Thought Cogitator::think(std::string_view stimulus) {
    Thought t;
    t.stimulus = std::string(stimulus);
    ++thoughts_;
    ensure_field_();

    // ENCODE
    t.tokens = khora::lexicon::tokenize(stimulus);
    t.probe  = encode_(t.tokens);

    // The resolve loop. Each pass either resolves the thought or learns
    // something and tries again. Failure is fuel, never a terminus.
    for (std::size_t attempt = 0; attempt < max_attempts_; ++attempt) {
        ++t.attempts;
        ++total_attempts_;

        // RESONATE
        t.resonances.clear();
        if (t.probe.popcount() > 0) {
            t.resonances = resonate_(t.probe, resonance_k_);
        }
        t.confidence = t.resonances.empty() ? 0.0 : t.resonances.front().similarity;
        t.gestalt    = gestalt_(t.probe, t.resonances);

        // PROJECT — every thought is also experience for the cortex.
        if (learn_from_thoughts_ && t.gestalt.popcount() > 0) {
            cortex_.step(t.gestalt);
        }
        t.projection = cortex_.predict();

        // Did it resolve?
        if (t.confidence >= novelty_threshold_ && !t.resonances.empty()) {
            t.novel        = false;
            t.chosen_label = t.resonances.front().label;
            break;
        }

        // --- NOVELTY: failure becomes the trigger to learn. ---
        // a. Curiosity spikes — the drive that turns not-knowing into seeking.
        soma_.stimulate(Drive::Curiosity, +0.15);

        // b. DECOMPOSE: resonate each token alone, gather the best fragments
        //    Khora *does* partially recognise, even when the whole is alien.
        std::vector<Glyph> fragments;
        fragments.reserve(t.tokens.size() + 2);
        if (t.probe.popcount() > 0) fragments.push_back(t.probe);
        for (const auto& tok : t.tokens) {
            const Glyph tg = lex_.glyph_for(tok);
            const auto fm = resonate_(tg, 1);
            if (!fm.empty() && fm.front().similarity > novelty_threshold_ * 0.5) {
                const Glyph g = recall_(fm.front().label);
                if (g.popcount() > 0) fragments.push_back(g);
            }
            fragments.push_back(tg);
        }
        // Fold in the cortex's forward projection as an imaginative ingredient.
        if (t.projection.popcount() > 0) fragments.push_back(t.projection);

        // c. SYNTHESIZE a hypothesis from the partial knowledge.
        t.hypothesis = fragments.empty()
            ? t.probe
            : bundle(std::span<const Glyph>{fragments.data(), fragments.size()});

        // d. CONSOLIDATE: the hypothesis becomes a provisional memory and
        //    cortex experience. Khora now knows more than it did a moment ago.
        if (consolidate_hypotheses_ && t.hypothesis.popcount() > 0) {
            const std::string label = "hypothesis_" + std::to_string(hypothesis_seq_++);
            memory_.store(label, t.hypothesis);
            cortex_.step(t.hypothesis);
            ++hypotheses_formed_;
            t.learned_this_cycle = true;
        }

        // e. Enrich the probe toward the hypothesis so the next RESONATE
        //    pass searches from a more-informed position. (Failure changed us.)
        if (t.hypothesis.popcount() > 0) {
            t.probe = bundle({t.probe, t.hypothesis});
        }
        // loop: re-attempt
    }

    if (t.confidence < novelty_threshold_) {
        t.novel = true;
        ++novel_count_;
    }

    // EVALUATE — the Soma judges the whole act through its drive lattice.
    Affinity a{};
    a.per_drive[static_cast<std::size_t>(Drive::Curiosity)]        = t.novel ? 1.0 : 0.3;
    a.per_drive[static_cast<std::size_t>(Drive::Mastery)]          = t.confidence;
    a.per_drive[static_cast<std::size_t>(Drive::Preservation)]     = -0.05 * static_cast<double>(t.attempts);
    a.per_drive[static_cast<std::size_t>(Drive::Efficiency)]       = -0.05 * static_cast<double>(t.attempts);
    a.per_drive[static_cast<std::size_t>(Drive::OperatorAffinity)] = 1.0;
    t.valence = soma_.evaluate(a);

    return t;
}

// ----------------------- non-linear cognition: the Prism -----------------------

const char* lens_name(Lens l) noexcept {
    switch (l) {
        case Lens::Holistic:    return "holistic";
        case Lens::Leading:     return "leading";
        case Lens::Trailing:    return "trailing";
        case Lens::Broad:       return "broad";
        case Lens::Focused:     return "focused";
        case Lens::Curious:     return "curious";
        case Lens::Associative: return "associative";
        case Lens::Chaotic:     return "chaotic";
        default:                return "?";
    }
}

// The breadth each lens resonates with.
static std::size_t lens_k(Lens lens, std::size_t base) {
    if (lens == Lens::Broad)   return std::max<std::size_t>(base, 8);
    if (lens == Lens::Focused) return 1;
    if (lens == Lens::Curious) return std::max<std::size_t>(base, 4);
    return base;
}

// Build the lens-shaped probe — different facets literally look at
// different aspects of the stimulus. No resonance here; that is batched.
Glyph Cogitator::facet_probe_(const std::vector<std::string>& tokens, Lens lens,
                              std::uint64_t entropy_seed) const {
    std::vector<Glyph> parts;
    parts.reserve(tokens.size());
    const std::size_t n = tokens.size();
    switch (lens) {
        case Lens::Leading: {
            const std::size_t half = (n + 1) / 2;
            for (std::size_t i = 0; i < half; ++i)
                if (is_content_(tokens[i])) parts.push_back(token_glyph_(tokens[i]));
            break;
        }
        case Lens::Trailing: {
            const std::size_t start = n / 2;
            for (std::size_t i = start; i < n; ++i)
                if (is_content_(tokens[i])) parts.push_back(token_glyph_(tokens[i]));
            break;
        }
        default:
            for (const auto& tok : tokens)
                if (is_content_(tok)) parts.push_back(token_glyph_(tok));
            break;
    }
    // If the lens's slice held only function words, fall back to its tokens.
    if (parts.empty()) {
        if (lens == Lens::Leading) {
            const std::size_t half = (n + 1) / 2;
            for (std::size_t i = 0; i < half; ++i) parts.push_back(token_glyph_(tokens[i]));
        } else if (lens == Lens::Trailing) {
            for (std::size_t i = n / 2; i < n; ++i) parts.push_back(token_glyph_(tokens[i]));
        } else {
            for (const auto& tok : tokens) parts.push_back(token_glyph_(tok));
        }
    }
    Glyph probe = parts.empty() ? Glyph::zero()
                                : bundle(std::span<const Glyph>{parts.data(), parts.size()});

    // The chaotic lens injects entropy — it explores a perturbed nearby
    // region of the manifold, the engine's way of courting the unexpected.
    if (lens == Lens::Chaotic && probe.popcount() > 0) {
        std::uint64_t s = entropy_seed;
        const std::size_t flips = khora::lattice::kGlyphBits / 50;  // ~2% perturbation
        for (std::size_t i = 0; i < flips; ++i) {
            s = s * 6364136223846793005ULL + 1442695040888963407ULL;
            probe.flip_bit(static_cast<std::size_t>(s % khora::lattice::kGlyphBits));
        }
    }

    // The associative lens follows the cortex's forward projection instead
    // of the literal stimulus — thinking about what tends to come next.
    if (lens == Lens::Associative) {
        const Glyph proj = cortex_.predict();
        if (proj.popcount() > 0)
            return (probe.popcount() > 0) ? bundle({probe, proj}) : proj;
    }
    return probe;
}

// Finish a facet given its already-resolved resonances (computed in one
// batched dispatch). Pure post-processing — runs concurrently across
// facets with no device contention.
Facet Cogitator::finish_facet_(Lens lens, const Glyph& query_probe,
                               std::vector<LatticeMatch> resonances) const {
    Facet f;
    f.lens       = lens;
    f.probe      = query_probe;
    f.resonances = std::move(resonances);

    // Choose this facet's candidate.
    std::size_t pick = 0;
    if (lens == Lens::Curious && f.resonances.size() > 1) {
        // Deliberately chase a non-obvious alternative (second-best) — the
        // facet that questions the obvious answer.
        pick = 1;
    }
    if (!f.resonances.empty()) {
        f.confidence = f.resonances.front().similarity;       // confidence = best available
        const auto& chosen = f.resonances[std::min(pick, f.resonances.size() - 1)];
        f.label     = chosen.label;
        f.candidate = recall_(chosen.label);
        if (f.candidate.popcount() == 0) f.candidate = query_probe;
    } else {
        f.candidate  = query_probe;
        f.confidence = 0.0;
    }
    f.novel = f.confidence < novelty_threshold_;

    // Score this facet through the drives — each lens flatters a different
    // drive, so the Soma's current mood tilts the contest.
    Affinity a{};
    a.per_drive[static_cast<std::size_t>(Drive::Curiosity)] =
        (lens == Lens::Curious || lens == Lens::Chaotic) ? 1.0 : (f.novel ? 0.6 : 0.2);
    a.per_drive[static_cast<std::size_t>(Drive::Mastery)]          = f.confidence;
    a.per_drive[static_cast<std::size_t>(Drive::Efficiency)] =
        (lens == Lens::Focused) ? 0.5 : (lens == Lens::Broad ? -0.2 : 0.0);
    a.per_drive[static_cast<std::size_t>(Drive::OperatorAffinity)] = 1.0;
    f.valence = soma_.evaluate(a);
    return f;
}

Deliberation Cogitator::deliberate(std::string_view stimulus, std::size_t facets) {
    Deliberation d;
    d.stimulus = std::string(stimulus);
    d.tokens   = khora::lexicon::tokenize(stimulus);
    ++deliberations_;

    const std::size_t n_lenses = std::min<std::size_t>(
        facets, static_cast<std::size_t>(Lens::_Count));
    if (n_lenses == 0) return d;
    ensure_field_();

    // 1. Build every lens's probe (serial, cheap). Each facet looks at a
    //    different aspect of the stimulus.
    std::vector<Glyph>       probes(n_lenses);
    std::vector<std::size_t> ks(n_lenses);
    for (std::size_t i = 0; i < n_lenses; ++i) {
        const Lens lens = static_cast<Lens>(i);
        const std::uint64_t seed =
            0xC0FFEEULL ^ (static_cast<std::uint64_t>(deliberations_) << 20) ^ (i * 0x9E3779B9ULL);
        probes[i] = facet_probe_(d.tokens, lens, seed);
        ks[i]     = lens_k(lens, resonance_k_);
    }

    // 2. Resonate ALL facets in a single batched dispatch — one GPU call,
    //    no device contention, instead of eight concurrent ones. Each facet
    //    then keeps its own lens-specific breadth from the shared result.
    std::size_t kmax = 1;
    for (auto k : ks) kmax = std::max(kmax, k);
    const auto batched = resonate_batch_(probes, kmax);

    // 3. Finish the facets CONCURRENTLY — pure post-processing (candidate
    //    choice, drive valence), read-only and device-free, so the chorus
    //    still thinks at once.
    std::vector<std::future<Facet>> futures;
    futures.reserve(n_lenses);
    for (std::size_t i = 0; i < n_lenses; ++i) {
        const Lens lens = static_cast<Lens>(i);
        std::vector<LatticeMatch> res =
            (i < batched.size()) ? batched[i] : std::vector<LatticeMatch>{};
        if (res.size() > ks[i]) res.resize(ks[i]);   // lens-specific breadth
        futures.push_back(std::async(std::launch::async,
            [this, lens, p = probes[i], r = std::move(res)]() mutable {
                return finish_facet_(lens, p, std::move(r));
            }));
    }
    d.facets.reserve(n_lenses);
    for (auto& fut : futures) d.facets.push_back(fut.get());

    // Arbitrate: the facet with the highest drive-weighted valence wins.
    d.winner = 0;
    for (std::size_t i = 1; i < d.facets.size(); ++i) {
        if (d.facets[i].valence > d.facets[d.winner].valence) d.winner = static_cast<int>(i);
    }

    // Coherence: mean pairwise similarity of the facet candidates — how
    // much the chorus agrees. Entropy: spread of valences — the chaos in
    // the contest.
    {
        double sim_sum = 0.0; std::size_t pairs = 0;
        for (std::size_t i = 0; i < d.facets.size(); ++i)
            for (std::size_t j = i + 1; j < d.facets.size(); ++j) {
                sim_sum += d.facets[i].candidate.similarity(d.facets[j].candidate);
                ++pairs;
            }
        d.coherence = pairs ? sim_sum / static_cast<double>(pairs) : 1.0;

        double mean_v = 0.0;
        for (const auto& f : d.facets) mean_v += f.valence;
        mean_v /= static_cast<double>(d.facets.size());
        double var = 0.0;
        for (const auto& f : d.facets) var += (f.valence - mean_v) * (f.valence - mean_v);
        d.entropy = std::sqrt(var / static_cast<double>(d.facets.size()));
    }

    // Collapse: bundle the coalition of facets that AGREE with the winner
    // (candidate similarity above threshold). Concord reinforces; dissent
    // is dropped. Meaning emerges from the coherent whole.
    const Glyph& win_cand = d.facets[d.winner].candidate;
    std::vector<Glyph> coalition;
    for (const auto& f : d.facets) {
        if (f.candidate.popcount() == 0) continue;
        if (&f == &d.facets[d.winner] || f.candidate.similarity(win_cand) > 0.3)
            coalition.push_back(f.candidate);
    }
    d.collapsed = coalition.empty() ? win_cand
        : bundle(std::span<const Glyph>{coalition.data(), coalition.size()});
    d.chosen_label = d.facets[d.winner].novel ? std::string{} : d.facets[d.winner].label;
    note_attractor_(d.chosen_label);

    // Consolidate the collapsed thought into memory + cortex (serial; the
    // exploration was read-only, the learning happens once, here).
    if (consolidate_hypotheses_ && d.collapsed.popcount() > 0) {
        const std::string label = "deliberation_" + std::to_string(hypothesis_seq_++);
        memory_.store(label, d.collapsed);
        if (learn_from_thoughts_) cortex_.step(d.collapsed);
        d.learned = true;
    }
    return d;
}

namespace {
bool is_trace(const std::string& label) {
    return label.rfind("deliberation_", 0) == 0 || label.rfind("hypothesis_", 0) == 0;
}

// The concept a deliberation lands on — the strongest real resonance
// across all facets, skipping transient trace concepts and any concept in
// `exclude`. Excluding the whole visited set forces the train onward into
// fresh territory rather than collapsing onto a central hub in two hops.
std::string landed_concept(const Deliberation& d,
                           const std::vector<std::string>& exclude) {
    double best = -2.0; std::string best_label;
    for (const auto& f : d.facets) {
        for (const auto& m : f.resonances) {
            if (is_trace(m.label)) continue;
            if (std::find(exclude.begin(), exclude.end(), m.label) != exclude.end()) continue;
            if (m.similarity > best) { best = m.similarity; best_label = m.label; }
        }
    }
    return best_label;
}
} // namespace

Rumination Cogitator::ruminate(std::string_view stimulus, std::size_t max_depth) {
    Rumination r;
    r.seed = std::string(stimulus);
    std::string current = r.seed;
    std::vector<std::string> visited;
    visited.push_back(current);      // don't loop back onto the seed

    r.train.push_back(current);      // the seed is the first stop
    for (std::size_t depth = 0; depth < max_depth; ++depth) {
        Deliberation d = deliberate(current);

        // Coherent hop: walk the clean associative structure. Prefer a sharp
        // Plexus associate of the current concept the train hasn't visited, so
        // the chain MEANS something (justice -> injustice -> law -> government)
        // instead of drifting through resonance hubs. Fall back to the
        // deliberation's landed concept (which can also reach the abstraction
        // tower) when the Plexus offers no fresh kin.
        std::string fresh;
        bool plexus_knew = false;
        if (plexus_ && plexus_->has(current)) {
            plexus_knew = true;
            // Anchored coherent hop: walk the current concept's sharp kin in their
            // robust (confidence-weighted) order and take the highest-ranked one
            // that ALSO stays in the seed's conceptual field — turning free
            // association (justice -> chief -> a ship's mate) into focused
            // contemplation (justice -> distributive -> commutative). If none of
            // the kin link back to the seed, the best-ranked fresh kin carries the
            // thought onward. Selecting WITHIN the robust ranking keeps the
            // rare-word bias out.
            std::string top_fresh;
            for (const auto& kin : plexus_->associates(current, 12)) {
                if (kin.first == current) continue;
                if (std::find(visited.begin(), visited.end(), kin.first) != visited.end()) continue;
                if (top_fresh.empty()) top_fresh = kin.first;              // best rank, robust
                if (plexus_->affinity(r.seed, kin.first) > 0.0) { fresh = kin.first; break; }  // seed-anchored
            }
            if (fresh.empty()) fresh = top_fresh;
        }
        // If the Plexus knew this concept but offered no fresh kin, the coherent
        // thread is spent — let the train converge here rather than drift into
        // resonance hubs. Only concepts the Plexus does not know fall back to the
        // Hamming-resonance landing.
        if (fresh.empty() && !plexus_knew) fresh = landed_concept(d, visited);
        if (!fresh.empty()) {
            r.train.push_back(fresh);
            visited.push_back(fresh);
            current = fresh;
            r.chain.push_back(std::move(d));
            continue;
        }

        // No fresh concept to hop to. If the coherent Plexus thread is what is
        // spent, the thought has settled where it stands (a real concept).
        // Otherwise the recurring resonance pull — the strongest concept overall
        // — is the attractor the train keeps returning to.
        const std::string attractor = plexus_knew ? current : landed_concept(d, {});
        r.chain.push_back(std::move(d));
        if (!attractor.empty()) {
            r.converged  = true;
            r.conclusion = attractor;
        }
        break;
    }

    if (r.conclusion.empty() && !r.train.empty()) r.conclusion = r.train.back();
    note_attractor_(r.conclusion);
    return r;
}

std::vector<std::string> Cogitator::infer_path(const std::string& start,
                                               const std::string& goal,
                                               std::size_t max_depth) const {
    if (!plexus_ || !plexus_->has(start) || !plexus_->has(goal)) return {};
    if (start == goal) return { start };

    // Beam search over the Plexus, A*-style: each candidate path is scored by its
    // cumulative edge affinity (how coherent the chain is so far) PLUS a heuristic
    // pull toward the goal (the affinity of the frontier node to the goal). The
    // heuristic is what makes this REASONING toward an answer rather than the
    // aimless wandering of ruminate.
    struct Path { std::vector<std::string> nodes; double score; };
    constexpr std::size_t kBeam   = 24;   // paths kept per level
    constexpr std::size_t kExpand = 16;   // associates explored per frontier node
    const double          kGoalPull = 1.5;

    std::vector<Path> beam{ { { start }, 0.0 } };
    std::vector<std::string> best_partial{ start };
    double best_h = plexus_->affinity(start, goal);

    for (std::size_t d = 0; d < max_depth; ++d) {
        std::vector<Path> next;
        next.reserve(beam.size() * kExpand);
        for (const auto& p : beam) {
            const std::string& last = p.nodes.back();
            for (const auto& kv : plexus_->associates(last, kExpand)) {
                const std::string& kin = kv.first;
                if (std::find(p.nodes.begin(), p.nodes.end(), kin) != p.nodes.end())
                    continue;                                   // no cycles within a path
                Path np;
                np.nodes = p.nodes;
                np.nodes.push_back(kin);
                if (kin == goal) return np.nodes;               // reached — inference complete
                const double to_goal = plexus_->affinity(kin, goal);
                np.score = p.score + kv.second + kGoalPull * to_goal;
                if (to_goal > best_h) { best_h = to_goal; best_partial = np.nodes; }
                next.push_back(std::move(np));
            }
        }
        if (next.empty()) break;
        const std::size_t keep = std::min(kBeam, next.size());
        std::partial_sort(next.begin(), next.begin() + keep, next.end(),
                          [](const Path& a, const Path& b) { return a.score > b.score; });
        next.resize(keep);
        beam = std::move(next);
    }
    return best_partial;   // the closest reasoned approach when no full path is found
}

} // namespace khora::cogitator
