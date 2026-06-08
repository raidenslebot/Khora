#include "khora/crucible/crucible.hpp"

#include <algorithm>
#include <limits>
#include <span>
#include <string>
#include <unordered_set>

namespace khora::crucible {

using khora::lattice::Glyph;
using khora::lattice::Lattice;
using khora::lattice::bind;
using khora::lattice::bundle;
using khora::lattice::permute;

namespace {
inline std::uint64_t mix(std::uint64_t x) noexcept {
    x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 27; x *= 0x94D049BB133111EBULL;
    x ^= x >> 31; return x;
}
// Stable seed from a string + a salt, so role/filler glyphs are
// deterministic and independent across categories.
std::uint64_t seed_for(const std::string& s, std::uint64_t salt) {
    std::uint64_t h = 0xCBF29CE484222325ULL ^ salt;
    for (unsigned char c : s) { h ^= c; h *= 0x100000001B3ULL; }
    return mix(h);
}
} // namespace

RelationalCrucible::RelationalCrucible(std::uint64_t seed) : seed_(seed) {}

void RelationalCrucible::add_record(Record r) {
    records_.push_back(std::move(r));
}

Glyph RelationalCrucible::role_glyph_(const std::string& role) const {
    return Glyph::random(seed_for(role, seed_ ^ 0x501E501E501E501EULL));   // "role" salt
}

Glyph RelationalCrucible::filler_glyph_(const std::string& filler) const {
    return Glyph::random(seed_for(filler, seed_ ^ 0xF111EDF111EDF111ULL)); // "filler" salt
}

Glyph RelationalCrucible::subject_glyph_(const std::string& subject) const {
    return Glyph::random(seed_for(subject, seed_ ^ 0x5B1EC75B1EC75B1EULL)); // "subject" salt
}

Glyph RelationalCrucible::bind_pair_(const Glyph& role, const Glyph& filler) const {
    if (redundancy_ <= 1) return bind(role, filler);
    // Redundant binding: R permuted copies of the bound pair, voted
    // together. On unbind we permute back and the votes reinforce the
    // true filler while crosstalk cancels.
    std::vector<Glyph> copies;
    copies.reserve(static_cast<std::size_t>(redundancy_));
    for (int r = 0; r < redundancy_; ++r) {
        copies.push_back(permute(bind(role, filler), r * 211));
    }
    return bundle(std::span<const Glyph>{copies.data(), copies.size()});
}

Glyph RelationalCrucible::encode_record_(const Record& rec) const {
    std::vector<Glyph> pairs;
    pairs.reserve(rec.fields.size());
    for (const auto& [role, filler] : rec.fields) {
        pairs.push_back(bind_pair_(role_glyph_(role), filler_glyph_(filler)));
    }
    if (pairs.empty()) return Glyph::zero();
    return bundle(std::span<const Glyph>{pairs.data(), pairs.size()});
}

void RelationalCrucible::rebuild_records_() {
    record_glyphs_ = Lattice{};
    for (const auto& rec : records_) {
        record_glyphs_.store(rec.subject, encode_record_(rec));
    }
}

void RelationalCrucible::build() {
    roles_   = Lattice{};
    fillers_ = Lattice{};
    std::unordered_set<std::string> seen_roles, seen_fillers;
    for (const auto& rec : records_) {
        for (const auto& [role, filler] : rec.fields) {
            if (seen_roles.insert(role).second)     roles_.store(role, role_glyph_(role));
            if (seen_fillers.insert(filler).second) fillers_.store(filler, filler_glyph_(filler));
        }
    }
    rebuild_records_();
}

std::string RelationalCrucible::cleanup_(const Glyph& noisy) const {
    const auto matches = fillers_.query(noisy, 1);
    return matches.empty() ? std::string{} : matches.front().label;
}

std::string RelationalCrucible::cleanup_role_(const Glyph& noisy) const {
    const auto matches = roles_.query(noisy, 1);
    return matches.empty() ? std::string{} : matches.front().label;
}

std::string RelationalCrucible::query_field(const std::string& subject,
                                            const std::string& role) const {
    auto rec = record_glyphs_.recall(subject);
    if (!rec) return {};
    const Glyph role_g = role_glyph_(role);

    if (redundancy_ <= 1) {
        // Single unbind: record XOR role  ~  filler (+ crosstalk)
        return cleanup_(bind(*rec, role_g));
    }
    // Redundant unbind: undo each permuted copy, vote.
    std::vector<Glyph> votes;
    votes.reserve(static_cast<std::size_t>(redundancy_));
    const Glyph unbound = bind(*rec, role_g);
    for (int r = 0; r < redundancy_; ++r) {
        votes.push_back(permute(unbound, -(r * 211)));
    }
    return cleanup_(bundle(std::span<const Glyph>{votes.data(), votes.size()}));
}

std::string RelationalCrucible::analogy(const std::string& src_subject,
                                        const std::string& dst_subject,
                                        const std::string& src_filler) const {
    auto src = record_glyphs_.recall(src_subject);
    auto dst = record_glyphs_.recall(dst_subject);
    if (!src || !dst) return {};
    // Stage 1: recover which role binds src_filler in the source record.
    //   role* ~ src_record XOR filler_src   (noisy)
    const Glyph role_star = bind(*src, filler_glyph_(src_filler));
    // Stage 2: CLEAN UP role* against the role codebook. This is the key
    // step — collapsing the noisy role estimate onto the exact role glyph
    // removes the crosstalk that wrecked the naive one-shot analogy.
    const std::string role_name = cleanup_role_(role_star);
    if (role_name.empty()) return {};
    const Glyph clean_role = role_glyph_(role_name);
    // Stage 3: apply the clean role to the destination record.
    //   filler_dst ~ dst_record XOR clean_role
    return cleanup_(bind(*dst, clean_role));
}

Glyph RelationalCrucible::build_world(std::size_t records_in_world) const {
    // Subject-keyed holographic store: bind each record to its subject
    // glyph before bundling. This makes records individually addressable
    // inside one combined glyph -- unbinding the subject key recovers
    // that record (approximately), dramatically reducing crosstalk.
    const std::size_t n = std::min(records_in_world, records_.size());
    std::vector<Glyph> recs;
    recs.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (auto g = record_glyphs_.recall(records_[i].subject)) {
            recs.push_back(bind(subject_glyph_(records_[i].subject), *g));
        }
    }
    if (recs.empty()) return Glyph::zero();
    return bundle(std::span<const Glyph>{recs.data(), recs.size()});
}

std::string RelationalCrucible::query_holographic(const Glyph& world,
                                                  const std::string& subject,
                                                  const std::string& role) const {
    // Stage 1: unbind the subject key -> approximate that subject's record.
    const Glyph rec_approx = bind(world, subject_glyph_(subject));
    // Stage 2: unbind the role from the recovered record -> filler.
    return cleanup_(bind(rec_approx, role_glyph_(role)));
}

TrialResult RelationalCrucible::trial_structured_unbind(double target) const {
    TrialResult r;
    r.name   = "structured_unbind";
    r.target = target;
    for (const auto& rec : records_) {
        for (const auto& [role, filler] : rec.fields) {
            ++r.trials;
            if (query_field(rec.subject, role) == filler) ++r.correct;
        }
    }
    r.score = r.trials ? static_cast<double>(r.correct) / static_cast<double>(r.trials) : 0.0;
    r.detail = "unbind every (subject,role) and clean up to the right filler";
    return r;
}

TrialResult RelationalCrucible::trial_analogy(double target) const {
    TrialResult r;
    r.name   = "analogy";
    r.target = target;
    // For each ordered pair of subjects sharing a role, test the analogy.
    for (std::size_t i = 0; i < records_.size(); ++i) {
        for (std::size_t j = 0; j < records_.size(); ++j) {
            if (i == j) continue;
            const auto& a = records_[i];
            const auto& b = records_[j];
            for (const auto& [role_a, filler_a] : a.fields) {
                // find same role in b
                for (const auto& [role_b, filler_b] : b.fields) {
                    if (role_a != role_b) continue;
                    ++r.trials;
                    if (analogy(a.subject, b.subject, filler_a) == filler_b) ++r.correct;
                }
            }
        }
    }
    r.score = r.trials ? static_cast<double>(r.correct) / static_cast<double>(r.trials) : 0.0;
    r.detail = "as filler is to A, recover the matching filler of B by record algebra";
    return r;
}

TrialResult RelationalCrucible::trial_holographic(std::size_t records_in_world,
                                                  double target) const {
    TrialResult r;
    r.name   = "holographic_capacity";
    r.target = target;
    const Glyph world = build_world(records_in_world);
    const std::size_t n = std::min(records_in_world, records_.size());
    for (std::size_t i = 0; i < n; ++i) {
        const auto& rec = records_[i];
        for (const auto& [role, filler] : rec.fields) {
            ++r.trials;
            if (query_holographic(world, rec.subject, role) == filler) ++r.correct;
        }
    }
    r.score = r.trials ? static_cast<double>(r.correct) / static_cast<double>(r.trials) : 0.0;
    r.detail = "pack " + std::to_string(n) + " records into one glyph, recover fields";
    return r;
}

std::vector<EvolutionStep> RelationalCrucible::evolve_structured(double target,
                                                                int max_redundancy) {
    std::vector<EvolutionStep> traj;
    int gen = 0;
    for (int r = 1; r <= max_redundancy; r += 2) {  // 1,3,5,7,9
        set_redundancy(r);
        const auto res = trial_structured_unbind(target);
        EvolutionStep step{gen++, r, res.score, ""};
        if (res.passed()) {
            step.note = "target met — encoding stabilised";
            traj.push_back(step);
            break;
        } else {
            step.note = "shortfall; escalating redundancy (failure -> study -> retry)";
            traj.push_back(step);
        }
    }
    return traj;
}

} // namespace khora::crucible
