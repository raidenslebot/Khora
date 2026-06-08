#include "khora/whetstone/whetstone.hpp"

#include "khora/cortex/predictive_column.hpp"
#include "khora/crucible/crucible.hpp"
#include "khora/lattice/glyph.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

namespace khora::whetstone {

// ---------------- engine ----------------

void Whetstone::add_faculty(std::unique_ptr<Faculty> f) {
    faculties_.push_back(std::move(f));
    states_.emplace_back();
}

std::size_t Whetstone::select_() {
    // Prefer the faculty furthest below mastery that is not maxed out.
    // Tie-break toward the least-practised. If all are maxed/mastered,
    // rotate to keep escalating difficulty on the strongest.
    std::size_t best = 0;
    double      best_priority = -std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < faculties_.size(); ++i) {
        const auto& s = states_[i];
        // headroom: how far below mastery; maxed faculties get low priority
        double headroom = mastery_ - s.competence;
        double priority = headroom;
        if (s.maxed) priority -= 10.0;                     // deprioritise dead-ends
        priority += 0.001 * static_cast<double>(round_ - s.rounds); // freshness nudge
        if (priority > best_priority) { best_priority = priority; best = i; }
    }
    return best;
}

WhetstoneStep Whetstone::step() {
    ++round_;
    const std::size_t i = select_();
    Faculty&      f = *faculties_[i];
    FacultyState& s = states_[i];

    WhetstoneStep step{};
    step.round      = round_;
    step.faculty    = f.name();
    step.difficulty = s.difficulty;

    const AttemptOutcome out = f.attempt(s.difficulty);
    s.competence = out.score;
    s.best       = std::max(s.best, out.score);
    ++s.rounds;
    step.score = out.score;

    if (out.score >= mastery_) {
        // ESCALATE — what was hard is now mastered; reach further.
        if (s.difficulty < f.max_difficulty()) {
            ++s.difficulty;
            s.best     = 0.0;
            s.plateau  = 0;
            s.maxed    = false;
            step.escalated = true;
            step.note = "mastered d=" + std::to_string(step.difficulty) +
                        " -> escalate to d=" + std::to_string(s.difficulty);
        } else {
            step.note = "mastered at max difficulty";
        }
    } else {
        // EVOLVE — struggle is the trigger to become capable. But the
        // mutation is *measured*: apply it, re-attempt, and keep it only
        // if it helped. Harmful mutations are reverted. Natural selection.
        const double before = out.score;
        const bool changed = f.evolve();
        if (changed) {
            const AttemptOutcome re = f.attempt(s.difficulty);
            if (re.score >= before) {
                s.competence = re.score;
                s.best       = std::max(s.best, re.score);
                step.score   = re.score;
                step.evolved = true;
                ++s.plateau;
                step.note = "shortfall " + std::to_string(before) +
                            " -> evolve ACCEPTED (level " +
                            std::to_string(f.evolution_level()) + ", now " +
                            std::to_string(re.score) + ")";
                // If the accepted evolution itself reached mastery, escalate.
                if (re.score >= mastery_ && s.difficulty < f.max_difficulty()) {
                    ++s.difficulty;
                    s.best   = 0.0;
                    s.plateau = 0;
                    s.maxed   = false;
                    step.escalated = true;
                    step.note += " -> escalate to d=" + std::to_string(s.difficulty);
                }
            } else {
                f.revert();
                step.note = "shortfall " + std::to_string(before) +
                            " -> evolution REJECTED (" + std::to_string(re.score) +
                            " < " + std::to_string(before) + "), reverted";
                ++s.plateau;
                if (s.plateau > 4) s.maxed = true;  // give up escalating this faculty for now
            }
        } else {
            s.maxed = true;
            step.note = "shortfall and evolution exhausted at this difficulty";
        }
    }
    return step;
}

std::vector<WhetstoneStep> Whetstone::run(std::size_t rounds) {
    std::vector<WhetstoneStep> out;
    out.reserve(rounds);
    for (std::size_t r = 0; r < rounds; ++r) out.push_back(step());
    return out;
}

// ---------------- relational faculty ----------------

namespace {

using khora::crucible::RelationalCrucible;
using khora::crucible::Record;

// A generative knowledge base: synthesise N structured records on demand,
// so difficulty (record count) can climb arbitrarily.
Record synth_record(std::size_t idx) {
    const std::string s = "subj" + std::to_string(idx);
    return Record{ s, {
        {"alpha", "a" + std::to_string(idx)},
        {"beta",  "b" + std::to_string(idx)},
        {"gamma", "g" + std::to_string(idx)},
        {"delta", "d" + std::to_string(idx)},
    }};
}

// Relational holographic capacity. difficulty d -> pack (d*4) records.
// A single 10,000-bit glyph saturates around ~150-200 facts; the right
// response to overload is NOT to cram one glyph harder (redundancy makes
// crosstalk worse) but to recruit more memory banks — split the records
// across `chunks_` independent world glyphs, each holding fewer records.
// Brain-like: when one region saturates, allocate more. evolve() doubles
// the banks; revert() halves them.
class RelationalFaculty final : public Faculty {
public:
    explicit RelationalFaculty(std::uint64_t seed) : seed_(seed) {}

    std::string name() const override { return "relational_capacity"; }

    AttemptOutcome attempt(int difficulty) override {
        const std::size_t records = static_cast<std::size_t>(difficulty) * 4;
        const std::size_t C = static_cast<std::size_t>(chunks_);

        // Distribute records across C independent sub-stores (memory banks).
        std::vector<RelationalCrucible> banks;
        banks.reserve(C);
        for (std::size_t c = 0; c < C; ++c) banks.emplace_back(seed_ + c * 0x9E37ULL);
        for (std::size_t i = 0; i < records; ++i) {
            banks[i % C].add_record(synth_record(i));
        }
        std::size_t correct = 0, total = 0;
        for (auto& b : banks) {
            b.build();
            // Each bank packs ALL its records into one world glyph and is
            // queried for every field — the honest per-bank capacity test.
            const auto r = b.trial_holographic(b.record_count(), 0.0);
            correct += r.correct;
            total   += r.trials;
        }
        AttemptOutcome o;
        o.score  = total ? static_cast<double>(correct) / static_cast<double>(total) : 1.0;
        o.detail = std::to_string(records) + " records across " +
                   std::to_string(C) + " banks";
        return o;
    }

    bool evolve() override {
        if (chunks_ >= 64) return false;
        prev_chunks_ = chunks_;
        chunks_ *= 2;               // recruit more memory banks
        return true;
    }

    bool revert() override {
        if (prev_chunks_ == 0) return false;
        chunks_ = prev_chunks_;
        prev_chunks_ = 0;
        return true;
    }

    int evolution_level() const override { return chunks_; }
    int max_difficulty()  const override { return 64; }   // up to 256 records

private:
    std::uint64_t seed_;
    int           chunks_      = 1;
    int           prev_chunks_ = 0;
};

// ---------------- sequence-induction faculty ----------------

class SequenceFaculty final : public Faculty {
public:
    explicit SequenceFaculty(std::uint64_t seed) : seed_(seed) {}

    std::string name() const override { return "sequence_induction"; }

    AttemptOutcome attempt(int difficulty) override {
        // difficulty d -> a repeating sequence of period (d + 1) over a
        // distinct-symbol alphabet. The cortex must learn to predict the
        // continuation. A fresh column each attempt: a real test, not
        // cumulative credit.
        const int period = difficulty + 1;
        khora::cortex::PredictiveColumn col(static_cast<std::size_t>(window_));

        std::vector<khora::lattice::Glyph> alphabet;
        alphabet.reserve(static_cast<std::size_t>(period));
        for (int p = 0; p < period; ++p) {
            alphabet.push_back(khora::lattice::Glyph::random(
                seed_ ^ (0x9E3779B97F4A7C15ULL * static_cast<std::uint64_t>(p + 1))));
        }

        // Train for several cycles, then measure accuracy over the last cycle.
        const int train_cycles = 40;
        for (int c = 0; c < train_cycles; ++c) {
            for (int p = 0; p < period; ++p) col.step(alphabet[static_cast<std::size_t>(p)]);
        }
        AttemptOutcome o;
        o.score  = col.recent_accuracy();
        o.detail = "period " + std::to_string(period) + ", window " +
                   std::to_string(window_);
        return o;
    }

    bool evolve() override {
        if (window_ >= 12) return false;
        prev_window_ = window_;
        ++window_;                 // a wider context window sees more of the period
        return true;
    }

    bool revert() override {
        if (prev_window_ == 0) return false;
        window_ = prev_window_;
        prev_window_ = 0;
        return true;
    }

    int evolution_level() const override { return window_; }
    int max_difficulty()  const override { return 16; }   // period up to 17

private:
    std::uint64_t seed_;
    int           window_      = 2;
    int           prev_window_ = 0;
};

} // namespace

std::unique_ptr<Faculty> make_relational_faculty(std::uint64_t seed) {
    return std::make_unique<RelationalFaculty>(seed);
}

std::unique_ptr<Faculty> make_sequence_faculty(std::uint64_t seed) {
    return std::make_unique<SequenceFaculty>(seed);
}

} // namespace khora::whetstone
