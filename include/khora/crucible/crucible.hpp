#pragma once

// The Crucible — Khora's forge of trial and evolution.
//
// Not a demo. Not a test. A pressure chamber where Khora's substrate is
// driven against hard cognition, measured without mercy, and — when it
// falls short — evolved and driven again. Failure is the trigger, never
// the verdict.
//
// The first faculty forged here is RELATIONAL REASONING via Vector
// Symbolic Architecture (Kanerva). Structured knowledge is encoded by
// binding role glyphs to filler glyphs and bundling the pairs into a
// single holographic record glyph. Khora then *reasons*:
//
//   - structured query : "currency of Mexico?"  -> unbind(CURRENCY) -> cleanup
//   - analogy          : "as Dollar is to USA, ? is to Mexico" -> record algebra
//   - holographic load : pack K records into ONE glyph, measure the
//                        capacity cliff where recovery degrades
//
// All of it is XOR-bind, majority-bundle, and nearest-neighbour cleanup
// over 10,000-bit glyphs. No lookup table holds the answers; the answers
// fall out of the algebra. That is reasoning, on the substrate, with no
// model weights and no language model.

#include "khora/lattice/glyph.hpp"
#include "khora/lattice/lattice.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace khora::crucible {

struct TrialResult {
    std::string name;
    std::size_t trials   = 0;
    std::size_t correct  = 0;
    double      score    = 0.0;   // correct / trials
    double      target   = 0.0;   // pass threshold
    std::string detail;
    bool passed() const { return score >= target; }
};

// One structured knowledge record: a subject and its (role -> filler) fields.
struct Record {
    std::string subject;
    std::vector<std::pair<std::string, std::string>> fields;
};

// Trajectory point from an evolution run.
struct EvolutionStep {
    int         generation;
    int         redundancy;
    double      score;
    std::string note;
};

class RelationalCrucible {
public:
    explicit RelationalCrucible(std::uint64_t seed = 0x5C0FF1CEC0FFEEULL);

    void add_record(Record r);
    void build();                       // assemble role/filler/record glyphs
    std::size_t record_count() const { return records_.size(); }
    std::size_t role_count()   const { return roles_.size(); }
    std::size_t filler_count() const { return fillers_.size(); }

    // --- the reasoning faculty ---

    // Structured query: unbind `role` from `subject`'s record, clean up.
    std::string query_field(const std::string& subject, const std::string& role) const;

    // Analogy: "as `src_filler` is to `src_subject`, ? is to `dst_subject`".
    std::string analogy(const std::string& src_subject,
                        const std::string& dst_subject,
                        const std::string& src_filler) const;

    // Holographic recall: build ONE world glyph from the first
    // `records_in_world` records, then query (subject,role) against it.
    std::string query_holographic(const khora::lattice::Glyph& world,
                                  const std::string& subject,
                                  const std::string& role) const;
    khora::lattice::Glyph build_world(std::size_t records_in_world) const;

    // --- the trials ---
    TrialResult trial_structured_unbind(double target = 0.95) const;
    TrialResult trial_analogy(double target = 0.80) const;
    TrialResult trial_holographic(std::size_t records_in_world, double target = 0.70) const;

    // --- evolution ---
    // Drive structured-unbind accuracy toward target by escalating the
    // redundancy of the encoding (each bound pair becomes a vote of R
    // permuted copies, cancelling crosstalk). Returns the trajectory.
    std::vector<EvolutionStep> evolve_structured(double target, int max_redundancy = 9);

    int redundancy() const { return redundancy_; }
    void set_redundancy(int r) { redundancy_ = (r < 1 ? 1 : r); rebuild_records_(); }

private:
    khora::lattice::Glyph role_glyph_(const std::string& role) const;
    khora::lattice::Glyph filler_glyph_(const std::string& filler) const;
    khora::lattice::Glyph subject_glyph_(const std::string& subject) const;
    khora::lattice::Glyph bind_pair_(const khora::lattice::Glyph& role,
                                     const khora::lattice::Glyph& filler) const;
    khora::lattice::Glyph encode_record_(const Record& r) const;
    std::string cleanup_(const khora::lattice::Glyph& noisy) const;       // against fillers
    std::string cleanup_role_(const khora::lattice::Glyph& noisy) const;  // against roles
    void rebuild_records_();

    std::uint64_t              seed_;
    int                        redundancy_ = 1;
    std::vector<Record>        records_;
    khora::lattice::Lattice    roles_;      // role name   -> role glyph
    khora::lattice::Lattice    fillers_;    // filler name -> filler glyph (the cleanup codebook)
    khora::lattice::Lattice    record_glyphs_; // subject  -> encoded record glyph
};

} // namespace khora::crucible
