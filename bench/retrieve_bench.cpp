// IS THE ASSOCIATIVE MEMORY A RETRIEVAL ENGINE?
//
// The Plexus answers "what is associated with X" and the Lattice answers "what
// is nearest to this glyph". Both readouts are load-bearing -- utter() steers by
// affinity(), the category work reads neighbours(), the Cortex is seeded from
// associates() -- and neither has ever been scored with a retrieval metric
// against a baseline that does not come from Khora. Information retrieval is a
// branch of the field with settled evaluation (Cranfield: fixed queries, fixed
// judgements, precision/recall/nDCG, and a lexical baseline everyone must
// beat). This applies that evaluation.
//
// dialect_bench and compress_bench already measured the Plexus as a PREDICTOR
// and found it worse than a bigram at both perplexity and bits. Prediction is
// not what it was built for. Association is. So this asks the question the
// substrate should be able to answer.
//
// THE JUDGEMENTS DO NOT COME FROM KHORA. data/eval/wn_categories.tsv -- 3,373
// WordNet category lines over 35,767 words -- is the only external labelled
// resource in the tree. Two words are RELATED here iff they share at least one
// WordNet category. Nothing in Khora contributed to that file.
//
// WHAT THIS MEASURES AND WHAT IT DOES NOT. Co-category membership is SEMANTIC
// RELATEDNESS, not topical relevance. "sparrow" and "vulture" are both in
// bird_genus and score as a hit; "sparrow" and "nest" are unrelated here and
// score as a miss even though any user who typed "sparrow" would want "nest".
// So this is a word-association benchmark wearing IR clothes, and the absolute
// numbers are NOT the numbers a document-retrieval system would report. What it
// can do is rank engines against each other on one fixed, external, uncontested
// question -- and that is the thing that has never been done here.
//
// The categories are also one level deep and badly incomplete (the same defect
// extraction_bench found): "chemist" and "philosopher" are both in `person`, but
// "chemist" and "chemistry" share nothing. Every engine is penalised by that
// identically, so DIFFERENCES are readable even where the level is not. The
// random baseline is printed for exactly this reason -- it is the only way to
// know what the level means.
//
// THE COLLECTION IS JUDGED-ONLY. A candidate must be a corpus word with
// frequency >= kMinFreq AND a word WordNet knows. That makes every returned
// item judged, so precision and recall are exact rather than pooled estimates.
// The cost, stated plainly: engines are handed a filter they did not earn. In
// open retrieval each would also have to avoid the ~90% of the vocabulary that
// is unjudged, and all the numbers below would be lower. It is applied
// identically to all eight engines, so the comparison holds.
//
// THE BASELINES ARE NOT DECORATION.
//   random     -- 100 collection words drawn uniformly. The floor.
//   frequency  -- the commonest words in the collection, the SAME list for
//                 every query. This is a notoriously strong baseline in
//                 DOCUMENT retrieval, where popularity is a real prior. It is
//                 structurally weak at word association, because the answer
//                 cannot depend on the query, and it lands at nDCG@10 0.009
//                 against random's 0.005. Reported anyway: a baseline you only
//                 keep when it wins is not a baseline.
//   bm25-rm    -- a real lexical engine: Okapi BM25 over a sentence-level
//                 document-term index built from the same corpus, then
//                 Lavrenko-Croft relevance-model term expansion over the top
//                 200 documents, idf-weighted. An engine that cannot beat BM25
//                 is not a retrieval engine. (Drop the idf weighting from the
//                 expansion and it collapses into the frequency baseline, which
//                 is printed above it, so the reader can see that too.)
//
// COST IS REPORTED BESIDE QUALITY. An engine that wins by two points and costs
// a hundred times as much is a different product from one that wins by two
// points for free.
//
// WHAT THE HARNESS CANNOT SEE:
//   - whether a returned word is USEFUL, only whether WordNet filed it in the
//     same box;
//   - polysemy: "bank" is one node in the Plexus and several words in WordNet;
//   - that a query drawn at random from the collection over-samples the huge
//     categories (`person` alone has ~700 members), so query difficulty is not
//     uniform and the mean is dominated by easy, populous categories;
//   - anything about ranked lists past 100.
//
// WHAT IT FOUND, on 57 books / 8.0M tokens / 400 queries:
//
//   The Plexus does beat the frequency baseline decisively (nDCG@10 0.042 vs
//   0.009) and it ties the BM25 lexical baseline (0.040) -- at a third of the
//   index and an eighth of the query latency. That is the honest positive.
//
//   It does NOT beat BM25. The gap is +0.002 with an SE of 0.005. After all the
//   PMI machinery, the answer to "is the associative graph better than counting
//   words in sentences" is: no, it is the same, cheaper.
//
//   RAW CO-OCCURRENCE BEATS PPMI AND BEATS associates(), the shipped readout, on
//   every rank-sensitive metric except P@1. affinity()'s noise floor (three
//   co-occurrences) and the function-word filter cut the mean candidate list
//   from 42 words to 8, which costs three times the recall. The sharpening the
//   module was built to do is real at rank 1 (6.8% vs 5.5%) and destructive
//   below it.
//
//   THE HEADLINE NUMBER IS A TRAP, and this file measures the trap. The best
//   engine on the table is `lattice glyph` -- char trigrams, no corpus, no
//   learning, a pure spelling code -- at nDCG@10 0.050 and P@1 11.0%. It wins
//   because the ground truth is partly a spelling test: a query's judged
//   relatives are 3.0x more trigram-similar to it than a random collection
//   word, because WordNet fills `person` with -er nouns and clusters -ion
//   abstractions by suffix. 55% of the lattice's correct hits are
//   near-homographs of the query ("permission" -> "commission", "submission").
//   Strike those out and it has 65 correct hits against the Plexus's 149. The
//   spelling audit at the bottom of the output exists to keep that from being
//   read as a result about meaning.
//
//   RAISING THE MEMORY BOUND DOES NOT HELP. A second graph with max_degree 1000
//   instead of 160 buys recall@100 (4.99% -> 6.51%) and loses precision (nDCG
//   0.042 -> 0.036) for 1.8x the index and 3x the latency. The pruner is keeping
//   the right edges; the ceiling is not the constant.

#include "khora/lattice/glyph.hpp"
#include "khora/lattice/lattice.hpp"
#include "khora/lexicon/lexicon.hpp"
#include "khora/plexus/plexus.hpp"
#include "khora/reservoir/reservoir.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using khora::plexus::Plexus;

namespace {

constexpr std::uint32_t kMinFreq    = 20;   // corpus floor for the collection
constexpr std::size_t   kMinRel     = 5;    // a query needs this many judged relatives
constexpr std::size_t   kCut        = 100;  // ranked list depth
constexpr std::size_t   kRmDocs     = 200;  // pseudo-relevant set for BM25-RM
constexpr double        kBm25K1     = 1.2;
constexpr double        kBm25B      = 0.75;

using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------------------
// External ground truth
// ---------------------------------------------------------------------------

struct WordNet {
    std::vector<std::string>                        cat_name;
    std::vector<std::vector<std::string>>           members;
    std::unordered_map<std::string, std::vector<std::uint32_t>> of;  // word -> category ids
};

WordNet load_wordnet(const std::string& path) {
    WordNet wn;
    std::ifstream in(path);
    if (!in) return wn;
    std::string line;
    while (std::getline(in, line)) {
        const auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        const auto cid = static_cast<std::uint32_t>(wn.cat_name.size());
        wn.cat_name.push_back(line.substr(0, tab));
        wn.members.emplace_back();
        std::istringstream ws(line.substr(tab + 1));
        std::string w;
        while (ws >> w) {
            wn.members[cid].push_back(w);
            wn.of[w].push_back(cid);
        }
    }
    return wn;
}

// ---------------------------------------------------------------------------
// Metrics. Binary relevance, standard definitions, no local inventions.
// ---------------------------------------------------------------------------

struct Metrics {
    std::size_t queries    = 0;
    std::size_t p1_hits    = 0;
    std::size_t p10_hits   = 0;
    std::size_t p10_slots  = 0;   // 10 per query, whether or not the engine filled them
    double      mrr = 0.0,  mrr_sq  = 0.0;
    double      ndcg = 0.0, ndcg_sq = 0.0;
    double      rec = 0.0,  rec_sq  = 0.0;
    std::size_t returned   = 0;   // total items across all queries (list length)
    double      nanos      = 0.0;
    std::size_t index_bytes = 0;
};

void tally(Metrics& m, const std::vector<std::uint32_t>& ranked,
           const std::unordered_set<std::uint32_t>& rel) {
    ++m.queries;
    m.returned  += ranked.size();
    m.p10_slots += 10;

    const std::size_t R = rel.size();
    double dcg = 0.0, rr = 0.0;
    std::size_t hits100 = 0;
    for (std::size_t i = 0; i < ranked.size() && i < kCut; ++i) {
        if (!rel.count(ranked[i])) continue;
        ++hits100;
        if (i == 0) ++m.p1_hits;
        if (i < 10) {
            ++m.p10_hits;
            dcg += 1.0 / std::log2(static_cast<double>(i) + 2.0);
        }
        if (rr == 0.0) rr = 1.0 / static_cast<double>(i + 1);
    }
    double idcg = 0.0;
    for (std::size_t i = 0; i < 10 && i < R; ++i)
        idcg += 1.0 / std::log2(static_cast<double>(i) + 2.0);

    const double nd = idcg > 0.0 ? dcg / idcg : 0.0;
    // recall@100 against the full judged relevant set. Where R > 100 it cannot
    // reach 1 by construction; mean |relevant| is printed so that is visible.
    const double rc = R ? static_cast<double>(hits100) / static_cast<double>(R) : 0.0;

    m.mrr  += rr; m.mrr_sq  += rr * rr;
    m.ndcg += nd; m.ndcg_sq += nd * nd;
    m.rec  += rc; m.rec_sq  += rc * rc;
}

// 95% Wilson interval on a proportion, in percent. A bare percentage over 400
// queries invites reading a dozen events as a result.
std::pair<double, double> wilson(std::size_t hits, std::size_t n) {
    if (n == 0) return {0.0, 0.0};
    const double z = 1.96, ph = static_cast<double>(hits) / static_cast<double>(n);
    const double d = 1.0 + z * z / static_cast<double>(n);
    const double c = ph + z * z / (2.0 * static_cast<double>(n));
    const double mg = z * std::sqrt(ph * (1.0 - ph) / static_cast<double>(n)
                                    + z * z / (4.0 * static_cast<double>(n) * static_cast<double>(n)));
    return {100.0 * (c - mg) / d, 100.0 * (c + mg) / d};
}

// MRR, nDCG and recall are means of per-query reals, not proportions, so a
// Wilson interval is the wrong instrument. Normal SE of the mean instead.
double se_mean(double sum, double sum_sq, std::size_t n) {
    if (n < 2) return 0.0;
    const double N = static_cast<double>(n);
    const double var = (sum_sq - sum * sum / N) / (N - 1.0);
    return var > 0.0 ? std::sqrt(var / N) : 0.0;
}

// ---------------------------------------------------------------------------
// BM25 over a sentence-level document-term index
// ---------------------------------------------------------------------------

struct Bm25 {
    std::vector<std::vector<std::pair<std::uint32_t, std::uint32_t>>> postings; // term -> (doc, tf)
    std::vector<double>        idf;
    const std::vector<std::vector<std::uint32_t>>* docs = nullptr;
    double      avglen = 0.0;
    std::size_t ndocs  = 0;
    std::size_t postings_entries = 0;
};

Bm25 build_bm25(const std::vector<std::vector<std::uint32_t>>& docs, std::size_t nterms) {
    Bm25 ix;
    ix.docs  = &docs;
    ix.ndocs = docs.size();
    ix.postings.resize(nterms);
    std::vector<std::uint32_t> scratch;
    std::uint64_t total_len = 0;
    for (std::uint32_t d = 0; d < docs.size(); ++d) {
        total_len += docs[d].size();
        scratch = docs[d];
        std::sort(scratch.begin(), scratch.end());
        for (std::size_t i = 0; i < scratch.size();) {
            std::size_t j = i;
            while (j < scratch.size() && scratch[j] == scratch[i]) ++j;
            ix.postings[scratch[i]].emplace_back(d, static_cast<std::uint32_t>(j - i));
            ++ix.postings_entries;
            i = j;
        }
    }
    ix.avglen = ix.ndocs ? static_cast<double>(total_len) / static_cast<double>(ix.ndocs) : 1.0;
    ix.idf.resize(nterms, 0.0);
    for (std::size_t t = 0; t < nterms; ++t) {
        const double df = static_cast<double>(ix.postings[t].size());
        const double N  = static_cast<double>(ix.ndocs);
        ix.idf[t] = std::log(1.0 + (N - df + 0.5) / (df + 0.5));
    }
    return ix;
}

} // namespace

int main(int argc, char** argv) {
    const std::string dir       = (argc > 1) ? argv[1] : "data/reservoir";
    const std::string wn_path   = (argc > 2) ? argv[2] : "data/eval/wn_categories.tsv";
    const std::size_t max_books = (argc > 3) ? std::stoul(argv[3]) : 200;
    const std::size_t want_q    = (argc > 4) ? std::stoul(argv[4]) : 400;

    std::printf("Information retrieval over Khora's associative memory\n");
    std::printf("  judgements: WordNet co-category (external), NOT topical relevance\n");

    const WordNet wn = load_wordnet(wn_path);
    if (wn.cat_name.empty()) {
        std::printf("  no %s -- nothing to score against\n", wn_path.c_str());
        return 0;
    }
    std::printf("  %zu categories, %zu judged words\n", wn.cat_name.size(), wn.of.size());

    // ---- corpus ------------------------------------------------------------
    //
    // One pass. Sentences become BM25 documents; the same sentences flattened
    // per book become the Plexus's token stream, so both engines see byte-for-
    // byte the same text. Co-occurrence therefore crosses a sentence boundary by
    // up to the window (3) at each full stop, which is how the production graph
    // is built too. Topic "code" is skipped: it is Khora's own source, not prose,
    // and its identifiers are not English.
    khora::reservoir::Reservoir res(dir);
    res.load_catalog();
    auto cat = res.catalog();
    std::sort(cat.begin(), cat.end(),
              [](const auto& a, const auto& b) { return a.title < b.title; });
    if (cat.empty()) { std::printf("  empty catalog\n"); return 0; }

    Plexus plex;
    // A second graph, same corpus, same code, only the memory bound moved. The
    // Plexus keeps at most max_degree edges per node (160 by default), which is
    // a hard ceiling on how many candidates any Plexus readout can offer -- so
    // the question "is its recall a property of the model or of one constant"
    // has to be answered by moving the constant, not by arguing about it.
    Plexus wide;
    wide.set_max_degree(1000);
    std::unordered_map<std::string, std::uint32_t> tid;   // term -> dense term id
    std::vector<std::string>  term;
    std::vector<std::uint32_t> freq;
    std::vector<std::vector<std::uint32_t>> docs;

    const auto t_build0 = Clock::now();
    std::size_t books = 0;
    std::uint64_t tokens = 0;
    for (const auto& t : cat) {
        if (books >= max_books) break;
        if (t.topic == "code") continue;
        auto text = res.read(t.title);
        if (!text || text->size() < 40000) continue;
        ++books;
        const auto sents = khora::lexicon::tokenize_sentences(*text);

        std::vector<std::string> flat;
        for (const auto& s : sents) flat.insert(flat.end(), s.begin(), s.end());
        tokens += flat.size();
        // observe() recomputes an O(V) partition function per call, so it is
        // called once per BOOK, never per sentence.
        plex.observe(flat, 3);
        wide.observe(flat, 3);

        for (const auto& s : sents) {
            if (s.empty()) continue;
            std::vector<std::uint32_t> d;
            d.reserve(s.size());
            for (const auto& w : s) {
                auto it = tid.find(w);
                std::uint32_t id;
                if (it == tid.end()) {
                    id = static_cast<std::uint32_t>(term.size());
                    tid.emplace(w, id);
                    term.push_back(w);
                    freq.push_back(0);
                } else {
                    id = it->second;
                }
                ++freq[id];
                d.push_back(id);
            }
            docs.push_back(std::move(d));
        }
    }
    plex.prune_all();
    wide.prune_all();
    const double build_s =
        std::chrono::duration<double>(Clock::now() - t_build0).count();

    std::printf("  %zu books, %llu tokens, %zu sentences, %zu types  (%.1fs to build)\n",
                books, static_cast<unsigned long long>(tokens), docs.size(),
                term.size(), build_s);
    if (docs.empty()) { std::printf("  no text\n"); return 0; }

    // ---- the collection ----------------------------------------------------
    //
    // Judged-only: a corpus word above the frequency floor that WordNet knows.
    std::vector<std::uint32_t> col_term;   // collection id -> term id
    std::unordered_map<std::string, std::uint32_t> cid;
    for (std::uint32_t t = 0; t < term.size(); ++t) {
        if (freq[t] < kMinFreq) continue;
        if (!wn.of.count(term[t])) continue;
        cid.emplace(term[t], static_cast<std::uint32_t>(col_term.size()));
        col_term.push_back(t);
    }
    const std::size_t C = col_term.size();
    std::printf("  collection: %zu words (freq >= %u and known to WordNet)\n", C, kMinFreq);
    if (C < 200) { std::printf("  collection too small to evaluate\n"); return 0; }

    // ---- query set ---------------------------------------------------------
    //
    // Drawn from the collection itself, seeded, keeping only queries with enough
    // judged relatives that the metrics are defined.
    std::vector<std::uint32_t> pool(C);
    for (std::uint32_t i = 0; i < C; ++i) pool[i] = i;
    std::shuffle(pool.begin(), pool.end(), std::mt19937(20260825u));

    struct Query { std::uint32_t c; std::unordered_set<std::uint32_t> rel; };
    std::vector<Query> queries;
    std::uint64_t rel_total = 0;
    for (const std::uint32_t q : pool) {
        if (queries.size() >= want_q) break;
        Query qu; qu.c = q;
        for (const std::uint32_t k : wn.of.at(term[col_term[q]])) {
            for (const auto& m : wn.members[k]) {
                auto it = cid.find(m);
                if (it != cid.end() && it->second != q) qu.rel.insert(it->second);
            }
        }
        if (qu.rel.size() < kMinRel) continue;
        rel_total += qu.rel.size();
        queries.push_back(std::move(qu));
    }
    const std::size_t Q = queries.size();
    std::printf("  %zu queries, mean %.1f judged relatives each\n",
                Q, Q ? static_cast<double>(rel_total) / static_cast<double>(Q) : 0.0);
    if (Q == 0) return 0;

    // ---- metric self-check -------------------------------------------------
    //
    // An oracle ranking must score 1.0 on everything. This is the one runnable
    // check on the scoring code: an off-by-one in the DCG discount or the rank
    // index would show up here and nowhere else.
    {
        Metrics om;
        for (const auto& qu : queries) {
            std::vector<std::uint32_t> perfect(qu.rel.begin(), qu.rel.end());
            std::sort(perfect.begin(), perfect.end());
            if (perfect.size() > kCut) perfect.resize(kCut);
            tally(om, perfect, qu.rel);
        }
        const double p1 = static_cast<double>(om.p1_hits) / static_cast<double>(om.queries);
        const double nd = om.ndcg / static_cast<double>(om.queries);
        std::printf("  metric self-check (oracle ranking): P@1=%.3f nDCG@10=%.3f  %s\n",
                    p1, nd,
                    (p1 > 0.9999 && nd > 0.9999) ? "ok" : "FAIL -- scoring code is wrong");
    }

    // ---- indexes -----------------------------------------------------------
    const auto t_bm0 = Clock::now();
    const Bm25 bm = build_bm25(docs, term.size());
    const double bm_build_s = std::chrono::duration<double>(Clock::now() - t_bm0).count();

    // Plexus node id <-> collection id, for each graph.
    std::vector<std::int32_t> pnode_cid(plex.vocabulary_size(), -1), wnode_cid(wide.vocabulary_size(), -1);
    std::vector<std::int32_t> col_pnode(C, -1), col_wnode(C, -1);
    for (std::size_t i = 0; i < plex.vocabulary_size(); ++i) {
        auto it = cid.find(std::string(plex.node_name(i)));
        if (it == cid.end()) continue;
        pnode_cid[i]          = static_cast<std::int32_t>(it->second);
        col_pnode[it->second] = static_cast<std::int32_t>(i);
    }
    for (std::size_t i = 0; i < wide.vocabulary_size(); ++i) {
        auto it = cid.find(std::string(wide.node_name(i)));
        if (it == cid.end()) continue;
        wnode_cid[i]          = static_cast<std::int32_t>(it->second);
        col_wnode[it->second] = static_cast<std::int32_t>(i);
    }

    // Lattice of pure structural glyphs -- char-trigram spelling codes, no
    // training of any kind. This is the Lexicon's baseline encoder, and the
    // question is whether spelling alone retrieves semantic relatives.
    const auto t_lat0 = Clock::now();
    khora::lattice::Lattice lat;
    std::vector<khora::lattice::Glyph> col_glyph(C);
    for (std::uint32_t i = 0; i < C; ++i) {
        col_glyph[i] = khora::lexicon::encode_token(term[col_term[i]]);
        lat.store(term[col_term[i]], col_glyph[i]);
    }
    const double lat_build_s = std::chrono::duration<double>(Clock::now() - t_lat0).count();

    // Frequency ranking: one list, the same for every query.
    std::vector<std::uint32_t> by_freq(C);
    for (std::uint32_t i = 0; i < C; ++i) by_freq[i] = i;
    std::sort(by_freq.begin(), by_freq.end(), [&](std::uint32_t a, std::uint32_t b) {
        return freq[col_term[a]] > freq[col_term[b]];
    });
    by_freq.resize(std::min<std::size_t>(by_freq.size(), kCut + 1));

    // ---- engines -----------------------------------------------------------
    //
    // Each takes a collection id and returns up to 100 collection ids, best
    // first, never including the query itself.
    using Engine = std::function<void(std::uint32_t, std::vector<std::uint32_t>&)>;

    std::vector<std::uint32_t> rnd_scratch(C);
    for (std::uint32_t i = 0; i < C; ++i) rnd_scratch[i] = i;

    const Engine e_random = [&](std::uint32_t q, std::vector<std::uint32_t>& out) {
        std::mt19937 rng(0x5EEDu + q);
        const std::size_t n = rnd_scratch.size();
        for (std::size_t i = 0; i < kCut + 1 && i < n; ++i)
            std::swap(rnd_scratch[i], rnd_scratch[i + rng() % (n - i)]);
        for (std::size_t i = 0; i < kCut + 1 && i < n; ++i)
            if (rnd_scratch[i] != q && out.size() < kCut) out.push_back(rnd_scratch[i]);
    };

    const Engine e_freq = [&](std::uint32_t q, std::vector<std::uint32_t>& out) {
        for (const std::uint32_t c : by_freq)
            if (c != q && out.size() < kCut) out.push_back(c);
    };

    // Okapi BM25 retrieval of sentences, then Lavrenko-Croft relevance-model
    // term expansion over the top kRmDocs, weighted by idf. Single-term query,
    // so idf(q) is constant across documents and drops out of the doc ranking.
    std::vector<double> rm_acc(C, 0.0);
    std::vector<std::uint32_t> rm_touched;
    const Engine e_bm25 = [&](std::uint32_t q, std::vector<std::uint32_t>& out) {
        const std::uint32_t qt = col_term[q];
        const auto& post = bm.postings[qt];
        if (post.empty()) return;
        std::vector<std::pair<double, std::uint32_t>> hits;
        hits.reserve(post.size());
        for (const auto& [d, tf] : post) {
            const double len = static_cast<double>((*bm.docs)[d].size());
            const double den = static_cast<double>(tf)
                             + kBm25K1 * (1.0 - kBm25B + kBm25B * len / bm.avglen);
            hits.emplace_back(static_cast<double>(tf) * (kBm25K1 + 1.0) / den, d);
        }
        const std::size_t keep = std::min(kRmDocs, hits.size());
        std::partial_sort(hits.begin(), hits.begin() + keep, hits.end(),
                          [](const auto& a, const auto& b) { return a.first > b.first; });
        double Zs = 0.0;
        for (std::size_t i = 0; i < keep; ++i) Zs += hits[i].first;
        if (Zs <= 0.0) return;

        for (std::size_t i = 0; i < keep; ++i) {
            const std::uint32_t d = hits[i].second;
            const auto& toks = (*bm.docs)[d];
            const double w = hits[i].first / Zs / static_cast<double>(toks.size());
            for (const std::uint32_t t : toks) {
                auto it = cid.find(term[t]);
                if (it == cid.end() || it->second == q) continue;
                if (rm_acc[it->second] == 0.0) rm_touched.push_back(it->second);
                rm_acc[it->second] += w;
            }
        }
        std::vector<std::pair<double, std::uint32_t>> ranked;
        ranked.reserve(rm_touched.size());
        for (const std::uint32_t c : rm_touched)
            ranked.emplace_back(rm_acc[c] * bm.idf[col_term[c]], c);
        const std::size_t k = std::min(kCut, ranked.size());
        std::partial_sort(ranked.begin(), ranked.begin() + k, ranked.end(),
                          [](const auto& a, const auto& b) { return a.first > b.first; });
        for (std::size_t i = 0; i < k; ++i) out.push_back(ranked[i].second);
        for (const std::uint32_t c : rm_touched) rm_acc[c] = 0.0;
        rm_touched.clear();
    };

    // The Plexus's stored adjacency, ranked by raw co-occurrence count. The
    // graph keeps at most max_degree (160) edges per node, so this list -- and
    // every Plexus engine below it -- can never exceed that many candidates
    // before the collection filter cuts it further. That is a hard ceiling on
    // recall@100 and it belongs in the reading of the table.
    const Engine e_cooc = [&](std::uint32_t q, std::vector<std::uint32_t>& out) {
        const std::int32_t p = col_pnode[q];
        if (p < 0) return;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> ranked;  // (count, cid)
        for (const auto& [nb, c] : plex.neighbours(static_cast<std::size_t>(p))) {
            const std::int32_t cc = pnode_cid[nb];
            if (cc < 0 || static_cast<std::uint32_t>(cc) == q) continue;
            ranked.emplace_back(c, static_cast<std::uint32_t>(cc));
        }
        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
            return a.first != b.first ? a.first > b.first : a.second < b.second;
        });
        for (std::size_t i = 0; i < ranked.size() && i < kCut; ++i)
            out.push_back(ranked[i].second);
    };

    // The same adjacency ranked by affinity() -- positive PMI with smoothed
    // context, the hub-proof score the whole module exists for. affinity()
    // returns 0 below its noise floor of three co-occurrences, and those are
    // dropped rather than ranked last, because that is what a caller of
    // affinity() sees.
    const Engine e_ppmi = [&](std::uint32_t q, std::vector<std::uint32_t>& out) {
        const std::int32_t p = col_pnode[q];
        if (p < 0) return;
        const std::string_view qw = plex.node_name(static_cast<std::size_t>(p));
        std::vector<std::pair<double, std::uint32_t>> ranked;
        for (const auto& [nb, c] : plex.neighbours(static_cast<std::size_t>(p))) {
            (void)c;
            const std::int32_t cc = pnode_cid[nb];
            if (cc < 0 || static_cast<std::uint32_t>(cc) == q) continue;
            const double a = plex.affinity(qw, plex.node_name(nb));
            if (a > 0.0) ranked.emplace_back(a, static_cast<std::uint32_t>(cc));
        }
        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
            return a.first != b.first ? a.first > b.first : a.second < b.second;
        });
        for (std::size_t i = 0; i < ranked.size() && i < kCut; ++i)
            out.push_back(ranked[i].second);
    };

    // The shipped readout: confidence-weighted PPMI (ppmi * log2(1+cooc)) plus
    // the function-word filter. This is what every caller inside Khora actually
    // gets when it asks "what is associated with X".
    const Engine e_assoc = [&](std::uint32_t q, std::vector<std::uint32_t>& out) {
        for (const auto& [w, a] : plex.associates(term[col_term[q]], 256)) {
            (void)a;
            auto it = cid.find(w);
            if (it == cid.end() || it->second == q) continue;
            if (out.size() < kCut) out.push_back(it->second);
        }
    };

    // Lattice nearest neighbour over structural glyphs. Hamming distance over
    // 10,000 bits against every word in the collection.
    const Engine e_lattice = [&](std::uint32_t q, std::vector<std::uint32_t>& out) {
        for (const auto& m : lat.query(col_glyph[q], kCut + 1)) {
            auto it = cid.find(m.label);
            if (it == cid.end() || it->second == q) continue;
            if (out.size() < kCut) out.push_back(it->second);
        }
    };

    // Same readout, same code, only the degree bound differs.
    const Engine e_cooc_wide = [&](std::uint32_t q, std::vector<std::uint32_t>& out) {
        const std::int32_t p = col_wnode[q];
        if (p < 0) return;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> ranked;
        for (const auto& [nb, c] : wide.neighbours(static_cast<std::size_t>(p))) {
            const std::int32_t cc = wnode_cid[nb];
            if (cc < 0 || static_cast<std::uint32_t>(cc) == q) continue;
            ranked.emplace_back(c, static_cast<std::uint32_t>(cc));
        }
        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
            return a.first != b.first ? a.first > b.first : a.second < b.second;
        });
        for (std::size_t i = 0; i < ranked.size() && i < kCut; ++i)
            out.push_back(ranked[i].second);
    };

    struct Row { const char* name; const Engine* fn; Metrics m; };
    std::vector<Row> rows = {
        {"random",         &e_random,  {}},
        {"frequency",      &e_freq,    {}},
        {"bm25-rm (lex)",  &e_bm25,    {}},
        {"plexus cooc",    &e_cooc,    {}},
        {"plexus cooc/1k",&e_cooc_wide, {}},
        {"plexus ppmi",    &e_ppmi,    {}},
        {"plexus assoc()", &e_assoc,   {}},
        {"lattice glyph",  &e_lattice, {}},
    };

    // ---- index sizes -------------------------------------------------------
    //
    // Serialised footprint, counted the way each structure is actually stored.
    std::size_t vocab_bytes = 0;
    for (const std::uint32_t c : col_term) vocab_bytes += term[c].size() + 1;
    std::size_t plex_vocab_bytes = 0;
    for (std::size_t i = 0; i < plex.vocabulary_size(); ++i)
        plex_vocab_bytes += plex.node_name(i).size() + 6;   // len + string + occ

    const std::size_t sz_random = C * 4;
    const std::size_t sz_freq   = by_freq.size() * 4;
    // The relevance model needs the forward index as well as the postings; a
    // plain BM25 document search would not, and that is worth seeing.
    const std::size_t sz_bm25   = bm.postings_entries * 8 + bm.ndocs * 4
                                + static_cast<std::size_t>(tokens) * 4;
    const std::size_t sz_plex   = static_cast<std::size_t>(plex.edge_count()) * 8
                                + plex_vocab_bytes;
    const std::size_t sz_wide   = static_cast<std::size_t>(wide.edge_count()) * 8
                                + plex_vocab_bytes;
    const std::size_t sz_lat    = C * (khora::lattice::kGlyphWords * 8) + vocab_bytes;
    rows[0].m.index_bytes = sz_random;
    rows[1].m.index_bytes = sz_freq;
    rows[2].m.index_bytes = sz_bm25;
    rows[3].m.index_bytes = sz_plex;
    rows[4].m.index_bytes = sz_wide;
    rows[5].m.index_bytes = sz_plex;
    rows[6].m.index_bytes = sz_plex;
    rows[7].m.index_bytes = sz_lat;

    // ---- run ---------------------------------------------------------------
    std::vector<std::uint32_t> out;
    out.reserve(kCut);
    for (auto& r : rows) {
        for (const auto& qu : queries) {
            out.clear();
            const auto t0 = Clock::now();
            (*r.fn)(qu.c, out);
            r.m.nanos += std::chrono::duration<double, std::nano>(Clock::now() - t0).count();
            tally(r.m, out, qu.rel);
        }
    }

    // ---- the table ---------------------------------------------------------
    std::printf("\n  === RETRIEVAL QUALITY (%zu queries, WordNet co-category, judged-only collection) ===\n", Q);
    std::printf("  engine         |    P@1  95%%CI          hits |   P@10  95%%CI          hits |     MRR       |   nDCG@10     | recall@100 | list\n");
    std::printf("  ---------------+-----------------------------+-----------------------------+---------------+---------------+------------+------\n");
    for (const auto& r : rows) {
        const auto [l1, u1]   = wilson(r.m.p1_hits, r.m.queries);
        const auto [l10, u10] = wilson(r.m.p10_hits, r.m.p10_slots);
        const double n = static_cast<double>(r.m.queries);
        std::printf("  %-14s | %5.1f%% [%4.1f,%5.1f] %5zu/%-5zu | %5.1f%% [%4.1f,%5.1f] %5zu/%-5zu | %.3f +-%.3f | %.3f +-%.3f | %6.2f%%    | %5.1f\n",
                    r.name,
                    100.0 * r.m.p1_hits / n, l1, u1, r.m.p1_hits, r.m.queries,
                    100.0 * r.m.p10_hits / static_cast<double>(r.m.p10_slots), l10, u10,
                    r.m.p10_hits, r.m.p10_slots,
                    r.m.mrr / n, se_mean(r.m.mrr, r.m.mrr_sq, r.m.queries),
                    r.m.ndcg / n, se_mean(r.m.ndcg, r.m.ndcg_sq, r.m.queries),
                    100.0 * r.m.rec / n,
                    static_cast<double>(r.m.returned) / n);
    }
    std::printf("\n  counts after P@1 and P@10 are HITS (P@10 out of %zu judged slots, 10 per query,\n"
                "  unfilled slots counted as misses). MRR and nDCG carry a normal SE of the mean,\n"
                "  not a Wilson interval -- they are means of per-query reals, not proportions.\n"
                "  'list' is the mean number of results the engine could return at all.\n",
                rows[0].m.p10_slots);

    // ---- cost --------------------------------------------------------------
    std::printf("\n  === COST ===\n");
    std::printf("  engine         | query latency | index size | build\n");
    std::printf("  ---------------+---------------+------------+--------\n");
    const double mb = 1024.0 * 1024.0;
    const char* build_note[8] = {"-", "-", "", "", "", "", "", ""};
    char bm_s[32], px_s[32], lt_s[32];
    std::snprintf(bm_s, sizeof bm_s, "%.1fs", bm_build_s);
    std::snprintf(px_s, sizeof px_s, "%.1fs (both graphs)", build_s);
    std::snprintf(lt_s, sizeof lt_s, "%.1fs", lat_build_s);
    build_note[2] = bm_s; build_note[3] = px_s; build_note[4] = px_s;
    build_note[5] = px_s; build_note[6] = px_s; build_note[7] = lt_s;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const double us = rows[i].m.nanos / 1000.0 / static_cast<double>(rows[i].m.queries);
        std::printf("  %-14s | %9.1f us  | %7.2f MB | %s\n",
                    rows[i].name, us,
                    static_cast<double>(rows[i].m.index_bytes) / mb, build_note[i]);
    }
    std::printf("\n  cooc / ppmi / assoc() share ONE graph, so they share its size; cooc/1k is\n"
                "  the second graph, and the build figure is the single corpus pass that made\n"
                "  both, not graph construction alone.\n"
                "  bm25-rm's index includes the forward index the relevance model needs;\n"
                "  a plain BM25 document search needs only the %.2f MB of postings.\n"
                "  the lattice figure is a full linear scan AND a full sort of the collection\n"
                "  on every query -- Lattice::query has no index, which is why it costs ~60x\n"
                "  the graph lookup for an answer that is mostly spelling.\n",
                static_cast<double>(bm.postings_entries * 8 + bm.ndocs * 4) / mb);

    // ---- what the engines actually return ----------------------------------
    //
    // A metric table hides whether the output is sane. Three queries, top eight,
    // relatives marked.
    std::printf("\n  === WHAT COMES BACK (top 8, * = judged related) ===\n");
    for (std::size_t qi = 0; qi < queries.size() && qi < 3; ++qi) {
        const auto& qu = queries[qi];
        std::printf("\n  query \"%s\"  (%zu judged relatives in the collection)\n",
                    term[col_term[qu.c]].c_str(), qu.rel.size());
        for (const auto& r : rows) {
            if (std::string(r.name) == "random") continue;
            out.clear();
            (*r.fn)(qu.c, out);
            std::printf("    %-14s", r.name);
            for (std::size_t i = 0; i < out.size() && i < 8; ++i)
                std::printf(" %s%s", term[col_term[out[i]]].c_str(),
                            qu.rel.count(out[i]) ? "*" : "");
            std::printf("\n");
        }
    }

    // ---- is the ground truth partly a SPELLING test? -----------------------
    //
    // The lattice engine encodes nothing but char trigrams. It has read no
    // corpus and learned nothing. If it scores well, either spelling carries
    // semantics or the judgements are morphologically contaminated -- WordNet
    // fills `person` with -er agent nouns and `-ion` abstractions cluster by
    // suffix, so co-category and shared affix are not independent.
    //
    // This measures the contamination directly: trigram Jaccard between a query
    // and (a) its judged relatives, (b) a random collection word. If (a) > (b),
    // the ground truth rewards spelling on its own, and every engine's score has
    // that component in it. Then per engine: how spelling-similar its answers
    // are, and what share of its CORRECT hits are near-homographs of the query.
    const auto trigrams = [](const std::string& w) {
        std::string s = "^" + w + "$";
        std::vector<std::uint32_t> g;
        for (std::size_t i = 0; i + 3 <= s.size(); ++i) {
            std::uint32_t h = 2166136261u;
            for (std::size_t j = 0; j < 3; ++j) { h ^= static_cast<unsigned char>(s[i + j]); h *= 16777619u; }
            g.push_back(h);
        }
        std::sort(g.begin(), g.end());
        g.erase(std::unique(g.begin(), g.end()), g.end());
        return g;
    };
    std::vector<std::vector<std::uint32_t>> tri(C);
    for (std::uint32_t i = 0; i < C; ++i) tri[i] = trigrams(term[col_term[i]]);
    const auto jac = [&](std::uint32_t a, std::uint32_t b) {
        std::size_t both = 0;
        std::size_t i = 0, j = 0;
        while (i < tri[a].size() && j < tri[b].size()) {
            if (tri[a][i] == tri[b][j]) { ++both; ++i; ++j; }
            else if (tri[a][i] < tri[b][j]) ++i; else ++j;
        }
        const std::size_t either = tri[a].size() + tri[b].size() - both;
        return either ? static_cast<double>(both) / static_cast<double>(either) : 0.0;
    };
    constexpr double kHomograph = 0.30;   // "commission" vs "permission" is 0.36

    double j_rel = 0.0, j_chance = 0.0;
    {
        std::mt19937 rng(4242u);
        std::size_t nr = 0, nc = 0;
        for (const auto& qu : queries) {
            std::size_t taken = 0;
            for (const std::uint32_t r : qu.rel) {
                if (taken++ >= 20) break;
                j_rel += jac(qu.c, r); ++nr;
            }
            for (int k = 0; k < 20; ++k) {
                const std::uint32_t r = static_cast<std::uint32_t>(rng() % C);
                if (r == qu.c) continue;
                j_chance += jac(qu.c, r); ++nc;
            }
        }
        j_rel    = nr ? j_rel / static_cast<double>(nr) : 0.0;
        j_chance = nc ? j_chance / static_cast<double>(nc) : 0.0;
    }
    std::printf("\n  === IS THE GROUND TRUTH A SPELLING TEST? ===\n");
    std::printf("  trigram Jaccard, query vs its judged relatives : %.4f\n", j_rel);
    std::printf("  trigram Jaccard, query vs a random collection word: %.4f  (chance)\n", j_chance);
    std::printf("  ratio %.2fx -- co-category and shared affix are %s independent.\n",
                j_chance > 0 ? j_rel / j_chance : 0.0,
                j_rel > 1.15 * j_chance ? "NOT" : "roughly");
    std::printf("\n  engine         | spelling sim | correct top-10 | near-homograph | SEMANTIC\n");
    std::printf("                 |  of its top10 |     hits      |  (J>=%.2f)     | hits only\n", kHomograph);
    std::printf("  ---------------+---------------+---------------+----------------+----------\n");
    std::vector<std::size_t> sem_hits(rows.size(), 0);
    for (std::size_t ri = 0; ri < rows.size(); ++ri) {
        const auto& r = rows[ri];
        double jsum = 0.0; std::size_t jn = 0, hits = 0, homo = 0;
        for (const auto& qu : queries) {
            out.clear();
            (*r.fn)(qu.c, out);
            for (std::size_t i = 0; i < out.size() && i < 10; ++i) {
                const double j = jac(qu.c, out[i]);
                jsum += j; ++jn;
                if (qu.rel.count(out[i])) { ++hits; if (j >= kHomograph) ++homo; }
            }
        }
        sem_hits[ri] = hits - homo;
        std::printf("  %-14s |    %.4f     | %5zu         |  %4zu (%5.1f%%) | %5zu\n", r.name,
                    jn ? jsum / static_cast<double>(jn) : 0.0, hits, homo,
                    hits ? 100.0 * static_cast<double>(homo) / static_cast<double>(hits) : 0.0,
                    sem_hits[ri]);
    }
    std::printf("\n  The last column is P@10's hit count with the spelling relatives removed --\n"
                "  the part of each engine's score that is NOT recoverable from the letters.\n"
                "  It reorders the table, so read it before reading the one above.\n");

    // ---- the reading -------------------------------------------------------
    const Metrics& fr = rows[1].m;
    const Metrics& bmm = rows[2].m;
    Metrics best_plex = rows[3].m;
    const char* best_plex_name = rows[3].name;
    for (std::size_t i = 4; i <= 6; ++i)
        if (rows[i].m.ndcg > best_plex.ndcg) { best_plex = rows[i].m; best_plex_name = rows[i].name; }

    const double n = static_cast<double>(Q);
    std::printf("\n  === THE READING ===\n");
    std::printf("  best plexus engine: %s at nDCG@10 %.3f\n", best_plex_name, best_plex.ndcg / n);
    std::printf("  frequency baseline:               nDCG@10 %.3f\n", fr.ndcg / n);
    std::printf("  bm25-rm lexical baseline:         nDCG@10 %.3f\n", bmm.ndcg / n);
    const double d_fr = (best_plex.ndcg - fr.ndcg) / n;
    const double d_bm = (best_plex.ndcg - bmm.ndcg) / n;
    // A difference of paired per-query scores is what should be tested, but the
    // per-query scores are not retained; this is the unpaired SE, which is the
    // conservative one, so a gap called significant here is significant.
    const double se_fr = std::sqrt(std::pow(se_mean(best_plex.ndcg, best_plex.ndcg_sq, Q), 2)
                                 + std::pow(se_mean(fr.ndcg, fr.ndcg_sq, Q), 2));
    const double se_bm = std::sqrt(std::pow(se_mean(best_plex.ndcg, best_plex.ndcg_sq, Q), 2)
                                 + std::pow(se_mean(bmm.ndcg, bmm.ndcg_sq, Q), 2));
    std::printf("  plexus - frequency = %+.3f (unpaired SE %.3f) -> %s\n", d_fr, se_fr,
                std::fabs(d_fr) < 1.96 * se_fr ? "no difference"
                                               : (d_fr > 0 ? "plexus wins" : "PLEXUS LOSES"));
    std::printf("  plexus - bm25-rm   = %+.3f (unpaired SE %.3f) -> %s\n", d_bm, se_bm,
                std::fabs(d_bm) < 1.96 * se_bm ? "no difference"
                                               : (d_bm > 0 ? "plexus wins" : "PLEXUS LOSES"));
    std::printf("\n  and with the spelling relatives struck out, correct-in-top-10 hits:\n");
    for (std::size_t i = 0; i < rows.size(); ++i)
        std::printf("    %-14s %5zu\n", rows[i].name, sem_hits[i]);
    std::printf("\n  Read all of it against the caveat at the top of this file: WordNet\n"
                "  co-category is semantic relatedness, not relevance, and a system can be\n"
                "  useful for retrieval while losing here, or win here and be useless.\n");
    return 0;
}
