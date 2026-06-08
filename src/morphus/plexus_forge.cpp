// plexus_forge — fast standalone (re)builder of the associative graph.
//
// The Plexus needs only the token stream — not the cortex, not the lexicon's
// heavy D-dim accumulators. So the whole graph can be woven over the FULL
// corpus in under a minute (no 100k-token cap, no predictive learning) by
// reading every prose tome straight from the Reservoir and observing it. The
// result is saved to the canonical data/plexus_archive/main.plexus that the
// runtime loads at startup, and a few weaves are printed as direct proof the
// frequency hubs are gone — sharp kin, not "the/of/and".
//
// This is a permanent asset: the Plexus can be rebuilt any time, decoupled from
// the slow cortex training, without disturbing khora.exe.

#include "khora/lexicon/lexicon.hpp"
#include "khora/plexus/plexus.hpp"
#include "khora/reservoir/reservoir.hpp"

#include <filesystem>
#include <iostream>
#include <string>

int main() {
    using namespace khora;
    namespace fs = std::filesystem;

    reservoir::Reservoir pool(fs::path("data") / "reservoir",
                              20ull * 1024 * 1024 * 1024);
    plexus::Plexus plex;

    const auto cat = pool.catalog();
    std::cout << "forging plexus over " << cat.size() << " tomes (full text, no cap)...\n";

    std::size_t studied = 0, skipped = 0;
    for (const auto& t : cat) {
        if (t.topic == "code") { ++skipped; continue; }  // code pollutes semantic kin
        auto text = pool.read(t.title);
        if (!text) { ++skipped; continue; }
        const auto toks = lexicon::tokenize(*text);
        const std::size_t ev = plex.observe(toks, 3);
        ++studied;
        std::cout << "  [" << studied << "] " << t.title << "  "
                  << toks.size() << " tokens, " << ev << " cooc  (nodes "
                  << plex.vocabulary_size() << ", edges " << plex.edge_count() << ")\n";
    }
    std::cout << "forged: " << studied << " tomes, " << skipped << " skipped.  "
              << plex.vocabulary_size() << " nodes, " << plex.edge_count()
              << " edges, " << plex.total_tokens() << " tokens.\n";

    plex.save(fs::path("data") / "plexus_archive" / "main");
    std::cout << "saved -> data/plexus_archive/main.plexus\n\n";

    // Proof: the hub-proof kin of charged concepts. If the hubs were alive,
    // every one of these would read "the of and to a..."; they do not.
    const char* probes[] = {"justice", "war", "love", "knowledge", "woman",
                            "death", "god", "power", "nature", "time",
                            "mind", "fear", "reason", "soul"};
    for (const char* w : probes) {
        const auto kin = plex.associates(w, 10);
        std::cout << w << " (" << plex.occurrences(w) << "x) -> ";
        if (kin.empty()) std::cout << "(no kin)";
        for (const auto& [k, s] : kin) std::cout << k << " ";
        std::cout << "\n";
    }
    return 0;
}
