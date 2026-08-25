#pragma once

// TELOS — learning what actions are FOR, from what they actually returned.
//
// A capability audit of this tree found reinforcement learning entirely absent:
// no value function, no policy, no temporal-difference error, no bandit, no
// exploration rule. The word "reward" appears only in comments. What exists
// instead is the Volition, which scores every act as
//
//     drive pressure  x  affinity
//
// where affinity is a constant a human wrote down declaring which drives an act
// is supposed to serve. Khora has ninety-odd actions, a Whetstone that measures
// the yield of its own faculties, and no mechanism whatsoever connecting the
// two. It cannot notice that an act it is told serves Curiosity has never once
// paid, and it cannot discover that one it is told serves nothing is the best
// thing it does in some situation.
//
// This is the missing connection: an estimate of what each action RETURNS, per
// context, learned from observation.
//
// UCB1 RATHER THAN EPSILON-GREEDY, and the reason is not fashion. Khora acts on
// a slow loop -- seconds to minutes per act -- so it will take hundreds of
// decisions, not millions. An epsilon-greedy policy spends a fixed fraction of a
// small budget on actions it already knows are bad, forever. UCB spends its
// exploration where the UNCERTAINTY is: an untried action is infinitely
// attractive, a thoroughly-tried bad one is never picked again, and the
// exploration decays on its own as counts grow. With a budget this small that
// difference is most of the performance, and it removes a tuning parameter that
// nobody could have set honestly.
//
// CONTEXT IS A SMALL DISCRETE KEY, deliberately. The obvious next thing is to
// condition on the full continuous drive vector, and with a few hundred
// observations that would be fitting noise. A handful of contexts -- which drive
// is dominant, say -- keeps the per-cell counts high enough for the estimates to
// mean something. This is a bandit, not a policy over a state space, and calling
// it one would overstate it.
//
// WHAT IT IS NOT: there is no bootstrapping and no discounting, so no temporal
// credit assignment. An act whose payoff arrives three acts later is invisible
// to it. That is a real limit and it is the next thing this file would need.

#include <cstddef>
#include <string>
#include <vector>

namespace khora::telos {

// One learned estimate: how much this action has returned in this context.
struct Estimate {
    double      mean  = 0.0;
    std::size_t count = 0;
};

class Valuer {
public:
    Valuer() = default;
    Valuer(std::size_t contexts, std::size_t actions);

    // Grow to fit. Called implicitly by observe(); exposed because a caller that
    // knows its repertoire up front should say so rather than discover it.
    void fit(std::size_t contexts, std::size_t actions);

    // What this action returned, this time. Rewards are whatever the caller
    // measures -- Whetstone yield, a benchmark delta, a success flag -- and only
    // their ORDER matters to selection, not their scale.
    void observe(std::size_t context, std::size_t action, double reward);

    Estimate estimate(std::size_t context, std::size_t action) const;

    // UCB1: mean + c * sqrt(2 ln(N) / n). An action never tried in this context
    // returns infinity, so everything is tried once before anything is trusted.
    double confidence_bound(std::size_t context, std::size_t action,
                            double c = 1.0) const;

    // The action with the highest bound. `allowed` may be empty, meaning all.
    std::size_t choose(std::size_t context, double c = 1.0,
                       const std::vector<std::size_t>& allowed = {}) const;

    // The best action by MEAN alone -- no exploration. What the learner would do
    // if it had to stop learning now, which is the honest thing to report when
    // asked what it has concluded.
    std::size_t best(std::size_t context,
                     const std::vector<std::size_t>& allowed = {}) const;

    std::size_t contexts() const noexcept { return contexts_; }
    std::size_t actions()  const noexcept { return actions_; }
    std::size_t total(std::size_t context) const;

    bool save(const std::string& path) const;
    bool load(const std::string& path);

private:
    std::size_t contexts_ = 0;
    std::size_t actions_  = 0;
    std::vector<Estimate> cell_;      // contexts_ x actions_, row-major

    Estimate&       at(std::size_t c, std::size_t a);
    const Estimate& at(std::size_t c, std::size_t a) const;
};

} // namespace khora::telos
