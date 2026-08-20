#pragma once

// TemporalMemory — sequence memory with per-cell context.
//
// THE CAPABILITY THIS EXISTS FOR
//
// Train on two interleaved sequences, A B C D and X B C Y. After X B C the
// system must predict Y and not D; after A B C it must predict D and not Y.
// The shared subsequence B C is identical in both, so the disambiguating
// element is arbitrarily far back.
//
// PredictiveColumn structurally cannot do this. It encodes context as a
// position-permuted bundle of the last K inputs and stores context -> next in a
// Lattice, so once the window slides past the disambiguating element the two
// contexts are the SAME GLYPH and the two futures collide. Widening the window
// only moves the wall.
//
// THE MECHANISM
//
// A minicolumn represents WHAT is being seen. The cells inside it represent
// WHICH CONTEXT it is being seen in. That is the whole idea, and it comes
// straight from the two kinds of input a cortical pyramidal cell receives.
//
// Proximal (feedforward) input decides WHETHER a cell can fire. Distal input --
// 8 to 20 co-active synapses within about 40 um of each other on one basal
// segment -- generates an NMDA plateau worth 3 to 23 mV at the soma, lasting
// 50 to 100 ms (Major et al. 2008, J Neurophysiol 99:2584; Antic et al. 2010,
// J Neurosci Res 88:2991). That is nowhere near enough to fire the cell. It can
// only PRIME it. Two input classes with two semantics, structurally unable to
// substitute for each other.
//
// So: a column that is fed forward AND primed fires only its primed cells --
// the specific-context representation. A column fed forward with nothing primed
// BURSTS, firing every cell, which means "this input, in no context I know".
// Bursting is not a failure mode; it is the system reporting its own ignorance,
// and the fraction of columns bursting is an anomaly score that costs nothing
// extra to compute.
//
// This depends entirely on the sparse substrate. A segment samples ~24 of the
// active cells and fires at a threshold, and the false-match rate of that test
// is P(Binomial(s, density) >= theta) -- 0.58 on a dense code at every
// dimension, and ~1e-16 on the sparse one. Without sparsity a segment would be
// primed by almost anything and every column would report every context.
//
// Reference: Hawkins & Ahmad 2016, Front Neural Circuits 10:23.

#include "khora/lattice/sdr.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace khora::cortex {

// One minicolumn per Sdr position, one active minicolumn per active position:
// 16,384 columns of which 256 are active, 1.5625%. The sparse substrate already
// carries exactly this structure, so no extra pooling step is needed to produce
// it -- and a Spatial Pooler is deliberately not built, since Numenta's own
// evaluation found it close to a random static projection once boosting is off.
inline constexpr std::size_t kColumns      = khora::lattice::kSdrBits;
inline constexpr std::size_t kActiveCols   = khora::lattice::kSdrBlocks;
inline constexpr std::size_t kCellsPerCol  = 32;
inline constexpr std::size_t kTotalCells   = kColumns * kCellsPerCol;

struct TemporalMemoryConfig {
    // Connected-synapse count for a segment to prime its cell.
    std::uint8_t activation_threshold = 13;
    // Lower bar used only to pick which cell to teach in a bursting column, so
    // a partly-right segment is reinforced instead of a fresh one being grown.
    std::uint8_t min_threshold = 10;
    // Synapses grown per learning step toward the previous winners.
    std::uint8_t new_synapse_count = 20;

    // Permanence as uint8. Binary connectivity: at or above `connected` the
    // synapse contributes, below it contributes nothing. Permanence NEVER
    // scales the contribution -- it is a learning variable, not a weight.
    std::uint8_t permanence_initial   = 54;   // 0.21
    std::uint8_t permanence_connected = 128;  // 0.50
    std::uint8_t permanence_increment = 26;   // 0.10
    std::uint8_t permanence_decrement = 26;   // 0.10

    // Punishment for a segment that primed a column which then did not
    // activate. Both reference implementations ship this at 0.0, which means a
    // wrong prediction is never unlearned -- fine for clean repeating data, and
    // wrong for anything noisy or branching. Non-zero by default here, and
    // exposed so the choice stays visible.
    std::uint8_t predicted_decrement = 3;     // ~0.01

    std::uint8_t max_segments_per_cell = 128;

    // A distal plateau outlasts a single step (50-100 ms against a much
    // shorter tick), so the primed state is a countdown rather than a flag.
    std::uint8_t predictive_lifetime = 2;

    // TWO RATES FROM ONE MECHANISM.
    //
    // Whether a new synapse is born already connected is the whole difference
    // between one-shot and statistical learning, and it is one number.
    //
    // With permanence_initial below permanence_connected, a synapse contributes
    // nothing until reinforcement has carried it over the line -- 54 -> 80 ->
    // 106 -> 132 at the default increment, so roughly three exposures. That is
    // the SLOW store: it learns what recurs and ignores what happened once,
    // which is what a semantic memory should do.
    //
    // With permanence_initial at or above permanence_connected, a single
    // exposure creates connected synapses and the sequence is known after one
    // presentation. That is the FAST store, and it is the reason the
    // hippocampus exists as separate tissue: dentate gyrus and CA3 encode in
    // one shot, sparsely and with high efficacy, precisely so that a single
    // episode can be laid down without waiting for it to repeat.
    //
    // McClelland, McNaughton & O'Reilly 1995 (Psych Rev 102:419) is the
    // argument that a system needs BOTH, and that the two cannot be the same
    // store at the same rate: fast learning of arbitrary new material
    // necessarily interferes with slow-learned structure. Here they are one
    // implementation with two configurations, and the boundary is explicit.

    // One-shot: a single presentation is enough to be recognised afterwards.
    static TemporalMemoryConfig episodic() {
        TemporalMemoryConfig c;
        c.permanence_initial   = 140;   // already above `connected`
        c.permanence_increment = 20;
        c.permanence_decrement = 10;    // gentler: one exposure is all there was
        c.predicted_decrement  = 2;
        return c;
    }

    // Statistical: learns what recurs, ignores what happened once. The default.
    static TemporalMemoryConfig semantic() { return TemporalMemoryConfig{}; }
};

struct TemporalMemoryStats {
    std::size_t active_columns   = 0;
    std::size_t bursting_columns = 0;
    std::size_t predicted_cells  = 0;
    std::size_t active_cells     = 0;
    std::size_t segments         = 0;
    std::size_t synapses         = 0;
    // Fraction of active columns that burst: the system's own report of how
    // much of what it just saw it did not see coming. One pass, no extra model.
    double      anomaly          = 0.0;
};

class TemporalMemory {
public:
    static constexpr std::size_t kMaxSynapsesPerSegment = 40;

    explicit TemporalMemory(TemporalMemoryConfig cfg = {});

    // Present one input. `learn` false runs inference without changing state
    // other than the activation and priming that inference needs.
    //
    // `source` tags whatever is learned on this step with the id of the episode
    // that taught it, so a later prediction can name its evidence. Free: one
    // uint32 per segment, written once.
    TemporalMemoryStats compute(const khora::lattice::Sdr& input, bool learn = true,
                                std::uint32_t source = kNoSource);

    static constexpr std::uint32_t kNoSource = 0xFFFFFFFFu;

    // PROVENANCE. Which stored episodes are responsible for the prediction the
    // system is currently making.
    //
    // Neither tissue nor a language model can answer this. A brain has no
    // introspective access to which memories produced a thought. A language
    // model's chain-of-thought is not causally load-bearing -- delete the source
    // it cites and the answer does not change, because the citation was
    // generated alongside the answer rather than consulted to produce it.
    //
    // Here the answer IS the mechanism: a cell is primed because specific
    // segments matched, each segment was grown during one specific episode, and
    // the tag was written at that moment. The claim is falsifiable in the
    // strongest way available -- forget exactly these ids and the prediction
    // must change; forget the same number of others and it must not.
    std::vector<std::uint32_t> explain() const;

    // Erase everything learned from one episode. Returns segments removed.
    std::size_t forget(std::uint32_t source);

    // Clear the temporal context without forgetting anything learned -- the
    // start of a new sequence, not a new life.
    void reset();

    // Which columns are currently primed. This is the PREDICTION, and it is a
    // union rather than a single pattern, because more than one continuation
    // can legitimately be expected at once.
    const khora::lattice::SdrUnion& predicted_columns() const noexcept { return predicted_; }
    bool is_column_predicted(std::size_t column) const noexcept;

    // Kill a fraction of cells outright, to verify that matching really is
    // subsampled. Real tissue loses units; a system whose recognition depends
    // on every unit being present is not doing what it claims to be doing.
    void lesion(double fraction, std::uint64_t seed);

    const TemporalMemoryStats& last() const noexcept { return last_; }
    std::size_t segment_count() const noexcept { return segments_.size(); }
    // Which cells fired on the last step. Exposed for diagnosis: if two
    // different contexts activate the SAME cells for a shared symbol, the
    // contexts have already merged and nothing downstream can separate them.
    const std::vector<std::uint32_t>& debug_active_cells() const noexcept {
        return active_cells_;
    }
    const TemporalMemoryConfig& config() const noexcept { return cfg_; }

private:
    struct Segment {
        std::uint32_t owner  = 0;                                 // cell id
        std::uint32_t source = kNoSource;                         // episode that taught it
        std::uint8_t  count = 0;
        std::array<std::uint32_t, kMaxSynapsesPerSegment> presyn{};
        std::array<std::uint8_t,  kMaxSynapsesPerSegment> perm{};
        bool          dead = false;
    };

    // Segment activity for the step just computed.
    struct SegmentActivity {
        std::uint32_t id = 0;
        std::uint16_t connected_hits = 0;   // synapses at or above threshold
        std::uint16_t potential_hits = 0;   // synapses of any permanence
    };

    TemporalMemoryConfig cfg_;

    std::vector<Segment>                                       segments_;
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> cell_segments_;   // cell -> segments it owns
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> presyn_segments_; // cell -> segments listening to it
    std::vector<bool>                                          cell_dead_;

    std::vector<std::uint32_t> active_cells_;
    std::vector<std::uint32_t> winner_cells_;
    std::vector<std::uint32_t> prev_active_cells_;
    std::vector<std::uint32_t> prev_winner_cells_;

    std::vector<SegmentActivity> active_segments_;       // primed this step
    std::vector<SegmentActivity> matching_segments_;     // partially matched
    khora::lattice::SdrUnion     predicted_;
    std::unordered_map<std::uint32_t, std::uint8_t> predictive_until_;  // cell -> ticks left

    TemporalMemoryStats last_;
    std::uint64_t       rng_ = 0x9E3779B97F4A7C15ULL;

    std::uint64_t next_rand() noexcept;
    void          compute_segment_activity(const std::vector<std::uint32_t>& active);
    std::uint32_t best_matching_cell(std::size_t column, std::uint32_t* seg_out,
                                     std::uint16_t* hits_out);
    std::uint32_t least_used_cell(std::size_t column);
    std::uint32_t grow_segment(std::uint32_t cell, std::uint32_t source);
    void          adapt_segment(std::uint32_t seg, const std::vector<std::uint32_t>& prev_active,
                                bool reinforce);
    void          grow_synapses(std::uint32_t seg, const std::vector<std::uint32_t>& candidates,
                                std::size_t n);
};

} // namespace khora::cortex
