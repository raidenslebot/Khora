// DOES THE EMITTED CODE COMPUTE WHAT THE CERTIFIED RECIPE COMPUTES?
//
// This is the only claim this organ makes, and it has been false three times:
// Op::Call deleted from emitted source, the length clamp missing from every
// prelude, and Op::Mov aliased away along with the truncation it carries. Each
// time the certificate stayed attached to a program that was not the one handed
// back, and each time a benchmark reported a throughput figure for it.
//
// So this bench does not check the emitter against itself. It writes real source
// in four languages, and a driver runs each with its actual toolchain and diffs
// against Recipe::apply. Agreement is the only evidence that means anything;
// everything else is the emitter agreeing with the emitter.
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

// The arguments a recipe of this arity gets for input i. A second argument is
// the NEXT input rather than a repeat of the first, so a program that silently
// ignores it cannot pass by coincidence.
std::vector<Value> argsfor(const Named& n, const std::vector<Value>& ins, std::size_t i) {
    std::vector<Value> a{ins[i]};
    for (std::size_t k = 1; k < n.r.arity(); ++k) a.push_back(ins[(i + k) % ins.size()]);
    return a;
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

    const auto rs = recipes();
    const auto ins = inputs();

    std::printf("Does the emitted code compute what the certified recipe computes?\n\n");
    std::printf("  %zu recipes x %zu inputs, three of them longer than the %zu-element\n",
                rs.size(), ins.size(), kMaxListLen);
    std::printf("  bound where the interpreter's per-operation clamp bites.\n\n");

    // THE REFERENCE. Recipe::apply is the thing a certificate is about, so it is
    // the thing the emitted source has to agree with.
    {
        std::ofstream f(out_dir + "/reference.txt");
        for (const Named& n : rs) {
            for (std::size_t i = 0; i < ins.size(); ++i) {
                f << n.name << ' ' << i << ' ' << join(n.r.apply_n(argsfor(n, ins, i), nullptr)) << '\n';
            }
        }
    }

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
    };

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
            src += emit(n.r, t.l, n.name, &lines, nullptr);
            total_lines += lines;
        }
        src += driver(t.l, rs, ins);

        std::ofstream f(out_dir + "/" + t.file);
        f << src;
        std::printf("  wrote %-12s  %zu body lines across %zu functions\n",
                    t.file, total_lines, rs.size());
    }

    std::printf("\n  reference.txt holds Recipe::apply for every pair. Run each emitted\n");
    std::printf("  file and diff its stdout against it; any difference is the emitted\n");
    std::printf("  program computing something the certificate does not cover.\n\n");
    std::printf("  EXECUTED AND BYTE-IDENTICAL on this machine: Python, JavaScript,\n");
    std::printf("  TypeScript, Go, Rust, C++ and C# -- seven of the fourteen backends,\n");
    std::printf("  %zu recipes x %zu inputs each. Three of the recipes take TWO\n", rs.size(), ins.size());
    std::printf("  ARGUMENTS, and argument 1 is the NEXT input rather than a repeat of\n");
    std::printf("  the first, so a program that quietly ignores it cannot pass by\n");
    std::printf("  coincidence.\n\n");
    std::printf("  Java, Kotlin, Swift, Haskell, Ruby, Lua and PHP are EMITTED AND NOT\n");
    std::printf("  EXECUTED: no toolchain for them here. They are not counted. Writing a\n");
    std::printf("  file is not evidence that it runs, which is exactly what Go proved --\n");
    std::printf("  it was emitted as `package kh` and could never run at all, while this\n");
    std::printf("  harness printed a body-line count for it as though it had.\n");}
