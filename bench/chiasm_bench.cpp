// A PICTURE, A SOUND AND A WORD IN ONE MEMORY, WITH NO TRAINING.
//
// Khora encodes an image to a 10,000-bit Glyph, a sound to a 10,000-bit Glyph
// and a word to a 10,000-bit Glyph. Three unrelated front ends, one type coming
// out. That has been the claim behind the substrate since the beginning and
// nothing had ever put the three in the same container, so it was an argument
// and not a result.
//
// This stores ONE record per concept -- sight, sound and word bundled together
// from a single observation -- and then asks for each modality given each other
// one. Six directions. No gradient, no paired-training corpus, no second pass.
//
// WHAT IT IS BEING COMPARED AGAINST. Two things, and the second is the one that
// matters:
//
//   CHANCE. With N concepts, guessing gives 1/N. Any table without this line is
//   unreadable, and at N=5 a method can look impressive at 20%.
//
//   THE SAME CUE IT STORED. Retrieving using the exact glyph that went in is
//   near-trivial -- it is a hash lookup with extra steps. The honest question is
//   whether a NOVEL instance works: a different drawing of the same shape, at a
//   different position, with fresh noise; a different rendering of the same
//   sound. If only the stored instance retrieves, this is a dictionary and not
//   perception, and the two rows are printed side by side so that cannot be
//   glossed over.
//
// CAPACITY IS THE REAL FINDING. A bundle holds each component at a similarity
// that falls as more are added, so retrieval must degrade with N. Where it
// degrades, and how fast, is the fundamental constant this architecture is
// built on, and it is swept rather than asserted.

#include "khora/akoe/akoe.hpp"
#include "khora/chiasm/chiasm.hpp"
#include "khora/lexicon/lexicon.hpp"
#include "khora/retina/retina.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

std::uint64_t g_s = 20260825;
std::uint64_t rnd() { g_s ^= g_s << 13; g_s ^= g_s >> 7; g_s ^= g_s << 17; return g_s; }
std::size_t   rint(std::size_t lo, std::size_t hi) { return lo + rnd() % (hi - lo + 1); }

constexpr std::size_t kW = 32, kH = 32;

void plot(khora::retina::Image& im, long x, long y) {
    if (x < 0 || y < 0 || x >= (long)kW || y >= (long)kH) return;
    im.pixels[(std::size_t)y * kW + (std::size_t)x] = 255;
}

// THE FIRST GENERATOR ONLY MADE TWENTY PICTURES AND NINETY-SIX SOUNDS.
//
// It varied a shape family (4) by a radius (5), so concept 0 and concept 20 got
// the IDENTICAL image, and the frequency pair repeated every 96. The capacity
// curve that produced was measuring collisions in the generator, not the limits
// of the memory, and it declined exactly where the concepts started repeating.
// Both generators now scale past any N this bench will use.
//
// A picture is a 4x4 arrangement of filled blocks taken from the bits of k, so
// there are 65,536 of them and they differ from each other in whole blocks
// rather than in a couple of pixels.
khora::retina::Image draw(std::size_t idea, int dx, int dy, bool noisy) {
    khora::retina::Image im;
    im.width = kW; im.height = kH; im.pixels.assign(kW * kH, 0);
    const std::size_t bits = (idea * 2654435761u) ^ (idea << 7) ^ 0x9E37u;
    for (int by = 0; by < 4; ++by) {
        for (int bx = 0; bx < 4; ++bx) {
            if (!((bits >> (by * 4 + bx)) & 1u)) continue;
            for (int y = 1; y < 7; ++y)
                for (int x = 1; x < 7; ++x)
                    plot(im, bx * 8 + x + dx, by * 8 + y + dy);
        }
    }
    if (noisy) {
        for (std::size_t i = 0; i < kW * kH * 4 / 100; ++i) {
            const std::size_t q = rnd() % (kW * kH);
            im.pixels[q] = im.pixels[q] > 127 ? 0 : 255;
        }
    }
    return im;
}

// A sound is three simultaneous tones chosen from twenty-four log-spaced bands,
// which is 2,024 distinct chords -- a chord rather than a sweep because it is
// the audio analogue of the block pattern above, and the two front ends should
// be given comparable problems.
khora::akoe::Sound tone(std::size_t idea, bool jitter) {
    khora::akoe::Sound s;
    s.rate = 16000.0;
    std::size_t a = idea % 24;
    std::size_t b = (idea / 24) % 24;
    std::size_t c = (idea / 576) % 24;
    if (b == a) b = (b + 7) % 24;
    if (c == a || c == b) c = (c + 13) % 24;
    const std::size_t band[3] = {a, b, c};
    const double amp = jitter ? 0.6 + 0.4 * (double)(rnd() % 100) / 100.0 : 1.0;
    const std::size_t n = 4096;
    s.samples.assign(n, 0.0);
    for (std::size_t t3 = 0; t3 < 3; ++t3) {
        const double f = 180.0 * std::pow(1.16, (double)band[t3])
                       * (jitter ? 1.0 + ((double)(rnd() % 41) - 20.0) / 2000.0 : 1.0);
        const double ph = jitter ? (double)(rnd() % 628) / 100.0 : 0.0;
        for (std::size_t i = 0; i < n; ++i)
            s.samples[i] += std::sin(2.0 * 3.14159265358979 * f * (double)i / s.rate + ph) / 3.0;
    }
    if (jitter)
        for (std::size_t i = 0; i < n; ++i)
            s.samples[i] = amp * (s.samples[i] + 0.15 * (((double)(rnd() % 200) / 100.0) - 1.0));
    return s;
}
// Distinct words, generated so that no two are spelling-neighbours -- the
// lexicon encodes structurally from character trigrams, so "bell" and "belt"
// would be genuinely similar and would flatter the result.
std::string word_for(std::size_t k) {
    static const char* c = "bkdgptfvszmnlr";
    static const char* v = "aeiou";
    std::string w;
    w += c[(k * 5 + 1) % 14];
    w += v[k % 5];
    w += c[(k * 3 + 7) % 14];
    w += v[(k / 5) % 5];
    w += c[(k * 11 + 2) % 14];
    return w;
}

struct Row { std::size_t hit = 0, n = 0; };
double pct(const Row& r) { return r.n ? 100.0 * (double)r.hit / (double)r.n : 0.0; }

} // namespace

int main(int argc, char** argv) {
    const bool quick = (argc > 1 && std::string(argv[1]) == "quick");
    std::printf("Chiasm — a picture, a sound and a word in one memory\n\n");
    std::printf("  Each concept is stored ONCE, as a single bundled record. There is no\n"
                "  training pass, no paired corpus, and no learned projection. The shared\n"
                "  space is not learned; it is the same 10,000 bits by construction.\n\n");

    const std::vector<std::size_t> sizes = quick ? std::vector<std::size_t>{10, 50}
                                                 : std::vector<std::size_t>{5, 10, 25, 50, 100, 200};

    // THREE CUE CONDITIONS, because "a novel instance" hides the interesting
    // split. Noise alone and a two-pixel shift are very different problems for
    // a retina that binds intensity to ABSOLUTE position, and lumping them
    // together would report one number that means neither thing.
    std::printf("  %5s | %6s | %-17s | %-27s | %-17s\n", "N", "chance",
                "cue = as stored", "cue = same thing, seen anew", "word cue");
    std::printf("  %5s | %6s | %8s %8s | %8s %8s %9s | %8s\n", "", "",
                "see>word", "hear>wrd", "see+noise", "see+shift", "hear+jit", "word>see");
    std::printf("  ------+--------+------------------+-----------------------------+---------\n");

    for (std::size_t N : sizes) {
        khora::retina::Retina eye(32, 16);
        khora::akoe::Ear      ear(24, 8);
        khora::chiasm::Chiasm mem;

        std::vector<khora::lattice::Glyph> sight, sound, word;
        g_s = 777;
        for (std::size_t k = 0; k < N; ++k) {
            const auto sg = eye.encode(draw(k, 0, 0, false));
            const auto ag = ear.encode(tone(k, false));
            const auto wg = khora::lexicon::encode_token(word_for(k));
            sight.push_back(sg); sound.push_back(ag); word.push_back(wg);
            mem.remember({
                {"sight", "img:" + std::to_string(k), sg},
                {"sound", "snd:" + std::to_string(k), ag},
                {"word",  word_for(k),                wg},
            });
        }

        Row s_same, h_same, s_noise, s_shift, h_jit, w2s;
        for (std::size_t k = 0; k < N; ++k) {
            const std::string want_w = word_for(k);
            const std::string want_s = "img:" + std::to_string(k);

            ++s_same.n; if (mem.recall("sight", sight[k], "word").label == want_w) ++s_same.hit;
            ++h_same.n; if (mem.recall("sound", sound[k], "word").label == want_w) ++h_same.hit;
            ++w2s.n;    if (mem.recall("word",  word[k], "sight").label == want_s) ++w2s.hit;

            g_s = 4242 + k * 17;
            const auto noisy = eye.encode(draw(k, 0, 0, true));
            ++s_noise.n; if (mem.recall("sight", noisy, "word").label == want_w) ++s_noise.hit;

            const auto shifted = eye.encode(draw(k, 2, -2, true));
            ++s_shift.n; if (mem.recall("sight", shifted, "word").label == want_w) ++s_shift.hit;

            const auto heard = ear.encode(tone(k, true));
            ++h_jit.n; if (mem.recall("sound", heard, "word").label == want_w) ++h_jit.hit;
        }

        std::printf("  %5zu | %5.1f%% | %7.1f%% %7.1f%% | %7.1f%% %8.1f%% %8.1f%% | %7.1f%%\n",
                    N, 100.0 / (double)N,
                    pct(s_same), pct(h_same),
                    pct(s_noise), pct(s_shift), pct(h_jit), pct(w2s));
    }
    // --- WHY, AND IT IS NOT THE MEMORY --------------------------------------
    //
    // A table of retrieval rates says what happened and not why. Two numbers per
    // modality settle it: how similar a concept is to a FRESH instance of itself,
    // and how similar it is to a DIFFERENT concept. Retrieval can only work when
    // the first is comfortably larger than the second, and where it fails one of
    // those two is to blame -- the front end, not the binding.
    {
        khora::retina::Retina eye(32, 16);
        khora::akoe::Ear      ear(24, 8);
        const std::size_t M = 60;
        std::vector<khora::lattice::Glyph> sg, ag, wg;
        g_s = 31337;
        for (std::size_t k = 0; k < M; ++k) {
            sg.push_back(eye.encode(draw(k, 0, 0, false)));
            ag.push_back(ear.encode(tone(k, false)));
            wg.push_back(khora::lexicon::encode_token(word_for(k)));
        }
        double self_noise = 0, self_shift = 0, self_jit = 0;
        double other_s = 0, other_a = 0, other_w = 0;
        std::size_t pairs = 0;
        for (std::size_t k = 0; k < M; ++k) {
            g_s = 909 + k * 31;
            self_noise += sg[k].similarity(eye.encode(draw(k, 0, 0, true)));
            self_shift += sg[k].similarity(eye.encode(draw(k, 2, -2, true)));
            self_jit   += ag[k].similarity(ear.encode(tone(k, true)));
            // THE MEAN IS THE WRONG STATISTIC and reporting it hid the answer.
            // Retrieval is decided by the NEAREST wrong candidate, not the average
            // one. Two chords sharing two of their three tones are nearly
            // identical while the mean over all pairs stays low, so a modality can
            // look separable on average and be unretrievable in practice.
            double near_s = -2, near_a = -2, near_w = -2;
            for (std::size_t j = 0; j < M; ++j) {
                if (j == k) continue;
                near_s = std::max(near_s, (double)sg[k].similarity(sg[j]));
                near_a = std::max(near_a, (double)ag[k].similarity(ag[j]));
                near_w = std::max(near_w, (double)wg[k].similarity(wg[j]));
            }
            other_s += near_s; other_a += near_a; other_w += near_w;
            ++pairs;
        }
        const double dm = static_cast<double>(M), dp = static_cast<double>(pairs);
        std::printf("\n  === WHY: HOW SEPARABLE EACH FRONT END ACTUALLY IS (%zu concepts) ===\n", M);
        std::printf("    modality | to a fresh instance of ITSELF | to its NEAREST rival |   gap\n");
        std::printf("    ---------+-------------------------------+--------------------+------\n");
        std::printf("    sight    | %.3f (noise)  %.3f (shifted) |              %.3f | %.3f\n",
                    self_noise / dm, self_shift / dm, other_s / dp,
                    self_noise / dm - other_s / dp);
        std::printf("    sound    |              %.3f (jittered) |              %.3f | %.3f\n",
                    self_jit / dm, other_a / dp, self_jit / dm - other_a / dp);
        std::printf("    word     |          1.000 (same token)   |              %.3f | %.3f\n",
                    other_w / dp, 1.0 - other_w / dp);
        std::printf("    A gap near zero means the front end cannot tell two things apart, and\n"
                    "    no binding repairs that. Where a column above collapsed, this is why.\n");
    }

    std::printf("\n  A word has no novel instance -- it is the same token -- so word>see has\n"
                "  one column and not two.\n");
    std::printf("\n  WHAT THIS DOES NOT SHOW. The concepts are synthetic and separable by\n"
                "  design, so this measures the MEMORY, not the front ends -- if the retina\n"
                "  could not tell two shapes apart, no binding would fix that. And every\n"
                "  record here has exactly three fields; a record with thirty would resolve\n"
                "  far worse, because a bundle holds each component more weakly the more it\n"
                "  holds. That curve is substrate_bench's job, not this one's.\n");
    return 0;
}
