// DOES CHOOSING WHAT TO READ BEAT READING AT RANDOM?
//
// Khora's Curator decides, unprompted, what to study next. That is the whole
// premise of self-directed learning, and an audit found nothing anywhere in the
// tree that measures it: no acquisition function, no query strategy, no
// uncertainty sampling, and -- the part that matters -- no comparison of the
// Curator's de facto policy against the only baseline that decides whether any
// of it is worth having, which is reading the same corpus in a random order.
//
// This is that comparison. Fixed TOKEN budget, not a fixed number of books: a
// policy that wins by reading more has measured nothing. Every policy consumes
// the same budget to within one sentence, and every policy is scored on books
// none of them was allowed to touch.
//
// WHAT THE CURATOR ACTUALLY DOES, read out of src/curator/curator.cpp:
//
//   decide() step 1 : the first tome with times_read == 0, in catalog order.
//   decide() step 2 : FORAGE a topic with no material -- productive topics
//                     (mathematics, physics, chemistry, engineering, logic,
//                     science, strategy, economics) before the rest.
//   decide() step 3 : DEEPEN -- acquire more, same productive-first order.
//   decide() step 4 : re-study the lowest-mastery tome, capped at 4 reads.
//
// Over a corpus that is ALREADY HELD -- which is the situation whenever the
// network is not being hit -- steps 2 and 3 never fire, and step 1 is the whole
// policy. Step 1 has no content term at all. The Curator's study order over
// held material is catalog order, i.e. the order the books happened to be
// acquired in. That is measured below as `curator-catalog`. The productive-topic
// preference from steps 2/3 does shape what the catalog eventually contains, so
// its projection onto study order is measured too, as `curator-topic`.
//
// WHAT THIS HARNESS CANNOT SEE
//
//  * It does not call the real Curator. bench/CMakeLists.txt links this target
//    against lexicon, plexus, reservoir and ligature; the Curator lives in
//    khora::curator and drags in cortex, lattice and aqueduct. The policy is
//    REIMPLEMENTED from decide() above, not invoked.
//  * The learner is a bigram counter plus the Ligature, not Khora's Cortex /
//    Lexicon / Plexus. Random indexing over 27 different orderings of millions
//    of tokens is minutes apiece. So this measures what an ORDER MAKES
//    AVAILABLE to a cheap learner. It does not prove the hypervector substrate
//    would rank the orders the same way.
//  * novelty, novelty-rate and diversity read each candidate's full vocabulary
//    BEFORE choosing it. No deployed curator has that oracle. They are upper
//    bounds on those families, not implementable policies.
//  * Re-reading is not modelled -- each policy reads each book at most once.
//    The Curator's step 4 re-reads; under a fixed token budget a re-read buys
//    zero new held-out coverage, so this is the Curator's best case.
//  * All policies are scored on the SAME held-out set, so the comparisons are
//    paired. The Wilson intervals printed are marginal and therefore
//    conservative for policy-vs-policy; the random spread is the real yardstick.
//  * "What was learned" is held-out vocabulary, held-out next-token prediction
//    and typed relations. It is not comprehension, and nothing here measures
//    whether the Ligature's triples are TRUE (bench/extraction_bench.cpp does).

#include "khora/lexicon/lexicon.hpp"
#include "khora/ligature/ligature.hpp"
#include "khora/reservoir/reservoir.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using khora::ligature::Ligature;

// 95% Wilson interval on a proportion -- same helper the extraction bench uses.
// Printed so a rate is never mistaken for a measurement of unknown precision.
std::pair<double, double> wilson(std::size_t hits, std::size_t n) {
    if (n == 0) return {0.0, 0.0};
    const double z = 1.96, ph = static_cast<double>(hits) / static_cast<double>(n);
    const double d = 1.0 + z * z / static_cast<double>(n);
    const double c = ph + z * z / (2.0 * static_cast<double>(n));
    const double m = z * std::sqrt(ph * (1.0 - ph) / static_cast<double>(n)
                                   + z * z / (4.0 * static_cast<double>(n) * static_cast<double>(n)));
    return {100.0 * (c - m) / d, 100.0 * (c + m) / d};
}

// The Curator's own list, copied verbatim from the anonymous namespace in
// src/curator/curator.cpp. If that list changes this one is stale.
bool productive_topic(const std::string& t) {
    return t == "mathematics" || t == "physics"  || t == "chemistry" ||
           t == "engineering"  || t == "logic"   || t == "science"   ||
           t == "strategy"     || t == "economics";
}

struct Book {
    std::string title, topic;
    std::vector<std::vector<std::uint32_t>> sents;   // interned, sentence-split
    std::size_t tokens = 0;
    std::vector<std::uint32_t> types;                // sorted distinct ids
    bool held_out = false;
};

// The evaluation set. Built once, before any policy runs, and never shown to
// one. Denominators are therefore identical for every policy at every budget.
struct HeldOut {
    std::vector<std::uint32_t> types;      // distinct types in held-out books
    std::vector<std::uint64_t> type_occ;   // parallel: occurrences of each
    std::size_t tokens = 0;
    std::vector<std::uint64_t> bigrams;    // (prev<<32)|next, within sentences
};

struct Snapshot {
    std::size_t books = 0, tokens = 0;
    std::size_t cov_types = 0, cov_tok = 0;   // held-out coverage numerators
    std::size_t hits = 0;                     // held-out next-token top-1 hits
    double      bits = 0.0;                   // held-out bits/token
    std::size_t triples = 0, trip2 = 0;       // Ligature: distinct, support>=2
};

inline std::uint64_t pack(std::uint32_t a, std::uint32_t b) {
    return (static_cast<std::uint64_t>(a) << 32) | b;
}

// Read an ORDER of books under a rising token budget, snapshotting what has
// been learned each time a budget line is crossed. Budgets are nested (10% is a
// prefix of 25% is a prefix of ...), so one streaming pass answers all four --
// which is the only reason 27 orderings fit in the time allowed.
std::vector<Snapshot> run_order(const std::vector<const Book*>& order,
                                const std::vector<std::size_t>& budgets,
                                const HeldOut& ho,
                                const std::vector<std::string>& vocab) {
    const std::size_t V = vocab.size();
    std::vector<char>          seen(V, 0);
    std::vector<std::uint32_t> uni(V, 0);
    std::unordered_map<std::uint64_t, std::uint32_t> bi;
    bi.reserve(1u << 21);
    Ligature lig;
    std::vector<std::string> buf;
    std::size_t ntok = 0, nbooks = 0;
    std::vector<Snapshot> out;

    auto snap = [&]() {
        Snapshot r;
        r.books = nbooks;
        r.tokens = ntok;
        for (std::size_t i = 0; i < ho.types.size(); ++i)
            if (seen[ho.types[i]]) { ++r.cov_types; r.cov_tok += static_cast<std::size_t>(ho.type_occ[i]); }

        // Top-1 next-token: the most-frequent successor of the context. A
        // context never seen in training simply has no prediction and the
        // position counts as a miss -- the denominator must not move with the
        // policy, or a policy could win by predicting less often.
        std::unordered_map<std::uint32_t, std::pair<std::uint32_t, std::uint32_t>> best;
        best.reserve(bi.size() / 3 + 1);
        for (const auto& kv : bi) {
            const std::uint32_t a = static_cast<std::uint32_t>(kv.first >> 32);
            const std::uint32_t b = static_cast<std::uint32_t>(kv.first);
            auto& e = best[a];
            if (kv.second > e.second || (kv.second == e.second && b < e.first)) { e.first = b; e.second = kv.second; }
        }
        // Interpolated bigram/unigram, add-one over the FULL corpus vocabulary.
        // V is fixed across policies on purpose: normalising by each policy's
        // own vocabulary would pay a policy for having learned fewer words.
        const double nd = static_cast<double>(ntok), Vd = static_cast<double>(V);
        double s = 0.0;
        for (const std::uint64_t pk : ho.bigrams) {
            const std::uint32_t a = static_cast<std::uint32_t>(pk >> 32);
            const std::uint32_t b = static_cast<std::uint32_t>(pk);
            const auto ib = best.find(a);
            if (ib != best.end() && ib->second.first == b) ++r.hits;
            double p = 0.3 * (static_cast<double>(uni[b]) + 1.0) / (nd + Vd);
            if (uni[a]) {
                const auto it = bi.find(pk);
                if (it != bi.end()) p += 0.7 * static_cast<double>(it->second) / static_cast<double>(uni[a]);
            }
            s -= std::log2(p);
        }
        r.bits    = ho.bigrams.empty() ? 0.0 : s / static_cast<double>(ho.bigrams.size());
        r.triples = static_cast<std::size_t>(lig.triple_count());
        r.trip2   = lig.all(2).size();
        return r;
    };

    for (const Book* b : order) {
        if (ntok >= budgets.back()) break;
        ++nbooks;
        for (const auto& s : b->sents) {
            if (s.empty()) continue;
            buf.resize(s.size());
            for (std::size_t i = 0; i < s.size(); ++i) {
                const std::uint32_t id = s[i];
                seen[id] = 1; ++uni[id];
                buf[i] = vocab[id];
            }
            for (std::size_t i = 0; i + 1 < s.size(); ++i) ++bi[pack(s[i], s[i + 1])];
            ntok += s.size();
            lig.extract(buf);
            while (out.size() < budgets.size() && ntok >= budgets[out.size()]) out.push_back(snap());
            if (ntok >= budgets.back()) break;
        }
    }
    while (out.size() < budgets.size()) out.push_back(snap());
    return out;
}

// Spread of a metric over the random draws. A single random ordering is not a
// baseline; the distribution is.
struct Dist {
    double mean = 0, sd = 0, mn = 0, mx = 0;
    double z(double v) const { return sd > 1e-12 ? (v - mean) / sd : 0.0; }
};
Dist dist_of(const std::vector<double>& v) {
    Dist d;
    if (v.empty()) return d;
    for (double x : v) d.mean += x;
    d.mean /= static_cast<double>(v.size());
    for (double x : v) d.sd += (x - d.mean) * (x - d.mean);
    d.sd = std::sqrt(d.sd / static_cast<double>(v.size() > 1 ? v.size() - 1 : 1));
    d.mn = *std::min_element(v.begin(), v.end());
    d.mx = *std::max_element(v.begin(), v.end());
    return d;
}

std::size_t overlap(const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b) {
    std::size_t n = 0, i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] < b[j]) ++i; else if (b[j] < a[i]) ++j; else { ++n; ++i; ++j; }
    }
    return n;
}

} // namespace

int main(int argc, char** argv) {
    const std::string dir   = (argc > 1) ? argv[1] : "data/reservoir";
    const std::size_t maxb  = (argc > 2) ? std::stoul(argv[2]) : 80;
    const std::size_t nrand = (argc > 3) ? std::stoul(argv[3]) : 20;
    const auto        t0    = std::chrono::steady_clock::now();

    khora::reservoir::Reservoir res(dir);
    const auto cat = res.catalog();
    std::printf("Study order, measured on Khora's own corpus\n");
    if (cat.empty()) { std::printf("  empty catalog at %s\n", dir.c_str()); return 0; }

    // ---- load, sentence-split, intern -------------------------------------
    std::unordered_map<std::string, std::uint32_t> vocab_id;
    std::vector<std::string> vocab;
    vocab_id.reserve(1u << 19);
    std::vector<Book> books;
    for (const auto& t : cat) {
        if (books.size() >= maxb) break;
        auto text = res.read(t.title);
        if (!text || text->size() < 20000) continue;
        Book b;
        b.title = t.title;
        b.topic = t.topic;
        for (auto& s : khora::lexicon::tokenize_sentences(*text)) {
            if (s.empty()) continue;
            std::vector<std::uint32_t> ids;
            ids.reserve(s.size());
            for (auto& w : s) {
                auto it = vocab_id.find(w);
                if (it == vocab_id.end()) {
                    it = vocab_id.emplace(w, static_cast<std::uint32_t>(vocab.size())).first;
                    vocab.push_back(w);
                }
                ids.push_back(it->second);
            }
            b.tokens += ids.size();
            b.sents.push_back(std::move(ids));
        }
        if (b.tokens == 0) continue;
        for (const auto& s : b.sents) b.types.insert(b.types.end(), s.begin(), s.end());
        std::sort(b.types.begin(), b.types.end());
        b.types.erase(std::unique(b.types.begin(), b.types.end()), b.types.end());
        books.push_back(std::move(b));
    }
    if (books.size() < 8) { std::printf("  only %zu usable books -- nothing to compare\n", books.size()); return 0; }

    // ---- split ------------------------------------------------------------
    // Every 4th book in catalog order is held out. Fixed before any policy is
    // constructed, and systematic rather than random so the held-out set spans
    // the whole acquisition history (early literature through late junk-topic
    // material) instead of clustering wherever a seed happened to land.
    HeldOut ho;
    std::vector<const Book*> pool;
    std::size_t pool_tokens = 0, ho_books = 0;
    {
        std::unordered_map<std::uint32_t, std::uint64_t> occ;
        for (std::size_t i = 0; i < books.size(); ++i) {
            if (i % 4 == 3) {
                books[i].held_out = true;
                ++ho_books;
                ho.tokens += books[i].tokens;
                for (const auto& s : books[i].sents) {
                    for (std::size_t k = 0; k < s.size(); ++k) {
                        ++occ[s[k]];
                        if (k + 1 < s.size()) ho.bigrams.push_back(pack(s[k], s[k + 1]));
                    }
                }
            } else {
                pool.push_back(&books[i]);
                pool_tokens += books[i].tokens;
            }
        }
        ho.types.reserve(occ.size());
        for (const auto& kv : occ) ho.types.push_back(kv.first);
        std::sort(ho.types.begin(), ho.types.end());
        ho.type_occ.reserve(ho.types.size());
        for (auto id : ho.types) ho.type_occ.push_back(occ[id]);
    }

    const std::vector<double> fracs = {0.10, 0.25, 0.50, 1.00};
    std::vector<std::size_t> budgets;
    for (double f : fracs) budgets.push_back(static_cast<std::size_t>(f * static_cast<double>(pool_tokens)));

    std::printf("  %zu books read, %zu in the choosable pool (%zu tokens), %zu held out (%zu tokens)\n",
                books.size(), pool.size(), pool_tokens, ho_books, ho.tokens);
    std::printf("  corpus vocabulary %zu types; held-out set %zu types, %zu scored bigrams\n",
                vocab.size(), ho.types.size(), ho.bigrams.size());
    std::printf("  budgets: ");
    for (std::size_t i = 0; i < budgets.size(); ++i)
        std::printf("%.0f%%=%zu%s", 100 * fracs[i], budgets[i], i + 1 < budgets.size() ? ", " : "\n");

    // ---- policies ---------------------------------------------------------
    const std::size_t n = pool.size();
    std::vector<std::pair<std::string, std::vector<const Book*>>> policies;

    auto ident = [&] { std::vector<std::size_t> v(n); for (std::size_t i = 0; i < n; ++i) v[i] = i; return v; };
    auto materialise = [&](const std::vector<std::size_t>& idx) {
        std::vector<const Book*> o; o.reserve(idx.size());
        for (auto i : idx) o.push_back(pool[i]);
        return o;
    };

    // 1. The Curator's literal rule over held material: catalog order.
    policies.emplace_back("curator-catalog", materialise(ident()));

    // 2. The Curator's productive-topic preference (decide() steps 2/3)
    //    projected onto study order: productive domains first, catalog order
    //    within each group.
    {
        auto v = ident();
        std::stable_partition(v.begin(), v.end(),
                              [&](std::size_t i) { return productive_topic(pool[i]->topic); });
        policies.emplace_back("curator-topic", materialise(v));
    }

    // 3/4. Novelty (uncertainty) sampling: greedily take the book contributing
    //      the most types the learner has not yet seen. Absolute, then per
    //      token -- the pair separates "new words" from "more words", which is
    //      the confound longest-first exists to expose.
    for (int rate = 0; rate < 2; ++rate) {
        std::vector<char> got(vocab.size(), 0), used(n, 0);
        std::vector<std::size_t> v;
        for (std::size_t k = 0; k < n; ++k) {
            std::size_t pick = n; double bestsc = -1.0;
            for (std::size_t j = 0; j < n; ++j) {
                if (used[j]) continue;
                std::size_t fresh = 0;
                for (auto id : pool[j]->types) if (!got[id]) ++fresh;
                const double sc = rate ? static_cast<double>(fresh) / static_cast<double>(pool[j]->tokens)
                                       : static_cast<double>(fresh);
                if (sc > bestsc) { bestsc = sc; pick = j; }
            }
            used[pick] = 1;
            for (auto id : pool[pick]->types) got[id] = 1;
            v.push_back(pick);
        }
        policies.emplace_back(rate ? "novelty-rate" : "novelty", materialise(v));
    }

    // 5. Diversity sampling: k-centre greedy on vocabulary Jaccard distance --
    //    each pick is the book furthest from everything already chosen. Seeded
    //    with the largest-vocabulary book so it is deterministic.
    {
        std::vector<std::vector<double>> d(n, std::vector<double>(n, 0.0));
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t j = i + 1; j < n; ++j) {
                const std::size_t inter = overlap(pool[i]->types, pool[j]->types);
                const std::size_t un    = pool[i]->types.size() + pool[j]->types.size() - inter;
                d[i][j] = d[j][i] = un ? 1.0 - static_cast<double>(inter) / static_cast<double>(un) : 0.0;
            }
        std::vector<char> used(n, 0);
        std::vector<std::size_t> v;
        std::size_t first = 0;
        for (std::size_t j = 1; j < n; ++j) if (pool[j]->types.size() > pool[first]->types.size()) first = j;
        used[first] = 1; v.push_back(first);
        std::vector<double> mind(n, 0.0);
        for (std::size_t j = 0; j < n; ++j) mind[j] = d[first][j];
        while (v.size() < n) {
            std::size_t pick = n; double bestsc = -1.0;
            for (std::size_t j = 0; j < n; ++j) if (!used[j] && mind[j] > bestsc) { bestsc = mind[j]; pick = j; }
            used[pick] = 1; v.push_back(pick);
            for (std::size_t j = 0; j < n; ++j) mind[j] = std::min(mind[j], d[pick][j]);
        }
        policies.emplace_back("diversity", materialise(v));
    }

    // 6/7. Pure ordering effects. Very often the whole explanation.
    for (int longest = 1; longest >= 0; --longest) {
        auto v = ident();
        std::stable_sort(v.begin(), v.end(), [&](std::size_t a, std::size_t b) {
            return longest ? pool[a]->tokens > pool[b]->tokens : pool[a]->tokens < pool[b]->tokens;
        });
        policies.emplace_back(longest ? "longest-first" : "shortest-first", materialise(v));
    }

    std::printf("\n  what each policy reads first (at the 10%% budget little past this is reached)\n");
    for (const auto& p : policies) {
        std::printf("    %-15s:", p.first.c_str());
        for (std::size_t i = 0; i < 4 && i < p.second.size(); ++i) {
            std::string t = p.second[i]->title;
            if (t.size() > 22) t = t.substr(0, 21) + ".";
            std::printf(" %s |", t.c_str());
        }
        std::printf(" ...\n");
    }

    // ---- run --------------------------------------------------------------
    std::vector<std::vector<Snapshot>> pol_res;
    for (const auto& p : policies) pol_res.push_back(run_order(p.second, budgets, ho, vocab));

    std::vector<std::vector<Snapshot>> rnd_res;
    for (std::size_t r = 0; r < nrand; ++r) {
        auto v = ident();
        std::mt19937_64 rng(1000 + r);
        std::shuffle(v.begin(), v.end(), rng);
        rnd_res.push_back(run_order(materialise(v), budgets, ho, vocab));
    }

    // ---- report -----------------------------------------------------------
    const double hoT = static_cast<double>(ho.types.size());
    const double hoK = static_cast<double>(ho.tokens);
    const double hoB = static_cast<double>(ho.bigrams.size());

    for (std::size_t bx = 0; bx < budgets.size(); ++bx) {
        std::printf("\n  === BUDGET %.0f%% OF THE POOL = %zu tokens ===\n", 100 * fracs[bx], budgets[bx]);

        std::vector<double> rt, rk, ra, rb, rr, rbk;
        for (const auto& rs : rnd_res) {
            rt.push_back(100.0 * rs[bx].cov_types / hoT);
            rk.push_back(100.0 * rs[bx].cov_tok   / hoK);
            ra.push_back(100.0 * rs[bx].hits      / hoB);
            rb.push_back(rs[bx].bits);
            rr.push_back(static_cast<double>(rs[bx].trip2));
            rbk.push_back(static_cast<double>(rs[bx].books));
        }
        const Dist dt = dist_of(rt), dk = dist_of(rk), da = dist_of(ra),
                   db = dist_of(rb), dr = dist_of(rr), dbk = dist_of(rbk);

        std::printf("    policy          | books |  ho types cov 95%% CI      | ho tok |  top-1 next tok 95%% CI    | bits/tok | rel s>=2\n");
        std::printf("    ----------------+-------+---------------------------+--------+---------------------------+----------+---------\n");
        std::printf("    RANDOM mean     | %5.1f | %6.2f%%                    | %5.2f%% | %6.2f%%                    | %8.4f | %7.0f\n",
                    dbk.mean, dt.mean, dk.mean, da.mean, db.mean, dr.mean);
        std::printf("    RANDOM sd       | %5.1f | %6.2f                     | %5.2f  | %6.3f                     | %8.4f | %7.0f\n",
                    dbk.sd, dt.sd, dk.sd, da.sd, db.sd, dr.sd);
        std::printf("    RANDOM min..max | %2.0f-%-2.0f | %6.2f..%-6.2f            | %5.2f  | %6.2f..%-6.2f            | %4.1f-%-4.1f| %4.0f-%-4.0f\n",
                    dbk.mn, dbk.mx, dt.mn, dt.mx, dk.mn, da.mn, da.mx, db.mn, db.mx, dr.mn, dr.mx);
        for (std::size_t p = 0; p < policies.size(); ++p) {
            const Snapshot& s = pol_res[p][bx];
            const auto ct = wilson(s.cov_types, ho.types.size());
            const auto ca = wilson(s.hits, ho.bigrams.size());
            std::printf("    %-15s | %5zu | %6.2f%% [%5.2f, %5.2f] | %5.2f%% | %6.2f%% [%5.2f, %5.2f] | %8.4f | %7zu\n",
                        policies[p].first.c_str(), s.books,
                        100.0 * s.cov_types / hoT, ct.first, ct.second,
                        100.0 * s.cov_tok / hoK,
                        100.0 * s.hits / hoB, ca.first, ca.second,
                        s.bits, s.trip2);
        }

        // The z-scores assume the random draws are roughly normal, which with 20
        // draws is an assumption and not a fact. The win counts beside them are
        // the same comparison without that assumption: how many of the N random
        // orderings this policy actually beat, head to head, on the same
        // held-out set. 20/20 or 0/20 is a result; 11/20 is a coin.
        std::printf("\n    z from the random mean (+ better; bits/tok sign flipped) and draws beaten out of %zu\n", nrand);
        std::printf("    policy          | z types | z tokcov | z top-1 | z bits | z rel | types | top-1 | bits\n");
        std::printf("    ----------------+---------+----------+---------+--------+-------+-------+-------+------\n");
        for (std::size_t p = 0; p < policies.size(); ++p) {
            const Snapshot& s = pol_res[p][bx];
            const double vt = 100.0 * s.cov_types / hoT;
            const double va = 100.0 * s.hits / hoB;
            std::size_t wt = 0, wa = 0, wb = 0;
            for (std::size_t r = 0; r < nrand; ++r) {
                if (vt      > rt[r]) ++wt;
                if (va      > ra[r]) ++wa;
                if (s.bits  < rb[r]) ++wb;   // fewer bits per token is better
            }
            std::printf("    %-15s | %+7.2f | %+8.2f | %+7.2f | %+6.2f | %+5.2f | %2zu/%-2zu | %2zu/%-2zu | %2zu/%-2zu\n",
                        policies[p].first.c_str(),
                        dt.z(vt), dk.z(100.0 * s.cov_tok / hoK), da.z(va),
                        -db.z(s.bits), dr.z(static_cast<double>(s.trip2)),
                        wt, nrand, wa, nrand, wb, nrand);
        }
    }

    const auto secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("\n  === WHAT THIS DOES AND DOES NOT SHOW ===\n");
    std::printf("    The Curator is NOT called. bench/CMakeLists.txt links this target against\n"
                "    lexicon, plexus, reservoir, ligature; khora::curator needs cortex, lattice and\n"
                "    aqueduct. `curator-catalog` is decide() step 1 reimplemented -- first tome with\n"
                "    times_read == 0, in catalog order -- which over already-held material is the\n"
                "    entire policy, because FORAGE and DEEPEN only fire when a topic has no material.\n"
                "    `curator-topic` is the productive-first preference of steps 2/3 projected onto\n"
                "    study order. Neither has a content term over books already held.\n");
    std::printf("    The learner is a bigram counter plus the Ligature, not Cortex/Lexicon/Plexus:\n"
                "    random indexing over %zu orderings of %zu tokens is minutes each. This measures\n"
                "    what an ORDER makes available to a cheap learner, not what the hypervector\n"
                "    substrate would do with the same order.\n", policies.size() + nrand, pool_tokens);
    std::printf("    novelty, novelty-rate and diversity inspect a candidate's full vocabulary before\n"
                "    reading it. That oracle does not exist at runtime; they bound their family.\n");
    std::printf("    Budgets are cut at a sentence boundary, so every policy consumes the stated\n"
                "    budget to within one sentence. Nobody wins by reading more. At the 100%% budget\n"
                "    every policy has read the whole pool, so every row there MUST be identical --\n"
                "    that is the harness checking its own budget accounting, not a result.\n");
    std::printf("    The `books` column is the mechanism: at a fixed token budget an order that\n"
                "    front-loads short books touches more distinct authors, registers and centuries,\n"
                "    and held-out vocabulary is exactly what that buys. Nothing here is semantic.\n");
    std::printf("    Every policy is scored on the same held-out books, so the differences are\n"
                "    paired and the Wilson intervals above (marginal) overstate the comparison error.\n"
                "    The random spread, not the Wilson interval, is what a policy has to clear.\n");
    std::printf("    Re-reading is not modelled; the Curator's step-4 re-reads would only spend\n"
                "    budget on already-seen tokens, so this is its best case.\n");
    std::printf("    %.1f s\n", secs);
    return 0;
}
