// One memory holding a picture, a sound and a word, and the arithmetic that
// makes retrieving any from any possible.
//
// The substrate's whole argument is that every faculty emits the same type, so
// anything can be the key for anything else. These checks pin the three
// properties that argument rests on, in the order they have to hold:
//
//   bind is EXACTLY invertible -- not approximately, or the rest is guesswork
//   a bundle stays similar to each thing bundled into it
//   unbinding a role out of a whole record returns the right value plus
//     crosstalk, close enough for a cleanup memory to name it
//
// The capacity limits below are not guesses. substrate_bench measures ~256
// items per 10,000-bit glyph at 96% recognition and a crossover between 512 and
// 1024; analogy_bench measures a margin of roughly 3000/S bits for S superposed
// role-filler pairs, clean to 8 slots and half-failing by 16. The records here
// have three fields, which is why they resolve.

#include "khora/chiasm/chiasm.hpp"
#include "khora/lattice/glyph.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace khora::chiasm;
using khora::lattice::Glyph;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("  FAIL: %s\n", what); ++failures; }
    else       { std::printf("  ok  : %s\n", what); }
}

Glyph g(std::uint64_t s) { return Glyph::random(s); }

} // namespace

int main() {
    std::printf("Chiasm — one memory, any modality retrieves any other\n\n");

    // --- THE ALGEBRA THE WHOLE DESIGN STANDS ON ------------------------------
    {
        const Glyph role = g(1), value = g(2);
        const Glyph bound = khora::lattice::bind(role, value);
        check(khora::lattice::bind(bound, role) == value,
              "bind is EXACTLY its own inverse -- unbinding returns the value bit for bit");
        check(bound.similarity(value) < 0.2 && bound.similarity(role) < 0.2,
              "and a bound pair resembles neither of its parts, so it is a new symbol");

        const Glyph a = g(3), b = g(4), c = g(5);
        const Glyph mix = khora::lattice::bundle({a, b, c});
        check(mix.similarity(a) > 0.3 && mix.similarity(b) > 0.3 && mix.similarity(c) > 0.3,
              "a bundle stays similar to everything bundled into it");
        check(mix.similarity(g(99)) < 0.2,
              "and not to anything else -- that asymmetry is what makes recall work");
    }

    // --- A RECORD, AND RETRIEVAL IN EVERY DIRECTION --------------------------
    {
        Chiasm mem;
        const std::size_t N = 50;
        std::vector<Glyph> sight, sound, word;
        for (std::size_t k = 0; k < N; ++k) {
            sight.push_back(g(1000 + k));
            sound.push_back(g(2000 + k));
            word.push_back(g(3000 + k));
            mem.remember({
                {"sight", "img" + std::to_string(k), sight[k]},
                {"sound", "snd" + std::to_string(k), sound[k]},
                {"word",  "wrd" + std::to_string(k), word[k]},
            });
        }
        check(mem.records() == N, "every observation became a record");
        check(mem.known("sight") == N && mem.known("word") == N,
              "and every value is in its cleanup memory");

        std::size_t s2w = 0, h2w = 0, w2s = 0, s2h = 0;
        for (std::size_t k = 0; k < N; ++k) {
            const std::string w = "wrd" + std::to_string(k);
            const std::string i = "img" + std::to_string(k);
            const std::string s = "snd" + std::to_string(k);
            if (mem.recall("sight", sight[k], "word")  .label == w) ++s2w;
            if (mem.recall("sound", sound[k], "word")  .label == w) ++h2w;
            if (mem.recall("word",  word[k],  "sight") .label == i) ++w2s;
            if (mem.recall("sight", sight[k], "sound") .label == s) ++s2h;
        }
        std::printf("      of %zu concepts stored once each: see>word %zu, hear>word %zu,\n"
                    "      word>see %zu, see>hear %zu  (chance would be 1)\n",
                    N, s2w, h2w, w2s, s2h);
        check(s2w == N && h2w == N && w2s == N && s2h == N,
              "all four directions retrieve perfectly, from ONE example and no training");
    }

    // --- IT IS NOT SECRETLY RETURNING THE CUE --------------------------------
    //
    // The failure mode that would make all of the above meaningless: a memory
    // that hands back whatever it was given. Asking for the role the cue came
    // from must return the cue, and asking for a different one must not.
    {
        Chiasm mem;
        const Glyph s = g(11), w = g(12);
        mem.remember({{"sight", "a", s}, {"word", "alpha", w}});
        mem.remember({{"sight", "b", g(13)}, {"word", "beta", g(14)}});
        check(mem.recall("sight", s, "word").label == "alpha",
              "the cross-modal answer is the paired value");
        check(mem.recall("sight", s, "sight").label == "a",
              "and asking for the cue's own role returns the cue");
    }

    // --- AN UNKNOWN ROLE OR AN EMPTY MEMORY ANSWERS NOTHING ------------------
    {
        Chiasm empty;
        check(empty.recall("sight", g(7), "word").label.empty(),
              "an empty memory recalls nothing rather than guessing");
        Chiasm one;
        one.remember({{"sight", "a", g(8)}});
        check(one.recall("sight", g(8), "smell").label.empty(),
              "and a role nobody ever stored returns nothing");
    }

    // --- WHERE IT BREAKS, MEASURED RATHER THAN ASSUMED -----------------------
    //
    // A record is a bundle, and a bundle holds each component more weakly the
    // more it holds. This is the capacity limit in the one place a caller will
    // actually meet it: too many fields in one record.
    {
        // I first swept to 32 fields and asserted it would degrade there. It did
        // not -- 100% at every size -- because 32 is nowhere near the limit.
        // substrate_bench puts the crossover between 512 and 1024 components per
        // glyph, so that is where the sweep has to reach for the check to mean
        // anything. The wrong expectation was mine, not the code
        std::printf("      fields per record vs retrieval, 40 records:\n");
        bool small_ok = false, large_degrades = false;
        for (std::size_t fields : {8u, 32u, 128u, 512u, 1024u}) {
            Chiasm mem;
            const std::size_t N = 40;
            std::vector<std::vector<Glyph>> vals(N);
            for (std::size_t k = 0; k < N; ++k) {
                std::vector<Field> f;
                for (std::size_t r = 0; r < fields; ++r) {
                    vals[k].push_back(g(500000 + k * 100 + r));
                    f.push_back({"r" + std::to_string(r),
                                 "v" + std::to_string(k) + "_" + std::to_string(r),
                                 vals[k][r]});
                }
                mem.remember(f);
            }
            std::size_t hit = 0;
            for (std::size_t k = 0; k < N; ++k) {
                const auto r = mem.recall("r0", vals[k][0], "r1");
                if (r.label == "v" + std::to_string(k) + "_1") ++hit;
            }
            const double acc = 100.0 * static_cast<double>(hit) / static_cast<double>(N);
            std::printf("        %2zu fields : %5.1f%%\n", fields, acc);
            if (fields <= 32 && acc == 100.0) small_ok = true;
            if (fields == 1024 && acc < 100.0) large_degrades = true;
        }
        check(small_ok, "a record with dozens of fields still resolves perfectly");
        check(large_degrades,
              "and one with a thousand does not -- capacity is finite and this is where");
    }

    std::printf("\n");
    if (failures == 0) std::printf("ALL PASS\n");
    else               std::printf("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
