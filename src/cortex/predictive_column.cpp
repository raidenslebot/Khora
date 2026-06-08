#include "khora/cortex/predictive_column.hpp"

#include <span>
#include <string>
#include <vector>

namespace khora::cortex {

using khora::lattice::Glyph;
using khora::lattice::bundle;
using khora::lattice::permute;

PredictiveColumn::PredictiveColumn(std::size_t context_window)
    : context_window_(context_window == 0 ? 1 : context_window) {}

Glyph PredictiveColumn::current_context_() const {
    if (recent_.empty()) return Glyph::zero();

    // Build a position-aware context glyph by permuting each member of
    // the sliding window by a position-specific amount and bundling.
    // Position 0 = most recent input (no permutation); older inputs get
    // larger permutations to mark their relative age.
    std::vector<Glyph> ordered;
    ordered.reserve(recent_.size());
    int pos = 0;
    for (auto it = recent_.rbegin(); it != recent_.rend(); ++it, ++pos) {
        ordered.push_back(permute(*it, pos * 137));  // 137: arbitrary distinct stride
    }
    return bundle(std::span<const Glyph>{ordered.data(), ordered.size()});
}

Glyph PredictiveColumn::predict() const {
    if (ctx_keys_.size() == 0) return Glyph::zero();
    const Glyph ctx = current_context_();
    const auto matches = ctx_keys_.query(ctx, 1);
    if (matches.empty()) return Glyph::zero();
    const auto val = ctx_vals_.recall(matches[0].label);
    return val.value_or(Glyph::zero());
}

PredictiveColumn::StepResult PredictiveColumn::step(const Glyph& input) {
    StepResult r;
    r.actual = input;

    // 1. Predict BEFORE this input is incorporated.
    r.predicted        = predict();
    r.prediction_error = r.predicted.hamming(input);
    r.similarity       = r.predicted.similarity(input);

    if (ctx_keys_.size() > 0) {
        const Glyph ctx = current_context_();
        const auto matches = ctx_keys_.query(ctx, 1);
        r.novel_context = matches.empty() || (matches[0].similarity < 0.3);
    } else {
        r.novel_context = true;
    }

    // 2. Learn — associate current context with this input as the "next" glyph.
    //    Requires at least one prior observation in the window.
    if (!recent_.empty()) {
        const Glyph ctx = current_context_();
        const std::string label = "ctx_" + std::to_string(next_assoc_id_++);
        ctx_keys_.store(label, ctx);
        ctx_vals_.store(label, input);
    }

    // 3. Advance the sliding window.
    recent_.push_back(input);
    if (recent_.size() > context_window_) recent_.pop_front();
    ++observations_;

    // 4. Track recent accuracy.
    recent_sims_.push_back(r.similarity);
    if (recent_sims_.size() > kRecentWindow) recent_sims_.pop_front();

    return r;
}

double PredictiveColumn::recent_accuracy() const {
    if (recent_sims_.empty()) return 0.0;
    double sum = 0.0;
    for (double s : recent_sims_) sum += s;
    return sum / static_cast<double>(recent_sims_.size());
}

} // namespace khora::cortex
