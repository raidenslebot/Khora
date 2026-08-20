// Sdr test — the sparse block-code substrate.
//
// The decisive measurement is SUBSAMPLE FALSE-MATCH RATE. Storing a small
// sample of a pattern and recognising it from that sample -- what a dendritic
// segment physically does -- has a false-match probability against unrelated
// input of P(Binomial(s, p_agree) >= theta). On a 50%-dense code p_agree is 0.5,
// so at s=24/theta=12 that is 0.58, and it is 0.58 at EVERY dimension: widening
// the vector cannot help, because a coin-flip bit meets a half-threshold by
// chance. On this block code p_agree is 1/64.
//
// That single number is why the sparse substrate exists, so this test measures
// it rather than citing it -- both codes, same segment size, same threshold.

#include "khora/lattice/sdr.hpp"
#include "khora/lattice/glyph.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace khora::lattice;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("  FAIL: %s\n", what); ++failures; }
    else         std::printf("  ok  : %s\n", what);
}

std::uint64_t rng_state = 0x1234567890ABCDEFULL;
std::uint64_t nextr() {
    std::uint64_t z = (rng_state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// P(Binomial(n, p) >= k), for reporting the model alongside the measurement.
double binom_tail(int n, double p, int k) {
    double total = 0.0;
    for (int i = k; i <= n; ++i) {
        double lg = std::lgamma(n + 1.0) - std::lgamma(i + 1.0) - std::lgamma(n - i + 1.0);
        total += std::exp(lg + i * std::log(p) + (n - i) * std::log1p(-p));
    }
    return total;
}

// The dense equivalent of a Segment: sample positions that are ACTIVE in the
// stored glyph, then count how many are active in the probe. Same subsample
// size, same threshold -- the fair comparison.
struct DenseSegment {
    std::vector<std::size_t> pos;

    static DenseSegment learn(const Glyph& g, std::size_t n) {
        DenseSegment d;
        std::size_t guard = 0;
        while (d.pos.size() < n && guard < kGlyphBits * 8) {
            ++guard;
            const std::size_t i = static_cast<std::size_t>(nextr() % kGlyphBits);
            if (g.bit(i)) d.pos.push_back(i);
        }
        return d;
    }
    std::size_t agreement(const Glyph& g) const {
        std::size_t n = 0;
        for (const std::size_t i : pos) n += g.bit(i) ? 1u : 0u;
        return n;
    }
};

} // namespace

int main() {
    std::printf("Sdr test  (B=%zu blocks of L=%zu, %zu bits, %zu active = %.4f%%)\n",
                kSdrBlocks, kSdrBlockSize, kSdrBits, kSdrActive,
                100.0 * kSdrActive / kSdrBits);

    // --- structure ----------------------------------------------------------
    {
        const Sdr a = Sdr::random(1);
        check(a.overlap(a) == kSdrBlocks, "an Sdr fully overlaps itself");
        check(Sdr::from_hash("khora") == Sdr::from_hash("khora"), "from_hash is deterministic");
        check(Sdr::from_hash("khora") != Sdr::from_hash("morphus"), "from_hash separates inputs");
    }

    // Unrelated Sdrs agree on B/L blocks by chance: 256/64 = 4.0, sd 1.98.
    {
        double sum = 0.0, sumsq = 0.0;
        const int N = 20000;
        for (int i = 0; i < N; ++i) {
            const double o = static_cast<double>(
                Sdr::random(nextr()).overlap(Sdr::random(nextr())));
            sum += o; sumsq += o * o;
        }
        const double mean = sum / N;
        const double sd   = std::sqrt(sumsq / N - mean * mean);
        std::printf("  chance overlap: mean %.3f (expect 4.000), sd %.3f (expect 1.984)\n",
                    mean, sd);
        check(std::fabs(mean - 4.0) < 0.15, "chance overlap matches B/L");
        check(std::fabs(sd - 1.984) < 0.15, "chance overlap spread matches the model");
    }

    // --- algebra: exact, and sparsity cannot drift --------------------------
    //
    // The dense code cannot make this claim. XOR drives a sparse Glyph's density
    // toward 0.5 (p -> 2p(1-p) has an attracting fixed point there), so a chain
    // of binds destroys sparsity. Here sparsity is structural: one active index
    // per block, before and after any operation.
    {
        std::size_t exact = 0;
        const int N = 200000;
        for (int i = 0; i < N; ++i) {
            const Sdr a = Sdr::random(nextr());
            const Sdr b = Sdr::random(nextr());
            if (unbind(bind(a, b), b) == a) ++exact;
        }
        std::printf("  unbind(bind(a,b),b) == a : %d/%d\n", static_cast<int>(exact), N);
        check(exact == static_cast<std::size_t>(N), "binding is EXACTLY invertible");

        Sdr chain = Sdr::random(7);
        for (int i = 0; i < 32; ++i) chain = bind(chain, Sdr::random(nextr()));
        std::size_t distinct_blocks = 0;
        for (std::size_t b = 0; b < kSdrBlocks; ++b)
            distinct_blocks += (chain.index(b) < kSdrBlockSize) ? 1u : 0u;
        check(distinct_blocks == kSdrBlocks, "sparsity survives a chain of 32 binds");

        // Binding is not self-inverse (unbind is subtraction, not addition) but
        // it IS commutative, exactly like XOR on the dense code. So it cannot
        // express direction on its own either, and a chain of bound transitions
        // needs the same permutation fix that whetstone's transition encoding
        // needed. Pinned here so nobody assumes otherwise from "it has a
        // separate unbind".
        const Sdr x = Sdr::random(11), y = Sdr::random(22);
        check(bind(x, y) == bind(y, x), "binding is commutative: direction needs permute");
        check(unbind(bind(x, y), y) == x && unbind(bind(x, y), x) == y,
              "either operand recovers the other -- the ambiguity direction must resolve");
        check(bind(permute(x, 1), y) != bind(permute(y, 1), x),
              "permuting one operand makes the binding directed");
        check(permute(permute(x, 5), -5) == x, "permute is invertible");
    }

    // --- THE DECISIVE MEASUREMENT -------------------------------------------
    {
        std::printf("\n  SUBSAMPLE FALSE-MATCH RATE (s=%zu synapses)\n", Segment::kSynapses);
        std::printf("    theta |     sparse (1/64)      |      dense (1/2)\n");
        std::printf("    ------+------------------------+-----------------------\n");

        const int N = 400000;
        const int s = static_cast<int>(Segment::kSynapses);

        // Build once, probe with many unrelated patterns.
        const Sdr   stored_s = Sdr::random(0xABCDE);
        const Segment seg    = Segment::learn(stored_s, 0x13579);
        const Glyph stored_g = Glyph::random(0xABCDE);
        const DenseSegment dseg = DenseSegment::learn(stored_g, Segment::kSynapses);

        std::vector<int> sparse_hits(25, 0), dense_hits(25, 0);
        for (int i = 0; i < N; ++i) {
            const std::size_t a1 = seg.agreement(Sdr::random(nextr()));
            const std::size_t a2 = dseg.agreement(Glyph::random(nextr()));
            for (std::size_t t = 0; t <= 24; ++t) {
                if (a1 >= t) ++sparse_hits[t];
                if (a2 >= t) ++dense_hits[t];
            }
        }
        for (const int t : {2, 3, 4, 6, 8, 12}) {
            const double sp = static_cast<double>(sparse_hits[t]) / N;
            const double dn = static_cast<double>(dense_hits[t]) / N;
            std::printf("     %2d   | %10.3e  (model %8.1e) | %8.4f  (model %6.4f)\n",
                        t, sp, binom_tail(s, 1.0 / 64.0, t), dn, binom_tail(s, 0.5, t));
        }

        const double dense_fp  = static_cast<double>(dense_hits[12]) / N;
        const double sparse_fp = static_cast<double>(sparse_hits[12]) / N;
        std::printf("    at theta=12: dense %.4f  vs  sparse %.3e (0 hits means < %.1e)\n",
                    dense_fp, sparse_fp, 1.0 / N);

        // The dense code fails outright: over half of all UNRELATED patterns
        // trip a segment trained on something else.
        check(dense_fp > 0.5, "dense code: a subsampled segment fires on random input");
        check(std::fabs(dense_fp - binom_tail(s, 0.5, 12)) < 0.01,
              "dense false-match rate matches Binomial(24, 0.5) >= 12");

        // The sparse code never once fired. The measurement can only bound it
        // below 1/N; the model -- validated against the measurable thresholds
        // printed above -- puts it near 1e-17.
        check(sparse_hits[12] == 0, "sparse code: NOT ONE false match in 400,000");
        check(binom_tail(s, 1.0 / 64.0, 12) < 1e-15,
              "sparse false-match model is below 1e-15 at theta=12");

        // The model is only worth quoting if it predicts the rates that ARE
        // measurable, so check it where the counts are large.
        const double m3 = binom_tail(s, 1.0 / 64.0, 3);
        const double e3 = static_cast<double>(sparse_hits[3]) / N;
        std::printf("    model check at theta=3: measured %.5f vs model %.5f\n", e3, m3);
        check(std::fabs(e3 - m3) < 0.2 * m3 + 1e-4,
              "the binomial model predicts the measurable sparse rates");
    }

    // --- what subsampling buys ----------------------------------------------
    {
        std::printf("\n  WHAT SUBSAMPLING BUYS\n");

        // 1. Robustness. Corrupt 40% of the pattern's blocks; the segment sees
        //    only 24 of 256 and should still recognise what it learned.
        const Sdr pattern = Sdr::random(0x5150);
        const Segment seg = Segment::learn(pattern, 0x2468);
        int survived = 0;
        const int trials = 2000;
        for (int t = 0; t < trials; ++t) {
            Sdr damaged = pattern;
            for (std::size_t b = 0; b < kSdrBlocks; ++b) {
                if ((nextr() % 100) < 40) {
                    damaged.set_index(b, static_cast<std::uint8_t>(nextr() & 63));
                }
            }
            if (seg.matches(damaged)) ++survived;
        }
        std::printf("    40%% of blocks randomised: still matched %d/%d\n", survived, trials);
        check(survived > trials * 90 / 100, "a segment survives 40% pattern loss");

        // 2. UNION TOLERANCE -- the property the sparse substrate exists for,
        //    and the one a dense code cannot give at all.
        //
        //    A segment must still find its pattern inside a set of many
        //    simultaneously active ones, and must NOT fire on a set it is
        //    absent from. Note this uses SdrUnion, not a bundle: a bundle
        //    answers "which single pattern best explains these votes" and keeps
        //    only ~1/M of each member, so a segment finds nothing in it
        //    (measured: 1 hit in 400 at M=4). Simultaneity is a set, not a vote.
        //    A member is ALWAYS found, at any M -- that is guaranteed by
        //    construction, since a union keeps every member's bits. What limits
        //    union capacity is the other side: density grows with M, so an
        //    ABSENT segment starts firing by chance. The capacity is therefore
        //    set by the same binomial the false-match rate is, now evaluated at
        //    the union's density instead of the code's. The test asserts that
        //    relationship rather than a guessed threshold.
        std::printf("    M   | member | absent fires | density | model P(FP)\n");
        std::size_t capacity_at_1pct = 0;
        for (const std::size_t M : {4u, 8u, 16u, 32u, 64u, 128u}) {
            int in_union = 0, out_union = 0;
            double dens = 0.0;
            const int reps = 400;
            for (int t = 0; t < reps; ++t) {
                SdrUnion with;
                with.add(pattern);
                for (std::size_t i = 1; i < M; ++i) with.add(Sdr::random(nextr()));
                if (seg.matches(with)) ++in_union;
                dens += with.density();

                SdrUnion without;
                for (std::size_t i = 0; i < M; ++i) without.add(Sdr::random(nextr()));
                if (seg.matches(without)) ++out_union;
            }
            dens /= reps;
            const double measured = static_cast<double>(out_union) / reps;
            const double model    = binom_tail(static_cast<int>(Segment::kSynapses), dens,
                                               Segment::kDefaultTheta);
            std::printf("   %4zu | %3d/%d|   %3d/%d     |  %.3f  |  %.4f\n",
                        M, in_union, reps, out_union, reps, dens, model);

            char msg[96];
            std::snprintf(msg, sizeof msg, "segment finds its pattern in a union of %zu", M);
            check(in_union == reps, msg);

            if (measured < 0.01) capacity_at_1pct = M;

            // The model must predict the measured false-fire rate wherever that
            // rate is large enough to measure. If it does, union capacity is a
            // computable design parameter rather than a discovered surprise.
            if (model > 0.05) {
                std::snprintf(msg, sizeof msg,
                              "union false-fire at M=%zu matches the density model", M);
                check(std::fabs(measured - model) < 0.10, msg);
            }
        }
        std::printf("    -> union capacity below 1%% false-fire: M = %zu"
                    " (at s=24, theta=12)\n", capacity_at_1pct);
        check(capacity_at_1pct >= 8,
              "at least 8 patterns can be held simultaneously and still discriminated");

        // Union capacity is a knob, not a constant: raising the threshold trades
        // tolerance of a degraded pattern for tolerance of a crowded one.
        {
            const std::size_t M = 32;
            int fired16 = 0, found16 = 0;
            const int reps = 400;
            for (int t = 0; t < reps; ++t) {
                SdrUnion with;  with.add(pattern);
                for (std::size_t i = 1; i < M; ++i) with.add(Sdr::random(nextr()));
                if (seg.matches(with, 18)) ++found16;
                SdrUnion without;
                for (std::size_t i = 0; i < M; ++i) without.add(Sdr::random(nextr()));
                if (seg.matches(without, 18)) ++fired16;
            }
            std::printf("    at M=32 with theta=18: member %d/%d, absent %d/%d"
                        "  (theta=12 gave absent 79/400)\n",
                        found16, reps, fired16, reps);
            check(fired16 * 20 < 79 * 4, "raising theta buys union capacity back");
        }
    }

    // --- superposition and recovery -----------------------------------------
    {
        std::printf("\n  BUNDLE RECOVERY\n");
        for (const std::size_t M : {2u, 4u, 8u, 16u, 32u, 64u}) {
            std::vector<Sdr> items;
            for (std::size_t i = 0; i < M; ++i) items.push_back(Sdr::random(nextr()));
            Trace t;
            for (const auto& s : items) t.add(s);
            const Sdr b = t.binarise();

            double worst = 1.0;
            for (const auto& s : items) worst = std::min(worst, b.similarity(s));
            std::printf("    M=%2zu  worst member similarity %.3f  (chance %.3f)  sharpness %.2f\n",
                        M, worst, 1.0 / static_cast<double>(kSdrBlockSize), t.sharpness());
            if (M <= 16) {
                char msg[96];
                std::snprintf(msg, sizeof msg, "bundle of %zu stays similar to every member", M);
                check(worst > 4.0 / static_cast<double>(kSdrBlockSize), msg);
            }
        }
    }

    // --- the bridge ---------------------------------------------------------
    {
        std::printf("\n  PROJECTION (dense Glyph -> sparse Sdr)\n");
        const Glyph g = Glyph::random(0xF00D);
        check(project(g) == project(g), "projection is deterministic");

        // Similar glyphs must project to similar Sdrs, and unrelated ones to
        // chance. Without that the bridge separates patterns that belong
        // together, and nothing downstream can generalise.
        double sum_near = 0.0, sum_far = 0.0;
        const int N = 300;
        for (int t = 0; t < N; ++t) {
            const Glyph base = Glyph::random(nextr());
            Glyph near = base;
            for (int f = 0; f < 500; ++f) near.flip_bit(nextr() % kGlyphBits);  // 5% noise
            sum_near += static_cast<double>(project(base).overlap(project(near)));
            sum_far  += static_cast<double>(project(base).overlap(project(Glyph::random(nextr()))));
        }
        std::printf("    5%%-corrupted glyph -> overlap %.1f/256 ; unrelated -> %.1f/256\n",
                    sum_near / N, sum_far / N);
        check(sum_near / N > 40.0, "projection preserves similarity");
        check(sum_far / N < 12.0, "projection sends unrelated glyphs to near-chance");
    }

    std::printf("\n");
    if (failures == 0) std::printf("ALL PASS\n");
    else               std::printf("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
