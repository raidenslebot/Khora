// THE RUNNING SYSTEM CAN NOW WRITE PROGRAMS.
//
// Khora has ninety-four tools. It reasons, abstracts, curates its own reading,
// acts on the machine, writes files, and measures the yield of its own thinking.
// Not one of those tools could write a program, and `khora_main.cpp` did not
// mention techne, ribosome, synapse or crucible even once -- an entire program
// synthesiser with fourteen language backends, certified answers and a learned
// library, sitting beside a system that could not reach it.
//
// That is the same failure this repo already has on record for Sdr and
// TemporalMemory: built, tested, benchmarked, and referenced by nothing. It is
// worth more than a benchmark number, because a capability the system cannot
// invoke is not a capability the system has.
//
// THREE TOOLS.
//
//   synth   -- give it examples, it returns a program that satisfies them and
//              carries a proof state, plus the source in any of fourteen
//              languages
//   synth_library -- what it has learned to reuse, and from where
//   synth_forget  -- start the vocabulary over
//
// The library PERSISTS. It is loaded from disk on first use and written back
// after every successful synthesis, so what Khora learns about programming in
// one session is available in the next. Without that the tool would relearn the
// same primitives forever, which is what every benchmark in this tree did until
// Library::save existed.

#include "khora/carapace/techne_tools.hpp"

#include "khora/techne/techne.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace khora::carapace {
namespace {

using khora::techne::Value;

// Local, matching builtin_tools.cpp. Two lines is cheaper than a shared header
// for something this small.
ToolResult make_ok(std::string output) { return {true, std::move(output), ""}; }
ToolResult make_err(std::string error) { return {false, "", std::move(error)}; }

// "1,2,3" -> {1,2,3}. An empty string is the empty list, which is a legitimate
// input and the one most likely to break a synthesised program.
bool parse_list(const std::string& s, Value& out) {
    out.clear();
    std::string tok;
    std::istringstream in(s);
    while (std::getline(in, tok, ',')) {
        if (tok.empty()) continue;
        try {
            out.push_back(static_cast<std::int64_t>(std::stoll(tok)));
        } catch (...) {
            return false;
        }
    }
    return true;
}

std::string show(const Value& v) {
    std::string t = "[";
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) t += ",";
        t += std::to_string(v[i]);
    }
    return t + "]";
}

khora::techne::Lang lang_from(const std::string& n) {
    using L = khora::techne::Lang;
    if (n == "cpp" || n == "c++") return L::Cpp;
    if (n == "py" || n == "python") return L::Python;
    if (n == "js" || n == "javascript") return L::JavaScript;
    if (n == "ts" || n == "typescript") return L::TypeScript;
    if (n == "rs" || n == "rust") return L::Rust;
    if (n == "go") return L::Go;
    if (n == "java") return L::Java;
    if (n == "cs" || n == "csharp") return L::CSharp;
    if (n == "rb" || n == "ruby") return L::Ruby;
    if (n == "lua") return L::Lua;
    if (n == "hs" || n == "haskell") return L::Haskell;
    if (n == "swift") return L::Swift;
    if (n == "kt" || n == "kotlin") return L::Kotlin;
    if (n == "php") return L::Php;
    return L::Python;
}

const char* proof_word(khora::techne::Proof p) {
    using P = khora::techne::Proof;
    switch (p) {
        case P::None:        return "none";
        case P::Tested:      return "TESTED (fits the examples; untrusted beyond them)";
        case P::Generalised: return "GENERALISED (also correct on held-out cases it never saw)";
        case P::Exhaustive:  return "EXHAUSTIVE";
        case P::Verified:    return "VERIFIED";
    }
    return "?";
}

// One library for the process, loaded lazily and written back after anything is
// learned. A budget of 128 is above the 96 the ascent settles on and well below
// the point where the extra level-0 candidates start costing more than the
// vocabulary returns -- that trade is measured in the README.
khora::techne::Library& shared_library(bool* loaded_out = nullptr) {
    static khora::techne::Library lib(128);
    static bool tried = false;
    static bool loaded = false;
    if (!tried) {
        tried = true;
        std::error_code ec;
        std::filesystem::create_directories("data", ec);
        loaded = lib.load("data/techne_library.txt");
    }
    if (loaded_out != nullptr) *loaded_out = loaded;
    return lib;
}

} // namespace

void register_techne_tools(Carapace& c) {
    c.register_tool({
        "synth",
        "Khora writes a PROGRAM from examples and proves it on cases it never saw  "
        "(usage: synth <lang> <in>=<out> <in>=<out> ...  e.g. synth python 1,2,3=3,2,1 4,5=5,4)",
        [](const Intent& i) -> ToolResult {
            if (i.args.size() < 2) {
                return make_err("usage: synth <lang> <in>=<out> ...   "
                                "lists are comma-separated, e.g. 1,2,3=3,2,1");
            }
            const std::string langname = i.args[0];
            khora::techne::Spec spec;
            spec.name = "synth";

            // The LAST example is held out. A program that fits every example it
            // was shown proves nothing -- this system's own measurements put 249
            // of 2,000 such programs wrong on the first input nobody looked at.
            // Holding one back is the cheapest honest check available, and the
            // proof state reported below says which side of it the answer fell.
            std::vector<std::pair<Value, Value>> pairs;
            for (std::size_t k = 1; k < i.args.size(); ++k) {
                const std::string& a = i.args[k];
                const std::size_t eq = a.find('=');
                if (eq == std::string::npos) {
                    return make_err("example '" + a + "' has no '='; write <in>=<out>");
                }
                Value in, out;
                if (!parse_list(a.substr(0, eq), in) || !parse_list(a.substr(eq + 1), out)) {
                    return make_err("example '" + a + "' is not two comma-separated integer lists");
                }
                pairs.emplace_back(std::move(in), std::move(out));
            }
            if (pairs.empty()) return make_err("no examples given");

            for (std::size_t k = 0; k < pairs.size(); ++k) {
                khora::techne::Case cs(pairs[k].first, pairs[k].second);
                if (k + 1 == pairs.size() && pairs.size() > 1) spec.holdout.push_back(cs);
                else spec.cases.push_back(cs);
            }

            bool loaded = false;
            khora::techne::Library& lib = shared_library(&loaded);
            const khora::techne::BuildResult b =
                khora::techne::solve_one(spec, 20000, &lib);

            std::ostringstream os;
            if (!b.recipe.found) {
                os << "no program found that fits those examples.\n"
                   << "  the search is bounded; more examples, or simpler ones, often help.";
                return make_ok(os.str());
            }

            os << "  " << b.recipe.render() << "\n"
               << "  proof   : " << proof_word(b.proof) << "\n"
               << "  cases   : " << b.cases_passed << "/" << b.cases_total;
            if (b.holdout_total > 0) {
                os << "   held out: " << b.holdout_passed << "/" << b.holdout_total;
            }
            os << "\n  size    : " << b.recipe.size() << " operations, arity "
               << b.recipe.arity() << "\n";

            // Emitted source is INLINED against the library, so what is printed
            // depends on nothing that lives only in this process.
            const khora::techne::Recipe standalone =
                khora::techne::inline_calls(b.recipe, lib);
            std::size_t lines = 0;
            const std::string src = khora::techne::emit(
                standalone, lang_from(langname), "khora_fn", &lines, nullptr);
            os << "\n" << src;

            // Only a GENERALISED answer is worth remembering. A merely tested one
            // is wrong somewhere nobody looked, and admitting it would put a
            // primitive that lies underneath every later search.
            if (b.proof == khora::techne::Proof::Generalised) {
                lib.admit_recipe("synth" + std::to_string(lib.size()), b.recipe, lib.size());
                lib.prune();
                lib.save("data/techne_library.txt");
            }
            return make_ok(os.str());
        }
    });

    c.register_tool({
        "synth_library",
        "what Khora has learned to reuse when it writes programs",
        [](const Intent&) -> ToolResult {
            bool loaded = false;
            khora::techne::Library& lib = shared_library(&loaded);
            std::ostringstream os;
            os << "  " << lib.size() << " learned primitives"
               << (loaded ? " (carried over from a previous session)" : " (nothing on disk yet)")
               << ", " << lib.evicted() << " evicted under budget\n";
            for (std::size_t k = 0; k < lib.size() && k < 24; ++k) {
                os << "    " << lib.at(k).name << "  " << lib.at(k).recipe.render() << "\n";
            }
            if (lib.size() > 24) os << "    ... and " << (lib.size() - 24) << " more\n";
            return make_ok(os.str());
        }
    });

    c.register_tool({
        "synth_forget",
        "discard the learned programming vocabulary and start over",
        [](const Intent&) -> ToolResult {
            std::error_code ec;
            std::filesystem::remove("data/techne_library.txt", ec);
            return make_ok("the programming library on disk is gone; it rebuilds "
                           "from the next synthesis onward (this process keeps "
                           "what it already had in memory)");
        }
    });
}

} // namespace khora::carapace
