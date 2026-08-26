// Khora choosing what to do, and learning that some of it was never worth doing.
//
// Volition has been the layer where cognition becomes action since it was
// written and has never had a test. It scored every act as drive-pressure times
// an AFFINITY -- a constant a human wrote down -- so Khora could be told an act
// serves Curiosity and never notice it had not paid once. On the live system
// that cost eight acts in forty: `dream` was chosen four times per twenty and
// retained nothing every time, while `study` was never chosen at all.
//
// The repertoire here is synthetic so the right answer is known in advance: two
// acts that always produce something, one that never does, and one that only
// pays under a particular drive. A policy that cannot learn takes the same
// wrong act forever; a policy that can should stop, without being told which one
// is which.

#include "khora/volition/volition.hpp"
#include "khora/telos/telos.hpp"

#include <cstdio>
#include <string>

using namespace khora::volition;
using khora::soma::Drive;
using khora::soma::kDriveCount;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("  FAIL: %s\n", what); ++failures; }
    else       { std::printf("  ok  : %s\n", what); }
}

constexpr std::size_t D(Drive d) { return static_cast<std::size_t>(d); }

// One act, with a fixed truth about whether it produces anything.
Act make(const char* name, Drive serves, double affinity, double truth) {
    Act a;
    a.name = name;
    a.affinity.per_drive[D(serves)] = affinity;
    a.perform = [truth]() -> Outcome { return {"", truth}; };
    return a;
}

double run(Volition& v, int beats) {
    const double before = v.total_yield();
    for (int i = 0; i < beats; ++i) v.act();
    return v.total_yield() - before;
}

} // namespace

int main() {
    std::printf("Volition — choosing, and learning what the choice was worth\n\n");

    // --- IT CHOOSES AT ALL ---------------------------------------------------
    {
        khora::soma::SomaNexus soma;
        Volition v(soma);
        check(v.decide().index < 0, "an empty repertoire chooses nothing");
        v.add(make("useless", Drive::Curiosity, 0.1, 0.0));
        v.add(make("useful",  Drive::Curiosity, 1.0, 1.0));
        const auto c = v.decide();
        check(c.index >= 0, "with acts available it chooses one");
        check(c.name == "useful", "and the higher affinity under the same drive wins");
    }

    // --- AN UNAVAILABLE ACT IS NOT CHOSEN ------------------------------------
    {
        khora::soma::SomaNexus soma;
        Volition v(soma);
        Act gated = make("gated", Drive::Curiosity, 10.0, 1.0);
        gated.available = []() { return false; };
        v.add(std::move(gated));
        v.add(make("open", Drive::Curiosity, 0.1, 1.0));
        check(v.decide().name == "open",
              "a precondition that fails removes the act however attractive");
    }

    // --- THE FIXED POLICY CANNOT NOTICE -------------------------------------
    //
    // "decoy" has the strongest affinity and produces nothing. Nothing in the
    // scoring can see that, so it is chosen forever.
    {
        khora::soma::SomaNexus soma;
        Volition v(soma);
        v.add(make("decoy",  Drive::Curiosity, 1.0, 0.0));
        v.add(make("worker", Drive::Curiosity, 0.9, 1.0));
        khora::telos::Valuer watch(kDriveCount, v.act_count());
        v.learn_with(&watch);
        v.select_with_learner(false);        // observe, do not choose
        const double got = run(v, 40);
        check(got == 0.0,
              "the fixed policy takes the strongest affinity 40 times and gains nothing");
        check(v.selecting() == false, "and it really was the fixed policy");
    }

    // --- THE LEARNER STOPS ---------------------------------------------------
    {
        khora::soma::SomaNexus soma;
        Volition v(soma);
        v.add(make("decoy",  Drive::Curiosity, 1.0, 0.0));
        v.add(make("worker", Drive::Curiosity, 0.9, 1.0));
        khora::telos::Valuer learned(kDriveCount, v.act_count());
        v.learn_with(&learned);

        const double early = run(v, 10);
        const double late  = run(v, 40);
        std::printf("      yield: %.0f of the first 10 beats, %.0f of the next 40\n",
                    early, late);
        check(late / 40.0 > early / 10.0,
              "the learner does better later than earlier, which is what learning is");
        check(late / 40.0 > 0.8,
              "and it converges on the act that pays, having been told nothing");
        // UCB1 keeps a little exploration forever, on purpose: an act that starts
        // paying later has to be able to be noticed.
        check(late < 40.0, "while still trying the other one occasionally");
    }

    // --- CONTEXT IS THE DOMINANT DRIVE, AND IT MATTERS -----------------------
    //
    // The drive system decides WHAT IS PRESSING and the learner chooses within
    // it. A single-context bandit would average the two situations together and
    // learn the wrong thing in both.
    {
        khora::soma::SomaNexus soma;
        Volition v(soma);
        v.add(make("a", Drive::Curiosity, 0.5, 1.0));
        v.add(make("b", Drive::Curiosity, 0.5, 0.0));
        khora::telos::Valuer learned(kDriveCount, v.act_count());
        v.learn_with(&learned);
        run(v, 30);
        const std::size_t ctx = v.last_context();
        check(ctx < kDriveCount, "the context is a drive index");
        check(learned.total(ctx) > 0, "and observations were recorded against it");
        check(learned.best(ctx) == 0, "the act that pays is the one it would now take");
    }

    std::printf("\n");
    if (failures == 0) std::printf("ALL PASS\n");
    else               std::printf("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
