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
#include "khora/chiasm/chiasm.hpp"

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

// PROGRAMS REMEMBERED BY THE SHAPE OF THE PROBLEM THEY SOLVE.
//
// The learned Library makes each search CHEAPER by widening the vocabulary.
// This skips the search entirely, which is a different mechanism, and the two
// run together here rather than competing.
//
// Measured on a stream of 120 tasks in compound_bench: search every time took
// 3,362 ms and remember-first took 665 ms for the identical 120 solved, with 21
// wrong guesses caught by execution and paid for in full. Cost per task fell
// from 17.0 ms in the first twenty to 1.9 ms once the memory had filled.
//
// A WRONG GUESS IS FREE TO BE WRONG, which is the only reason this is safe. The
// retrieved program is RUN on the caller's own examples before it is offered,
// and if it fails the search runs exactly as it would have. Retrieval never
// changes what is returned, only how long it took.
constexpr const char* kProgramMemoryDir = "data/chiasm_archive/programs";

struct ProgramMemory {
    khora::chiasm::Chiasm              mem;
    // ITS OWN RECIPE STORE, and the first version did not have one.
    //
    // A glyph is not a program: the chiasm stores WHICH program, so something has
    // to hold the programs themselves. I keyed them off the techne Library and it
    // failed on the first restart -- the Library is BUDGETED and prunes, so
    // `synth5` was bound in the memory and evicted from the archive before it was
    // written, and the next process retrieved a label it could not resolve. It
    // then searched, silently, and looked like it was simply not remembering.
    //
    // This store is never pruned. That is a real cost -- it grows without bound
    // where the Library deliberately does not -- and it is the right trade here
    // because the Library's budget exists to keep the SEARCH SPACE small, and
    // nothing in this store is ever a search candidate.
    khora::techne::Library             store{1000000};
    std::vector<khora::techne::Recipe> recipes;
    std::vector<std::string>           labels;
    bool                               loaded = false;
};

ProgramMemory& program_memory() {
    static ProgramMemory pm;
    if (!pm.loaded) {
        pm.loaded = true;
        pm.mem.load(kProgramMemoryDir);
        pm.store.load(std::string(kProgramMemoryDir) + "/recipes.txt");
        for (std::size_t i = 0; i < pm.store.size(); ++i) {
            pm.recipes.push_back(pm.store.at(i).recipe);
            pm.labels.push_back(pm.store.at(i).name);
        }
    }
    return pm;
}

const khora::lattice::Glyph& tt_pos_glyph(std::size_t i) {
    static std::vector<khora::lattice::Glyph> tab;
    while (tab.size() <= i)
        tab.push_back(khora::lattice::Glyph::from_hash("@" + std::to_string(tab.size())));
    return tab[i];
}
const khora::lattice::Glyph& tt_num_glyph(std::int64_t v) {
    static std::vector<khora::lattice::Glyph> pos, neg;
    auto& tab = v < 0 ? neg : pos;
    const std::size_t i = static_cast<std::size_t>(v < 0 ? -v : v);
    while (tab.size() <= i)
        tab.push_back(khora::lattice::Glyph::from_hash(
            (v < 0 ? std::string("n") : std::string("p")) + std::to_string(tab.size())));
    return tab[i];
}

// The encoding capability_bench measured at a +0.557 same-family gap, against
// +0.131 for bundling the examples themselves: describe WHERE each output
// element came from, which is the same for every instance of a rearrangement and
// says nothing about the particular numbers.
khora::lattice::Glyph encode_task(const std::vector<khora::techne::Case>& cs) {
    std::vector<khora::lattice::Glyph> terms;
    for (const auto& c : cs) {
        for (std::size_t j = 0; j < c.out.size(); ++j) {
            std::size_t src = c.in.size();
            for (std::size_t i = 0; i < c.in.size(); ++i)
                if (c.in[i] == c.out[j]) { src = i; break; }
            terms.push_back(khora::lattice::bind(tt_pos_glyph(j),
                            khora::lattice::permute(tt_pos_glyph(src), 1)));
        }
        terms.push_back(khora::lattice::bind(khora::lattice::Glyph::from_hash("#len"),
                        tt_num_glyph(static_cast<std::int64_t>(c.out.size()) -
                                     static_cast<std::int64_t>(c.in.size()))));
    }
    if (terms.empty()) return khora::lattice::Glyph::from_hash("<none>");
    return khora::lattice::bundle(std::span<const khora::lattice::Glyph>(terms));
}

bool runs_clean(const khora::techne::Recipe& r,
                const std::vector<khora::techne::Case>& cs,
                const khora::techne::Library* lib) {
    if (cs.empty()) return false;
    for (const auto& c : cs)
        if (r.apply_n(c.args(), lib) != c.out) return false;
    return true;
}

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

            // --- REMEMBER FIRST ----------------------------------------------
            //
            // Every case, not just the visible ones: the holdout is part of what
            // the caller asked for, and a remembered program has to satisfy all
            // of it or it is not an answer.
            std::vector<khora::techne::Case> all = spec.cases;
            all.insert(all.end(), spec.holdout.begin(), spec.holdout.end());
            ProgramMemory& pm = program_memory();
            const khora::lattice::Glyph task_glyph = encode_task(all);

            std::ostringstream os;
            if (pm.mem.records() > 0) {
                const auto got = pm.mem.recall("task", task_glyph, "prog");
                const auto at = std::find(pm.labels.begin(), pm.labels.end(), got.label);
                if (at != pm.labels.end()) {
                    const auto& rec = pm.recipes[(std::size_t)(at - pm.labels.begin())];
                    if (runs_clean(rec, all, &lib)) {
                        const khora::techne::Recipe standalone =
                            khora::techne::inline_calls(rec, lib);
                        std::size_t lines = 0;
                        const std::string src = khora::techne::emit(
                            standalone, lang_from(langname), "khora_fn", &lines, nullptr);
                        os << "  " << rec.render() << "\n"
                           << "  proof   : REMEMBERED -- this program was derived earlier and\n"
                              "            has just been re-run on every case you gave, all of\n"
                              "            which it passes. No search was needed.\n"
                           << "  from    : " << got.label << "   (similarity "
                           << got.confidence << ", margin " << got.margin << ")\n"
                           << "  size    : " << rec.size() << " operations, arity "
                           << rec.arity() << "\n\n" << src;
                        return make_ok(os.str());
                    }
                    // Wrong guess. It cost one execution; the search now runs
                    // exactly as it would have, and nothing is reported wrongly.
                }
            }

            const khora::techne::BuildResult b =
                khora::techne::solve_one(spec, 20000, &lib);

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
                const std::string label = "synth" + std::to_string(lib.size());
                lib.admit_recipe(label, b.recipe, lib.size());
                lib.prune();
                lib.save("data/techne_library.txt");

                // Bind the answer to the shape of the question, so the next
                // caller with a task like this one skips the search. Only a
                // GENERALISED result is remembered, for the same reason it is the
                // only kind admitted to the library: a merely tested program is
                // wrong somewhere nobody looked.
                const std::string mlabel = "mem" + std::to_string(pm.recipes.size());
                if (pm.store.admit_recipe(mlabel, b.recipe, pm.recipes.size())) {
                    pm.recipes.push_back(b.recipe);
                    pm.labels.push_back(mlabel);
                    pm.mem.remember({
                        {"task", mlabel, task_glyph},
                        {"prog", mlabel, khora::lattice::Glyph::from_hash("prog:" + mlabel)}});
                    pm.mem.save(kProgramMemoryDir);
                    pm.store.save(std::string(kProgramMemoryDir) + "/recipes.txt");
                }
                // A refused admission means this program is already in the store
                // under another label, so the task it came from is already bound
                // to something that solves it. Binding it twice would put two
                // records in the way of every future query for no gain.
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
