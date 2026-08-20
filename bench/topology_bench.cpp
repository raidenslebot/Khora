// IS KHORA'S LEARNED GRAPH SHAPED LIKE A NERVOUS SYSTEM?
//
// The three sources the operator pointed at were checked directly, and the
// honest verdict is worth writing down before any of this:
//
//   humanconnectome.org  -- a portal. The data lives behind ConnectomeDB with
//                           registration and a data use agreement, and it is
//                           raw and preprocessed IMAGING, not a connectivity
//                           matrix. Useful for neuroimaging, not for lifting
//                           architecture out of.
//   openneuro.org        -- raw BIDS imaging datasets. Turning any of it into
//                           connectivity means running a full preprocessing
//                           pipeline, hours of compute per subject, to arrive
//                           at a matrix somebody has already published.
//   FreeSurfer           -- confirmed from its own documentation: it ships
//                           three cortical parcellations (Desikan-Killiany,
//                           Destrieux, DKT40) and they carry ANATOMICAL LABELS
//                           ONLY. No connectivity of any kind. It is a surface
//                           reconstruction and labelling tool.
//
// So none of the three hands over a connectome. What the literature DOES hand
// over is better anyway: measured topology statistics, already computed by
// people with the imaging pipelines and the subjects. Those are the yardstick
// here.
//
// AND NOTE WHAT THIS IS NOT DOING. It is not copying connection probabilities
// into Khora. A connectome gives topology and nothing else -- Shiu et al. 2024
// had the COMPLETE fly wiring diagram and still had to guess every synaptic
// sign and weight. Structure constrains a hypothesis space; the objective and
// the learning rule do the work. Copying a matrix in would be decoration.
//
// Using it as a MEASURING STICK is different, and legitimate. Khora's Plexus is
// a graph learned from corpus statistics by a rule that knows nothing about
// brains. Whether it lands anywhere near the organisation biology converged on
// is a real question with a real answer, and either answer is informative.

#include "khora/plexus/plexus.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

using khora::plexus::Plexus;

namespace {



struct Topology {
    std::size_t nodes = 0, edges = 0;
    double density = 0.0;
    double mean_degree = 0.0, max_degree = 0.0;
    double clustering = 0.0;
    double path_length = 0.0;
    double degree_cv = 0.0;          // sd/mean: heterogeneity
    // Rich-club coefficient at the top 12% of nodes by degree: the DENSITY OF
    // CONNECTIONS AMONG THE HUBS THEMSELVES. This is the metric that actually
    // discriminates, because a degree-preserving rewiring keeps every node's
    // degree and can still scatter the hubs apart -- whereas "what share of
    // edges touch a hub" is fixed by the degree sequence and is therefore
    // identical in the null by construction. Measured that way first, which was
    // uninformative: 0.495 against 0.494.
    double rich_club = 0.0;
};

// Local clustering, sampled: of a node's neighbours, what fraction of the pairs
// among them are themselves connected. Sampling because the exact computation
// over 80k nodes at degree 160 is not the point of this exercise.
double sample_clustering(const std::vector<std::vector<std::uint32_t>>& adj,
                         const std::vector<std::unordered_set<std::uint32_t>>& adjset,
                         std::mt19937_64& rng, int samples) {
    double total = 0.0;
    int counted = 0;
    std::uniform_int_distribution<std::size_t> pick(0, adj.size() - 1);
    for (int s = 0; s < samples; ++s) {
        const std::size_t v = pick(rng);
        const auto& nb = adj[v];
        if (nb.size() < 2) continue;
        // Sample pairs rather than enumerating all of them.
        const int tries = 200;
        int links = 0, seen = 0;
        std::uniform_int_distribution<std::size_t> pn(0, nb.size() - 1);
        for (int t = 0; t < tries; ++t) {
            const std::size_t i = pn(rng), j = pn(rng);
            if (i == j) continue;
            ++seen;
            if (adjset[nb[i]].count(nb[j])) ++links;
        }
        if (seen) { total += static_cast<double>(links) / seen; ++counted; }
    }
    return counted ? total / counted : 0.0;
}

// Characteristic path length by sampled BFS.
double sample_path_length(const std::vector<std::vector<std::uint32_t>>& adj,
                          std::mt19937_64& rng, int sources) {
    std::uniform_int_distribution<std::size_t> pick(0, adj.size() - 1);
    double sum = 0.0;
    std::size_t pairs = 0;
    std::vector<int> dist(adj.size());
    for (int s = 0; s < sources; ++s) {
        std::fill(dist.begin(), dist.end(), -1);
        const std::size_t src = pick(rng);
        std::vector<std::uint32_t> frontier{static_cast<std::uint32_t>(src)};
        dist[src] = 0;
        int d = 0;
        // Cap the horizon: anything past 8 hops is not what "characteristic
        // path length" is measuring in a network this size.
        while (!frontier.empty() && d < 8) {
            ++d;
            std::vector<std::uint32_t> next;
            for (const std::uint32_t v : frontier) {
                for (const std::uint32_t w : adj[v]) {
                    if (dist[w] >= 0) continue;
                    dist[w] = d;
                    next.push_back(w);
                }
            }
            frontier.swap(next);
        }
        for (const int x : dist) if (x > 0) { sum += x; ++pairs; }
    }
    return pairs ? sum / static_cast<double>(pairs) : 0.0;
}

Topology measure(const std::vector<std::vector<std::uint32_t>>& adj, int samples) {
    Topology t;
    t.nodes = adj.size();
    std::vector<std::unordered_set<std::uint32_t>> adjset(adj.size());
    double sum = 0.0, sumsq = 0.0;
    std::vector<std::size_t> degs;
    degs.reserve(adj.size());
    for (std::size_t i = 0; i < adj.size(); ++i) {
        adjset[i].insert(adj[i].begin(), adj[i].end());
        const double d = static_cast<double>(adj[i].size());
        t.edges += adj[i].size();
        sum += d; sumsq += d * d;
        t.max_degree = std::max(t.max_degree, d);
        degs.push_back(adj[i].size());
    }
    t.edges /= 2;
    t.mean_degree = sum / adj.size();
    const double var = sumsq / adj.size() - t.mean_degree * t.mean_degree;
    t.degree_cv = t.mean_degree > 0 ? std::sqrt(std::max(0.0, var)) / t.mean_degree : 0.0;
    t.density = t.mean_degree / static_cast<double>(adj.size() - 1);

    // Rich club: take the top 12% by degree -- the fraction the human rich club
    // occupies -- and measure how densely THEY are wired to each other.
    {
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
            for (const std::uint32_t w : adj[v])
                if (club.count(w)) ++within;
        const double possible = static_cast<double>(top) * (static_cast<double>(top) - 1.0);
        t.rich_club = possible > 0 ? static_cast<double>(within) / possible : 0.0;
    }

    std::mt19937_64 rng(12345);
    t.clustering  = sample_clustering(adj, adjset, rng, samples);
    t.path_length = sample_path_length(adj, rng, std::min(samples, 60));
    return t;
}

// Degree-preserving random rewiring: the null every small-world statistic is
// measured against. Same degree sequence, no structure.
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
    return out;
}

} // namespace

int main(int argc, char** argv) {
    const std::string prefix = (argc > 1) ? argv[1] : "data/plexus_archive/main";
    std::printf("Khora Plexus topology\n  loading %s.plexus ...\n", prefix.c_str());

    Plexus plex;
    plex.load(prefix);
    if (plex.vocabulary_size() == 0) {
        std::printf("  no graph found at that prefix -- pass the archive prefix as argv[1]\n");
        return 0;
    }

    // Undirected view. The Plexus accrues edges in both directions over a
    // corpus, so it is already effectively symmetric; make that explicit.
    std::vector<std::vector<std::uint32_t>> adj(plex.vocabulary_size());
    for (std::size_t i = 0; i < plex.vocabulary_size(); ++i) {
        for (const auto& [nb, c] : plex.neighbours(i)) {
            (void)c;
            if (nb != i) adj[i].push_back(nb);
        }
    }
    for (auto& v : adj) { std::sort(v.begin(), v.end());
                          v.erase(std::unique(v.begin(), v.end()), v.end()); }

    const Topology k = measure(adj, 3000);
    std::mt19937_64 rng(999);
    auto rnd_adj = rewire(adj, rng);
    for (auto& v : rnd_adj) { std::sort(v.begin(), v.end());
                              v.erase(std::unique(v.begin(), v.end()), v.end()); }
    const Topology r = measure(rnd_adj, 3000);

    std::printf("\n  nodes %zu, undirected edges %zu, mean degree %.1f (max %.0f)\n",
                k.nodes, k.edges, k.mean_degree, k.max_degree);
    std::printf("\n  metric                Khora    degree-matched random\n");
    std::printf("  --------------------+---------+----------------------\n");
    std::printf("  density              %8.5f  %8.5f\n", k.density, r.density);
    std::printf("  clustering C         %8.4f  %8.4f\n", k.clustering, r.clustering);
    std::printf("  path length L        %8.3f  %8.3f\n", k.path_length, r.path_length);
    std::printf("  rich club (top 12%%)  %8.5f  %8.5f   -> %.2fx random\n",
                k.rich_club, r.rich_club,
                r.rich_club > 0 ? k.rich_club / r.rich_club : 0.0);
    std::printf("  degree CV (sd/mean)  %8.3f  %8.3f   (equal by construction --\n"
                "                                        the null preserves degree)\n",
                k.degree_cv, r.degree_cv);

    const double gamma = r.clustering  > 0 ? k.clustering  / r.clustering  : 0.0;
    const double lam   = r.path_length > 0 ? k.path_length / r.path_length : 0.0;
    std::printf("\n  normalised: gamma (C/C_rand) %.2f, lambda (L/L_rand) %.2f,"
                " sigma %.2f\n", gamma, lam, lam > 0 ? gamma / lam : 0.0);

    std::printf("\n  AGAINST MEASURED NERVOUS SYSTEMS\n");
    std::printf("    human structural connectome (1014 ROIs)  density 2.8-3.0%%"
                "   [Hagmann 2008]\n");
    std::printf("    human resting fMRI (90 AAL)              C 0.53 ~2x random,"
                " L 2.49 ~random   [Achard 2006]\n");
    std::printf("    C. elegans chemical (279 neurons)        C 0.16 vs 0.055,"
                " L 2.18 vs 2.28, sigma 2.13   [Varshney 2011]\n");
    std::printf("    macaque cortex, weighted (29 areas)      gamma 1.59,"
                " lambda 1.27, sigma 1.25   [Bassett & Bullmore 2017]\n");
    std::printf("    human rich club                          12%% of nodes carry"
                " 69%% of shortest paths   [van den Heuvel 2012]\n");
    std::printf("    human degree distribution                EXPONENTIAL, not"
                " scale-free; ~10-fold range   [Hagmann 2008]\n");

    std::printf("\n  Caveat the literature is explicit about: sigma is unbounded and"
                "\n  density-dependent, and at high density the binary small-world"
                "\n  signature collapses toward 1 for any graph. Read gamma and the"
                "\n  degree distribution, not sigma alone.\n");
    return 0;
}
