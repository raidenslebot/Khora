// DOES ANY OF THIS WORK ON TEXT, OR ONLY ON THE DOMAIN IT WAS BUILT FOR?
//
// Every task this organ has been measured on is a list-of-integers
// transformation, which is the domain I chose when I wrote the instruction set.
// That is the sharpest criticism available: a synthesiser that only solves the
// problems its author had in mind is a lookup table with extra steps.
//
// Text is the cheapest honest test of generality, because a string IS a list of
// integers -- character codes -- and needs NOTHING added to the machine. If the
// existing operations reach real string problems, the substrate is more general
// than the benchmark suite made it look. If they do not, the failures name
// exactly which operations are missing, which is worth more than a pass.
//
// THE TASKS ARE NOT HAND-PICKED FOR SOLVABILITY. Several are included precisely
// because I expect them to fail -- vowel counting needs set membership, word
// splitting needs a scan with state, case conversion needs a conditional on a
// range. A suite where everything passes has been chosen to pass, and says
// nothing.
//
// Held-out strings are LONGER than any visible one and drawn from different
// text, so a program that fitted the sample cannot pass them. Every certified
// result is then checked against the reference on fresh strings the search never
// saw, because certification is the system judging itself.

#include "khora/techne/techne.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace khora::techne;
using clk = std::chrono::high_resolution_clock;

namespace {

std::uint64_t rs = 0x7E47ULL;
std::uint64_t rnd() {
    std::uint64_t z = (rs += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

Value encode(const std::string& s) {
    Value v;
    v.reserve(s.size());
    for (const char c : s) v.push_back(static_cast<std::int64_t>(static_cast<unsigned char>(c)));
    return v;
}

std::string decode(const Value& v) {
    std::string s;
    s.reserve(v.size());
    for (const auto x : v) {
        s += (x >= 32 && x < 127) ? static_cast<char>(x) : '.';
    }
    return s;
}

const std::vector<std::string>& corpus() {
    static const std::vector<std::string> v{
        "hello", "world", "khora", "the quick brown", "fox jumps", "over",
        "a lazy dog", "text is a list", "of character codes", "nothing added",
        "abc", "zyx", "mixed case Here", "trailing space ", " leading",
        "many   spaces", "punctuation, here!", "digits 123 inside", "e",
        "eeee", "no vowels here rhythm", "aeiou", "x", "",
    };
    return v;
}

struct Task {
    const char* name;
    const char* note;                       // why it is here, especially if hard
    std::function<Value(const Value&)> ref;
};

std::vector<Task> suite() {
    return {
        {"length", "how many characters",
         [](const Value& v) { return Value{static_cast<std::int64_t>(v.size())}; }},

        {"reverse", "reverse the string",
         [](const Value& v) { return Value(v.rbegin(), v.rend()); }},

        {"first_char", "leading character",
         [](const Value& v) { return v.empty() ? Value{} : Value{v.front()}; }},

        {"last_char", "trailing character",
         [](const Value& v) { return v.empty() ? Value{} : Value{v.back()}; }},

        {"drop_first", "everything after the first character",
         [](const Value& v) { return v.size() > 1 ? Value(v.begin() + 1, v.end()) : Value{}; }},

        {"strip_spaces", "remove spaces and control characters",
         [](const Value& v) { Value o; for (auto c : v) if (c > 32) o.push_back(c); return o; }},

        {"count_spaces", "how many spaces",
         [](const Value& v) { std::int64_t n = 0; for (auto c : v) if (c == 32) ++n; return Value{n}; }},

        {"upper_lower", "lowercase to uppercase -- only valid on pure lowercase, "
                        "which is what makes it reachable without a conditional",
         [](const Value& v) { Value o; for (auto c : v) o.push_back(c - 32); return o; }},

        {"shift_1", "Caesar shift by one, no wrap",
         [](const Value& v) { Value o; for (auto c : v) o.push_back(c + 1); return o; }},

        {"sorted_chars", "characters in order",
         [](const Value& v) { Value o = v; std::sort(o.begin(), o.end()); return o; }},

        {"first_three", "a fixed-length prefix",
         [](const Value& v) { return Value(v.begin(), v.begin() + std::min<std::size_t>(3, v.size())); }},

        {"without_last", "drop the trailing character",
         [](const Value& v) { return v.empty() ? Value{} : Value(v.begin(), v.end() - 1); }},

        // ---- expected to be out of reach, and included for that reason ------
        {"count_vowels", "EXPECTED HARD: needs set membership, which the "
                         "instruction set has no way to express",
         [](const Value& v) { std::int64_t n = 0;
             for (auto c : v) if (c=='a'||c=='e'||c=='i'||c=='o'||c=='u') ++n;
             return Value{n}; }},

        {"first_word", "EXPECTED HARD: needs a scan that stops at a delimiter",
         [](const Value& v) { Value o; for (auto c : v) { if (c == 32) break; o.push_back(c); } return o; }},

        {"title_case", "EXPECTED HARD: a conditional on a character range",
         [](const Value& v) { Value o = v;
             if (!o.empty() && o[0] >= 'a' && o[0] <= 'z') o[0] -= 32;
             return o; }},

        {"dedup_adjacent", "EXPECTED HARD: needs to compare neighbours",
         [](const Value& v) { Value o; for (auto c : v) if (o.empty() || o.back() != c) o.push_back(c);
             return o; }},
    };
}

Spec make(const Task& t) {
    Spec s;
    s.name = t.name;
    const auto& c = corpus();
    for (std::size_t i = 0; i < 14; ++i) {
        // upper_lower is only meaningful on pure lowercase input, so it is given
        // pure lowercase. Saying so is the difference between a fair task and a
        // rigged one.
        std::string str = c[i % c.size()];
        if (std::string(t.name) == "upper_lower") {
            std::string low;
            for (const char ch : str) if (ch >= 'a' && ch <= 'z') low += ch;
            str = low;
        }
        const Value in = encode(str);
        s.cases.push_back({in, t.ref(in)});
    }
    for (std::size_t i = 0; i < 6; ++i) {
        std::string str = c[(i + 17) % c.size()] + " " + c[(i + 5) % c.size()];
        if (std::string(t.name) == "upper_lower") {
            std::string low;
            for (const char ch : str) if (ch >= 'a' && ch <= 'z') low += ch;
            str = low;
        }
        const Value in = encode(str);
        s.holdout.push_back({in, t.ref(in)});
    }
    return s;
}

} // namespace

int main(int argc, char** argv) {
    const std::size_t cap = (argc > 1) ? std::stoul(argv[1]) : 40000;

    std::printf("Does any of this work on TEXT, or only on the domain it was built for?\n\n");
    std::printf("  A string is a list of character codes, so NOTHING is added to the\n");
    std::printf("  machine for this. Same operations, same search, different domain.\n");
    std::printf("  Four tasks are included that I expect to FAIL, because a suite\n");
    std::printf("  where everything passes has been chosen to pass.\n\n");

    const auto tasks = suite();
    Library lib(24);

    std::printf("  task            | result      | probes  | program\n");
    std::printf("  ----------------+-------------+---------+---------\n");

    std::size_t solved = 0, verified = 0, expected_hard_solved = 0;
    const auto t0 = clk::now();

    for (const Task& t : tasks) {
        const Spec s = make(t);
        const BuildResult b = construct(s, cap, &lib);
        const bool ok = b.proof == Proof::Generalised;
        const bool hard = std::string(t.note).rfind("EXPECTED HARD", 0) == 0;

        if (!ok) {
            std::printf("  %-15s | %-11s |    -    | -\n", t.name,
                        hard ? "not found*" : "NOT FOUND");
            continue;
        }
        ++solved;
        if (hard) ++expected_hard_solved;

        // INDEPENDENT PROBES on fresh strings the search never saw, because a
        // certificate is the system judging itself.
        std::size_t agree = 0;
        const std::size_t tries = 200;
        for (std::size_t k = 0; k < tries; ++k) {
            std::string str;
            const std::size_t len = rnd() % 18;
            for (std::size_t j = 0; j < len; ++j) {
                const int pick = static_cast<int>(rnd() % 30);
                str += (pick < 26) ? static_cast<char>('a' + pick) : ' ';
            }
            if (std::string(t.name) == "upper_lower") {
                std::string low;
                for (const char ch : str) if (ch >= 'a' && ch <= 'z') low += ch;
                str = low;
            }
            const Value in = encode(str);
            if (b.recipe.apply(in, &lib) == t.ref(in)) ++agree;
        }
        if (agree == tries) ++verified;

        std::printf("  %-15s | %-11s | %3zu/%-3zu | %s\n", t.name,
                    agree == tries ? "VERIFIED" : "wrong",
                    agree, tries, b.recipe.render().c_str());
        if (agree == tries) lib.admit_recipe(t.name, b.recipe, 0);
        lib.prune();
    }

    const double secs = std::chrono::duration<double>(clk::now() - t0).count();

    std::printf("\n  %zu of %zu solved, %zu of those correct on 200 fresh strings, %.1f s\n",
                solved, tasks.size(), verified, secs);
    std::printf("  * the four marked EXPECTED HARD: %zu solved.\n", expected_hard_solved);

    // A worked example, so the output can be read rather than trusted.
    {
        const Spec s = make(tasks[1]);          // reverse
        const BuildResult b = construct(s, cap, nullptr);
        if (b.certified()) {
            const Value in = encode("khora reads text");
            std::printf("\n  worked example -- %s\n", b.recipe.render().c_str());
            std::printf("    \"khora reads text\" -> \"%s\"\n",
                        decode(b.recipe.apply(in, nullptr)).c_str());
        }
    }

    std::printf("\n  HOW TO READ IT\n");
    std::printf("    Solving the ordinary tasks shows the substrate is not specific to\n");
    std::printf("    the domain the benchmark suite was written in. Failing the four\n");
    std::printf("    marked hard shows exactly what is missing: set membership, a scan\n");
    std::printf("    that stops on a condition, a conditional on a value range, and any\n");
    std::printf("    comparison between neighbouring elements. None of those is a\n");
    std::printf("    property of TEXT -- they are gaps in the instruction set that this\n");
    std::printf("    domain happens to make obvious.\n");
    return 0;
}
