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

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace khora::volition {

// Something Khora can choose to do. `affinity` says which drives it serves;
// `available` (optional) gates it on preconditions; `perform` does it and
// returns a one-line outcome note.
struct Act {
    std::string                  name;
    khora::soma::Affinity        affinity;
    std::function<bool()>        available = {};
    std::function<std::string()> perform   = {};
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

private:
    khora::soma::SomaNexus& soma_;
    std::vector<Act>        acts_;
    std::size_t             performed_ = 0;
    double                  relief_    = 0.20;
};

} // namespace khora::volition
