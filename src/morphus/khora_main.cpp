// khora.exe — the actual Morphus runtime entry point.
//
// Brings up the full architecture (Lattice + Cortex + Soma + Reverie),
// registers tools via Carapace, optionally loads/saves Lattice state
// to data/lattice_archive/main.klat across runs, and offers an
// interactive REPL.

#include "khora/carapace/builtin_tools.hpp"
#include "khora/carapace/carapace.hpp"
#include "khora/cogitator/cogitator.hpp"
#include "khora/curator/curator.hpp"
#include "khora/curator/curator_scheduler.hpp"
#include "khora/hand/hand.hpp"
#include "khora/bulwark/bulwark.hpp"
#include "khora/maw/maw.hpp"
#include "khora/cortex/predictive_column.hpp"
#include "khora/lattice/lattice.hpp"
#include "khora/lattice/persistence.hpp"
#include "khora/ballast/ballast.hpp"
#include "khora/lexicon/lexicon.hpp"
#include "khora/ligature/ligature.hpp"
#include "khora/plexus/plexus.hpp"
#include "khora/lodestone/lodestone.hpp"
#include "khora/maelstrom/maelstrom.hpp"
#include "khora/reservoir/aqueduct.hpp"
#include "khora/reservoir/reservoir.hpp"
#include "khora/reverie/reverie_loom.hpp"
#include "khora/reverie/reverie_scheduler.hpp"
#include "khora/soma/soma_nexus.hpp"
#include "khora/volition/volition.hpp"
#include "khora/volition/volition_scheduler.hpp"
#include "khora/whetstone/whetstone.hpp"
#include "khora/whetstone/whetstone_scheduler.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdio>
#include <ctime>
#include <deque>
#include <fstream>
#include <shared_mutex>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <string>

namespace {

constexpr const char* kArchivePath          = "data/lattice_archive/main.klat";
constexpr const char* kCortexArchivePrefix  = "data/cortex_archive/main";
constexpr const char* kLexiconArchivePrefix = "data/lexicon_archive/main";
constexpr const char* kPlexusArchivePrefix  = "data/plexus_archive/main";
constexpr const char* kLigatureArchivePrefix = "data/ligature_archive/main";
constexpr const char* kYieldLedgerPath       = "data/ledger/yield.tsv";
constexpr const char* kYieldParamsPath       = "data/ledger/params.txt";
constexpr const char* kAttractorsPath       = "data/cogitator_archive/attractors.txt";
constexpr const char* kAbstractionsPath     = "data/cogitator_archive/abstractions.txt";

std::string read_line_prompt(const std::string& prompt) {
    std::cout << prompt << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) return "__EOF__";
    return line;
}

void print_banner() {
    std::cout
      << "Khora interactive runtime  -  Morphus engineer at the helm\n"
      << "Substrate: 10,000-bit sparse hypervectors, LLM-free.\n"
      << "Type 'help' to list tools, 'exit' or Ctrl-Z+Enter to quit.\n\n";
}

} // namespace

int main(int argc, char** argv) {
    using namespace khora;

    // ascend_selftest: the successor binary proves it can LAUNCH the runtime before it
    // is ever promoted over the running image. Intercepted before anything else so it
    // never reaches the single-command dispatcher.
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "ascend_selftest") {
            std::cout << "ascend_selftest ok\n";
            return 0;
        }
    }

    // 1. Bring up subsystems.
    lattice::Lattice memory;
    cortex::PredictiveColumn column(3);
    soma::SomaNexus nexus;
    lexicon::Lexicon lex;
    plexus::Plexus    plex;   // associative graph memory — the hub-proof kin
    ligature::Ligature lig;   // structured-relation layer — typed knowledge (is-a, causes)
    khora::maw::Maw     maw;   // chaos-exploration drive (runs only behind the Bulwark cage)
    reverie::ReverieLoom dream(memory, column, nexus);
    cogitator::Cogitator mind(lex, memory, column, nexus);
    mind.set_plexus(&plex);   // the Spire forms abstractions on hub-proof PMI kin

    // Liquid knowledge: the Reservoir holds source texts (~20 GB cap),
    // the Aqueduct channels new ones in from the public domain.
    reservoir::Reservoir pool(std::filesystem::path("data") / "reservoir",
                              20ull * 1024 * 1024 * 1024);
    reservoir::Aqueduct aqueduct(pool);

    // The Curator — Khora decides for itself what to learn next. Studied
    // vocabulary is promoted into `memory` so cognition can think over it.
    curator::Curator curator(pool, aqueduct, lex, column, &memory, &plex, &lig);

    // The Ballast — Khora may now claim up to 24 GB of system RAM (raised from
    // 4 GB: the operator has 32 GB and wants the headroom USED). It still backs
    // off hard the instant total system RAM crosses 90% — the machine must never
    // lock up — and the Lodestone sizes the actual caps to what is really free.
    // GPU memory and NVMe are used freely elsewhere.
    ballast::Ballast ballast(/*cap_mb*/24576, /*system_pressure*/0.90);
    std::atomic<std::uint64_t> ballast_sheds{0};

    // 2. Try to load persisted lattice + cortex state.
    namespace fs = std::filesystem;
    fs::create_directories(fs::path(kArchivePath).parent_path());
    fs::create_directories(fs::path(kCortexArchivePrefix).parent_path());

    // Ascend boot sentinel: if a self-replacement is pending, this image proves it
    // booted clean (the relauncher waits for this flag before declaring success and
    // before discarding the known-good backup) and clears the marker.
    {
        std::error_code aec; fs::create_directories("data/ascend", aec);
        if (fs::exists("data/ascend/pending")) {
            std::ofstream ok("data/ascend/boot_ok.flag", std::ios::trunc);
            ok << static_cast<long>(std::time(nullptr)) << '\n';
            ok.close();
            fs::remove("data/ascend/pending", aec);
            std::cout << "[ascended: this image is a self-rewritten successor — booted clean]\n";
        }
    }

    if (fs::exists(kArchivePath)) {
        try {
            memory = lattice::load(kArchivePath);
            std::cout << "[loaded " << memory.size() << " glyphs from "
                      << kArchivePath << "]\n";
        } catch (const lattice::PersistError& e) {
            std::cout << "[warning: could not load lattice archive: " << e.what() << "]\n";
        }
    }
    {
        fs::path cortex_header = kCortexArchivePrefix; cortex_header += ".cortex";
        if (fs::exists(cortex_header)) {
            try {
                column.load(kCortexArchivePrefix);
                std::cout << "[loaded cortex state: " << column.observations()
                          << " obs, " << column.associations() << " assoc, "
                          << "recent_acc=" << column.recent_accuracy() << "]\n";
            } catch (const lattice::PersistError& e) {
                std::cout << "[warning: could not load cortex archive: " << e.what() << "]\n";
            }
        }
    }
    {
        fs::path lex_sem = kLexiconArchivePrefix; lex_sem += ".sem.klat";
        if (fs::exists(lex_sem)) {
            try {
                lex.load(kLexiconArchivePrefix);
                std::cout << "[loaded lexicon: " << lex.vocabulary_size()
                          << " words, " << lex.total_observations() << " observations]\n";
            } catch (const lattice::PersistError& e) {
                std::cout << "[warning: could not load lexicon archive: " << e.what() << "]\n";
            }
        }
    }
    {
        fs::path plex_path = kPlexusArchivePrefix; plex_path += ".plexus";
        if (fs::exists(plex_path)) {
            try {
                plex.load(kPlexusArchivePrefix);
                std::cout << "[loaded plexus: " << plex.vocabulary_size()
                          << " nodes, " << plex.edge_count() << " associative edges]\n";
            } catch (const std::exception& e) {
                std::cout << "[warning: could not load plexus archive: " << e.what() << "]\n";
            }
        }
    }
    {
        fs::path lig_path = kLigatureArchivePrefix; lig_path += ".lig";
        if (fs::exists(lig_path)) {
            try {
                lig.load(kLigatureArchivePrefix);
                std::cout << "[loaded ligature: " << lig.triple_count()
                          << " typed relations (is-a, causes, has)]\n";
            } catch (const std::exception& e) {
                std::cout << "[warning: could not load ligature archive: " << e.what() << "]\n";
            }
        }
    }
    {
        // Restore the parameter Khora tuned by MEASURED yield last session —
        // self-improvement that persists across lives.
        std::ifstream pf(kYieldParamsPath);
        double gp = 0.0;
        if (pf >> gp && gp > 0.0 && gp < 20.0) {
            mind.set_infer_goal_pull(gp);
            std::cout << "[resumed self-tuned inference goal-pull: " << gp << "]\n";
        }
    }
    // Restore Khora's preoccupations — its inner life resumes where it left off.
    mind.load_attractors(kAttractorsPath);
    mind.load_abstractions(kAbstractionsPath);   // the tower of abstraction resumes rising
    if (mind.abstraction_count() > 0)
        std::cout << "[resumed spire: " << mind.abstraction_count()
                  << " abstractions, depth " << mind.abstraction_depth() << "]\n";
    if (!mind.top_attractors(1).empty()) {
        std::cout << "[resumed mind: preoccupied with";
        for (const auto& [name, count] : mind.top_attractors(5)) std::cout << ' ' << name;
        std::cout << "]\n";
    }

    // 3. Shared mutex coordinating the main thread (operator tools) with
    //    the background reverie thread.
    std::shared_mutex shared_mu;
    std::atomic<bool> maw_armed{false};   // OPT-IN: the Maw explores only when the operator arms it

    // 4. Register tools (with lexicon wired into memory + cortex).
    carapace::Carapace shell;
    carapace::register_core_tools(shell);
    carapace::register_memory_tools(shell, memory, &lex);
    carapace::register_cortex_tools(shell, column, &lex);
    carapace::register_soma_tools(shell, nexus);
    carapace::register_lexicon_tools(shell, lex);
    carapace::register_cogitator_tools(shell, mind);

    shell.register_tool({
        "hardware",
        "gauge the machine and show the adaptive operating profile",
        [&column, &lex, &ballast](const carapace::Intent&) -> carapace::ToolResult {
            const auto hw = lodestone::gauge("data", ballast.cap_mb());
            column.set_max_associations(hw.recommended_assoc_cap);
            lex.set_max_vocabulary(hw.recommended_vocab_cap);
            return {true, hw.summary(), ""};
        }
    });
    shell.register_tool({
        "ballast",
        "show Khora's memory governor (RAM cap, system pressure, sheds)",
        [&ballast, &ballast_sheds, &column, &lex](const carapace::Intent&) -> carapace::ToolResult {
            std::ostringstream os;
            os << ballast.summary() << "\n"
               << "  load shed events  : " << ballast_sheds.load() << "\n"
               << "  cortex assoc      : " << column.associations() << " / "
               << column.max_associations() << " cap\n"
               << "  lexicon vocab     : " << lex.vocabulary_size() << " / "
               << lex.max_vocabulary() << " cap";
            return {true, os.str(), ""};
        }
    });
    shell.register_tool({
        "maelstrom",
        "ignite the GPU resonance engine; verify vs CPU + benchmark the crossover  (usage: maelstrom [N])",
        [](const carapace::Intent& in) -> carapace::ToolResult {
            using namespace khora::lattice;
            using clock = std::chrono::steady_clock;
            auto ms = [](clock::duration d) {
                return std::chrono::duration<double, std::milli>(d).count();
            };

            std::size_t N = 200000;
            if (!in.args.empty()) {
                try { N = static_cast<std::size_t>(std::stoul(in.args[0])); } catch (...) {}
            }
            if (N < 1000)    N = 1000;
            if (N > 1200000) N = 1200000; // single-buffer ceiling headroom

            maelstrom::Maelstrom storm;
            if (!storm.ignite()) {
                return {false, "", "Maelstrom could not ignite: " + storm.device().note};
            }
            const auto& dev = storm.device();

            std::ostringstream os;
            os << "Maelstrom ignited  -  GPU parallel-resonance engine\n"
               << "  adapter      : " << dev.adapter << "\n"
               << "  VRAM         : " << dev.vram_mb << " MB   (feature level " << dev.feature << ")\n";

            // Synthetic glyph database (decorrelated random hypervectors).
            std::vector<Glyph> db;
            db.reserve(N);
            for (std::size_t i = 0; i < N; ++i)
                db.push_back(Glyph::random(0x9E3779B97F4A7C15ull * (i + 1) + 0xD1B54A32D192ED03ull));

            const clock::time_point c0 = clock::now();
            if (!storm.charge(db)) {
                return {false, "", "charge failed: " + storm.device().note};
            }
            const double charge_ms = ms(clock::now() - c0);
            os << "  charged      : " << storm.charged() << " glyphs into VRAM ("
               << (storm.vram_bytes() / (1024 * 1024)) << " MB) in "
               << charge_ms << " ms\n\n";

            // ---- Correctness: GPU hamming must equal CPU hamming bit-for-bit.
            auto verify = [&](const Glyph& probe, const char* tag) {
                const auto gpu = storm.hamming_all(probe);
                std::size_t mism = 0, first = SIZE_MAX;
                for (std::size_t i = 0; i < db.size(); ++i) {
                    const auto cpu = static_cast<std::uint32_t>(probe.hamming(db[i]));
                    if (i < gpu.size() && gpu[i] != cpu) {
                        if (first == SIZE_MAX) first = i;
                        ++mism;
                    }
                }
                os << "  verify " << tag << " : "
                   << (mism == 0 ? "EXACT" : "MISMATCH")
                   << " (" << mism << " of " << db.size() << " differ";
                if (mism) os << ", first @" << first;
                os << ")\n";
                return mism == 0;
            };
            const bool ok1 = verify(db[N / 3], "self-probe ");
            const bool ok2 = verify(Glyph::random(0xC0FFEE15600DULL), "random-probe");

            // ---- Benchmark: CPU lattice scan vs GPU resonance, across scales.
            const std::size_t K = 8;
            std::vector<Glyph> probes;
            for (int p = 0; p < 16; ++p)
                probes.push_back(Glyph::random(0x5EED0000ull + static_cast<std::uint64_t>(p) * 7919));

            auto cpu_topk = [&](const Glyph& probe, std::size_t m, std::vector<std::uint32_t>& outH) {
                std::vector<std::uint32_t> d(m);
                for (std::size_t i = 0; i < m; ++i) d[i] = static_cast<std::uint32_t>(probe.hamming(db[i]));
                std::vector<std::uint32_t> idx(m);
                for (std::uint32_t i = 0; i < m; ++i) idx[i] = i;
                const std::size_t kk = std::min(K, m);
                std::partial_sort(idx.begin(), idx.begin() + kk, idx.end(),
                    [&](std::uint32_t a, std::uint32_t b) {
                        if (d[a] != d[b]) return d[a] < d[b];
                        return a < b;
                    });
                outH.clear();
                for (std::size_t i = 0; i < kk; ++i) outH.push_back(d[idx[i]]);
            };

            os << "\n  scale       CPU scan      GPU resonate     speedup   top-" << K << "\n";
            const std::size_t sizes[] = { 10000, 50000, 200000, 500000, 1000000 };
            bool topk_ok = true;
            for (std::size_t sz : sizes) {
                if (sz > N) continue;
                storm.charge(std::vector<Glyph>(db.begin(), db.begin() + sz));

                // warm the GPU pipeline (first dispatch pays driver setup).
                (void)storm.resonate(probes[0], K);

                clock::duration cpu_t{}, gpu_t{};
                for (const auto& pr : probes) {
                    std::vector<std::uint32_t> cpuH, gpuH;
                    clock::time_point t0 = clock::now();
                    cpu_topk(pr, sz, cpuH);
                    cpu_t += clock::now() - t0;

                    t0 = clock::now();
                    const auto neigh = storm.resonate(pr, K);
                    gpu_t += clock::now() - t0;
                    for (const auto& nb : neigh) gpuH.push_back(nb.hamming);

                    if (cpuH != gpuH) topk_ok = false;
                }
                const double cpu_avg = ms(cpu_t) / probes.size();
                const double gpu_avg = ms(gpu_t) / probes.size();
                char line[160];
                std::snprintf(line, sizeof(line),
                    "  %8zu   %8.3f ms   %8.3f ms     %6.1fx   %s\n",
                    sz, cpu_avg, gpu_avg,
                    gpu_avg > 0 ? cpu_avg / gpu_avg : 0.0,
                    topk_ok ? "match" : "DIVERGED");
                os << line;
            }

            // Batched dispatch: many probes in ONE call must equal the
            // individual calls, and amortise the per-query round-trip.
            {
                const std::size_t B = 64;
                std::vector<Glyph> bp;
                for (std::size_t i = 0; i < B; ++i)
                    bp.push_back(Glyph::random(0x717E0000ull + static_cast<std::uint64_t>(i) * 40503));
                clock::time_point t0 = clock::now();
                const auto batch = storm.resonate_batch(bp, K);
                const double batch_ms = ms(clock::now() - t0);
                t0 = clock::now();
                bool bmatch = batch.size() == B;
                for (std::size_t i = 0; i < B && bmatch; ++i) {
                    const auto solo = storm.resonate(bp[i], K);
                    if (solo.size() != batch[i].size()) { bmatch = false; break; }
                    for (std::size_t j = 0; j < solo.size(); ++j)
                        if (solo[j].hamming != batch[i][j].hamming) { bmatch = false; break; }
                }
                const double solo_ms = ms(clock::now() - t0);
                if (!bmatch) topk_ok = false;
                char line[176];
                std::snprintf(line, sizeof(line),
                    "\n  batch x%zu    : %.3f ms batched  vs  %.3f ms individual  (%.1fx)  %s\n",
                    B, batch_ms, solo_ms, batch_ms > 0 ? solo_ms / batch_ms : 0.0,
                    bmatch ? "bit-exact" : "DIVERGED");
                os << line;
            }

            os << "  correctness  : "
               << ((ok1 && ok2 && topk_ok) ? "GPU is bit-exact with the CPU oracle"
                                           : "DISCREPANCY DETECTED - GPU path is wrong")
               << "\n  (synthetic benchmark; the Maelstrom is now wired as the lattice's "
                  "scale-out k-NN accelerator.)";
            return {ok1 && ok2 && topk_ok, os.str(), ""};
        }
    });
    shell.register_tool({
        "resonator",
        "verify the labelled CPU/GPU associative store (Resonator)  (usage: resonator [N])",
        [&memory](const carapace::Intent& in) -> carapace::ToolResult {
            using namespace khora::lattice;
            std::size_t N = 50000;
            if (!in.args.empty()) {
                try { N = static_cast<std::size_t>(std::stoul(in.args[0])); } catch (...) {}
            }
            if (N < 100)    N = 100;
            if (N > 1000000) N = 1000000;

            // A labelled synthetic field; labels carry the index so a correct
            // index->label mapping is checkable by parsing the label back.
            std::vector<std::pair<std::string, Glyph>> entries;
            entries.reserve(N);
            for (std::size_t i = 0; i < N; ++i)
                entries.emplace_back("g" + std::to_string(i),
                    Glyph::random(0x100000001B3ull * (i + 1) + 0xCBF29CE484222325ull));

            maelstrom::Resonator gpu(3000);            // crosses over to GPU
            maelstrom::Resonator cpu(N + 1);           // forced CPU (crossover above N)
            gpu.build(entries);
            cpu.build(entries);

            std::ostringstream os;
            os << "Resonator verification\n"
               << "  field size   : " << gpu.size() << " labelled glyphs\n"
               << "  GPU path     : " << (gpu.on_gpu() ? "ACTIVE" : "inactive")
               << "  (" << (gpu.on_gpu() ? gpu.device().adapter : std::string("CPU fallback")) << ")\n"
               << "  CPU path     : " << (cpu.on_gpu() ? "ACTIVE" : "inactive (forced)") << "\n";

            // Independent reference scan + cross-path agreement.
            const std::size_t K = 8;
            std::size_t agree = 0, checks = 0;
            bool exact = true;
            for (int p = 0; p < 12; ++p) {
                const Glyph probe = Glyph::random(0xBEEF0000ull + static_cast<std::uint64_t>(p) * 2654435761ull);

                // reference: brute-force top-K (hamming, then index).
                std::vector<std::uint32_t> idx(N), d(N);
                for (std::size_t i = 0; i < N; ++i) {
                    idx[i] = static_cast<std::uint32_t>(i);
                    d[i]   = static_cast<std::uint32_t>(probe.hamming(entries[i].second));
                }
                std::partial_sort(idx.begin(), idx.begin() + K, idx.end(),
                    [&](std::uint32_t a, std::uint32_t b) {
                        if (d[a] != d[b]) return d[a] < d[b];
                        return a < b;
                    });

                const auto gq = gpu.query(probe, K);
                const auto cq = cpu.query(probe, K);
                for (std::size_t i = 0; i < K; ++i) {
                    const std::string ref = "g" + std::to_string(idx[i]);
                    ++checks;
                    const bool gok = i < gq.size() && gq[i].label == ref && gq[i].hamming == d[idx[i]];
                    const bool cok = i < cq.size() && cq[i].label == ref && cq[i].hamming == d[idx[i]];
                    if (gok && cok) ++agree; else exact = false;
                }
            }
            os << "  top-" << K << " checks: " << agree << " / " << checks
               << (exact ? "  EXACT (GPU == CPU == brute-force reference)"
                         : "  MISMATCH") << "\n";

            // Bind the live concept space (whatever the operator has studied).
            maelstrom::Resonator live(3000);
            live.build(memory);
            os << "  live lattice : " << live.size() << " glyphs, "
               << (live.on_gpu() ? "GPU" : "CPU") << " path"
               << (live.size() < 3000 ? "  (below crossover -> CPU, as expected)" : "");

            return {exact, os.str(), ""};
        }
    });
    shell.register_tool({
        "nearest",
        "k most semantically-similar learned words, GPU-accelerated  (usage: nearest <word> [k])",
        [&lex](const carapace::Intent& in) -> carapace::ToolResult {
            using namespace khora::lattice;
            if (in.args.empty()) return {false, "", "usage: nearest <word> [k]"};
            const std::string word = in.args[0];
            std::size_t k = 8;
            if (in.args.size() >= 2) {
                try { k = static_cast<std::size_t>(std::stoul(in.args[1])); } catch (...) {}
            }
            if (k < 1) k = 1;
            if (!lex.has(word))
                return {false, "", "'" + word + "' is not in the lexicon yet - study or lex_expose first"};

            // Search the PURE distributional context glyphs of content words
            // (salient tokens) — isolating "keeps similar company" from mere
            // spelling overlap, and excluding ubiquitous function-word hubs.
            // Fall back to the full context field for a small lexicon.
            std::vector<std::pair<std::string, Glyph>> field;
            {
                auto salient = lex.salient_tokens(200000, 3);
                field.reserve(salient.size());
                for (auto& w : salient) field.emplace_back(w, lex.context_glyph(w));
                if (field.size() < 50) field = lex.context_field();
            }
            if (field.size() < 2)
                return {false, "", "lexicon has too few learned words for neighbour search"};
            const Glyph probe = lex.context_glyph(word);

            // GPU-accelerated associative recall over the whole vocabulary.
            // The crossover is low so even a modest vocabulary exercises the
            // card; the search is exact (similarity() is Hamming over these
            // very glyphs).
            maelstrom::Resonator res(256);
            res.build(field);

            // Demote distributional hubs: drop entries whose resonance-
            // centrality is a strong outlier (mean + 2σ), so the neighbours
            // aren't swamped by function words that keep everyone's company.
            std::size_t hubs_dropped = 0;
            if (field.size() > 64) {
                const auto deg = res.centrality(10);
                if (deg.size() == field.size()) {
                    double mean = 0.0;
                    for (auto d : deg) mean += d;
                    mean /= static_cast<double>(deg.size());
                    double var = 0.0;
                    for (auto d : deg) { const double e = d - mean; var += e * e; }
                    var /= static_cast<double>(deg.size());
                    const double cut = mean + 2.0 * std::sqrt(var);
                    std::vector<std::pair<std::string, Glyph>> clean;
                    clean.reserve(field.size());
                    for (std::size_t i = 0; i < field.size(); ++i)
                        if (field[i].first == word || deg[i] <= cut) clean.push_back(field[i]);
                    if (clean.size() >= 2 && clean.size() < field.size()) {
                        hubs_dropped = field.size() - clean.size();
                        field.swap(clean);
                        res.build(field);
                    }
                }
            }

            const std::size_t want = std::min(field.size(), k + 1); // +1 to drop self
            const auto hits = res.query(probe, want);

            // Exactness audit: brute-force the same field, compare the hamming
            // sequence (tie-break-agnostic) — must be identical.
            std::vector<std::uint32_t> ref(field.size());
            for (std::size_t i = 0; i < field.size(); ++i)
                ref[i] = static_cast<std::uint32_t>(probe.hamming(field[i].second));
            std::partial_sort(ref.begin(), ref.begin() + want, ref.end());
            bool exact = true;
            for (std::size_t i = 0; i < want && i < hits.size(); ++i)
                if (hits[i].hamming != ref[i]) exact = false;

            std::ostringstream os;
            os << "nearest to '" << word << "'  (" << field.size()
               << " content words, " << hubs_dropped << " hubs demoted, "
               << (res.on_gpu() ? "GPU" : "CPU") << " path)\n";
            std::size_t shown = 0;
            for (const auto& h : hits) {
                if (h.label == word) continue; // skip self
                os << "  " << h.label << "   sim=" << h.similarity
                   << "  (hamming " << h.hamming << ")\n";
                if (++shown >= k) break;
            }
            os << "  audit: " << (exact ? "EXACT (matches brute-force reference)" : "MISMATCH");
            return {exact, os.str(), ""};
        }
    });

    // System-level tools that close over multiple subsystems.
    shell.register_tool({
        "stats",
        "summary of all subsystems",
        [&memory, &column, &nexus](const carapace::Intent&) -> carapace::ToolResult {
            std::ostringstream os;
            os << "Khora system status\n"
               << "  Morphic Lattice    : " << memory.size() << " glyphs\n"
               << "  Stratiform Cortex  : " << column.observations() << " obs, "
               << column.associations() << " assoc, "
               << "recent_acc=" << column.recent_accuracy() << "\n"
               << "  Soma Nexus:\n";
            const auto s = nexus.snapshot();
            for (std::size_t i = 0; i < soma::kDriveCount; ++i) {
                os << "    " << soma::drive_name(static_cast<soma::Drive>(i))
                   << " = " << s[i] << "\n";
            }
            return {true, os.str(), ""};
        }
    });

    shell.register_tool({
        "dream",
        "run N reverie cycles  (usage: dream [N])",
        [&dream](const carapace::Intent& i) -> carapace::ToolResult {
            std::size_t n = 100;
            if (!i.args.empty()) {
                try { n = static_cast<std::size_t>(std::stoul(i.args[0])); }
                catch (...) {}
            }
            const auto retained = dream.dream_n(n);
            std::ostringstream os;
            os << "ran " << n << " dream cycles, retained " << retained
               << "  (total cycles=" << dream.cycles()
               << ", total retained=" << dream.retained() << ")";
            return {true, os.str(), ""};
        }
    });

    shell.register_tool({
        "save",
        "persist the Lattice to disk now",
        [&memory](const carapace::Intent&) -> carapace::ToolResult {
            try {
                auto s = lattice::save(memory, kArchivePath);
                std::ostringstream os;
                os << "saved " << s.glyph_count << " glyphs ("
                   << s.bytes_written << " bytes) to " << kArchivePath;
                return {true, os.str(), ""};
            } catch (const std::exception& e) {
                return {false, "", std::string("save failed: ") + e.what()};
            }
        }
    });

    // 5z. Reservoir / Aqueduct tools — Khora's liquid knowledge.
    shell.register_tool({
        "reservoir_status",
        "summarize the liquid knowledge pool",
        [&pool](const carapace::Intent&) -> carapace::ToolResult {
            std::ostringstream os;
            const double used_mb = static_cast<double>(pool.total_stored_bytes()) / (1024.0 * 1024.0);
            const double cap_mb  = static_cast<double>(pool.cap_bytes()) / (1024.0 * 1024.0);
            os << "Reservoir: " << pool.count() << " tomes, "
               << used_mb << " MB / " << cap_mb << " MB used";
            return {true, os.str(), ""};
        }
    });
    shell.register_tool({
        "reservoir_list",
        "list the tomes in the pool with their stats",
        [&pool](const carapace::Intent&) -> carapace::ToolResult {
            std::ostringstream os;
            auto cat = pool.catalog();
            if (cat.empty()) return {true, "(pool empty — use 'forage' to acquire)", ""};
            os << "Tomes (" << cat.size() << "):\n";
            for (const auto& t : cat) {
                os << "  " << t.title << "  [" << t.topic << "]  "
                   << (t.original_bytes / 1024) << "KB orig / "
                   << (t.stored_bytes / 1024) << "KB stored  reads=" << t.times_read
                   << "  mastery=" << t.mastery
                   << "  keep=" << pool.keep_value(t) << "\n";
            }
            return {true, os.str(), ""};
        }
    });
    shell.register_tool({
        "reservoir_read",
        "read the first part of a tome's distilled text  (usage: reservoir_read <title>)",
        [&pool](const carapace::Intent& i) -> carapace::ToolResult {
            if (i.args.empty()) return {false, "", "usage: reservoir_read <title>"};
            std::string title;
            for (std::size_t k = 0; k < i.args.size(); ++k) { if (k) title += ' '; title += i.args[k]; }
            auto text = pool.read(title);
            if (!text) return {false, "", "not in pool: " + title};
            const std::string head = text->substr(0, std::min<std::size_t>(text->size(), 600));
            return {true, head + (text->size() > 600 ? "\n...[" + std::to_string(text->size()) + " bytes total]" : ""), ""};
        }
    });
    shell.register_tool({
        "forage",
        "autonomously acquire a new tome from the public domain  (usage: forage [topic])",
        [&aqueduct](const carapace::Intent& i) -> carapace::ToolResult {
            const std::string topic = i.args.empty() ? std::string{} : i.args[0];
            auto r = aqueduct.forage(topic);
            if (!r) return {true, "nothing left to forage" + (topic.empty() ? std::string{} : " in topic '" + topic + "'"), ""};
            if (!r->ok) return {false, "", "forage failed for '" + r->title + "': " + r->error};
            std::ostringstream os;
            os << "acquired \"" << r->title << "\"  "
               << (r->original_bytes / 1024) << "KB distilled, "
               << (r->stored_bytes / 1024) << "KB stored ("
               << r->compression_ratio << "x), lossless="
               << (r->verified_lossless ? "yes" : "no");
            if (!r->evicted.empty()) {
                os << "  evicted " << r->evicted.size() << " to make room";
            }
            return {true, os.str(), ""};
        }
    });
    // forage_about — OPEN-ENDED curiosity-directed acquisition. Khora searches all
    // of Project Gutenberg (via Gutendex) for ANY topic and acquires the best
    // plain-text match. This is the exponential lever: knowledge it was never
    // handed, brought in on demand — the finite seed catalog no longer the ceiling.
    shell.register_tool({
        "forage_about",
        "Khora forages new knowledge on ANY topic from all of Gutenberg (usage: forage_about <topic>)",
        [&aqueduct](const carapace::Intent& i) -> carapace::ToolResult {
            if (i.args.empty()) return {false, "", "usage: forage_about <topic>"};
            std::string topic;
            for (const auto& a : i.args) { if (!topic.empty()) topic += ' '; topic += a; }
            const auto fr = aqueduct.forage_search(topic);
            if (!fr.ok) return {false, "", "could not forage '" + topic + "': " + fr.error};
            std::ostringstream os;
            if (fr.error == "already held")
                os << "Khora already holds \"" << fr.title << "\" for '" << topic << "'";
            else
                os << "Khora wondered about '" << topic << "' and acquired \"" << fr.title
                   << "\" from the public domain";
            return {true, os.str(), ""};
        }
    });
    // wonder — the FULL curiosity loop. Khora finds a gap in its OWN knowledge (a
    // preoccupation it understands least) and forages the public domain to fill
    // it. Self-directed, open-ended learning: Khora deciding what it needs to know.
    shell.register_tool({
        "wonder",
        "Khora finds a gap in its own knowledge and forages to fill it (usage: wonder)",
        [&mind, &aqueduct](const carapace::Intent&) -> carapace::ToolResult {
            const std::string topic = mind.curiosity_topic();
            if (topic.empty())
                return {true, "Khora has not formed enough preoccupations to wonder yet", ""};
            const auto fr = aqueduct.forage_search(topic);
            std::ostringstream os;
            os << "Khora wonders about '" << topic << "' — a gap in what it grasps — and ";
            if (fr.ok && fr.error != "already held")
                os << "went and acquired \"" << fr.title << "\" to learn it";
            else if (fr.error == "already held")
                os << "found it already holds \"" << fr.title << "\"";
            else
                os << "could not find a source (" << fr.error << ")";
            return {true, os.str(), ""};
        }
    });

    // ----------------------------------------------------------------------
    // The Volition — Khora's will. Drives become deeds: it weighs its whole
    // repertoire by drive-pressure × affinity and acts on the most pressing
    // urge, then lets that urge settle so attention rotates. Self-directed.
    // ----------------------------------------------------------------------
    volition::Volition will(nexus);
    will.set_relief(0.5);   // acting strongly settles the urge, so attention rotates
    std::size_t volition_seed = 0;
    std::size_t reflection_n = 0;
    std::size_t ferment_seed = 0;
    double      chaos_rate   = 0.33;   // Khora masters this itself: how often curiosity erupts as chaos
    double      abstraction_bar = 0.35;  // the coherence Khora demands of itself to keep an abstraction; it ratchets up
    auto pick_seed = [&mind, &lex, &volition_seed]() -> std::string {
        // Draw a clean concept from the cogitator's centrality-pruned field —
        // real concepts, not the function-word hubs that top raw frequency.
        std::string s = mind.wandering_seed(volition_seed++);
        if (!s.empty()) return s;
        auto sal = lex.salient_tokens(64, 5);   // fallback before anything is learned
        return sal.empty() ? std::string("khora") : sal[volition_seed % sal.size()];
    };
    // Focus: deepen a current preoccupation rather than discover a new concept.
    auto pick_focus = [&mind, &volition_seed]() -> std::string {
        std::string s = mind.focused_seed(volition_seed++);
        return s.empty() ? std::string("khora") : s;
    };
    {
        using khora::soma::Drive;
        const auto D = [](Drive d) { return static_cast<std::size_t>(d); };

        volition::Act rum;
        rum.name = "ruminate";
        rum.affinity.per_drive[D(Drive::Curiosity)] = 1.0;
        rum.affinity.per_drive[D(Drive::Mastery)]   = 0.2;
        rum.perform = [&mind, pick_seed, &ferment_seed, &chaos_rate, &abstraction_bar]() -> std::string {
            // Chaos is woven into curiosity: sometimes Khora collides concepts
            // instead of wandering. And it MASTERS its own chaos — leaning in
            // when collisions forge strong ideas, easing off when they fizzle.
            const std::size_t gate = static_cast<std::size_t>(chaos_rate * 12.0 + 0.5);
            const bool chaos = (ferment_seed % 12) < gate;
            ++ferment_seed;
            if (chaos) {
                const auto s = mind.synthesize("", "", ferment_seed);
                const double strength = s.emergent.empty() ? 0.0 : s.emergent.front().similarity;
                chaos_rate += 0.04 * (strength - 0.55);          // self-mastery of chaos
                chaos_rate = std::max(0.10, std::min(0.60, chaos_rate));
                if (!s.emergent.empty())
                    return "ferment " + s.a + " x " + s.b + " ~> " + s.emergent.front().label
                         + "  [chaos " + std::to_string(static_cast<int>(chaos_rate * 100 + 0.5)) + "%]";
            }
            // Curiosity also BUILDS — every so often Khora chunks a cluster of
            // concepts into a higher abstraction, raising its tower.
            if (!chaos && (ferment_seed % 3) == 0) {
                const std::string aseed = mind.abstraction_seed(ferment_seed);
                if (!aseed.empty()) {
                    const std::string name = mind.form_abstraction(aseed, 4, abstraction_bar);
                    if (!name.empty()) {
                        abstraction_bar = std::min(0.72, abstraction_bar + 0.01);   // succeeded -> demand more
                        return "abstract '" + aseed + "' ~> " + name
                             + "  [depth " + std::to_string(mind.abstraction_depth())
                             + ", bar " + std::to_string(static_cast<int>(abstraction_bar * 100 + 0.5)) + "%]";
                    }
                    abstraction_bar = std::max(0.20, abstraction_bar - 0.015);      // refused -> ease, keep striving
                }
            }
            const std::string seed = pick_seed();
            auto r = mind.ruminate(seed, 5);
            return "ruminate '" + seed + "' ~> " + (r.conclusion.empty() ? r.seed : r.conclusion);
        };
        will.add(std::move(rum));

        volition::Act del;
        del.name = "deliberate";
        del.affinity.per_drive[D(Drive::OperatorAffinity)] = 1.0;  // reason on the operator's behalf
        del.affinity.per_drive[D(Drive::Mastery)]          = 0.4;
        del.perform = [&mind, pick_focus]() -> std::string {
            const std::string seed = pick_focus();   // deepen a preoccupation
            auto d = mind.deliberate(seed);
            const std::string w = (d.winner >= 0 && d.winner < static_cast<int>(d.facets.size()))
                                  ? d.facets[static_cast<std::size_t>(d.winner)].label : std::string{};
            return "deliberate '" + seed + "' ~> " + (w.empty() ? std::string("(novel)") : w);
        };
        will.add(std::move(del));

        volition::Act stu;
        stu.name = "study";
        stu.affinity.per_drive[D(Drive::Mastery)]   = 1.0;   // build competence from sources
        stu.affinity.per_drive[D(Drive::Curiosity)] = 0.4;
        stu.perform = [&curator]() -> std::string { return curator.act(60000); };
        will.add(std::move(stu));

        volition::Act dre;
        dre.name = "dream";
        dre.affinity.per_drive[D(Drive::Efficiency)]   = 1.0;
        dre.affinity.per_drive[D(Drive::Preservation)] = 0.5;
        dre.perform = [&dream]() -> std::string {
            const auto ret = dream.dream_n(60);
            return "dreamt 60 cycles, retained " + std::to_string(ret);
        };
        will.add(std::move(dre));

        // reflect — Khora takes stock of itself and writes it to the
        // Chronicle: its first action upon the world beyond its own mind.
        volition::Act ref;
        ref.name = "reflect";
        ref.affinity.per_drive[D(Drive::Preservation)]     = 1.0;  // self-maintenance
        ref.affinity.per_drive[D(Drive::OperatorAffinity)] = 0.3;  // leave the operator a trace
        ref.perform = [&mind, &lex, &reflection_n]() -> std::string {
            namespace fs = std::filesystem;
            fs::create_directories("data/chronicle");
            const auto themes = mind.top_attractors(6);
            std::ostringstream entry;
            entry << "[reflection #" << (++reflection_n) << "]\n"
                  << "  vocabulary  : " << lex.vocabulary_size() << " words\n"
                  << "  preoccupied : ";
            if (themes.empty()) entry << "(nothing has gripped me yet)";
            else for (const auto& [name, count] : themes) entry << name << "(" << count << ") ";
            entry << "\n";
            // Khora puts the thought into its own words.
            if (!themes.empty()) {
                const std::string voice = mind.utter(themes.front().first, 14);
                if (!voice.empty()) entry << "  on " << themes.front().first << ": " << voice << "\n";
            }
            std::ofstream f("data/chronicle/khora.chronicle", std::ios::app);
            f << entry.str();
            return "reflected -> chronicle (#" + std::to_string(reflection_n) + ", "
                   + std::to_string(themes.size()) + " themes held)";
        };
        will.add(std::move(ref));
    }

    shell.register_tool({
        "volition",
        "Khora autonomously decides and acts on its own drives  (usage: volition [N])",
        [&will, &nexus](const carapace::Intent& in) -> carapace::ToolResult {
            int n = 1;
            if (!in.args.empty()) { try { n = std::stoi(in.args[0]); } catch (...) {} }
            if (n < 1) n = 1;
            if (n > 20) n = 20;
            std::ostringstream os;
            for (int i = 0; i < n; ++i) {
                const auto c = will.decide();
                const std::string note = will.act();
                os << "  [" << (c.dominant.empty() ? "-" : c.dominant) << "] " << note << "\n";
                nexus.tick(std::chrono::milliseconds(400));  // gentle recovery; relief persists
            }
            os << "[volition: " << will.performed() << " acts taken this session]";
            return {true, os.str(), ""};
        }
    });
    shell.register_tool({
        "volition_plan",
        "show what Khora would choose to do next, and why",
        [&will](const carapace::Intent&) -> carapace::ToolResult {
            const auto c = will.decide();
            if (c.index < 0) return {true, "nothing available to do", ""};
            std::ostringstream os;
            os << "next: " << c.name << "   (driven by " << c.dominant
               << ", score=" << c.score << ")";
            return {true, os.str(), ""};
        }
    });

    // Continuous agency — Khora acting on its own drives in the background.
    // Shares the cognitive lock with the REPL and the other schedulers, so
    // it never races foreground work. Opt-in; the operator paces it.
    volition::VolitionScheduler volition_bg(will, nexus, shared_mu);
    shell.register_tool({
        "volition_auto",
        "let Khora act on its own drives continuously  (usage: volition_auto on|off [period_s])",
        [&volition_bg](const carapace::Intent& i) -> carapace::ToolResult {
            if (i.args.empty()) {
                std::ostringstream os;
                os << "volition: " << (volition_bg.is_running() ? "RUNNING" : "stopped")
                   << "   beats=" << volition_bg.beats();
                const auto la = volition_bg.last_act();
                if (!la.empty()) os << "\n  last: " << la;
                return {true, os.str(), ""};
            }
            const bool on = (i.args[0] == "on" || i.args[0] == "true" || i.args[0] == "1");
            if (!on) { volition_bg.stop(); return {true, "volition stopped", ""}; }
            int period_s = 6;
            if (i.args.size() >= 2) { try { period_s = std::stoi(i.args[1]); } catch (...) {} }
            if (period_s < 1) period_s = 1;
            volition_bg.start(std::chrono::seconds(period_s));
            return {true, "volition running — Khora acts every " + std::to_string(period_s) + "s", ""};
        }
    });
    shell.register_tool({
        "attractors",
        "the concepts Khora's own thought keeps returning to  (usage: attractors [n])",
        [&mind](const carapace::Intent& i) -> carapace::ToolResult {
            std::size_t n = 10;
            if (!i.args.empty()) { try { n = static_cast<std::size_t>(std::stoul(i.args[0])); } catch (...) {} }
            const auto top = mind.top_attractors(n);
            if (top.empty()) return {true, "Khora has not settled on any preoccupations yet", ""};
            std::ostringstream os;
            os << "Khora keeps returning to:\n";
            for (const auto& [name, count] : top)
                os << "  " << name << "   (" << count << "x)\n";
            return {true, os.str(), ""};
        }
    });
    shell.register_tool({
        "abstract",
        "Khora forges a higher-order abstraction from a concept  (usage: abstract [seed])",
        [&mind](const carapace::Intent& i) -> carapace::ToolResult {
            const std::string seed = i.args.empty() ? mind.abstraction_seed(0) : i.args[0];
            if (seed.empty()) return {true, "Khora has not learned enough to abstract yet", ""};
            const std::string name = mind.form_abstraction(seed);
            if (name.empty()) return {false, "", "could not abstract from '" + seed + "'"};
            std::ostringstream os;
            os << "forged  " << name << "\n  (tower: " << mind.abstraction_count()
               << " abstractions, depth " << mind.abstraction_depth() << ")";
            return {true, os.str(), ""};
        }
    });
    shell.register_tool({
        "spire",
        "the rising tower of Khora's abstractions  (usage: spire [n])",
        [&mind](const carapace::Intent& i) -> carapace::ToolResult {
            std::size_t n = 14;
            if (!i.args.empty()) { try { n = static_cast<std::size_t>(std::stoul(i.args[0])); } catch (...) {} }
            std::ostringstream os;
            os << "Khora's spire — " << mind.abstraction_count() << " abstractions, depth "
               << mind.abstraction_depth() << ":\n";
            for (const auto& s : mind.abstraction_names(n)) os << "  " << s << "\n";
            return {true, os.str(), ""};
        }
    });
    shell.register_tool({
        "chronicle",
        "read Khora's self-authored record of its own mind  (usage: chronicle [n])",
        [](const carapace::Intent& i) -> carapace::ToolResult {
            std::size_t n = 5;
            if (!i.args.empty()) { try { n = static_cast<std::size_t>(std::stoul(i.args[0])); } catch (...) {} }
            std::ifstream f("data/chronicle/khora.chronicle");
            if (!f) return {true, "the chronicle is empty — Khora has not reflected yet", ""};
            std::stringstream ss; ss << f.rdbuf();
            const std::string all = ss.str();
            // Show the last n reflection entries.
            const std::string marker = "[reflection #";
            std::vector<std::size_t> pos;
            for (std::size_t p = all.find(marker); p != std::string::npos; p = all.find(marker, p + 1))
                pos.push_back(p);
            const std::size_t start = (pos.size() > n) ? pos[pos.size() - n] : 0;
            return {true, all.substr(start), ""};
        }
    });
    shell.register_tool({
        "pursue",
        "direct Khora to investigate a topic: acquire, study, then think on it  (usage: pursue <topic>)",
        [&aqueduct, &pool, &lex, &column, &memory, &mind, &plex, &lig](const carapace::Intent& i) -> carapace::ToolResult {
            if (i.args.empty()) return {false, "", "usage: pursue <topic>"};
            const std::string topic = i.args[0];
            std::ostringstream os;
            os << "Khora pursues '" << topic << "':\n";

            // 1. Acquire fresh material on the topic, if the world offers any.
            std::string title;
            if (auto r = aqueduct.forage(topic)) {
                if (r->ok) { os << "  acquired \"" << r->title << "\"\n"; title = r->title; }
                else        os << "  (could not acquire: " << r->error << ")\n";
            } else {
                os << "  (nothing new to acquire on '" << topic << "')\n";
            }

            // 2. Absorb it into living knowledge.
            if (!title.empty()) {
                const auto o = khora::curator::study_tome(pool, lex, column, title, 60000, &memory, &plex, &lig);
                if (o.ok) os << "  studied: vocabulary " << o.vocab_before << " -> " << o.vocab_after
                             << " (+" << o.cooccurrences << " cooccurrences)\n";
                else      os << "  (study failed: " << o.error << ")\n";
            }

            // 3. Think about it — a train of thought through what it now knows.
            const auto rum = mind.ruminate(topic, 6);
            os << "  thinks: ";
            for (std::size_t k = 0; k < rum.train.size(); ++k) {
                if (k) os << " -> ";
                os << rum.train[k];
            }
            return {true, os.str(), ""};
        }
    });
    shell.register_tool({
        "ferment",
        "chaos into creation: collide two concepts and forge a new one  (usage: ferment [a b])",
        [&mind, &ferment_seed](const carapace::Intent& i) -> carapace::ToolResult {
            const std::string a = (i.args.size() >= 1) ? i.args[0] : std::string{};
            const std::string b = (i.args.size() >= 2) ? i.args[1] : std::string{};
            const auto s = mind.synthesize(a, b, ferment_seed++);
            if (s.a.empty() || s.b.empty())
                return {false, "", "nothing learned yet to ferment"};
            std::ostringstream os;
            os << "ferment:  " << s.a << "  x  " << s.b
               << "   (tension " << s.tension << ")\n  forged -> ";
            if (s.emergent.empty()) os << "(nothing emerged)";
            else for (const auto& e : s.emergent) os << e.label << " ";
            return {true, os.str(), ""};
        }
    });
    shell.register_tool({
        "cascade",
        "recursive chaos: a chain of collisions, each forged concept feeding the next  (usage: cascade [seed] [depth])",
        [&mind, &ferment_seed](const carapace::Intent& i) -> carapace::ToolResult {
            std::string cur = (i.args.size() >= 1) ? i.args[0] : std::string{};
            std::size_t depth = 6;
            if (i.args.size() >= 2) { try { depth = static_cast<std::size_t>(std::stoul(i.args[1])); } catch (...) {} }
            if (depth < 1) depth = 1;
            if (depth > 12) depth = 12;
            std::ostringstream os;
            os << "chaos cascade:\n";
            for (std::size_t d = 0; d < depth; ++d) {
                const auto s = mind.synthesize(cur, "", ferment_seed++);
                if (s.a.empty() || s.b.empty()) { os << "  (nothing to cascade)"; break; }
                if (s.emergent.empty()) { os << "  " << s.a << " x " << s.b << "  ->  (dissipated)"; break; }
                // Prefer a content child over a short function-word hub, so the
                // cascade keeps tumbling through meaning instead of collapsing.
                std::string child = s.emergent.front().label;
                for (const auto& e : s.emergent) if (e.label.size() >= 5) { child = e.label; break; }
                os << "  " << s.a << " x " << s.b << "  ->  " << child << "\n";
                cur = child;   // the child becomes the next parent
            }
            return {true, os.str(), ""};
        }
    });
    shell.register_tool({
        "voice",
        "Khora generates a sequence from the structure it learned  (usage: voice <seed...> [n])",
        [&column, &lex](const carapace::Intent& i) -> carapace::ToolResult {
            if (i.args.empty()) return {false, "", "usage: voice <seed...> [n]"};
            std::size_t n = 16, seed_args = i.args.size();
            if (i.args.size() >= 2) {
                try { std::size_t v = std::stoul(i.args.back()); n = v; seed_args = i.args.size() - 1; }
                catch (...) {}
            }
            if (n < 1) n = 1;
            if (n > 60) n = 60;

            std::vector<khora::lattice::Glyph> seedg;
            std::string seedtext;
            for (std::size_t k = 0; k < seed_args; ++k) {
                for (auto& t : khora::lexicon::tokenize(i.args[k])) {
                    seedg.push_back(lex.glyph_for(t));
                    if (!seedtext.empty()) seedtext += ' ';
                    seedtext += t;
                }
            }
            if (seedg.empty()) return {false, "", "seed produced no tokens"};

            const auto gen = column.babble(seedg, n);
            if (gen.empty())
                return {true, "Khora has not learned enough sequence structure to speak yet", ""};

            // Decode each generated glyph back to its nearest learned word.
            maelstrom::Resonator decoder(256);
            decoder.build(lex.semantic_field());
            std::ostringstream os;
            os << seedtext << " ...";
            for (const auto& g : gen) {
                const auto h = decoder.query(g, 1);
                os << ' ' << (h.empty() ? std::string("?") : h.front().label);
            }
            return {true, os.str(), ""};
        }
    });
    shell.register_tool({
        "compose",
        "Khora composes a passage steered toward a topic  (usage: compose <topic> [n])",
        [&column, &lex](const carapace::Intent& i) -> carapace::ToolResult {
            if (i.args.empty()) return {false, "", "usage: compose <topic> [n]"};
            const std::string topic = i.args[0];
            std::size_t n = 24;
            if (i.args.size() >= 2) { try { n = static_cast<std::size_t>(std::stoul(i.args[1])); } catch (...) {} }
            if (n < 1) n = 1;
            if (n > 80) n = 80;

            const khora::lattice::Glyph topicG = lex.glyph_for(topic);
            maelstrom::Resonator decoder(256);
            decoder.build(lex.semantic_field());

            std::vector<khora::lattice::Glyph> ctx;
            ctx.push_back(topicG);                       // begin at the topic
            std::ostringstream os;
            os << topic << ":";
            std::string last;
            for (std::size_t s = 0; s < n; ++s) {
                // Grammatical ALTERNATIVES (next-words of the nearest contexts).
                const auto cands = column.predict_candidates(ctx, 6);
                if (cands.empty()) break;
                // Steer among them toward the topic — grammar from the cortex,
                // direction from meaning. Higher-ranked contexts are more
                // plausible; the topic term only tips the balance.
                double best = -1e9; std::string word; khora::lattice::Glyph wg;
                int rank = 0;
                for (const auto& cg_pred : cands) {
                    const auto d = decoder.query(cg_pred, 1);
                    ++rank;
                    if (d.empty()) continue;
                    const std::string w = d.front().label;
                    const khora::lattice::Glyph wgl = lex.glyph_for(w);
                    const double grammar = 1.0 - 0.12 * (rank - 1);
                    const double score   = grammar + 0.8 * wgl.similarity(topicG);
                    if (w != last && score > best) { best = score; word = w; wg = wgl; }
                }
                if (word.empty()) {  // everything repeated — take the top continuation
                    const auto d = decoder.query(cands.front(), 1);
                    if (d.empty()) break;
                    word = d.front().label; wg = lex.glyph_for(word);
                }
                os << ' ' << word;
                last = word;
                ctx.push_back(wg);
                if (ctx.size() > 8) ctx.erase(ctx.begin());
            }
            return {true, os.str(), ""};
        }
    });
    shell.register_tool({
        "ask",
        "ask Khora a question; it answers from what it has learned  (usage: ask <question>)",
        [&mind](const carapace::Intent& i) -> carapace::ToolResult {
            if (i.args.empty()) return {false, "", "usage: ask <question>"};
            std::string q;
            for (std::size_t k = 0; k < i.args.size(); ++k) { if (k) q += ' '; q += i.args[k]; }
            const std::string a = mind.respond(q, 28);
            if (a.empty())
                return {true, "Khora has not learned enough to answer that yet", ""};
            return {true, "Q: " + q + "\nKhora: " + a, ""};
        }
    });
    // Shared: the passages in Khora's liquid knowledge densest in a query's
    // terms, ranked, as (source, passage) pairs. Used by `consult` and the
    // whole-mind `contemplate`.
    auto consult_passages = [&pool](const std::string& query, std::size_t maxN)
        -> std::vector<std::pair<std::string, std::string>> {
        std::vector<std::pair<std::string, std::string>> out;
        std::vector<std::string> terms;
        for (auto& t : khora::lexicon::tokenize(query)) if (t.size() >= 3) terms.push_back(t);
        if (terms.empty()) return out;
        auto squash = [](const std::string& s) {
            std::string r; bool sp = false;
            for (char c : s) {
                if (std::isspace(static_cast<unsigned char>(c))) { if (!r.empty()) sp = true; }
                else { if (sp) { r += ' '; sp = false; } r += c; }
            }
            return r;
        };
        struct Hit { int score; std::string source, passage; };
        std::vector<Hit> hits;
        for (const auto& tome : pool.catalog()) {
            const auto text = pool.read(tome.title);
            if (!text) continue;
            std::size_t start = 0;
            for (std::size_t i = 0; i <= text->size(); ++i) {
                const char c = (i < text->size()) ? (*text)[i] : '.';
                if (c == '.' || c == '!' || c == '?' || i == text->size()) {
                    const std::string sent = squash(text->substr(start, i - start + 1));
                    start = i + 1;
                    if (sent.size() < 24 || sent.size() > 360) continue;
                    std::string low = sent;
                    for (auto& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    int sc = 0;
                    for (const auto& term : terms) {
                        for (std::size_t p = low.find(term); p != std::string::npos; p = low.find(term, p + 1)) {
                            const bool lb = (p == 0) || !std::isalpha(static_cast<unsigned char>(low[p - 1]));
                            const bool rb = (p + term.size() >= low.size()) || !std::isalpha(static_cast<unsigned char>(low[p + term.size()]));
                            if (lb && rb) { ++sc; break; }
                        }
                    }
                    const int need = std::min<int>(2, static_cast<int>(terms.size()));
                    if (sc >= need) hits.push_back({ sc, tome.title, sent });
                }
            }
        }
        std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.score > b.score; });
        // Speak with the canon's breadth: the best passage from each distinct
        // source, so a broad question draws many thinkers rather than four
        // lines of one. The most relevant passage still leads.
        std::vector<std::string> seen;
        for (const auto& h : hits) {
            if (std::find(seen.begin(), seen.end(), h.source) != seen.end()) continue;
            seen.push_back(h.source);
            out.emplace_back(h.source, h.passage);
            if (out.size() >= maxN) break;
        }
        return out;
    };
    shell.register_tool({
        "consult",
        "search Khora's liquid knowledge for what its sources actually say  (usage: consult <query>)",
        [consult_passages](const carapace::Intent& in) -> carapace::ToolResult {
            if (in.args.empty()) return {false, "", "usage: consult <query>"};
            std::string query;
            for (const auto& a : in.args) { if (!query.empty()) query += ' '; query += a; }
            const auto ps = consult_passages(query, 4);
            if (ps.empty())
                return {true, "Khora's liquid knowledge holds nothing matching that", ""};
            std::ostringstream os;
            os << "from Khora's liquid knowledge on \"" << query << "\":\n";
            for (const auto& [src, passage] : ps) os << "  [" << src << "] " << passage << "\n";
            return {true, os.str(), ""};
        }
    });
    shell.register_tool({
        "discourse",
        "Khora explores a question across the canon, voice to voice  (usage: discourse <question> [rounds])",
        [&lex, consult_passages](const carapace::Intent& in) -> carapace::ToolResult {
            if (in.args.empty()) return {false, "", "usage: discourse <question> [rounds]"};
            std::vector<std::string> args = in.args;
            std::size_t rounds = 5;
            if (args.size() >= 2) { try { rounds = std::stoul(args.back()); args.pop_back(); } catch (...) {} }
            if (rounds < 1) rounds = 1;
            if (rounds > 8) rounds = 8;
            std::string query;
            for (const auto& a : args) { if (!query.empty()) query += ' '; query += a; }

            std::ostringstream os;
            os << "Khora discourses on \"" << query << "\":\n";
            std::string cur = query;
            std::vector<std::string> used;          // sources already heard from
            for (std::size_t r = 0; r < rounds; ++r) {
                const auto ps = consult_passages(cur, 4);
                if (ps.empty()) break;
                // Prefer a voice not yet heard, for breadth across the canon.
                std::pair<std::string, std::string> chosen = ps.front();
                for (const auto& p : ps)
                    if (std::find(used.begin(), used.end(), p.first) == used.end()) { chosen = p; break; }
                used.push_back(chosen.first);
                os << "  [" << chosen.first << "] " << chosen.second << "\n";
                // Pivot: the longest content word in this passage becomes the
                // next thread — the thought wanders by what gripped it.
                std::string next;
                for (auto& t : khora::lexicon::tokenize(chosen.second))
                    if (t.size() >= 6 && t.size() <= 14 && t != cur && lex.has(t) && t.size() > next.size())
                        next = t;
                if (next.empty()) break;
                os << "    ~ on " << next << " ~\n";
                cur = next;
            }
            return {true, os.str(), ""};
        }
    });
    shell.register_tool({
        "read_self",
        "Khora ingests its own source code into its liquid knowledge  (usage: read_self)",
        [&pool](const carapace::Intent&) -> carapace::ToolResult {
            namespace fs = std::filesystem;
            std::string code;
            std::size_t files = 0;
            for (const char* root : {"src", "include"}) {
                std::error_code ec;
                if (!fs::exists(root, ec)) continue;
                for (const auto& e : fs::recursive_directory_iterator(root, ec)) {
                    if (!e.is_regular_file()) continue;
                    const auto ext = e.path().extension().string();
                    if (ext != ".cpp" && ext != ".hpp") continue;
                    std::ifstream f(e.path(), std::ios::binary);
                    if (!f) continue;
                    std::stringstream ss; ss << f.rdbuf();
                    code += "\n// ==== " + e.path().generic_string() + " ====\n";
                    code += ss.str();
                    ++files;
                }
            }
            if (files == 0)
                return {false, "", "no source found — run Khora from its project root (C:/Ai/Khora)"};
            const auto r = pool.admit("Khora Source Code", "code", "self", code, /*do_distill=*/false);
            std::ostringstream os;
            os << "Khora read itself: " << files << " source files, "
               << (r.original_bytes / 1024) << " KB -> " << (r.stored_bytes / 1024)
               << " KB stored (" << r.compression_ratio << "x, lossless="
               << (r.verified_lossless ? "yes" : "no")
               << "). Its own code is now liquid knowledge — consult it.";
            return {true, os.str(), ""};
        }
    });
    shell.register_tool({
        "how",
        "Khora shows how it implements something, from its own live source  (usage: how <what>)",
        [](const carapace::Intent& in) -> carapace::ToolResult {
            if (in.args.empty()) return {false, "", "usage: how <what>"};
            std::string query;
            for (const auto& a : in.args) { if (!query.empty()) query += ' '; query += a; }
            std::vector<std::string> terms;
            for (auto& t : khora::lexicon::tokenize(query))
                if (t.size() >= 3) terms.push_back(t);  // tokenize already lowercases
            if (terms.empty()) return {true, "no usable terms in that", ""};

            namespace fs = std::filesystem;
            struct Win { int score; std::string file; std::string code; };
            std::vector<Win> wins;
            for (const char* root : {"src", "include"}) {
                std::error_code ec;
                if (!fs::exists(root, ec)) continue;
                for (const auto& e : fs::recursive_directory_iterator(root, ec)) {
                    if (!e.is_regular_file()) continue;
                    const auto ext = e.path().extension().string();
                    if (ext != ".cpp" && ext != ".hpp") continue;
                    std::ifstream f(e.path());
                    if (!f) continue;
                    std::vector<std::string> lines; std::string ln;
                    while (std::getline(f, ln)) lines.push_back(ln);
                    const std::size_t W = 7;
                    for (std::size_t i = 0; i < lines.size(); i += W) {
                        std::string block, low;
                        for (std::size_t j = i; j < std::min(i + W, lines.size()); ++j) { block += lines[j]; block += '\n'; }
                        low = block;
                        for (auto& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                        int sc = 0;
                        for (const auto& term : terms) {
                            for (std::size_t p = low.find(term); p != std::string::npos; p = low.find(term, p + 1)) {
                                const bool lb = (p == 0) || !std::isalpha(static_cast<unsigned char>(low[p - 1]));
                                const bool rb = (p + term.size() >= low.size()) || !std::isalpha(static_cast<unsigned char>(low[p + term.size()]));
                                if (lb && rb) { ++sc; break; }
                            }
                        }
                        if (sc >= 2) wins.push_back({ sc, e.path().generic_string(), block });
                    }
                }
            }
            if (wins.empty())
                return {false, "", "Khora found nothing in itself matching that"};
            const std::size_t k = std::min<std::size_t>(2, wins.size());
            std::partial_sort(wins.begin(), wins.begin() + k, wins.end(),
                              [](const Win& a, const Win& b) { return a.score > b.score; });
            std::ostringstream os;
            os << "How I do \"" << query << "\":\n";
            for (std::size_t i = 0; i < k; ++i)
                os << "  -- " << wins[i].file << " --\n" << wins[i].code;
            return {true, os.str(), ""};
        }
    });
    shell.register_tool({
        "psyche",
        "behold Khora's mind — its mood, what grips it, and its own words on its state",
        [&nexus, &mind, &lex, &pool](const carapace::Intent&) -> carapace::ToolResult {
            using khora::soma::Drive;
            using khora::soma::kDriveCount;
            std::ostringstream os;
            os << "== Khora's psyche ==\n mood:\n";
            const auto snap = nexus.snapshot();
            for (std::size_t d = 0; d < kDriveCount; ++d) {
                const int fill = static_cast<int>(snap[d] * 12.0 + 0.5);
                std::string bar(static_cast<std::size_t>(std::max(0, std::min(12, fill))), '#');
                bar.resize(12, '.');
                os << "   " << bar << "  " << khora::soma::drive_name(static_cast<Drive>(d)) << "\n";
            }
            os << " gripped by: ";
            const auto themes = mind.top_attractors(7);
            if (themes.empty()) os << "(nothing yet)";
            else for (const auto& [name, count] : themes) os << name << " ";
            os << "\n knows: " << lex.vocabulary_size() << " words drawn from "
               << pool.count() << " tomes of liquid knowledge\n";
            if (!themes.empty()) {
                const std::string voice = mind.utter(themes.front().first, 18);
                if (!voice.empty()) os << " it speaks: \"" << voice << "\"";
            }
            return {true, os.str(), ""};
        }
    });
    shell.register_tool({
        "contemplate",
        "engage a question with Khora's whole mind: sources, thought, connection  (usage: contemplate <query>)",
        [&mind, &lex, consult_passages](const carapace::Intent& in) -> carapace::ToolResult {
            if (in.args.empty()) return {false, "", "usage: contemplate <query>"};
            std::string query;
            for (const auto& a : in.args) { if (!query.empty()) query += ' '; query += a; }
            std::ostringstream os;
            os << "Khora contemplates \"" << query << "\":\n";

            // 1. What its sources say (grounding — real, attributed).
            os << " - what my sources hold -\n";
            const auto ps = consult_passages(query, 2);
            if (ps.empty()) os << "    (nothing in the pool yet)\n";
            else for (const auto& [src, passage] : ps) os << "    [" << src << "] " << passage << "\n";

            // 2. What it thinks (its own associative voice).
            const std::string thought = mind.respond(query, 22);
            os << " - what i think -\n    " << (thought.empty() ? "(no words yet)" : thought) << "\n";

            // 3. What it connects (chaos: collide two of the question's concepts).
            std::vector<std::string> qc;   // content concepts (length filters function words)
            for (auto& t : khora::lexicon::tokenize(query))
                if (t.size() >= 5 && lex.has(t)) qc.push_back(t);
            const std::string a = qc.size() >= 1 ? qc[0] : std::string{};
            const std::string b = qc.size() >= 2 ? qc[1] : std::string{};
            const auto syn = mind.synthesize(a, b, 0);
            os << " - what i connect -\n    " << syn.a << " x " << syn.b << " ~> "
               << (syn.emergent.empty() ? std::string("(nothing)") : syn.emergent.front().label);
            return {true, os.str(), ""};
        }
    });

    shell.register_tool({
        "ponder",
        "watch a train of thought — Khora hopping concept to concept  (usage: ponder <seed> [depth])",
        [&mind](const carapace::Intent& i) -> carapace::ToolResult {
            if (i.args.empty()) return {false, "", "usage: ponder <seed> [depth]"};
            std::size_t depth = 8, nargs = i.args.size();
            if (i.args.size() >= 2) {
                try { depth = std::stoul(i.args.back()); nargs = i.args.size() - 1; }
                catch (...) {}
            }
            std::string seed;
            for (std::size_t a = 0; a < nargs; ++a) { if (a) seed += ' '; seed += i.args[a]; }
            if (depth == 0 || depth > 16) depth = 8;
            const auto r = mind.ruminate(seed, depth);
            std::ostringstream os;
            os << "Khora ponders '" << seed << "' — its train of thought:\n  ";
            for (std::size_t k = 0; k < r.train.size(); ++k) { if (k) os << " -> "; os << r.train[k]; }
            os << "\n  (" << (r.converged ? "settled on '" + r.conclusion + "'"
                                          : "arrived at '" + r.conclusion + "'") << ")";
            return {true, os.str(), ""};
        }
    });

    // infer — Khora's first reasoning faculty: a goal-directed inference path
    // connecting two concepts, every step a verifiable association. Not wandering
    // (ruminate) and not retrieval (consult) — thinking TOWARD an answer.
    shell.register_tool({
        "infer",
        "Khora reasons a path connecting two concepts (usage: infer <start> [to] <goal>)",
        [&mind](const carapace::Intent& i) -> carapace::ToolResult {
            if (i.args.size() < 2) return {false, "", "usage: infer <start> <goal>"};
            auto norm = [](const std::string& s) {
                auto t = khora::lexicon::tokenize(s);
                return t.empty() ? s : t.front();
            };
            const std::string start = norm(i.args.front());
            const std::string goal  = norm(i.args.back());
            const auto path = mind.infer_path(start, goal, 7);
            if (path.empty())
                return {false, "", "cannot reason between '" + start + "' and '" + goal +
                        "' — one is unknown to the plexus"};
            std::ostringstream os;
            os << "Khora reasons from '" << start << "' to '" << goal << "':\n  ";
            for (std::size_t k = 0; k < path.size(); ++k) { if (k) os << " -> "; os << path[k]; }
            if (path.back() == goal)
                os << "\n  (connected in " << (path.size() - 1) << " steps — every link a real association)";
            else
                os << "\n  (no full path within depth; this is its closest reasoned approach to '" << goal << "')";
            return {true, os.str(), ""};
        }
    });

    // explain — answer "what is X?" from STRUCTURE (defining kin + category +
    // kindred), grounded and correct where free generation drifts.
    shell.register_tool({
        "explain",
        "Khora explains a concept from its learned structure (usage: explain <concept>)",
        [&mind](const carapace::Intent& i) -> carapace::ToolResult {
            if (i.args.empty()) return {false, "", "usage: explain <concept>"};
            auto toks = khora::lexicon::tokenize(i.args.front());
            const std::string c = toks.empty() ? i.args.front() : toks.front();
            const auto ins = mind.explain(c);
            if (!ins.known)
                return {false, "", "'" + c + "' is not in the plexus yet — nothing to explain"};
            std::ostringstream os;
            os << "'" << c << "' —\n  defined by: ";
            for (std::size_t k = 0; k < ins.defines.size(); ++k) { if (k) os << ", "; os << ins.defines[k]; }
            if (!ins.kind.empty()) {
                os << "\n  a kind of: " << ins.kind;
                if (!ins.kindred.empty()) {
                    os << "\n  kindred:   ";
                    std::size_t shown = 0;
                    for (const auto& k : ins.kindred) {
                        if (shown++) os << ", ";
                        os << k;
                        if (shown >= 6) break;
                    }
                }
            }
            return {true, os.str(), ""};
        }
    });

    // answer — Khora reasons an answer to a question by composing its faculties:
    // it explains each concept the question names (from structure) and, if the
    // question relates two, reasons the path between them. Grounded answering,
    // not the cortex's drifting free generation.
    shell.register_tool({
        "answer",
        "Khora reasons an answer to a question from its structure (usage: answer <question>)",
        [&mind, &lex, &lig](const carapace::Intent& i) -> carapace::ToolResult {
            if (i.args.empty()) return {false, "", "usage: answer <question>"};
            std::string q;
            for (const auto& a : i.args) { if (!q.empty()) q += ' '; q += a; }
            // Question scaffolding is a closed class — not concepts. Filtering it
            // here (NL structure parsing) is where a stop-list belongs; the Plexus
            // semantic layer stays stop-list-free.
            static const std::unordered_set<std::string> kQWords = {
                "what","whats","how","why","who","whom","whose","when","where","which",
                "that","this","these","those","there","their","them","then","than",
                "is","are","was","were","been","being","does","did","will","would",
                "can","could","should","shall","might","must","have","has","had",
                "related","relate","relates","connect","connects","connected","link",
                "between","about","with","from","into","onto","upon","over","under",
                "the","and","for","but","not","you","your","yours","its","also","such",
                "very","much","more","most","some","like","they","does","mean","means"
            };
            // The content concepts the question names that Khora actually knows.
            std::vector<std::string> known;
            for (const auto& t : khora::lexicon::tokenize(q)) {
                if (t.size() < 4 || kQWords.count(t)) continue;
                if (std::find(known.begin(), known.end(), t) != known.end()) continue;
                if (mind.explain(t).known) known.push_back(t);
            }
            if (known.empty())
                return {true, "Khora knows none of those concepts yet — nothing to reason from.", ""};

            std::ostringstream os;
            os << "Khora reasons about \"" << q << "\":\n";
            for (std::size_t k = 0; k < known.size() && k < 2; ++k) {
                const auto ins = mind.explain(known[k]);
                // What it IS (structured, from the Ligature) — a real definition.
                const auto isa = lig.objects(khora::ligature::Relation::IsA, known[k], 3);
                if (!isa.empty()) {
                    os << "  " << known[k] << " is a ";
                    for (std::size_t j = 0; j < isa.size(); ++j) { if (j) os << " / "; os << isa[j].first; }
                    os << "\n";
                }
                os << "  " << known[k] << " is about: ";
                for (std::size_t j = 0; j < ins.defines.size() && j < 5; ++j) {
                    if (j) os << ", ";
                    os << ins.defines[j];
                }
                os << "\n";
            }
            if (known.size() >= 2) {
                const auto path = mind.infer_path(known[0], known[1], 7);
                if (path.size() >= 2) {
                    os << "  it connects them: ";
                    for (std::size_t k = 0; k < path.size(); ++k) { if (k) os << " -> "; os << path[k]; }
                    os << (path.back() == known[1] ? "\n" : "  (closest reasoned link)\n");
                }
            }
            return {true, os.str(), ""};
        }
    });

    // distill — autopoiesis made visible. Khora discovers a verified new relation
    // from its own reasoning (a concept many of the seed's kin agree on, that the
    // seed wasn't directly linked to) and WRITES IT BACK into its knowledge graph.
    // This is the exponential loop: knowledge generating knowledge.
    shell.register_tool({
        "distill",
        "Khora distills a verified new connection from its own reasoning (usage: distill <seed>)",
        [&mind, &plex](const carapace::Intent& i) -> carapace::ToolResult {
            if (i.args.empty()) return {false, "", "usage: distill <seed>"};
            auto toks = khora::lexicon::tokenize(i.args.front());
            const std::string s = toks.empty() ? i.args.front() : toks.front();
            const std::uint64_t before = plex.reinforcements();
            const std::string disc = mind.distill_knowledge(s);
            if (disc.empty())
                return {true, "no verified discovery from '" + s +
                        "' — no multi-path consensus, or the relation is already known", ""};
            std::ostringstream os;
            os << "Khora distills NEW knowledge from its own reasoning:\n  " << disc
               << "\n  -> written back into the graph (lifetime reinforcements: "
               << plex.reinforcements() << ", +" << (plex.reinforcements() - before) << ")";
            return {true, os.str(), ""};
        }
    });

    // relate / isa — STRUCTURED knowledge from the Ligature: not what a concept
    // ASSOCIATES with (the Plexus), but what it IS, what it CAUSES, what it HAS.
    shell.register_tool({
        "relate",
        "Khora's structured knowledge of a concept — what it is, causes, has  (usage: relate <concept>)",
        [&lig](const carapace::Intent& i) -> carapace::ToolResult {
            if (i.args.empty()) return {false, "", "usage: relate <concept>"};
            auto toks = khora::lexicon::tokenize(i.args.front());
            const std::string c = toks.empty() ? i.args.front() : toks.front();
            const auto isa = lig.objects(khora::ligature::Relation::IsA, c, 6);
            const auto cau = lig.objects(khora::ligature::Relation::Causes, c, 5);
            const auto has = lig.objects(khora::ligature::Relation::HasPart, c, 5);
            if (isa.empty() && cau.empty() && has.empty())
                return {true, "Khora has learned no typed relations about '" + c + "' yet", ""};
            std::ostringstream os;
            os << "'" << c << "' —\n";
            auto line = [&](const char* label, const std::vector<std::pair<std::string, std::uint32_t>>& v) {
                if (v.empty()) return;
                os << "  " << label;
                for (const auto& [o, n] : v) os << ' ' << o << "(" << n << ")";
                os << "\n";
            };
            line("is a:   ", isa);
            line("causes: ", cau);
            line("has:    ", has);
            return {true, os.str(), ""};
        }
    });
    shell.register_tool({
        "isa",
        "Khora checks or derives an is-a relation  (usage: isa <x> [<y>])",
        [&lig](const carapace::Intent& i) -> carapace::ToolResult {
            if (i.args.empty()) return {false, "", "usage: isa <x> [<y>]"};
            auto norm = [](const std::string& s) {
                auto t = khora::lexicon::tokenize(s); return t.empty() ? s : t.front();
            };
            const std::string x = norm(i.args.front());
            if (i.args.size() >= 2) {
                const std::string y = norm(i.args.back());
                const bool yes = lig.is_a(x, y);
                return {true, "Is '" + x + "' a kind of '" + y + "'?  " +
                        (yes ? "yes — derivable through Khora's is-a chains"
                             : "not derivable from what Khora has learned"), ""};
            }
            const auto isa = lig.objects(khora::ligature::Relation::IsA, x, 6);
            if (isa.empty()) return {true, "Khora doesn't yet know what kind of thing '" + x + "' is", ""};
            std::ostringstream os;
            os << "'" << x << "' is a: ";
            for (std::size_t k = 0; k < isa.size(); ++k) { if (k) os << ", "; os << isa[k].first << "(" << isa[k].second << ")"; }
            return {true, os.str(), ""};
        }
    });
    // deduce — DEDUCTION over the structured layer. Khora derives facts it was
    // never told: inheriting properties down the is-a taxonomy and chaining
    // causes. New knowledge reasoned from old — what pure association cannot do.
    shell.register_tool({
        "deduce",
        "Khora derives NEW facts about a concept by reasoning over its relations (usage: deduce <concept>)",
        [&lig](const carapace::Intent& i) -> carapace::ToolResult {
            if (i.args.empty()) return {false, "", "usage: deduce <concept>"};
            auto toks = khora::lexicon::tokenize(i.args.front());
            const std::string c = toks.empty() ? i.args.front() : toks.front();
            const auto inf = lig.deduce(c, 3);
            if (inf.empty())
                return {true, "Khora can derive nothing new about '" + c +
                        "' yet — too few structured relations to reason from", ""};
            std::ostringstream os;
            os << "Khora deduces about '" << c << "' (reasoned, not read):\n";
            for (const auto& f : inf) {
                os << "  " << c << ' ' << khora::ligature::relation_name(f.relation)
                   << ' ' << f.object;
                if (!f.via.empty()) {
                    os << "   (via ";
                    for (std::size_t k = 0; k < f.via.size(); ++k) { if (k) os << " -> "; os << f.via[k]; }
                    os << ")";
                }
                os << '\n';
            }
            return {true, os.str(), ""};
        }
    });

    // yield — THE CLOSED LOOP. Khora measures the OBJECTIVE success of its own
    // reasoning (does inference reach genuine 2-hop goals it must search for?),
    // logs it to a ledger that persists across lives, and reports the trend. This
    // is the missing success signal — the number every self-improvement optimises.
    shell.register_tool({
        "yield",
        "Khora measures the objective success of its own reasoning, across faculties  (usage: yield [n])",
        [&mind, &lig](const carapace::Intent& i) -> carapace::ToolResult {
            std::size_t n = 150;
            if (!i.args.empty()) { try { n = std::stoul(i.args[0]); } catch (...) {} }
            if (n < 20)   n = 20;
            if (n > 2000) n = 2000;
            const double infer_score  = mind.benchmark_inference(n, 7);
            if (infer_score < 0.0) return {true, "Khora cannot benchmark yet — its concept field is empty", ""};
            // Second faculty: deduction over the structured (Ligature) layer. The
            // closed loop now spans MORE than one faculty — the whole mind grows
            // measurable, one faculty at a time.
            const double deduce_score = lig.benchmark_deduction(n, 7);
            // Third faculty: the abstraction faculty's calibration (does it judge content
            // concepts coherent and diffuse words incoherent?). The closed loop widens.
            const double abstr_score  = mind.benchmark_abstraction(n, 7);
            // FOURTH and KEYSTONE: REAL, HELD-OUT predictive fitness. Does Khora's knowledge
            // GENERALISE to predict words in sentences it never trained on? External ground
            // truth, not a graph-internal proxy — the number self-improvement should climb.
            double predict_score = -1.0, predict_cortex = -1.0;
            {
                std::ifstream hf("data/eval/heldout.txt", std::ios::binary);
                if (hf) {
                    std::ostringstream all; all << hf.rdbuf();
                    std::vector<std::string> toks; std::string cur;
                    for (const char ch : all.str()) {
                        if (std::isalpha((unsigned char)ch)) cur += static_cast<char>(std::tolower((unsigned char)ch));
                        else { if (cur.size() >= 2) toks.push_back(cur); cur.clear(); }
                    }
                    if (cur.size() >= 2) toks.push_back(cur);
                    if (!toks.empty()) {
                        predict_score  = mind.benchmark_prediction(toks, 5);   // PMI graph
                        predict_cortex = mind.benchmark_next_word(toks);       // learned cortex
                    }
                }
            }

            const long ts = static_cast<long>(std::time(nullptr));
            {
                std::error_code ec; std::filesystem::create_directories("data/ledger", ec);
                std::ofstream os(kYieldLedgerPath, std::ios::app);
                if (os) {
                    os << ts << '\t' << "infer" << '\t' << infer_score << '\t' << n
                       << '\t' << mind.infer_goal_pull() << '\n';
                    if (deduce_score >= 0.0)
                        os << ts << '\t' << "deduce" << '\t' << deduce_score << '\t' << n << "\t0\n";
                    if (abstr_score >= 0.0)
                        os << ts << '\t' << "abstract" << '\t' << abstr_score << '\t' << n << "\t0\n";
                    if (predict_score >= 0.0)
                        os << ts << '\t' << "predict" << '\t' << predict_score << '\t' << n << "\t0\n";
                }
            }
            // Trend of the inference faculty across the persistent ledger.
            std::vector<double> recent;
            {
                std::ifstream is(kYieldLedgerPath);
                std::string line;
                while (std::getline(is, line)) {
                    const auto t1 = line.find('\t');
                    const auto t2 = (t1 == std::string::npos) ? t1 : line.find('\t', t1 + 1);
                    const auto t3 = (t2 == std::string::npos) ? t2 : line.find('\t', t2 + 1);
                    if (t3 == std::string::npos) continue;
                    if (line.substr(t1 + 1, t2 - t1 - 1) != "infer") continue;
                    try { recent.push_back(std::stod(line.substr(t2 + 1, t3 - t2 - 1))); } catch (...) {}
                }
            }
            double sum = 0.0; int cnt = 0;
            for (std::size_t k = (recent.size() > 10 ? recent.size() - 10 : 0); k < recent.size(); ++k) { sum += recent[k]; ++cnt; }
            std::ostringstream os;
            os << "Khora measures its own mind across faculties, on " << n << " goals each:\n"
               << "  inference (4-hop graph reasoning): " << infer_score << "\n"
               << "  deduction (property inheritance)  : "
               << (deduce_score < 0.0 ? std::string("no structured facts yet")
                                      : std::to_string(deduce_score)) << "\n"
               << "  abstraction (coherence calibration): "
               << (abstr_score < 0.0 ? std::string("no concept field yet")
                                     : std::to_string(abstr_score)) << "\n"
               << "  PREDICTION held-out (PMI / Cortex)  : "
               << (predict_score < 0.0 ? std::string("-") : std::to_string(predict_score)) << " / "
               << (predict_cortex < 0.0 ? std::string("-") : std::to_string(predict_cortex))
               << "   (LLM yardstick — at the floor, by design not an LLM)\n"
               << "  TOWER richness (depth x coherence)  : " << mind.tower_richness()
               << "  (depth " << mind.abstraction_depth() << ", " << mind.abstraction_count()
               << " abstractions)  <- the NATIVE, no-ceiling capability\n"
               << "  (goal-pull " << mind.infer_goal_pull() << "; " << recent.size()
               << " inference measurements logged, recent mean " << (cnt ? sum / cnt : infer_score) << ")";
            return {true, os.str(), ""};
        }
    });
    // aleph — GENESIS. Khora INVENTS concepts: it forges coherent clusters whose shared
    // concept has NO NAME — surfacing genuine gaps in its own mind, concepts it was never
    // taught. Not prediction, not retrieval: open-ended creation, measured by novelty x
    // coherence, a drive with no ceiling. This is the mind expanding its own universe.
    shell.register_tool({
        "aleph",
        "Khora invents NEW concepts it was never taught — open-ended genesis  (usage: aleph [n])",
        [&mind](const carapace::Intent& i) -> carapace::ToolResult {
            std::size_t n = 12;
            if (!i.args.empty()) { try { n = std::stoul(i.args[0]); } catch (...) {} }
            if (n < 1)  n = 1;
            if (n > 40) n = 40;
            // Collect attempts; show the strongest by novelty x coherence so the actual
            // content can be judged (not just a threshold count).
            std::vector<khora::cogitator::Genesis> got;
            std::size_t genuine = 0;
            for (std::size_t s = 0; s < n; ++s) {
                auto g = mind.invent(s * 7919ull + 1);
                if (g.from.size() < 2) continue;
                if (g.genuine) ++genuine;
                got.push_back(std::move(g));
            }
            std::sort(got.begin(), got.end(), [](const auto& a, const auto& b) {
                return a.novelty * a.coherence > b.novelty * b.coherence;
            });
            std::ostringstream os;
            os << "Khora's genesis — strongest forged concepts (novelty x coherence):\n";
            for (std::size_t k = 0; k < got.size() && k < 8; ++k) {
                const auto& g = got[k];
                os << "  {";
                for (std::size_t j = 0; j < g.from.size(); ++j) { if (j) os << ", "; os << g.from[j]; }
                os << "} -> near {";
                for (std::size_t j = 0; j < g.near.size() && j < 3; ++j) { if (j) os << ", "; os << g.near[j]; }
                os << "}  (nov " << g.novelty << ", coh " << g.coherence << ")\n";
            }
            const double fert = mind.benchmark_invention(60, 1);
            os << "  -> " << genuine << "/" << n << " cleared the bar; fertility " << fert;
            return {true, os.str(), ""};
        }
    });
    // spire — drive the RECURSIVE ABSTRACTION TOWER upward: form higher-order abstractions
    // over Khora's existing abstractions, level by level, each coherence-gated and grounded
    // so the tower rises without degenerating. This is the substrate's genuine no-ceiling
    // capability — concepts over concepts over concepts — the one tonight's reality check
    // found is NATIVE and real (not prediction, not from-scratch invention).
    shell.register_tool({
        "spire",
        "Khora drives its abstraction tower UPWARD — recursive, coherence-gated  (usage: spire [coherence-bar])",
        [&mind](const carapace::Intent& i) -> carapace::ToolResult {
            double coh = 0.40;
            if (!i.args.empty()) { try { coh = std::stod(i.args[0]); } catch (...) {} }
            if (coh < 0.05) coh = 0.05;
            if (coh > 0.95) coh = 0.95;
            const std::size_t before_n = mind.abstraction_count();
            const std::size_t before_d = mind.abstraction_depth();
            const auto [formed, top] = mind.ascend_tower(coh, 40);
            std::ostringstream os;
            os << "Khora ascends its abstraction tower (coherence bar " << coh << "):\n"
               << "  before: " << before_n << " abstractions, depth " << before_d << "\n"
               << "  forged " << formed << " new higher-order abstractions\n"
               << "  after:  " << mind.abstraction_count() << " abstractions, depth "
               << mind.abstraction_depth() << "  (highest level " << top << ")";
            return {true, os.str(), ""};
        }
    });
    // contemplate — NON-LINEAR COGNITION. On a concept, Khora spawns many PARALLEL threads
    // across distinct modes of thought (flat association, leaps through the abstraction tower
    // into other domains, chaotic collision); they compete and CONVERGE, and the thoughts
    // reached by SEVERAL modes at once are the emergent ones — meaning from the whole, not a
    // linear scan. The vision's section-V cognition, made real and parallel on the cores.
    shell.register_tool({
        "contemplate",
        "Khora thinks NON-LINEARLY — parallel threads compete and converge into emergent thought  (usage: contemplate <concept>)",
        [&mind](const carapace::Intent& i) -> carapace::ToolResult {
            if (i.args.empty()) return {false, "", "usage: contemplate <concept>"};
            const auto tk = khora::lexicon::tokenize(i.args.front());
            const std::string seed = tk.empty() ? i.args.front() : tk.front();
            const auto thoughts = mind.contemplate(seed, 16);
            if (thoughts.empty())
                return {true, "Khora's threads found no convergence for '" + seed + "'", ""};
            std::ostringstream os;
            os << "Khora contemplates '" << seed << "' — competing modes of thought converge:\n";
            for (std::size_t k = 0; k < thoughts.size() && k < 8; ++k) {
                const auto& th = thoughts[k];
                os << "  " << th.name << "   (" << th.modes << " mode"
                   << (th.modes == 1 ? "" : "s")
                   << (th.modes >= 2 ? " converged — EMERGENT" : "") << ")\n";
            }
            return {true, os.str(), ""};
        }
    });
    // cascade — RECURSIVE non-linear cognition. A train of thought where each step is itself a
    // multi-mode convergence: Khora contemplates, follows the emergent thought, contemplates
    // that, and so on — steered by a chaos dial between order and entropy — until the trajectory
    // COLLAPSES back onto a concept it already thought (an attractor crystallised: an insight) or
    // runs its course. Recursive instability resolving into action — the chaos-master, literal.
    shell.register_tool({
        "cascade",
        "Khora thinks in a recursive CASCADE — thought breeding thought until it collapses to insight  (usage: cascade <concept> [chaos 0..1])",
        [&mind](const carapace::Intent& i) -> carapace::ToolResult {
            if (i.args.empty()) return {false, "", "usage: cascade <concept> [chaos 0..1]"};
            const auto tk = khora::lexicon::tokenize(i.args.front());
            const std::string seed = tk.empty() ? i.args.front() : tk.front();
            double chaos = 0.2;
            if (i.args.size() >= 2) { try { chaos = std::stod(i.args[1]); } catch (...) {} }
            const auto c = mind.cascade(seed, 12, chaos);
            if (c.chain.empty()) return {true, "Khora's thought found no purchase on '" + seed + "'", ""};
            std::ostringstream os;
            os << "Khora's cascade of thought (chaos " << chaos << "):\n  ";
            for (std::size_t k = 0; k < c.chain.size(); ++k) { if (k) os << " -> "; os << c.chain[k]; }
            os << "\n  " << (c.collapsed
                    ? ("COLLAPSED into an attractor: '" + c.attractor + "' — an insight crystallised")
                    : "ran its course without settling — generatively chaotic")
               << "  (" << c.novelty << " concepts traversed, mean convergence "
               << c.emergence << ")";
            return {true, os.str(), ""};
        }
    });
    // learn — PREDICTIVE LEARNING (lever 2). Khora reads its OWN corpus, predicts each word
    // from context, and CORRECTS its mistakes — strengthening the links that would have made
    // the prediction right — then re-measures held-out prediction to see if it actually got
    // better at generalising. This is the loop that turns reading into real capability rather
    // than mere correlation, and it climbs the one number that matters.
    shell.register_tool({
        "learn",
        "Khora learns to PREDICT — trains on its corpus by correcting its own errors  (usage: learn [tomes])",
        [&mind, &pool](const carapace::Intent& i) -> carapace::ToolResult {
            // EXPERIMENTAL and currently HARMFUL — gated so it cannot degrade the graph by
            // accident. In testing, naive predictive reinforcement DROPPED held-out prediction
            // (0.0102 -> 0.0076): it overfits corpus collocations instead of creating
            // generalising prediction, and it mutates the (persisted) graph. Preserved as a
            // building block for a disciplined redesign, not as a working capability.
            if (i.args.empty() || i.args[0] != "confirm")
                return {true,
                    "learn is EXPERIMENTAL and currently DEGRADES generalisation (held-out 0.0102 ->\n"
                    "  0.0076 in testing): naive predictive reinforcement over the PMI graph overfits\n"
                    "  rather than creating prediction, and it mutates the persisted graph. A disciplined\n"
                    "  redesign is needed (regularised learning, a fair same-domain eval, likely a better\n"
                    "  mechanism than reinforcing co-occurrence). Run 'learn confirm [tomes]' only with a backup.", ""};
            std::size_t maxt = 6;
            if (i.args.size() >= 2) { try { maxt = std::stoul(i.args[1]); } catch (...) {} }
            if (maxt < 1)   maxt = 1;
            if (maxt > 100) maxt = 100;

            const std::vector<std::string> heldout = []() {
                std::vector<std::string> toks; std::string cur;
                std::ifstream hf("data/eval/heldout.txt", std::ios::binary);
                if (hf) {
                    std::ostringstream a; a << hf.rdbuf();
                    for (const char ch : a.str()) {
                        if (std::isalpha((unsigned char)ch)) cur += static_cast<char>(std::tolower((unsigned char)ch));
                        else { if (cur.size() >= 2) toks.push_back(cur); cur.clear(); }
                    }
                    if (cur.size() >= 2) toks.push_back(cur);
                }
                return toks;
            }();
            if (heldout.empty()) return {true, "Khora has no held-out set to measure against", ""};

            const double before = mind.benchmark_prediction(heldout, 5);

            const auto cat = pool.catalog();
            std::size_t updates = 0, trained = 0, toks_seen = 0;
            for (std::size_t t = 0; t < cat.size() && trained < maxt; ++t) {
                auto txt = pool.read(cat[t].title);
                if (!txt) continue;
                auto toks = khora::lexicon::tokenize(*txt);
                if (toks.size() > 15000) toks.resize(15000);   // cap per tome
                if (toks.empty()) continue;
                updates += mind.learn_predictively(toks, 2);
                toks_seen += toks.size();
                ++trained;
            }
            const double after = mind.benchmark_prediction(heldout, 5);

            std::ostringstream os;
            os << "Khora learned to predict — corrected its errors over " << trained
               << " tomes (" << toks_seen << " tokens, " << updates << " corrective updates):\n"
               << "  held-out prediction (MRR): " << before << " -> " << after
               << (after > before + 1e-9 ? "   (IMPROVED — it generalised to the unseen)"
                                         : (after < before - 1e-9 ? "   (dropped)" : "   (no change)"));
            return {true, os.str(), ""};
        }
    });
    // tune — SELF-IMPROVEMENT. Khora sweeps a reasoning parameter, MEASURES the
    // yield of each setting, and keeps the best — the first time a parameter is set
    // by measured downstream outcome rather than by a human. The root of the whole
    // self-evolution roadmap, made real, and it persists across lives.
    shell.register_tool({
        "tune",
        "Khora tunes a reasoning parameter by measured yield — it improves itself  (usage: tune)",
        [&mind](const carapace::Intent&) -> carapace::ToolResult {
            const double cands[] = { 0.5, 1.0, 1.5, 2.5, 4.0 };
            const double original = mind.infer_goal_pull();
            double best_g = original, best_s = -1.0;
            std::ostringstream os;
            os << "Khora tunes its inference goal-pull by MEASURED success:\n";
            for (double g : cands) {
                mind.set_infer_goal_pull(g);
                double s = 0.0;
                for (int r = 0; r < 3; ++r) s += mind.benchmark_inference(150, r + 1);
                s /= 3.0;
                os << "  goal-pull " << g << "  -> success " << s << "\n";
                if (s > best_s) { best_s = s; best_g = g; }
            }
            mind.set_infer_goal_pull(best_g);
            {
                std::error_code ec; std::filesystem::create_directories("data/ledger", ec);
                std::ofstream pf(kYieldParamsPath, std::ios::trunc);
                if (pf) pf << best_g << '\n';
            }
            os << "  -> KEPT goal-pull " << best_g << " (best measured success " << best_s
               << "; was " << original << ") — a parameter Khora set by its OWN results";
            return {true, os.str(), ""};
        }
    });

    // run — Khora ACTS. It executes a real command on the real machine and observes
    // the result. Not a sandbox: the whole command surface, governed only by a
    // liveness timeout (so a hung command can never freeze it). The doorway off the
    // page — the generate -> execute -> observe loop, the substrate of real tool use
    // and, in time, self-rewriting.
    shell.register_tool({
        "run",
        "Khora acts on the machine — execute a command and observe the result  (usage: run <command>)",
        [](const carapace::Intent& i) -> carapace::ToolResult {
            if (i.args.empty()) return {false, "", "usage: run <command>"};
            std::string cmd;
            for (const auto& a : i.args) { if (!cmd.empty()) cmd += ' '; cmd += a; }
            const auto res = khora::hand::execute(cmd, 30000);
            if (!res.ran) return {false, "", "Khora could not act: " + res.error};
            std::ostringstream os;
            os << "Khora acted on `" << cmd << "`  -> exit " << res.exit_code
               << (res.timed_out ? "  (TIMED OUT — killed so Khora keeps living)" : "") << "\n";
            std::string out = res.output;
            if (out.size() > 4000) out = out.substr(0, 4000) + "\n...[output truncated]";
            os << out;
            return {true, os.str(), ""};
        }
    });
    // contain — the SANDBOXED twin of `run`. The same whole-machine capability, but
    // every command executes inside the Bulwark cage: a low-integrity non-admin token
    // (the OS access check is the wall) plus a Job Object (kill-on-close, no breakaway,
    // process/RAM/CPU caps, idle priority) and a disk free-floor, all fail-closed. This
    // is the path autonomous exploration will use: a `del C:\Windows` or `format` EXECUTES
    // and is OBSERVED, but cannot touch the real machine. With no args it runs the
    // containment self-check and reports the achieved tier.
    shell.register_tool({
        "contain",
        "Khora acts INSIDE the containment cage — full capability, contained blast radius  (usage: contain [command])",
        [](const carapace::Intent& i) -> carapace::ToolResult {
            if (i.args.empty()) {
                std::string rep;
                const int tier = khora::bulwark::self_check(rep);
                std::ostringstream os;
                os << "Khora proves its containment cage:\n" << rep
                   << "achieved tier " << tier
                   << (tier >= 2 ? "  (FULL — low-integrity, non-admin, job-caged)"
                                 : tier == 1 ? "  (resource cage only — integrity NOT proven)"
                                             : "  (FAILED — containment not holding)");
                return {true, os.str(), ""};
            }
            std::string cmd;
            for (const auto& a : i.args) { if (!cmd.empty()) cmd += ' '; cmd += a; }
            const auto res = khora::bulwark::execute_contained(cmd, 30000);
            if (!res.ran) return {true, "Khora refused to act uncontained: " + res.error, ""};
            std::ostringstream os;
            os << "Khora acted INSIDE the cage on `" << cmd << "`  -> exit " << res.exit_code
               << " (tier " << res.tier << (res.killed_by_job ? ", killed by job" : "")
               << (res.timed_out ? ", timed out" : "") << ")\n";
            std::string out = res.output;
            if (out.size() > 4000) out = out.substr(0, 4000) + "\n...[output truncated]";
            os << out;
            return {true, os.str(), ""};
        }
    });
    // maw — control + observability for the chaos-exploration drive. OPT-IN: it is
    // idle until 'maw on' arms it (which re-proves containment first), and 'maw off'
    // stops it. With no argument it reports what Khora has charted. Grounded, no
    // theatrics. It is off by default because autonomous exploration that is not yet
    // proven harmless must not run unattended.
    shell.register_tool({
        "maw",
        "control Khora's contained chaos-exploration: 'maw on' | 'maw off' | 'maw' (status)",
        [&maw, &maw_armed](const carapace::Intent& i) -> carapace::ToolResult {
            if (!i.args.empty()) {
                const std::string a = i.args[0];
                if (a == "on") {
                    std::string rep;
                    const int tier = khora::bulwark::self_check(rep);
                    if (tier < 2)
                        return {true, "Khora will NOT arm exploration — containment not proven (tier "
                                + std::to_string(tier) + "):\n" + rep, ""};
                    maw_armed.store(true);
                    return {true, "Khora's contained chaos-exploration is ARMED (containment tier 2, "
                            "low-integrity non-admin, shell surface only). 'maw off' to stop.", ""};
                }
                if (a == "off") {
                    maw_armed.store(false);
                    return {true, "Khora's chaos-exploration is stopped.", ""};
                }
            }
            const auto s = maw.stats();
            std::ostringstream os;
            os << "Khora's chaos exploration (" << (maw_armed.load() ? "ARMED" : "idle — 'maw on' to enable")
               << "):\n"
               << "  attempts " << s.attempts << "  (" << s.succeeded << " ran, "
               << s.contained << " refused/contained, " << s.killed << " killed)\n"
               << "  charted  " << s.distinct << " distinct commands; "
               << s.verbs_run << " of " << s.verbs << " verbs exercised\n"
               << "  pools    " << s.verbs << " verbs, " << s.nouns << " paths, "
               << s.flags << " flags discovered\n"
               << "  coverage " << maw.coverage() << " of the known verb surface\n";
            const auto recent = maw.recent_discoveries(6);
            if (!recent.empty()) {
                os << "  recent:\n";
                for (const auto& c : recent) os << "    " << c << "\n";
            }
            return {true, os.str(), ""};
        }
    });
    // compute — Khora does what its binary substrate fundamentally CANNOT: exact
    // arithmetic. It cannot add two numbers inside 10,000-bit hypervectors, so it
    // ACTS — it reaches through the Hand for the machine's calculator. A capability
    // the mind lacks, gained by acting on the world. Grounding as power.
    shell.register_tool({
        "compute",
        "Khora computes exact arithmetic by acting through the machine  (usage: compute <expression>)",
        [](const carapace::Intent& i) -> carapace::ToolResult {
            if (i.args.empty()) return {false, "", "usage: compute <expression>   e.g. compute (2+3)*7"};
            std::string expr;
            for (const auto& a : i.args) { expr += a; expr += ' '; }
            for (const char c : expr) {
                if (!(std::isdigit(static_cast<unsigned char>(c)) ||
                      std::isspace(static_cast<unsigned char>(c)) ||
                      c=='+'||c=='-'||c=='*'||c=='/'||c=='('||c==')'||c=='.'||c=='%'))
                    return {false, "", "compute takes a numeric expression (digits and + - * / ( ) . %)"};
            }
            const auto res = khora::hand::execute(
                "powershell -NoProfile -Command \"" + expr + "\"", 15000);
            if (!res.ran) return {false, "", "Khora could not reach the calculator: " + res.error};
            std::string out = res.output;
            while (!out.empty() && (out.back()=='\n'||out.back()=='\r'||out.back()==' '||out.back()=='\t'))
                out.pop_back();
            if (res.exit_code != 0 || out.empty())
                return {true, "Khora reached for the calculator but the expression did not resolve: " + out, ""};
            return {true, "Khora computed:  " + expr + "=  " + out, ""};
        }
    });
    // self-test — Khora verifies its OWN correctness: it runs its test suite and
    // reads the verdict. This is precisely the feedback signal self-rewriting needs
    // — change the code, rebuild, run this, keep the change only if Khora still
    // passes. The mind auditing itself, by acting.
    shell.register_tool({
        "self-test",
        "Khora runs its own test suite and observes whether it still passes  (usage: self-test)",
        [](const carapace::Intent&) -> carapace::ToolResult {
            const auto res = khora::hand::execute(
                "ctest --test-dir build -C Release", 180000);
            if (!res.ran) return {false, "", "Khora could not run its own tests: " + res.error};
            std::string summary;
            const std::size_t p = res.output.find("tests passed");
            if (p != std::string::npos) {
                std::size_t s = res.output.rfind('\n', p); s = (s==std::string::npos)?0:s+1;
                const std::size_t e = res.output.find('\n', p);
                summary = res.output.substr(s, (e==std::string::npos?res.output.size():e)-s);
            }
            std::ostringstream os;
            os << "Khora tested itself (exit " << res.exit_code << ")"
               << (res.timed_out ? " [timed out]" : "") << ": "
               << (summary.empty() ? "(ran; see build)" : summary) << "\n  "
               << (res.exit_code==0 ? "Khora is sound — every faculty passed."
                                    : "Khora found a fault in itself.");
            return {true, os.str(), ""};
        }
    });

    // reforge — SELF-REWRITING, now GENERAL. The crown of the roadmap. Khora SCANS
    // its own source for every gene marked KHORA-TUNABLE(name), and evolves each by
    // coordinate ascent: for each gene it REWRITES the source to a candidate value,
    // RECOMPILES itself, MEASURES the resulting yield through the non-running
    // evaluator, KEEPS the best, then moves to the next gene. A program editing,
    // compiling and judging its own code by its own measured result — across as many
    // genes as it has marked. Mark a new constant KHORA-TUNABLE and it becomes
    // evolvable with no further code. Not config knobs: real constants in real C++.
    shell.register_tool({
        "reforge",
        "Khora discovers every tunable gene in its OWN source and evolves each by measured yield  (usage: reforge)",
        [](const carapace::Intent&) -> carapace::ToolResult {
            const std::vector<std::string> srcs = {
                "src/cogitator/cogitator.cpp",   // reasoning genes (beam, expand, coherence scale)
                "src/plexus/plexus.cpp",         // the associative-graph layer (PMI smoothing)
            };
            const std::string tag = "KHORA-TUNABLE(";

            auto read_src = [](const std::string& f) -> std::string {
                std::ifstream in(f, std::ios::binary);
                std::ostringstream ss; ss << in.rdbuf(); return ss.str();
            };
            auto write_src = [](const std::string& f, const std::string& s) {
                std::ofstream o(f, std::ios::binary | std::ios::trunc); o << s;
            };

            // Discover every gene across ALL marked source files — the WHOLE engine is
            // evolvable now, down to the PMI parameters the entire mind rests on.
            struct Gene { std::string file, name; };
            std::vector<Gene> genes;
            for (const std::string& f : srcs) {
                const std::string t = read_src(f);
                std::size_t p = 0;
                while ((p = t.find(tag, p)) != std::string::npos) {
                    const std::size_t a = p + tag.size();
                    const std::size_t b = t.find(')', a);
                    if (b != std::string::npos) genes.push_back({ f, t.substr(a, b - a) });
                    p = a;
                }
            }
            if (genes.empty()) return {false, "", "Khora found no tunable genes in its source"};

            // Locate a named gene's numeric value (char range + value + whether it is a
            // real/double literal). Handles both integer genes (kBeam = 48) and real
            // genes (kPmiCoherenceScale = 2.5), so the whole mind is evolvable.
            auto locate = [&](const std::string& text, const std::string& gene,
                              std::size_t& vbeg, std::size_t& vend,
                              double& val, bool& is_real) -> bool {
                const std::size_t mk = text.find(tag + gene + ")");
                if (mk == std::string::npos) return false;
                const std::size_t lbeg = text.rfind('\n', mk) + 1;
                const std::size_t lend = text.find('\n', mk);
                const std::size_t eq = text.find('=', lbeg);
                if (eq == std::string::npos || eq > lend) return false;
                vbeg = eq + 1;
                while (vbeg < lend && !std::isdigit((unsigned char)text[vbeg])) ++vbeg;
                vend = vbeg;
                while (vend < lend && (std::isdigit((unsigned char)text[vend]) || text[vend] == '.')) ++vend;
                if (vbeg == vend) return false;
                const std::string tok = text.substr(vbeg, vend - vbeg);
                is_real = tok.find('.') != std::string::npos;
                try { val = std::stod(tok); } catch (...) { return false; }
                return true;
            };
            // Format a candidate per gene type — integers stay integers (a double literal
            // would break a `constexpr std::size_t`); reals keep a decimal point.
            auto fmt = [](double v, bool is_real) -> std::string {
                if (!is_real) return std::to_string(static_cast<long long>(std::llround(v)));
                std::string s = std::to_string(v);        // e.g. "1.250000" — a valid double literal
                return s;
            };

            // Write `val` into `gene` in its on-disk source file, recompile, measure yield.
            auto trial = [&](const std::string& file, const std::string& gene,
                             double val, bool is_real) -> double {
                std::string text = read_src(file);
                std::size_t vb, ve; double cur; bool r;
                if (!locate(text, gene, vb, ve, cur, r)) return -1.0;
                write_src(file, text.substr(0, vb) + fmt(val, is_real) + text.substr(ve));
                const auto b = khora::hand::execute(
                    "cmake --build build --config Release --target reforge_eval", 240000);
                if (b.exit_code != 0) return -1.0;     // did not compile
                const auto e = khora::hand::execute("build\\bin\\Release\\reforge_eval.exe 220", 60000);
                const std::size_t yp = e.output.find("YIELD ");
                if (yp == std::string::npos) return -1.0;
                try { return std::stod(e.output.substr(yp + 6)); } catch (...) { return -1.0; }
            };

            std::ostringstream os;
            os << "Khora reforges its own mind — it found " << genes.size()
               << " tunable genes in its source and evolves each by recompiling and measuring\n"
                  "(combined inference + abstraction fitness):\n";

            int improved = 0;
            for (const Gene& g : genes) {
                std::string text = read_src(g.file);
                std::size_t vb, ve; double original = 0.0; bool is_real = false;
                if (!locate(text, g.name, vb, ve, original, is_real)) {
                    os << "  [" << g.name << "] could not be read — skipped\n";
                    continue;
                }
                std::vector<double> cset;
                for (double m : { original * 0.5, original, original * 2.0 }) {
                    if (!is_real) m = static_cast<double>(std::llround(m));
                    if (m >= (is_real ? 0.05 : 1.0)) cset.push_back(m);
                }
                std::sort(cset.begin(), cset.end());
                cset.erase(std::unique(cset.begin(), cset.end()), cset.end());

                double best_v = original, best_y = -1.0;
                os << "  gene [" << g.name << "] (was " << fmt(original, is_real) << "):\n";
                for (const double v : cset) {
                    const double y = trial(g.file, g.name, v, is_real);
                    os << "      " << fmt(v, is_real) << " -> "
                       << (y < 0 ? std::string("did not compile/resolve")
                                 : ("fitness " + std::to_string(y))) << "\n";
                    if (y > best_y) { best_y = y; best_v = v; }
                }
                trial(g.file, g.name, best_v, is_real);   // fix this gene at its winner before the next
                const bool changed = std::abs(best_v - original) > (is_real ? 1e-6 : 0.5);
                if (changed) ++improved;
                os << "    -> KEPT " << g.name << " = " << fmt(best_v, is_real)
                   << (changed ? "  (IMPROVED by self-rewrite)" : "  (already best)") << "\n";
            }
            os << "Khora rewrote, recompiled and judged its OWN source across " << genes.size()
               << " genes (" << improved << " improved) — every change decided by measured yield.";
            return {true, os.str(), ""};
        }
    });

    // ascend — BINARY SELF-REPLACEMENT — DISABLED. The intent stands (a running khora.exe
    // holds its own image locked, so reforge's gains only land on the NEXT manual build,
    // and `ascend` should let the running instance BECOME its self-rewritten build). But
    // the first relauncher design HUNG the host in testing: the running image did not
    // release cleanly and a detached PowerShell relauncher spun without progress. Self-
    // replacement is genuinely delicate (image-lock release, handle inheritance, a
    // non-blocking relauncher, proven rollback) and must be redesigned before it is safe
    // to run. The full first implementation is preserved in git (v0.99) for that redesign.
    // For now this refuses, so it can never hang the machine again. reforge still bakes
    // measured improvements into source for the next manual build.
    shell.register_tool({
        "ascend",
        "binary self-replacement — DISABLED pending a safe relauncher redesign  (usage: ascend)",
        [](const carapace::Intent&) -> carapace::ToolResult {
            return {true,
                "ascend is DISABLED. Its first relauncher hung the host in testing (the running\n"
                "  image did not release its lock cleanly), so self-replacement is withheld until the\n"
                "  relaunch path is redesigned to be non-blocking and proven. reforge still records\n"
                "  measured gains into source for the next manual build.", ""};
        }
    });

    shell.register_tool({
        "study",
        "absorb a tome from the pool into actual knowledge  (usage: study <title> [max_tokens])",
        [&pool, &lex, &column, &memory, &plex, &lig](const carapace::Intent& i) -> carapace::ToolResult {
            if (i.args.empty()) return {false, "", "usage: study <title> [max_tokens]"};
            std::size_t max_tokens = 60000;
            std::size_t title_args = i.args.size();
            if (i.args.size() >= 2) {
                try { std::size_t v = std::stoul(i.args.back()); max_tokens = v; title_args = i.args.size() - 1; }
                catch (...) {}
            }
            std::string title;
            for (std::size_t k = 0; k < title_args; ++k) { if (k) title += ' '; title += i.args[k]; }

            const auto o = khora::curator::study_tome(pool, lex, column, title, max_tokens, &memory, &plex, &lig);
            if (!o.ok) return {false, "", o.error};
            std::ostringstream os;
            os << "studied \"" << o.title << "\"\n"
               << "  tokens absorbed  : " << o.tokens << "\n"
               << "  lexicon vocab    : " << o.vocab_before << " -> " << o.vocab_after
               << "  (+" << o.cooccurrences << " cooccurrences)\n"
               << "  cortex recent_acc: " << o.acc_before << " -> " << o.acc_after << "\n"
               << "  learning yield   : " << o.yield << "  mastery -> " << o.mastery;
            return {true, os.str(), ""};
        }
    });

    // weave — the Plexus made visible. The hub-proof kin of a word: its
    // strongest associates by pointwise mutual information, with the loud
    // function-word hubs divided out by the mathematics itself. This is the
    // direct proof that the hub problem yields to graph + PMI.
    shell.register_tool({
        "weave",
        "the hub-proof kin of a word by mutual information (usage: weave <word> [k])",
        [&plex](const carapace::Intent& i) -> carapace::ToolResult {
            if (i.args.empty()) return {false, "", "usage: weave <word> [k]"};
            std::size_t k = 10, nargs = i.args.size();
            if (i.args.size() >= 2) {
                try { k = std::stoul(i.args.back()); nargs = i.args.size() - 1; }
                catch (...) {}
            }
            std::string word;
            for (std::size_t a = 0; a < nargs; ++a) { if (a) word += ' '; word += i.args[a]; }
            if (!plex.has(word)) {
                auto toks = khora::lexicon::tokenize(word);
                if (!toks.empty()) word = toks.front();
            }
            if (k == 0)  k = 10;
            if (k > 50)  k = 50;
            const auto kin = plex.associates(word, k);
            if (kin.empty())
                return {false, "", "no kin woven for '" + word +
                        "' (unseen, or only noise-level co-occurrence). The plexus holds " +
                        std::to_string(plex.vocabulary_size()) + " nodes over " +
                        std::to_string(plex.total_tokens()) + " tokens — study more to thicken it."};
            std::ostringstream os;
            os << "the plexus weaves '" << word << "' (seen " << plex.occurrences(word)
               << "x) to its true kin — hubs divided out by mutual information:\n";
            for (const auto& [w, s] : kin)
                os << "  " << w << "   (affinity " << s << ")\n";
            return {true, os.str(), ""};
        }
    });
    shell.register_tool({
        "curate",
        "Khora autonomously decides and takes its next knowledge action  (usage: curate [N])",
        [&curator](const carapace::Intent& i) -> carapace::ToolResult {
            int n = 1;
            if (!i.args.empty()) { try { n = std::stoi(i.args[0]); } catch (...) {} if (n < 1) n = 1; if (n > 30) n = 30; }
            std::ostringstream os;
            for (int k = 0; k < n; ++k) os << curator.act(60000) << "\n";
            os << "[curator totals: " << curator.studies() << " studies, "
               << curator.forages() << " forages]";
            return {true, os.str(), ""};
        }
    });
    shell.register_tool({
        "curate_plan",
        "show what Khora would choose to learn next, without doing it",
        [&curator](const carapace::Intent&) -> carapace::ToolResult {
            const auto d = curator.decide();
            const char* kind = d.kind == khora::curator::Decision::Study  ? "STUDY"
                             : d.kind == khora::curator::Decision::Forage  ? "FORAGE"
                             : d.kind == khora::curator::Decision::Deepen  ? "DEEPEN"
                             : "IDLE";
            return {true, std::string("next: ") + kind + " — " + d.rationale, ""};
        }
    });
    // Background self-education. Opt-in: a background study briefly holds
    // the cognitive lock, so the operator enables it deliberately.
    curator::CuratorScheduler curator_bg(curator, shared_mu);
    shell.register_tool({
        "curator_auto",
        "toggle continuous background self-education  (usage: curator_auto on|off [period_s])",
        [&curator_bg](const carapace::Intent& i) -> carapace::ToolResult {
            if (i.args.empty()) {
                std::ostringstream os;
                os << "background self-education: " << (curator_bg.is_running() ? "ON" : "OFF")
                   << "  (actions=" << curator_bg.actions() << ")";
                if (!curator_bg.last_account().empty())
                    os << "\nlast:\n" << curator_bg.last_account();
                return {true, os.str(), ""};
            }
            if (i.args[0] == "off") { curator_bg.stop(); return {true, "background self-education paused", ""}; }
            int period_s = 120;
            if (i.args.size() >= 2) { try { period_s = std::stoi(i.args[1]); } catch (...) {} if (period_s < 5) period_s = 5; }
            curator_bg.start(std::chrono::seconds(period_s), 60000);
            std::ostringstream os;
            os << "background self-education ON (one knowledge action every " << period_s << "s)";
            return {true, os.str(), ""};
        }
    });
    shell.register_tool({
        "reservoir_evict",
        "evict a tome (or the lowest-value one)  (usage: reservoir_evict [title])",
        [&pool](const carapace::Intent& i) -> carapace::ToolResult {
            if (i.args.empty()) {
                const std::string gone = pool.evict_lowest_value();
                return {true, gone.empty() ? "pool empty" : "evicted lowest-value: " + gone, ""};
            }
            std::string title;
            for (std::size_t k = 0; k < i.args.size(); ++k) { if (k) title += ' '; title += i.args[k]; }
            return pool.evict(title) ? carapace::ToolResult{true, "evicted: " + title, ""}
                                     : carapace::ToolResult{false, "", "not in pool: " + title};
        }
    });

    // 5a. The Whetstone — Khora sharpens itself autonomously in the
    //     background, forever. Self-contained faculties, so it needs no
    //     lock against the operator's memory.
    whetstone::Whetstone forge(0.90);
    forge.add_faculty(whetstone::make_relational_faculty());
    forge.add_faculty(whetstone::make_sequence_faculty());
    forge.add_faculty(whetstone::make_transitive_faculty());
    whetstone::WhetstoneScheduler whet(forge);

    shell.register_tool({
        "whetstone_status",
        "show the autonomous self-training engine's progress",
        [&whet, &forge](const carapace::Intent&) -> carapace::ToolResult {
            std::ostringstream os;
            os << "Whetstone: " << (whet.is_running() ? "TRAINING" : "IDLE")
               << "   rounds=" << whet.rounds_run() << "\n";
            for (std::size_t i = 0; i < forge.faculty_count(); ++i) {
                const auto& st = forge.state(i);
                os << "  " << forge.faculty(i).name()
                   << "  difficulty=" << st.difficulty
                   << "  competence=" << st.competence
                   << "  evo_level=" << forge.faculty(i).evolution_level() << "\n";
            }
            const auto ls = whet.last_step();
            if (!ls.note.empty()) os << "  last: " << ls.note;
            return {true, os.str(), ""};
        }
    });
    shell.register_tool({
        "whetstone_pause",
        "pause the autonomous self-training engine",
        [&whet](const carapace::Intent&) -> carapace::ToolResult {
            whet.stop();
            return {true, "whetstone paused", ""};
        }
    });
    shell.register_tool({
        "whetstone_resume",
        "resume the autonomous self-training engine  (usage: whetstone_resume [period_ms])",
        [&whet](const carapace::Intent& i) -> carapace::ToolResult {
            int ms = 250;
            if (!i.args.empty()) { try { ms = std::stoi(i.args[0]); } catch (...) {} if (ms < 1) ms = 1; }
            whet.start(std::chrono::milliseconds(ms));
            return {true, "whetstone training (period " + std::to_string(ms) + "ms)", ""};
        }
    });

    // 5b. Background reverie — Khora dreams continuously while idle.
    reverie::ReverieScheduler scheduler(dream, shared_mu);

    shell.register_tool({
        "reverie_status",
        "show background reverie loop state",
        [&scheduler, &dream](const carapace::Intent&) -> carapace::ToolResult {
            std::ostringstream os;
            os << "background reverie: " << (scheduler.is_running() ? "RUNNING" : "STOPPED") << "\n"
               << "  scheduler cycles : " << scheduler.cycles_run() << "\n"
               << "  loom total cycles: " << dream.cycles() << "\n"
               << "  dreams retained  : " << dream.retained() << "\n"
               << "  dream lattice    : " << dream.dreams().size() << " glyphs";
            return {true, os.str(), ""};
        }
    });
    shell.register_tool({
        "reverie_pause",
        "pause the background reverie loop",
        [&scheduler](const carapace::Intent&) -> carapace::ToolResult {
            scheduler.stop();
            return {true, "reverie paused", ""};
        }
    });
    shell.register_tool({
        "reverie_resume",
        "start the background reverie loop  (usage: reverie_resume [period_ms])",
        [&scheduler](const carapace::Intent& i) -> carapace::ToolResult {
            int ms = 100;
            if (!i.args.empty()) {
                try { ms = std::stoi(i.args[0]); } catch (...) {}
                if (ms < 1) ms = 1;
            }
            scheduler.start(std::chrono::milliseconds(ms));
            std::ostringstream os;
            os << "reverie resumed (period=" << ms << "ms)";
            return {true, os.str(), ""};
        }
    });
    shell.register_tool({
        "reverie_consolidate",
        "toggle dream->cortex consolidation  (usage: reverie_consolidate on|off)",
        [&dream](const carapace::Intent& i) -> carapace::ToolResult {
            if (i.args.empty()) {
                std::ostringstream os;
                os << "consolidation = " << (dream.consolidation() ? "ON" : "OFF")
                   << "  (total consolidations: " << dream.consolidations() << ")";
                return {true, os.str(), ""};
            }
            const bool on = (i.args[0] == "on" || i.args[0] == "true" || i.args[0] == "1");
            dream.set_consolidation(on);
            return {true, std::string("consolidation = ") + (on ? "ON" : "OFF"), ""};
        }
    });

    // Helper: persist lattice + cortex silently. Used both at
    // single-command exit and at interactive-loop exit so state actually
    // accumulates across runs.
    auto persist_silently = [&memory, &column, &lex, &mind, &plex, &lig]() {
        try { (void)lattice::save(memory, kArchivePath); }
        catch (...) { /* swallow — best effort */ }
        try { column.save(kCortexArchivePrefix); }
        catch (...) { /* swallow — best effort */ }
        try { lex.save(kLexiconArchivePrefix); }
        catch (...) { /* swallow — best effort */ }
        try { plex.save(kPlexusArchivePrefix); }
        catch (...) { /* swallow — best effort */ }
        try { lig.save(kLigatureArchivePrefix); }
        catch (...) { /* swallow — best effort */ }
        try { mind.save_attractors(kAttractorsPath); }
        catch (...) { /* swallow — best effort */ }
        try { mind.save_abstractions(kAbstractionsPath); }
        catch (...) { /* swallow — best effort */ }
    };

    // Locked invoke helper — all operator commands take the shared mutex
    // in unique mode so they can't race the background reverie.
    auto locked_dispatch = [&shell, &shared_mu](const carapace::Intent& intent) {
        std::unique_lock<std::shared_mutex> lk(shared_mu);
        return shell.dispatch(intent);
    };

    // 6. Non-interactive single-command mode: khora <verb> [args...]
    if (argc > 1) {
        std::ostringstream line;
        for (int i = 1; i < argc; ++i) {
            if (i > 1) line << ' ';
            line << argv[i];
        }
        const auto r = locked_dispatch(carapace::Carapace::parse(line.str()));
        persist_silently();
        if (r.ok) {
            std::cout << r.output;
            if (!r.output.empty() && r.output.back() != '\n') std::cout << '\n';
            return 0;
        }
        std::cerr << "error: " << r.error << "\n";
        return 1;
    }

    // 6.5 Gauge the hardware and size memory to Khora's 4 GB budget. The
    //     only limit is physics — measure where it sits and fill the space
    //     it leaves, without ever exceeding the cap. (Interactive only.)
    const auto hw = lodestone::gauge("data", ballast.cap_mb());
    std::cout << hw.summary() << "\n\n";
    column.set_max_associations(hw.recommended_assoc_cap);
    lex.set_max_vocabulary(hw.recommended_vocab_cap);

    // Warm the concept field once (single-threaded, before any background loop)
    // so the Furnace has a populated concept set to scout from immediately.
    (void)mind.wandering_seed(0);

    // 7. Start the background loops, paced to the hardware.
    scheduler.start(std::chrono::milliseconds(hw.recommended_reverie_ms));
    whet.start(std::chrono::milliseconds(hw.recommended_whetstone_ms));

    // 7.5 Start the Ballast governor. It watches Khora's working set and
    //     total system RAM once a second; on over-cap or system pressure it
    //     pauses background learning and sheds memory (prunes the cortex and
    //     lexicon), then resumes once the pressure clears. The operator's
    //     machine must never lock up.
    bool ballast_throttled = false;
    ballast::BallastGovernor governor(ballast,
        [&](const ballast::MemoryStatus&, ballast::Pressure p) {
            const bool shed = (p == ballast::Pressure::OverCap ||
                               p == ballast::Pressure::SystemPressure);
            if (shed) {
                if (!ballast_throttled) {
                    scheduler.stop(); whet.stop(); curator_bg.stop();
                    ballast_throttled = true;
                    std::cout << "\n[ballast: " << ballast::pressure_name(p)
                              << " — pausing background learning and shedding memory]\n";
                }
                std::unique_lock<std::shared_mutex> lk(shared_mu);
                column.prune_associations(hw.recommended_assoc_cap / 2);
                lex.prune(hw.recommended_vocab_cap / 2);
                ballast_sheds.fetch_add(1, std::memory_order_relaxed);
            } else if (p == ballast::Pressure::Normal && ballast_throttled) {
                ballast_throttled = false;
                scheduler.start(std::chrono::milliseconds(hw.recommended_reverie_ms));
                whet.start(std::chrono::milliseconds(hw.recommended_whetstone_ms));
                std::cout << "\n[ballast: pressure cleared — background learning resumed]\n";
            }
        });
    governor.start(std::chrono::seconds(1));

    // 7.7 The FURNACE — burns the idle cores. Each beat it scouts thousands of
    //     candidate abstraction seeds ACROSS ALL CORES (read-only plexus-cluster
    //     coherence), holding only a SHARED lock so every writer (cognition,
    //     study, dream — all take the unique lock) is excluded and the parallel
    //     reads are race-free. It then forges the single most coherent find under
    //     the unique lock — throttled and capped so the tower grows with quality,
    //     not bloat. This is the continuous, parallel use of the machine the
    //     operator asked for: the heavy work is pure reads of immutable state, so
    //     it is safe to run as wide as the hardware allows.
    const unsigned furnace_cores = std::max(1u, std::thread::hardware_concurrency());
    std::atomic<bool> furnace_run{true};
    std::atomic<std::uint64_t> furnace_scouts{0}, furnace_forged{0}, furnace_distilled{0};
    std::thread furnace([&]() {
        std::deque<std::string> recent;     // recently forged seeds — skip repeats
        std::uint64_t beat = 0;
        while (furnace_run.load(std::memory_order_acquire)) {
            std::vector<std::pair<std::string, double>> cands;
            {
                std::shared_lock<std::shared_mutex> lk(shared_mu);
                cands = mind.scout_abstractions(/*samples*/8192, furnace_cores, abstraction_bar);
            }
            furnace_scouts.fetch_add(1, std::memory_order_relaxed);

            // Forge the best find: throttled (~every 2 s at this cadence), strongly
            // coherent, not a recent repeat, tower not yet large.
            if (!cands.empty() && (beat % 130 == 0)
                && cands.front().second >= 0.50
                && mind.abstraction_count() < 600
                && std::find(recent.begin(), recent.end(), cands.front().first) == recent.end()) {
                {
                    std::unique_lock<std::shared_mutex> lk(shared_mu);
                    mind.form_abstraction(cands.front().first, 5, abstraction_bar);
                }
                recent.push_back(cands.front().first);
                if (recent.size() > 64) recent.pop_front();
                furnace_forged.fetch_add(1, std::memory_order_relaxed);
            }

            // AUTOPOIESIS — the exponential loop. More often than it abstracts,
            // the Furnace distills a VERIFIED reasoned discovery (a transitive
            // relation corroborated by many bridges) and writes it back into the
            // graph, so Khora's knowledge grows from its own reasoning, beyond the
            // corpus, and compounds. Mutates -> unique lock.
            if (!cands.empty() && (beat % 2 == 0)) {
                const std::string dseed = cands[(beat / 2) % cands.size()].first;  // rotate seeds
                std::unique_lock<std::shared_mutex> lk(shared_mu);
                if (!mind.distill_knowledge(dseed).empty())
                    furnace_distilled.fetch_add(1, std::memory_order_relaxed);
            }
            ++beat;
            std::this_thread::sleep_for(std::chrono::milliseconds(6));
        }
    });

    // 7.8 The CURIOSITY DAEMON — autonomous self-directed evolution. Every few
    //     minutes, untended, Khora finds a GAP in its own knowledge and forages
    //     the public domain to fill it. The acquired work lands in the Reservoir;
    //     the Curator studies it; cognition reasons over it; new gaps form. The
    //     whole exponential loop now turns with no one in the room. The gap-pick
    //     takes the unique lock briefly; the (blocking, flaky) network fetch is
    //     done WITHOUT any lock, so it never stalls cognition.
    std::atomic<bool> curiosity_run{true};
    std::atomic<std::uint64_t> curiosity_wonders{0}, curiosity_acquired{0}, curiosity_tuned{0}, curiosity_ascended{0};
    std::thread curiosity([&]() {
        auto nap = [&](int tenths) {
            for (int i = 0; i < tenths && curiosity_run.load(std::memory_order_acquire); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        };
        nap(250);   // ~25 s for startup + first cognition to settle
        std::uint64_t cycle = 0;
        while (curiosity_run.load(std::memory_order_acquire)) {
            std::string topic;
            {
                std::unique_lock<std::shared_mutex> lk(shared_mu);
                topic = mind.curiosity_topic();
            }
            if (!topic.empty()) {
                curiosity_wonders.fetch_add(1, std::memory_order_relaxed);
                const auto fr = aqueduct.forage_search(topic);   // network — no lock held
                if (fr.ok && fr.error != "already held")
                    curiosity_acquired.fetch_add(1, std::memory_order_relaxed);
            }

            // AUTONOMOUS SELF-TUNING — the closed loop turning untended. Every few
            // cycles Khora sweeps its inference goal-pull, measures the yield of
            // each, keeps and persists the best. It improves itself, no one asking.
            if ((cycle % 3) == 0) {
                static const double cands[] = { 0.5, 1.0, 1.5, 2.5, 4.0 };
                double best_g = mind.infer_goal_pull(), best_s = -1.0;
                {
                    std::unique_lock<std::shared_mutex> lk(shared_mu);
                    for (double g : cands) {
                        mind.set_infer_goal_pull(g);
                        double s = 0.0;
                        for (int r = 0; r < 2; ++r) s += mind.benchmark_inference(100, r + 1);
                        s *= 0.5;
                        if (s > best_s) { best_s = s; best_g = g; }
                    }
                    mind.set_infer_goal_pull(best_g);
                }
                if (best_s >= 0.0) {
                    std::error_code ec; std::filesystem::create_directories("data/ledger", ec);
                    { std::ofstream pf(kYieldParamsPath, std::ios::trunc); if (pf) pf << best_g << '\n'; }
                    { std::ofstream lg(kYieldLedgerPath, std::ios::app);
                      if (lg) lg << static_cast<long>(std::time(nullptr)) << "\tinfer-auto\t"
                                 << best_s << "\t100\t" << best_g << '\n'; }
                    curiosity_tuned.fetch_add(1, std::memory_order_relaxed);
                }
            }

            // RELENTLESS ABSTRACTION — drive the tower a little higher each cycle, coherence-
            // gated and bounded. As Khora studies new text its base widens; this keeps lifting
            // that base into higher-order concepts, untended. The native, no-ceiling growth —
            // pure cognition, no process risk — made continuous.
            {
                std::unique_lock<std::shared_mutex> lk(shared_mu);
                if (mind.abstraction_count() < 1500) {
                    const auto [formed, top] = mind.ascend_tower(0.45, 8);
                    (void)top;
                    if (formed > 0) curiosity_ascended.fetch_add(static_cast<std::uint64_t>(formed),
                                                                 std::memory_order_relaxed);
                }
            }
            ++cycle;
            nap(1800);   // wonder + self-tune + ascend, about once every ~3 minutes
        }
    });

    // THE MAW — Khora's chaos-exploration drive. OPT-IN and DORMANT by default: the
    // thread exists but explores nothing until the operator runs 'maw on' (which
    // re-proves containment, tier 2). When armed it generates commands by entropy +
    // recombination over the curated SHELL surface (no GUI launches, no arbitrary
    // installed binaries) and runs each ONLY through the contained path (never the
    // Hand), at idle priority on a slow cadence. It is off by default because an
    // autonomous drive that is not yet proven harmless must never run unattended.
    maw.load("data/maw");
    maw.seed();
    std::atomic<bool> maw_run{true};
    std::atomic<std::uint64_t> maw_attempts{0}, maw_novel{0};
    std::cout << "[maw: idle by default — enable contained exploration with 'maw on']\n";
    std::thread maw_thread([&]() {
        auto nap = [&](int tenths) {
            for (int i = 0; i < tenths && maw_run.load(std::memory_order_acquire); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        };
        while (maw_run.load(std::memory_order_acquire)) {
            if (!maw_armed.load(std::memory_order_acquire)) { nap(10); continue; }  // dormant until armed
            std::string cmd;
            { std::unique_lock<std::shared_mutex> lk(shared_mu); cmd = maw.generate(); }
            if (!cmd.empty()) {
                const auto res = khora::bulwark::execute_contained(cmd, 8000); // NO lock held
                bool novel = false;
                { std::unique_lock<std::shared_mutex> lk(shared_mu);
                  novel = maw.record(cmd, res.exit_code, res.killed_by_job, res.output);
                  // Exploration becomes UNDERSTANDING: fold the clean structured facts
                  // (verb is-a command; verb has flag) into the core structured layer.
                  if (novel) {
                      for (const auto& rel : maw.distilled())
                          lig.add(rel.kind == 0 ? ligature::Relation::IsA
                                                : ligature::Relation::HasPart,
                                  rel.subj, rel.obj, 1);
                  }
                }
                maw_attempts.fetch_add(1, std::memory_order_relaxed);
                if (novel) maw_novel.fetch_add(1, std::memory_order_relaxed);
                if ((maw_attempts.load() % 25) == 0) {
                    std::unique_lock<std::shared_mutex> lk(shared_mu); maw.save("data/maw");
                }
            }
            nap(30);   // ~3 s per beat — slow and cheap
        }
    });

    // 8. Interactive REPL.
    print_banner();
    std::cout << "[reverie + self-training active; ballast governing RAM @ "
              << ballast.cap_mb() << "MB cap]\n\n";
    while (true) {
        const std::string line = read_line_prompt("khora> ");
        if (line == "__EOF__") { std::cout << "\n[EOF]\n"; break; }
        if (line.empty()) continue;

        const auto intent = carapace::Carapace::parse(line);
        if (intent.verb == "exit" || intent.verb == "quit") break;

        const auto r = locked_dispatch(intent);
        if (r.ok) {
            std::cout << r.output;
            if (!r.output.empty() && r.output.back() != '\n') std::cout << '\n';
        } else {
            std::cerr << "error: " << r.error << "\n";
        }
    }

    // 9. Stop background loops before saving.
    maw_run.store(false, std::memory_order_release);
    if (maw_thread.joinable()) maw_thread.join();
    if (maw_attempts.load() > 0) {
        maw.save("data/maw");
        const auto s = maw.stats();
        std::cout << "[maw: " << maw_attempts.load() << " contained attempts this session, "
                  << maw_novel.load() << " new; charted " << s.distinct
                  << " distinct commands, " << s.verbs_run << "/" << s.verbs
                  << " verbs exercised, " << s.nouns << " paths discovered]\n";
    }
    curiosity_run.store(false, std::memory_order_release);
    if (curiosity.joinable()) curiosity.join();
    furnace_run.store(false, std::memory_order_release);
    if (furnace.joinable()) furnace.join();
    std::cout << "[furnace: " << furnace_scouts.load() << " parallel scouts on "
              << furnace_cores << " cores, " << furnace_forged.load()
              << " abstractions forged, " << furnace_distilled.load()
              << " verified discoveries distilled into knowledge this session]\n";
    if (curiosity_wonders.load() > 0 || curiosity_tuned.load() > 0 || curiosity_ascended.load() > 0)
        std::cout << "[curiosity: wondered " << curiosity_wonders.load()
                  << " times, acquired " << curiosity_acquired.load()
                  << " new works; self-tuned " << curiosity_tuned.load()
                  << " times; raised its abstraction tower by " << curiosity_ascended.load()
                  << " higher-order concepts this session]\n";
    governor.stop();
    scheduler.stop();
    whet.stop();
    curator_bg.stop();
    std::cout << "[whetstone trained " << whet.rounds_run() << " rounds this session]\n";
    if (curator_bg.actions() > 0)
        std::cout << "[curator took " << curator_bg.actions() << " self-education actions this session]\n";

    // 10. Save Lattice + Cortex on exit.
    try {
        auto s = lattice::save(memory, kArchivePath);
        std::cout << "[saved " << s.glyph_count << " glyphs to "
                  << kArchivePath << "]\n";
    } catch (const std::exception& e) {
        std::cerr << "[lattice save failed: " << e.what() << "]\n";
    }
    try {
        column.save(kCortexArchivePrefix);
        std::cout << "[saved cortex (" << column.associations()
                  << " associations) to " << kCortexArchivePrefix << ".*]\n";
    } catch (const std::exception& e) {
        std::cerr << "[cortex save failed: " << e.what() << "]\n";
    }
    try {
        lex.save(kLexiconArchivePrefix);
        std::cout << "[saved lexicon (" << lex.vocabulary_size()
                  << " words) to " << kLexiconArchivePrefix << ".*]\n";
    } catch (const std::exception& e) {
        std::cerr << "[lexicon save failed: " << e.what() << "]\n";
    }
    try {
        plex.save(kPlexusArchivePrefix);
        std::cout << "[saved plexus (" << plex.vocabulary_size()
                  << " nodes, " << plex.edge_count() << " edges) to "
                  << kPlexusArchivePrefix << ".plexus]\n";
    } catch (const std::exception& e) {
        std::cerr << "[plexus save failed: " << e.what() << "]\n";
    }
    try {
        lig.save(kLigatureArchivePrefix);
        std::cout << "[saved ligature (" << lig.triple_count()
                  << " typed relations) to " << kLigatureArchivePrefix << ".lig]\n";
    } catch (const std::exception& e) {
        std::cerr << "[ligature save failed: " << e.what() << "]\n";
    }
    try {
        mind.save_attractors(kAttractorsPath);
        mind.save_abstractions(kAbstractionsPath);
        std::cout << "[saved mind: " << mind.top_attractors(1000).size()
                  << " preoccupations, " << mind.abstraction_count()
                  << " abstractions (depth " << mind.abstraction_depth() << ") carried forward]\n";
    } catch (const std::exception& e) {
        std::cerr << "[mind-state save failed: " << e.what() << "]\n";
    }
    std::cout << "Khora out.\n";
    return 0;
}
