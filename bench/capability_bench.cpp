// RETRIEVING A PROGRAM BY LOOKING AT THE PROBLEM.
//
// Khora synthesises certified programs (techne) and it binds anything to
// anything (chiasm). Those two have never met. Put them together and the
// question becomes: can Khora be shown a task it has never seen, hand back a
// program for it WITHOUT searching, and have that program actually work?
//
// Not generated and not searched -- unbound. A synthesis that takes seconds
// becomes a similarity query and an XOR, and the program that comes back still
// carries the proof state it was certified with. If it holds, the system stops
// re-deriving what it already knows.
//
// IT IS UNFAKEABLE TO MEASURE, which is the reason to do it this way. The
// retrieved program is EXECUTED on the new task's own cases and checked against
// their outputs. There is no label to match and no similarity threshold to
// argue about: either the program computes the right answers on inputs it was
// not retrieved from, or it does not.
//
// THE ENCODING IS THE WHOLE PROBLEM, and I expect the obvious one to fail.
//
// A task is a set of (input, output) pairs. The obvious encoding is
// `bundle over examples of bind(enc(in), enc(out))`. Two instances of the SAME
// function have entirely different inputs, so their bound pairs are unrelated
// glyphs and the bundles should not resemble each other at all. If that is what
// happens, the naive encoding cannot support retrieval and saying so is the
// result.
//
// The alternative is value-independent: describe the TRANSFORMATION rather than
// the values. For each output position, which input position did its value come
// from? `bundle over j of bind(outpos_j, inpos_src(j))` is identical for every
// instance of "reverse" and differs from every instance of "rotate", whatever
// the numbers were. That is a designed representation and it only covers
// programs that rearrange -- a limit stated here rather than discovered later.
//
// Both are measured, side by side, on the same tasks.

#include "khora/chiasm/chiasm.hpp"
#include "khora/lattice/glyph.hpp"
#include "khora/techne/techne.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

using khora::lattice::Glyph;
using khora::techne::Value;

std::uint64_t g_s = 20260826;
std::uint64_t rnd() { g_s ^= g_s << 13; g_s ^= g_s >> 7; g_s ^= g_s << 17; return g_s; }

// --- encoding a list -------------------------------------------------------
//
// Distinct glyphs per number rather than a graded scale. A graded scale would
// make 5 and 6 similar, which is right for perception and wrong here: two
// programs that differ only in a constant must not look alike.
const Glyph& num_glyph(std::int64_t v) {
    static std::vector<Glyph> pos, neg;
    auto& tab = v < 0 ? neg : pos;
    const std::size_t i = static_cast<std::size_t>(v < 0 ? -v : v);
    while (tab.size() <= i)
        tab.push_back(Glyph::from_hash((v < 0 ? "n" : "p") + std::to_string(tab.size())));
    return tab[i];
}

const Glyph& pos_glyph(std::size_t i) {
    static std::vector<Glyph> tab;
    while (tab.size() <= i) tab.push_back(Glyph::from_hash("@" + std::to_string(tab.size())));
    return tab[i];
}

Glyph enc_list(const Value& v) {
    if (v.empty()) return Glyph::from_hash("<empty>");
    std::vector<Glyph> cells;
    cells.reserve(v.size());
    for (std::size_t i = 0; i < v.size(); ++i)
        cells.push_back(khora::lattice::bind(pos_glyph(i), num_glyph(v[i])));
    return khora::lattice::bundle(std::span<const Glyph>(cells));
}

// --- the two task encodings ------------------------------------------------

// NAIVE: the examples themselves.
Glyph enc_task_naive(const std::vector<khora::techne::Case>& cs) {
    std::vector<Glyph> pairs;
    for (const auto& c : cs)
        pairs.push_back(khora::lattice::bind(enc_list(c.in), enc_list(c.out)));
    return khora::lattice::bundle(std::span<const Glyph>(pairs));
}

// STRUCTURAL: where each output element came from, which is the same for every
// instance of a rearranging function and says nothing about the values.
// Positions that cannot be sourced from the input get a marker, so "the value is
// not from the input" is itself a distinguishing feature rather than silence.
Glyph enc_task_structural(const std::vector<khora::techne::Case>& cs) {
    std::vector<Glyph> terms;
    for (const auto& c : cs) {
        for (std::size_t j = 0; j < c.out.size(); ++j) {
            std::size_t src = c.in.size();          // = "not found"
            for (std::size_t i = 0; i < c.in.size(); ++i)
                if (c.in[i] == c.out[j]) { src = i; break; }
            terms.push_back(khora::lattice::bind(pos_glyph(j),
                            khora::lattice::permute(pos_glyph(src), 1)));
        }
        // Length relation, which separates take/drop from rearrange even when
        // every surviving element keeps its position.
        terms.push_back(khora::lattice::bind(Glyph::from_hash("#len"),
                        num_glyph(static_cast<std::int64_t>(c.out.size()) -
                                  static_cast<std::int64_t>(c.in.size()))));
    }
    if (terms.empty()) return Glyph::from_hash("<none>");
    return khora::lattice::bundle(std::span<const Glyph>(terms));
}

// --- the task families -----------------------------------------------------
struct Family {
    const char*                        name;
    std::function<Value(const Value&)> f;
};

std::vector<Family> families() {
    return {
        {"reverse",   [](const Value& v) { Value o(v.rbegin(), v.rend()); return o; }},
        {"sort",      [](const Value& v) { Value o = v; std::sort(o.begin(), o.end()); return o; }},
        {"tail",      [](const Value& v) { return v.empty() ? v : Value(v.begin() + 1, v.end()); }},
        {"init",      [](const Value& v) { return v.empty() ? v : Value(v.begin(), v.end() - 1); }},
        {"rot1",      [](const Value& v) { if (v.empty()) return v; Value o(v.begin() + 1, v.end()); o.push_back(v[0]); return o; }},
        {"dup_first", [](const Value& v) { Value o = v; if (!o.empty()) o.insert(o.begin(), o[0]); return o; }},
        {"take2",     [](const Value& v) { return Value(v.begin(), v.begin() + std::min<std::size_t>(2, v.size())); }},
        {"sort_desc", [](const Value& v) { Value o = v; std::sort(o.begin(), o.end(), std::greater<>()); return o; }},
    };
}

std::vector<khora::techne::Case> sample(const Family& fam, std::size_t n) {
    std::vector<khora::techne::Case> cs;
    for (std::size_t k = 0; k < n; ++k) {
        Value in;
        const std::size_t len = 4 + rnd() % 4;
        for (std::size_t i = 0; i < len; ++i)
            in.push_back(static_cast<std::int64_t>(rnd() % 40));
        cs.emplace_back(in, fam.f(in));
    }
    return cs;
}

struct Score { std::size_t hit = 0, n = 0; };
double pc(const Score& s) { return s.n ? 100.0 * (double)s.hit / (double)s.n : 0.0; }

} // namespace

int main() {
    std::printf("Capability — retrieving a program by looking at the problem\n\n");
    const auto fams = families();

    // --- 0. DOES EITHER ENCODING CLUSTER BY FAMILY AT ALL? -------------------
    //
    // Retrieval is impossible unless two sample sets from the same function land
    // closer to each other than to a different function. This is the precondition
    // and it is measured before anything is built on it.
    std::printf("  === 0. DO TWO SAMPLES OF THE SAME FUNCTION LOOK ALIKE? ===\n");
    std::printf("    encoding    | same family | different family |    gap\n");
    std::printf("    ------------+-------------+------------------+--------\n");
    for (int mode = 0; mode < 2; ++mode) {
        auto enc = mode == 0 ? enc_task_naive : enc_task_structural;
        std::vector<Glyph> a, b;
        g_s = 1234;
        for (const auto& f : fams) { a.push_back(enc(sample(f, 8))); }
        for (const auto& f : fams) { b.push_back(enc(sample(f, 8))); }
        double same = 0, diff = 0;
        std::size_t nd = 0;
        for (std::size_t i = 0; i < fams.size(); ++i) {
            same += a[i].similarity(b[i]);
            for (std::size_t j = 0; j < fams.size(); ++j)
                if (i != j) { diff += a[i].similarity(b[j]); ++nd; }
        }
        same /= (double)fams.size();
        diff /= (double)nd;
        std::printf("    %-11s | %11.3f | %16.3f | %+.3f\n",
                    mode == 0 ? "naive" : "structural", same, diff, same - diff);
    }
    std::printf("    A gap at or below zero means retrieval cannot work, whatever is\n"
                "    built on top of it.\n\n");

    // --- 1. SYNTHESISE ONCE, THEN RETRIEVE AND RUN ---------------------------
    //
    // Every family is solved once by search and the recipe is bound to the task
    // that produced it. Then a FRESH sample of the same family is encoded, a
    // program is retrieved, and the program is EXECUTED on the fresh cases.
    // Nothing is scored on labels.
    std::printf("  === 1. SOLVE ONCE, THEN RETRIEVE AND EXECUTE ===\n");
    khora::chiasm::Chiasm mem_naive, mem_struct;
    std::vector<khora::techne::Recipe> recipes;
    std::vector<std::string> names;
    double synth_ms = 0;

    g_s = 999;
    for (const auto& f : fams) {
        const auto cs = sample(f, 10);
        khora::techne::Spec spec;
        spec.name = f.name;
        spec.cases = cs;
        const auto t0 = std::chrono::steady_clock::now();
        const auto br = khora::techne::solve_one(spec, 30000, nullptr);
        synth_ms += std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - t0).count();
        if (br.proof == khora::techne::Proof::None) {
            std::printf("    %-10s : NOT SOLVED by search -- excluded\n", f.name);
            continue;
        }
        const std::size_t idx = recipes.size();
        recipes.push_back(br.recipe);
        names.push_back(f.name);
        const Glyph prog = Glyph::from_hash(std::string("prog:") + f.name);
        mem_naive.remember({{"task", f.name, enc_task_naive(cs)},
                            {"prog", f.name, prog}});
        mem_struct.remember({{"task", f.name, enc_task_structural(cs)},
                             {"prog", f.name, prog}});
        (void)idx;
    }
    std::printf("    %zu of %zu families solved by search, %.0f ms of synthesis in total\n",
                recipes.size(), fams.size(), synth_ms);
    if (recipes.empty()) { std::printf("    nothing to retrieve\n"); return 0; }

    // --- 2. THE MEASUREMENT: DOES THE RETRIEVED PROGRAM ACTUALLY WORK? -------
    Score naive_run, struct_run, naive_lbl, struct_lbl, random_run;
    double retrieve_ms = 0;
    const std::size_t trials = 20;

    g_s = 555;
    for (std::size_t t = 0; t < trials; ++t) {
        for (std::size_t k = 0; k < names.size(); ++k) {
            const auto& fam = *std::find_if(fams.begin(), fams.end(),
                [&](const Family& f) { return names[k] == f.name; });
            const auto fresh = sample(fam, 10);

            auto try_one = [&](khora::chiasm::Chiasm& mem, const Glyph& q,
                               Score& runs, Score& lbls) {
                const auto t0 = std::chrono::steady_clock::now();
                const auto r = mem.recall("task", q, "prog");
                retrieve_ms += std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() - t0).count();
                ++lbls.n;
                if (r.label == names[k]) ++lbls.hit;
                // THE ONLY MEASUREMENT THAT MATTERS: run whatever came back.
                ++runs.n;
                const auto it = std::find(names.begin(), names.end(), r.label);
                if (it == names.end()) return;
                const auto& rec = recipes[(std::size_t)(it - names.begin())];
                bool all = true;
                for (const auto& c : fresh)
                    if (rec.apply(c.in, nullptr) != c.out) { all = false; break; }
                if (all) ++runs.hit;
            };

            try_one(mem_naive,  enc_task_naive(fresh),      naive_run,  naive_lbl);
            try_one(mem_struct, enc_task_structural(fresh), struct_run, struct_lbl);

            // DUMB BASELINE: take a program at random and run it. With eight
            // families this is not negligible, and several of them agree on some
            // inputs, so it is the honest floor rather than 1/N.
            ++random_run.n;
            const auto& rec = recipes[rnd() % recipes.size()];
            bool all = true;
            for (const auto& c : fresh)
                if (rec.apply(c.in, nullptr) != c.out) { all = false; break; }
            if (all) ++random_run.hit;
        }
    }

    std::printf("\n    method                     | retrieved right | RAN CORRECTLY on fresh cases\n");
    std::printf("    ---------------------------+-----------------+-----------------------------\n");
    std::printf("    naive task encoding        | %6.1f%% (%zu/%zu) | %6.1f%% (%zu/%zu)\n",
                pc(naive_lbl), naive_lbl.hit, naive_lbl.n, pc(naive_run), naive_run.hit, naive_run.n);
    std::printf("    structural task encoding   | %6.1f%% (%zu/%zu) | %6.1f%% (%zu/%zu)\n",
                pc(struct_lbl), struct_lbl.hit, struct_lbl.n, pc(struct_run), struct_run.hit, struct_run.n);
    std::printf("    a program picked at random |        --       | %6.1f%% (%zu/%zu)\n",
                pc(random_run), random_run.hit, random_run.n);

    std::printf("\n    synthesis: %.1f ms for %zu programs (%.1f ms each)\n",
                synth_ms, recipes.size(), synth_ms / (double)recipes.size());
    std::printf("    retrieval: %.3f ms for %zu queries (%.4f ms each)\n",
                retrieve_ms, naive_lbl.n + struct_lbl.n,
                retrieve_ms / (double)std::max<std::size_t>(naive_lbl.n + struct_lbl.n, 1));

    std::printf("\n  WHAT THIS DOES NOT SHOW. Eight families, all of them rearrangements of\n"
                "  a list, which is exactly the class the structural encoding was designed\n"
                "  for -- it says nothing about arithmetic or predicate programs, and the\n"
                "  encoding has no way to describe them. The programs are also retrieved\n"
                "  from a memory that contains the very family being asked for; retrieving\n"
                "  something USEFUL for a family never stored is a different and harder\n"
                "  question this does not touch.\n");
    return 0;
}
