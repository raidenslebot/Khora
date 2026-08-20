// CATEGORY INDUCTION, EVALUATED AGAINST AN EXTERNAL GOLD STANDARD
//
// An earlier version of this used six categories whose member words I chose
// myself, and reported a win. That was not a limit to disclose, it was
// unfinished work -- the sample can be made large and the choosing handed to
// somebody else. Done properly, the win disappeared.
//
// Ground truth is WordNet 3.1: 3,373 noun categories over 54,135 member words,
// built by lexicographers at Princeton across three decades with no knowledge of
// Khora. It is used ONLY as an answer key. Nothing from it enters any code, any
// graph, or any representation -- Khora's word codes come entirely from books it
// read.
//
// THE TASK. Given a few members of a category, rank the REST of that category
// above words drawn from the vocabulary. No is-a relation is stored anywhere:
// the category is the intersection of its members' distributed codes, and
// membership is overlap with that intersection.
//
// THE NEGATIVES ARE FREQUENCY-MATCHED, which is the difference between a real
// result and a fake one. Drawn uniformly, "guess whichever word is commoner"
// scores 0.6566 against a 0.4889 floor and beats every real method -- not
// because frequency is clever, but because WordNet's well-populated categories
// are full of common words. Each stranger is now drawn from the same frequency
// octave as the member it stands against, which forces that baseline to chance
// by construction.
//
// AND THE PARAMETERS ARE SWEPT, because a weak number is not a finding until it
// is established that the idea is weak rather than the guess.

#include "khora/lattice/sdr.hpp"
#include "khora/plexus/plexus.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using khora::plexus::Plexus;
using namespace khora::lattice;

namespace {

std::uint64_t rs = 0xE7A15EEDULL;

std::uint64_t rnd() {
    std::uint64_t z = (rs += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

struct WnCategory {
    std::string name;
    std::vector<std::string> members;
};

// A word's code is the SET of its strongest associates' codes. The associates
// keep random codes; structure enters one level up, from WHICH associates a word
// has -- exactly as a cortical cell's selectivity comes from what projects to it
// rather than from anything intrinsic to the cell.
SdrUnion context_code(const Plexus& plex, std::uint32_t id, std::size_t top_k) {
    SdrUnion u;
    const std::string self(plex.node_name(id));
    std::vector<std::pair<double, std::string>> ranked;
    for (const auto& [nb, c] : plex.neighbours(id)) {
        const std::string name(plex.node_name(nb));
        const double a = plex.affinity(self, name);
        if (a > 0.0) ranked.emplace_back(a * std::log2(1.0 + c), name);
    }
    std::sort(ranked.rbegin(), ranked.rend());
    for (std::size_t i = 0; i < ranked.size() && i < top_k; ++i)
        u.add(Sdr::from_hash("ctx:" + ranked[i].second));
    return u;
}

double similarity(const SdrUnion& a, const SdrUnion& b) {
    std::size_t both = 0, either = 0;
    for (std::size_t blk = 0; blk < kSdrBlocks; ++blk) {
        const std::uint64_t x = a.mask()[blk], y = b.mask()[blk];
        both   += static_cast<std::size_t>(std::popcount(x & y));
        either += static_cast<std::size_t>(std::popcount(x | y));
    }
    return either ? static_cast<double>(both) / either : 0.0;
}

// Positions shared by at least `frac` of the members. frac = 1.0 is a strict
// intersection; anything lower is a majority vote, which tolerates one member
// whose usage in these particular books is idiosyncratic.
SdrUnion quorum_of(const std::vector<SdrUnion>& ms, double frac) {
    SdrUnion out;
    if (ms.empty()) return out;
    const std::size_t need = std::max<std::size_t>(
        2, static_cast<std::size_t>(std::ceil(frac * static_cast<double>(ms.size()))));
    for (std::size_t blk = 0; blk < kSdrBlocks; ++blk) {
        for (int i = 0; i < 64; ++i) {
            std::size_t hits = 0;
            for (const auto& m : ms)
                hits += static_cast<std::size_t>((m.mask()[blk] >> i) & 1ULL);
            if (hits >= need) out.add_position(blk, static_cast<std::uint8_t>(i));
        }
    }
    return out;
}

double auc(std::vector<double> pos, std::vector<double> neg) {
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

struct Result {
    double code = 0, aff = 0, freq = 0, rnd_ = 0;
    double core = 0, mem = 0, str = 0;
    std::size_t n = 0;
    std::vector<std::pair<double, std::string>> per_cat;
    std::vector<double> code_by_cat, aff_by_cat;   // paired, same order
};

Result evaluate(const Plexus& plex,
                const std::unordered_map<std::string, std::uint32_t>& ids,
                const std::vector<std::uint32_t>& usable,
                const std::vector<WnCategory>& cats,
                std::size_t top_k, std::size_t n_seeds, double quorum) {
    Result r;
    std::unordered_map<std::uint32_t, SdrUnion> cache;
    const auto code = [&](std::uint32_t id) -> const SdrUnion& {
        auto it = cache.find(id);
        if (it != cache.end()) return it->second;
        return cache.emplace(id, context_code(plex, id, top_k)).first->second;
    };

    for (const auto& c : cats) {
        if (c.members.size() < n_seeds + 4) continue;

        std::vector<SdrUnion> seeds;
        std::vector<std::string> seed_words;
        for (std::size_t i = 0; i < n_seeds; ++i) {
            seeds.push_back(code(ids.at(c.members[i])));
            seed_words.push_back(c.members[i]);
        }
        const SdrUnion core = quorum_of(seeds, quorum);
        if (core.active() == 0) continue;

        const std::unordered_set<std::string> member_set(c.members.begin(), c.members.end());
        std::vector<double> cp, cn, ap, an, fp, fn, rp, rn;

        for (std::size_t i = n_seeds; i < c.members.size(); ++i) {
            const std::uint32_t id = ids.at(c.members[i]);
            cp.push_back(similarity(core, code(id)));
            double a = 0.0;
            for (const auto& sw : seed_words) a += plex.affinity(c.members[i], sw);
            ap.push_back(a / static_cast<double>(seed_words.size()));
            fp.push_back(static_cast<double>(plex.occurrences(c.members[i])));
            rp.push_back(static_cast<double>(rnd() % 1000000) / 1e6);
        }

        const std::size_t want = cp.size();
        std::size_t guard = 0;
        while (cn.size() < want && guard < want * 400) {
            ++guard;
            const std::uint32_t id = usable[rnd() % usable.size()];
            const std::string w(plex.node_name(id));
            if (member_set.count(w)) continue;
            const double target = fp[cn.size()];
            const double got = static_cast<double>(plex.occurrences(w));
            if (got < target * 0.5 || got > target * 2.0) continue;
            cn.push_back(similarity(core, code(id)));
            double a = 0.0;
            for (const auto& sw : seed_words) a += plex.affinity(w, sw);
            an.push_back(a / static_cast<double>(seed_words.size()));
            fn.push_back(got);
            rn.push_back(static_cast<double>(rnd() % 1000000) / 1e6);
        }
        if (cp.empty() || cn.empty()) continue;

        const double ac = auc(cp, cn);
        r.code += ac;
        r.aff  += auc(ap, an);
        r.freq += auc(fp, fn);
        r.rnd_ += auc(rp, rn);
        r.core += static_cast<double>(core.active());
        double ms = 0.0, ss = 0.0;
        for (const double v : cp) ms += v;
        for (const double v : cn) ss += v;
        r.mem += ms / static_cast<double>(cp.size());
        r.str += ss / static_cast<double>(cn.size());
        ++r.n;
        r.per_cat.emplace_back(ac, c.name);
        r.code_by_cat.push_back(ac);
        r.aff_by_cat.push_back(auc(ap, an));
    }
    if (r.n) {
        const double n = static_cast<double>(r.n);
        r.code /= n; r.aff /= n; r.freq /= n; r.rnd_ /= n;
        r.core /= n; r.mem /= n; r.str /= n;
    }
    return r;
}

} // namespace

int main(int argc, char** argv) {
    const std::string plex_path = (argc > 1) ? argv[1] : "data/plexus_archive/main";
    const std::string cats_path = (argc > 2) ? argv[2] : "data/eval/wn_categories.tsv";
    const std::size_t min_occ   = (argc > 3) ? std::stoul(argv[3]) : 20;

    Plexus plex;
    plex.load(plex_path);
    std::printf("Category induction against WordNet 3.1\n");
    std::printf("  Khora graph: %zu words, %llu edges\n", plex.vocabulary_size(),
                static_cast<unsigned long long>(plex.edge_count()));
    if (plex.vocabulary_size() == 0) { std::printf("  no graph\n"); return 1; }

    std::unordered_map<std::string, std::uint32_t> ids;
    ids.reserve(plex.vocabulary_size() * 2);
    for (std::size_t i = 0; i < plex.vocabulary_size(); ++i)
        ids.emplace(std::string(plex.node_name(i)), static_cast<std::uint32_t>(i));

    std::vector<std::uint32_t> usable;
    for (std::size_t i = 0; i < plex.vocabulary_size(); ++i)
        if (plex.occurrences(plex.node_name(i)) >= min_occ && plex.degree(i) >= 8)
            usable.push_back(static_cast<std::uint32_t>(i));
    std::printf("  words with >=%zu occurrences and >=8 associates: %zu\n",
                min_occ, usable.size());

    std::ifstream in(cats_path);
    if (!in) { std::printf("  cannot open %s\n", cats_path.c_str()); return 1; }

    std::vector<WnCategory> cats;
    std::string line;
    while (std::getline(in, line)) {
        const auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        WnCategory c;
        c.name = line.substr(0, tab);
        std::istringstream ws(line.substr(tab + 1));
        std::string w;
        while (ws >> w) {
            const auto it = ids.find(w);
            if (it == ids.end()) continue;
            if (plex.occurrences(w) < min_occ || plex.degree(it->second) < 8) continue;
            c.members.push_back(w);
        }
        // At least 13 members, so even the largest seed set leaves enough held
        // out for an AUC to mean something. At 6 members it was 3 against 3,
        // which produced a great many exact 1.000 and 0.000 scores.
        if (c.members.size() >= 13) cats.push_back(std::move(c));
    }
    std::printf("  WordNet categories with >=13 members Khora knows well: %zu\n",
                cats.size());
    if (cats.empty()) { std::printf("  nothing to evaluate\n"); return 1; }

    std::printf("\n  === PARAMETER SWEEP (mean AUC) ===\n");
    std::printf("   assoc | seeds | quorum |  CODE  | affinity |  freq  | random | core\n");
    std::printf("  -------+-------+--------+--------+----------+--------+--------+------\n");

    Result best;
    std::size_t bk = 0, bs = 0;
    double bq = 0.0;
    for (const std::size_t k : {std::size_t{8}, std::size_t{16}, std::size_t{24},
                                std::size_t{48}, std::size_t{96}}) {
        for (const std::size_t ns : {std::size_t{3}, std::size_t{5}, std::size_t{8}}) {
            for (const double q : {1.0, 0.6}) {
                const Result r = evaluate(plex, ids, usable, cats, k, ns, q);
                if (r.n == 0) continue;
                std::printf("   %5zu | %5zu |  %.2f  | %.4f |  %.4f  | %.4f | %.4f | %5.0f\n",
                            k, ns, q, r.code, r.aff, r.freq, r.rnd_, r.core);
                // A setting that qualifies only a handful of categories is not
                // the best setting, it is a small sample. The 8-seed strict
                // intersection scored 0.6740 on FOURTEEN categories -- exactly
                // the cherry-pick this whole exercise exists to avoid.
                if (r.n >= 100 && r.code > best.code) { best = r; bk = k; bs = ns; bq = q; }
            }
        }
    }

    if (best.n == 0) { std::printf("\n  nothing evaluated\n"); return 1; }

    std::printf("\n  === BEST SETTING EVALUATED ON >=100 CATEGORIES:"
                " %zu associates, %zu seeds, quorum %.2f ===\n", bk, bs, bq);
    std::printf("  %zu categories | CODE %.4f | affinity %.4f | frequency %.4f | random %.4f\n",
                best.n, best.code, best.aff, best.freq, best.rnd_);
    std::printf("  member similarity %.4f vs stranger %.4f, mean core %.0f positions\n",
                best.mem, best.str, best.core);

    // PAIRED COMPARISON. Mean AUC hides whether one method wins consistently or
    // wins hugely on a few categories and loses on the rest. Same categories,
    // same splits, same negatives -- so the comparison can be made per category.
    {
        std::size_t wins = 0, losses = 0, ties = 0;
        std::vector<double> diffs;
        for (std::size_t i = 0; i < best.code_by_cat.size(); ++i) {
            const double d = best.code_by_cat[i] - best.aff_by_cat[i];
            diffs.push_back(d);
            if (d > 1e-9)       ++wins;
            else if (d < -1e-9) ++losses;
            else                ++ties;
        }
        std::sort(diffs.begin(), diffs.end());
        const double median = diffs.empty() ? 0.0 : diffs[diffs.size() / 2];
        // Normal approximation to the sign test: under the null the code wins
        // half the decided comparisons.
        const double n = static_cast<double>(wins + losses);
        const double z = n > 0 ? (static_cast<double>(wins) - n / 2.0) / std::sqrt(n / 4.0) : 0.0;
        std::printf("\n  paired against raw affinity, per category:\n");
        std::printf("    code wins %zu, loses %zu, ties %zu   median difference %+.4f\n",
                    wins, losses, ties, median);
        std::printf("    sign-test z = %.2f  (|z| > 1.96 is p < 0.05, > 2.58 is p < 0.01)\n", z);
    }

    std::sort(best.per_cat.rbegin(), best.per_cat.rend());
    std::printf("\n  best-recovered:\n");
    for (std::size_t i = 0; i < best.per_cat.size() && i < 15; ++i)
        std::printf("    %.3f  %s\n", best.per_cat[i].first, best.per_cat[i].second.c_str());
    std::printf("  worst-recovered:\n");
    for (std::size_t i = 0; i < 10 && i < best.per_cat.size(); ++i)
        std::printf("    %.3f  %s\n", best.per_cat[best.per_cat.size() - 1 - i].first,
                    best.per_cat[best.per_cat.size() - 1 - i].second.c_str());

    std::printf("\n  Negatives are FREQUENCY-MATCHED to the member they stand against,\n"
                "  which forces the frequency baseline to chance. WordNet is only an\n"
                "  answer key -- nothing from it enters any code or graph.\n");
    return 0;
}
