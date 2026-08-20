// CATEGORIES AS OVERLAP, NOT AS EDGES
//
// Three attempts to learn an is-a taxonomy all failed: Hearst patterns need a
// corpus a thousand times larger than Khora holds; changing genre moved almost
// nothing; distributional inclusion over the Plexus produced co-hyponyms and
// collocations instead of superordinates.
//
// They failed because they were solving a problem the tissue does not have.
//
// THERE IS NO TAXONOMY MODULE IN CORTEX. No is-a table, no edge from sparrow to
// bird. What there is: overlapping distributed population codes. "Bird" is not
// a node with children -- it is the subspace that sparrow, robin and jay all
// activate, the part of their representations they hold in common. Category
// membership is OVERLAP, computed on demand, never stored. That is the standard
// distributed-representation account of concepts, and it is why a brain can
// recognise a category it was never taught the name of.
//
// Khora's word codes are Sdr::from_hash(word) -- random, sharing nothing by
// construction. Two words about the same thing are as orthogonal as two words
// about nothing. That is the actual defect: the substrate throws away the very
// structure the category relation is supposed to be made of.
//
// THE FIX, and it needs no new corpus and no new layer. Build a word's code
// from the company it keeps: superpose the codes of its strongest associates,
// weighted by affinity, into one sparse vector. Words with similar
// neighbourhoods then share bits, automatically, because they are made of the
// same parts.
//
// THE FALSIFIABLE CLAIM. Take a handful of members of a category. Intersect
// their codes -- keep only what they all share. If that intersection is the
// category, then it must overlap with members that were NOT used to build it
// more than with words from anywhere else. No taxonomy is stored anywhere; the
// category is computed from the geometry.

#include "khora/lattice/sdr.hpp"
#include "khora/plexus/plexus.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

using khora::plexus::Plexus;
using namespace khora::lattice;

namespace {

// A word's code is the superposition of its associates' codes. The associates
// themselves keep random codes -- the structure enters one level up, from WHICH
// associates a word has, exactly as a cortical cell's selectivity comes from
// what projects to it rather than from anything intrinsic.
// A word's code is the SET of its associates' codes, superposed as a union.
//
// A binarised bundle was tried first and is the wrong operation, for the same
// reason it was wrong for simultaneous predictions: Trace::binarise is
// winner-take-all, answering "which single pattern best explains these votes".
// A word's context is not one pattern, it is a SET, and argmax over ~160
// competing associates keeps whichever one happened to win each block --
// discarding precisely the shared structure two related words hold in common.
// Measured with binarise: related pairs overlapped on 7.6 of 256 blocks against
// 5.6 for unrelated, and the three-way intersection of a category came to ONE
// block. The signal was real and almost entirely thrown away.
//
// The structure enters one level up, from WHICH associates a word has -- exactly
// as a cortical cell's selectivity comes from what projects to it rather than
// from anything intrinsic to the cell.
SdrUnion context_code(const Plexus& plex, const std::string& word,
                      const std::unordered_map<std::string, std::uint32_t>& ids,
                      std::size_t top_k = 24) {
    SdrUnion u;
    const auto it = ids.find(word);
    if (it == ids.end()) return u;

    // Strongest associates only. Density of a union is about k/64, so this is
    // also what keeps the code sparse enough to stay discriminative.
    std::vector<std::pair<double, std::string>> ranked;
    for (const auto& [nb, c] : plex.neighbours(it->second)) {
        const std::string name(plex.node_name(nb));
        const double a = plex.affinity(word, name);
        if (a > 0.0) ranked.emplace_back(a * std::log2(1.0 + c), name);
    }
    std::sort(ranked.rbegin(), ranked.rend());
    for (std::size_t i = 0; i < ranked.size() && i < top_k; ++i)
        u.add(Sdr::from_hash("ctx:" + ranked[i].second));
    return u;
}

// Overlap of two context sets, normalised so that a word with a bigger
// neighbourhood does not win by size alone.
double similarity(const SdrUnion& a, const SdrUnion& b) {
    std::size_t both = 0, either = 0;
    for (std::size_t blk = 0; blk < kSdrBlocks; ++blk) {
        const std::uint64_t x = a.mask()[blk], y = b.mask()[blk];
        both   += static_cast<std::size_t>(std::popcount(x & y));
        either += static_cast<std::size_t>(std::popcount(x | y));
    }
    return either ? static_cast<double>(both) / either : 0.0;
}

// What a set of context sets holds in common: the positions every member has.
// That intersection is the category -- what the members share once everything
// distinguishing them is removed.
SdrUnion intersect(const std::vector<SdrUnion>& members) {
    SdrUnion out;
    if (members.empty()) return out;
    for (std::size_t blk = 0; blk < kSdrBlocks; ++blk) {
        std::uint64_t m = members[0].mask()[blk];
        for (const auto& x : members) m &= x.mask()[blk];
        for (int i = 0; i < 64 && m; ++i)
            if ((m >> i) & 1ULL) out.add_position(blk, static_cast<std::uint8_t>(i));
    }
    return out;
}

struct Category {
    const char* name;
    std::vector<std::string> seed;      // used to BUILD the category
    std::vector<std::string> held_out;  // members never shown to it
};

} // namespace

int main(int argc, char** argv) {
    const std::string prefix = (argc > 1) ? argv[1] : "data/plexus_archive/main";
    Plexus plex;
    plex.load(prefix);
    std::printf("Categories as overlap, over Khora's real graph\n");
    std::printf("  %zu words, %llu edges\n", plex.vocabulary_size(),
                static_cast<unsigned long long>(plex.edge_count()));
    if (plex.vocabulary_size() == 0) { std::printf("  no graph\n"); return 0; }

    std::unordered_map<std::string, std::uint32_t> ids;
    ids.reserve(plex.vocabulary_size() * 2);
    for (std::size_t i = 0; i < plex.vocabulary_size(); ++i)
        ids.emplace(std::string(plex.node_name(i)), static_cast<std::uint32_t>(i));

    // --- does the code carry meaning at all? -------------------------------
    //
    // Before anything about categories: do related words now share bits, where
    // hashed codes shared only chance (4 of 256 blocks)?
    std::printf("\n  === DO RELATED WORDS SHARE STRUCTURE? ===\n");
    const std::vector<std::pair<std::string, std::string>> related = {
        {"king", "queen"}, {"gold", "silver"}, {"sword", "spear"},
        {"horse", "dog"},  {"sun", "moon"},    {"father", "mother"},
        {"ship", "boat"},  {"war", "battle"}
    };
    const std::vector<std::pair<std::string, std::string>> unrelated = {
        {"king", "molecule"}, {"gold", "sorrow"}, {"sword", "theory"},
        {"horse", "justice"}, {"sun", "committee"}, {"father", "iron"},
        {"ship", "virtue"},   {"war", "flower"}
    };
    const auto mean_sim =
        [&](const std::vector<std::pair<std::string, std::string>>& ps, bool hashed) {
            double sum = 0.0; int n = 0;
            for (const auto& [a, b] : ps) {
                if (!ids.count(a) || !ids.count(b)) continue;
                if (hashed) {
                    SdrUnion ua, ub;
                    ua.add(Sdr::from_hash(a));
                    ub.add(Sdr::from_hash(b));
                    sum += similarity(ua, ub);
                } else {
                    sum += similarity(context_code(plex, a, ids),
                                      context_code(plex, b, ids));
                }
                ++n;
            }
            return n ? sum / n : 0.0;
        };
    std::printf("                        hashed code   context set\n");
    std::printf("    related pairs           %.4f        %.4f\n",
                mean_sim(related, true), mean_sim(related, false));
    std::printf("    unrelated pairs         %.4f        %.4f\n",
                mean_sim(unrelated, true), mean_sim(unrelated, false));

    // --- the claim ----------------------------------------------------------
    std::printf("\n  === IS THE INTERSECTION OF MEMBERS A CATEGORY? ===\n");
    const std::vector<Category> cats = {
        {"metals",     {"gold", "silver", "iron"},        {"brass", "copper", "steel"}},
        {"weapons",    {"sword", "spear", "shield"},      {"bow", "arrow", "helmet"}},
        {"kin",        {"father", "mother", "brother"},   {"sister", "daughter", "son"}},
        {"vessels",    {"ship", "boat", "sail"},          {"fleet", "vessel", "deck"}},
        {"heavens",    {"sun", "moon", "star"},           {"sky", "cloud", "heaven"}},
        {"beasts",     {"horse", "dog", "sheep"},         {"cattle", "wolf", "goat"}}
    };
    const std::vector<std::string> strangers = {
        "justice", "theory", "committee", "sorrow", "molecule", "grammar",
        "opinion", "custom", "argument", "virtue"
    };

    std::printf("    category  | shared | held-out members | strangers | separation\n");
    std::printf("    ----------+--------+------------------+-----------+-----------\n");
    int wins = 0, tested = 0;
    for (const auto& c : cats) {
        std::vector<SdrUnion> seeds;
        for (const auto& w : c.seed)
            if (ids.count(w)) seeds.push_back(context_code(plex, w, ids));
        if (seeds.size() < 2) continue;
        const SdrUnion core = intersect(seeds);
        const std::size_t shared = core.active();
        if (shared == 0) { std::printf("    %-9s |   0    | (nothing shared)\n", c.name); continue; }

        const auto score = [&](const std::string& w) {
            if (!ids.count(w)) return -1.0;
            return similarity(core, context_code(plex, w, ids));
        };
        double held = 0.0; int hn = 0;
        for (const auto& w : c.held_out) { const double v = score(w); if (v >= 0) { held += v; ++hn; } }
        double str = 0.0; int sn = 0;
        for (const auto& w : strangers)  { const double v = score(w); if (v >= 0) { str += v; ++sn; } }
        if (!hn || !sn) continue;
        held /= hn; str /= sn;
        ++tested;
        if (held > str) ++wins;
        std::printf("    %-9s |  %3zu   |     %.4f       |  %.4f   |  %+.4f\n",
                    c.name, shared, held, str, held - str);
    }
    std::printf("\n    %d of %d categories place their unseen members above strangers.\n",
                wins, tested);
    // === THE CONTROL ===
    //
    // Getting the direction right six times out of six is not the same as being
    // useful, and it is certainly not the same as being BETTER THAN THE OBVIOUS
    // THING. The obvious thing here is plain association: score a candidate by
    // its mean Plexus affinity to the seed words -- no codes, no unions, no
    // intersection. If that ranks members above strangers just as well, then
    // every layer of representation above it is decoration.
    //
    // Ranking is also the usable form of the question, so both are scored by
    // AUC over a mixed list of held-out members and strangers.
    std::printf("\n  === AGAINST PLAIN ASSOCIATION ===\n");
    std::printf("    category  | category-code AUC | raw-affinity AUC\n");
    std::printf("    ----------+-------------------+------------------\n");

    const auto auc = [](std::vector<double> pos, std::vector<double> neg) {
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
    };

    double code_sum = 0.0, aff_sum = 0.0;
    int scored = 0;
    for (const auto& c : cats) {
        std::vector<SdrUnion> seeds;
        for (const auto& w : c.seed)
            if (ids.count(w)) seeds.push_back(context_code(plex, w, ids));
        if (seeds.size() < 2) continue;
        const SdrUnion core = intersect(seeds);
        if (core.active() == 0) continue;

        std::vector<double> code_pos, code_neg, aff_pos, aff_neg;
        const auto both = [&](const std::string& w, bool member) {
            if (!ids.count(w)) return;
            const double cs = similarity(core, context_code(plex, w, ids));
            double a = 0.0;
            for (const auto& sw : c.seed) a += plex.affinity(w, sw);
            a /= static_cast<double>(c.seed.size());
            if (member) { code_pos.push_back(cs); aff_pos.push_back(a); }
            else        { code_neg.push_back(cs); aff_neg.push_back(a); }
        };
        for (const auto& w : c.held_out) both(w, true);
        for (const auto& w : strangers)  both(w, false);
        if (code_pos.empty() || code_neg.empty()) continue;

        const double ac = auc(code_pos, code_neg);
        const double aa = auc(aff_pos, aff_neg);
        code_sum += ac; aff_sum += aa; ++scored;
        std::printf("    %-9s |       %.3f       |      %.3f\n", c.name, ac, aa);
    }
    if (scored) {
        std::printf("    ----------+-------------------+------------------\n");
        std::printf("    mean      |       %.3f       |      %.3f\n",
                    code_sum / scored, aff_sum / scored);
    }

    std::printf("\n    Held-out members were never used to build the category. AUC 0.5\n"
                "    is a coin flip. If the two columns are equal, the representation\n"
                "    is adding nothing the association graph did not already have.\n");
    return 0;
}
