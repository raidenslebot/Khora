// DOES THE EMITTED CODE COMPUTE WHAT THE CERTIFIED RECIPE COMPUTES?
//
// This is the only claim this organ makes, and it has been false three times:
// Op::Call deleted from emitted source, the length clamp missing from every
// prelude, and Op::Mov aliased away along with the truncation it carries. Each
// time the certificate stayed attached to a program that was not the one handed
// back, and each time a benchmark reported a throughput figure for it.
//
// So this bench does not check the emitter against itself. It writes real source
// in all fourteen backends, probes for a genuine toolchain, RUNS the ones that
// are here, and diffs stdout against Recipe::apply. Agreement is the only
// evidence that means anything; everything else is the emitter agreeing with the
// emitter.
//
// FOUR OUTCOMES, and only the first is a pass:
//   matches                    ran, and byte-identical to Recipe::apply
//   DIFFERS from line N        ran, and computed something else -- a real defect
//   could not build or run     the toolchain is here and the build failed
//   skipped, no toolchain      nothing ran, and nothing is claimed
//
// The last two are not failures of the emitter and are never counted as passes
// either. Collapsing them into one verdict is how a missing kernel32.lib gets
// reported as the Rust backend computing the wrong answer.
//
// A FIFTH OUTCOME USED TO EXIST AND WAS THE WORST OF THEM. This file ended with
// an unconditional printf reading "ALL FOURTEEN BACKENDS EXECUTED AND
// BYTE-IDENTICAL on this machine". It was earned, once, by hand -- the history
// walks it from nine to twelve to fourteen -- and then frozen into a literal. No
// toolchain was ever invoked from this process, so the line printed the same
// whether the emitter was correct, broken, or deleted. A guarantee that cannot
// be re-run is not a guarantee, and this file of all files should not have
// needed telling.
//
// THE INPUTS ARE CHOSEN TO BREAK IT. Above all, lists LONGER THAN kMaxListLen,
// because that is where the interpreter's per-operation clamp bites and where
// every one of the three defects hid. A suite of short lists would have passed
// throughout.
//
// THE RECIPES ARE CHOSEN THE SAME WAY. [Mov(x)] alone, and Mov followed by an
// operation whose result depends on WHICH 512 elements survive -- Rev is the
// sharp one, because a dropped clamp gives the right LENGTH and the wrong
// CONTENTS, which a length check would miss. Controls containing no Mov at all
// are included so a failure can be attributed to the alias rather than to the
// operation after it.

#include "khora/techne/techne.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#include <iterator>

using namespace khora::techne;

namespace {

struct Named { const char* name; Recipe r; };

Recipe chain(std::initializer_list<Expr> nodes) {
    Recipe r;
    for (const Expr& e : nodes) r.pool.push_back(e);
    r.root = r.pool.size() - 1;
    r.found = true;
    return r;
}

// A LIBRARY, so the paths that reach into one are exercised at all.
//
// Op::Call being DELETED from emitted source is one of the three defects this
// file was written for, and not one of the sixteen named recipes above contains
// a Call -- so the bench that exists because of that defect stopped covering it.
// MapF and FoldF are in the same position, and emission refuses them outright
// when the library is empty, so passing nullptr made that refusal invisible too.
//
// Emission INLINES calls: a certified recipe naming library index 3 has to
// become source that depends on nothing outside itself. That splice is the thing
// under test here, and it cannot be tested without something to splice.
const Library& fuzz_lib() {
    static Library lib = [] {
        Library l(32);
        l.admit_recipe("l_rev",  chain({Expr{Op::Rev,  -1, -1, 0}}), 0);
        l.admit_recipe("l_sort", chain({Expr{Op::Sort, -1, -1, 0}}), 0);
        l.admit_recipe("l_dbl",  chain({Expr{Op::Add,  -1, -1, 0}}), 0);
        l.admit_recipe("l_tail", chain({Expr{Op::Tail, -1, -1, 0}}), 0);
        // Two nodes deep, so the splice has to inline something that is not a
        // single operation.
        l.admit_recipe("l_sr",   chain({Expr{Op::Sort, -1, -1, 0},
                                        Expr{Op::Rev,   0, -1, 0}}), 0);
        return l;
    }();
    return lib;
}
// SIXTEEN HAND-WRITTEN RECIPES CATCH THE BUGS SOMEBODY ALREADY THOUGHT OF.
//
// Each one above exists because a specific defect got through: Op::Call deleted
// from emitted source, the length clamp missing from every prelude, Op::Mov
// aliased away with the truncation it carries. They are regression tests, and a
// regression test cannot find the NEXT defect -- the synthesiser emits arbitrary
// recipes, and sixteen shapes is not the space it draws from.
//
// So: generate well-formed recipes over the WHOLE operation set, by ordinal, so
// an operation added next week is covered without anybody remembering to add it
// here. Recipe::apply is the reference for whatever comes out; the emitted code
// has to agree with it whatever it is.
//
// Call, MapF and FoldF ARE included, with fuzz_lib() supplying the bodies. They
// were excluded while this harness passed nullptr, which left the splice path
// with no coverage at all.
std::vector<Named> fuzz_recipes(std::size_t n, std::uint64_t seed) {
    std::vector<Op> usable;
    for (int i = 0; i < static_cast<int>(Op::kCount); ++i) {
        const Op o = static_cast<Op>(i);
        if (o == Op::Arg) continue;
        usable.push_back(o);
    }

    std::uint64_t st = seed | 1;
    const auto rnd = [&st]() {
        st ^= st << 13; st ^= st >> 7; st ^= st << 17; return st;
    };

    std::vector<Named> v;
    static std::vector<std::string> names;
    names.clear();
    names.reserve(n);
    for (std::size_t f = 0; f < n; ++f) {
        const std::size_t size = 1 + rnd() % 5;
        Recipe r;
        for (std::size_t i = 0; i < size; ++i) {
            Expr e;
            // A literal every so often, since has_lit is honoured separately by
            // evaluation, rendering and emission and has been wrong in one of
            // them before.
            if (i + 1 < size && rnd() % 4 == 0) {
                e.op = Op::Const;
                e.a = -1; e.b = -1;
                e.lit = static_cast<std::int64_t>(rnd() % 200) - 100;
                e.has_lit = true;
            } else {
                e.op = usable[rnd() % usable.size()];
                // -1 is argument 0; anything else must be an EARLIER node, which
                // is what keeps the pool acyclic and the recipe well formed.
                e.a = (i == 0 || rnd() % 3 == 0) ? -1 : static_cast<int>(rnd() % i);
                e.b = (i == 0 || rnd() % 2 == 0) ? -1 : static_cast<int>(rnd() % i);
                // For Call, MapF and FoldF this selects the library BODY, so it
                // is drawn over the library rather than over the constant table.
                const bool names_body = (e.op == Op::Call || e.op == Op::MapF ||
                                         e.op == Op::FoldF || e.op == Op::FoldS);
                e.k = static_cast<std::uint8_t>(
                    rnd() % (names_body ? fuzz_lib().size() : 4));
            }
            r.pool.push_back(e);
        }
        r.root = r.pool.size() - 1;
        r.found = true;
        names.push_back("fuzz" + std::to_string(f));
        v.push_back({names.back().c_str(), r});
    }
    return v;
}
// The arguments a recipe of this arity gets for input i. A second argument is
// the NEXT input rather than a repeat of the first, so a program that silently
// ignores it cannot pass by coincidence.
std::vector<Value> argsfor(const Named& n, const std::vector<Value>& ins, std::size_t i) {
    std::vector<Value> a{ins[i]};
    for (std::size_t k = 1; k < n.r.arity(); ++k) a.push_back(ins[(i + k) % ins.size()]);
    return a;
}

// RUNNING THEM, RATHER THAN SAYING THEY RAN.
//
// This harness used to end with an unconditional printf reading "ALL FOURTEEN
// BACKENDS EXECUTED AND BYTE-IDENTICAL on this machine". It was true when it was
// written -- the git history walks it up from nine to twelve to fourteen -- and
// it was a LITERAL. No toolchain was ever invoked from this process, so the line
// printed identically whether the emitter was correct, broken, or absent, which
// is the exact failure the header of this file objects to.
//
// A guarantee that cannot be re-run is not a guarantee. So: probe for a real
// toolchain, run what is there, diff against reference.txt, and report what
// actually happened -- including "no toolchain", which is an honest answer and
// not a pass.

// Captures stdout so a probe can check WHICH tool answered. `php` on this
// machine is a Python script in Python312/Scripts that prints "comment in php:
// parsing input data", so presence on PATH proves nothing.
// The exit status matters as much as the text. A COMPILER THAT FAILED and a
// PROGRAM THAT DISAGREED are opposite findings -- one is a missing dependency on
// this host, the other is the emitter generating code that computes the wrong
// thing -- and a harness that prints DIFFERS for both is making exactly the
// mistake this file exists to catch. Measured: the Rust backend reported DIFFERS
// while rustc had compiled it cleanly and `link.exe` then failed to find
// kernel32.lib, which says nothing whatever about the emitted Rust.
struct Ran { std::string out; int status = 0; };

Ran capture(const std::string& cmd) {
    Ran r;
#ifdef _WIN32
    FILE* pipe = _popen((cmd + " 2>&1").c_str(), "r");
#else
    FILE* pipe = popen((cmd + " 2>&1").c_str(), "r");
#endif
    if (pipe == nullptr) { r.status = -1; return r; }
    char buf[512];
    while (std::fgets(buf, sizeof buf, pipe) != nullptr) r.out += buf;
#ifdef _WIN32
    r.status = _pclose(pipe);
#else
    r.status = pclose(pipe);
#endif
    return r;
}

// CRLF and a trailing newline are not disagreements about what was computed.
std::string normalise(const std::string& t) {
    std::string o;
    o.reserve(t.size());
    for (const char c : t) if (c != 13) o += c;
    while (!o.empty() && o.back() == 10) o.pop_back();
    return o;
}

std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return normalise(std::string(std::istreambuf_iterator<char>(f),
                                 std::istreambuf_iterator<char>()));
}

struct Runner {
    Lang        l;
    std::string probe;      // a command that only a genuine toolchain answers
    std::string expect;     // ...and what its answer must contain, or equal
    std::string run;        // run the emitted file, printing the same lines
    bool        exact = false;   // require equality, for probes whose expected
                                 // answer is short enough to appear by accident
};

// A SHELL SAYING "'javac' is not recognized" CONTAINS THE WORD javac, so a
// substring probe for the tool's own name passes on every machine that does not
// have it. Four backends reported DIFFERS on that basis before this existed,
// which reads as an emitter defect and is a harness defect.
bool have_toolchain(const std::string& answer, const std::string& expect, bool exact) {
    static const char* absent[] = {"is not recognized", "not found", "cannot find",
                                   "No such file", "not be found", "Could not execute"};
    for (const char* a : absent) if (answer.find(a) != std::string::npos) return false;
    if (exact) {
        std::string t = answer;
        while (!t.empty() && (t.back() == 10 || t.back() == 13 || t.back() == 32)) t.pop_back();
        return t == expect;
    }
    return answer.find(expect) != std::string::npos;
}

// One entry per backend. A missing toolchain is reported, never inferred away.
//
// Locally built executables are invoked as `.\name.exe`. This host sets
// NoDefaultCurrentDirectoryInExePath=1, so cmd refuses to run a binary sitting
// in the working directory by bare name -- it builds, and then the run step
// says "is not recognized as an internal or external command", which the
// harness would otherwise file under "could not build".
std::vector<Runner> runners() {
    return {
        {Lang::Python,     "python -V",          "Python",  "python emitted.py"},
        {Lang::JavaScript, "node -v",            "v",       "node emitted.js"},
        {Lang::TypeScript, "data\\toolchains\\node_modules\\.bin\\tsc.cmd --version", "Version",
                           "data\\toolchains\\node_modules\\.bin\\tsc.cmd emitted.ts --outFile ts_out.js "
                           "--target es2020 --lib es2020,dom 2>nul && node ts_out.js"},
        {Lang::Go,         "go version",         "go1",     "go run emitted.go"},
        {Lang::Rust,       "rustc --version",    "rustc",
                           "rustc -O -o emitted_rs.exe emitted.rs 2>nul && .\\emitted_rs.exe"},
        {Lang::Cpp,        "cl",                 "Microsoft",
                           "cl /nologo /EHsc /std:c++20 /O2 /Fe:emitted_cpp.exe emitted.cpp "
                           ">nul 2>nul && .\\emitted_cpp.exe"},
        // dotnet will not run a bare .cs file, so the emitted source goes into a
        // scaffolded console project. tools/toolchains.ps1 creates it.
        {Lang::CSharp,     "dotnet --list-sdks", "sdk",
                           "copy /y emitted.cs data\\toolchains\\cs\\Program.cs >nul && "
                           "dotnet run --project data\\toolchains\\cs -c Release -v q --nologo"},
        // NOT the `php` on PATH, which on this host is a Python script that
        // prints "comment in php: parsing input data".
        {Lang::Php,        "data\\toolchains\\php\\php.exe -v", "PHP",
                           "data\\toolchains\\php\\php.exe emitted.php"},
        {Lang::Java,       "data\\toolchains\\jdk21\\bin\\javac.exe -version", "javac",
                           "data\\toolchains\\jdk21\\bin\\javac.exe -d data\\toolchains\\javaout Main.java "
                           "2>nul && data\\toolchains\\jdk21\\bin\\java.exe -cp data\\toolchains\\javaout Main"},
        {Lang::Kotlin,     "tools\\run_kotlin.cmd", "kotlinc",
                           "tools\\run_kotlin.cmd emitted.kt"},
        {Lang::Swift,      "tools\\run_swift.cmd", "Swift",
                           "tools\\run_swift.cmd main.swift"},
        {Lang::Haskell,    "data\\toolchains\\ghc966\\bin\\ghc.exe --version", "Glasgow",
                           "data\\toolchains\\ghc966\\bin\\runghc.exe emitted.hs"},
        // Real Lua 5.4 and real CRuby, compiled to WebAssembly and run in
        // process by node -- the implementations, not reimplementations of
        // them. No lua.exe or ruby.exe exists on this host.
        {Lang::Lua,        "node --no-warnings tools\\run_lua.js", "Lua",
                           "node --no-warnings tools\\run_lua.js emitted.lua"},
        {Lang::Ruby,       "node --no-warnings tools\\run_ruby.js", "ruby",
                           "node --no-warnings tools\\run_ruby.js emitted.rb"},
    };
}
std::vector<Named> recipes() {
    std::vector<Named> v;

    // TWO-ARGUMENT RECIPES. Every program this system could express used to be a
    // unary function of one list, so nothing here ever exercised a second
    // parameter -- and the emitter's op_fn defaults to kh_id, which would have
    // bound argument 1 to argument 0 in source that compiles and reads correctly.
    // Argument 1 is INPUTS[(i+1) % n], so a program that ignores it cannot pass
    // by coincidence.
    v.push_back({"two_cat",   chain({Expr{Op::Mov, -1, -1, 0}, Expr{Op::Arg, -1, -1, 1},
                                     Expr{Op::Append, 0, 1, 0}})});
    v.push_back({"two_gt",    chain({Expr{Op::Mov, -1, -1, 0}, Expr{Op::Arg, -1, -1, 1},
                                     Expr{Op::Gt, 0, 1, 0}})});
    v.push_back({"two_rev2",  chain({Expr{Op::Arg, -1, -1, 1}, Expr{Op::Rev, 0, -1, 0}})});

    // The three shapes that exposed the alias defect.
    v.push_back({"mov_alone", chain({Expr{Op::Mov, -1, -1, 0}})});
    v.push_back({"mov_rev",   chain({Expr{Op::Mov, -1, -1, 0}, Expr{Op::Rev, 0, -1, 0}})});
    v.push_back({"mov_tail",  chain({Expr{Op::Mov, -1, -1, 0}, Expr{Op::Tail, 0, -1, 0}})});
    v.push_back({"mov_delta", chain({Expr{Op::Mov, -1, -1, 0}, Expr{Op::Delta, 0, -1, 0}})});

    // Controls with NO Mov, so a failure above cannot be blamed on the operation.
    v.push_back({"rev_only",   chain({Expr{Op::Rev, -1, -1, 0}})});
    v.push_back({"delta_only", chain({Expr{Op::Delta, -1, -1, 0}})});
    v.push_back({"sort_only",  chain({Expr{Op::Sort, -1, -1, 0}})});

    // A Mov of another NODE, which is the case the alias is allowed to drop --
    // the source is already emitted through kh_lim, so clamping twice is a no-op.
    v.push_back({"rev_mov_rev", chain({Expr{Op::Rev, -1, -1, 0},
                                       Expr{Op::Mov, 0, -1, 0},
                                       Expr{Op::Rev, 1, -1, 0}})});

    // Growth past the bound without any Mov: append doubles the length.
    v.push_back({"cat_self", chain({Expr{Op::Append, -1, -1, 0}})});

    // The four newest operations, on the same oversized inputs.
    v.push_back({"gt_lit", chain({Expr{Op::Const, -1, -1, 0, 2, true},
                                  Expr{Op::Gt, -1, 0, 0}})});
    v.push_back({"member_self", chain({Expr{Op::Member, -1, -1, 0}})});
    v.push_back({"until_lit", chain({Expr{Op::Const, -1, -1, 0, 3, true},
                                     Expr{Op::Until, -1, 0, 0}})});
    // A mined literal, to prove has_lit survives emission.
    v.push_back({"addk_mined", chain({Expr{Op::Const, -1, -1, 0, -32, true},
                                      Expr{Op::MapAdd, -1, 0, 0}})});
    return v;
}

std::vector<Value> inputs() {
    std::vector<Value> v{
        {}, {0}, {7}, {3, 3, 3}, {5, 4, 3, 2, 1}, {-9, 0, 9},
    };
    // THE ONES THAT MATTER: longer than kMaxListLen, so the clamp is load-bearing.
    Value big600, big513, ramp1000;
    for (std::int64_t i = 0; i < 600; ++i) big600.push_back(i % 17 - 8);
    for (std::int64_t i = 0; i < 513; ++i) big513.push_back(i);
    for (std::int64_t i = 0; i < 1000; ++i) ramp1000.push_back(1000 - i);
    v.push_back(std::move(big600));
    v.push_back(std::move(big513));
    v.push_back(std::move(ramp1000));
    return v;
}

std::string join(const Value& v) {
    std::string s;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) s += ',';
        s += std::to_string(v[i]);
    }
    return s;
}

// Per-language driver text: read nothing, just call each function on each
// hard-coded input and print one line per (recipe, input).
std::string driver(Lang l, const std::vector<Named>& rs, const std::vector<Value>& ins) {
    std::string s;
    // The call, with however many arguments the recipe takes. Written once
    // here rather than in six drivers, because six places to forget the
    // second argument is six chances to emit a program that ignores it.
    const std::size_t nin = ins.size();
    // `pre` is what an element must be prefixed with to be passed. Rust takes
    // &V; everything else passes the element itself.
    auto call = [&](const Named& n, const std::string& idx, const std::string& arr,
                    const std::string& pre = "") {
        auto elem = [&](const std::string& e) { return pre + arr + "[" + e + "]"; };
        std::string c = std::string(n.name) + "(" + elem(idx);
        for (std::size_t k = 1; k < n.r.arity(); ++k) {
            c += ", " + elem("(" + idx + " + " + std::to_string(k) + ") % "
                            + std::to_string(nin));
        }
        return c + ")";
    };
    auto lit = [&](const Value& v) {
        switch (l) {
            case Lang::Cpp:  return "V{" + join(v) + "}";
            case Lang::Rust: return "vec![" + join(v) + "]";
            case Lang::Go:   return "V{" + join(v) + "}";
            case Lang::CSharp: return "new long[]{" + join(v) + "}";
            case Lang::Java:   return "new long[]{" + join(v) + "}";
            case Lang::Kotlin: { std::string t; for (std::size_t q = 0; q < v.size(); ++q)
                    { if (q) t += ", "; t += std::to_string(v[q]) + "L"; }
                return "listOf<Long>(" + t + ")"; }
            case Lang::Lua:    return "{" + join(v) + "}";   // Lua tables, not [ ]
            default:         return "[" + join(v) + "]";
        }
    };

    if (l == Lang::Python) {
        s += "INPUTS = [" ;
        for (std::size_t i = 0; i < ins.size(); ++i) { if (i) s += ", "; s += lit(ins[i]); }
        s += "]\n";
        for (const Named& n : rs) {
            s += "for _i in range(len(INPUTS)):\n";
            s += std::string("    print('") + n.name + " %d %s' % (_i, ','.join(str(z) for z in "
                 + call(n, "_i", "INPUTS") + ")))\n";
        }
    } else if (l == Lang::JavaScript) {
        s += "const INPUTS = [";
        for (std::size_t i = 0; i < ins.size(); ++i) { if (i) s += ", "; s += lit(ins[i]); }
        s += "];\n";
        for (const Named& n : rs) {
            s += "for (let i = 0; i < INPUTS.length; i++) console.log('" + std::string(n.name) +
                 " ' + i + ' ' + " + call(n, "i", "INPUTS") + ".join(','));\n";
        }
    } else if (l == Lang::Haskell) {
        // Curried application by juxtaposition, and every emitted function is a
        // top-level binding, so the driver is a plain main.
        s += "khjoin :: [String] -> String\nkhjoin [] = \"\"\nkhjoin [x] = x\nkhjoin (x:xs) = x ++ \",\" ++ khjoin xs\n\ninputs :: [V]\ninputs = [";
        for (std::size_t i = 0; i < ins.size(); ++i) { if (i) s += ", "; s += lit(ins[i]); }
        s += "]\n\nmain :: IO ()\nmain = do\n";
        for (const Named& nm : rs) {
            std::string args = "(inputs !! i)";
            for (std::size_t k = 1; k < nm.r.arity(); ++k) {
                args += " (inputs !! ((i + " + std::to_string(k) + ") `mod` "
                      + std::to_string(nin) + "))";
            }
            s += "  mapM_ (\\i -> putStrLn (\"" + std::string(nm.name)
               + " \" ++ show i ++ \" \" ++ khjoin (map show ("
               + std::string(nm.name) + " " + args + "))))\n";
            s += "        [0 .. length inputs - 1]\n";
        }
    } else if (l == Lang::Swift) {
        // Top-level code in a file named main.swift is the program entry point.
        s += "let INPUTS: [V] = [";
        for (std::size_t i = 0; i < ins.size(); ++i) { if (i) s += ", "; s += lit(ins[i]); }
        s += "]\n";
        for (const Named& nm : rs) {
            s += "for i in 0..<INPUTS.count {\n";
            s += "  let r = " + call(nm, "i", "INPUTS") + "\n";
            s += "  print(\"" + std::string(nm.name) + " \" + String(i) + \" \" + r.map { String($0) }.joined(separator: \",\"))\n";
            s += "}\n";
        }
    } else if (l == Lang::Kotlin) {
        // Kotlin emits top-level functions over List<Long>, so the driver is a
        // plain main() in the same file.
        s += "fun main() {\n  val INPUTS = listOf(";
        for (std::size_t i = 0; i < ins.size(); ++i) { if (i) s += ", "; s += lit(ins[i]); }
        s += ")\n";
        for (const Named& nm : rs) {
            s += "  for (i in INPUTS.indices) {\n";
            s += "    val r = " + call(nm, "i", "INPUTS") + "\n";
            s += "    println(\"" + std::string(nm.name) + " \" + i + \" \" + r.joinToString(\",\"))\n";
            s += "  }\n";
        }
        s += "}\n";
    } else if (l == Lang::Java) {
        // Each emitted function lives in its own non-public class Fn_<name>, so
        // the driver is one more class in the same file and calls through them.
        s += "public class Main {\n  public static void main(String[] a) {\n",
        s += "    long[][] INPUTS = new long[][]{";
        for (std::size_t i = 0; i < ins.size(); ++i) { if (i) s += ", "; s += lit(ins[i]); }
        s += "};\n";
        for (const Named& nm : rs) {
            s += "    for (int i = 0; i < INPUTS.length; i++) {\n";
            s += "      StringBuilder sb = new StringBuilder();\n";
            s += "      long[] r = Fn_" + std::string(nm.name) + "." + call(nm, "i", "INPUTS") + ";\n";
            s += "      for (int k = 0; k < r.length; k++) { if (k > 0) sb.append(\",\"); sb.append(r[k]); }\n";
            s += "      System.out.println(\"" + std::string(nm.name) + " \" + i + \" \" + sb);\n";
            s += "    }\n";
        }
        s += "  }\n}\n";
    } else if (l == Lang::Ruby) {
        // Returns the whole transcript as a STRING rather than printing it. The
        // Ruby available here is CRuby compiled to WASM, whose WASI stdout is not
        // wired up in this harness -- but a returned value crosses the boundary
        // cleanly, and the language semantics under test are identical either way.
        s += "INPUTS = [";
        for (std::size_t i = 0; i < ins.size(); ++i) { if (i) s += ", "; s += lit(ins[i]); }
        s += "]\n_out = []\n";
        for (const Named& nm : rs) {
            s += "INPUTS.each_index { |i| _out << ('" + std::string(nm.name)
               + "' + ' ' + i.to_s + ' ' + " + call(nm, "i", "INPUTS") + ".join(',')) }\n";
        }
        s += "_out.join(10.chr)\n";
    } else if (l == Lang::Lua) {
        // Lua indexes from ONE. The reference prints a 0-based case index and
        // argument k is input (i + k) mod n, so both need translating rather
        // than reusing the shared call builder. Single-quoted Lua strings keep
        // the C++ literal free of escaped quotes.
        s += "local INPUTS = {";
        for (std::size_t i = 0; i < ins.size(); ++i) { if (i) s += ", "; s += lit(ins[i]); }
        s += "}\n";
        for (const Named& nm : rs) {
            std::string args = "INPUTS[i + 1]";
            for (std::size_t k = 1; k < nm.r.arity(); ++k) {
                args += ", INPUTS[((i + " + std::to_string(k) + ") % "
                      + std::to_string(nin) + ") + 1]";
            }
            s += "for j = 1, #INPUTS do\n";
            s += "  local i = j - 1\n";
            s += "  local r = " + std::string(nm.name) + "(" + args + ")\n";
            s += "  print('" + std::string(nm.name) + "' .. ' ' .. i .. ' ' .. table.concat(r, ','))\n";
            s += "end\n";
        }
    } else if (l == Lang::TypeScript) {
        s += "const INPUTS: V[] = [";
        for (std::size_t i = 0; i < ins.size(); ++i) { if (i) s += ", "; s += lit(ins[i]); }
        s += "];\n";
        for (const Named& n : rs) {
            s += "for (let i = 0; i < INPUTS.length; i++) console.log('" + std::string(n.name) +
                 " ' + i + ' ' + " + call(n, "i", "INPUTS") + ".join(','));\n";
        }
    } else if (l == Lang::Go) {
        s += "func main() {\n  inputs := []V{";
        for (std::size_t i = 0; i < ins.size(); ++i) { if (i) s += ", "; s += lit(ins[i]); }
        s += "}\n";
        for (const Named& n : rs) {
            s += "  for i := range inputs {\n";
            s += "    parts := []string{}\n";
            s += "    for _, z := range " + call(n, "i", "inputs") + " { parts = append(parts, strconv.FormatInt(z, 10)) }\n";
            s += "    fmt.Printf(\"" + std::string(n.name) + " %d %s\\n\", i, strings.Join(parts, \",\"))\n";
            s += "  }\n";
        }
        s += "}\n";
    } else if (l == Lang::CSharp) {
        s += "static class Program {\n  static void Main() {\n    long[][] INPUTS = new long[][]{";
        for (std::size_t i = 0; i < ins.size(); ++i) { if (i) s += ", "; s += lit(ins[i]); }
        s += "};\n";
        for (const Named& n : rs) {
            s += "    for (int i = 0; i < INPUTS.Length; i++)\n";
            s += "      Console.WriteLine(\"" + std::string(n.name) + " \" + i + \" \" + string.Join(\",\", Fn_" + std::string(n.name) + "." + call(n, "i", "INPUTS") + "));\n";
        }
        s += "  }\n}\n";
    } else if (l == Lang::Php) {
        s += "$INPUTS = [";
        for (std::size_t i = 0; i < ins.size(); ++i) { if (i) s += ", "; s += lit(ins[i]); }
        s += "];\n";
        for (const Named& n : rs) {
            s += "foreach ($INPUTS as $i => $v) { echo \"" + std::string(n.name) +
                 " $i \" . implode(',', " + call(n, "$i", "$INPUTS") + ") . \"\\n\"; }\n";
        }
    } else if (l == Lang::Cpp) {
        s += "#include <cstdio>\n#include <string>\nint main() {\n  std::vector<V> inputs{";
        for (std::size_t i = 0; i < ins.size(); ++i) { if (i) s += ", "; s += lit(ins[i]); }
        s += "};\n";
        for (const Named& n : rs) {
            s += "  for (std::size_t i = 0; i < inputs.size(); ++i) {\n";
            s += "    V r = " + call(n, "i", "inputs") + ";\n";
            s += "    std::string t; for (std::size_t k = 0; k < r.size(); ++k) "
                 "{ if (k) t += ','; t += std::to_string(r[k]); }\n";
            s += "    std::printf(\"" + std::string(n.name) + " %zu %s\\n\", i, t.c_str());\n";
            s += "  }\n";
        }
        s += "  return 0;\n}\n";
    } else if (l == Lang::Rust) {
        s += "fn main() {\n  let inputs: Vec<V> = vec![";
        for (std::size_t i = 0; i < ins.size(); ++i) { if (i) s += ", "; s += lit(ins[i]); }
        s += "];\n";
        for (const Named& n : rs) {
            s += "  for i in 0..inputs.len() {\n";
            s += "    let r = " + call(n, "i", "inputs", "&") + ";\n";
            s += "    let t: Vec<String> = r.iter().map(|z| z.to_string()).collect();\n";
            s += "    println!(\"" + std::string(n.name) + " {} {}\", i, t.join(\",\"));\n";
            s += "  }\n";
        }
        s += "}\n";
    }
    return s;
}

} // namespace

int main(int argc, char** argv) {
    const std::string out_dir = (argc > 1) ? argv[1] : ".";

    auto rs = recipes();
    {
        // Generated shapes alongside the regression cases, in the same run and
        // graded the same way.
        const auto fz = fuzz_recipes(48, 0xC0FFEEULL);
        rs.insert(rs.end(), fz.begin(), fz.end());
    }
    const auto ins = inputs();

    std::printf("Does the emitted code compute what the certified recipe computes?\n\n");
    std::printf("  %zu recipes x %zu inputs, three of them longer than the %zu-element\n",
                rs.size(), ins.size(), kMaxListLen);
    std::printf("  bound where the interpreter's per-operation clamp bites.\n\n");

    struct Target { Lang l; const char* file; };
    const Target targets[] = {
        {Lang::Python,     "emitted.py"},
        {Lang::JavaScript, "emitted.js"},
        {Lang::Go,         "emitted.go"},
        {Lang::Rust,       "emitted.rs"},
        {Lang::Php,        "emitted.php"},
        {Lang::Cpp,        "emitted.cpp"},
        {Lang::CSharp,     "emitted.cs"},
        {Lang::TypeScript, "emitted.ts"},
        {Lang::Lua,        "emitted.lua"},
        {Lang::Ruby,       "emitted.rb"},
        {Lang::Java,       "Main.java"},
        {Lang::Kotlin,     "emitted.kt"},
        {Lang::Swift,      "main.swift"},
        {Lang::Haskell,    "emitted.hs"},
    };

    // WHAT THE EMITTER REFUSES IS NOT WHAT IT GETS WRONG.
    //
    // emit() returns an empty string for a recipe it cannot render -- a library
    // body nested past kMaxCallDepth, for one. The driver was written from the
    // recipe list regardless, so a refused recipe became a call to a function
    // nobody defined and EVERY backend reported "could not build", which reads
    // as fourteen broken emitters and is one unrendered recipe.
    //
    // A recipe is kept only if every target renders it, so all fourteen files
    // and reference.txt describe the same set. Refusals are counted and printed.
    std::size_t refused = 0;
    {
        std::vector<Named> keep;
        for (const Named& n : rs) {
            bool all = true;
            for (const Target& t : targets) {
                std::size_t ln = 0;
                if (emit(n.r, t.l, n.name, &ln, &fuzz_lib()).empty()) { all = false; break; }
            }
            if (all) keep.push_back(n); else ++refused;
        }
        rs = keep;
    }
    if (refused != 0)
        std::printf("  %zu of %zu recipes are refused by emit() in at least one\n"
                    "  backend and are excluded from every file, so the comparison is\n"
                    "  over the same set everywhere. A refusal is a limit, not a defect,\n"
                    "  and it is not counted as a pass.\n\n",
                    refused, refused + rs.size());

    // THE REFERENCE. Recipe::apply is the thing a certificate is about, so it is
    // the thing the emitted source has to agree with.
    {
        std::ofstream f(out_dir + "/reference.txt");
        for (const Named& n : rs) {
            for (std::size_t i = 0; i < ins.size(); ++i) {
                                // THE SAME LIBRARY THE EMITTER GETS. Recipe::apply resolves a
                // Call through it; emission inlines it. Handing one of them a
                // library and the other a nullptr would compare two different
                // programs and call the difference an emitter defect.
                f << n.name << ' ' << i << ' '
                  << join(n.r.apply_n(argsfor(n, ins, i), &fuzz_lib())) << '\n';
            }
        }
    }


    for (const Target& t : targets) {
        std::string src = prelude(t.l);
        if (t.l == Lang::Go) {
            // Go requires every import in one block before any declaration, and
            // the prelude already opens one. Splicing rather than appending is
            // the difference between a file that builds and one that does not.
            const std::string need = "import (\n\t\"strconv\"\n\t\"strings\"\n\t\"fmt\"\n)\n";
            const std::size_t at = src.find("package ");
            const std::size_t eol = src.find('\n', at);
            src.insert(eol + 1, need);
            // AND THE PACKAGE HAS TO BE main. The prelude declares `package kh`,
            // which is right for a library and means `go run` refuses the file
            // outright -- so this harness has been writing Go it could not
            // execute, and reporting a line count for it as if it had.
            src.replace(at, eol - at, "package main");
        }
        if (t.l == Lang::Rust) src = "#![allow(dead_code, unused_parens)]\n" + src;

        std::size_t total_lines = 0;
        for (const Named& n : rs) {
            std::size_t lines = 0;
            src += emit(n.r, t.l, n.name, &lines, &fuzz_lib());
            total_lines += lines;
        }
        src += driver(t.l, rs, ins);

        std::ofstream f(out_dir + "/" + t.file);
        f << src;
        std::printf("  wrote %-12s  %zu body lines across %zu functions\n",
                    t.file, total_lines, rs.size());
    }

    std::printf("\n  reference.txt holds Recipe::apply for every pair. Each backend below\n");
    std::printf("  is RUN and its stdout diffed against it; any difference is the\n");
    std::printf("  emitted program computing something the certificate does not cover.\n\n");

    const std::string ref = slurp(out_dir + "/reference.txt");
    if (ref.empty()) { std::printf("  reference.txt is empty -- nothing to compare.\n"); return 1; }

    std::printf("  backend     | toolchain        | result\n");
    std::printf("  ------------+------------------+--------------------------------\n");
    std::size_t ran = 0, matched = 0, missing = 0, unbuildable = 0;
    std::vector<std::string> failures;
    for (const Runner& rn : runners()) {
        const std::string probe = capture("cd " + out_dir + " && " + rn.probe).out;
        if (!have_toolchain(probe, rn.expect, rn.exact)) {
            ++missing;
            std::printf("  %-11s | %-16s | skipped, no toolchain here\n",
                        lang_name(rn.l), "not found");
            continue;
        }
        ++ran;
        std::string ver = probe.substr(0, probe.find_first_of("\r\n"));
        if (ver.size() > 16) ver = ver.substr(0, 16);
        const Ran ran_it = capture("cd " + out_dir + " && " + rn.run);
        const std::string got = normalise(ran_it.out);
        if (ran_it.status != 0) {
            ++unbuildable;
            std::printf("  %-11s | %-16s | could not build or run here\n",
                        lang_name(rn.l), ver.c_str());
            continue;
        }
        if (got == ref) {
            ++matched;
            std::printf("  %-11s | %-16s | matches on every pair\n", lang_name(rn.l), ver.c_str());
        } else {
            failures.push_back(lang_name(rn.l));
            // Where it first diverges is the useful half of a mismatch.
            std::size_t line = 0, i = 0, j = 0;
            while (i < got.size() && j < ref.size() && got[i] == ref[j]) {
                if (got[i] == 10) ++line;
                ++i; ++j;
            }
            std::printf("  %-11s | %-16s | DIFFERS from line %zu\n",
                        lang_name(rn.l), ver.c_str(), line + 1);
        }
    }

    std::printf("\n  %zu of %zu backends have a toolchain here. %zu of them reproduce\n",
                ran, runners().size(), matched);
    std::printf("  Recipe::apply BYTE FOR BYTE, %zu disagree with it, and %zu could not\n",
                ran - matched - unbuildable, unbuildable);
    if (missing == 0) {
        std::printf("  be built on this host at all, and NONE is unrun. Only the\n");
    } else {
        std::printf("  be built on this host at all. %zu have no toolchain here -- run\n", missing);
        std::printf("  tools\\toolchains.ps1, which fetches every one of them. Only the\n");
    }
    std::printf("  first of those four is a pass, and none of the others is counted as\n");
    std::printf("  one -- a backend that will not build says nothing about the code it\n");
    std::printf("  emits, and neither does a backend nobody ran.\n\n");
    std::printf("  %zu recipes x %zu inputs each. Three of the recipes take TWO\n", rs.size(), ins.size());
    std::printf("  ARGUMENTS, and argument 1 is the NEXT input rather than a repeat of\n");
    std::printf("  the first, so a program that quietly ignores it cannot pass by\n");
    std::printf("  coincidence. Three of the inputs are longer than the 512-element\n");
    std::printf("  bound where the interpreter clamps every operation.\n\n");
    std::printf("  THIS TABLE USED TO BE A LITERAL. The line read \"ALL FOURTEEN BACKENDS\n");
    std::printf("  EXECUTED AND BYTE-IDENTICAL\" and no toolchain was ever invoked from\n");
    std::printf("  this process -- it printed the same whether the emitter was right,\n");
    std::printf("  broken or missing. It was earned once, by hand, and then frozen. A\n");
    std::printf("  guarantee that cannot be re-run is not a guarantee.\n\n");
    std::printf("  Nothing here is counted for being emitted. Go was written as\n");
    std::printf("  `package kh` and could never run at all, while this harness printed\n");
    std::printf("  a body-line count for it as though it had -- and the php on PATH is\n");
    std::printf("  a different tool wearing the name, which is why every probe checks\n");
    std::printf("  WHAT answered and not merely THAT something did.\n\n");
    return failures.empty() ? 0 : 1;
}
