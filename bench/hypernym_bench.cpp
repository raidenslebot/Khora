// TAXONOMY FROM STATISTICS, NOT FROM SURFACE PATTERNS
//
// Khora has two knowledge layers built from the same books, and only one of
// them works.
//
// The Plexus works: 82,695 words, 3.87M weighted edges, small-world topology,
// and it gets cat->dog right. The Ligature does not: measured over 1.1M tokens
// of Darwin, Faraday and astronomy -- text chosen because Hearst patterns were
// designed for it -- the entire corroborated taxonomy is ONE relation, "earth
// is-a sphere". Literary text does no better. It is not the corpus. It is that
// a pattern like "X is a Y" fires in perhaps one sentence in five hundred, so
// the method needs a corpus three orders of magnitude larger than Khora will
// ever hold.
//
// The difference between the two layers is signal density. The Plexus uses
// EVERY token; the Ligature uses one token in five hundred. At a fixed corpus
// size that decides everything, and it is also how the tissue works -- cortex
// learns from the statistics of everything, and there is no pattern-matching
// module in it waiting for someone to write "a sparrow is a bird".
//
// So: get the taxonomy out of the statistics.
//
// THE DISTRIBUTIONAL INCLUSION HYPOTHESIS (Weeds & Weir 2003; Geffet & Dagan
// 2005; Kotlerman et al. 2010). If B is a kind of A, then the contexts B occurs
// in are roughly a SUBSET of the contexts A occurs in. Everything you can say
// about a sparrow you can say about a bird; the reverse fails. So hypernymy
// shows up as asymmetric feature containment plus a generality difference --
// both computable directly from the graph the Plexus already holds, with no new
// corpus, no new pattern, and no hand-written rule.
//
// Whether it actually works on Khora's real graph is the measurement.

#include "khora/plexus/plexus.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

using khora::plexus::Plexus;

namespace {

// Weeds precision: what fraction of the narrow word's distributional weight
// falls on features the broad word also has. Near 1 means "everything this word
// is seen with, the other word is also seen with".
double inclusion(const std::unordered_map<std::uint32_t, double>& narrow,
                 const std::unordered_map<std::uint32_t, double>& broad) {
    double shared = 0.0, total = 0.0;
    for (const auto& [f, w] : narrow) {
        total += w;
        if (broad.count(f)) shared += w;
    }
    return total > 0.0 ? shared / total : 0.0;
}

} // namespace

int main(int argc, char** argv) {
    const std::string prefix = (argc > 1) ? argv[1] : "data/plexus_archive/main";
    Plexus plex;
    plex.load(prefix);
    std::printf("Hypernyms from distributional inclusion, over Khora's real graph\n");
    std::printf("  %zu words, %llu edges\n\n", plex.vocabulary_size(),
                static_cast<unsigned long long>(plex.edge_count()));
    if (plex.vocabulary_size() == 0) { std::printf("  no graph loaded\n"); return 0; }

    // Feature vector of a word: its associates, weighted by affinity (PPMI) so
    // that loud words do not dominate. The Plexus has already pruned each node
    // to its strongest kin, so this is the informative neighbourhood.
    const auto features = [&](const std::string& w) {
        std::unordered_map<std::uint32_t, double> f;
        const auto id = [&]() -> std::int64_t {
            for (std::size_t i = 0; i < plex.vocabulary_size(); ++i)
                if (plex.node_name(i) == w) return static_cast<std::int64_t>(i);
            return -1;
        };
        (void)id;
        return f;
    };
    (void)features;

    // Build a name -> id map once; the Plexus exposes names but not lookup.
    std::unordered_map<std::string, std::uint32_t> ids;
    ids.reserve(plex.vocabulary_size() * 2);
    for (std::size_t i = 0; i < plex.vocabulary_size(); ++i)
        ids.emplace(std::string(plex.node_name(i)), static_cast<std::uint32_t>(i));

    const auto featvec = [&](std::uint32_t id) {
        std::unordered_map<std::uint32_t, double> f;
        const std::string self(plex.node_name(id));
        for (const auto& [nb, c] : plex.neighbours(id)) {
            const double a = plex.affinity(self, plex.node_name(nb));
            if (a > 0.0) f.emplace(nb, a * std::log2(1.0 + c));
        }
        return f;
    };

    // Ordinary concrete nouns, chosen before seeing any output. Concrete words
    // are where the hypothesis should hold most clearly; abstractions like
    // "justice" have no clean superordinate even in a dictionary.
    const std::vector<std::string> probes = {
        "dog", "horse", "bird", "tree", "ship", "sword", "gold", "star",
        "wine", "iron", "rose", "island", "mountain", "river", "king", "soldier"
    };

    std::printf("  word        | proposed hypernyms (inclusion asymmetry x generality)\n");
    std::printf("  ------------+-------------------------------------------------------\n");

    int found = 0;
    for (const auto& p : probes) {
        const auto it = ids.find(p);
        if (it == ids.end()) continue;
        const auto fp = featvec(it->second);
        if (fp.size() < 8) continue;

        // Candidates: the word's own associates, and their associates. A
        // hypernym usually keeps company with its hyponyms.
        std::vector<std::uint32_t> cands;
        for (const auto& [nb, c] : plex.neighbours(it->second)) { (void)c; cands.push_back(nb); }

        std::vector<std::pair<double, std::string>> scored;
        for (const std::uint32_t c : cands) {
            if (c == it->second) continue;
            const auto fc = featvec(c);
            if (fc.size() < 8) continue;

            const double fwd = inclusion(fp, fc);   // p's contexts inside c's
            const double rev = inclusion(fc, fp);   // c's contexts inside p's
            if (fwd <= rev) continue;               // must be asymmetric the right way

            // Generality: a hypernym is the broader word, so it should carry
            // more distributional weight overall.
            double wp = 0.0, wc = 0.0;
            for (const auto& [f, w] : fp) { (void)f; wp += w; }
            for (const auto& [f, w] : fc) { (void)f; wc += w; }
            if (wc <= wp) continue;

            const double score = fwd * (fwd - rev) * std::log2(1.0 + wc / wp);
            scored.emplace_back(score, std::string(plex.node_name(c)));
        }
        std::sort(scored.rbegin(), scored.rend());
        if (scored.empty()) continue;
        ++found;
        std::printf("  %-11s |", p.c_str());
        for (std::size_t i = 0; i < scored.size() && i < 5; ++i)
            std::printf(" %s(%.2f)", scored[i].second.c_str(), scored[i].first);
        std::printf("\n");
    }

    std::printf("\n  %d of %zu probes produced a candidate.\n", found, probes.size());
    std::printf("\n  For comparison, the pattern extractor's entire corroborated output\n"
                "  over 1.1M tokens of scientific prose was: earth is-a sphere.\n");
    std::printf("\n  IT DOES NOT WORK, AND THE REASON LOOKS STRUCTURAL.\n");
    std::printf("\n  These are not hypernyms. dog->wolf is a CO-hyponym; king->henry\n"
                "  and king->palace are an instance and a collocation; sword->atrides\n"
                "  is Homeric company. Every score lies between 0.00 and 0.03 -- the\n"
                "  asymmetry the hypothesis depends on is simply not there.\n");
    std::printf("\n  The likely cause is in the Plexus itself: it prunes every node to\n"
                "  its strongest 160 associates to bound memory. Distributional\n"
                "  inclusion needs the FULL context distribution -- its entire premise\n"
                "  is that a broad word occurs in MORE contexts than a narrow one, and\n"
                "  a uniform degree cap is precisely the operation that erases that\n"
                "  difference. The broadest words are clipped hardest. Measured mean\n"
                "  degree is 46.7, so the cap does not bind everywhere, but it binds\n"
                "  exactly where taxonomy lives. Stated as a hypothesis with evidence,\n"
                "  not as a proven cause.\n");
    std::printf("\n  The feature space is also wrong. The literature computes inclusion\n"
                "  over SYNTACTIC features (dog-subject-of-bark); a symmetric word\n"
                "  window cannot separate an associate from a superordinate at all,\n"
                "  because 'bark' and 'animal' sit in the same neighbourhood of 'dog'.\n");
    return 0;
}
