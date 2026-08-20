// WHY DOES KHORA'S GRAPH HAVE HUBS BUT NO CORE?
//
// Measured against six real connectomes through identical code, Khora's Plexus
// has a rich-club coefficient of 0.013 where C. elegans, Drosophila, mouse, cat
// and macaque range from 0.12 to 0.98. Its hubs are ten to seventy times less
// interconnected than any nervous system yet measured. Real brains wire their
// hubs densely to one another; this graph does not.
//
// That was left as an open question. This is the test, and it is a test of a
// MECHANISM rather than a search for a knob: two specific things in Khora's own
// learning rule would each produce exactly this signature, and they are
// separable.
//
//   1. PPMI PENALISES HUB-HUB PAIRS BY CONSTRUCTION. PMI is
//      log( P(a,b) / (P(a) P(b)) ). Two hubs both have large marginals, so the
//      denominator is large, and their score is small EVEN WHEN THEY CO-OCCUR
//      CONSTANTLY. The measure is designed to divide out exactly the loudness
//      that makes a hub a hub.
//
//   2. PER-NODE PRUNING THEN EVICTS THEM, AT BOTH ENDS. prune_ keeps a node's
//      top max_degree edges by ppmi * log2(1+cooc). A hub-hub edge is competing
//      against a hub's strongest kin -- and it has to survive that competition
//      at BOTH of its endpoints to remain in the graph at all.
//
// So the experiment builds one graph with pruning effectively off, measures its
// core, then prunes COPIES of it by two different rules and measures again:
//
//      by ppmi * log2(1+cooc)   the rule Khora actually uses
//      by raw co-occurrence     the same cap, ranking on evidence alone
//
// If the unpruned graph has a core and the PPMI rule removes it while the raw
// rule keeps it, the cause is the ranking, not the cap. If both remove it, the
// cause is the cap. If the unpruned graph has no core either, then neither is
// to blame and a co-occurrence graph simply does not grow one -- which is also
// an answer, and the one that would close the question.

#include "khora/lexicon/lexicon.hpp"
#include "khora/plexus/plexus.hpp"
#include "khora/reservoir/reservoir.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using khora::plexus::Plexus;

namespace {

// One node's kept edges, as (neighbour, co-occurrence count).
using Edges = std::vector<std::pair<std::uint32_t, std::uint32_t>>;
using Graph = std::vector<Edges>;

struct Stats {
    double mean_degree = 0.0, max_degree = 0.0;
    double clustering = 0.0;
    double rich_club = 0.0;       // density among the top 12% by degree
    double rich_ratio = 0.0;      // against a degree-preserving null
    std::size_t edges = 0;
};

std::vector<std::vector<std::uint32_t>> to_adj(const Graph& g) {
    std::vector<std::vector<std::uint32_t>> a(g.size());
    for (std::size_t i = 0; i < g.size(); ++i) {
        a[i].reserve(g[i].size());
        for (const auto& e : g[i]) if (e.first != i) a[i].push_back(e.first);
        std::sort(a[i].begin(), a[i].end());
        a[i].erase(std::unique(a[i].begin(), a[i].end()), a[i].end());
    }
    return a;
}

double rich_club_of(const std::vector<std::vector<std::uint32_t>>& adj) {
    std::vector<std::pair<std::size_t, std::uint32_t>> by_deg;
    by_deg.reserve(adj.size());
    for (std::size_t i = 0; i < adj.size(); ++i)
        by_deg.emplace_back(adj[i].size(), static_cast<std::uint32_t>(i));
    std::sort(by_deg.rbegin(), by_deg.rend());
    const std::size_t top = std::max<std::size_t>(2, by_deg.size() * 12 / 100);
    std::unordered_set<std::uint32_t> club;
    for (std::size_t i = 0; i < top; ++i) club.insert(by_deg[i].second);
    std::size_t within = 0;
    for (const std::uint32_t v : club)
        for (const std::uint32_t w : adj[v]) if (club.count(w)) ++within;
    const double possible = static_cast<double>(top) * (static_cast<double>(top) - 1.0);
    return possible > 0 ? static_cast<double>(within) / possible : 0.0;
}

double clustering_of(const std::vector<std::vector<std::uint32_t>>& adj,
                     std::mt19937_64& rng, int samples) {
    std::vector<std::unordered_set<std::uint32_t>> set(adj.size());
    for (std::size_t i = 0; i < adj.size(); ++i) set[i].insert(adj[i].begin(), adj[i].end());
    std::uniform_int_distribution<std::size_t> pick(0, adj.size() - 1);
    double total = 0.0; int counted = 0;
    for (int s = 0; s < samples; ++s) {
        const std::size_t v = pick(rng);
        if (adj[v].size() < 2) continue;
        std::uniform_int_distribution<std::size_t> pn(0, adj[v].size() - 1);
        int links = 0, seen = 0;
        for (int t = 0; t < 200; ++t) {
            const std::size_t i = pn(rng), j = pn(rng);
            if (i == j) continue;
            ++seen;
            if (set[adj[v][i]].count(adj[v][j])) ++links;
        }
        if (seen) { total += static_cast<double>(links) / seen; ++counted; }
    }
    return counted ? total / counted : 0.0;
}

std::vector<std::vector<std::uint32_t>>
rewire(const std::vector<std::vector<std::uint32_t>>& adj, std::mt19937_64& rng) {
    std::vector<std::uint32_t> stubs;
    for (std::size_t i = 0; i < adj.size(); ++i)
        for (std::size_t k = 0; k < adj[i].size(); ++k)
            stubs.push_back(static_cast<std::uint32_t>(i));
    std::shuffle(stubs.begin(), stubs.end(), rng);
    std::vector<std::vector<std::uint32_t>> out(adj.size());
    for (std::size_t i = 0; i + 1 < stubs.size(); i += 2) {
        const std::uint32_t a = stubs[i], b = stubs[i + 1];
        if (a == b) continue;
        out[a].push_back(b);
        out[b].push_back(a);
    }
    for (auto& v : out) { std::sort(v.begin(), v.end());
                          v.erase(std::unique(v.begin(), v.end()), v.end()); }
    return out;
}

Stats measure(const Graph& g) {
    const auto adj = to_adj(g);
    Stats s;
    double sum = 0.0;
    for (const auto& v : adj) {
        sum += static_cast<double>(v.size());
        s.edges += v.size();
        s.max_degree = std::max(s.max_degree, static_cast<double>(v.size()));
    }
    s.edges /= 2;
    s.mean_degree = adj.empty() ? 0.0 : sum / adj.size();
    std::mt19937_64 rng(4242);
    s.clustering = clustering_of(adj, rng, 3000);
    s.rich_club  = rich_club_of(adj);
    std::mt19937_64 r2(4242);
    const double null_rc = rich_club_of(rewire(adj, r2));
    s.rich_ratio = null_rc > 0 ? s.rich_club / null_rc : 0.0;
    return s;
}

// Prune a copy of the graph to `cap` edges per node, ranked by `score`.
// This mirrors Plexus::prune_ exactly: per node, independently, keep the top
// `cap`. An edge therefore has to survive at BOTH of its endpoints to remain,
// which is the part that matters for hub-to-hub links.
template <class Score>
Graph prune_by(const Graph& g, std::size_t cap, Score score) {
    Graph out(g.size());
    for (std::size_t i = 0; i < g.size(); ++i) {
        if (g[i].size() <= cap) { out[i] = g[i]; continue; }
        std::vector<std::pair<double, std::pair<std::uint32_t, std::uint32_t>>> ranked;
        ranked.reserve(g[i].size());
        for (const auto& e : g[i])
            ranked.emplace_back(score(static_cast<std::uint32_t>(i), e.first, e.second), e);
        std::nth_element(ranked.begin(), ranked.begin() + cap, ranked.end(),
                         [](const auto& a, const auto& b) { return a.first > b.first; });
        ranked.resize(cap);
        for (const auto& r : ranked) out[i].push_back(r.second);
    }
    // An edge survives only if BOTH endpoints kept it -- the same effect
    // Plexus's independent per-node pruning has on the undirected view.
    std::vector<std::unordered_set<std::uint32_t>> kept(out.size());
    for (std::size_t i = 0; i < out.size(); ++i)
        for (const auto& e : out[i]) kept[i].insert(e.first);
    Graph sym(g.size());
    for (std::size_t i = 0; i < out.size(); ++i)
        for (const auto& e : out[i])
            if (e.first < kept.size() && kept[e.first].count(static_cast<std::uint32_t>(i)))
                sym[i].push_back(e);
    return sym;
}

std::vector<std::string> tokenize(const std::string& text, std::size_t want) {
    std::vector<std::string> out;
    std::string cur;
    for (const char ch : text) {
        if (std::isalpha(static_cast<unsigned char>(ch))) {
            cur += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        } else if (!cur.empty()) {
            if (cur.size() >= 2) out.push_back(cur);
            cur.clear();
            if (out.size() >= want) return out;
        }
    }
    return out;
}

void row(const char* label, const Stats& s) {
    std::printf("  %-30s %7.1f %8zu %8.4f %8.4f %8.2f\n",
                label, s.mean_degree, s.edges, s.clustering, s.rich_club, s.rich_ratio);
}

} // namespace

int main(int argc, char** argv) {
    const std::string dir   = (argc > 1) ? argv[1] : "data/reservoir";
    const std::size_t books = (argc > 2) ? std::stoul(argv[2]) : 8;
    const std::size_t words = (argc > 3) ? std::stoul(argv[3]) : 40000;
    const std::size_t cap   = (argc > 4) ? std::stoul(argv[4]) : 160;

    std::printf("Does Khora's own pruning rule destroy its core?\n\n");

    khora::reservoir::Reservoir res(dir);
    res.load_catalog();
    const auto cat = res.catalog();
    if (cat.empty()) { std::printf("  no tomes at %s\n", dir.c_str()); return 1; }

    // Build ONE graph with pruning effectively off, so the comparison is
    // against what the corpus actually supports rather than against another
    // pruned graph.
    Plexus plex;
    plex.set_max_degree(100000);
    std::size_t used = 0, total_words = 0;
    for (const auto& t : cat) {
        if (used >= books) break;
        auto text = res.read(t.title);
        if (!text || text->size() < 20000) continue;
        auto ws = tokenize(*text, words);
        if (ws.size() < 1000) continue;
        plex.observe(ws, 3);
        total_words += ws.size();
        ++used;
    }
    std::printf("  corpus: %zu books, %zu tokens -> %zu words, %llu edges\n\n",
                used, total_words, plex.vocabulary_size(),
                static_cast<unsigned long long>(plex.edge_count()));
    if (plex.vocabulary_size() < 100) { std::printf("  too small\n"); return 1; }

    // Extract the unpruned graph once.
    Graph raw(plex.vocabulary_size());
    for (std::size_t i = 0; i < plex.vocabulary_size(); ++i)
        for (const auto& [nb, c] : plex.neighbours(i))
            if (nb != i) raw[i].emplace_back(nb, c);

    // Khora's actual ranking, and the evidence-only alternative.
    const auto ppmi_score = [&plex](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        return plex.affinity(plex.node_name(a), plex.node_name(b)) *
               std::log2(1.0 + static_cast<double>(c));
    };
    const auto cooc_score = [](std::uint32_t, std::uint32_t, std::uint32_t c) {
        return static_cast<double>(c);
    };

    std::printf("  graph                            mean-deg    edges  cluster"
                "  rich-club  vs-null\n");
    std::printf("  ------------------------------+---------+--------+--------"
                "+---------+--------\n");
    row("unpruned (what the corpus has)", measure(raw));
    row("pruned by ppmi*log(1+c)  [Khora]", measure(prune_by(raw, cap, ppmi_score)));
    row("pruned by raw co-occurrence", measure(prune_by(raw, cap, cooc_score)));

    std::printf("\n  cap = %zu edges per node. An edge survives only if BOTH endpoints\n"
                "  keep it, which is what Khora's independent per-node pruning does to\n"
                "  the undirected graph.\n", cap);
    // How much of the core does the cap cost, and does raising it recover one?
    std::printf("\n  RICH CLUB AGAINST THE PRUNING CAP (ppmi ranking, Khora's rule)\n");
    std::printf("    cap    mean-deg   rich-club   vs-null\n");
    std::printf("   ------+----------+-----------+---------\n");
    for (const std::size_t c : {std::size_t{40}, std::size_t{80}, std::size_t{160},
                                std::size_t{320}, std::size_t{640}, std::size_t{1280}}) {
        const Stats st = measure(prune_by(raw, c, ppmi_score));
        std::printf("   %5zu   %8.1f    %8.4f  %8.2f\n",
                    c, st.mean_degree, st.rich_club, st.rich_ratio);
    }
    {
        const Stats st = measure(raw);
        std::printf("    none   %8.1f    %8.4f  %8.2f\n",
                    st.mean_degree, st.rich_club, st.rich_ratio);
    }

    // The comparison that matters -- and the one I got wrong the first time.
    std::printf("\n  AGAINST REAL CONNECTOMES: ABSOLUTE, AND AGAINST THEIR OWN NULL\n");
    std::printf("    network                   rich-club   vs-null\n");
    std::printf("   -------------------------+-----------+---------\n");
    std::printf("    C. elegans neural            0.3092      0.98\n");
    std::printf("    Drosophila medulla           0.1249      1.18\n");
    std::printf("    mouse visual cortex          0.1344      0.72\n");
    std::printf("    cat brain                    0.9048      1.19\n");
    std::printf("    macaque brain                0.6059      1.23\n");
    std::printf("    macaque cerebral cortex      0.9778      1.16\n");
    std::printf("    KHORA (full 82k graph)       0.0133      1.39\n");
    std::printf("\n  READ THE SECOND COLUMN. Every real connectome sits between 0.72 and\n");
    std::printf("  1.23 times its own degree-preserving null, which means their high\n");
    std::printf("  ABSOLUTE rich club is very largely explained by their degree sequence\n");
    std::printf("  and their density rather than by hubs being specially wired to one\n");
    std::printf("  another. Khora is at 1.39, above all six.\n\n");
    std::printf("  So \"Khora has hubs but no core\" was WRONG, and wrong in exactly the\n");
    std::printf("  way the gamma reading was wrong one measurement earlier: comparing an\n");
    std::printf("  absolute figure across graphs of wildly different size and density.\n");
    std::printf("  Relative to what its own degree sequence predicts, Khora's hubs are\n");
    std::printf("  MORE interconnected than any of these nervous systems, not less.\n");
    return 0;
}
