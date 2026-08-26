// CAN KHORA PRODUCE A SENTENCE, AND CAN ANYONE TELL?
//
// Khora has read eight million tokens and cannot write a line. There is a
// Cogitator::utter() -- src/cogitator/cogitator.cpp:734 -- but read what it
// does: it asks the Cortex for six candidate glyphs, decodes each to a word,
// hard-skips anything in a five-word anti-repetition window, and takes the
// argmax of (rank bonus + Plexus topic pull). That is a greedy walk with a
// loop-breaker. There is no distribution over the next token anywhere in the
// tree, so there is no perplexity, no held-out likelihood, no sampling, and no
// way to say whether the output is better or worse than guessing. The single
// most visible capability in the field, and the system has neither a
// principled version of it nor a number for the version it has.
//
// This builds the missing thing and then measures the existing thing against
// it on the same held-out text with the same metric.
//
//   1. n-gram language models, n = 1..5, three smoothings: add-k, Witten-Bell,
//      interpolated Kneser-Ney. Every one of them is a proper distribution over
//      the vocabulary -- checked at runtime by summing over all V words, see
//      "sum-to-one" below, because a smoothing bug shows up as a suspiciously
//      good perplexity and nothing else.
//
//   2. THE SUBSTRATE'S OWN ATTEMPT. The Plexus is a learned co-occurrence graph
//      with PMI affinities; it is the thing Khora actually runs on. Read as a
//      next-token predictor three ways -- raw co-occurrence, PPMI (the
//      affinity() that utter() steers by), and the associates() readout -- each
//      mixed with the unigram so it cannot assign zero. Scored on the same
//      held-out books with the same metric.
//
//   3. The dumb baselines, because a perplexity with nothing beside it is a
//      decoration: uniform over the vocabulary, and the unigram.
//
// SPLIT. Books are held out WHOLE, sorted by title then assigned by index:
// i%5==0 -> test, i%5==1 -> dev, else train. Splitting inside a book leaks its
// vocabulary, its proper nouns and its register into the training counts, and
// the resulting perplexity is a measurement of the split, not the model. dev
// exists so that k, the Kneser-Ney discount, and the Plexus mixing weight are
// chosen somewhere other than the number being reported.
//
// VOCABULARY AND OOV, stated exactly, because perplexity without them is
// meaningless. The vocabulary is CLOSED and derived from the training split
// alone: every training type with count >= min_count (default 3), plus <unk>
// and </s>. Training types below the threshold are rewritten to <unk> before
// any counting, so <unk> owns real probability mass. Held-out tokens outside
// the vocabulary are rewritten to <unk> as well and ARE scored -- they are not
// skipped, and the OOV rate is printed. <s> is a context-only symbol, never a
// prediction target, and is NOT counted in V. Sentence ends are targets: </s>
// is a word the model has to predict. No model can return zero: add-k has k,
// Witten-Bell and Kneser-Ney interpolate down to a 1/V floor, and the Plexus
// readouts are mixed with the unigram. So no infinities, and the sum-to-one
// check confirms the mass is where it is claimed to be.
//
// WHAT THIS HARNESS CANNOT SEE:
//   - Whether a sample is TRUE, or even grammatical. There is no judge here.
//     The samples are printed unedited so a human can look; that is the whole
//     mechanism.
//   - How this compares to a neural language model. There is no LSTM and no
//     transformer in this file. A small one would beat every row in the table.
//     The comparison here is n-gram against Plexus, not Khora against the field.
//   - Anything about punctuation, capitalisation, or numbers. The shared
//     tokenizer lowercases and drops everything non-alphanumeric, so these
//     perplexities are over a lowercased alnum word stream and are NOT
//     comparable to published Penn Treebank or WikiText numbers, which use
//     different vocabularies and different preprocessing.
//   - Out-of-domain generalisation. Held-out books are held out, but the whole
//     Reservoir is public-domain prose of roughly one era. This measures
//     held-out, not out-of-distribution.
//   - The SHIPPED 84k-node Plexus. That graph read every book in the catalogue,
//     including these held-out ones, so scoring it here would be leakage. The
//     Plexus measured below is built inside this process from the training
//     split only -- twice, once at the shipped max_degree of 160 and once at
//     4000, so that "bad readout" and "too few edges kept" can be told apart.

#include "khora/lexicon/lexicon.hpp"
#include "khora/plexus/plexus.hpp"
#include "khora/reservoir/reservoir.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using Id    = std::uint32_t;
using Clock = std::chrono::steady_clock;

constexpr int kOrder = 5;   // maximum n
constexpr Id  kUnk   = 0;   // vocabulary ids 0 and 1 are reserved
constexpr Id  kEos   = 1;

double secs(Clock::time_point a) {
    return std::chrono::duration<double>(Clock::now() - a).count();
}

// 64-bit chained hash of an n-gram. Chained so that the hash of the k-gram
// ending at position i is one mix of the (k-1)-gram ending at i-1: that
// recurrence is what makes all five orders cost five mixes per token instead
// of fifteen. Collisions are the price of not storing the tokens themselves;
// at ~10^7 distinct keys in 2^64 the expected number of colliding pairs is
// ~3e-6, which is below every difference reported here.
inline std::uint64_t hmix(std::uint64_t h, std::uint64_t x) {
    h += (x + 1) * 0x9e3779b97f4a7c15ull;
    h ^= h >> 30; h *= 0xbf58476d1ce4e5b9ull;
    h ^= h >> 27; h *= 0x94d049bb133111ebull;
    h ^= h >> 31;
    return h;
}

// --- COUNTS -----------------------------------------------------------------
//
// One cell per distinct n-gram, holding the six numbers every smoothing in
// this file needs. Three of them describe the n-gram AS A HISTORY (the stats
// of the (n+1)-grams that extend it) which is why there is no separate context
// table: a context is just a shorter n-gram.
//
// cnt        c(g)            raw count
// ctx_tot    sum_w c(g.w)    total count of g used as a history
// ctx_types  |{w: c(g.w)>0}| distinct continuations of g   -- N1+(g.)
// cont       |{u: c(u.g)>0}| distinct LEFT extensions of g -- N1+(.g), Kneser-Ney
// cctx_tot   sum_w cont(g.w)
// cctx_types |{w: cont(g.w)>0}|
struct Cell {
    std::uint32_t cnt = 0, ctx_tot = 0, ctx_types = 0;
    std::uint32_t cont = 0, cctx_tot = 0, cctx_types = 0;
};

struct Counts {
    std::unordered_map<std::uint64_t, Cell> t[kOrder + 1];
    Cell     root;      // the empty gram, as a history: unigram totals live here
    std::uint32_t V = 0;

    const Cell* find(int k, std::uint64_t h) const {
        auto it = t[k].find(h);
        return (it == t[k].end()) ? nullptr : &it->second;
    }
};

// --- CORPUS -----------------------------------------------------------------

struct Split {
    std::vector<Id>           s;      // token stream, <s>-padded, </s>-terminated
    std::vector<std::uint8_t> tgt;    // 1 where the position is a prediction target
    std::size_t               targets = 0, sentences = 0, books = 0, oov = 0;
};

// A sentence-per-line store of one split, kept as strings until the vocabulary
// is known (the vocabulary comes from the training split, so held-out text
// cannot be turned into ids before training text has been counted).
using Text = std::vector<std::vector<std::string>>;

// --- MODELS -----------------------------------------------------------------

enum Kind { M_UNIFORM, M_ADDK, M_WB, M_KN, M_PLEX };

struct Model {
    Kind        kind  = M_UNIFORM;
    int         order = 1;
    double      p     = 0.0;   // add-k's k, Kneser-Ney's D, or the Plexus lambda
    int         readout = 0;   // Plexus only: 0 cooc, 1 ppmi, 2 associates
    std::string name;
};

// A Plexus row, re-keyed into LM ids and sorted so a candidate can be found by
// binary search. Three weightings of the same edges: the raw evidence, the
// PPMI that affinity() returns, and the confidence-weighted+filtered score
// that associates() ranks by.
struct PlexRow {
    std::vector<Id>    nb;              // ascending, for binary search
    std::vector<float> w[3];
    double             sum[3] = {0.0, 0.0, 0.0};
    std::vector<Id>    top;             // the neighbours the graph likes most
};

// Everything a probability query needs that does not depend on the candidate.
// The history cells are hoisted out here because they are identical for every
// candidate word at a position, and the accuracy loop asks about a thousand
// candidates per position.
struct Ctx {
    std::uint64_t prev[kOrder] = {0};   // prev[m] = hash of the m-gram ending at i-1
    const Cell*   h[kOrder]    = {nullptr};
    Id            last         = 0;     // the immediately preceding token
    bool          last_real    = false; // false for <s>: the Plexus has no such node
};

struct Engine {
    const Counts*               C   = nullptr;
    const std::vector<double>*  uni = nullptr;    // add-0.1 unigram, the mixing floor
    const std::vector<PlexRow>* rows[2] = {nullptr, nullptr};   // 0 = shipped graph, 1 = generous

    void set_ctx(Ctx& c) const {
        c.h[0] = &C->root;
        for (int m = 1; m < kOrder; ++m) c.h[m] = C->find(m, c.prev[m]);
    }

    // Witten-Bell: interpolate by how many distinct continuations the history
    // has. A history seen in many different ways trusts itself; one seen the
    // same way every time defers.
    double wb(int m, const Ctx& c, const std::uint64_t* g) const {
        if (m == 0) return 1.0 / C->V;
        const Cell* hc = c.h[m - 1];
        const double T   = hc ? hc->ctx_types : 0.0;
        const double tot = hc ? hc->ctx_tot   : 0.0;
        const double low = wb(m - 1, c, g);
        if (tot + T <= 0.0) return low;
        const Cell* gc = C->find(m, g[m]);
        return ((gc ? gc->cnt : 0u) + T * low) / (tot + T);
    }

    // Kneser-Ney's lower orders run on CONTINUATION counts, not raw counts:
    // how many distinct words precede this gram, not how often it occurs. That
    // is the whole idea -- "francisco" is frequent but follows only "san", so
    // its continuation count is 1 and it is not a plausible generic next word.
    double kn_low(int m, const Ctx& c, const std::uint64_t* g, double D) const {
        if (m == 0) return 1.0 / C->V;
        const Cell* hc = c.h[m - 1];
        const double tot = hc ? hc->cctx_tot   : 0.0;
        const double ty  = hc ? hc->cctx_types : 0.0;
        const double low = kn_low(m - 1, c, g, D);
        if (tot <= 0.0) return low;
        const Cell* gc = C->find(m, g[m]);
        const double cc = gc ? gc->cont : 0u;
        return ((cc > D ? cc - D : 0.0) + D * ty * low) / tot;
    }

    double kn(int n, const Ctx& c, const std::uint64_t* g, double D) const {
        const Cell* hc = c.h[n - 1];
        const double tot = hc ? hc->ctx_tot   : 0.0;
        const double ty  = hc ? hc->ctx_types : 0.0;
        const double low = kn_low(n - 1, c, g, D);
        if (tot <= 0.0) return low;
        const Cell* gc = C->find(n, g[n]);
        const double cn = gc ? gc->cnt : 0u;
        return ((cn > D ? cn - D : 0.0) + D * ty * low) / tot;
    }

    // The Plexus as a conditional distribution. It is not one: the graph is
    // SYMMETRIC over a +/-3 window, so its edge weight answers "does w occur
    // near prev", which cannot tell "prev w" from "w prev". Mixing with the
    // unigram is what makes it a distribution at all -- the graph has no node
    // for <s>, none for <unk>, and (in the associates readout) none for any
    // function word, so on its own it assigns zero to targets it is certain to
    // meet.
    double plex_q(const Ctx& c, Id w, int code) const {
        if (!c.last_real) return -1.0;
        const int ro = code % 3, gr = code / 3;
        const PlexRow& r = (*rows[gr])[c.last];
        if (r.sum[ro] <= 0.0) return -1.0;
        const auto it = std::lower_bound(r.nb.begin(), r.nb.end(), w);
        if (it == r.nb.end() || *it != w) return 0.0;
        return r.w[ro][static_cast<std::size_t>(it - r.nb.begin())] / r.sum[ro];
    }

    double prob(const Model& m, const Ctx& c, const std::uint64_t* g, Id w) const {
        switch (m.kind) {
        case M_UNIFORM: return 1.0 / C->V;
        case M_ADDK: {
            const Cell* hc = c.h[m.order - 1];
            const Cell* gc = C->find(m.order, g[m.order]);
            return ((gc ? gc->cnt : 0u) + m.p)
                 / ((hc ? hc->ctx_tot : 0u) + m.p * C->V);
        }
        case M_WB: return wb(m.order, c, g);
        case M_KN: return kn(m.order, c, g, m.p);
        case M_PLEX: {
            const double q = plex_q(c, w, m.readout);
            const double u = (*uni)[w];
            return (q < 0.0) ? u : m.p * q + (1.0 - m.p) * u;
        }
        }
        return 1.0 / C->V;
    }
};

// The n-gram hashes for one candidate word under one context.
inline void grams(const Ctx& c, Id w, std::uint64_t* g) {
    g[0] = 0;
    for (int m = 1; m <= kOrder; ++m) g[m] = hmix(c.prev[m - 1], w);
}

// A 95% Wilson interval on a proportion. Top-1 accuracy is measured on a
// subsample, so the interval is the difference between a number and a claim.
std::pair<double, double> wilson(std::size_t hits, std::size_t n) {
    if (n == 0) return {0.0, 0.0};
    const double z = 1.96, ph = static_cast<double>(hits) / static_cast<double>(n);
    const double d = 1.0 + z * z / static_cast<double>(n);
    const double ctr = ph + z * z / (2.0 * static_cast<double>(n));
    const double mrg = z * std::sqrt(ph * (1.0 - ph) / static_cast<double>(n)
                                     + z * z / (4.0 * static_cast<double>(n) * static_cast<double>(n)));
    return {100.0 * (ctr - mrg) / d, 100.0 * (ctr + mrg) / d};
}

} // namespace

int main(int argc, char** argv) {
    const std::string dir        = (argc > 1) ? argv[1] : "data/reservoir";
    const std::size_t train_cap  = (argc > 2) ? std::stoul(argv[2]) : 4500000;   // no cap at 8M corpus
    const std::size_t held_cap   = (argc > 3) ? std::stoul(argv[3]) : 900000;    // per held-out split
    const std::uint32_t min_count = (argc > 4) ? static_cast<std::uint32_t>(std::stoul(argv[4])) : 3;
    const std::size_t acc_sample = (argc > 5) ? std::stoul(argv[5]) : 10000;

    std::printf("A LANGUAGE MODEL FOR KHORA, AND A SCORE FOR THE ONE IT HAS\n");
    std::printf("==========================================================\n\n");

    khora::reservoir::Reservoir res(dir);
    res.load_catalog();
    auto cat = res.catalog();
    if (cat.empty()) { std::printf("  empty catalog at %s\n", dir.c_str()); return 0; }
    std::sort(cat.begin(), cat.end(),
              [](const khora::reservoir::Tome& a, const khora::reservoir::Tome& b) {
                  return a.title < b.title;
              });

    // --- READ, AND SPLIT BY BOOK --------------------------------------------
    const auto t_read = Clock::now();
    Text tr_text, dev_text, te_text;
    std::size_t tr_books = 0, dev_books = 0, te_books = 0;
    std::size_t tr_all = 0, dev_all = 0, te_all = 0;   // tokens before any capping
    {
        std::size_t i = 0;
        for (const auto& t : cat) {
            auto text = res.read(t.title);
            if (!text || text->size() < 40000) continue;
            const std::size_t bucket = i % 5;   // 0 test, 1 dev, 2..4 train
            ++i;
            Text* dst = (bucket == 0) ? &te_text : (bucket == 1) ? &dev_text : &tr_text;
            std::size_t* nb = (bucket == 0) ? &te_books : (bucket == 1) ? &dev_books : &tr_books;
            std::size_t* nt = (bucket == 0) ? &te_all   : (bucket == 1) ? &dev_all   : &tr_all;
            ++(*nb);
            for (auto& sent : khora::lexicon::tokenize_sentences(*text)) {
                if (sent.empty()) continue;
                *nt += sent.size();
                dst->push_back(std::move(sent));
            }
        }
    }
    std::printf("corpus  %zu train books (%zu tokens), %zu dev (%zu), %zu test (%zu)"
                "   [read+tokenize %.1fs]\n",
                tr_books, tr_all, dev_books, dev_all, te_books, te_all, secs(t_read));

    // Capping by DROPPING WHOLE SENTENCES at a uniform rate, not by truncating
    // the book list. Truncating would hand the model a handful of authors; no
    // n-gram here crosses a sentence boundary (every sentence is <s>-padded
    // afresh), so dropping sentences costs nothing but corpus size.
    auto subsample = [](Text& v, std::size_t have, std::size_t cap, std::uint64_t seed) {
        if (have <= cap) return;
        std::mt19937_64 rng(seed);
        const double keep = static_cast<double>(cap) / static_cast<double>(have);
        Text out;
        out.reserve(static_cast<std::size_t>(v.size() * keep) + 16);
        for (auto& s : v)
            if (std::uniform_real_distribution<double>(0.0, 1.0)(rng) < keep)
                out.push_back(std::move(s));
        v.swap(out);
    };
    subsample(tr_text,  tr_all,  train_cap, 20260825ull);
    subsample(dev_text, dev_all, held_cap,  20260826ull);
    subsample(te_text,  te_all,  held_cap,  20260827ull);

    // --- VOCABULARY ----------------------------------------------------------
    std::unordered_map<std::string, std::uint64_t> raw;
    for (const auto& s : tr_text) for (const auto& w : s) ++raw[w];

    std::unordered_map<std::string, Id> vid;
    std::vector<std::string> vword = {"<unk>", "</s>"};
    vid.emplace("<unk>", kUnk); vid.emplace("</s>", kEos);
    for (const auto& [w, c] : raw) if (c >= min_count) { vid.emplace(w, static_cast<Id>(vword.size())); vword.push_back(w); }
    const Id V   = static_cast<Id>(vword.size());
    const Id kBos = V;    // context-only, deliberately outside V
    std::printf("vocab   %zu training types seen, %u kept at count>=%u (V includes <unk> and </s>);"
                " <s> is context-only and not in V\n",
                raw.size(), V, min_count);

    // --- STREAMS -------------------------------------------------------------
    auto build = [&](const Text& txt, Split& sp, std::size_t books) {
        sp.books = books;
        std::size_t n = 0;
        for (const auto& s : txt) n += s.size() + kOrder;
        sp.s.reserve(n); sp.tgt.reserve(n);
        for (const auto& sent : txt) {
            ++sp.sentences;
            for (int k = 0; k < kOrder - 1; ++k) { sp.s.push_back(kBos); sp.tgt.push_back(0); }
            for (const auto& w : sent) {
                auto it = vid.find(w);
                if (it == vid.end()) { sp.s.push_back(kUnk); ++sp.oov; }
                else                   sp.s.push_back(it->second);
                sp.tgt.push_back(1); ++sp.targets;
            }
            sp.s.push_back(kEos); sp.tgt.push_back(1); ++sp.targets;
        }
    };
    Split TR, DEV, TE;
    build(tr_text, TR, tr_books);
    build(dev_text, DEV, dev_books);
    build(te_text, TE, te_books);
    std::printf("scored  train %zu targets / %zu sentences | dev %zu (%.2f%% OOV) | test %zu (%.2f%% OOV)\n",
                TR.targets, TR.sentences,
                DEV.targets, DEV.targets ? 100.0 * DEV.oov / DEV.targets : 0.0,
                TE.targets,  TE.targets  ? 100.0 * TE.oov  / TE.targets  : 0.0);
    std::printf("        (OOV = held-out tokens rewritten to <unk>. They are SCORED, not skipped.\n"
                "         </s> is a target too: the model must predict where a sentence ends.)\n");

    // --- COUNT ---------------------------------------------------------------
    const auto t_count = Clock::now();
    Counts C; C.V = V;
    for (int k = 3; k <= kOrder; ++k) C.t[k].reserve(TR.targets);
    C.t[2].reserve(TR.targets / 2); C.t[1].reserve(V * 2);
    {
        std::uint64_t prev[kOrder + 1] = {0}, cur[kOrder + 1] = {0};
        bool is_new[kOrder + 2] = {false};
        for (std::size_t i = 0; i < TR.s.size(); ++i) {
            cur[0] = 0;
            for (int k = 1; k <= kOrder; ++k) cur[k] = hmix(prev[k - 1], TR.s[i]);
            if (TR.tgt[i]) {
                for (int k = 1; k <= kOrder; ++k) {
                    Cell& g = C.t[k][cur[k]];
                    is_new[k] = (g.cnt == 0);
                    ++g.cnt;
                    Cell& h = (k == 1) ? C.root : C.t[k - 1][prev[k - 1]];
                    ++h.ctx_tot;
                    if (is_new[k]) ++h.ctx_types;
                }
                // Continuation counts, accumulated as deltas: the first time a
                // (k+1)-gram is ever seen is the moment its k-gram suffix gains
                // one distinct left extension.
                for (int k = 1; k < kOrder; ++k) {
                    if (!is_new[k + 1]) continue;
                    Cell& g = C.t[k][cur[k]];
                    const bool first = (g.cont == 0);
                    ++g.cont;
                    Cell& h = (k == 1) ? C.root : C.t[k - 1][prev[k - 1]];
                    ++h.cctx_tot;
                    if (first) ++h.cctx_types;
                }
            }
            for (int k = 0; k <= kOrder; ++k) prev[k] = cur[k];
        }
    }
    std::printf("counts  ");
    for (int k = 1; k <= kOrder; ++k) std::printf("%d-gram %zu  ", k, C.t[k].size());
    std::printf("  [%.1fs]\n", secs(t_count));

    // The unigram used as the Plexus mixing floor and as a dumb baseline in its
    // own right. add-0.1 rather than MLE so a held-out <unk> can never be zero.
    std::vector<double> uni(V, 0.0);
    {
        std::uint64_t g1;
        const double denom = C.root.ctx_tot + 0.1 * V;
        for (Id w = 0; w < V; ++w) {
            g1 = hmix(0, w);
            const Cell* c = C.find(1, g1);
            uni[w] = ((c ? c->cnt : 0u) + 0.1) / denom;
        }
    }

    // Bigram successors, by count descending. Used as the candidate pool for
    // argmax and for sampling. It is a SUPERSET of every word with a nonzero
    // n-gram count in any context ending in `last`: an n-gram (.. last w) can
    // only exist if the bigram (last w) does.
    const auto t_succ = Clock::now();
    std::vector<std::vector<std::pair<Id, std::uint32_t>>> succ(V + 1);
    {
        std::unordered_map<std::uint64_t, std::uint32_t> big;
        big.reserve(TR.targets / 2);
        for (std::size_t i = 1; i < TR.s.size(); ++i)
            if (TR.tgt[i]) ++big[(static_cast<std::uint64_t>(TR.s[i - 1]) << 32) | TR.s[i]];
        for (const auto& [k, c] : big)
            succ[static_cast<Id>(k >> 32)].emplace_back(static_cast<Id>(k & 0xffffffffu), c);
        for (auto& v : succ)
            std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) {
                return a.second != b.second ? a.second > b.second : a.first < b.first; });
    }
    // The commonest types, standing in for the tail that pure backoff mass
    // could reach. Words in neither pool have zero count in every order >= 2,
    // so they can only win a position by backoff, and the backoff ordering is
    // led by exactly these words.
    std::vector<Id> common(V);
    for (Id w = 0; w < V; ++w) common[w] = w;
    std::sort(common.begin(), common.end(),
              [&](Id a, Id b) { return uni[a] > uni[b]; });
    common.resize(std::min<std::size_t>(common.size(), 300));
    std::printf("succ    %zu distinct bigram histories  [%.1fs]\n",
                succ.size(), secs(t_succ));

    // --- THE PLEXUS, BUILT ON THE TRAINING SPLIT ONLY ------------------------
    //
    // The shipped graph read every book in the catalogue. Using it would score a
    // model on its own training data, so this rebuilds one here, from the
    // training split. observe() is called in large blocks rather than once per
    // sentence because it recomputes an O(V) partition function on every call;
    // per-sentence that alone would dominate the run.
    //
    // TWO graphs, because "the Plexus is a bad language model" and "the Plexus
    // is pruned to 160 associates per node" are different claims and only one of
    // them is fixable by spending memory. Graph 0 is the shipped default; graph
    // 1 keeps 25x as many edges per node. If graph 1 is not much better, the
    // memory bound is not what is wrong.
    std::vector<PlexRow> rows[2];
    std::size_t assoc_empty[2] = {0, 0};
    std::size_t plex_nodes[2] = {0, 0}, plex_deg[2] = {0, 0};
    unsigned long long plex_edges[2] = {0, 0};

    auto build_plexus = [&](int slot, std::size_t max_degree) {
        const auto t0 = Clock::now();
        khora::plexus::Plexus plex;
        plex.set_max_degree(max_degree);
        std::vector<std::string> buf;
        std::size_t emitted = 0;
        for (const auto& sent : tr_text) {
            for (const auto& w : sent) buf.push_back(vid.count(w) ? w : "<unk>");
            buf.push_back("</s>");     // the graph is at least told where sentences end
            if (buf.size() >= 400000) { plex.observe(buf, 3); emitted += buf.size(); buf.clear(); }
        }
        if (!buf.empty()) { plex.observe(buf, 3); emitted += buf.size(); }
        plex.prune_all();
        plex_nodes[slot] = plex.vocabulary_size();
        plex_edges[slot] = plex.edge_count();
        plex_deg[slot]   = plex.max_degree();

        // Re-key the graph into LM ids with the three readouts precomputed. The
        // associates() readout reproduces that function's own filters exactly:
        // co-occurrence < 3 is noise, and a neighbour above 0.6% of all tokens
        // is a function word and is dropped. That filter is why the readout
        // cannot emit "the".
        rows[slot].assign(V + 1, PlexRow{});
        const double stop_occ = static_cast<double>(plex.total_tokens()) * 0.006;
        std::vector<Id> p2lm(plex.vocabulary_size(), V);   // V = "not in LM vocab"
        for (std::size_t p = 0; p < plex.vocabulary_size(); ++p) {
            auto it = vid.find(std::string(plex.node_name(p)));
            if (it != vid.end()) p2lm[p] = it->second;
        }
        for (std::size_t p = 0; p < plex.vocabulary_size(); ++p) {
            const Id lm = p2lm[p];
            if (lm >= V) continue;
            const std::string a(plex.node_name(p));
            PlexRow& r = rows[slot][lm];
            for (const auto& [nb, c] : plex.neighbours(p)) {
                const Id lnb = p2lm[nb];
                if (lnb >= V) continue;
                const double pm = plex.affinity(a, plex.node_name(nb));  // 0 if cooc < 3
                const double ev = pm * std::log2(1.0 + static_cast<double>(c));
                const bool   fn = static_cast<double>(plex.occurrences(plex.node_name(nb))) > stop_occ;
                r.nb.push_back(lnb);
                r.w[0].push_back(static_cast<float>(c));
                r.w[1].push_back(static_cast<float>(pm));
                r.w[2].push_back(static_cast<float>((c >= 3 && !fn) ? ev : 0.0));
            }
            std::vector<std::size_t> ord(r.nb.size());
            for (std::size_t j = 0; j < ord.size(); ++j) ord[j] = j;
            std::sort(ord.begin(), ord.end(),
                      [&](std::size_t x, std::size_t y) { return r.nb[x] < r.nb[y]; });
            PlexRow srt;
            srt.nb.reserve(ord.size());
            for (int k = 0; k < 3; ++k) srt.w[k].reserve(ord.size());
            for (std::size_t j : ord) {
                srt.nb.push_back(r.nb[j]);
                for (int k = 0; k < 3; ++k) { srt.w[k].push_back(r.w[k][j]); srt.sum[k] += r.w[k][j]; }
            }
            r = std::move(srt);
            if (r.sum[2] <= 0.0) ++assoc_empty[slot];
            // The shortlist the accuracy search will add to its candidate pool:
            // ranked by the best of the three normalised readouts, so no readout
            // is judged on a pool that excludes its own favourites.
            {
                std::vector<std::size_t> o(r.nb.size());
                for (std::size_t j = 0; j < o.size(); ++j) o[j] = j;
                auto score = [&](std::size_t j) {
                    double m = 0.0;
                    for (int k = 0; k < 3; ++k)
                        if (r.sum[k] > 0.0) m = std::max(m, r.w[k][j] / r.sum[k]);
                    return m;
                };
                const std::size_t keep = std::min<std::size_t>(o.size(), 400);
                std::partial_sort(o.begin(), o.begin() + keep, o.end(),
                                  [&](std::size_t x, std::size_t y) { return score(x) > score(y); });
                r.top.reserve(keep);
                for (std::size_t j = 0; j < keep; ++j) r.top.push_back(r.nb[o[j]]);
            }
        }
        std::printf("plexus  graph %d: %zu nodes, %llu edges over %zu tokens, max_degree %zu  [%.1fs]\n",
                    slot, plex_nodes[slot], plex_edges[slot], emitted, plex_deg[slot], secs(t0));
    };
    build_plexus(0, 160);      // exactly what Khora runs
    build_plexus(1, 4000);     // the same corpus, 25x the edge budget
    std::printf("        (smaller than the shipped 84k-node graph: that one read all 57 books\n"
                "         and every token, this one reads 34 books with rare types folded into\n"
                "         <unk>. Same code, same window, same PMI -- fewer nodes.)\n");

    // --- SCORING -------------------------------------------------------------
    Engine E; E.C = &C; E.uni = &uni; E.rows[0] = &rows[0]; E.rows[1] = &rows[1];

    auto ppl = [&](const Model& m, const Split& sp) {
        double ll = 0.0; std::size_t n = 0;
        Ctx c; std::uint64_t cur[kOrder + 1], g[kOrder + 1];
        std::uint64_t prev[kOrder + 1] = {0};
        Id last = kBos;
        for (std::size_t i = 0; i < sp.s.size(); ++i) {
            cur[0] = 0;
            for (int k = 1; k <= kOrder; ++k) cur[k] = hmix(prev[k - 1], sp.s[i]);
            if (sp.tgt[i]) {
                for (int k = 0; k < kOrder; ++k) c.prev[k] = prev[k];
                c.last = (last == kBos) ? 0 : last;
                c.last_real = (last != kBos);
                E.set_ctx(c);
                grams(c, sp.s[i], g);
                const double p = E.prob(m, c, g, sp.s[i]);
                ll += std::log(p > 0.0 ? p : 1e-300);
                ++n;
            }
            for (int k = 0; k <= kOrder; ++k) prev[k] = cur[k];
            last = sp.s[i];
        }
        return n ? std::exp(-ll / static_cast<double>(n)) : 0.0;
    };

    // SUM-TO-ONE. Every smoothing above is claimed to be a proper distribution.
    // A missing normaliser shows up as a better perplexity and nothing else, so
    // it is checked rather than asserted in a comment: sum P(w|h) over all V
    // words at real held-out contexts.
    {
        std::printf("\n--- sum-to-one check (a smoothing bug reads as a good perplexity) ---\n");
        std::vector<Ctx> probes;
        {
            std::uint64_t prev[kOrder + 1] = {0}, cur[kOrder + 1];
            Id last = kBos; std::size_t seen = 0;
            for (std::size_t i = 0; i < DEV.s.size() && probes.size() < 3; ++i) {
                cur[0] = 0;
                for (int k = 1; k <= kOrder; ++k) cur[k] = hmix(prev[k - 1], DEV.s[i]);
                if (DEV.tgt[i] && (++seen % 977) == 0) {
                    Ctx c;
                    for (int k = 0; k < kOrder; ++k) c.prev[k] = prev[k];
                    c.last = (last == kBos) ? 0 : last;
                    c.last_real = (last != kBos);
                    E.set_ctx(c);
                    probes.push_back(c);
                }
                for (int k = 0; k <= kOrder; ++k) prev[k] = cur[k];
                last = DEV.s[i];
            }
        }
        const Model checks[] = {
            {M_ADDK, 3, 0.1, 0, "add-0.1 n=3"}, {M_WB, 5, 0.0, 0, "witten-bell n=5"},
            {M_KN, 5, 0.75, 0, "kneser-ney n=5"}, {M_PLEX, 1, 0.7, 0, "plexus cooc l=0.7"},
            {M_PLEX, 1, 0.7, 2, "plexus assoc l=0.7"},
        };
        for (const auto& m : checks) {
            double worst = 0.0;
            for (const auto& c : probes) {
                double s = 0.0; std::uint64_t g[kOrder + 1];
                for (Id w = 0; w < V; ++w) { grams(c, w, g); s += E.prob(m, c, g, w); }
                worst = std::max(worst, std::abs(s - 1.0));
            }
            std::printf("  %-22s max |sum_w P(w|h) - 1| over %zu contexts = %.2e  %s\n",
                        m.name.c_str(), probes.size(), worst, worst < 1e-6 ? "ok" : "*** BROKEN ***");
        }
    }

    // --- TUNE ON DEV ---------------------------------------------------------
    std::printf("\n--- chosen on DEV, reported on TEST (so no number below is its own tuning set) ---\n");
    std::vector<Model> models;
    models.push_back({M_UNIFORM, 0, 0.0, 0, "uniform over V"});

    std::printf("  add-k        ");
    for (const double k : {1.0, 0.1, 0.01, 0.001, 0.0001}) std::printf("   k=%-7.4g", k);
    std::printf("\n");
    for (int n = 1; n <= kOrder; ++n) {
        double best = 1e300, bk = 0.1;
        std::printf("    n=%d       ", n);
        for (const double k : {1.0, 0.1, 0.01, 0.001, 0.0001}) {
            const double p = ppl({M_ADDK, n, k, 0, ""}, DEV);
            std::printf(" %11.1f", p);
            if (p < best) { best = p; bk = k; }
        }
        std::printf("   -> k=%g\n", bk);
        char nm[64]; std::snprintf(nm, sizeof nm, "add-k  n=%d (k=%g)", n, bk);
        models.push_back({M_ADDK, n, bk, 0, nm});
    }

    std::printf("  kneser-ney   ");
    for (const double d : {0.5, 0.7, 0.85, 0.95, 0.99}) std::printf("   D=%-7.3g", d);
    std::printf("\n");
    for (int n = 1; n <= kOrder; ++n) {
        double best = 1e300, bd = 0.75;
        std::printf("    n=%d       ", n);
        for (const double d : {0.5, 0.7, 0.85, 0.95, 0.99}) {
            const double p = ppl({M_KN, n, d, 0, ""}, DEV);
            std::printf(" %11.1f", p);
            if (p < best) { best = p; bd = d; }
        }
        std::printf("   -> D=%g\n", bd);
        char nm[64]; std::snprintf(nm, sizeof nm, "kneser-ney n=%d (D=%g)", n, bd);
        models.push_back({M_KN, n, bd, 0, nm});
    }
    for (int n = 1; n <= kOrder; ++n) {
        char nm[64]; std::snprintf(nm, sizeof nm, "witten-bell n=%d", n);
        models.push_back({M_WB, n, 0.0, 0, nm});
    }

    // The Plexus mixing weight, swept in the open. lambda=0 IS the unigram, so
    // the sweep says exactly how much the graph is worth on top of counting.
    static const char* ro_name[3] = {"raw co-occurrence", "PPMI (affinity())", "associates()"};
    std::printf("\n  plexus mixing weight lambda (lambda=0 IS the unigram, exactly):\n");
    std::printf("    graph / readout                  |");
    for (const double l : {0.0, 0.1, 0.2, 0.3, 0.45, 0.6, 0.8, 0.95}) std::printf("  l=%-5.2f", l);
    std::printf(" | chosen\n");
    for (int gr = 0; gr < 2; ++gr) {
        for (int ro = 0; ro < 3; ++ro) {
            double best = 1e300, bl = 0.0;
            char lbl[64];
            std::snprintf(lbl, sizeof lbl, "deg %zu / %s", plex_deg[gr], ro_name[ro]);
            std::printf("    %-32s |", lbl);
            for (const double l : {0.0, 0.1, 0.2, 0.3, 0.45, 0.6, 0.8, 0.95}) {
                const double p = ppl({M_PLEX, 1, l, ro + 3 * gr, ""}, DEV);
                std::printf(" %8.1f", p);
                if (p < best) { best = p; bl = l; }
            }
            std::printf(" | l=%.2f\n", bl);
            char nm[96];
            std::snprintf(nm, sizeof nm, "PLEXUS deg%zu %s (l=%.2f)", plex_deg[gr], ro_name[ro], bl);
            models.push_back({M_PLEX, 1, bl, ro + 3 * gr, nm});
        }
    }

    // --- PERPLEXITY ON TEST ---------------------------------------------------
    std::printf("\n=== HELD-OUT PERPLEXITY, %zu test books, %zu scored tokens ===\n",
                TE.books, TE.targets);
    std::printf("  model                                      |  test PPL  |   dev PPL  | vs uniform | vs unigram\n");
    std::printf("  -------------------------------------------+------------+------------+------------+-----------\n");
    std::vector<double> test_ppl(models.size(), 0.0), dev_ppl(models.size(), 0.0);
    double ppl_uniform = 0.0, ppl_unigram = 0.0;
    for (std::size_t i = 0; i < models.size(); ++i) {
        test_ppl[i] = ppl(models[i], TE);
        dev_ppl[i]  = ppl(models[i], DEV);
        if (models[i].kind == M_UNIFORM) ppl_uniform = test_ppl[i];
        if (models[i].kind == M_ADDK && models[i].order == 1) ppl_unigram = test_ppl[i];
    }
    for (std::size_t i = 0; i < models.size(); ++i)
        std::printf("  %-42s | %10.1f | %10.1f |   %6.2fx   |  %6.2fx\n",
                    models[i].name.c_str(), test_ppl[i], dev_ppl[i],
                    ppl_uniform / test_ppl[i], ppl_unigram / test_ppl[i]);
    std::printf("  (uniform is exactly V = %u by construction. 'vs unigram' > 1 means better\n"
                "   than counting words and ignoring order.)\n", V);

    // --- NEXT-TOKEN ACCURACY --------------------------------------------------
    //
    // The argmax is searched over a candidate pool: the previous token's bigram
    // successors (capped at 1000, ordered by count), the 400 strongest Plexus
    // neighbours from each graph, and the 300 commonest types. The bigram
    // successors alone already hold every word with a nonzero n-gram count in
    // this context -- an n-gram (.. last w) cannot exist unless the bigram
    // (last w) does -- but they are a pool the Plexus never chose, so its own
    // favourites are added too and every model is scored over the same union.
    // A word in none of the three has probability from backoff alone. This is
    // an approximation, stated as one, not a proof that the argmax is in it.
    struct Acc { std::size_t n = 0, top1 = 0, top10 = 0; };
    std::vector<Acc> acc(models.size());
    const auto t_acc = Clock::now();
    {
        std::vector<std::size_t> pos;
        for (std::size_t i = 0; i < TE.s.size(); ++i) if (TE.tgt[i]) pos.push_back(i);
        std::mt19937_64 rng(20260828ull);
        std::shuffle(pos.begin(), pos.end(), rng);
        if (pos.size() > acc_sample) pos.resize(acc_sample);
        std::sort(pos.begin(), pos.end());

        // Walk the stream once, stopping at the sampled positions.
        std::vector<Ctx> ctxs(pos.size());
        std::vector<Id>  tgt(pos.size());
        {
            std::uint64_t prev[kOrder + 1] = {0}, cur[kOrder + 1];
            Id last = kBos; std::size_t p = 0;
            for (std::size_t i = 0; i < TE.s.size() && p < pos.size(); ++i) {
                cur[0] = 0;
                for (int k = 1; k <= kOrder; ++k) cur[k] = hmix(prev[k - 1], TE.s[i]);
                if (i == pos[p]) {
                    Ctx& c = ctxs[p];
                    for (int k = 0; k < kOrder; ++k) c.prev[k] = prev[k];
                    c.last = (last == kBos) ? 0 : last;
                    c.last_real = (last != kBos);
                    E.set_ctx(c);
                    tgt[p] = TE.s[i];
                    ++p;
                }
                for (int k = 0; k <= kOrder; ++k) prev[k] = cur[k];
                last = TE.s[i];
            }
        }
        std::vector<Id> pool;
        std::vector<std::pair<double, Id>> scored;
        std::uint64_t g[kOrder + 1];
        for (std::size_t p = 0; p < ctxs.size(); ++p) {
            const Ctx& c = ctxs[p];
            pool.clear();
            const Id prev_tok = c.last_real ? c.last : kBos;
            const auto& sv = succ[prev_tok];
            for (std::size_t j = 0; j < sv.size() && j < 1000; ++j) pool.push_back(sv[j].first);
            for (const Id w : common) pool.push_back(w);
            if (c.last_real)
                for (int gr = 0; gr < 2; ++gr)
                    for (const Id w : rows[gr][c.last].top) pool.push_back(w);
            std::sort(pool.begin(), pool.end());
            pool.erase(std::unique(pool.begin(), pool.end()), pool.end());
            for (std::size_t mi = 0; mi < models.size(); ++mi) {
                if (models[mi].kind == M_UNIFORM) { ++acc[mi].n; continue; }  // 1/V everywhere
                scored.clear();
                for (const Id w : pool) { grams(c, w, g); scored.emplace_back(E.prob(models[mi], c, g, w), w); }
                const std::size_t keep = std::min<std::size_t>(10, scored.size());
                std::partial_sort(scored.begin(), scored.begin() + keep, scored.end(),
                                  [](const auto& a, const auto& b) {
                                      return a.first != b.first ? a.first > b.first : a.second < b.second; });
                ++acc[mi].n;
                if (scored[0].second == tgt[p]) ++acc[mi].top1;
                for (std::size_t j = 0; j < keep; ++j) if (scored[j].second == tgt[p]) { ++acc[mi].top10; break; }
            }
        }
    }
    std::printf("\n=== NEXT-TOKEN ACCURACY on %zu random held-out positions  [%.1fs] ===\n",
                acc.empty() ? 0 : acc[0].n, secs(t_acc));
    std::printf("  model                                      |  top-1  |  hits | 95%% Wilson       |  top-10 |  hits\n");
    std::printf("  -------------------------------------------+---------+-------+------------------+---------+------\n");
    for (std::size_t i = 0; i < models.size(); ++i) {
        if (models[i].kind == M_UNIFORM) {
            std::printf("  %-42s | %6.3f%% |     - | (1/V analytically) | %6.3f%% |     -\n",
                        models[i].name.c_str(), 100.0 / V, 1000.0 / V);
            continue;
        }
        const auto ci = wilson(acc[i].top1, acc[i].n);
        std::printf("  %-42s | %6.2f%% | %5zu | [%5.2f%%, %5.2f%%] | %6.2f%% | %5zu\n",
                    models[i].name.c_str(),
                    100.0 * acc[i].top1 / acc[i].n, acc[i].top1, ci.first, ci.second,
                    100.0 * acc[i].top10 / acc[i].n, acc[i].top10);
    }

    // --- WHERE THE PLEXUS LOSES IT --------------------------------------------
    //
    // A perplexity says how badly; this says why. Coverage is the share of
    // held-out (previous word, next word) pairs where the next word is anywhere
    // in the previous word's stored neighbour list. Whatever is not covered can
    // only be served by the unigram mixed in behind it.
    {
        std::printf("\n=== WHY: what the graph can even reach ===\n");
        std::printf("  graph    | targets | prev is <s> (no node) | next word IS a stored nbr"
                    " | survives associates() | empty assoc lists\n");
        for (int gr = 0; gr < 2; ++gr) {
            std::size_t n = 0, no_node = 0, cov = 0, cov_assoc = 0;
            Id last = kBos;
            for (std::size_t i = 0; i < TE.s.size(); ++i) {
                if (TE.tgt[i]) {
                    ++n;
                    if (last == kBos) ++no_node;
                    else {
                        const PlexRow& r = rows[gr][last];
                        const auto it = std::lower_bound(r.nb.begin(), r.nb.end(), TE.s[i]);
                        if (it != r.nb.end() && *it == TE.s[i]) {
                            ++cov;
                            if (r.w[2][static_cast<std::size_t>(it - r.nb.begin())] > 0.0f) ++cov_assoc;
                        }
                    }
                }
                last = TE.s[i];
            }
            std::printf("  deg %-5zu| %7zu | %9zu (%5.2f%%)    | %13zu (%5.2f%%)    |"
                        " %9zu (%5.2f%%)    | %6zu of %u\n",
                        plex_deg[gr], n, no_node, 100.0 * no_node / n,
                        cov, 100.0 * cov / n, cov_assoc, 100.0 * cov_assoc / n,
                        assoc_empty[gr], V);
        }
        std::printf("  Everything outside that coverage is answered by the unigram behind the\n"
                    "  mixture, not by the graph. And a stored edge is ranked by PMI, which is a\n"
                    "  measure of SURPRISE -- the opposite of what belongs at the top of a\n"
                    "  next-token distribution, where the answer is usually \"the\".\n");
    }

    // --- SAMPLES ---------------------------------------------------------------
    //
    // Numbers hide whether the output is language. These are unedited: whatever
    // the sampler produced, including <unk>, is what is printed.
    auto sample = [&](const Model& m, std::uint64_t seed, double temp, std::size_t topk,
                      std::size_t maxlen, const std::vector<Id>& seed_words) {
        std::mt19937_64 rng(seed);
        std::vector<Id> hist(kOrder - 1, kBos);
        std::string out;
        for (const Id w : seed_words) { hist.push_back(w); out += vword[w]; out += ' '; }
        std::vector<std::pair<double, Id>> cand;
        std::uint64_t g[kOrder + 1];
        for (std::size_t step = 0; step < maxlen; ++step) {
            Ctx c;
            const std::size_t L = hist.size();
            for (int mo = 1; mo < kOrder; ++mo) {
                std::uint64_t h = 0;
                for (std::size_t j = L - static_cast<std::size_t>(mo); j < L; ++j) h = hmix(h, hist[j]);
                c.prev[mo] = h;
            }
            c.prev[0] = 0;
            c.last = (hist.back() == kBos) ? 0 : hist.back();
            c.last_real = (hist.back() != kBos);
            E.set_ctx(c);

            cand.clear();
            const auto& sv = succ[hist.back()];
            for (std::size_t j = 0; j < sv.size() && j < 2000; ++j) cand.emplace_back(0.0, sv[j].first);
            for (const Id w : common) cand.emplace_back(0.0, w);
            std::sort(cand.begin(), cand.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
            cand.erase(std::unique(cand.begin(), cand.end(),
                                   [](const auto& a, const auto& b) { return a.second == b.second; }), cand.end());
            double z = 0.0;
            for (auto& [p, w] : cand) {
                grams(c, w, g);
                p = std::pow(E.prob(m, c, g, w), 1.0 / temp);
                z += p;
            }
            if (z <= 0.0) break;
            const std::size_t keep = std::min(topk, cand.size());
            std::partial_sort(cand.begin(), cand.begin() + keep, cand.end(),
                              [](const auto& a, const auto& b) {
                                  return a.first != b.first ? a.first > b.first : a.second < b.second; });
            cand.resize(keep);
            z = 0.0; for (const auto& [p, w] : cand) z += p;
            double r = std::uniform_real_distribution<double>(0.0, z)(rng), acc2 = 0.0;
            Id pick = cand.back().second;
            for (const auto& [p, w] : cand) { acc2 += p; if (acc2 >= r) { pick = w; break; } }
            if (pick == kEos) break;
            out += vword[pick]; out += ' ';
            hist.push_back(pick);
        }
        return out;
    };

    // The best n-gram model by TEST perplexity, and the best Plexus readout.
    std::size_t best_ng = 0, best_px = 0;
    for (std::size_t i = 0; i < models.size(); ++i) {
        if (models[i].kind != M_PLEX && models[i].kind != M_UNIFORM
            && (models[best_ng].kind == M_UNIFORM || dev_ppl[i] < dev_ppl[best_ng])) best_ng = i;
        if (models[i].kind == M_PLEX
            && (models[best_px].kind != M_PLEX || dev_ppl[i] < dev_ppl[best_px])) best_px = i;
    }

    std::printf("\n=== SAMPLES from the best n-gram model: %s (test PPL %.1f) ===\n",
                models[best_ng].name.c_str(), test_ppl[best_ng]);
    std::printf("  temperature 1.0, top-k 40, unedited:\n");
    for (int i = 0; i < 8; ++i)
        std::printf("   %d. %s\n", i + 1, sample(models[best_ng], 900 + i, 1.0, 40, 28, {}).c_str());
    std::printf("  temperature 0.7, top-k 20:\n");
    for (int i = 0; i < 3; ++i)
        std::printf("   %d. %s\n", i + 1, sample(models[best_ng], 700 + i, 0.7, 20, 28, {}).c_str());

    std::printf("\n=== SAMPLES from the Plexus: %s ===\n", models[best_px].name.c_str());
    std::printf("  same sampler, same temperature 1.0, top-k 40, unedited:\n");
    for (int i = 0; i < 8; ++i)
        std::printf("   %d. %s\n", i + 1, sample(models[best_px], 900 + i, 1.0, 40, 28, {}).c_str());
    // Seeded, because utter(topic) is seeded: this is the closest this file
    // gets to what Khora prints today, with the difference that it samples a
    // distribution instead of taking a greedy argmax over six candidates.
    std::printf("  seeded with a topic word, the way utter(topic) is seeded:\n");
    {
        std::uint64_t sd = 4242;
        for (const char* topic : {"justice", "water", "government", "mind"}) {
            auto it = vid.find(topic);
            if (it == vid.end()) continue;
            std::printf("   %-11s -> %s\n", topic,
                        sample(models[best_px], sd += 101, 1.0, 40, 20, {it->second}).c_str());
        }
        std::printf("  and the same topics through the n-gram model, for contrast:\n");
        sd = 4242;
        for (const char* topic : {"justice", "water", "government", "mind"}) {
            auto it = vid.find(topic);
            if (it == vid.end()) continue;
            std::printf("   %-11s -> %s\n", topic,
                        sample(models[best_ng], sd += 101, 1.0, 40, 20, {it->second}).c_str());
        }
    }

    std::printf("\n  Sampling truncates to the same candidate pool, then to top-k. Words outside\n"
                "  it are unreachable by the sampler even though the models give them backoff\n"
                "  mass -- an artefact of this sampler, not of the models.\n");

    // --- THE READ, printed from the numbers above so it cannot drift from them
    std::size_t bigram = 0, trigram = 0;
    for (std::size_t i = 0; i < models.size(); ++i) {
        if (models[i].kind == M_KN && models[i].order == 2) bigram  = i;
        if (models[i].kind == M_KN && models[i].order == 3) trigram = i;
    }
    std::printf("\n=== THE READ ===\n");
    std::printf("  best n-gram        %-42s test PPL %8.1f\n",
                models[best_ng].name.c_str(), test_ppl[best_ng]);
    std::printf("  best Plexus        %-42s test PPL %8.1f\n",
                models[best_px].name.c_str(), test_ppl[best_px]);
    std::printf("  unigram            %-42s test PPL %8.1f\n",
                models[1].name.c_str(), test_ppl[1]);
    std::printf("  The Plexus readout, given a tuned mixing weight and %.0fx the shipped edge\n"
                "  budget, is %.2fx worse than a plain Kneser-Ney trigram (%.1f vs %.1f) and\n"
                "  %.2fx worse than a plain bigram (%.1f). Against the unigram -- counting words\n"
                "  and ignoring order entirely -- it buys %.2fx. The graph is an associative\n"
                "  memory that was never asked to be a sequence model, and read as one it is\n"
                "  worth less than remembering which word followed which.\n",
                4000.0 / 160.0,
                test_ppl[best_px] / test_ppl[trigram], test_ppl[best_px], test_ppl[trigram],
                test_ppl[best_px] / test_ppl[bigram], test_ppl[bigram],
                test_ppl[1] / test_ppl[best_px]);
    return 0;
}
