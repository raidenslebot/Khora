// SELF-SUPERVISION — CAN A REPRESENTATION LEARNED FROM UNLABELLED TEXT BEAT THE
// ONE THAT WAS HAND-DESIGNED?
//
// A capability audit found no self-supervised learning anywhere in this tree: no
// contrastive objective, no masked prediction, no pretext task, no augmentation.
// Every representation Khora has is either hand-written (the char-trigram glyph
// in khora::lexicon::encode_token) or trained on labels (descent, on Taxis tags).
// There are 8M tokens of unlabelled books in data/reservoir and nothing has ever
// learned a representation from them and then been asked to prove it.
//
// So: four pretext tasks over those books, and a downstream probe that none of
// them ever saw.
//
//   A. MASKED CONTEXT PREDICTION, through khora::descent. A word's spelling
//      (hashed char trigrams) into the checked-backprop MLP, asked which of the
//      256 commonest words stands near it in the corpus. The representation is
//      the hidden layer. Its ceiling is spelling: two words spelt the same get
//      the same vector, always. What it can do is RESHAPE spelling space toward
//      the distributional structure, and the hand-designed glyph is the exact
//      control for whether that reshaping is worth anything.
//
//   B. MASKED TOKEN PREDICTION, CBOW with negative sampling. Hide the token,
//      predict it from the mean of its neighbours' embeddings against five
//      sampled distractors. This is a true per-word embedding table, learned by
//      hand-written SGD (descent::Mlp cannot take a 40,000-way one-hot input at
//      this budget -- a dense forward pass over the vocabulary is ~100x the work
//      of the sparse update this objective actually needs).
//
//   C. CONTRASTIVE, NT-Xent over augmented views of the same window. Two views
//      of one 8-token window, pulled together; the other 63 windows in the batch
//      pushed apart. Augmentations: token dropout (p=0.35) and window shift
//      (+/-2). NOT shuffling -- the encoder is a mean over token embeddings, so
//      a permutation is the identity function and "shuffle" would be an
//      augmentation that augments nothing. Recording that rather than shipping
//      it as a third augmentation.
//
//   D. CONTEXT BUNDLING IN THE GLYPH ALGEBRA. Each word accumulates the sparse
//      ternary index vectors of its neighbours; the sign of the accumulator is
//      the glyph. This is the hyperdimensional form of the same
//      predict-from-context objective, and it is NOT new here --
//      khora::lexicon::Lexicon has done random indexing for a long time. What
//      has never happened is anyone measuring it against a downstream task. It
//      is reimplemented locally only because Lexicon holds a 40 KB accumulator
//      per word and would need 1.6 GB for this vocabulary.
//
// THE PROBE, which is the part that matters. A pretext loss going down proves
// nothing at all. Freeze each representation, hand a nearest-centroid classifier
// n labels per class, and ask it to name a word's WordNet category -- a label no
// pretext task saw. n runs 5, 20, 100, all, because the low-label regime is the
// entire justification for self-supervision.
//
// FOUR MANDATORY BASELINES, all reported whatever they say:
//   - chance (uniform over the classes) and the majority class;
//   - khora::lexicon::encode_token, the incumbent, under the identical probe;
//   - a random 10,000-bit glyph, so the incumbent's dimension is controlled;
//   - a random 64-dim vector AND the untrained MLP's hidden layer, so we can
//     see whether the probe alone, or a random projection of spelling alone,
//     is doing the work.
//
// AND: the probe is run at five points during pretraining, so the question "does
// the pretext loss track downstream accuracy" gets an answer instead of an
// assumption. Very often it does not.
//
// WHAT THIS HARNESS CANNOT SEE, stated before the numbers rather than after:
//   - WordNet's categories here are one level deep and badly incomplete. Words
//     in two chosen categories are DISCARDED, so polysemy is invisible and the
//     task is easier than the real one.
//   - Only corpus-frequent words are evaluated (freq >= 10). Rare-word
//     behaviour, which is where a spelling prior should win most, is unmeasured.
//   - The corpus is public-domain books. Whole WordNet categories (bird_genus,
//     animal_order) barely occur and are never selected, so the class inventory
//     is chosen BY the corpus.
//   - Nearest-centroid is a weak probe by construction. A stronger probe could
//     reorder the arms; that is a real limitation and not a hedge.
//   - The 10,000-bit arms and the 64-dim arms are not dimension-matched. Each
//     has its own random control, which brackets the effect but does not remove
//     it.

#include "khora/descent/descent.hpp"
#include "khora/lattice/glyph.hpp"
#include "khora/lexicon/lexicon.hpp"
#include "khora/reservoir/reservoir.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

using khora::lattice::Glyph;
using khora::lattice::kGlyphBits;

// --- knobs, all in one place so the runtime budget is auditable ---------------
constexpr std::size_t kDim          = 64;    // every learned representation
constexpr std::size_t kFeat         = 256;   // hashed spelling trigram buckets
constexpr std::size_t kCtxClasses   = 256;   // the 256 commonest words = MLP targets
constexpr std::size_t kWindow       = 4;     // +/- neighbours for context tasks
constexpr std::size_t kNegatives    = 5;
constexpr std::size_t kCbowEpochs   = 2;
constexpr std::size_t kNtxentBatch  = 64;
// 20,000 steps rather than 4,000. Not a tuning search -- the arms are compared
// on the same corpus and the contrastive objective touches far fewer embedding
// rows per unit of work than CBOW does, so the step count is raised to bring the
// compute within an order of magnitude. The residual imbalance is printed.
constexpr std::size_t kNtxentSteps  = 20000;
constexpr std::size_t kWindowLen    = 8;
constexpr double      kDropKeep     = 0.65;
constexpr double      kTau          = 0.10;
constexpr std::size_t kCheckpoints  = 4;     // probes taken DURING pretraining
constexpr std::size_t kMaxEvalWords = 3000;  // caps the 10,000-dim arms at ~120 MB
constexpr std::size_t kClasses      = 12;
constexpr std::size_t kDraws        = 8;     // label draws per few-shot setting

struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ULL) {}
    std::uint64_t next() {
        std::uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    double   unit()                { return static_cast<double>(next() >> 11) / 9007199254740992.0; }
    std::size_t below(std::size_t n) { return n ? static_cast<std::size_t>(next() % n) : 0; }
};

std::uint64_t fnv1a(std::string_view s) {
    std::uint64_t h = 0xCBF29CE484222325ULL;
    for (unsigned char c : s) { h ^= c; h *= 0x100000001B3ULL; }
    return h;
}

// 95% Wilson interval, in percent. Every rate in this file carries one, because
// a bare percentage over 1,200 test words invites reading two points of noise as
// a result.
std::pair<double, double> wilson(std::size_t hits, std::size_t n) {
    if (n == 0) return {0.0, 0.0};
    const double z = 1.96, ph = static_cast<double>(hits) / static_cast<double>(n);
    const double d = 1.0 + z * z / static_cast<double>(n);
    const double c = ph + z * z / (2.0 * static_cast<double>(n));
    const double m = z * std::sqrt(ph * (1.0 - ph) / static_cast<double>(n)
                                   + z * z / (4.0 * static_cast<double>(n) * static_cast<double>(n)));
    return {100.0 * (c - m) / d, 100.0 * (c + m) / d};
}

double pearson(const std::vector<double>& a, const std::vector<double>& b) {
    const std::size_t n = std::min(a.size(), b.size());
    if (n < 3) return 0.0;
    double ma = 0, mb = 0;
    for (std::size_t i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; }
    ma /= static_cast<double>(n); mb /= static_cast<double>(n);
    double sab = 0, saa = 0, sbb = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double da = a[i] - ma, db = b[i] - mb;
        sab += da * db; saa += da * da; sbb += db * db;
    }
    return (saa > 0 && sbb > 0) ? sab / std::sqrt(saa * sbb) : 0.0;
}

// ---------------------------------------------------------------------------
// THE CORPUS
// ---------------------------------------------------------------------------
struct Corpus {
    std::vector<std::string>                vocab;
    std::vector<std::uint32_t>              freq;
    std::unordered_map<std::string, int>    id;
    std::vector<std::vector<std::int32_t>>  sents;   // vocab ids, OOV dropped
    std::size_t books = 0, raw_tokens = 0, kept_tokens = 0, raw_types = 0;
};

Corpus load_corpus(const std::string& dir, std::size_t max_tokens, std::uint32_t min_freq) {
    Corpus c;
    khora::reservoir::Reservoir res(dir);
    res.load_catalog();

    // Intern every token to an id on the first pass. Holding 2.5M std::strings
    // costs ~80 MB and holding 2.5M int32 costs 10 MB; the frequency floor is
    // not known until the whole corpus is counted, so the ids are provisional
    // and get remapped below.
    std::unordered_map<std::string, std::int32_t> all_id;
    std::vector<std::uint32_t>                    all_freq;
    std::vector<std::vector<std::int32_t>>        raw;

    for (const auto& t : res.catalog()) {
        if (c.raw_tokens >= max_tokens) break;
        auto text = res.read(t.title);
        if (!text || text->size() < 40000) continue;
        ++c.books;
        for (auto& s : khora::lexicon::tokenize_sentences(*text)) {
            if (s.size() < 3) continue;
            std::vector<std::int32_t> ids;
            ids.reserve(s.size());
            for (auto& w : s) {
                auto it = all_id.find(w);
                std::int32_t k;
                if (it == all_id.end()) {
                    k = static_cast<std::int32_t>(all_freq.size());
                    all_id.emplace(w, k);
                    all_freq.push_back(0);
                } else {
                    k = it->second;
                }
                ++all_freq[static_cast<std::size_t>(k)];
                ids.push_back(k);
            }
            c.raw_tokens += ids.size();
            raw.push_back(std::move(ids));
            if (c.raw_tokens >= max_tokens) break;
        }
    }
    c.raw_types = all_freq.size();

    // Compact to the words above the floor, ordered by descending frequency so
    // that "the 256 commonest words" is just ids [0, 256).
    std::vector<std::int32_t> order;
    order.reserve(all_freq.size());
    for (std::size_t i = 0; i < all_freq.size(); ++i)
        if (all_freq[i] >= min_freq) order.push_back(static_cast<std::int32_t>(i));
    std::sort(order.begin(), order.end(),
              [&](std::int32_t a, std::int32_t b) { return all_freq[a] > all_freq[b]; });

    std::vector<std::int32_t> remap(all_freq.size(), -1);
    c.vocab.reserve(order.size());
    c.freq.reserve(order.size());
    // all_id is the only place the spelling still lives; invert it once.
    std::vector<const std::string*> spelling(all_freq.size(), nullptr);
    for (const auto& [w, k] : all_id) spelling[static_cast<std::size_t>(k)] = &w;
    for (std::size_t i = 0; i < order.size(); ++i) {
        remap[static_cast<std::size_t>(order[i])] = static_cast<std::int32_t>(i);
        c.vocab.push_back(*spelling[static_cast<std::size_t>(order[i])]);
        c.freq.push_back(all_freq[static_cast<std::size_t>(order[i])]);
        c.id.emplace(c.vocab.back(), static_cast<int>(i));
    }

    c.sents.reserve(raw.size());
    for (auto& s : raw) {
        std::vector<std::int32_t> out;
        out.reserve(s.size());
        for (std::int32_t k : s) {
            const std::int32_t m = remap[static_cast<std::size_t>(k)];
            if (m >= 0) out.push_back(m);
        }
        if (out.size() >= 3) { c.kept_tokens += out.size(); c.sents.push_back(std::move(out)); }
    }
    return c;
}

// ---------------------------------------------------------------------------
// THE DOWNSTREAM TASK: WordNet category membership
// ---------------------------------------------------------------------------
struct Task {
    std::vector<std::string>              classes;
    std::vector<std::string>              words;      // eval words
    std::vector<int>                      word_id;    // corpus vocab id
    std::vector<int>                      y;
    std::vector<std::size_t>              test;
    std::vector<std::vector<std::size_t>> train_by_class;
    std::size_t                           min_train = 0;
};

// A word is held out by a hash of its spelling, so the split is identical for
// every arm and cannot drift with corpus order.
bool is_test(const std::string& w) { return (fnv1a(w) % 5) < 2; }   // 40% held out

Task build_task(const Corpus& c, const std::string& wn_path, std::uint32_t eval_min_freq) {
    Task t;
    std::unordered_map<std::string, std::vector<std::string>> members;
    std::unordered_map<std::string, std::vector<std::string>> cats_of;
    {
        std::ifstream in(wn_path);
        std::string line;
        while (std::getline(in, line)) {
            const auto tab = line.find('\t');
            if (tab == std::string::npos) continue;
            const std::string name = line.substr(0, tab);
            std::istringstream ws(line.substr(tab + 1));
            std::string w;
            while (ws >> w) { members[name].push_back(w); cats_of[w].push_back(name); }
        }
    }
    if (members.empty()) return t;

    // Which categories does this corpus actually support? Counted, not chosen.
    std::vector<std::pair<std::size_t, std::string>> ranked;
    for (const auto& [name, ms] : members) {
        std::size_t n = 0;
        for (const auto& w : ms) {
            auto it = c.id.find(w);
            if (it != c.id.end() && c.freq[static_cast<std::size_t>(it->second)] >= eval_min_freq) ++n;
        }
        if (n >= 40) ranked.emplace_back(n, name);
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first > b.first;
        return a.second < b.second;
    });
    if (ranked.size() > kClasses) ranked.resize(kClasses);
    for (const auto& [n, name] : ranked) { (void)n; t.classes.push_back(name); }
    if (t.classes.size() < 3) { t.classes.clear(); return t; }

    std::unordered_map<std::string, int> chosen;
    for (std::size_t i = 0; i < t.classes.size(); ++i) chosen.emplace(t.classes[i], static_cast<int>(i));

    // Single membership only. A word in two chosen categories has no single
    // right answer under this probe, so it is dropped -- which makes the task
    // easier than the real one and is stated in the header.
    std::vector<std::vector<std::size_t>> per_class(t.classes.size());
    for (const auto& [w, cs] : cats_of) {
        int hit = -1; int n = 0;
        for (const auto& cn : cs) {
            auto it = chosen.find(cn);
            if (it != chosen.end()) { hit = it->second; ++n; }
        }
        if (n != 1) continue;
        auto vit = c.id.find(w);
        if (vit == c.id.end()) continue;
        if (c.freq[static_cast<std::size_t>(vit->second)] < eval_min_freq) continue;
        per_class[static_cast<std::size_t>(hit)].push_back(t.words.size());
        t.words.push_back(w);
        t.word_id.push_back(vit->second);
        t.y.push_back(hit);
    }

    // Drop classes too small to probe, then optionally subsample to keep the
    // 10,000-dim arms inside a sane memory footprint.
    std::vector<int> keep(t.classes.size(), -1);
    std::vector<std::string> kept_names;
    for (std::size_t i = 0; i < per_class.size(); ++i) {
        if (per_class[i].size() >= 30) { keep[i] = static_cast<int>(kept_names.size()); kept_names.push_back(t.classes[i]); }
    }
    std::size_t budget_per_class = kMaxEvalWords / std::max<std::size_t>(kept_names.size(), 1);
    Rng rng(0xC0FFEEULL);

    Task out;
    out.classes = kept_names;
    out.train_by_class.assign(kept_names.size(), {});
    for (std::size_t i = 0; i < per_class.size(); ++i) {
        if (keep[i] < 0) continue;
        auto idxs = per_class[i];
        // Deterministic subsample, class-balanced, so no class dominates the
        // 120 MB the dense glyph arms cost.
        for (std::size_t k = idxs.size(); k > 1; --k) std::swap(idxs[k - 1], idxs[rng.below(k)]);
        if (idxs.size() > budget_per_class) idxs.resize(budget_per_class);
        for (std::size_t src : idxs) {
            const std::size_t dst = out.words.size();
            out.words.push_back(t.words[src]);
            out.word_id.push_back(t.word_id[src]);
            out.y.push_back(keep[i]);
            if (is_test(out.words.back())) out.test.push_back(dst);
            else out.train_by_class[static_cast<std::size_t>(keep[i])].push_back(dst);
        }
    }
    out.min_train = out.train_by_class.empty() ? 0 : std::numeric_limits<std::size_t>::max();
    for (const auto& v : out.train_by_class) out.min_train = std::min(out.min_train, v.size());
    return out;
}

// ---------------------------------------------------------------------------
// THE PROBE: nearest centroid, cosine, frozen representation
// ---------------------------------------------------------------------------
void l2_normalize_rows(std::vector<float>& R, std::size_t dim) {
    for (std::size_t r = 0; r + dim <= R.size(); r += dim) {
        double n = 0;
        for (std::size_t i = 0; i < dim; ++i) n += static_cast<double>(R[r + i]) * R[r + i];
        if (n <= 0) continue;
        const float inv = static_cast<float>(1.0 / std::sqrt(n));
        for (std::size_t i = 0; i < dim; ++i) R[r + i] *= inv;
    }
}

struct ProbeResult {
    double      acc = 0.0;
    std::size_t correct = 0, trials = 0;
    double      lo = 0.0, hi = 0.0;
    double      worst = 0.0, best = 0.0;
};

// n_labels == SIZE_MAX means "all the labels there are", which has only one
// possible draw.
ProbeResult run_probe(const std::vector<float>& R, std::size_t dim, const Task& t,
                      std::size_t n_labels, std::size_t draws, std::uint64_t seed) {
    ProbeResult pr;
    const std::size_t C = t.classes.size();
    if (C == 0 || t.test.empty()) return pr;
    const bool full = (n_labels == std::numeric_limits<std::size_t>::max());
    if (full) draws = 1;
    pr.worst = 1.0; pr.best = 0.0;

    std::vector<float> cen(C * dim);
    for (std::size_t d = 0; d < draws; ++d) {
        std::fill(cen.begin(), cen.end(), 0.0f);
        Rng rng(seed + 0x51ED2701ULL * (d + 1));
        for (std::size_t c = 0; c < C; ++c) {
            auto idxs = t.train_by_class[c];
            if (!full) {
                for (std::size_t k = idxs.size(); k > 1; --k) std::swap(idxs[k - 1], idxs[rng.below(k)]);
                if (idxs.size() > n_labels) idxs.resize(n_labels);
            }
            for (std::size_t i : idxs)
                for (std::size_t k = 0; k < dim; ++k) cen[c * dim + k] += R[i * dim + k];
            double n = 0;
            for (std::size_t k = 0; k < dim; ++k) n += static_cast<double>(cen[c * dim + k]) * cen[c * dim + k];
            if (n > 0) {
                const float inv = static_cast<float>(1.0 / std::sqrt(n));
                for (std::size_t k = 0; k < dim; ++k) cen[c * dim + k] *= inv;
            }
        }
        std::size_t hit = 0;
        for (std::size_t i : t.test) {
            std::size_t arg = 0; double bestv = -1e30;
            for (std::size_t c = 0; c < C; ++c) {
                double s = 0;
                const float* a = &R[i * dim];
                const float* b = &cen[c * dim];
                for (std::size_t k = 0; k < dim; ++k) s += static_cast<double>(a[k]) * b[k];
                if (s > bestv) { bestv = s; arg = c; }
            }
            if (static_cast<int>(arg) == t.y[i]) ++hit;
        }
        const double a = static_cast<double>(hit) / static_cast<double>(t.test.size());
        pr.worst = std::min(pr.worst, a);
        pr.best  = std::max(pr.best, a);
        pr.correct += hit;
        pr.trials  += t.test.size();
    }
    pr.acc = 100.0 * static_cast<double>(pr.correct) / static_cast<double>(pr.trials);
    // The draws share one test set, so this interval is narrower than the true
    // draw-to-draw spread. The worst/best columns are the honest spread.
    std::tie(pr.lo, pr.hi) = wilson(pr.correct, pr.trials);
    return pr;
}

struct Row {
    std::string name;
    std::size_t dim = 0;
    ProbeResult r[4];
    double      auc = 0.0;      // the second probe, below
    std::size_t auc_pairs = 0;
};

// A SECOND DOWNSTREAM TASK, BECAUSE ONE PROBE IS NOT A RESULT.
//
// Nearest-centroid classification is one task with one shape, and a
// representation can suit it for reasons that do not generalise -- a centroid is
// a mean, so anything that makes classes compact under averaging wins whether or
// not the geometry is otherwise any good. CBOW beating the hand-designed glyph by
// ten points on that probe is a signal about the substrate, and before it could
// justify changing the substrate it has to survive a differently shaped question.
//
// So: pairwise co-hyponymy. Given two held-out words, do they belong to the same
// WordNet category? Scored by cosine similarity and summarised as AUC -- the
// probability that a same-class pair scores above a different-class pair. This
// uses no centroids, no labelled training set and no threshold, so it shares
// almost nothing with the first probe except the words and the categories.
//
// Chance is exactly 0.5 and needs no baseline row to establish it, which is the
// other reason to prefer AUC here.
double pair_auc(const std::vector<float>& R, std::size_t dim, const Task& t,
                std::size_t& n_pairs, std::uint64_t seed) {
    Rng rng(seed);
    std::vector<double> pos, neg;
    const std::size_t want = 20000;
    // Sample pairs rather than enumerate: |test|^2 / 2 is fine here but the
    // class sizes are very uneven, and sampling keeps the positive and negative
    // sets the same size so the AUC is not dominated by one big category.
    std::size_t guard = 0;
    while ((pos.size() < want || neg.size() < want) && guard++ < want * 40) {
        const std::size_t a = t.test[rng.next() % t.test.size()];
        const std::size_t b = t.test[rng.next() % t.test.size()];
        if (a == b) continue;
        double d = 0.0;
        for (std::size_t k = 0; k < dim; ++k)
            d += static_cast<double>(R[a * dim + k]) * static_cast<double>(R[b * dim + k]);
        if (t.y[a] == t.y[b]) { if (pos.size() < want) pos.push_back(d); }
        else                  { if (neg.size() < want) neg.push_back(d); }
    }
    n_pairs = pos.size() + neg.size();
    if (pos.empty() || neg.empty()) return 0.0;
    // AUC by rank: sort everything, sum the ranks of the positives. Equivalent to
    // the pairwise definition and O(n log n) rather than O(n^2).
    std::vector<std::pair<double, int>> all;
    all.reserve(pos.size() + neg.size());
    for (double v : pos) all.emplace_back(v, 1);
    for (double v : neg) all.emplace_back(v, 0);
    std::sort(all.begin(), all.end(),
              [](const auto& x, const auto& y) { return x.first < y.first; });
    double rank_sum = 0.0;
    for (std::size_t i = 0; i < all.size(); ) {
        std::size_t j = i;
        while (j < all.size() && all[j].first == all[i].first) ++j;
        // Ties share the average rank, or a representation with many identical
        // similarities scores spuriously well.
        const double avg = (static_cast<double>(i) + static_cast<double>(j) - 1.0) / 2.0 + 1.0;
        for (std::size_t k = i; k < j; ++k) if (all[k].second) rank_sum += avg;
        i = j;
    }
    const double np = static_cast<double>(pos.size()), nn = static_cast<double>(neg.size());
    return (rank_sum - np * (np + 1.0) / 2.0) / (np * nn);
}

const std::size_t kLabelSettings[4] = {5, 20, 100, std::numeric_limits<std::size_t>::max()};

Row evaluate(const std::string& name, std::size_t dim, std::vector<float> rep, const Task& t) {
    l2_normalize_rows(rep, dim);
    Row row; row.name = name; row.dim = dim;
    for (std::size_t i = 0; i < 4; ++i)
        row.r[i] = run_probe(rep, dim, t, kLabelSettings[i], kDraws, 0xBEEF0001ULL + i);
    row.auc = pair_auc(rep, dim, t, row.auc_pairs, 0xA0C0ULL);
    return row;
}

// Quick probe used at pretraining checkpoints: fewer draws, two settings.
std::pair<double, double> quick_probe(std::vector<float> rep, std::size_t dim, const Task& t) {
    l2_normalize_rows(rep, dim);
    const double a20 = run_probe(rep, dim, t, 20, 4, 0xBEEF0002ULL).acc;
    const double aall = run_probe(rep, dim, t, std::numeric_limits<std::size_t>::max(), 1, 0xBEEF0003ULL).acc;
    return {a20, aall};
}

std::vector<float> rep_from_table(const std::vector<float>& W, std::size_t dim, const Task& t) {
    std::vector<float> R(t.words.size() * dim, 0.0f);
    for (std::size_t i = 0; i < t.words.size(); ++i)
        std::copy_n(&W[static_cast<std::size_t>(t.word_id[i]) * dim], dim, &R[i * dim]);
    return R;
}

// ---------------------------------------------------------------------------
// SPELLING FEATURES (the input the descent arm shares with the incumbent glyph)
// ---------------------------------------------------------------------------
std::vector<double> spelling_features(const std::string& w) {
    std::vector<double> x(kFeat, 0.0);
    const std::string p = "^^" + w + "$$";
    for (std::size_t i = 0; i + 2 < p.size(); ++i)
        x[fnv1a(std::string_view(p).substr(i, 3)) % kFeat] = 1.0;
    return x;
}

std::vector<float> rep_from_mlp(const khora::descent::Mlp& m, const Task& t,
                                const std::vector<std::vector<double>>& feats) {
    std::vector<float> R(t.words.size() * kDim, 0.0f);
    std::vector<double> h;
    for (std::size_t i = 0; i < t.words.size(); ++i) {
        m.forward(feats[i], &h);
        for (std::size_t k = 0; k < kDim; ++k) R[i * kDim + k] = static_cast<float>(h[k]);
    }
    return R;
}

// ---------------------------------------------------------------------------
// A pretraining trajectory: loss and downstream accuracy at the same points
// ---------------------------------------------------------------------------
struct Traj {
    std::vector<std::string> tag;
    std::vector<double>      loss;    // NaN at init
    std::vector<double>      a20, aall;
};

void print_traj(const char* name, const Traj& tr) {
    std::printf("\n    %s: pretext loss vs downstream accuracy\n", name);
    std::printf("      point      | pretext loss | probe @20/class | probe @all\n");
    std::printf("      -----------+--------------+-----------------+-----------\n");
    for (std::size_t i = 0; i < tr.tag.size(); ++i) {
        if (std::isnan(tr.loss[i]))
            std::printf("      %-10s |        --    |      %5.1f%%     |   %5.1f%%\n",
                        tr.tag[i].c_str(), tr.a20[i], tr.aall[i]);
        else
            std::printf("      %-10s |    %8.4f  |      %5.1f%%     |   %5.1f%%\n",
                        tr.tag[i].c_str(), tr.loss[i], tr.a20[i], tr.aall[i]);
    }
    std::vector<double> l, a;
    for (std::size_t i = 0; i < tr.tag.size(); ++i)
        if (!std::isnan(tr.loss[i])) { l.push_back(tr.loss[i]); a.push_back(tr.aall[i]); }
    if (l.size() >= 3)
        std::printf("      Pearson r(loss, probe@all) over %zu trained points = %+.3f\n",
                    l.size(), pearson(l, a));
}

double sigmoidf(double x) { return 1.0 / (1.0 + std::exp(-x)); }

} // namespace

int main(int argc, char** argv) {
    const auto t_start = std::chrono::steady_clock::now();
    const std::string dir        = (argc > 1) ? argv[1] : "data/reservoir";
    const std::string wn_path    = (argc > 2) ? argv[2] : "data/eval/wn_categories.tsv";
    const std::size_t max_tokens = (argc > 3) ? std::stoul(argv[3]) : 8000000;

    std::printf("Self-supervision — does a representation learned from unlabelled text\n");
    std::printf("beat the one that was hand-designed?\n\n");

    const Corpus corpus = load_corpus(dir, max_tokens, 5);
    if (corpus.sents.empty()) { std::printf("  no corpus at %s\n", dir.c_str()); return 0; }
    std::printf("  corpus: %zu books, %zu tokens read, %zu kept in vocabulary,\n"
                "          %zu word types seen, %zu above the frequency floor of 5\n",
                corpus.books, corpus.raw_tokens, corpus.kept_tokens,
                corpus.raw_types, corpus.vocab.size());

    const Task task = build_task(corpus, wn_path, 10);
    if (task.classes.size() < 3) {
        std::printf("  could not build a downstream task from %s\n", wn_path.c_str());
        return 0;
    }
    std::printf("\n  downstream task: WordNet category, %zu classes, %zu words\n",
                task.classes.size(), task.words.size());
    std::printf("    class            | words | train | test\n");
    std::printf("    -----------------+-------+-------+------\n");
    std::vector<std::size_t> test_per_class(task.classes.size(), 0);
    for (std::size_t i : task.test) ++test_per_class[static_cast<std::size_t>(task.y[i])];
    for (std::size_t c = 0; c < task.classes.size(); ++c)
        std::printf("    %-16s | %5zu | %5zu | %4zu\n", task.classes[c].c_str(),
                    task.train_by_class[c].size() + test_per_class[c],
                    task.train_by_class[c].size(), test_per_class[c]);
    std::printf("    smallest training pool: %zu words -- the 100/class column is capped\n"
                "    at that for any class below it, which is stated again under the table.\n",
                task.min_train);

    const double chance_acc = 100.0 / static_cast<double>(task.classes.size());
    const std::size_t maj = *std::max_element(test_per_class.begin(), test_per_class.end());
    const double maj_acc = 100.0 * static_cast<double>(maj) / static_cast<double>(task.test.size());

    std::vector<Row> rows;
    std::vector<std::pair<std::string, double>> correlations;

    // -----------------------------------------------------------------------
    // BASELINE 1: the incumbent. khora::lexicon::encode_token, hand-designed.
    // -----------------------------------------------------------------------
    std::vector<Glyph> hand_glyphs(task.words.size());
    for (std::size_t i = 0; i < task.words.size(); ++i)
        hand_glyphs[i] = khora::lexicon::encode_token(task.words[i]);
    {
        std::vector<float> R(task.words.size() * kGlyphBits);
        for (std::size_t i = 0; i < task.words.size(); ++i)
            for (std::size_t b = 0; b < kGlyphBits; ++b)
                R[i * kGlyphBits + b] = hand_glyphs[i].bit(b) ? 1.0f : -1.0f;
        rows.push_back(evaluate("hand-designed glyph (encode_token)", kGlyphBits, std::move(R), task));
    }

    // BASELINE 2: a random glyph of the same dimension. If the incumbent scores
    // near this, the trigram structure is not what the probe is using.
    {
        std::vector<float> R(task.words.size() * kGlyphBits);
        for (std::size_t i = 0; i < task.words.size(); ++i) {
            const Glyph g = Glyph::random(0xA11CE000ULL + i);
            for (std::size_t b = 0; b < kGlyphBits; ++b)
                R[i * kGlyphBits + b] = g.bit(b) ? 1.0f : -1.0f;
        }
        rows.push_back(evaluate("random glyph, same dimension", kGlyphBits, std::move(R), task));
    }

    // BASELINE 3: random 64-dim vectors. This is the "is the probe doing the
    // work" control and it should land on chance. If it does not, the probe is
    // exploiting something about the split rather than the representation.
    {
        Rng rng(0x5EED1234ULL);
        std::vector<float> R(task.words.size() * kDim);
        for (float& x : R) x = static_cast<float>(rng.unit() * 2.0 - 1.0);
        rows.push_back(evaluate("random vectors, D=64", kDim, std::move(R), task));
    }

    // -----------------------------------------------------------------------
    // PRETEXT A: masked context prediction through khora::descent.
    // Spelling in, "which of the 256 commonest words stands near this one" out.
    // -----------------------------------------------------------------------
    {
        std::printf("\n  --- PRETEXT A: masked context prediction (khora::descent MLP) ---\n");
        // Training pairs: up to 6 context observations for each of the 8,000
        // most frequent words. Sampling per word rather than per position keeps
        // "the" from being 5% of the training set.
        const std::size_t kTrainWords = std::min<std::size_t>(8000, corpus.vocab.size());
        std::vector<std::vector<std::size_t>> obs(kTrainWords);
        for (const auto& s : corpus.sents) {
            for (std::size_t p = 0; p < s.size(); ++p) {
                const std::size_t w = static_cast<std::size_t>(s[p]);
                if (w >= kTrainWords || obs[w].size() >= 6) continue;
                const std::size_t lo = (p > kWindow) ? p - kWindow : 0;
                const std::size_t hi = std::min(s.size(), p + kWindow + 1);
                for (std::size_t q = lo; q < hi && obs[w].size() < 6; ++q) {
                    if (q == p) continue;
                    if (static_cast<std::size_t>(s[q]) < kCtxClasses)
                        obs[w].push_back(static_cast<std::size_t>(s[q]));
                }
            }
        }
        std::vector<std::vector<double>> xs;
        std::vector<std::size_t>         ys;
        for (std::size_t w = 0; w < kTrainWords; ++w) {
            if (obs[w].empty()) continue;
            const std::vector<double> f = spelling_features(corpus.vocab[w]);
            for (std::size_t c : obs[w]) { xs.push_back(f); ys.push_back(c); }
        }
        std::printf("    %zu training pairs over %zu word types, %zu-way target\n",
                    xs.size(), kTrainWords, kCtxClasses);

        std::vector<std::vector<double>> eval_feats;
        eval_feats.reserve(task.words.size());
        for (const auto& w : task.words) eval_feats.push_back(spelling_features(w));

        khora::descent::Mlp mlp(kFeat, kDim, kCtxClasses, 0x1D0CULL);

        // BASELINE 4: the SAME network before a single gradient step. A random
        // projection of the hand-designed spelling features -- the control that
        // says whether training the projection matters at all.
        rows.push_back(evaluate("untrained MLP hidden (random proj. of spelling)",
                                kDim, rep_from_mlp(mlp, task, eval_feats), task));

        Traj tr;
        {
            auto [a20, aall] = quick_probe(rep_from_mlp(mlp, task, eval_feats), kDim, task);
            tr.tag.push_back("init"); tr.loss.push_back(std::nan(""));
            tr.a20.push_back(a20); tr.aall.push_back(aall);
        }
        khora::descent::Matrix dW1(kDim, kFeat), dW2(kCtxClasses, kDim);
        std::vector<double> db1(kDim), db2(kCtxClasses);
        std::vector<std::size_t> order(xs.size());
        std::iota(order.begin(), order.end(), std::size_t{0});
        Rng rng(0x515CULL);
        constexpr std::size_t kBatch = 32;
        const double lr = 0.30;
        for (std::size_t ep = 0; ep < kCheckpoints; ++ep) {
            for (std::size_t k = order.size(); k > 1; --k) std::swap(order[k - 1], order[rng.below(k)]);
            double total = 0;
            for (std::size_t st = 0; st < order.size(); st += kBatch) {
                const std::size_t en = std::min(st + kBatch, order.size());
                std::fill(dW1.v.begin(), dW1.v.end(), 0.0);
                std::fill(dW2.v.begin(), dW2.v.end(), 0.0);
                std::fill(db1.begin(), db1.end(), 0.0);
                std::fill(db2.begin(), db2.end(), 0.0);
                for (std::size_t q = st; q < en; ++q)
                    total += mlp.backward(xs[order[q]], ys[order[q]], dW1, db1, dW2, db2);
                const double sc = lr / static_cast<double>(en - st);
                for (std::size_t i = 0; i < mlp.W1().v.size(); ++i) mlp.W1().v[i] -= sc * dW1.v[i];
                for (std::size_t i = 0; i < mlp.W2().v.size(); ++i) mlp.W2().v[i] -= sc * dW2.v[i];
                for (std::size_t i = 0; i < kDim; ++i)        mlp.b1()[i] -= sc * db1[i];
                for (std::size_t i = 0; i < kCtxClasses; ++i) mlp.b2()[i] -= sc * db2[i];
            }
            auto [a20, aall] = quick_probe(rep_from_mlp(mlp, task, eval_feats), kDim, task);
            tr.tag.push_back("epoch " + std::to_string(ep + 1));
            tr.loss.push_back(total / static_cast<double>(xs.size()));
            tr.a20.push_back(a20); tr.aall.push_back(aall);
        }
        print_traj("A (masked context, descent)", tr);
        {
            std::vector<double> l, a;
            for (std::size_t i = 0; i < tr.tag.size(); ++i)
                if (!std::isnan(tr.loss[i])) { l.push_back(tr.loss[i]); a.push_back(tr.aall[i]); }
            correlations.emplace_back("A masked context (descent)", pearson(l, a));
        }
        rows.push_back(evaluate("A: masked context prediction (descent MLP)",
                                kDim, rep_from_mlp(mlp, task, eval_feats), task));
    }

    // -----------------------------------------------------------------------
    // PRETEXT B: masked token prediction. CBOW + negative sampling.
    // -----------------------------------------------------------------------
    {
        std::printf("\n  --- PRETEXT B: masked token prediction (CBOW + negative sampling) ---\n");
        const std::size_t V = corpus.vocab.size();
        Rng rng(0xCB0B0ULL);
        std::vector<float> Win(V * kDim), Wout(V * kDim, 0.0f);
        for (float& x : Win) x = static_cast<float>((rng.unit() - 0.5) / kDim);

        // Unigram^0.75 sampling table, the word2vec distribution: frequent words
        // appear as negatives often enough to be pushed away from everything,
        // rare ones often enough to move at all.
        std::vector<std::int32_t> negtab;
        {
            double tot = 0;
            for (std::uint32_t f : corpus.freq) tot += std::pow(static_cast<double>(f), 0.75);
            negtab.reserve(1000000);
            double acc = 0;
            for (std::size_t i = 0; i < V; ++i) {
                acc += std::pow(static_cast<double>(corpus.freq[i]), 0.75) / tot;
                while (negtab.size() < static_cast<std::size_t>(acc * 1000000.0) && negtab.size() < 1000000)
                    negtab.push_back(static_cast<std::int32_t>(i));
            }
            while (negtab.size() < 1000000) negtab.push_back(static_cast<std::int32_t>(V - 1));
        }

        Traj tr;
        {
            auto [a20, aall] = quick_probe(rep_from_table(Win, kDim, task), kDim, task);
            tr.tag.push_back("init"); tr.loss.push_back(std::nan(""));
            tr.a20.push_back(a20); tr.aall.push_back(aall);
        }

        // kCheckpoints probes per epoch, so kCbowEpochs * kCheckpoints chunks
        // in total and each chunk is a quarter of one pass over the corpus.
        const std::size_t sents_per_chunk = (corpus.sents.size() + kCheckpoints - 1) / kCheckpoints;
        std::vector<double> h(kDim), dh(kDim);
        std::size_t chunk = 0, si = 0, epoch = 0;
        std::size_t positions = 0, row_updates = 0;
        while (epoch < kCbowEpochs) {
            double loss_sum = 0; std::size_t loss_n = 0;
            for (std::size_t k = 0; k < sents_per_chunk; ++k) {
                if (si >= corpus.sents.size()) { si = 0; ++epoch; if (epoch >= kCbowEpochs) break; }
                const auto& s = corpus.sents[si++];
                for (std::size_t p = 0; p < s.size(); ++p) {
                    const std::size_t lo = (p > kWindow) ? p - kWindow : 0;
                    const std::size_t hi = std::min(s.size(), p + kWindow + 1);
                    std::size_t nctx = 0;
                    std::fill(h.begin(), h.end(), 0.0);
                    for (std::size_t q = lo; q < hi; ++q) {
                        if (q == p) continue;
                        const float* v = &Win[static_cast<std::size_t>(s[q]) * kDim];
                        for (std::size_t d = 0; d < kDim; ++d) h[d] += v[d];
                        ++nctx;
                    }
                    if (nctx == 0) continue;
                    for (std::size_t d = 0; d < kDim; ++d) h[d] /= static_cast<double>(nctx);
                    std::fill(dh.begin(), dh.end(), 0.0);
                    // Linear decay, word2vec's schedule. A constant rate here
                    // leaves the frequent words oscillating at the end of training.
                    const double frac = static_cast<double>(positions) /
                                        static_cast<double>(kCbowEpochs * corpus.kept_tokens + 1);
                    const double lr = 0.05 * std::max(0.02, 1.0 - frac);
                    for (std::size_t n = 0; n <= kNegatives; ++n) {
                        const std::size_t tgt = (n == 0)
                            ? static_cast<std::size_t>(s[p])
                            : static_cast<std::size_t>(negtab[rng.below(negtab.size())]);
                        if (n > 0 && tgt == static_cast<std::size_t>(s[p])) continue;
                        const double label = (n == 0) ? 1.0 : 0.0;
                        float* u = &Wout[tgt * kDim];
                        double dot = 0;
                        for (std::size_t d = 0; d < kDim; ++d) dot += h[d] * u[d];
                        const double pr = sigmoidf(dot);
                        loss_sum += -std::log(std::max(label > 0 ? pr : 1.0 - pr, 1e-12));
                        ++loss_n;
                        const double g = (pr - label) * lr;
                        for (std::size_t d = 0; d < kDim; ++d) {
                            dh[d] += g * u[d];
                            u[d]  -= static_cast<float>(g * h[d]);
                        }
                    }
                    const double inv = 1.0 / static_cast<double>(nctx);
                    for (std::size_t q = lo; q < hi; ++q) {
                        if (q == p) continue;
                        float* v = &Win[static_cast<std::size_t>(s[q]) * kDim];
                        for (std::size_t d = 0; d < kDim; ++d) v[d] -= static_cast<float>(dh[d] * inv);
                    }
                    row_updates += nctx;
                    ++positions;
                }
            }
            if (loss_n) {   // the last chunk can be empty when the epoch ends on a boundary
                ++chunk;
                auto [a20, aall] = quick_probe(rep_from_table(Win, kDim, task), kDim, task);
                tr.tag.push_back("chunk " + std::to_string(chunk));
                tr.loss.push_back(loss_sum / static_cast<double>(loss_n));
                tr.a20.push_back(a20); tr.aall.push_back(aall);
            }
            if (epoch >= kCbowEpochs) break;
        }
        std::printf("    %zu masked positions, %zu negatives each, window +/-%zu,\n"
                    "    %zu embedding-row updates\n",
                    positions, kNegatives, kWindow, row_updates);
        print_traj("B (masked token, CBOW+neg)", tr);
        {
            std::vector<double> l, a;
            for (std::size_t i = 0; i < tr.tag.size(); ++i)
                if (!std::isnan(tr.loss[i])) { l.push_back(tr.loss[i]); a.push_back(tr.aall[i]); }
            correlations.emplace_back("B masked token (CBOW+neg)", pearson(l, a));
        }
        rows.push_back(evaluate("B: masked token prediction (CBOW+neg)",
                                kDim, rep_from_table(Win, kDim, task), task));
    }

    // -----------------------------------------------------------------------
    // PRETEXT C: contrastive. Two augmented views of one window, NT-Xent.
    // -----------------------------------------------------------------------
    {
        std::printf("\n  --- PRETEXT C: contrastive NT-Xent over augmented windows ---\n");
        const std::size_t V = corpus.vocab.size();
        Rng rng(0xC047ULL);
        std::vector<float> W(V * kDim);
        for (float& x : W) x = static_cast<float>((rng.unit() - 0.5) / std::sqrt(double(kDim)));

        std::vector<std::size_t> long_sents;
        for (std::size_t i = 0; i < corpus.sents.size(); ++i)
            if (corpus.sents[i].size() >= kWindowLen) long_sents.push_back(i);
        if (long_sents.empty()) { std::printf("    no windows long enough\n"); }

        std::vector<char> touched(V, 0);
        std::size_t row_updates = 0;
        Traj tr;
        {
            auto [a20, aall] = quick_probe(rep_from_table(W, kDim, task), kDim, task);
            tr.tag.push_back("init"); tr.loss.push_back(std::nan(""));
            tr.a20.push_back(a20); tr.aall.push_back(aall);
        }

        const std::size_t B = kNtxentBatch;
        std::vector<std::vector<std::int32_t>> va(B), vb(B);
        std::vector<double> ma(B * kDim), mb(B * kDim), za(B * kDim), zb(B * kDim);
        std::vector<double> nrma(B), nrmb(B), S(B * B), P(B * B);
        std::vector<double> dza(B * kDim), dzb(B * kDim);

        const std::size_t steps_per_chunk = kNtxentSteps / kCheckpoints;
        for (std::size_t ck = 0; ck < kCheckpoints && !long_sents.empty(); ++ck) {
            double loss_sum = 0; std::size_t loss_n = 0;
            for (std::size_t st = 0; st < steps_per_chunk; ++st) {
                // --- build a batch of augmented view pairs ---
                for (std::size_t i = 0; i < B; ++i) {
                    const auto& s = corpus.sents[long_sents[rng.below(long_sents.size())]];
                    const std::size_t span = s.size() - kWindowLen;
                    const std::size_t base = span ? rng.below(span + 1) : 0;
                    for (int v = 0; v < 2; ++v) {
                        // window shift: +/-2, clipped to the sentence
                        long shift = static_cast<long>(rng.below(5)) - 2;
                        long start = static_cast<long>(base) + shift;
                        if (start < 0) start = 0;
                        if (start > static_cast<long>(span)) start = static_cast<long>(span);
                        auto& out = (v == 0) ? va[i] : vb[i];
                        out.clear();
                        for (std::size_t k = 0; k < kWindowLen; ++k) {   // token dropout
                            if (rng.unit() < kDropKeep) out.push_back(s[static_cast<std::size_t>(start) + k]);
                        }
                        if (out.size() < 2)
                            for (std::size_t k = 0; k < kWindowLen; ++k)
                                out.push_back(s[static_cast<std::size_t>(start) + k]);
                    }
                }
                // --- encode: mean pool then L2 normalise ---
                auto encode = [&](const std::vector<std::vector<std::int32_t>>& views,
                                  std::vector<double>& m, std::vector<double>& z,
                                  std::vector<double>& nrm) {
                    for (std::size_t i = 0; i < B; ++i) {
                        double* mi = &m[i * kDim];
                        std::fill(mi, mi + kDim, 0.0);
                        for (std::int32_t tk : views[i]) {
                            const float* w = &W[static_cast<std::size_t>(tk) * kDim];
                            for (std::size_t d = 0; d < kDim; ++d) mi[d] += w[d];
                        }
                        const double inv = 1.0 / static_cast<double>(views[i].size());
                        double n = 0;
                        for (std::size_t d = 0; d < kDim; ++d) { mi[d] *= inv; n += mi[d] * mi[d]; }
                        nrm[i] = std::sqrt(std::max(n, 1e-12));
                        for (std::size_t d = 0; d < kDim; ++d) z[i * kDim + d] = mi[d] / nrm[i];
                    }
                };
                encode(va, ma, za, nrma);
                encode(vb, mb, zb, nrmb);

                // --- NT-Xent, in-batch negatives, one direction ---
                for (std::size_t i = 0; i < B; ++i) {
                    double mx = -1e30;
                    for (std::size_t j = 0; j < B; ++j) {
                        double s = 0;
                        for (std::size_t d = 0; d < kDim; ++d) s += za[i * kDim + d] * zb[j * kDim + d];
                        S[i * B + j] = s / kTau;
                        mx = std::max(mx, S[i * B + j]);
                    }
                    double sum = 0;
                    for (std::size_t j = 0; j < B; ++j) { P[i * B + j] = std::exp(S[i * B + j] - mx); sum += P[i * B + j]; }
                    for (std::size_t j = 0; j < B; ++j) P[i * B + j] /= sum;
                    loss_sum += -std::log(std::max(P[i * B + i], 1e-12));
                    ++loss_n;
                }
                std::fill(dza.begin(), dza.end(), 0.0);
                std::fill(dzb.begin(), dzb.end(), 0.0);
                const double scale = 1.0 / (kTau * static_cast<double>(B));
                for (std::size_t i = 0; i < B; ++i)
                    for (std::size_t j = 0; j < B; ++j) {
                        const double g = (P[i * B + j] - (i == j ? 1.0 : 0.0)) * scale;
                        if (g == 0.0) continue;
                        for (std::size_t d = 0; d < kDim; ++d) {
                            dza[i * kDim + d] += g * zb[j * kDim + d];
                            dzb[j * kDim + d] += g * za[i * kDim + d];
                        }
                    }
                // --- back through the normalisation, then to the token rows ---
                const double lr = 0.05;
                auto apply = [&](const std::vector<std::vector<std::int32_t>>& views,
                                 const std::vector<double>& z, const std::vector<double>& nrm,
                                 const std::vector<double>& dz) {
                    for (std::size_t i = 0; i < B; ++i) {
                        double zd = 0;
                        for (std::size_t d = 0; d < kDim; ++d) zd += z[i * kDim + d] * dz[i * kDim + d];
                        const double inv = 1.0 / (nrm[i] * static_cast<double>(views[i].size()));
                        for (std::int32_t tk : views[i]) {
                            float* w = &W[static_cast<std::size_t>(tk) * kDim];
                            touched[static_cast<std::size_t>(tk)] = 1;
                            ++row_updates;
                            for (std::size_t d = 0; d < kDim; ++d)
                                w[d] -= static_cast<float>(lr * (dz[i * kDim + d] - z[i * kDim + d] * zd) * inv);
                        }
                    }
                };
                apply(va, za, nrma, dza);
                apply(vb, zb, nrmb, dzb);
            }
            auto [a20, aall] = quick_probe(rep_from_table(W, kDim, task), kDim, task);
            tr.tag.push_back("chunk " + std::to_string(ck + 1));
            tr.loss.push_back(loss_n ? loss_sum / static_cast<double>(loss_n) : std::nan(""));
            tr.a20.push_back(a20); tr.aall.push_back(aall);
        }
        std::size_t cov = 0;
        for (std::size_t i = 0; i < task.words.size(); ++i)
            if (touched[static_cast<std::size_t>(task.word_id[i])]) ++cov;
        std::printf("    %zu steps x %zu pairs; %zu embedding-row updates; %zu/%zu eval\n"
                    "    words received at least one gradient%s\n",
                    kNtxentSteps, B, row_updates, cov, task.words.size(),
                    cov == task.words.size() ? ""
                        : " (the rest keep their random init,\n    which is a property of the objective, not a bug in the harness)");
        print_traj("C (contrastive NT-Xent)", tr);
        {
            std::vector<double> l, a;
            for (std::size_t i = 0; i < tr.tag.size(); ++i)
                if (!std::isnan(tr.loss[i])) { l.push_back(tr.loss[i]); a.push_back(tr.aall[i]); }
            correlations.emplace_back("C contrastive (NT-Xent)", pearson(l, a));
        }
        rows.push_back(evaluate("C: contrastive NT-Xent (augmented windows)",
                                kDim, rep_from_table(W, kDim, task), task));
    }

    // -----------------------------------------------------------------------
    // PRETEXT D: context bundling in the Glyph algebra. Same objective family
    // as B, no gradients, no floats -- the sign of a sum of sparse ternary
    // index vectors. This is what khora::lexicon::Lexicon already does; what it
    // has never had is a downstream probe.
    // -----------------------------------------------------------------------
    {
        std::printf("\n  --- PRETEXT D: context bundling in the Glyph algebra ---\n");
        const std::size_t V = corpus.vocab.size();
        constexpr int kPairs = 12;                       // +/- nonzeros per index vector
        std::vector<std::uint16_t> idx(V * kPairs * 2);  // bit positions
        for (std::size_t i = 0; i < V; ++i) {
            Rng r(fnv1a(corpus.vocab[i]) | 1ULL);
            for (int k = 0; k < kPairs * 2; ++k)
                idx[i * kPairs * 2 + k] = static_cast<std::uint16_t>(r.below(kGlyphBits));
        }
        std::vector<std::int32_t> eval_of(V, -1);
        for (std::size_t i = 0; i < task.words.size(); ++i)
            eval_of[static_cast<std::size_t>(task.word_id[i])] = static_cast<std::int32_t>(i);

        // int16 accumulators: 3,000 words x 10,000 dims = 60 MB. Saturating,
        // because a word seen 50,000 times would otherwise wrap.
        std::vector<std::int16_t> acc(task.words.size() * kGlyphBits, 0);
        std::size_t updates = 0;
        for (const auto& s : corpus.sents) {
            for (std::size_t p = 0; p < s.size(); ++p) {
                const std::int32_t e = eval_of[static_cast<std::size_t>(s[p])];
                if (e < 0) continue;
                std::int16_t* a = &acc[static_cast<std::size_t>(e) * kGlyphBits];
                const std::size_t lo = (p > kWindow) ? p - kWindow : 0;
                const std::size_t hi = std::min(s.size(), p + kWindow + 1);
                for (std::size_t q = lo; q < hi; ++q) {
                    if (q == p) continue;
                    const std::uint16_t* ix = &idx[static_cast<std::size_t>(s[q]) * kPairs * 2];
                    for (int k = 0; k < kPairs; ++k) {
                        std::int16_t& hp = a[ix[2 * k]];
                        std::int16_t& hm = a[ix[2 * k + 1]];
                        if (hp < 30000) ++hp;
                        if (hm > -30000) --hm;
                    }
                    ++updates;
                }
            }
        }
        std::printf("    %zu co-occurrence updates, %d +/- nonzeros per index vector\n",
                    updates, kPairs);

        std::vector<Glyph> learned(task.words.size());
        std::size_t empty = 0;
        for (std::size_t i = 0; i < task.words.size(); ++i) {
            bool any = false;
            for (std::size_t b = 0; b < kGlyphBits; ++b)
                if (acc[i * kGlyphBits + b] > 0) { learned[i].set_bit(b); any = true; }
            if (!any) ++empty;
        }
        if (empty) std::printf("    %zu eval words ended with an all-zero glyph\n", empty);
        {
            std::vector<float> R(task.words.size() * kGlyphBits);
            for (std::size_t i = 0; i < task.words.size(); ++i)
                for (std::size_t b = 0; b < kGlyphBits; ++b)
                    R[i * kGlyphBits + b] = learned[i].bit(b) ? 1.0f : -1.0f;
            std::vector<std::int16_t>().swap(acc);
            rows.push_back(evaluate("D: context bundling (Glyph algebra)",
                                    kGlyphBits, std::move(R), task));
        }

        // A qualitative look at what the two glyph families actually encode.
        // Not a metric; it is here because "spelling neighbours vs company
        // neighbours" is the whole difference and a table hides it.
        // The probe words are the six commonest words IN THE EVAL SET rather
        // than a list I chose, because a list I chose is a list I could choose
        // again after seeing the output.
        std::vector<std::size_t> probes(task.words.size());
        std::iota(probes.begin(), probes.end(), std::size_t{0});
        std::partial_sort(probes.begin(), probes.begin() + 6, probes.end(),
                          [&](std::size_t a, std::size_t b) {
                              return corpus.freq[static_cast<std::size_t>(task.word_id[a])]
                                   > corpus.freq[static_cast<std::size_t>(task.word_id[b])];
                          });
        probes.resize(6);
        std::printf("\n    nearest eval-set neighbours, hand-designed vs learned glyph:\n");
        for (std::size_t self : probes) {
            const std::string& pw = task.words[self];
            auto top3 = [&](auto sim) {
                std::vector<std::pair<double, std::size_t>> v;
                for (std::size_t i = 0; i < task.words.size(); ++i)
                    if (i != self) v.emplace_back(sim(i), i);
                std::partial_sort(v.begin(), v.begin() + 3, v.end(),
                                  [](const auto& a, const auto& b) { return a.first > b.first; });
                std::string s;
                for (int k = 0; k < 3; ++k) { s += " "; s += task.words[v[static_cast<std::size_t>(k)].second]; }
                return s;
            };
            const std::string h = top3([&](std::size_t i) {
                return hand_glyphs[self].similarity(hand_glyphs[i]);
            });
            const std::string l = top3([&](std::size_t i) { return learned[self].similarity(learned[i]); });
            std::printf("      %-11s hand:%-32s learned:%s\n", pw.c_str(), h.c_str(), l.c_str());
        }
    }

    // -----------------------------------------------------------------------
    // THE TABLE
    // -----------------------------------------------------------------------
    std::printf("\n\n  === DOWNSTREAM PROBE: WordNet category, nearest centroid, frozen rep ===\n");
    std::printf("  %zu classes, %zu held-out test words, %zu label draws per few-shot cell\n",
                task.classes.size(), task.test.size(), kDraws);
    std::printf("  cells are accuracy%% [95%% Wilson over draws x test words]\n\n");
    const std::string bar(47, '-'), cbar(18, '-');
    std::printf("  %-47s| %5s |%17s |%17s |%17s |%17s\n",
                "representation", "dim", "5/class", "20/class", "100/class", "all");
    std::printf("  %s+-------+%s+%s+%s+%s\n", bar.c_str(),
                cbar.c_str(), cbar.c_str(), cbar.c_str(), cbar.c_str());
    auto cell = [](const ProbeResult& p) {
        char b[64];
        std::snprintf(b, sizeof b, "%5.1f [%4.1f-%4.1f] ", p.acc, p.lo, p.hi);
        return std::string(b);
    };
    {
        char b[64];
        std::snprintf(b, sizeof b, "%5.1f", chance_acc);
        std::printf("  %-47s| %5s |%17s |%17s |%17s |%17s \n", "chance (uniform)", "-", "-", "-", "-", b);
        std::snprintf(b, sizeof b, "%5.1f", maj_acc);
        std::printf("  %-47s| %5s |%17s |%17s |%17s |%17s \n", "majority class", "-", "-", "-", "-", b);
    }
    for (const Row& r : rows)
        std::printf("  %-47s| %5zu |%18s|%18s|%18s|%18s\n", r.name.c_str(), r.dim,
                    cell(r.r[0]).c_str(), cell(r.r[1]).c_str(),
                    cell(r.r[2]).c_str(), cell(r.r[3]).c_str());
    std::printf("\n  draw-to-draw spread at 5 labels/class (min-max over %zu draws):\n", kDraws);
    for (const Row& r : rows)
        std::printf("    %-47s %5.1f%% .. %5.1f%%\n", r.name.c_str(),
                    100.0 * r.r[0].worst, 100.0 * r.r[0].best);
    if (task.min_train < 100)
        std::printf("\n  NOTE: the smallest training pool holds %zu words, so the 100/class\n"
                    "  column is capped at the pool size for those classes.\n", task.min_train);

    // A mechanical verdict rather than a sentence written after looking at the
    // table. Disjoint 95% intervals is a conservative test -- it can miss a real
    // difference, it does not manufacture one.
    {
        const Row& inc = rows[0];
        std::printf("\n  === EVERY ARM MINUS THE INCUMBENT (points; * = 95%% intervals disjoint) ===\n");
        std::printf("  %-47s|%11s|%11s|%11s|%11s\n", "arm", "5/class", "20/class", "100/class", "all");
        std::printf("  %s+-----------+-----------+-----------+-----------\n", bar.c_str());
        for (std::size_t k = 1; k < rows.size(); ++k) {
            std::printf("  %-47s", rows[k].name.c_str());
            for (std::size_t i = 0; i < 4; ++i) {
                const bool disjoint = rows[k].r[i].lo > inc.r[i].hi || rows[k].r[i].hi < inc.r[i].lo;
                char b[32];
                std::snprintf(b, sizeof b, "%+6.1f%s", rows[k].r[i].acc - inc.r[i].acc,
                              disjoint ? " *" : "  ");
                std::printf("|%11s", b);
            }
            std::printf("\n");
        }
        // The majority class is the bar below which a representation has bought
        // nothing at all, so which arms clear it is counted, not asserted.
        std::printf("\n  clears the majority class (%.1f%%) with all labels:", maj_acc);
        bool any = false;
        for (const Row& r : rows)
            if (r.r[3].acc > maj_acc) { std::printf(" [%s]", r.name.c_str()); any = true; }
        std::printf("%s\n", any ? "" : " NONE");
    }

    // --- THE SECOND TASK -----------------------------------------------------
    {
        std::printf("\n  === A SECOND, DIFFERENTLY SHAPED TASK: PAIRWISE CO-HYPONYMY ===\n");
        std::printf("    Do two held-out words share a WordNet category? Scored by cosine\n"
                    "    similarity and summarised as AUC, so there are no centroids, no\n"
                    "    labelled training set and no threshold. Chance is exactly 0.500.\n"
                    "    One probe is not a result; this shares only the words and the\n"
                    "    categories with the table above.\n\n");
        std::printf("    %-47s|    AUC | vs incumbent | pairs\n", "representation");
        std::printf("    %s+--------+--------------+-------\n", bar.c_str());
        const double inc_auc = rows[0].auc;
        for (const Row& r : rows)
            std::printf("    %-47s| %.4f |      %+.4f  | %zu\n", r.name.c_str(), r.auc,
                        r.auc - inc_auc, r.auc_pairs);
        std::printf("\n    An arm that wins the first table and loses this one suited the probe\n"
                    "    rather than the problem, which is the thing a single probe cannot\n"
                    "    tell you.\n");
    }

    std::printf("\n  === DOES THE PRETEXT LOSS PREDICT DOWNSTREAM ACCURACY? ===\n");
    std::printf("    pretext task                    | Pearson r(loss, probe@all)\n");
    std::printf("    --------------------------------+---------------------------\n");
    for (const auto& [n, r] : correlations)
        std::printf("    %-31s | %+.3f\n", n.c_str(), r);
    std::printf("    r near -1 means falling loss tracked rising accuracy. Four trained\n"
                "    points each -- enough to see a direction, not enough for a p-value.\n");

    const auto secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();
    std::printf("\n  %.1f s wall clock.\n", secs);

    std::printf("\n  WHAT THIS HARNESS CANNOT SEE:\n"
                "    - words in two chosen WordNet categories were discarded, so polysemy\n"
                "      is invisible and the task is easier than the real one;\n"
                "    - only corpus-frequent words (freq >= 10) are evaluated, so the\n"
                "      rare-word regime, where a spelling prior should win most, is\n"
                "      untested;\n"
                "    - the class inventory was chosen BY the corpus (categories with under\n"
                "      40 frequent members were never eligible);\n"
                "    - nearest centroid is a weak probe; a stronger one could reorder these\n"
                "      arms, and nothing here rules that out;\n"
                "    - the 10,000-dim and 64-dim arms are not dimension-matched. Each has\n"
                "      its own random control, which brackets the effect without removing it;\n"
                "    - none of these numbers say anything about whether a representation is\n"
                "      useful to the rest of Khora. They say it linearly separates twelve\n"
                "      WordNet categories, and that is all.\n");
    return 0;
}
