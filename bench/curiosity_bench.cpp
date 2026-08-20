// CLOSING THE LOOP: does Khora learn more when it chooses what to look at?
//
// Every capability built so far is a store that answers when asked. The
// temporal memory produces a measured ignorance signal -- the fraction of
// columns that burst -- and nothing consumes it. The Soma holds drives that
// decay to fixed setpoints and adapt from nothing. The Bulwark proves
// containment at tier 2 and gates effectors that stay off. Four pieces of a
// control loop, no wires between them.
//
// That loop is what nervous tissue actually IS. Not sparsity, not sequence
// memory -- those are implementation. A nervous system is the part of an animal
// where prediction error turns into action, action turns into experience, and
// experience reduces the error. It is also the one thing a language model
// structurally cannot have: no persistent state across time, no drive, no way
// to go and find out.
//
// THE EXPERIMENT, and it is designed so that the obvious answer LOSES.
//
// A world of regions. Most are learnable -- structured sequences that become
// predictable with exposure. One is pure noise: every visit is freshly random
// and it can never be learned, so its surprise stays pinned at maximum forever.
//
// Khora's Soma already encodes a policy for this, in its own header:
//
//     Curiosity,   // novelty-seeking; spike on unfamiliar input
//
// That is greedy novelty-seeking, and against a noise source it is a trap: the
// most surprising thing in the world is the thing that can never be learned, so
// an agent that chases surprise will stare at static forever while the
// learnable world goes unvisited. This is Oudeyer & Kaplan's result, and the
// question here is whether Khora's stated design walks into it.
//
// The alternative is to chase LEARNING PROGRESS -- not how surprising a region
// is, but how fast its surprise is FALLING. Noise has high surprise and zero
// progress, so it is correctly ignored. Something already mastered has low
// surprise and zero progress, so it is also ignored. What gets attention is
// what is currently yielding.
//
// Four policies, identical budgets, identical worlds, reported together with
// two dumb baselines because a self-designed benchmark without them is worth
// nothing:
//
//   round-robin      no choice at all
//   random           choice without information
//   greedy novelty   Khora's stated design
//   learning progress
//
// The score is the mean surprise remaining across the LEARNABLE regions after
// the budget is spent. Lower means more was learned from the same experience.
//
// ============================================================================
// RESULT: BOTH INFORMED POLICIES LOSE, AT EVERY SIGNAL-TO-NOISE RATIO TESTED.
// ============================================================================
//
//   signal   round-robin  random  greedy novelty  learning progress
//     96%       0.4427    0.4605      1.0000           0.7669
//     75%       0.4292    0.4021      1.0000           0.7333
//     50%       0.4531    0.4156      1.0000           0.7500
//     25%       0.4500    0.4344      1.0000           0.7250
//
// Greedy novelty scores 1.0000 everywhere -- it learns literally nothing,
// spending 92-98% of its budget on static and starving every learnable region.
// That is Khora's own stated design, in the Soma header: "Curiosity,
// novelty-seeking; spike on unfamiliar input". It is a trap and it is written
// into the architecture.
//
// Learning progress avoids starving anything but still loses to doing nothing
// clever, and still burns 69-92% of its budget on noise. The reason is subtler
// than the greedy trap and worth stating: a region whose surprise is CONSTANT
// BUT NOISY produces apparent progress by measurement variance alone. Taking an
// argmax over thirty-two regions systematically selects whichever estimator is
// noisiest, so progress-seeking does not escape the noise trap -- it just needs
// more variance to fall into it. Fixing that means comparing progress against
// its own uncertainty rather than raw, which is a bandit problem, not a tweak.
//
// AND A WARNING ABOUT THIS BENCHMARK ITSELF. It gave three different answers
// across three bugs in it: an EMA-based progress estimator that lagged so badly
// the EASIEST regions looked most promising; an optimism rule that could not
// distinguish "nothing left here" from "never looked"; and a bootstrap in which
// every region looks equally surprising on first contact, so the argmax is
// decided by array order -- moving the noise region from index 31 to index 0
// took greedy novelty from 10% wasted to 92%. A benchmark that sensitive is
// measuring the implementation's accidents, not the system's capability.
//
// Kept as a record of a negative result. The mechanism is NOT wired into Khora.

#include "khora/cortex/temporal_memory.hpp"
#include "khora/lattice/sdr.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace khora::cortex;
using khora::lattice::Sdr;

namespace {

std::uint64_t rs = 0xC0510517ULL;
std::uint64_t rnd() {
    std::uint64_t z = (rs += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// SCARCITY AND A DIFFICULTY GRADIENT, both of which the first version lacked.
//
// With 8 equally easy regions and 400 steps, exhaustive coverage learns
// everything and choosing cannot possibly help -- round-robin and random both
// scored a perfect 0.0000, which says nothing about attention and everything
// about the world being small. A policy is only worth having when the budget
// cannot cover the world.
//
// Real worlds also vary in how much there is to learn, and that gradient is
// what a curriculum exploits: some regions are exhausted in two visits, some
// repay a dozen, one repays nothing ever.
constexpr int kRegions   = 32;
constexpr int kSeqLen    = 5;
constexpr int kBudget    = 400;    // ~12 per region if spread evenly: scarce

// HOW MUCH OF THE WORLD IS UNLEARNABLE.
//
// With one noise region in thirty-two, uniform coverage wastes 3% of its budget
// and wins outright -- round-robin 0.3871 against learning progress 0.4341. That
// is not evidence that choosing does not help; it is evidence that a world which
// is 97% signal does not need choosing. Sweeping this is the actual experiment,
// because the honest question is not "does attention beat coverage" but "at what
// signal-to-noise ratio does it start to".
//
// Regions are noise if their index is below this count.
int g_noise_regions = 1;
bool is_noise(int r) { return r < g_noise_regions; }

// Sequences per region, cycling 1 / 2 / 4 / 12: trivial to substantial.
int seqs_in(int r) {
    static const int ladder[4] = {1, 2, 4, 12};
    return ladder[r % 4];
}

// A region is a small fixed repertoire of sequences over its own symbols --
// except the noise region, which generates fresh symbols every single visit and
// therefore can never become predictable.
struct World {
    std::vector<std::vector<std::vector<Sdr>>> seqs;   // region -> sequence -> step
    std::uint64_t noise_counter = 0;

    World() {
        seqs.resize(kRegions);
        for (int r = 0; r < kRegions; ++r) {
            if (is_noise(r)) continue;
            for (int s = 0; s < seqs_in(r); ++s) {
                std::vector<Sdr> seq;
                for (int i = 0; i < kSeqLen; ++i) {
                    seq.push_back(Sdr::from_hash("r" + std::to_string(r) +
                                                 "s" + std::to_string(s) +
                                                 "i" + std::to_string(i)));
                }
                seqs[r].push_back(std::move(seq));
            }
        }
    }

    std::vector<Sdr> sample(int region) {
        if (is_noise(region)) {
            std::vector<Sdr> seq;
            for (int i = 0; i < kSeqLen; ++i) {
                    seq.push_back(Sdr::from_hash("noise" + std::to_string(noise_counter++)));
            }
            return seq;
        }
        return seqs[region][static_cast<std::size_t>(rnd() % seqs[region].size())];
    }
};

// Present one sequence, return the mean surprise over its predictable steps.
// The first step of a sequence cannot be predicted from anything, so it is not
// evidence about what the system knows.
double visit(TemporalMemory& tm, World& w, int region, bool learn) {
    const auto seq = w.sample(region);
    tm.reset();
    double sum = 0.0;
    int n = 0;
    for (std::size_t i = 0; i < seq.size(); ++i) {
        const auto st = tm.compute(seq[i], learn);
        if (i > 0) { sum += st.anomaly; ++n; }
    }
    return n ? sum / n : 0.0;
}

enum class Policy { RoundRobin, Random, GreedyNovelty, LearningProgress };
const char* policy_name(Policy p) {
    switch (p) {
        case Policy::RoundRobin:       return "round-robin";
        case Policy::Random:           return "random";
        case Policy::GreedyNovelty:    return "greedy novelty";
        case Policy::LearningProgress: return "learning progress";
    }
    return "?";
}

struct Result {
    double learnable_surprise = 0.0;   // what remains unlearned, the score
    double noise_share = 0.0;          // fraction of the budget wasted on static
    std::vector<int> visits;
};

Result run(Policy policy, std::uint64_t seed) {
    rs = seed;
    World w;
    TemporalMemory tm(TemporalMemoryConfig::semantic());

    // Learning progress is measured over the last four VISITS to a region --
    // the older pair against the newer pair -- not as the difference between a
    // fast and a slow exponential average.
    //
    // The EMA form was tried first and is a lagging indicator with a long tail:
    // a region learned completely on its second visit keeps reporting progress
    // for many steps afterwards while the slow average is still catching up. The
    // effect is backwards from what is wanted -- the EASIEST regions looked the
    // most promising for the longest, and attention concentrated on them
    // (regions of difficulty 1 and 2 took 114 and 118 of 400 steps). A window
    // over actual visits has no tail: once surprise stops falling, progress is
    // zero immediately.
    std::vector<std::vector<double>> history(kRegions);
    std::vector<double> fast(kRegions, 1.0);       // latest surprise, for greedy
    std::vector<int>    visits(kRegions, 0);

    const auto progress_of = [&](int r) -> double {
        const auto& h = history[r];
        if (h.size() < 4) return 0.0;
        const std::size_t n = h.size();
        const double older = (h[n - 4] + h[n - 3]) / 2.0;
        const double newer = (h[n - 2] + h[n - 1]) / 2.0;
        return older - newer;
    };

    for (int step = 0; step < kBudget; ++step) {
        int choice = 0;
        // Every policy visits each region once first. Without it the informed
        // policies would be choosing on priors rather than evidence, which
        // would be a rigged comparison.
        if (step < kRegions) {
            choice = step;
        } else {
            switch (policy) {
                case Policy::RoundRobin:
                    choice = step % kRegions;
                    break;
                case Policy::Random:
                    choice = static_cast<int>(rnd() % kRegions);
                    break;
                case Policy::GreedyNovelty: {
                    double best = -1.0;
                    for (int r = 0; r < kRegions; ++r)
                        if (fast[r] > best) { best = fast[r]; choice = r; }
                    break;
                }
                case Policy::LearningProgress: {
                    // OPTIMISM UNDER UNCERTAINTY. A region visited once has
                    // fast == slow, so its measured progress is exactly zero --
                    // indistinguishable from a region that has been exhausted.
                    // Scoring unknown as zero is why the first version locked
                    // onto four regions and never visited the rest: it could not
                    // tell "nothing left here" from "never looked". An unvisited
                    // region is therefore credited with the best progress seen
                    // anywhere so far, and has to earn its way down.
                    double best_seen = 0.0;
                    for (int r = 0; r < kRegions; ++r)
                        best_seen = std::max(best_seen, progress_of(r));

                    double best = -1e9;
                    for (int r = 0; r < kRegions; ++r) {
                        // A region with too few visits has no measurable
                        // progress, which is indistinguishable from having none
                        // left. Credit it with the best seen anywhere so it has
                        // to earn its way down rather than never being tried.
                        const double progress = (visits[r] < 4)
                                                    ? best_seen + 1e-3
                                                    : progress_of(r);
                        if (progress > best) { best = progress; choice = r; }
                    }
                    // Nothing is yielding anywhere: fall back to whatever is
                    // still least known rather than freezing.
                    if (best <= 1e-4) {
                        double worst = -1.0;
                        for (int r = 0; r < kRegions; ++r)
                            if (fast[r] > worst) { worst = fast[r]; choice = r; }
                    }
                    break;
                }
            }
        }

        const double a = visit(tm, w, choice, true);
        fast[choice] = a;
        history[choice].push_back(a);
        ++visits[choice];
    }

    // Score by measuring every learnable region, with learning off, on the same
    // number of held-out presentations. What the agent chose to look at does not
    // change how it is graded.
    Result res;
    res.visits = visits;
    double total = 0.0;
    int counted = 0;
    for (int r = 0; r < kRegions; ++r) {
        if (is_noise(r)) continue;
        for (int t = 0; t < 8; ++t) { total += visit(tm, w, r, false); ++counted; }
    }
    res.learnable_surprise = counted ? total / counted : 0.0;
    int on_noise = 0;
    for (int r = 0; r < kRegions; ++r) if (is_noise(r)) on_noise += visits[r];
    res.noise_share = static_cast<double>(on_noise) / kBudget;
    return res;
}

} // namespace

int main() {
    std::printf("Closing the loop: at what signal-to-noise does choosing pay?\n");
    std::printf("  %d regions, %d attention steps, 5 seeds. Lower surprise is better.\n",
                kRegions, kBudget);

    for (const int noise_regions : {1, 8, 16, 24}) {
        g_noise_regions = noise_regions;
        const int learnable = kRegions - noise_regions;
        std::printf("\n  %d of %d regions unlearnable  (%d%% of the world is signal)\n",
                    noise_regions, kRegions, 100 * learnable / kRegions);
        std::printf("  policy             | surprise left | budget on noise | starved\n");
        std::printf("  -------------------+---------------+-----------------+---------\n");

        double best_score = 1e9;
        const char* winner = "";
        for (const Policy p : {Policy::RoundRobin, Policy::Random,
                               Policy::GreedyNovelty, Policy::LearningProgress}) {
            double surprise = 0.0, noise = 0.0;
            std::vector<int> visits(kRegions, 0);
            const int seeds = 5;
            for (int s = 0; s < seeds; ++s) {
                const Result r = run(p, 0xC0510517ULL + s * 7919);
                surprise += r.learnable_surprise;
                noise    += r.noise_share;
                for (int i = 0; i < kRegions; ++i) visits[i] += r.visits[i];
            }
            surprise /= seeds;
            int starved = 0;
            for (int i = 0; i < kRegions; ++i)
                if (!is_noise(i) && visits[i] <= seeds * 2) ++starved;
            std::printf("  %-18s |    %.4f     |     %5.1f%%      |  %2d/%d\n",
                        policy_name(p), surprise, 100.0 * noise / seeds,
                        starved, learnable);
            if (surprise < best_score) { best_score = surprise; winner = policy_name(p); }
        }
        std::printf("      -> best: %s\n", winner);
    }

    std::printf("\n  surprise left = mean bursting over the LEARNABLE regions, measured\n"
                "  with learning off, so what the agent chose to look at does not\n"
                "  change how it is graded.\n");
    return 0;
}
