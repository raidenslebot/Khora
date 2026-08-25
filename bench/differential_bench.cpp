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

std::vector<Named> recipes() {
    std::vector<Named> v;

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
    auto lit = [&](const Value& v) {
        switch (l) {
            case Lang::Cpp:  return "V{" + join(v) + "}";
            case Lang::Rust: return "vec![" + join(v) + "]";
            case Lang::Go:   return "V{" + join(v) + "}";
            default:         return "[" + join(v) + "]";
        }
    };

    if (l == Lang::Python) {
        s += "INPUTS = [" ;
        for (std::size_t i = 0; i < ins.size(); ++i) { if (i) s += ", "; s += lit(ins[i]); }
        s += "]\n";
        for (const Named& n : rs) {
            s += "for _i, _v in enumerate(INPUTS):\n";
            s += std::string("    print('") + n.name + " %d %s' % (_i, ','.join(str(z) for z in "
                 + n.name + "(_v))))\n";
        }
    } else if (l == Lang::JavaScript) {
        s += "const INPUTS = [";
        for (std::size_t i = 0; i < ins.size(); ++i) { if (i) s += ", "; s += lit(ins[i]); }
        s += "];\n";
        for (const Named& n : rs) {
            s += "INPUTS.forEach((v, i) => console.log('" + std::string(n.name) +
                 " ' + i + ' ' + " + n.name + "(v).join(',')));\n";
        }
    } else if (l == Lang::Go) {
        s += "func main() {\n  inputs := []V{";
        for (std::size_t i = 0; i < ins.size(); ++i) { if (i) s += ", "; s += lit(ins[i]); }
        s += "}\n";
        for (const Named& n : rs) {
            s += "  for i, v := range inputs {\n";
            s += "    parts := []string{}\n";
            s += "    for _, z := range " + std::string(n.name) + "(v) { parts = append(parts, strconv.FormatInt(z, 10)) }\n";
            s += "    fmt.Printf(\"" + std::string(n.name) + " %d %s\\n\", i, strings.Join(parts, \",\"))\n";
            s += "  }\n";
        }
        s += "}\n";
    } else if (l == Lang::Rust) {
        s += "fn main() {\n  let inputs: Vec<V> = vec![";
        for (std::size_t i = 0; i < ins.size(); ++i) { if (i) s += ", "; s += lit(ins[i]); }
        s += "];\n";
        for (const Named& n : rs) {
            s += "  for (i, v) in inputs.iter().enumerate() {\n";
            s += "    let r = " + std::string(n.name) + "(v);\n";
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
                f << n.name << ' ' << i << ' ' << join(n.r.apply(ins[i], nullptr)) << '\n';
            }
        }
    }

    struct Target { Lang l; const char* file; };
    const Target targets[] = {
        {Lang::Python,     "emitted.py"},
        {Lang::JavaScript, "emitted.js"},
        {Lang::Go,         "emitted.go"},
        {Lang::Rust,       "emitted.rs"},
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
    std::printf("  program computing something the certificate does not cover.\n");
    return 0;
}
