// plexus_forge — fast standalone (re)builder of the associative graph, now
// PARALLEL across every core.
//
// The Plexus needs only the token stream — not the cortex, not the lexicon's
// heavy accumulators. And co-occurrence counting is an additive, commutative
// monoid: counts gathered over disjoint slices of the corpus sum to the same
// graph as a serial pass. So the whole graph is woven across all hardware
// threads — each worker builds a thread-local Plexus over its slice of the
// tomes, then they are absorbed into one and pruned once. The reservoir read is
// stateful, so I/O stays serial; only the CPU-heavy counting goes wide.
//
// Result is saved to the canonical data/plexus_archive/main.plexus the runtime
// loads at startup, with a few weaves printed as proof the hubs are gone.

#include "khora/lexicon/lexicon.hpp"
#include "khora/plexus/plexus.hpp"
#include "khora/reservoir/reservoir.hpp"

#include <filesystem>
#include <future>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

int main() {
    using namespace khora;
    namespace fs = std::filesystem;

    reservoir::Reservoir pool(fs::path("data") / "reservoir",
                              20ull * 1024 * 1024 * 1024);

    // Serial I/O — pull every prose tome's text into memory. The reservoir's
    // read() mutates catalog metadata, so it must not be called concurrently.
    const auto cat = pool.catalog();
    std::vector<std::string> texts;
    texts.reserve(cat.size());
    std::size_t skipped = 0;
    for (const auto& t : cat) {
        if (t.topic == "code") { ++skipped; continue; }  // code pollutes semantic kin
        auto text = pool.read(t.title);
        if (!text) { ++skipped; continue; }
        texts.push_back(std::move(*text));
    }

    const unsigned T = std::max(1u, std::thread::hardware_concurrency());
    std::cout << "forging plexus over " << texts.size() << " tomes on " << T
              << " threads (full text, additive merge)...\n";

    // Parallel CPU — each worker weaves a thread-local Plexus over a stride of
    // the corpus (tomes k, k+T, k+2T, ...), so the slices are balanced.
    auto worker = [&texts, T](unsigned k) -> plexus::Plexus {
        plexus::Plexus local;
        for (std::size_t i = k; i < texts.size(); i += T)
            local.observe(lexicon::tokenize(texts[i]), 3);
        return local;
    };
    std::vector<std::future<plexus::Plexus>> futs;
    futs.reserve(T);
    for (unsigned k = 0; k < T; ++k)
        futs.push_back(std::async(std::launch::async, worker, k));

    // Merge — absorb each thread-local graph, freeing it as we go, then prune
    // the unified graph once (prune-at-end keeps each node's true strongest kin).
    plexus::Plexus plex;
    for (auto& f : futs) {
        plexus::Plexus local = f.get();
        plex.absorb(local);
    }
    plex.prune_all();

    std::cout << "forged: " << texts.size() << " tomes, " << skipped << " skipped.  "
              << plex.vocabulary_size() << " nodes, " << plex.edge_count()
              << " edges, " << plex.total_tokens() << " tokens.\n";

    plex.save(fs::path("data") / "plexus_archive" / "main");
    std::cout << "saved -> data/plexus_archive/main.plexus\n\n";

    const char* probes[] = {"force", "energy", "motion", "light", "heat",
                            "machine", "number", "problem", "matter", "engine",
                            "work", "quantity", "cause", "knowledge", "nature",
                            "power", "reason", "mind"};
    for (const char* w : probes) {
        const auto kin = plex.associates(w, 10);
        std::cout << w << " (" << plex.occurrences(w) << "x) -> ";
        if (kin.empty()) std::cout << "(no kin)";
        for (const auto& [k, s] : kin) std::cout << k << " ";
        std::cout << "\n";
    }
    return 0;
}
