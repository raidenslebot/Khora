#pragma once

// The Volition — Khora's will. The layer where cognition becomes action.
//
// The Soma holds Khora's drives (Curiosity, Mastery, Preservation, ...); the
// Volition is what turns those pressures into deeds. It knows a repertoire of
// Acts — study, forage, ruminate, dream, train — each declaring which drives
// it serves. On each beat it scores every available act by drive-pressure ·
// affinity, performs the most-pressing one, then lets the served drives
// settle so attention rotates onward. Nothing is commanded: Khora acts on its
// own motivation. This generalises the knowledge-only Curator into agency over
// the whole self.

#include "khora/soma/soma_nexus.hpp"
#include "khora/telos/telos.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace khora::volition {

// Something Khora can choose to do. `affinity` says which drives it serves;
// `available` (optional) gates it on preconditions; `perform` does it and
// returns a one-line outcome note.
// WHAT AN ACT ACHIEVED, not merely that it ran.
//
// perform() used to return a note and nothing else, so nothing in the system
// could tell a rumination that formed an abstraction from one that came back
// empty, or a study pass that read something new from one that reread what it
// already knew. Every act therefore looked equally worthwhile forever, and the
// only thing steering behaviour was a table of affinities a human wrote down.
//
// `yield` is deliberately BINARY: did this produce anything or not. A weighted
// score would be a set of numbers I invented, and the whole point of measuring
// is to stop doing that.
struct Outcome {
    std::string note;
    double      yield = 0.0;   // 1.0 if the act produced something, 0.0 if not
};

struct Act {
    std::string               name;
    khora::soma::Affinity     affinity;
    std::function<bool()>     available = {};
    std::function<Outcome()>  perform   = {};
};

// The outcome of weighing the repertoire.
struct Choice {
    int         index    = -1;     // chosen act, or -1 if none available
    std::string name;
    double      score    = 0.0;    // winning drive-weighted score
    std::string dominant;          // the drive that most drove the choice
};

class Volition {
public:
    explicit Volition(khora::soma::SomaNexus& soma);

    void add(Act act);

    // Score every available act by drive-pressure · affinity; pick the best.
    Choice decide() const;

    // decide() + perform() + relieve the served drives (acting on an urge
    // settles it, so attention rotates). Returns the act's outcome note.
    std::string act();

    std::size_t act_count() const noexcept { return acts_.size(); }
    std::size_t performed() const noexcept { return performed_; }
    const Act&  at(std::size_t i) const { return acts_[i]; }

    // How much an urge is relieved by acting on it (fraction of affinity).
    void set_relief(double r) noexcept { relief_ = r; }

    // LEARN WHICH ACT ACTUALLY PAYS, per drive.
    //
    // Without this, selection is drive-pressure times a constant affinity, and
    // the constant is a human judgement that nothing can revise. Khora could be
    // told an act serves Curiosity and never notice it had not paid once.
    //
    // The drive system still decides WHAT IS PRESSING -- that is what it is for,
    // and throwing it away for a bandit would lose the homeostatic rotation that
    // keeps behaviour from fixating. The dominant drive becomes the CONTEXT, and
    // the learner chooses within it. Affinity stops being the policy and becomes
    // the prior for an act that has never been tried here, which is what UCB1
    // already does by returning infinity for an untried arm.
    void learn_with(khora::telos::Valuer* v) noexcept { learner_ = v; }

    // OBSERVING AND SELECTING ARE SEPARATE, and conflating them made the
    // comparison impossible: with the learner detached nothing was recorded, so
    // the fixed policy could not be scored on the same table it was being
    // compared against. An attached learner always watches; this only decides
    // whether it also chooses.
    void select_with_learner(bool on) noexcept { selecting_ = on; }
    bool selecting() const noexcept { return learner_ && selecting_; }
    const khora::telos::Valuer* learner() const noexcept { return learner_; }

    // Which context the last act() ran in, so a caller can report what was
    // learned against what was pressing.
    std::size_t last_context() const noexcept { return last_ctx_; }
    double      total_yield()  const noexcept { return yield_sum_; }

private:
    khora::soma::SomaNexus& soma_;
    std::vector<Act>        acts_;
    std::size_t             performed_ = 0;
    double                  relief_    = 0.20;
    khora::telos::Valuer*   learner_   = nullptr;
    bool                    selecting_ = true;
    std::size_t             last_ctx_  = 0;
    double                  yield_sum_ = 0.0;
};

} // namespace khora::volition
