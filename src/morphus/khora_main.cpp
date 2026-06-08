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
#include "khora/lexicon/lexicon.hpp"
#include "khora/reservoir/aqueduct.hpp"
#include "khora/reservoir/reservoir.hpp"
#include "khora/reverie/reverie_loom.hpp"
#include "khora/reverie/reverie_scheduler.hpp"
#include "khora/soma/soma_nexus.hpp"
#include "khora/whetstone/whetstone.hpp"
#include "khora/whetstone/whetstone_scheduler.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <shared_mutex>
#include <sstream>
#include <string>

namespace {

constexpr const char* kArchivePath          = "data/lattice_archive/main.klat";
constexpr const char* kCortexArchivePrefix  = "data/cortex_archive/main";
constexpr const char* kLexiconArchivePrefix = "data/lexicon_archive/main";

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

    // The Curator — Khora decides for itself what to learn next.
    curator::Curator curator(pool, aqueduct, lex, column);

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
    shell.register_tool({
        "study",
        "absorb a tome from the pool into actual knowledge  (usage: study <title> [max_tokens])",
        [&pool, &lex, &column](const carapace::Intent& i) -> carapace::ToolResult {
            if (i.args.empty()) return {false, "", "usage: study <title> [max_tokens]"};
            std::size_t max_tokens = 60000;
            std::size_t title_args = i.args.size();
            if (i.args.size() >= 2) {
                try { std::size_t v = std::stoul(i.args.back()); max_tokens = v; title_args = i.args.size() - 1; }
                catch (...) {}
            }
            std::string title;
            for (std::size_t k = 0; k < title_args; ++k) { if (k) title += ' '; title += i.args[k]; }

            const auto o = khora::curator::study_tome(pool, lex, column, title, max_tokens);
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
    auto persist_silently = [&memory, &column, &lex]() {
        try { (void)lattice::save(memory, kArchivePath); }
        catch (...) { /* swallow — best effort */ }
        try { column.save(kCortexArchivePrefix); }
        catch (...) { /* swallow — best effort */ }
        try { lex.save(kLexiconArchivePrefix); }
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

    // 7. Start the background loops for interactive mode: Khora dreams
    //    and sharpens itself the whole time the operator is present.
    scheduler.start(std::chrono::milliseconds(100));
    whet.start(std::chrono::milliseconds(250));

    // 8. Interactive REPL.
    print_banner();
    std::cout << "[background reverie @ 100ms + autonomous self-training @ 250ms active]\n\n";
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
    std::cout << "Khora out.\n";
    return 0;
}
