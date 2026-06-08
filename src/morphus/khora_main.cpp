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
#include "khora/cortex/predictive_column.hpp"
#include "khora/lattice/lattice.hpp"
#include "khora/lattice/persistence.hpp"
#include "khora/ballast/ballast.hpp"
#include "khora/lexicon/lexicon.hpp"
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
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <shared_mutex>
#include <sstream>
#include <string>

namespace {

constexpr const char* kArchivePath          = "data/lattice_archive/main.klat";
constexpr const char* kCortexArchivePrefix  = "data/cortex_archive/main";
constexpr const char* kLexiconArchivePrefix = "data/lexicon_archive/main";
constexpr const char* kAttractorsPath       = "data/cogitator_archive/attractors.txt";

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

    // 1. Bring up subsystems.
    lattice::Lattice memory;
    cortex::PredictiveColumn column(3);
    soma::SomaNexus nexus;
    lexicon::Lexicon lex;
    reverie::ReverieLoom dream(memory, column, nexus);
    cogitator::Cogitator mind(lex, memory, column, nexus);

    // Liquid knowledge: the Reservoir holds source texts (~20 GB cap),
    // the Aqueduct channels new ones in from the public domain.
    reservoir::Reservoir pool(std::filesystem::path("data") / "reservoir",
                              20ull * 1024 * 1024 * 1024);
    reservoir::Aqueduct aqueduct(pool);

    // The Curator — Khora decides for itself what to learn next. Studied
    // vocabulary is promoted into `memory` so cognition can think over it.
    curator::Curator curator(pool, aqueduct, lex, column, &memory);

    // The Ballast — Khora is hard-capped at 4 GB of system RAM and backs
    // off when total system RAM crosses 90% (the operator's machine must
    // never lock up). GPU memory and NVMe are used freely elsewhere.
    ballast::Ballast ballast(/*cap_mb*/4096, /*system_pressure*/0.90);
    std::atomic<std::uint64_t> ballast_sheds{0};

    // 2. Try to load persisted lattice + cortex state.
    namespace fs = std::filesystem;
    fs::create_directories(fs::path(kArchivePath).parent_path());
    fs::create_directories(fs::path(kCortexArchivePrefix).parent_path());

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
    // Restore Khora's preoccupations — its inner life resumes where it left off.
    mind.load_attractors(kAttractorsPath);
    if (!mind.top_attractors(1).empty()) {
        std::cout << "[resumed mind: preoccupied with";
        for (const auto& [name, count] : mind.top_attractors(5)) std::cout << ' ' << name;
        std::cout << "]\n";
    }

    // 3. Shared mutex coordinating the main thread (operator tools) with
    //    the background reverie thread.
    std::shared_mutex shared_mu;

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
        [&column, &lex](const carapace::Intent&) -> carapace::ToolResult {
            const auto hw = lodestone::gauge("data", 4096);
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
        rum.perform = [&mind, pick_seed, &ferment_seed]() -> std::string {
            // Chaos is woven into curiosity: now and then Khora collides
            // concepts instead of wandering — entropy as a source of ideas.
            const bool chaos = (ferment_seed % 3) == 0;
            ++ferment_seed;
            if (chaos) {
                const auto s = mind.synthesize("", "", ferment_seed);
                if (!s.emergent.empty())
                    return "ferment " + s.a + " x " + s.b + " ~> " + s.emergent.front().label;
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
        [&aqueduct, &pool, &lex, &column, &memory, &mind](const carapace::Intent& i) -> carapace::ToolResult {
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
                const auto o = khora::curator::study_tome(pool, lex, column, title, 60000, &memory);
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
        "study",
        "absorb a tome from the pool into actual knowledge  (usage: study <title> [max_tokens])",
        [&pool, &lex, &column, &memory](const carapace::Intent& i) -> carapace::ToolResult {
            if (i.args.empty()) return {false, "", "usage: study <title> [max_tokens]"};
            std::size_t max_tokens = 60000;
            std::size_t title_args = i.args.size();
            if (i.args.size() >= 2) {
                try { std::size_t v = std::stoul(i.args.back()); max_tokens = v; title_args = i.args.size() - 1; }
                catch (...) {}
            }
            std::string title;
            for (std::size_t k = 0; k < title_args; ++k) { if (k) title += ' '; title += i.args[k]; }

            const auto o = khora::curator::study_tome(pool, lex, column, title, max_tokens, &memory);
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
    auto persist_silently = [&memory, &column, &lex, &mind]() {
        try { (void)lattice::save(memory, kArchivePath); }
        catch (...) { /* swallow — best effort */ }
        try { column.save(kCortexArchivePrefix); }
        catch (...) { /* swallow — best effort */ }
        try { lex.save(kLexiconArchivePrefix); }
        catch (...) { /* swallow — best effort */ }
        try { mind.save_attractors(kAttractorsPath); }
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
        mind.save_attractors(kAttractorsPath);
        std::cout << "[saved mind: " << mind.top_attractors(1000).size()
                  << " preoccupations carried forward]\n";
    } catch (const std::exception& e) {
        std::cerr << "[attractor save failed: " << e.what() << "]\n";
    }
    std::cout << "Khora out.\n";
    return 0;
}
