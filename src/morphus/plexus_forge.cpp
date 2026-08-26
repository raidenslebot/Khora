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
#include "khora/taxis/taxis.hpp"

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
    taxis::Taxis tx;
    std::cout << "forging plexus over " << texts.size() << " tomes on " << T
              << " threads (full text, additive merge)...\n";

    // PASS ONE — the part of speech, before anything is extracted.
    //
    // The extractor can veto an is-a whose head it has positive evidence is not
    // a noun, and a tagger has to have seen the words before it can vote on
    // them. So the whole corpus is read for grammar first. Counts are additive
    // exactly as the Plexus counts are, so it goes wide the same way.
    auto tagger = [&texts, T](unsigned k) -> taxis::Taxis {
        taxis::Taxis t;
        for (std::size_t i = k; i < texts.size(); i += T)
            for (const auto& sent : lexicon::tokenize_sentences(texts[i])) t.observe(sent);
        return t;
    };
    {
        std::vector<std::future<taxis::Taxis>> tf;
        tf.reserve(T);
        for (unsigned k = 0; k < T; ++k)
            tf.push_back(std::async(std::launch::async, tagger, k));
        for (auto& f : tf) { taxis::Taxis t = f.get(); tx.absorb(t); }
    }
    std::cout << "tagged: " << tx.vocabulary() << " word types, " << tx.tagged()
              << " given a part of speech.\n";

    // Parallel CPU — each worker weaves a thread-local Plexus (association) AND
    // Ligature (typed relations) over a stride of the corpus.
    //
    // THE TWO STREAMS ARE DIFFERENT ON PURPOSE. The Plexus counts co-occurrence
    // and wants the document as one stream, because words either side of a full
    // stop really are near each other. The Ligature matches syntactic frames and
    // must not cross a sentence boundary: it scans up to five words past a verb
    // for the head of its object, and reading into the next sentence is where
    // is-a(woman, adler) came from -- a proper noun that was never in the same
    // clause. extraction_bench measured the two side by side and this forge was
    // still building the live graph the losing way.
    struct Woven { plexus::Plexus plex; ligature::Ligature lig; };
    auto worker = [&texts, T, &tx](unsigned k) -> Woven {
        Woven w;
        for (std::size_t i = k; i < texts.size(); i += T) {
            w.plex.observe(lexicon::tokenize(texts[i]), 3);
            for (const auto& sent : lexicon::tokenize_sentences(texts[i]))
                w.lig.extract(sent, ligature::Ligature::PatAll, &tx);
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

    // The tagger is saved too. It cost a whole pass over the corpus to build and
    // every consumer of the graph has the same question about a word that the
    // extractor did.
    tx.save(fs::path("data") / "taxis_archive");
    plex.save(fs::path("data") / "plexus_archive" / "main");
    lig.save(fs::path("data") / "ligature_archive" / "main");
    std::cout << "saved -> plexus_archive + ligature_archive + taxis_archive\n\n";

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
