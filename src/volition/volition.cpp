#include "khora/volition/volition.hpp"

#include <limits>
#include <vector>
#include <utility>

namespace khora::volition {

using khora::soma::Drive;
using khora::soma::kDriveCount;

Volition::Volition(khora::soma::SomaNexus& soma) : soma_(soma) {}

void Volition::add(Act act) { acts_.push_back(std::move(act)); }

Choice Volition::decide() const {
    const auto snap = soma_.snapshot();   // current drive strengths = pressures
    Choice best;
    double best_score = -std::numeric_limits<double>::infinity();

    for (std::size_t i = 0; i < acts_.size(); ++i) {
        const Act& a = acts_[i];
        if (a.available && !a.available()) continue;

        double score = 0.0;
        double dom_contrib = -std::numeric_limits<double>::infinity();
        std::size_t dom = 0;
        for (std::size_t d = 0; d < kDriveCount; ++d) {
            const double c = snap[d] * a.affinity.per_drive[d];
            score += c;
            if (c > dom_contrib) { dom_contrib = c; dom = d; }
        }
        if (score > best_score) {
            best_score    = score;
            best.index    = static_cast<int>(i);
            best.name     = a.name;
            best.score    = score;
            best.dominant = khora::soma::drive_name(static_cast<Drive>(dom));
        }
    }
    return best;
}

std::string Volition::act() {
    // WHICH DRIVE IS PRESSING is the context; WHICH ACT SERVES IT is the choice.
    // Splitting the decision that way keeps the homeostatic rotation the drive
    // system exists for and puts the part that was a hand-written constant --
    // which act serves which drive, and how well -- where it can be learned.
    const auto snap = soma_.snapshot();
    std::size_t ctx = 0;
    for (std::size_t d = 1; d < kDriveCount; ++d) if (snap[d] > snap[ctx]) ctx = d;
    last_ctx_ = ctx;

    int chosen = -1;
    if (selecting()) {
        std::vector<std::size_t> allowed;
        for (std::size_t i = 0; i < acts_.size(); ++i)
            if (!acts_[i].available || acts_[i].available()) allowed.push_back(i);
        if (!allowed.empty())
            chosen = static_cast<int>(learner_->choose(ctx, 1.0, allowed));
    } else {
        chosen = decide().index;
    }
    if (chosen < 0) return "[volition] nothing available to do";

    const Act& a = acts_[static_cast<std::size_t>(chosen)];
    const Outcome out = a.perform ? a.perform() : Outcome{"(noop)", 0.0};
    std::string note = out.note;
    yield_sum_ += out.yield;
    if (learner_) learner_->observe(ctx, static_cast<std::size_t>(chosen), out.yield);

    history_.push_back(Step{ctx, static_cast<std::size_t>(chosen),
                            prev_act_ == static_cast<std::size_t>(-1) ? acts_.size() : prev_act_,
                            out.yield});
    prev_act_ = static_cast<std::size_t>(chosen);

    // Acting on an urge relieves it — homeostasis then rotates attention to
    // the next-most-pressing drive, so behaviour doesn't fixate.
    for (std::size_t d = 0; d < kDriveCount; ++d) {
        const double aff = a.affinity.per_drive[d];
        if (aff > 0.0) soma_.stimulate(static_cast<Drive>(d), -relief_ * aff);
    }
    ++performed_;
    return note;
}

} // namespace khora::volition
