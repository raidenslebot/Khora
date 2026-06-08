#pragma once

// Stratiform Cortex — predictive-coding column.
//
// A PredictiveColumn consumes a stream of input Glyphs. It maintains a
// sliding context window of the last K inputs, encodes the context as
// a position-aware bundled Glyph, and associates each (context -> next)
// pair it observes in an internal Lattice. At each step it predicts
// what's about to arrive based on the current context, then learns
// from the actual input. Prediction error and recent accuracy are
// reported so the column's learning curve is observable.

#include "khora/lattice/lattice.hpp"

#include <cstddef>
#include <deque>
#include <filesystem>

namespace khora::cortex {

class PredictiveColumn {
public:
    explicit PredictiveColumn(std::size_t context_window = 3);

    struct StepResult {
        khora::lattice::Glyph predicted;        // pre-input prediction
        khora::lattice::Glyph actual;           // glyph that arrived
        std::size_t           prediction_error; // hamming(predicted, actual)
        double                similarity;       // sim(predicted, actual), [-1, 1]
        bool                  novel_context;    // best context match was poor (sim < 0.3)
    };

    // Feed one input. Returns the column's prediction *before* seeing it,
    // along with the actual and the error signals. Then learns. This does
    // a k-NN query (O(associations)) for the prediction.
    StepResult step(const khora::lattice::Glyph& input);

    // Fast learning for bulk study: stores the (context -> input)
    // association and advances the window WITHOUT the per-token k-NN
    // prediction. O(context_window) per call instead of O(associations).
    // Use this to absorb large texts; sample accuracy with step()/predict()
    // periodically if needed.
    void learn(const khora::lattice::Glyph& input);

    // Predict-without-learn — peek at what would come next.
    khora::lattice::Glyph predict() const;

    // Bounded associative memory (brain-like forgetting). When the number
    // of stored associations exceeds the cap, the oldest is evicted. This
    // keeps memory finite and per-step queries bounded. 0 = unbounded.
    void        set_max_associations(std::size_t n) { max_associations_ = n; }
    std::size_t max_associations() const noexcept   { return max_associations_; }

    // Shed memory: evict oldest associations down to `target`. Returns the
    // number evicted. Used by the Ballast under memory pressure.
    std::size_t prune_associations(std::size_t target);

    // Stats
    std::size_t observations()    const noexcept { return observations_; }
    std::size_t associations()    const          { return ctx_keys_.size(); }
    double      recent_accuracy() const;          // mean similarity over last 64 steps

    // Persistence — writes/reads three files under the given prefix:
    //   <prefix>.cortex      — small header (version, counters, window state)
    //   <prefix>.keys.klat   — context-key Lattice
    //   <prefix>.vals.klat   — next-value Lattice
    // Throws khora::lattice::PersistError on I/O failure or format mismatch.
    void save(const std::filesystem::path& prefix) const;
    void load(const std::filesystem::path& prefix);

private:
    std::size_t                       context_window_;
    std::deque<khora::lattice::Glyph> recent_;
    khora::lattice::Lattice           ctx_keys_;
    khora::lattice::Lattice           ctx_vals_;
    std::size_t                       observations_ = 0;
    std::size_t                       next_assoc_id_ = 0;
    std::size_t                       max_associations_ = 200000;
    std::deque<std::string>           assoc_order_;   // FIFO for eviction

    static constexpr std::size_t kRecentWindow = 64;
    std::deque<double>                recent_sims_;

    khora::lattice::Glyph current_context_() const;
    void store_and_advance_(const khora::lattice::Glyph& input);
};

} // namespace khora::cortex
