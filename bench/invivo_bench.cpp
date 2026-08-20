// IN VIVO: does any of this work on the books Khora has actually read?
//
// Every test written so far runs on Sdr::from_hash("symbol:A") -- synthetic
// sequences, in worlds of my own design, scored by metrics of my own choosing.
// Meanwhile Khora holds 23 MB of real Gutenberg text in its Reservoir, 82,695
// learned words in its Plexus, and 19,475 typed relations in its Ligature. The
// organs were built and never put in the animal.
//
// THE TEST. Train on the opening of several real books. Then ask the system to
// separate passages it HAS read from passages it has NOT -- taken from THE SAME
// BOOKS, further in. Same authors, same vocabulary, same register, same
// typographical quirks. The only thing that differs is whether these particular
// word sequences ever went past it.
//
// That is deliberately the hard version. Holding out a DIFFERENT book would be
// easy and would prove nothing: unfamiliar proper nouns alone would give it
// away, and the system would be detecting vocabulary rather than memory.
//
// AND IT IS SCORED AGAINST A BASELINE IT MUST BEAT. A trigram table over the
// same training text, scoring a passage by the fraction of its word triples
// never seen. That is perhaps thirty lines of counting with no representation,
// no learning rule and no biology, and if the temporal memory cannot beat it
// then none of the machinery is earning its keep on real data.

#include "khora/cortex/temporal_memory.hpp"
#include "khora/lattice/sdr.hpp"
#include "khora/reservoir/reservoir.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <vector>

using namespace khora::cortex;
using khora::lattice::Sdr;

namespace {

constexpr std::size_t kBooks      = 6;
constexpr std::size_t kTrainWords = 900;   // per book
constexpr std::size_t kTestWords  = 500;   // per book, taken AFTER the training span
constexpr std::size_t kWindow     = 8;     // words per passage

std::vector<std::string> tokenize(const std::string& text, std::size_t skip,
                                  std::size_t want) {
    std::vector<std::string> out;
    std::string cur;
    std::size_t seen = 0;
    for (const char ch : text) {
        if (std::isalpha(static_cast<unsigned char>(ch))) {
            cur += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        } else if (!cur.empty()) {
            if (seen++ >= skip) { out.push_back(cur); if (out.size() >= want) return out; }
            cur.clear();
        }
    }
    return out;
}

Sdr word_sdr(const std::string& w) { return Sdr::from_hash("w:" + w); }

// The baseline: count word triples, score a passage by how many of its triples
// are new. No representation, no learning rule, no biology.
struct TrigramModel {
    std::unordered_set<std::string> seen;

    void learn(const std::vector<std::string>& ws) {
        for (std::size_t i = 0; i + 2 < ws.size(); ++i)
            seen.insert(ws[i] + "\x1f" + ws[i + 1] + "\x1f" + ws[i + 2]);
    }
    double novelty(const std::vector<std::string>& ws) const {
        if (ws.size() < 3) return 1.0;
        std::size_t missing = 0, total = 0;
        for (std::size_t i = 0; i + 2 < ws.size(); ++i) {
            ++total;
            if (!seen.count(ws[i] + "\x1f" + ws[i + 1] + "\x1f" + ws[i + 2])) ++missing;
        }
        return total ? static_cast<double>(missing) / total : 1.0;
    }
};

double tm_novelty(TemporalMemory& tm, const std::vector<std::string>& ws) {
    tm.reset();
    double sum = 0.0;
    std::size_t n = 0;
    for (std::size_t i = 0; i < ws.size(); ++i) {
        const auto st = tm.compute(word_sdr(ws[i]), false);
        if (i > 0) { sum += st.anomaly; ++n; }
    }
    return n ? sum / n : 1.0;
}

double auc(const std::vector<double>& pos, const std::vector<double>& neg) {
    std::vector<std::pair<double, int>> all;
    for (const double v : pos) all.emplace_back(v, 1);
    for (const double v : neg) all.emplace_back(v, 0);
    std::sort(all.begin(), all.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    double rank_sum = 0.0;
    std::size_t i = 0;
    while (i < all.size()) {
        std::size_t j = i;
        while (j + 1 < all.size() && all[j + 1].first == all[i].first) ++j;
        const double avg = (static_cast<double>(i + j) / 2.0) + 1.0;
        for (std::size_t k = i; k <= j; ++k) if (all[k].second == 1) rank_sum += avg;
        i = j + 1;
    }
    const double n1 = static_cast<double>(pos.size()), n0 = static_cast<double>(neg.size());
    return (n1 && n0) ? (rank_sum - n1 * (n1 + 1) / 2.0) / (n1 * n0) : 0.5;
}

} // namespace

int main(int argc, char** argv) {
    const std::string dir = (argc > 1) ? argv[1] : "data/reservoir";
    khora::reservoir::Reservoir res(dir);
    res.load_catalog();
    const auto cat = res.catalog();
    std::printf("In vivo: real books from Khora's own Reservoir\n");
    std::printf("  %zu tomes held, %.1f MB\n",
                res.count(), res.total_stored_bytes() / 1048576.0);
    if (cat.empty()) { std::printf("  empty catalog -- pass the reservoir dir\n"); return 0; }

    TemporalMemory tm(TemporalMemoryConfig::episodic());
    TrigramModel   tri;

    std::vector<std::vector<std::string>> train_words, test_words;
    std::size_t used = 0;

    for (const auto& t : cat) {
        if (used >= kBooks) break;
        auto text = res.read(t.title);
        if (!text || text->size() < 40000) continue;
        auto tr = tokenize(*text, 200, kTrainWords);            // skip front matter
        auto te = tokenize(*text, 200 + kTrainWords + 500, kTestWords);
        if (tr.size() < kTrainWords || te.size() < kTestWords) continue;
        std::printf("  [%zu] %.58s  (%zu train words, %zu held-out)\n",
                    used, t.title.c_str(), tr.size(), te.size());
        train_words.push_back(std::move(tr));
        test_words.push_back(std::move(te));
        ++used;
    }
    if (used == 0) { std::printf("  no usable tomes\n"); return 0; }

    // Learn the training span of each book, in passages.
    for (const auto& ws : train_words) {
        tri.learn(ws);
        for (std::size_t i = 0; i + kWindow <= ws.size(); i += kWindow) {
            tm.reset();
            for (std::size_t k = 0; k < kWindow; ++k) tm.compute(word_sdr(ws[i + k]), true);
        }
    }
    std::printf("\n  learned %zu books, %zu words each -> %zu segments, %zu trigrams\n",
                used, kTrainWords, tm.segment_count(), tri.seen.size());

    // Score passages: seen (from the training span) against unseen (later in the
    // SAME books). Same authors, same vocabulary -- only the sequences differ.
    std::vector<double> tm_seen, tm_unseen, tri_seen, tri_unseen;
    for (std::size_t b = 0; b < used; ++b) {
        const auto& tr = train_words[b];
        const auto& te = test_words[b];
        for (std::size_t i = 0; i + kWindow <= tr.size(); i += kWindow) {
            const std::vector<std::string> w(tr.begin() + i, tr.begin() + i + kWindow);
            tm_seen.push_back(tm_novelty(tm, w));
            tri_seen.push_back(tri.novelty(w));
        }
        for (std::size_t i = 0; i + kWindow <= te.size(); i += kWindow) {
            const std::vector<std::string> w(te.begin() + i, te.begin() + i + kWindow);
            tm_unseen.push_back(tm_novelty(tm, w));
            tri_unseen.push_back(tri.novelty(w));
        }
    }

    const auto mean = [](const std::vector<double>& v) {
        double s = 0.0; for (const double x : v) s += x; return v.empty() ? 0.0 : s / v.size();
    };

    std::printf("\n  %zu seen passages, %zu held-out passages, %zu words each\n",
                tm_seen.size(), tm_unseen.size(), kWindow);
    std::printf("\n  model            | novelty: seen | held-out |   AUC\n");
    std::printf("  -----------------+---------------+----------+--------\n");
    std::printf("  trigram table    |    %.3f      |  %.3f   | %.4f\n",
                mean(tri_seen), mean(tri_unseen), auc(tri_unseen, tri_seen));
    std::printf("  temporal memory  |    %.3f      |  %.3f   | %.4f\n",
                mean(tm_seen), mean(tm_unseen), auc(tm_unseen, tm_seen));

    std::printf("\n  Held-out passages come from the SAME books, further in: same\n"
                "  authors, same vocabulary, same register. Only the specific word\n"
                "  sequences differ. AUC 0.5 is a coin flip.\n");

    // ---------------------------------------------------------------------
    // THE TEST THE BASELINE SHOULD LOSE.
    //
    // Above, a hash set of word triples wins outright -- and it should. The
    // question asked was "have I seen this exact sequence", which is an
    // exact-match membership query, and a hash set IS the correct data
    // structure for that. No amount of representation beats it. That test was
    // rigged in the baseline's favour by construction, and reporting it is the
    // point rather than an embarrassment.
    //
    // The property the sparse substrate was actually adopted for is a different
    // one: a segment stores ~24 of the active bits and fires at a threshold, so
    // it should still recognise a pattern that has been PARTLY CORRUPTED. Exact
    // matching cannot do that at all -- change one word and three triples miss
    // together.
    //
    // So: take passages the system HAS read, corrupt a growing fraction of the
    // words with words drawn from elsewhere in the same corpus, and ask each
    // model whether it still recognises them. The degradation curve is the
    // measurement.
    std::printf("\n  RECOGNISING WHAT IT HAS READ, THROUGH CORRUPTION\n");
    std::printf("  Passages the system HAS seen, with k of %zu words replaced by\n"
                "  words drawn from elsewhere in the same corpus.\n\n", kWindow);

    // Replacements come from the corpus so the corruption stays in register;
    // swapping in nonsense would be a much easier problem.
    std::vector<std::string> pool;
    for (const auto& ws : train_words)
        for (const auto& w : ws) if (w.size() > 3) pool.push_back(w);

    std::uint64_t seed = 0xABCDEF12345ULL;
    auto rnd = [&seed]() {
        std::uint64_t z = (seed += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    };

    std::printf("    corrupted | trigram novelty | temporal-memory novelty\n");
    std::printf("    ----------+-----------------+------------------------\n");
    for (const std::size_t k : {std::size_t{0}, std::size_t{1}, std::size_t{2},
                                std::size_t{3}, std::size_t{4}}) {
        std::vector<double> t_nov, m_nov;
        for (std::size_t b = 0; b < used; ++b) {
            const auto& tr = train_words[b];
            for (std::size_t i = 0; i + kWindow <= tr.size(); i += kWindow * 2) {
                std::vector<std::string> w(tr.begin() + i, tr.begin() + i + kWindow);
                for (std::size_t c = 0; c < k; ++c)
                    w[rnd() % kWindow] = pool[rnd() % pool.size()];
                t_nov.push_back(tri.novelty(w));
                m_nov.push_back(tm_novelty(tm, w));
            }
        }
        std::printf("    %zu of %zu   |      %.3f      |         %.3f\n",
                    k, kWindow, mean(t_nov), mean(m_nov));
    }
    std::printf("\n    Lower is better here: these are passages the system HAS read,\n"
                "    so a model that still recognises one reports low novelty for it.\n");
    return 0;
}
