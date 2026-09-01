// KHORA REBUILDING PART OF KHORA, WITH THE LIVING ORGAN AS THE JUDGE.
//
// Every prior self-hosting measurement rebuilt DSL primitives from other DSL
// primitives -- the system feeding on its own vocabulary. This bench aims the
// synthesiser at functions from Khora's OWN SOURCE TREE: the lattice algebra
// in src/lattice/sdr.cpp, which every organ above it depends on.
//
// Two layers, kept explicit because collapsing them is how benches lie:
//
//   LAYER 1 -- the mathematical core of each function, specified as a TOTAL
//   n-ary oracle, synthesised and PROVED with synthesise_hardened_n: every
//   tuple of lists over the bounded domain, the cartesian power of the
//   extremal inputs, counterexample refinement. This uses the n-ary proof
//   machinery that did not exist before this bench needed it -- no
//   multi-argument function had ever been proved by this module.
//
//   LAYER 2 -- the certified recipe against the REAL implementation, on the
//   real domain: 500 random Sdr pairs, compared three ways -- Recipe::apply_n
//   in process, and the emit_program C++ output compiled and executed, both
//   diffed against what src/lattice/sdr.cpp actually computes.
//
// The adapters are the honest seam: Sdr::overlap takes two 256-block Sdrs and
// nothing else, so the layer-1 spec is the canonical total extension (counts
// over the shorter length; empty folds to zero agreements) and the layer-2
// diff is what ties the proof back to the organ's actual domain.
#include <khora/lattice/sdr.hpp>
#include <khora/techne/techne.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

using namespace khora::techne;
using khora::lattice::Sdr;
using khora::lattice::kSdrBlocks;

namespace {

std::uint64_t st = 0xD06F00DULL;
std::uint64_t rnd() { st ^= st << 13; st ^= st >> 7; st ^= st << 17; return st; }

std::int64_t emod64(std::int64_t x) { return ((x % 64) + 64) % 64; }

struct Target {
    const char* name;
    OracleN oracle;      // the total mathematical core, layer 1
    // the REAL organ, layer 2: takes two 256-length index lists
    std::function<Value(const Value&, const Value&)> organ;
};

Sdr to_sdr(const Value& v) {
    Sdr::Storage s{};
    for (std::size_t i = 0; i < kSdrBlocks && i < v.size(); ++i)
        s[i] = static_cast<std::uint8_t>(emod64(v[i]));
    return Sdr(s);
}

Value sdr_to_value(const Sdr& s) {
    Value v(kSdrBlocks);
    for (std::size_t i = 0; i < kSdrBlocks; ++i)
        v[i] = static_cast<std::int64_t>(s.index(i));
    return v;
}

std::vector<Target> targets() {
    return {
        {"overlap_core",
         [](const std::vector<Value>& a) {
             const std::size_t n = std::min(a[0].size(), a[1].size());
             std::int64_t c = 0;
             for (std::size_t i = 0; i < n; ++i) if (a[0][i] == a[1][i]) ++c;
             return Value{c};
         },
         [](const Value& x, const Value& y) {
             return Value{static_cast<std::int64_t>(to_sdr(x).overlap(to_sdr(y)))};
         }},
        // The same organ function under a DIFFERENT total extension: b cycled
        // over a's length, which is what the instruction set's own zip does.
        // On the organ's domain -- two equal-length index vectors -- cyclic and
        // shortest-length extensions are the SAME function, so layer 2 pins
        // them equally; they differ only outside it. Offering both is the
        // point: the canonical extension needs a depth-4 composition the
        // search cannot reach, and this one does not, and WHICH extension a
        // spec author picks decides whether the proof machinery can help them.
        {"overlap_cyc",
         [](const std::vector<Value>& a) {
             if (a[0].empty() || a[1].empty()) return Value{0};
             std::int64_t c = 0;
             for (std::size_t i = 0; i < a[0].size(); ++i)
                 if (a[0][i] == a[1][i % a[1].size()]) ++c;
             return Value{c};
         },
         [](const Value& x, const Value& y) {
             return Value{static_cast<std::int64_t>(to_sdr(x).overlap(to_sdr(y)))};
         }},
        {"bind_core",
         [](const std::vector<Value>& a) {
             const std::size_t n = std::min(a[0].size(), a[1].size());
             Value o;
             for (std::size_t i = 0; i < n; ++i) o.push_back(emod64(a[0][i] + a[1][i]));
             return o;
         },
         [](const Value& x, const Value& y) {
             return sdr_to_value(khora::lattice::bind(to_sdr(x), to_sdr(y)));
         }},
        {"unbind_core",
         [](const std::vector<Value>& a) {
             const std::size_t n = std::min(a[0].size(), a[1].size());
             Value o;
             for (std::size_t i = 0; i < n; ++i) o.push_back(emod64(a[0][i] - a[1][i]));
             return o;
         },
         [](const Value& x, const Value& y) {
             return sdr_to_value(khora::lattice::unbind(to_sdr(x), to_sdr(y)));
         }},
    };
}

Spec spec_for(const Target& t) {
    Spec s;
    s.name = t.name;
    // Random equal-length pairs in the index range the organ actually uses...
    for (std::size_t c = 0; c < 10; ++c) {
        const std::size_t len = 2 + c % 5;
        Value a, b;
        for (std::size_t j = 0; j < len; ++j) {
            a.push_back(static_cast<std::int64_t>(rnd() % 64));
            b.push_back(static_cast<std::int64_t>(rnd() % 64));
        }
        s.cases.push_back({a, {b}, t.oracle({a, b})});
    }
    // ...plus the boundaries the search cannot invent: the modulus itself as
    // an input value (64 is not in the constant table, so it has to be MINED
    // from the specification), value 63 against 1, unequal lengths, empties.
    const std::vector<std::pair<Value, Value>> edges = {
        {{64}, {0}}, {{63}, {1}}, {{0, 63, 64}, {64, 63, 0}},
        {{5, 6, 7}, {5, 9}}, {{}, {3}}, {{2}, {}},
    };
    for (const auto& [a, b] : edges) s.cases.push_back({a, {b}, t.oracle({a, b})});
    for (std::size_t c = 0; c < 6; ++c) {
        const std::size_t len = 7 + c % 4;
        Value a, b;
        for (std::size_t j = 0; j < len; ++j) {
            a.push_back(static_cast<std::int64_t>(rnd() % 200) - 40);
            b.push_back(static_cast<std::int64_t>(rnd() % 200) - 40);
        }
        s.holdout.push_back({a, {b}, t.oracle({a, b})});
    }
    return s;
}

const char* proof_name(Proof p) {
    switch (p) {
        case Proof::Exhaustive:  return "PROVED";
        case Proof::Verified:    return "verified";
        case Proof::Generalised: return "generalised";
        case Proof::Tested:      return "fitted only";
        default:                 return "none";
    }
}

std::string run_capture(const std::string& cmd) {
#ifdef _WIN32
    FILE* p = _popen((cmd + " 2>&1").c_str(), "r");
#else
    FILE* p = popen((cmd + " 2>&1").c_str(), "r");
#endif
    std::string out;
    if (!p) return out;
    char buf[512];
    while (fgets(buf, sizeof buf, p)) out += buf;
#ifdef _WIN32
    _pclose(p);
#else
    pclose(p);
#endif
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    const std::size_t pool   = (argc > 1) ? std::strtoull(argv[1], nullptr, 10) : 40000;
    const std::size_t rounds = (argc > 2) ? std::strtoull(argv[2], nullptr, 10) : 3;
    std::printf("\n  KHORA REBUILDING PART OF KHORA\n\n");
    std::printf("  Targets are the lattice algebra in src/lattice/sdr.cpp. Layer 1\n");
    std::printf("  proves the mathematical core with synthesise_hardened_n -- the\n");
    std::printf("  first multi-argument proofs this module has ever produced. Layer 2\n");
    std::printf("  holds the certified recipe against the LIVING ORGAN on 500 random\n");
    std::printf("  Sdr pairs, in process and again through emitted, compiled C++.\n\n");

    struct Row { std::string name; Recipe r; bool proved; };
    std::vector<Row> rows;

    // PROBE. count(sub(x, x1), 0) is depth TWO, satisfies the cyclic spec on
    // every case in it, and by hand-reasoning should be exhaustively clean --
    // yet the search returns something else. Before blaming the search, pin
    // the program itself: build it by hand, grade it against the oracle on the
    // spec cases and the full bounded domain. If it is clean, the defect is in
    // the two-argument SEARCH path and this line will say so.
    {
        const Target cyc = targets()[1];
        Recipe ref;
        Expr c0; c0.op = Op::Const; c0.lit = 0; c0.has_lit = true;
        ref.pool.push_back(c0);                                  // 0: const 0
        ref.pool.push_back(Expr{Op::Arg, -1, -1, 1});            // 1: x1
        ref.pool.push_back(Expr{Op::Sub, -1, 1, 0});             // 2: sub(x, x1)
        ref.pool.push_back(Expr{Op::Count, 2, 0, 0});            // 3: count(sub, 0)
        ref.root = 3; ref.found = true;
        const Spec sc = spec_for(cyc);
        std::size_t fit = 0;
        for (const Case& c : sc.cases)
            if (ref.apply_n(c.args(), nullptr) == c.out) ++fit;
        const Exhaust pe = check_exhaustive_n(ref, nullptr, cyc.oracle, -2, 2, 4, 2);
        std::printf("  probe: hand-built count(sub(x, x1), 0) fits %zu/%zu cases;"
                    " exhaustive over %zu tuples: %s\n\n",
                    fit, sc.cases.size(), pe.checked, pe.clean ? "CLEAN" : "dirty");
        if (!pe.clean && !pe.counterexample_n.empty()) {
            std::printf("    counterexample: a=[");
            for (const auto v : pe.counterexample_n[0]) std::printf(" %lld", (long long)v);
            std::printf(" ] b=[");
            for (const auto v : pe.counterexample_n[1]) std::printf(" %lld", (long long)v);
            std::printf(" ]\n\n");
        }
    }

    std::printf("  target       | layer-1 proof | program\n");
    std::printf("  -------------|---------------|-----------------------------------\n");
    for (const Target& t : targets()) {
        const Spec s = spec_for(t);
        Exhaust ex;
        const BuildResult b =
            synthesise_hardened_n(s, pool, t.oracle, -2, 2, 4, rounds, nullptr, &ex);
        std::printf("  %-12s | %-13s | %s\n", t.name, proof_name(b.proof),
                    b.recipe.found ? b.recipe.render().c_str() : "--");
        rows.push_back({t.name, b.recipe, b.proof == Proof::Exhaustive});
    }

    // ---- LAYER 2, in process ------------------------------------------------
    std::printf("\n  target       | vs the real organ, 500 random Sdr pairs\n");
    std::printf("  -------------|----------------------------------------\n");
    const std::vector<Target> ts = targets();
    std::vector<std::pair<Value, Value>> pairs;
    for (std::size_t i = 0; i < 500; ++i)
        pairs.emplace_back(sdr_to_value(Sdr::random(rnd())),
                           sdr_to_value(Sdr::random(rnd())));
    for (std::size_t ti = 0; ti < ts.size(); ++ti) {
        if (!rows[ti].r.found) { std::printf("  %-12s | no recipe\n", ts[ti].name); continue; }
        std::size_t ok = 0;
        for (const auto& [x, y] : pairs)
            if (rows[ti].r.apply_n({x, y}, nullptr) == ts[ti].organ(x, y)) ++ok;
        std::printf("  %-12s | %zu/500 agree\n", ts[ti].name, ok);
    }

    // ---- LAYER 2, through the emitter --------------------------------------
    // The same recipes, emitted as one C++ unit, compiled with the same
    // toolchain that builds Khora, run on the same 500 pairs, diffed against
    // the organ. cl has to be on PATH (the task runner puts it there); if it
    // is not, say so and claim nothing.
    if (run_capture("cl 2>&1").find("Microsoft") == std::string::npos) {
        std::printf("\n  cl not on PATH: the emitted-C++ leg did not run and nothing\n");
        std::printf("  is claimed for it. Run through tools/khora.ps1.\n");
        return 0;
    }
    std::vector<std::pair<std::string, const Recipe*>> fns;
    for (const Row& r : rows) if (r.r.found) fns.emplace_back(r.name, &r.r);
    std::string unit = emit_program(fns, Lang::Cpp, nullptr);
    if (unit.empty()) {
        std::printf("\n  emit_program refused the set; the emitted leg did not run.\n");
        return 0;
    }
    unit += "\n";
    unit += "#include <cstdio>" "\n"
           "int main() {" "\n"
           "    long long n0, v; V a, b;" "\n"
           "    int tgt;" "\n"
           "    while (std::scanf(\"%d %lld\", &tgt, &n0) == 2) {" "\n"
           "        a.clear(); b.clear();" "\n"
           "        for (long long i = 0; i < n0; ++i) { std::scanf(\"%lld\", &v); a.push_back(v); }" "\n"
           "        std::scanf(\"%lld\", &n0);" "\n"
           "        for (long long i = 0; i < n0; ++i) { std::scanf(\"%lld\", &v); b.push_back(v); }" "\n"
           "        V r;" "\n";
    for (std::size_t ti = 0, live = 0; ti < rows.size(); ++ti) {
        if (!rows[ti].r.found) continue;
        unit += "        ";
        if (live++) unit += "else ";
        unit += "if (tgt == " + std::to_string(ti) + ") r = " + rows[ti].name + "(a, b);" "\n";
    }
    unit += "        for (std::size_t i = 0; i < r.size(); ++i)" "\n"
           "            std::printf(i ? \" %lld\" : \"%lld\", r[i]);" "\n"
           "        std::printf(\"\\n\");" "\n"
           "    }" "\n"
           "}" "\n";
    {
        std::ofstream f("dogfood_emitted.cpp");
        f << unit;
    }
    {
        std::ofstream f("dogfood_in.txt");
        for (std::size_t ti = 0; ti < rows.size(); ++ti) {
            if (!rows[ti].r.found) continue;
            for (const auto& [x, y] : pairs) {
                f << ti << " " << x.size();
                for (const auto e : x) f << " " << e;
                f << " " << y.size();
                for (const auto e : y) f << " " << e;
                f << "\n";
            }
        }
    }
    // Compile and run SEPARATELY: chained behind && in one cmd, the freshly
    // linked exe was intermittently "not recognized" -- the directory entry
    // lags the linker on this host -- and the error landed in the output file.
    const std::string cc = run_capture("cl /nologo /O2 /EHsc /std:c++20 dogfood_emitted.cpp");
    std::string rr = run_capture(".\\dogfood_emitted.exe < dogfood_in.txt > dogfood_out.txt");
    if (rr.find("not recognized") != std::string::npos)
        rr = run_capture(".\\dogfood_emitted.exe < dogfood_in.txt > dogfood_out.txt");
    std::ifstream got("dogfood_out.txt");
    std::printf("\n  target       | emitted C++ vs the real organ\n");
    std::printf("  -------------|------------------------------\n");
    bool io_ok = got.good();
    for (std::size_t ti = 0; ti < rows.size() && io_ok; ++ti) {
        if (!rows[ti].r.found) continue;
        std::size_t ok = 0, n = 0;
        for (const auto& [x, y] : pairs) {
            std::string line;
            if (!std::getline(got, line)) { io_ok = false; break; }
            const Value want = ts[ti].organ(x, y);
            std::string ws;
            for (std::size_t i = 0; i < want.size(); ++i)
                ws += (i ? " " : "") + std::to_string(want[i]);
            ++n;
            if (line == ws) ++ok;
        }
        std::printf("  %-12s | %zu/%zu byte-identical\n", rows[ti].name, ok, n);
    }
    if (!io_ok)
        std::printf("  the compiled program produced too little output -- compiler said:\n%s\n",
                    cc.c_str());

    std::printf("\n  What this does and does not claim: layer 1 is a bounded proof of\n");
    std::printf("  the CORE, on its own total spec. Layer 2 ties that core to\n");
    std::printf("  the organ on its real domain. Neither layer claims the organ is\n");
    std::printf("  replaced -- the adapters (index extraction, masking) are mine, and\n");
    std::printf("  they are exactly the part no proof covers.\n\n");
    return 0;
}
