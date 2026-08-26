// A SYSTEM THAT GETS FASTER AT PROGRAMMING THE MORE IT PROGRAMS.
//
// capability_bench showed a program can be retrieved by looking at the problem,
// 84.4% correct against a 15% floor, at 0.0013 ms against 30 ms to derive it.
// That is a component. This is what it is FOR.
//
// The loop: encounter a task, try to remember a program for it, RUN the
// remembered program on the task's own cases, and fall back to search only if it
// fails. Whatever search produces is then bound to the task, so the next
// encounter is a lookup.
//
// WHY THE VERIFICATION STEP MAKES THIS SAFE RATHER THAN CLEVER. Retrieval is
// approximate and will return the wrong program. The cost of being wrong is one
// execution over ten cases -- microseconds -- and then the search runs exactly as
// it would have. So the memory can only save time, never correctness, and the
// claim is not "retrieval is reliable" but "retrieval is free to be wrong". That
// is a much weaker requirement and it is why this works at 84% rather than
// needing 99%.
//
// THE HONEST COMPARISON is against the same stream solved by search every time,
// and against the library techne already has. A learned library of subroutines
// is a DIFFERENT mechanism -- it makes each search cheaper by widening the
// vocabulary -- and it is already in the tree, so the memory has to earn its
// place beside it rather than against a strawman.
//
// The stream is Zipf-distributed over families, because the whole premise is
// that some problems recur.
//
// I ALSO RAN A UNIFORM STREAM AND CALLED IT THE NO-REPETITION CASE. It is not.
// With ten families over a hundred and twenty tasks, a uniform draw still hits
// each family about twelve times, so both streams repeat heavily and the
// comparison isolates nothing. It stays in the table because it is a second
// distribution and the policy should survive both, but the gap between their
// speedups is a fact about WHICH families each stream favours -- Zipf
// concentrates on the cheap ones -- and not about repetition.
//
// What does show compounding is the hit rate through the stream: the memory is
// empty at task one and the question is whether it fills.

#include "khora/chiasm/chiasm.hpp"
#include "khora/lattice/glyph.hpp"
#include "khora/techne/techne.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

using khora::lattice::Glyph;
using khora::techne::Value;
using khora::techne::Case;

std::uint64_t g_s = 424242;
std::uint64_t rnd() { g_s ^= g_s << 13; g_s ^= g_s >> 7; g_s ^= g_s << 17; return g_s; }

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

// The structural encoding capability_bench measured at a +0.557 gap: describe
// where each output element came from, not what it was.
Glyph enc_task(const std::vector<Case>& cs) {
    std::vector<Glyph> terms;
    for (const auto& c : cs) {
        for (std::size_t j = 0; j < c.out.size(); ++j) {
            std::size_t src = c.in.size();
            for (std::size_t i = 0; i < c.in.size(); ++i)
                if (c.in[i] == c.out[j]) { src = i; break; }
            terms.push_back(khora::lattice::bind(pos_glyph(j),
                            khora::lattice::permute(pos_glyph(src), 1)));
        }
        terms.push_back(khora::lattice::bind(Glyph::from_hash("#len"),
                        num_glyph((std::int64_t)c.out.size() - (std::int64_t)c.in.size())));
    }
    if (terms.empty()) return Glyph::from_hash("<none>");
    return khora::lattice::bundle(std::span<const Glyph>(terms));
}

struct Family { const char* name; std::function<Value(const Value&)> f; };

std::vector<Family> families() {
    return {
        {"reverse",   [](const Value& v) { return Value(v.rbegin(), v.rend()); }},
        {"sort",      [](const Value& v) { Value o = v; std::sort(o.begin(), o.end()); return o; }},
        {"tail",      [](const Value& v) { return v.empty() ? v : Value(v.begin() + 1, v.end()); }},
        {"init",      [](const Value& v) { return v.empty() ? v : Value(v.begin(), v.end() - 1); }},
        {"rot1",      [](const Value& v) { if (v.empty()) return v; Value o(v.begin()+1, v.end()); o.push_back(v[0]); return o; }},
        {"dup_first", [](const Value& v) { Value o = v; if (!o.empty()) o.insert(o.begin(), o[0]); return o; }},
        {"take2",     [](const Value& v) { return Value(v.begin(), v.begin() + std::min<std::size_t>(2, v.size())); }},
        {"sort_desc", [](const Value& v) { Value o = v; std::sort(o.begin(), o.end(), std::greater<>()); return o; }},
        {"last2",     [](const Value& v) { return v.size() < 2 ? v : Value(v.end() - 2, v.end()); }},
        {"rot2",      [](const Value& v) { if (v.size() < 2) return v; Value o(v.begin()+2, v.end()); o.push_back(v[0]); o.push_back(v[1]); return o; }},
    };
}

std::vector<Case> sample(const Family& fam, std::size_t n) {
    std::vector<Case> cs;
    for (std::size_t k = 0; k < n; ++k) {
        Value in;
        const std::size_t len = 4 + rnd() % 4;
        for (std::size_t i = 0; i < len; ++i) in.push_back((std::int64_t)(rnd() % 40));
        cs.emplace_back(in, fam.f(in));
    }
    return cs;
}

bool solves(const khora::techne::Recipe& r, const std::vector<Case>& cs) {
    for (const auto& c : cs) if (r.apply(c.in, nullptr) != c.out) return false;
    return true;
}

struct Run {
    std::size_t solved = 0, from_memory = 0, wrong_guesses = 0, searches = 0;
    double      ms = 0.0;
    // Hits and cost per window of the stream, which is where compounding is
    // visible at all. A single total cannot distinguish a memory that filled up
    // immediately from one that never did.
    std::vector<std::size_t> win_hits, win_n;
    std::vector<double>      win_ms;
};

} // namespace

int main(int argc, char** argv) {
    const std::size_t stream_len = (argc > 1) ? std::stoul(argv[1]) : 120;
    const auto fams = families();

    std::printf("Compound — does remembering a program make the next one cheaper?\n\n");
    std::printf("  %zu tasks over %zu families, drawn Zipf so some recur far more than\n"
                "  others, and again uniform as a second distribution. NEITHER is a\n"
                "  no-repetition case -- at ten families a uniform draw still hits each\n"
                "  about twelve times -- so the compounding is read off the windowed hit\n"
                "  rate at the end rather than off the difference between the two.\n\n",
                stream_len, fams.size());

    // A fixed stream, so both policies see EXACTLY the same tasks in the same
    // order. Comparing two policies on two different streams would measure the
    // streams.
    struct Item { std::size_t fam; std::vector<Case> cases; };
    auto build_stream = [&](bool zipf) {
        std::vector<Item> out;
        g_s = 20260826;
        for (std::size_t t = 0; t < stream_len; ++t) {
            std::size_t f;
            if (zipf) {
                // rank r drawn with probability ~ 1/r: a few families dominate.
                const double u = (double)(rnd() % 100000) / 100000.0;
                f = (std::size_t)std::floor(std::pow((double)fams.size(), u)) - 1;
                if (f >= fams.size()) f = fams.size() - 1;
            } else {
                f = rnd() % fams.size();
            }
            out.push_back({f, sample(fams[f], 10)});
        }
        return out;
    };

    auto policy_search_only = [&](const std::vector<Item>& stream) {
        Run r;
        for (const auto& it : stream) {
            khora::techne::Spec spec;
            spec.name = fams[it.fam].name;
            spec.cases = it.cases;
            const auto t0 = std::chrono::steady_clock::now();
            const auto br = khora::techne::solve_one(spec, 30000, nullptr);
            r.ms += std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - t0).count();
            ++r.searches;
            if (br.proof != khora::techne::Proof::None && solves(br.recipe, it.cases)) ++r.solved;
        }
        return r;
    };

    auto policy_remember_first = [&](const std::vector<Item>& stream) {
        Run r;
        khora::chiasm::Chiasm mem;
        std::vector<khora::techne::Recipe> recipes;
        std::vector<std::string> names;
        for (const auto& it : stream) {
            const auto t0 = std::chrono::steady_clock::now();
            const Glyph q = enc_task(it.cases);

            bool done = false;
            if (mem.records() > 0) {
                const auto got = mem.recall("task", q, "prog");
                const auto at = std::find(names.begin(), names.end(), got.label);
                if (at != names.end()) {
                    // THE CHECK THAT MAKES A WRONG GUESS HARMLESS.
                    if (solves(recipes[(std::size_t)(at - names.begin())], it.cases)) {
                        ++r.solved; ++r.from_memory; done = true;
                    } else {
                        ++r.wrong_guesses;
                    }
                }
            }
            if (!done) {
                khora::techne::Spec spec;
                spec.name = fams[it.fam].name;
                spec.cases = it.cases;
                const auto br = khora::techne::solve_one(spec, 30000, nullptr);
                ++r.searches;
                if (br.proof != khora::techne::Proof::None && solves(br.recipe, it.cases)) {
                    ++r.solved;
                    // Bind it to the task that produced it. The label is the
                    // recipe's own identity, not the family name -- the loop is
                    // never told which family anything came from.
                    const std::string label = "r" + std::to_string(recipes.size());
                    recipes.push_back(br.recipe);
                    names.push_back(label);
                    mem.remember({{"task", label, q},
                                  {"prog", label, Glyph::from_hash("prog:" + label)}});
                }
            }
            const double dt = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - t0).count();
            r.ms += dt;
            const std::size_t w = (&it - stream.data()) / 20;
            if (r.win_n.size() <= w) { r.win_n.resize(w + 1, 0); r.win_hits.resize(w + 1, 0);
                                       r.win_ms.resize(w + 1, 0.0); }
            ++r.win_n[w];
            r.win_ms[w] += dt;
            if (done) ++r.win_hits[w];
        }
        return r;
    };

    auto report = [&](const char* label, const Run& r) {
        std::printf("    %-22s | %6zu | %7zu | %6zu | %6zu | %9.0f | %7.2f\n",
                    label, r.solved, r.from_memory, r.wrong_guesses, r.searches,
                    r.ms, r.ms / (double)stream_len);
    };

    std::printf("    policy                 | solved | from mem | wrong | search |  total ms | ms/task\n");
    std::printf("    -----------------------+--------+----------+-------+--------+-----------+--------\n");

    const auto zipf = build_stream(true);
    const Run a = policy_search_only(zipf);
    const Run b = policy_remember_first(zipf);
    report("search every time", a);
    report("remember first", b);

    const auto flat = build_stream(false);
    const Run c = policy_search_only(flat);
    const Run d = policy_remember_first(flat);
    report("search, uniform stream", c);
    report("remember, uniform", d);

    std::printf("\n    SOLVED IS THE COLUMN THAT MATTERS FIRST. Remembering may only make the\n"
                "    stream cheaper, never less correct: a retrieved program is executed on\n"
                "    the task's own cases before it is accepted, and a failure costs one\n"
                "    execution and then the same search that would have run anyway. If the\n"
                "    solved counts differ, the memory is broken and the timing is irrelevant.\n");
    if (a.solved == b.solved && c.solved == d.solved)
        std::printf("    They match on both streams.\n");
    else
        std::printf("    THEY DO NOT MATCH -- the memory is changing outcomes, not just cost.\n");

    if (a.ms > 0)
        std::printf("\n    Zipf stream:    %.2fx faster, %zu of %zu tasks answered from memory,\n"
                    "                    %zu wrong guesses caught by execution and paid for.\n",
                    a.ms / std::max(b.ms, 1e-9), b.from_memory, stream_len, b.wrong_guesses);
    if (c.ms > 0)
        std::printf("    Uniform stream: %.2fx faster, %zu of %zu from memory.\n",
                    c.ms / std::max(d.ms, 1e-9), d.from_memory, stream_len);
    std::printf("    The two speedups differ because Zipf favours the families that are\n"
                "    CHEAP to synthesise, so its baseline is smaller. Both streams repeat\n"
                "    heavily at ten families over %zu tasks; neither is a no-repetition\n"
                "    case and this bench does not have one.\n", stream_len);

    // --- WHERE THE COMPOUNDING IS ACTUALLY VISIBLE ---------------------------
    std::printf("\n    hit rate and cost through the stream, in windows of 20:\n");
    std::printf("      window |  zipf: from memory | ms/task |  uniform: from memory | ms/task\n");
    std::printf("      -------+--------------------+---------+-----------------------+--------\n");
    for (std::size_t w = 0; w < b.win_n.size() && w < d.win_n.size(); ++w) {
        std::printf("      %2zu-%2zu  | %9zu of %-6zu | %7.1f | %12zu of %-6zu | %7.1f\n",
                    w * 20, w * 20 + 19,
                    b.win_hits[w], b.win_n[w], b.win_ms[w] / (double)std::max<std::size_t>(b.win_n[w], 1),
                    d.win_hits[w], d.win_n[w], d.win_ms[w] / (double)std::max<std::size_t>(d.win_n[w], 1));
    }
    std::printf("      The memory is empty at task one. If the first window is already at\n"
                "      the final rate there was nothing to compound and the total is just\n"
                "      a cache hit; if it climbs, the system is getting cheaper as it goes.\n");

    std::printf("\n  WHAT THIS DOES NOT SHOW. Ten families of list rearrangement, which is the\n"
                "  class the task encoding can describe at all. The learned LIBRARY techne\n"
                "  already has is a different mechanism -- it makes each search cheaper by\n"
                "  widening the vocabulary rather than skipping the search -- and it is\n"
                "  disabled here so the two are not confused; measuring them together is a\n"
                "  separate experiment.\n");
    return 0;
}
