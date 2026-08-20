#include "khora/carapace/builtin_tools.hpp"
#include "khora/cogitator/cogitator.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace khora::carapace {

namespace {

ToolResult make_ok(std::string output) { return {true, std::move(output), ""}; }
ToolResult make_err(std::string error) { return {false, "", std::move(error)}; }

std::string join_args(const std::vector<std::string>& args, std::size_t start = 0) {
    std::string out;
    for (std::size_t i = start; i < args.size(); ++i) {
        if (i > start) out.push_back(' ');
        out += args[i];
    }
    return out;
}

std::string now_iso() {
    const auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    return buf;
}

constexpr std::uintmax_t kMaxReadBytes = 4ULL * 1024ULL * 1024ULL;  // 4 MB cap on cat

} // namespace

void register_core_tools(Carapace& c) {
    c.register_tool({
        "help",
        "list all registered tools",
        [&c](const Intent&) -> ToolResult {
            std::ostringstream os;
            os << "Khora tools:\n";
            for (const auto& name : c.list_tools()) {
                const auto* t = c.find_tool(name);
                os << "  " << name;
                if (t && !t->description.empty()) os << "  -- " << t->description;
                os << "\n";
            }
            return make_ok(os.str());
        }
    });

    c.register_tool({
        "echo",
        "echo arguments back",
        [](const Intent& i) -> ToolResult {
            return make_ok(join_args(i.args));
        }
    });

    c.register_tool({
        "now",
        "current local time in ISO-8601",
        [](const Intent&) -> ToolResult {
            return make_ok(now_iso());
        }
    });

    c.register_tool({
        "pwd",
        "print current working directory",
        [](const Intent&) -> ToolResult {
            std::error_code ec;
            auto p = std::filesystem::current_path(ec);
            if (ec) return make_err(ec.message());
            return make_ok(p.string());
        }
    });

    c.register_tool({
        "ls",
        "list directory  (usage: ls [path])",
        [](const Intent& i) -> ToolResult {
            namespace fs = std::filesystem;
            const fs::path p = i.args.empty() ? fs::current_path() : fs::path(i.args[0]);
            std::error_code ec;
            if (!fs::exists(p, ec)) return make_err("no such path: " + p.string());
            if (!fs::is_directory(p, ec)) {
                std::ostringstream os;
                os << p.filename().string() << "  (" << fs::file_size(p, ec) << " bytes)";
                return make_ok(os.str());
            }
            std::ostringstream os;
            for (const auto& entry : fs::directory_iterator(p, ec)) {
                const bool dir = entry.is_directory(ec);
                os << (dir ? "[d] " : "[f] ") << entry.path().filename().string();
                if (!dir) {
                    auto sz = entry.file_size(ec);
                    if (!ec) os << "  (" << sz << " bytes)";
                }
                os << "\n";
            }
            return make_ok(os.str());
        }
    });

    c.register_tool({
        "cat",
        "read a file  (usage: cat <path>, max 4 MB)",
        [](const Intent& i) -> ToolResult {
            namespace fs = std::filesystem;
            if (i.args.empty()) return make_err("usage: cat <path>");
            const fs::path p(i.args[0]);
            std::error_code ec;
            if (!fs::exists(p, ec)) return make_err("no such file: " + p.string());
            if (fs::is_directory(p, ec)) return make_err("is a directory: " + p.string());
            const auto sz = fs::file_size(p, ec);
            if (sz > kMaxReadBytes) {
                return make_err("file too large (" + std::to_string(sz) +
                                " bytes; cap is " + std::to_string(kMaxReadBytes) + ")");
            }
            std::ifstream is(p, std::ios::binary);
            if (!is) return make_err("cannot open: " + p.string());
            std::ostringstream os;
            os << is.rdbuf();
            return make_ok(os.str());
        }
    });

    c.register_tool({
        "stat",
        "file metadata  (usage: stat <path>)",
        [](const Intent& i) -> ToolResult {
            namespace fs = std::filesystem;
            if (i.args.empty()) return make_err("usage: stat <path>");
            const fs::path p(i.args[0]);
            std::error_code ec;
            if (!fs::exists(p, ec)) return make_err("no such path: " + p.string());
            std::ostringstream os;
            os << "path : " << p.string() << "\n";
            os << "type : " << (fs::is_directory(p, ec) ? "directory" :
                                fs::is_regular_file(p, ec) ? "file" : "other") << "\n";
            if (fs::is_regular_file(p, ec)) {
                os << "size : " << fs::file_size(p, ec) << " bytes\n";
            }
            return make_ok(os.str());
        }
    });

    c.register_tool({
        "write",
        "write a file  (usage: write <path> \"<content>\")",
        [](const Intent& i) -> ToolResult {
            namespace fs = std::filesystem;
            if (i.args.size() < 2) return make_err("usage: write <path> <content>");
            const fs::path p(i.args[0]);
            std::error_code ec;
            if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
            std::ofstream os(p, std::ios::binary | std::ios::trunc);
            if (!os) return make_err("cannot open for write: " + p.string());
            const std::string content = join_args(i.args, 1);
            os.write(content.data(), static_cast<std::streamsize>(content.size()));
            os.close();
            return make_ok("wrote " + std::to_string(content.size()) +
                           " bytes to " + p.string());
        }
    });
}

void register_memory_tools(Carapace& c,
                           khora::lattice::Lattice& memory,
                           khora::lexicon::Lexicon* lex) {
    using khora::lattice::Glyph;

    auto encode = [lex](const std::string& s) -> Glyph {
        return lex ? lex->glyph_for(s) : Glyph::from_hash(s);
    };

    c.register_tool({
        "memorize",
        "store a label as a glyph in the Lattice  (usage: memorize <label>)",
        [&memory, encode](const Intent& i) -> ToolResult {
            if (i.args.empty()) return make_err("usage: memorize <label>");
            const std::string& label = i.args[0];
            memory.store(label, encode(label));
            return make_ok("memorized: " + label + "  (lattice size = " +
                           std::to_string(memory.size()) + ")");
        }
    });

    c.register_tool({
        "recall",
        "confirm a label is in the Lattice and show its glyph stats",
        [&memory](const Intent& i) -> ToolResult {
            if (i.args.empty()) return make_err("usage: recall <label>");
            const auto g = memory.recall(i.args[0]);
            if (!g) return make_err("not in lattice: " + i.args[0]);
            std::ostringstream os;
            os << "recalled: " << i.args[0]
               << "  popcount=" << g->popcount()
               << "  density=" << g->density();
            return make_ok(os.str());
        }
    });

    c.register_tool({
        "query",
        "find lattice glyphs nearest to a token probe  (usage: query <text> [k])",
        [&memory, encode](const Intent& i) -> ToolResult {
            if (i.args.empty()) return make_err("usage: query <text> [k]");
            std::size_t k = 3;
            if (i.args.size() >= 2) {
                try { k = static_cast<std::size_t>(std::stoul(i.args[1])); }
                catch (...) {}
            }
            const Glyph probe = encode(i.args[0]);
            const auto matches = memory.query(probe, k);
            if (matches.empty()) return make_ok("no matches (lattice empty)");
            std::ostringstream os;
            os << "top-" << matches.size() << " matches for \"" << i.args[0] << "\":\n";
            for (std::size_t j = 0; j < matches.size(); ++j) {
                os << "  " << (j + 1) << ". " << matches[j].label
                   << "  hamming=" << matches[j].hamming
                   << "  sim=" << matches[j].similarity << "\n";
            }
            return make_ok(os.str());
        }
    });
}

void register_cortex_tools(Carapace& c,
                           khora::cortex::PredictiveColumn& cortex,
                           khora::lexicon::Lexicon* lex) {
    using khora::lattice::Glyph;

    auto encode = [lex](const std::string& s) -> Glyph {
        return lex ? lex->glyph_for(s) : Glyph::from_hash(s);
    };

    c.register_tool({
        // Renamed from "learn". register_cortex_tools runs before khora_main's
        // inline registrations, so a later "learn" -- an EXPERIMENTAL predictive
        // trainer that refuses to run without 'confirm' and documents itself as
        // degrading generalisation -- was silently overwriting this one. A
        // working tool was unreachable behind a gated one. The name is taken
        // from this tool's own description.
        "feed",
        "feed a text token into the cortex  (usage: feed <text>)",
        [&cortex, encode](const Intent& i) -> ToolResult {
            if (i.args.empty()) return make_err("usage: feed <text>");
            const auto r = cortex.step(encode(i.args[0]));
            std::ostringstream os;
            os << "fed \"" << i.args[0] << "\"  sim=" << r.similarity
               << "  novel=" << (r.novel_context ? "yes" : "no")
               << "  obs=" << cortex.observations()
               << "  recent_acc=" << cortex.recent_accuracy();
            return make_ok(os.str());
        }
    });

    c.register_tool({
        "predict",
        "show cortex's next-step prediction stats",
        [&cortex](const Intent&) -> ToolResult {
            const auto g = cortex.predict();
            std::ostringstream os;
            os << "prediction popcount=" << g.popcount()
               << "  density=" << g.density()
               << "  associations=" << cortex.associations()
               << "  recent_acc=" << cortex.recent_accuracy();
            return make_ok(os.str());
        }
    });

    c.register_tool({
        "cortex_stats",
        "summarize the predictive cortex column",
        [&cortex](const Intent&) -> ToolResult {
            std::ostringstream os;
            os << "Cortex column:\n"
               << "  observations  : " << cortex.observations() << "\n"
               << "  associations  : " << cortex.associations() << "\n"
               << "  recent_accuracy: " << cortex.recent_accuracy() << "\n";
            return make_ok(os.str());
        }
    });

    c.register_tool({
        "train",
        "train cortex (and lexicon if wired) on a text file  (usage: train <path> [per_char|per_word] [max_tokens])",
        [&cortex, lex, encode](const Intent& i) -> ToolResult {
            namespace fs = std::filesystem;
            if (i.args.empty()) return make_err("usage: train <path> [per_char|per_word] [max_tokens]");
            const fs::path p(i.args[0]);
            std::error_code ec;
            if (!fs::exists(p, ec))         return make_err("no such file: " + p.string());
            if (fs::is_directory(p, ec))    return make_err("is a directory: " + p.string());

            const bool per_word = (i.args.size() >= 2 && i.args[1] == "per_word");
            std::size_t max_tokens = 20000;
            if (i.args.size() >= 3) {
                try { max_tokens = static_cast<std::size_t>(std::stoul(i.args[2])); }
                catch (...) {}
            }

            std::ifstream is(p, std::ios::binary);
            if (!is) return make_err("cannot open: " + p.string());
            std::ostringstream buf;
            buf << is.rdbuf();
            const std::string text = buf.str();

            const auto t0 = std::chrono::steady_clock::now();
            std::size_t fed = 0;
            double      acc_before = cortex.recent_accuracy();
            std::size_t lex_pairs = 0;

            if (per_word) {
                // Tokenize once with the lexicon's tokenizer, feed cortex
                // word-by-word, and ALSO expose the lexicon to the same
                // stream so cooccurrence semantics builds while predictive
                // memory does.
                auto tokens = khora::lexicon::tokenize(text);
                if (tokens.size() > max_tokens) tokens.resize(max_tokens);
                for (const auto& w : tokens) {
                    cortex.step(encode(w));
                    ++fed;
                }
                if (lex) lex_pairs = lex->expose_sequence(tokens, 3);
            } else {
                for (char c : text) {
                    if (fed >= max_tokens) break;
                    char tk[2] = { c, '\0' };
                    cortex.step(encode(tk));
                    ++fed;
                }
                // Even in per_char mode, expose lexicon to the same text
                // at word granularity so semantic drift still accrues.
                if (lex) lex_pairs = lex->expose_text(text, 3);
            }
            const auto t1 = std::chrono::steady_clock::now();
            const double secs = std::chrono::duration<double>(t1 - t0).count();
            const double acc_after = cortex.recent_accuracy();

            std::ostringstream os;
            os << "trained on " << fed << (per_word ? " words" : " chars")
               << " from " << p.string() << "\n"
               << "  duration         : " << secs << "s ("
               << (secs > 0 ? static_cast<double>(fed) / secs : 0.0) << " tokens/s)\n"
               << "  recent_accuracy  : " << acc_before << " -> " << acc_after << "\n"
               << "  cortex assoc     : " << cortex.associations() << "\n"
               << "  cortex obs       : " << cortex.observations();
            if (lex) {
                os << "\n  lexicon vocab    : " << lex->vocabulary_size()
                   << "\n  lexicon obs      : " << lex->total_observations()
                   << "  (+" << lex_pairs << " this call)";
            }
            return make_ok(os.str());
        }
    });
}

void register_lexicon_tools(Carapace& c, khora::lexicon::Lexicon& lex) {
    c.register_tool({
        "lex_stats",
        "show lexicon vocabulary size and total observations",
        [&lex](const Intent&) -> ToolResult {
            std::ostringstream os;
            os << "Lexicon:\n"
               << "  vocabulary    : " << lex.vocabulary_size() << " tokens\n"
               << "  observations  : " << lex.total_observations();
            return make_ok(os.str());
        }
    });

    c.register_tool({
        "lex_sim",
        "semantic similarity between two tokens  (usage: lex_sim <a> <b>)",
        [&lex](const Intent& i) -> ToolResult {
            if (i.args.size() < 2) return make_err("usage: lex_sim <a> <b>");
            const double s = lex.similarity(i.args[0], i.args[1]);
            std::ostringstream os;
            os << i.args[0] << " ~ " << i.args[1] << "  sim=" << s
               << "  (a_exp=" << lex.exposures_for(i.args[0])
               << ", b_exp=" << lex.exposures_for(i.args[1]) << ")";
            return make_ok(os.str());
        }
    });

    c.register_tool({
        "lex_expose",
        "expose lexicon to a text  (usage: lex_expose <text> [window])",
        [&lex](const Intent& i) -> ToolResult {
            if (i.args.empty()) return make_err("usage: lex_expose <text> [window]");
            std::size_t window = 3;
            if (i.args.size() >= 2) {
                try { window = static_cast<std::size_t>(std::stoul(i.args[1])); }
                catch (...) {}
            }
            const std::size_t pairs = lex.expose_text(i.args[0], window);
            std::ostringstream os;
            os << "exposed: +" << pairs << " pairs  (vocab=" << lex.vocabulary_size() << ")";
            return make_ok(os.str());
        }
    });
}

void register_soma_tools(Carapace& c, khora::soma::SomaNexus& soma) {
    using khora::soma::Drive;
    using khora::soma::drive_name;
    using khora::soma::kDriveCount;

    auto drive_from_name = [](const std::string& s) -> int {
        for (std::size_t i = 0; i < kDriveCount; ++i) {
            if (s == drive_name(static_cast<Drive>(i))) return static_cast<int>(i);
        }
        return -1;
    };

    c.register_tool({
        "mood",
        "print all drive strengths",
        [&soma](const Intent&) -> ToolResult {
            const auto snap = soma.snapshot();
            std::ostringstream os;
            os << "Soma snapshot:\n";
            for (std::size_t i = 0; i < kDriveCount; ++i) {
                os << "  " << drive_name(static_cast<Drive>(i)) << " = " << snap[i] << "\n";
            }
            return make_ok(os.str());
        }
    });

    c.register_tool({
        "stimulate",
        "adjust a drive  (usage: stimulate <drive_name> <delta>)",
        [&soma, drive_from_name](const Intent& i) -> ToolResult {
            if (i.args.size() < 2) return make_err("usage: stimulate <drive_name> <delta>");
            const int idx = drive_from_name(i.args[0]);
            if (idx < 0) return make_err("unknown drive: " + i.args[0]);
            double delta = 0.0;
            try { delta = std::stod(i.args[1]); }
            catch (...) { return make_err("bad delta: " + i.args[1]); }
            soma.stimulate(static_cast<Drive>(idx), delta);
            std::ostringstream os;
            os << "stimulated " << i.args[0] << " by " << delta
               << " -> " << soma.strength(static_cast<Drive>(idx));
            return make_ok(os.str());
        }
    });
}

void register_cogitator_tools(Carapace& c, khora::cogitator::Cogitator& cog) {
    c.register_tool({
        "think",
        "run a Morphic Cogitator resolve-cycle on the given input",
        [&cog](const Intent& i) -> ToolResult {
            std::string stim;
            for (std::size_t k = 0; k < i.args.size(); ++k) {
                if (k > 0) stim.push_back(' ');
                stim += i.args[k];
            }
            const auto t = cog.think(stim);
            std::ostringstream os;
            os << "Thought  stimulus=\"" << t.stimulus << "\"  attempts=" << t.attempts << "\n"
               << "  tokens       : ";
            for (const auto& tok : t.tokens) os << tok << ' ';
            os << "\n  resonances   :\n";
            if (t.resonances.empty()) {
                os << "    (none)\n";
            } else {
                for (std::size_t k = 0; k < t.resonances.size(); ++k) {
                    os << "    " << (k + 1) << ". " << t.resonances[k].label
                       << "  sim=" << t.resonances[k].similarity << "\n";
                }
            }
            os << "  confidence   : " << t.confidence << "\n"
               << "  novel        : " << (t.novel ? "yes" : "no") << "\n"
               << "  learned      : " << (t.learned_this_cycle ? "yes" : "no") << "\n"
               << "  valence      : " << t.valence << "\n"
               << "  resolution   : "
               << (t.chosen_label.empty() ? std::string{"(provisional hypothesis formed)"}
                                          : t.chosen_label);
            return {true, os.str(), ""};
        }
    });
    c.register_tool({
        "cogitator_stats",
        "show how much Khora has thought and learned",
        [&cog](const Intent&) -> ToolResult {
            std::ostringstream os;
            os << "Cogitator:\n"
               << "  thoughts_completed   : " << cog.thoughts_completed() << "\n"
               << "  novel_thoughts       : " << cog.novel_thoughts() << "\n"
               << "  hypotheses_formed    : " << cog.hypotheses_formed() << "\n"
               << "  total_attempts       : " << cog.total_attempts() << "\n"
               << "  deliberations        : " << cog.deliberations() << "\n"
               << "  resonance_k          : " << cog.resonance_k() << "\n"
               << "  novelty_threshold    : " << cog.novelty_threshold() << "\n"
               << "  max_resolve_attempts : " << cog.max_resolve_attempts();
            return {true, os.str(), ""};
        }
    });

    c.register_tool({
        "deliberate",
        "think non-linearly: refract the input into parallel competing facets",
        [&cog](const Intent& i) -> ToolResult {
            std::string stim;
            for (std::size_t k = 0; k < i.args.size(); ++k) { if (k) stim.push_back(' '); stim += i.args[k]; }
            const auto d = cog.deliberate(stim);
            std::ostringstream os;
            os << "Deliberation  stimulus=\"" << d.stimulus << "\"  ("
               << d.facets.size() << " parallel facets)\n";
            for (int idx = 0; idx < static_cast<int>(d.facets.size()); ++idx) {
                const auto& f = d.facets[idx];
                os << "  " << (idx == d.winner ? "* " : "  ")
                   << khora::cogitator::lens_name(f.lens)
                   << "  conf=" << f.confidence
                   << "  valence=" << f.valence
                   << "  -> " << (f.label.empty() ? std::string{"(novel)"} : f.label) << "\n";
            }
            os << "  coherence=" << d.coherence << "  entropy=" << d.entropy << "\n";
            os << "  WINNER: " << khora::cogitator::lens_name(d.facets[d.winner].lens)
               << "  resolution: "
               << (d.chosen_label.empty() ? std::string{"(emergent — collapsed coalition)"} : d.chosen_label);
            return {true, os.str(), ""};
        }
    });

    c.register_tool({
        "ruminate",
        "follow a recursive train of thought  (usage: ruminate <text> [depth])",
        [&cog](const Intent& i) -> ToolResult {
            if (i.args.empty()) return make_err("usage: ruminate <text> [depth]");
            std::size_t depth = 6;
            std::size_t title_args = i.args.size();
            if (i.args.size() >= 2) {
                try { depth = static_cast<std::size_t>(std::stoul(i.args.back())); title_args = i.args.size() - 1; }
                catch (...) {}
            }
            std::string stim;
            for (std::size_t k = 0; k < title_args; ++k) { if (k) stim.push_back(' '); stim += i.args[k]; }
            const auto r = cog.ruminate(stim, depth);
            std::ostringstream os;
            os << "Train of thought from \"" << r.seed << "\":\n  ";
            for (std::size_t k = 0; k < r.train.size(); ++k) {
                if (k) os << " -> ";
                os << r.train[k];
            }
            os << "\n  " << (r.converged ? "converged on attractor: " : "ended at: ")
               << r.conclusion;
            return make_ok(os.str());
        }
    });
}

} // namespace khora::carapace
