#include "khora/cortex/temporal_memory.hpp"

#include <algorithm>
#include <limits>

namespace khora::cortex {

using khora::lattice::Sdr;
using khora::lattice::SdrUnion;

namespace {

inline std::uint32_t cell_of(std::size_t column, std::size_t idx) noexcept {
    return static_cast<std::uint32_t>(column * kCellsPerCol + idx);
}
inline std::size_t column_of(std::uint32_t cell) noexcept {
    return cell / kCellsPerCol;
}

} // namespace

TemporalMemory::TemporalMemory(TemporalMemoryConfig cfg)
    : cfg_(cfg), cell_dead_(kTotalCells, false) {}

std::uint64_t TemporalMemory::next_rand() noexcept {
    std::uint64_t z = (rng_ += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

void TemporalMemory::reset() {
    active_cells_.clear();
    winner_cells_.clear();
    prev_active_cells_.clear();
    prev_winner_cells_.clear();
    active_segments_.clear();
    matching_segments_.clear();
    predictive_until_.clear();
    predicted_.clear();
}

bool TemporalMemory::is_column_predicted(std::size_t column) const noexcept {
    const std::size_t block = column / khora::lattice::kSdrBlockSize;
    const std::uint8_t idx  = static_cast<std::uint8_t>(column % khora::lattice::kSdrBlockSize);
    return predicted_.contains(block, idx);
}

// Walk only the cells that were active, and only the segments listening to
// them. The naive form -- every segment of every cell of every column -- is the
// difference between real time and not, and it is the reason the presynaptic
// index exists at all.
void TemporalMemory::compute_segment_activity(const std::vector<std::uint32_t>& active) {
    active_segments_.clear();
    matching_segments_.clear();

    if (hit_scratch_.size() < segments_.size())
        hit_scratch_.resize(segments_.size(), {std::uint16_t{0}, std::uint16_t{0}});
    hit_touched_.clear();

    for (const std::uint32_t c : active) {
        const auto it = presyn_segments_.find(c);
        if (it == presyn_segments_.end()) continue;
        for (const auto& [sid, k] : it->second) {
            const Segment& s = segments_[sid];
            if (s.dead || cell_dead_[s.owner]) continue;
            auto& h = hit_scratch_[sid];
            if (h.second == 0) hit_touched_.push_back(sid);
            ++h.second;                                            // potential
            if (s.perm[k] >= cfg_.permanence_connected) ++h.first;  // connected
        }
    }

    for (const std::uint32_t sid : hit_touched_) {
        const auto h = hit_scratch_[sid];
        hit_scratch_[sid] = {std::uint16_t{0}, std::uint16_t{0}};
        if (h.first >= cfg_.activation_threshold) {
            active_segments_.push_back({sid, h.first, h.second});
        } else if (h.second >= cfg_.min_threshold) {
            matching_segments_.push_back({sid, h.first, h.second});
        }
    }
    // Deterministic order: the substrate's claim is reproducible reasoning, and
    // an unordered_map's iteration order is not a basis for one.
    const auto by_id = [](const SegmentActivity& a, const SegmentActivity& b) {
        return a.id < b.id;
    };
    std::sort(active_segments_.begin(), active_segments_.end(), by_id);
    std::sort(matching_segments_.begin(), matching_segments_.end(), by_id);
}

std::uint32_t TemporalMemory::best_matching_cell(std::size_t column, std::uint32_t* seg_out,
                                                 std::uint16_t* hits_out) {
    std::uint32_t best_seg = std::numeric_limits<std::uint32_t>::max();
    std::uint16_t best_hits = 0;
    for (const auto& m : matching_segments_) {
        const std::uint32_t owner = segments_[m.id].owner;
        if (column_of(owner) != column) continue;
        if (m.potential_hits > best_hits) { best_hits = m.potential_hits; best_seg = m.id; }
    }
    if (best_seg != std::numeric_limits<std::uint32_t>::max()) {
        if (seg_out)  *seg_out  = best_seg;
        if (hits_out) *hits_out = best_hits;
        return segments_[best_seg].owner;
    }
    if (seg_out)  *seg_out  = std::numeric_limits<std::uint32_t>::max();
    if (hits_out) *hits_out = 0;
    return least_used_cell(column);
}

// Fewest segments wins; ties are broken by the seeded RNG, NOT by index. This
// is the allocation policy, and the tie-break is load-bearing rather than
// cosmetic.
//
// The first time a symbol is seen in two different contexts, every cell in its
// column has zero segments, so every cell ties. Picking the lowest index makes
// BOTH contexts choose the same cell, that one cell acquires a segment for each,
// and thereafter it fires in both -- so the contexts never separate and the two
// futures are predicted together. Measured with a deterministic tie-break: after
// A B C the system predicted D at 1.00 and Y at 1.00, which is no prediction at
// all. Choosing at random among the tied cells lands the two contexts on
// different cells, which is the entire mechanism.
//
// Still reproducible: the generator is seeded and advanced in call order, so two
// identical runs allocate identically.
std::uint32_t TemporalMemory::least_used_cell(std::size_t column) {
    std::size_t best_n = std::numeric_limits<std::size_t>::max();
    std::uint32_t tied[kCellsPerCol];
    std::size_t   n_tied = 0;

    for (std::size_t i = 0; i < kCellsPerCol; ++i) {
        const std::uint32_t c = cell_of(column, i);
        if (cell_dead_[c]) continue;
        const auto it = cell_segments_.find(c);
        const std::size_t n = (it == cell_segments_.end()) ? 0 : it->second.size();
        if (n < best_n) { best_n = n; n_tied = 0; }
        if (n == best_n) tied[n_tied++] = c;
    }
    if (n_tied == 0) return cell_of(column, 0);          // whole column lesioned
    return tied[static_cast<std::size_t>(next_rand() % n_tied)];
}

std::uint32_t TemporalMemory::grow_segment(std::uint32_t cell, std::uint32_t source) {
    auto& owned = cell_segments_[cell];
    if (owned.size() >= cfg_.max_segments_per_cell) {
        // Recycle the least-populated segment rather than growing without bound.
        std::uint32_t victim = owned.front();
        std::uint8_t  fewest = segments_[victim].count;
        for (const std::uint32_t sid : owned) {
            if (segments_[sid].count < fewest) { fewest = segments_[sid].count; victim = sid; }
        }
        Segment& s = segments_[victim];
        for (std::size_t k = 0; k < s.count; ++k) {
            auto it = presyn_segments_.find(s.presyn[k]);
            if (it != presyn_segments_.end()) {
                auto& v = it->second;
                v.erase(std::remove_if(v.begin(), v.end(),
                                       [victim](const auto& e) { return e.first == victim; }),
                        v.end());
            }
        }
        total_synapses_ -= s.count;
        s.count = 0;
        s.source = source;
        return victim;
    }
    Segment s;
    s.owner  = cell;
    s.source = source;
    segments_.push_back(s);
    const std::uint32_t id = static_cast<std::uint32_t>(segments_.size() - 1);
    owned.push_back(id);
    return id;
}

// Hebbian on the segment: what was active gets stronger, what was not gets
// weaker. Permanence is a learning variable and never a weight -- the synapse
// either contributes or does not, decided by one threshold.
void TemporalMemory::adapt_segment(std::uint32_t seg,
                                   const std::vector<std::uint32_t>& prev_active,
                                   bool reinforce) {
    Segment& s = segments_[seg];
    for (std::size_t k = 0; k < s.count; ++k) {
        const bool was_active =
            std::binary_search(prev_active.begin(), prev_active.end(), s.presyn[k]);
        if (reinforce && was_active) {
            s.perm[k] = static_cast<std::uint8_t>(
                std::min<int>(255, s.perm[k] + cfg_.permanence_increment));
        } else if (reinforce) {
            s.perm[k] = static_cast<std::uint8_t>(
                std::max<int>(0, s.perm[k] - cfg_.permanence_decrement));
        } else if (was_active) {
            // Punishing a segment that primed a column which did not activate.
            s.perm[k] = static_cast<std::uint8_t>(
                std::max<int>(0, s.perm[k] - cfg_.predicted_decrement));
        }
    }
}

void TemporalMemory::grow_synapses(std::uint32_t seg,
                                   const std::vector<std::uint32_t>& candidates,
                                   std::size_t n) {
    if (candidates.empty()) return;
    Segment& s = segments_[seg];

    // Sample without replacement from the candidates, skipping any already
    // present on this segment.
    std::vector<std::uint32_t> pool;
    pool.reserve(candidates.size());
    for (const std::uint32_t c : candidates) {
        bool present = false;
        for (std::size_t k = 0; k < s.count; ++k) {
            if (s.presyn[k] == c) { present = true; break; }
        }
        if (!present && !cell_dead_[c]) pool.push_back(c);
    }
    const std::size_t room = (s.count >= kMaxSynapsesPerSegment)
                                 ? 0 : kMaxSynapsesPerSegment - std::size_t{s.count};
    const std::size_t want = std::min({n, pool.size(), room});
    if (want == 0) return;
    for (std::size_t i = 0; i < want; ++i) {
        const std::size_t j = i + static_cast<std::size_t>(next_rand() % (pool.size() - i));
        std::swap(pool[i], pool[j]);
        s.presyn[s.count] = pool[i];
        s.perm[s.count]   = cfg_.permanence_initial;
        presyn_segments_[pool[i]].emplace_back(seg, s.count);
        ++s.count;
        ++total_synapses_;
    }
}

TemporalMemoryStats TemporalMemory::compute(const Sdr& input, bool learn,
                                           std::uint32_t source) {
    prev_active_cells_ = active_cells_;
    prev_winner_cells_ = winner_cells_;
    std::sort(prev_active_cells_.begin(), prev_active_cells_.end());

    active_cells_.clear();
    winner_cells_.clear();

    // Which cells are currently primed, from the segments that fired on the
    // PREVIOUS step. This is the prediction being tested by the input now
    // arriving.
    std::unordered_map<std::uint32_t, std::pair<std::uint32_t, std::uint16_t>> primed;
    for (const auto& a : active_segments_)
        primed.emplace(segments_[a.id].owner, std::make_pair(a.id, a.potential_hits));

    TemporalMemoryStats st;
    std::vector<std::uint32_t> active_columns;
    active_columns.reserve(kActiveCols);
    for (std::size_t b = 0; b < khora::lattice::kSdrBlocks; ++b) {
        active_columns.push_back(
            static_cast<std::uint32_t>(b * khora::lattice::kSdrBlockSize + input.index(b)));
    }
    st.active_columns = active_columns.size();

    // (segment, potential synapses already matching) and
    // (winner cell, segment or MAX, potential synapses already matching)
    std::vector<std::pair<std::uint32_t, std::uint16_t>> segments_to_reinforce;
    struct Burst { std::uint32_t winner; std::uint32_t seg; std::uint16_t already; };
    std::vector<Burst> bursts;

    for (const std::uint32_t col : active_columns) {
        bool any_primed = false;
        for (std::size_t i = 0; i < kCellsPerCol; ++i) {
            const std::uint32_t c = cell_of(col, i);
            if (cell_dead_[c]) continue;
            const auto it = primed.find(c);
            if (it == primed.end()) continue;
            any_primed = true;
            active_cells_.push_back(c);
            winner_cells_.push_back(c);
            if (learn) segments_to_reinforce.emplace_back(it->second.first, it->second.second);
        }
        if (any_primed) continue;

        // BURSTING. Nothing in this column expected this input, so every cell
        // fires: "this input, in no context I recognise". One winner is chosen
        // to carry the lesson.
        ++st.bursting_columns;
        for (std::size_t i = 0; i < kCellsPerCol; ++i) {
            const std::uint32_t c = cell_of(col, i);
            if (!cell_dead_[c]) active_cells_.push_back(c);
        }
        std::uint32_t seg = std::numeric_limits<std::uint32_t>::max();
        std::uint16_t already = 0;
        const std::uint32_t winner = best_matching_cell(col, &seg, &already);
        winner_cells_.push_back(winner);
        if (learn) bursts.push_back({winner, seg, already});
    }

    if (learn && !prev_active_cells_.empty()) {
        // How many synapses a segment may GROW is capped by how many it already
        // has matching the current input, and that cap is load-bearing.
        //
        // On the first exposure a predecessor column BURSTS, so every one of its
        // cells is active and two genuinely different contexts look identical.
        // A segment trained in one context therefore matches the other, gets
        // selected as the best match, and -- if allowed to grow freely -- wires
        // itself to the second context's winners as well. It then fires in both
        // forever, and the two contexts can never separate no matter how many
        // epochs follow. Measured with unrestricted growth: B separated cleanly
        // (0 cells shared) while C was bit-identical in both contexts (256 of
        // 256 shared), and the network predicted both futures at once.
        //
        // Capping growth at (new_synapse_count - already_matching) means a
        // segment that already explains the input adds nothing. Once the
        // predecessor stops bursting the contexts diverge, the stale segment no
        // longer matches, the column bursts, and a fresh cell is allocated --
        // which is the separation actually happening.
        const auto room_to_grow = [&](std::uint16_t already) {
            return (already >= cfg_.new_synapse_count)
                       ? std::size_t{0}
                       : std::size_t{cfg_.new_synapse_count} - already;
        };

        // Correct predictions: strengthen what fired, weaken what did not.
        for (const auto& [sid, already] : segments_to_reinforce) {
            adapt_segment(sid, prev_active_cells_, true);
            grow_synapses(sid, prev_winner_cells_, room_to_grow(already));
        }
        // Bursts: teach the chosen cell this context.
        for (const auto& [winner, seg, already] : bursts) {
            std::uint32_t sid = seg;
            if (sid == std::numeric_limits<std::uint32_t>::max()) sid = grow_segment(winner, source);
            else                                                  adapt_segment(sid, prev_active_cells_, true);
            grow_synapses(sid, prev_winner_cells_, room_to_grow(already));
        }
        // Wrong predictions: a segment primed a column that did not activate.
        // Without this a false prediction is never unlearned, which is fine on
        // clean repeating data and wrong on anything that branches.
        //
        // MATCHING segments are punished as well as active ones, and that is
        // not a detail. A segment below the connected threshold does not fire,
        // so punishing only what fired leaves it untouched -- and it still gets
        // REINFORCED whenever its column bursts, because a burst activates every
        // cell and therefore matches stale segments trained on any context.
        // Left alone, a first-epoch segment learned under an ambiguous burst is
        // driven above threshold by exactly the bursts that should be teaching
        // the system to tell the contexts apart. Measured with active-only
        // punishment: X B C predicted Y at 256/256 and D at 3/256 (correct)
        // while A B C predicted BOTH at 256/256 -- one stale segment, revived.
        if (cfg_.predicted_decrement > 0) {
            std::vector<std::uint32_t> sorted_cols = active_columns;
            std::sort(sorted_cols.begin(), sorted_cols.end());
            const auto punish = [&](const std::vector<SegmentActivity>& segs) {
                for (const auto& a : segs) {
                    const std::uint32_t owner_col =
                        static_cast<std::uint32_t>(column_of(segments_[a.id].owner));
                    if (!std::binary_search(sorted_cols.begin(), sorted_cols.end(), owner_col)) {
                        adapt_segment(a.id, prev_active_cells_, false);
                    }
                }
            };
            punish(active_segments_);
            punish(matching_segments_);
        }
    }

    // Now compute what the cells that just fired predict for the NEXT step.
    std::vector<std::uint32_t> sorted_active = active_cells_;
    std::sort(sorted_active.begin(), sorted_active.end());
    compute_segment_activity(sorted_active);

    predicted_.clear();
    for (const auto& a : active_segments_) {
        const std::size_t col = column_of(segments_[a.id].owner);
        predicted_.add_position(col / khora::lattice::kSdrBlockSize,
                                static_cast<std::uint8_t>(col % khora::lattice::kSdrBlockSize));
        ++st.predicted_cells;
    }

    st.active_cells = active_cells_.size();
    st.segments     = segments_.size();
    st.synapses = total_synapses_;
    st.anomaly = st.active_columns
                     ? static_cast<double>(st.bursting_columns) /
                           static_cast<double>(st.active_columns)
                     : 0.0;
    last_ = st;
    return st;
}

// The evidence for the prediction currently being made: the episodes that grew
// the segments now firing. Not a reconstruction and not a rationalisation --
// these are the segments whose connected synapses actually crossed threshold,
// and the tag on each was written at the moment it was learned.
std::vector<std::uint32_t> TemporalMemory::explain() const {
    std::vector<std::uint32_t> out;
    out.reserve(active_segments_.size());
    for (const auto& a : active_segments_) {
        const Segment& s = segments_[a.id];
        if (s.dead || s.source == kNoSource) continue;
        out.push_back(s.source);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// Erase one episode. Every segment it taught is removed from the presynaptic
// index and marked dead, so nothing it contributed survives -- which is what
// makes the provenance claim testable rather than decorative.
std::size_t TemporalMemory::forget(std::uint32_t source) {
    std::size_t removed = 0;
    for (std::size_t sid = 0; sid < segments_.size(); ++sid) {
        Segment& s = segments_[sid];
        if (s.dead || s.source != source) continue;
        for (std::size_t k = 0; k < s.count; ++k) {
            auto it = presyn_segments_.find(s.presyn[k]);
            if (it != presyn_segments_.end()) {
                auto& v = it->second;
                const auto dead_id = static_cast<std::uint32_t>(sid);
                v.erase(std::remove_if(v.begin(), v.end(),
                                       [dead_id](const auto& e) { return e.first == dead_id; }),
                        v.end());
            }
        }
        auto oit = cell_segments_.find(s.owner);
        if (oit != cell_segments_.end()) {
            auto& v = oit->second;
            v.erase(std::remove(v.begin(), v.end(), static_cast<std::uint32_t>(sid)), v.end());
        }
        total_synapses_ -= s.count;
        s.dead  = true;
        s.count = 0;
        ++removed;
    }
    return removed;
}

void TemporalMemory::lesion(double fraction, std::uint64_t seed) {
    rng_ = seed ? seed : 1;
    for (std::size_t c = 0; c < kTotalCells; ++c) {
        if (static_cast<double>(next_rand() % 10000) / 10000.0 < fraction) {
            cell_dead_[c] = true;
        }
    }
}

} // namespace khora::cortex
