// TemporalMemory test — high-order sequences, and the ignorance signal.
//
// THE TEST THIS MODULE EXISTS TO PASS. Train interleaved A B C D and X B C Y.
// After X B C, predict Y and not D. After A B C, predict D and not Y. The
// shared subsequence B C is bit-identical in both, so the only thing that can
// separate the futures is a representation of B and C that differs by CONTEXT.
//
// This is not a quantitative improvement over PredictiveColumn. It is a
// capability PredictiveColumn structurally lacks: it encodes context as a
// position-permuted bundle of the last K inputs, so once the window slides past
// A or X the two contexts are the same glyph and the two futures collide.

#include "khora/cortex/temporal_memory.hpp"
#include "khora/lattice/sdr.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace khora::cortex;
using khora::lattice::Sdr;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("  FAIL: %s\n", what); ++failures; }
    else         std::printf("  ok  : %s\n", what);
}

// One Sdr per symbol, stable across the whole test.
Sdr sym(char c) { return Sdr::from_hash(std::string("symbol:") + c); }

// Feed a sequence, return the stats of the LAST step.
TemporalMemoryStats feed(TemporalMemory& tm, const std::string& seq, bool learn) {
    TemporalMemoryStats st{};
    tm.reset();
    for (const char c : seq) st = tm.compute(sym(c), learn);
    return st;
}

// After presenting `prefix`, is `next` among the predicted columns?
// A column is predicted iff its (block, index) position is in the union.
bool predicts(TemporalMemory& tm, const std::string& prefix, char next) {
    feed(tm, prefix, false);
    const Sdr target = sym(next);
    std::size_t hit = 0;
    for (std::size_t b = 0; b < khora::lattice::kSdrBlocks; ++b) {
        if (tm.predicted_columns().contains(b, target.index(b))) ++hit;
    }
    // A prediction is a union over many columns; require most of the target's
    // columns to be primed, not a couple by chance (chance is ~1 in 64).
    return hit > khora::lattice::kSdrBlocks * 3 / 4;
}

double predicted_fraction(TemporalMemory& tm, const std::string& prefix, char next) {
    feed(tm, prefix, false);
    const Sdr target = sym(next);
    std::size_t hit = 0;
    for (std::size_t b = 0; b < khora::lattice::kSdrBlocks; ++b) {
        if (tm.predicted_columns().contains(b, target.index(b))) ++hit;
    }
    return static_cast<double>(hit) / static_cast<double>(khora::lattice::kSdrBlocks);
}

} // namespace

int main() {
    std::printf("TemporalMemory test  (%zu columns x %zu cells, %zu active)\n",
                kColumns, kCellsPerCol, kActiveCols);

    // --- THE HIGH-ORDER SEQUENCE TEST ---------------------------------------
    {
        TemporalMemory tm;
        // Separation is not immediate, and the shape of getting there is the
        // mechanism working. On the first exposure the shared symbols BURST, so
        // both contexts genuinely look identical and the same cells are chosen.
        // Only once the predecessors stop bursting do the contexts diverge, a
        // fresh cell get allocated, and the stale cross-context segment decay
        // below threshold under repeated punishment. Measured trajectory:
        //
        //   epoch  ABC->D  ABC->Y | XBC->Y  XBC->D   segments
        //       5   1.000   1.000 |  1.000   1.000     1533
        //      12   1.000   1.000 |  1.000   0.012     1789
        //      25   1.000   1.000 |  1.000   0.012     1789
        //      50   1.000   0.012 |  1.000   0.012     1789   <- separated
        //     200   1.000   0.012 |  1.000   0.012     1789   <- and stable
        //
        // 0.012 is 3 of 256 blocks, which is chance for a 1-in-64 code.
        const int epochs = 60;
        for (int e = 0; e < epochs; ++e) {
            feed(tm, "ABCD", true);
            feed(tm, "XBCY", true);
        }
        std::printf("  after %d epochs: %zu segments, %zu synapses\n",
                    epochs, tm.last().segments, tm.last().synapses);

        const double abc_d = predicted_fraction(tm, "ABC", 'D');
        const double abc_y = predicted_fraction(tm, "ABC", 'Y');
        const double xbc_y = predicted_fraction(tm, "XBC", 'Y');
        const double xbc_d = predicted_fraction(tm, "XBC", 'D');
        std::printf("    after ABC -> D %.2f  Y %.2f\n", abc_d, abc_y);
        std::printf("    after XBC -> Y %.2f  D %.2f\n", xbc_y, xbc_d);

        check(predicts(tm, "ABC", 'D'), "after A B C it predicts D");
        check(predicts(tm, "XBC", 'Y'), "after X B C it predicts Y");
        check(!predicts(tm, "ABC", 'Y'), "after A B C it does NOT predict Y");
        check(!predicts(tm, "XBC", 'D'), "after X B C it does NOT predict D");
        check(abc_d > abc_y + 0.5 && xbc_y > xbc_d + 0.5,
              "the two futures are cleanly separated by context alone");
    }

    // --- the ignorance signal ------------------------------------------------
    //
    // Bursting is a column saying "this input, in no context I recognise". The
    // fraction of active columns bursting is therefore an anomaly score that
    // costs nothing extra: it must be high on first exposure and fall as the
    // sequence becomes familiar.
    {
        TemporalMemory tm;
        std::printf("\n  ANOMALY over repetitions of a novel sequence:\n   ");
        std::vector<double> curve;
        for (int rep = 0; rep < 8; ++rep) {
            tm.reset();
            double sum = 0.0;
            int n = 0;
            for (const char c : std::string("PQRST")) {
                const auto st = tm.compute(sym(c), true);
                if (n > 0) { sum += st.anomaly; }   // the first step cannot predict
                ++n;
            }
            curve.push_back(sum / (n - 1));
            std::printf(" %.2f", curve.back());
        }
        std::printf("\n");
        check(curve.front() > 0.9, "a wholly novel sequence bursts almost every column");
        check(curve.back() < 0.1, "a learned sequence barely bursts at all");
        bool monotone_enough = true;
        for (std::size_t i = 1; i < curve.size(); ++i) {
            if (curve[i] > curve[i - 1] + 0.02) monotone_enough = false;
        }
        check(monotone_enough, "the anomaly score falls monotonically with familiarity");

        // And it must SPIKE again when the sequence is violated.
        tm.reset();
        tm.compute(sym('P'), false);
        tm.compute(sym('Q'), false);
        const auto expected = tm.compute(sym('R'), false);
        tm.reset();
        tm.compute(sym('P'), false);
        tm.compute(sym('Q'), false);
        const auto violated = tm.compute(sym('Z'), false);
        std::printf("    anomaly on the expected symbol %.2f, on a violation %.2f\n",
                    expected.anomaly, violated.anomaly);
        check(violated.anomaly > expected.anomaly + 0.5,
              "the anomaly score spikes when the sequence is violated");
    }

    // --- what survives losing 40% of the cells --------------------------------
    {
        TemporalMemory tm;
        for (int e = 0; e < 60; ++e) { feed(tm, "ABCD", true); feed(tm, "XBCY", true); }
        const double base_d = predicted_fraction(tm, "ABC", 'D');
        check(base_d > 0.9, "baseline before lesion");

        // Cell loss costs a prediction twice over, and the second cost is the
        // one that decides the shape of the curve.
        //
        // Killing a cell destroys the segments it OWNS, so its column can no
        // longer predict -- that is a linear loss. But it also removes the cell
        // from every segment that LISTENS to it, and a segment fires only on a
        // threshold. With new_synapse_count = 20 against activation_threshold =
        // 13, a 40% loss leaves 12 connected synapses: one short. The threshold
        // is a cliff, and where the cliff sits is set entirely by the ratio of
        // synapses to threshold, not by anything biological.
        //
        // Ahmad & Hawkins' 40%-tolerance figure assumes 24 synapses at
        // threshold 12, which leaves 14.4 and clears. This configuration is
        // tuned for selectivity instead, and pays for it here. Measured rather
        // than asserted, so the trade is visible.
        std::printf("\n  DEGRADATION UNDER CELL LOSS (20 synapses, threshold 13)\n");
        std::printf("    loss | ABC->D  ABC->Y | XBC->Y  XBC->D\n");
        double at20_d = 0.0, at20_y = 0.0;
        for (const double loss : {0.0, 0.10, 0.20, 0.30, 0.40, 0.50}) {
            TemporalMemory t2;
            for (int e = 0; e < 60; ++e) { feed(t2, "ABCD", true); feed(t2, "XBCY", true); }
            if (loss > 0.0) t2.lesion(loss, 0xDEADBEEF);
            const double d = predicted_fraction(t2, "ABC", 'D');
            const double y = predicted_fraction(t2, "ABC", 'Y');
            const double yy = predicted_fraction(t2, "XBC", 'Y');
            const double dd = predicted_fraction(t2, "XBC", 'D');
            std::printf("    %3.0f%% |  %.2f    %.2f  |  %.2f    %.2f\n",
                        100 * loss, d, y, yy, dd);
            if (loss == 0.20) { at20_d = d; at20_y = y; }
        }

        // Within the margin the threshold allows, prediction must survive and
        // stay discriminating. Beyond it the cliff is expected, not a defect.
        check(at20_d > 0.5, "prediction survives 20% cell loss");
        check(at20_d > at20_y + 0.4, "and the contexts stay separated there");

        // Whatever the magnitude, loss must never make a segment fire for the
        // WRONG context -- that is the failure subsampling exists to prevent.
        tm.lesion(0.40, 0xDEADBEEF);
        const double abc_d = predicted_fraction(tm, "ABC", 'D');
        const double abc_y = predicted_fraction(tm, "ABC", 'Y');
        const double xbc_y = predicted_fraction(tm, "XBC", 'Y');
        const double xbc_d = predicted_fraction(tm, "XBC", 'D');
        std::printf("    at 40%%: ABC -> D %.2f Y %.2f | XBC -> Y %.2f D %.2f\n",
                    abc_d, abc_y, xbc_y, xbc_d);
        check(abc_d > abc_y && xbc_y > xbc_d,
              "even past the threshold cliff, the right context still leads");
    }

    // --- determinism ---------------------------------------------------------
    {
        TemporalMemory a, b;
        for (int e = 0; e < 10; ++e) { feed(a, "ABCD", true); feed(b, "ABCD", true); }
        check(a.segment_count() == b.segment_count(),
              "two identical runs learn identical structure");
        check(predicted_fraction(a, "ABC", 'D') == predicted_fraction(b, "ABC", 'D'),
              "two identical runs predict identically");
    }

    std::printf("\n");
    if (failures == 0) std::printf("ALL PASS\n");
    else               std::printf("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
