// khora.exe — the actual Morphus runtime entry point.
//
// Brings up the full architecture (Lattice + Cortex + Soma + Reverie),
// registers tools via Carapace, optionally loads/saves Lattice state
// to data/lattice_archive/main.klat across runs, and offers an
// interactive REPL.

#include "khora/carapace/builtin_tools.hpp"
#include "khora/carapace/carapace.hpp"
#include "khora/cortex/predictive_column.hpp"
#include "khora/lattice/lattice.hpp"
#include "khora/lattice/persistence.hpp"
#include "khora/lexicon/lexicon.hpp"
#include "khora/reverie/reverie_loom.hpp"
#include "khora/reverie/reverie_scheduler.hpp"
#include "khora/soma/soma_nexus.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <shared_mutex>
#include <sstream>
#include <string>

namespace {

constexpr const char* kArchivePath        = "data/lattice_archive/main.klat";
constexpr const char* kCortexArchivePrefix = "data/cortex_archive/main";

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

    // 5. Background reverie — Khora dreams continuously while idle.
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
    auto persist_silently = [&memory, &column]() {
        try { (void)lattice::save(memory, kArchivePath); }
        catch (...) { /* swallow — best effort */ }
        try { column.save(kCortexArchivePrefix); }
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

    // 7. Start the background reverie thread for interactive mode.
    scheduler.start(std::chrono::milliseconds(100));

    // 8. Interactive REPL.
    print_banner();
    std::cout << "[background reverie loop active @ 100ms period]\n\n";
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

    // 9. Stop background reverie before saving (we own the mutex now).
    scheduler.stop();

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
    std::cout << "Khora out.\n";
    return 0;
}
