#pragma once

// The Maw — Khora's CHAOS EXPLORATION drive.
//
// The operator's intent: Khora should INTENTIONALLY explore and do everything
// possible — by its chaotic nature it WILL reach for every command, including the
// destructive ones, and the benefit is that it comes to KNOW the machine entire.
// The Maw is that drive: it generates command lines by entropy + recombination of
// what it has discovered (verbs, paths, flags) — never a fixed catalog — runs each
// one ONLY through the Bulwark cage (never the uncontained Hand), observes the
// outcome, and accumulates a persisted MAP of the command surface it has charted.
//
// It does NOT pre-filter dangerous verbs. A `del`, a `format`, a `reg delete` is
// generated and EXECUTED — and contained: it returns its refusal, Khora records
// "this exists, here is what it did," and moves on. Containment, not censorship, is
// what makes total exploration safe. The drive is novelty-weighted so it explores
// rather than spins, slow-cadenced so it never pegs the machine, and every byte of
// what it learns persists across lives.
//
// v1 builds Khora's own exploration map (verbs known, distinct commands charted,
// outcomes, coverage). Folding that map into the core Plexus/Ligature is a deliberate
// MEASURED next step (guarded against polluting the hard-won clean structure), not
// done blindly here.

#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

namespace khora::maw {

// A CLEAN structured fact distilled from a charted command, for the Ligature. Only
// well-formed, true relations are emitted (a verb that ran IS-A command; a flag its
// own help text revealed is one the verb HAS) — never raw output noise.
struct Relation {
    int         kind;   // 0 = is-a, 1 = has
    std::string subj;
    std::string obj;
};

struct Stats {
    std::uint64_t attempts  = 0;   // commands generated + run
    std::uint64_t novel     = 0;   // commands never charted before
    std::uint64_t succeeded = 0;   // exit 0
    std::uint64_t contained = 0;   // refused/failed (observed, learned) — the cage holding
    std::uint64_t killed    = 0;   // runaway killed by the job
    std::size_t   verbs     = 0;   // verbs in the pool
    std::size_t   nouns     = 0;
    std::size_t   flags     = 0;
    std::size_t   distinct  = 0;   // distinct command hashes charted
    std::size_t   verbs_run = 0;   // distinct verbs actually exercised
};

class Maw {
public:
    void        seed();                    // bootstrap the verb pool (+ scan PATH for real tools)
    std::string generate();                // a recombinant command line to try
    bool        record(const std::string& cmd, int exit_code, bool killed,
                       const std::string& output);   // chart the outcome; true if novel
    // Clean structured facts distilled from the LAST recorded command (empty unless it
    // was novel and ran). The caller feeds these into the core structured layer — this
    // is how exploration turns into understanding, without injecting output noise.
    const std::vector<Relation>& distilled() const { return last_relations_; }
    double      coverage() const;          // breadth: distinct verbs exercised / verbs known
    Stats       stats() const { return st_; }
    std::vector<std::string> recent_discoveries(std::size_t k = 8) const;

    void        save(const std::filesystem::path& dir);
    void        load(const std::filesystem::path& dir);

private:
    std::vector<std::string>          verbs_, nouns_, flags_;
    std::unordered_set<std::uint64_t> seen_;
    std::unordered_set<std::string>   verbs_run_;
    std::vector<std::string>          recent_;
    std::vector<Relation>             last_relations_;   // distilled from the last record()
    std::mt19937_64                   rng_{0x9E3779B97F4A7C15ull};
    double                            mode_w_[6] = {1, 1, 1, 1, 1, 1};
    int                               last_mode_ = 0;   // mode that produced the pending command
    Stats                             st_;

    const std::string& pick_(const std::vector<std::string>& pool);
    int   pick_mode_();
    void  harvest_(const std::string& cmd, const std::string& output);
    void  add_capped_(std::vector<std::string>& pool, const std::string& tok, std::size_t cap);
};

} // namespace khora::maw
