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
#include "khora/ligature/ligature.hpp"
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

    // Parallel CPU — each worker weaves a thread-local Plexus (association) AND
    // Ligature (typed relations) over a stride of the corpus.
    struct Woven { plexus::Plexus plex; ligature::Ligature lig; };
    auto worker = [&texts, T](unsigned k) -> Woven {
        Woven w;
        for (std::size_t i = k; i < texts.size(); i += T) {
            const auto toks = lexicon::tokenize(texts[i]);
            w.plex.observe(toks, 3);
            w.lig.extract(toks);
        }
        return w;
    };
    std::vector<std::future<Woven>> futs;
    futs.reserve(T);
    for (unsigned k = 0; k < T; ++k)
        futs.push_back(std::async(std::launch::async, worker, k));

    // Merge — absorb each thread-local graph, freeing it as we go, then prune
    // the unified Plexus once (prune-at-end keeps each node's true strongest kin).
    plexus::Plexus    plex;
    ligature::Ligature lig;
    for (auto& f : futs) {
        Woven w = f.get();
        plex.absorb(w.plex);
        lig.absorb(w.lig);
    }
    plex.prune_all();

    std::cout << "forged: " << texts.size() << " tomes, " << skipped << " skipped.  "
              << plex.vocabulary_size() << " nodes, " << plex.edge_count()
              << " edges, " << plex.total_tokens() << " tokens.\n";
    std::cout << "ligature: " << lig.triple_count() << " distinct typed relations, "
              << lig.assertions() << " assertions.\n";

    plex.save(fs::path("data") / "plexus_archive" / "main");
    lig.save(fs::path("data") / "ligature_archive" / "main");
    std::cout << "saved -> data/plexus_archive/main.plexus + data/ligature_archive/main.lig\n\n";

    // Proof of STRUCTURE (not just association): what Khora now knows AS A KIND
    // OF and AS A CAUSE.
    const char* rprobes[] = {"force","energy","light","heat","motion","matter",
                             "number","water","man","mind","reason","power"};
    for (const char* w : rprobes) {
        const auto isa = lig.objects(ligature::Relation::IsA, w, 4);
        const auto cau = lig.objects(ligature::Relation::Causes, w, 4);
        if (isa.empty() && cau.empty()) continue;
        std::cout << w << ":";
        for (const auto& [o, c] : isa) std::cout << "  is-a " << o << "(" << c << ")";
        for (const auto& [o, c] : cau) std::cout << "  causes " << o << "(" << c << ")";
        std::cout << "\n";
    }
    std::cout << "\n";

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
