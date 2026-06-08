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
#include "khora/reverie/reverie_loom.hpp"
#include "khora/soma/soma_nexus.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>

namespace {

constexpr const char* kArchivePath = "data/lattice_archive/main.klat";

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
    reverie::ReverieLoom dream(memory, column, nexus);

    // 2. Try to load persisted lattice state.
    namespace fs = std::filesystem;
    fs::create_directories(fs::path(kArchivePath).parent_path());
    if (fs::exists(kArchivePath)) {
        try {
            memory = lattice::load(kArchivePath);
            std::cout << "[loaded " << memory.size() << " glyphs from "
                      << kArchivePath << "]\n";
        } catch (const lattice::PersistError& e) {
            std::cout << "[warning: could not load archive: " << e.what() << "]\n";
        }
    }

    // 3. Register tools.
    carapace::Carapace shell;
    carapace::register_core_tools(shell);
    carapace::register_memory_tools(shell, memory);
    carapace::register_cortex_tools(shell, column);
    carapace::register_soma_tools(shell, nexus);

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

    // Helper: persist the lattice silently. Used both at single-command
    // exit and at interactive-loop exit so state actually accumulates
    // across runs.
    auto persist_silently = [&memory]() {
        try { (void)lattice::save(memory, kArchivePath); }
        catch (...) { /* swallow — best effort */ }
    };

    // 4. Non-interactive single-command mode: khora <verb> [args...]
    if (argc > 1) {
        std::ostringstream line;
        for (int i = 1; i < argc; ++i) {
            if (i > 1) line << ' ';
            line << argv[i];
        }
        const auto r = shell.invoke(line.str());
        persist_silently();
        if (r.ok) {
            std::cout << r.output;
            if (!r.output.empty() && r.output.back() != '\n') std::cout << '\n';
            return 0;
        }
        std::cerr << "error: " << r.error << "\n";
        return 1;
    }

    // 5. Interactive REPL.
    print_banner();
    while (true) {
        const std::string line = read_line_prompt("khora> ");
        if (line == "__EOF__") { std::cout << "\n[EOF]\n"; break; }
        if (line.empty()) continue;

        const auto intent = carapace::Carapace::parse(line);
        if (intent.verb == "exit" || intent.verb == "quit") break;

        const auto r = shell.dispatch(intent);
        if (r.ok) {
            std::cout << r.output;
            if (!r.output.empty() && r.output.back() != '\n') std::cout << '\n';
        } else {
            std::cerr << "error: " << r.error << "\n";
        }
    }

    // 6. Save Lattice on exit.
    try {
        auto s = lattice::save(memory, kArchivePath);
        std::cout << "[saved " << s.glyph_count << " glyphs to "
                  << kArchivePath << "]\n";
    } catch (const std::exception& e) {
        std::cerr << "[save failed: " << e.what() << "]\n";
    }
    std::cout << "Khora out.\n";
    return 0;
}
