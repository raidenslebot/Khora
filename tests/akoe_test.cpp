// Hearing, and the two things that have to be true before it is hearing at all.
//
// The front end is a filter bank and the encoder puts its output into the same
// 10,000-bit space as everything else. Two properties decide whether that is
// perception or decoration:
//
//   THE SPECTROGRAM MUST FIND THE TONE. A 440 Hz sine has to light the band at
//   440 Hz and not the others. If it does not, nothing above it can work and
//   every downstream number is measuring the wrong thing.
//
//   TIME MUST SURVIVE. Bundling is commutative, so without the per-frame
//   permutation a rising sweep and a falling one encode IDENTICALLY. That is the
//   single easiest thing to get wrong here and the hardest to notice, because
//   everything still runs and classification of steady tones still works.
//
// The separability limit is measured rather than asserted: chiasm_bench found a
// fresh recording of a chord scores 0.521 against its own stored glyph while its
// nearest rival chord scores 0.737 -- a NEGATIVE margin, meaning this ear cannot
// separate chords that share tones. That is a real limit of the filter bank and
// the last check here pins the case that does work so the difference is on
// record.

#include "khora/akoe/akoe.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace khora::akoe;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("  FAIL: %s\n", what); ++failures; }
    else       { std::printf("  ok  : %s\n", what); }
}

Sound sine(double hz, std::size_t n = 4096, double rate = 16000.0) {
    Sound s; s.rate = rate; s.samples.resize(n);
    for (std::size_t i = 0; i < n; ++i)
        s.samples[i] = std::sin(2.0 * 3.14159265358979 * hz * (double)i / rate);
    return s;
}

Sound sweep(double from, double to, std::size_t n = 4096, double rate = 16000.0) {
    Sound s; s.rate = rate; s.samples.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double f = from + (to - from) * (double)i / (double)n;
        s.samples[i] = std::sin(2.0 * 3.14159265358979 * f * (double)i / rate);
    }
    return s;
}

} // namespace

int main() {
    std::printf("Akoe — hearing, in the same 10,000 bits as everything else\n\n");

    // --- THE FILTER BANK FINDS THE TONE --------------------------------------
    {
        Ear ear(24, 8, 80.0, 6000.0);
        const auto sp = ear.analyse(sine(440.0));
        check(sp.frames > 0 && sp.bands == 24, "a spectrogram comes out with the right shape");

        std::size_t peak = 0;
        for (std::size_t b = 1; b < sp.bands; ++b)
            if (sp.at(0, b) > sp.at(0, peak)) peak = b;
        // Bands are log-spaced from 80 to 6000 over 24 channels, so 440 Hz lands
        // around band 10. Asserting the exact index would break if the bank is
        // ever retuned; asserting it is in the lower half and not at an edge is
        // the property that actually matters.
        const double centre = 80.0 * std::pow(6000.0 / 80.0, (double)peak / 23.0);
        std::printf("      440 Hz peaks in band %zu, centred %.0f Hz\n", peak, centre);
        check(centre > 300.0 && centre < 650.0, "and the loudest band is the one at 440 Hz");

        const auto sp2 = ear.analyse(sine(2000.0));
        std::size_t peak2 = 0;
        for (std::size_t b = 1; b < sp2.bands; ++b)
            if (sp2.at(0, b) > sp2.at(0, peak2)) peak2 = b;
        check(peak2 > peak, "a higher tone peaks in a higher band");
    }

    // --- TIME SURVIVES, WHICH IT WOULD NOT WITHOUT THE PERMUTATION -----------
    {
        Ear ear(24, 8);
        const auto up   = ear.encode(sweep(200.0, 3000.0));
        const auto down = ear.encode(sweep(3000.0, 200.0));
        const double s = up.similarity(down);
        std::printf("      rising sweep vs falling sweep: similarity %.3f\n", s);
        check(s < 0.6,
              "a rising and a falling sweep encode DIFFERENTLY -- bundling is "
              "commutative, so this is the permutation doing its job");

        const auto up2 = ear.encode(sweep(200.0, 3000.0));
        check(up.similarity(up2) > 0.99, "and the same sound twice encodes the same");
    }

    // --- SILENCE IS ABSENT, NOT A SYMBOL -------------------------------------
    //
    // Binding a glyph for an empty band means every sound shares a term with
    // every other wherever both are quiet, which is most bands most of the time.
    // Measured over 60 chords, dropping it took the similarity between two
    // UNRELATED sounds from 0.484 to 0.328.
    {
        Ear ear(24, 8);
        const auto a = ear.encode(sine(300.0));
        const auto b = ear.encode(sine(3000.0));
        std::printf("      two unrelated pure tones: similarity %.3f\n", a.similarity(b));
        check(a.similarity(b) < 0.5,
              "two unrelated tones are not mostly the same glyph");
    }

    // --- IT CAN ACTUALLY TELL SOUNDS APART, WHERE THEY ARE FAR ENOUGH APART --
    {
        Ear ear(24, 8);
        Listener ls;
        for (int k = 0; k < 4; ++k)
            ls.learn(ear.encode(sine(250.0 * std::pow(2.0, k))), k);
        std::size_t hit = 0;
        for (int k = 0; k < 4; ++k) {
            // A fresh rendering at a slightly different length, so it is not the
            // identical sample buffer going back in.
            const auto q = ear.encode(sine(250.0 * std::pow(2.0, k), 3800));
            if (ls.classify(q).first == k) ++hit;
        }
        std::printf("      four octave-separated tones, re-rendered: %zu/4 correct\n", hit);
        check(hit == 4, "octave-separated tones are recognised from a fresh recording");
        check(ls.classes() == 4, "and each label kept its own prototype");
    }

    std::printf("\n");
    if (failures == 0) std::printf("ALL PASS\n");
    else               std::printf("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
