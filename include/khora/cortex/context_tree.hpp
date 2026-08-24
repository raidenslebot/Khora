#pragma once

// ContextTree — the pawl.
//
// WHY THIS EXISTS, and it is not a hunch.
//
// TemporalMemory allocates a segment whenever a driven column has nothing
// primed. That is right when contexts recur, and it is why the module beats a
// pair-encoding 100% to chance on structured sequences. On prose it never
// converges: burst 0.93, 2.9M segments for 24k tokens, growth linear in the
// corpus forever.
//
// Three fixes were proposed and all three died to measurement -- a population of
// competing specialists (lost to the monolith on every arm), similarity-
// preserving input codes (3% improvement), and threshold tuning (burst flat
// across the sweep). Then the diagnostic that should have come first explained
// why all three had to fail. The share of n-word contexts that EVER recur, over
// the whole 7.66M-token corpus:
//
//     n=1  66.90%     n=4   4.92%
//     n=2  30.68%     n=5   1.76%
//     n=3  13.46%     n=8   0.32%
//
// A 319-fold increase in data moved the n=8 figure from 0.00% to 0.32%. It
// saturates. TemporalMemory keys on an 8-deep context, so its burst rate was
// never an architecture failure: there was no recurrence to detect. Distinct
// contexts per token at n=8 is 0.996 -- new contexts arrive as fast as tokens
// do, so no fixed memory holds them at any scale.
//
// The survey of prior art named the missing piece precisely: a system that only
// SPECIALISES when it fails to predict is a ratchet with no pawl. XCS bounds its
// population because a wildcard plus accuracy-based fitness creates an opposing
// GENERALISATION pressure. TemporalMemory has allocation-on-failure and nothing
// pushing back.
//
// This is the pawl. Rather than allocating against the 0.32% that does not
// recur, it BACKS OFF to the depth where recurrence actually lives, harvesting
// 66.9 / 30.7 / 13.5% instead of chasing 0.32%.
//
// AND THE OBVIOUS RULE FOR DOING THAT IS WRONG. The first version predicted
// from the longest context it had seen twice, on the assumption that deeper
// context is better context. Measured on 400,000 tokens of real books:
//
//     order 1   46.3% of predictions   14.88% accurate
//     order 2   35.0%                  13.87%
//     order 3    8.6%                  11.87%
//
// Accuracy FALLS through the region carrying 90% of the traffic. The rule was
// systematically trading a working order-1 guess for a worse deeper one, and a
// thirty-line bigram table beat the whole module 14.22% to 13.02%.
//
// So depth is not the selection criterion; measured reliability is. Each node
// carries its own hit rate, shrunk toward the rate of the shorter context it
// sits inside -- the same shape as the discounting in Kneser-Ney and
// hierarchical Pitman-Yor. A deep context has to EARN the right to override a
// shallow one that already works. After that change accuracy rises monotonically
// with the order chosen (7.8 / 12.4 / 19.8 / 26.9 / 40.0 / 85.7), which is what
// a calibrated selector looks like: when it reaches for depth, depth pays.
//
// AND THIS IS THE POPULATION IDEA, AT THE SCALE IT SHOULD HAVE BEEN.
//
// The experiment that died proposed eight competing sequence memories under a
// shared budget: organisms with heritable genomes, replication on success,
// starvation on failure. It lost to a single monolith on every arm. But the
// failure was one of GRANULARITY, not of principle -- eight organisms over
// 24,000 tokens gives each one 3,000 tokens and starves them all, which is
// exactly what c-BTM measured at industrial scale.
//
// The organisms belong here instead, and there are hundreds of thousands of
// them. Every context node IS one:
//
//     genome      the context it claims -- one specific slice of the stream
//     birth       allocation the first time that context is observed
//     fitness     `correct`, the number of predictions it actually GOT RIGHT
//     death       eviction, when the budget binds and it is the least useful
//     niche       given by the data, never evolved
//
// That fitness line took two attempts. The first version scored a node by how
// often it was CONSULTED, which is the same broken fitness the population
// experiment had: a context that is reliably wrong is consulted exactly as
// often as one that is reliably right, so the budget was being allocated by
// popularity rather than by merit. Fitness is now measured OFF-POLICY -- every
// step, every context that could have predicted is graded on whether it WOULD
// have been right, whether or not it was the one used. The whole population is
// under continuous selection, not just the incumbents.
//
// That last line is the one the prior art insisted on: fix the niches by
// construction rather than searching for them. Hash routing and balanced
// k-means beat learned routers precisely because they cannot collapse or
// imbalance, and choosing niches optimally is provably as hard as PAC-learning
// DNF. Here the corpus assigns every niche for free -- a context node's territory
// is simply the contexts that occur.
//
// So the selection pressure the population experiment tried to impose from
// outside is now internal and continuous. Contexts that predict correctly keep
// their memory; contexts that are wrong are evicted to make room. Hundreds of
// thousands of tiny specialists competing for one budget, born from the data and
// killed by being wrong, with backoff as the generalisation pressure that keeps
// the whole thing from ratcheting.
//
// The mechanics are otherwise deliberately unoriginal -- variable-order Markov
// modelling with backoff is Kneser-Ney, hierarchical Pitman-Yor and context-tree
// weighting, and this project's own trigram baseline already beat the temporal
// memory on real books for exactly that reason. What is added is the hard budget
// with accuracy-based eviction, so the model is bounded by construction instead
// of by hoping the corpus ends.
//
// WHAT IT IS WORTH, measured rather than asserted. Frozen on held-out text, all
// models trained on the same 1.8M tokens of real books:
//
//     bigram table         7.66%   (every successor kept, no ceiling, 4.9 MB)
//     ContextTree          8.45%   (hard budget of 300,000 nodes, 12.4 MB)
//
// Eight tenths of a point over a bigram is a modest win and it should be read as
// one. The claim that matters is not the margin, it is that the margin is held
// under a ceiling the baselines never have to respect, and that the depth signal
// below comes free with it.
//
// AND THE HONEST LIMIT OF THE WHOLE MODULE. Variable-order Markov modelling with
// backoff is 1990s technology and this is a careful implementation of it, not a
// new idea. What it is FOR is not competitive next-word prediction -- that
// contest was settled elsewhere and by other means. It is a bounded, calibrated,
// continuously-selected model of a symbol stream that reports honestly how well
// it knows the territory it is reading. That report is the input to something
// else. On its own it is a good component and nothing more.
//
// The biological reading, honest rather than decorative: cortex does not solve
// stability-plasticity by growing without limit. Synapse density peaks in early
// childhood and is roughly halved through adolescence, and what survives is what
// carried signal. Allocation without eviction was only ever the first half of
// the arrangement -- and selection over a huge population of tiny units, not a
// tournament between a handful of big ones, is how the tissue actually does it.

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace khora::cortex {

struct ContextTreeConfig {
    // Deepest context considered. Beyond 5 the recurrence measurement says
    // there is essentially nothing to find in English, so the default stops
    // where the data does.
    std::size_t max_order = 5;

    // A context must have been seen this many times before it is trusted to
    // predict. At 1 every novel context would "predict" its single successor,
    // which is memorisation wearing a prediction costume.
    std::uint32_t min_count = 2;

    // Hard ceiling on stored contexts. This is the bound TemporalMemory lacked.
    std::size_t max_nodes = 400000;

    // Successors kept per context. A context with many continuations keeps only
    // its commonest, which is where the probability mass is under Zipf. Bounded
    // by Space-Saving, so the true heavy hitters can always break in.
    //
    // MEASURED, and it is the knob that decides everything. Swept against
    // prior_weight on 2M tokens: prior_weight is FLAT across a 50-fold range,
    // while this one is the whole story.
    //
    //     keep  4   5.33%      keep 32   8.45%
    //     keep  8   7.40%      keep 64   8.44%
    //     keep 16   8.26%      (bigram baseline 7.66%)
    //
    // Below 32 the head of a common word's successor distribution does not fit,
    // so the top-1 estimate is corrupted for exactly the high-traffic contexts
    // that carry the score. 64 buys nothing. The memory ceiling stays hard:
    // max_nodes * (20 + 8 * max_successors) bytes, whatever the stream does.
    std::size_t max_successors = 32;

    // How much evidence a context needs before its own measured hit rate
    // outweighs the rate of the next-shorter context. This is the strength of
    // the hierarchical shrinkage that stops an untested deep context from
    // overriding a shallow one that is already working.
    double prior_weight = 20.0;

    // The rate assumed before any evidence at all exists.
    double prior_rate = 0.05;

    // The empty context is a LAST RESORT, not a competitor.
    //
    // Measured, and it is a selection-bias trap worth naming. The order-0 node's
    // hit rate is estimated over every position in the corpus, but it only ever
    // WINS the comparison on the hard ones -- the positions where no longer
    // context is reliable. Those two populations are not the same, and the
    // estimate is optimistic by a factor of five: order 0 was chosen on 26.6% of
    // predictions on the strength of a ~7% measured rate, and delivered 0.75%.
    // The bigram scored 3.47% on those exact positions.
    //
    // Any node conditioned on real context beats a node conditioned on nothing,
    // so order 0 is used only when nothing else qualifies.
    bool floor_is_last_resort = true;
};

struct Prediction {
    std::uint32_t symbol = 0;
    std::size_t   order  = 0;      // context length actually used
    double        confidence = 0.0; // share of that context's observations
    bool          known = false;    // false when even order-0 had nothing
};

class ContextTree {
public:
    explicit ContextTree(ContextTreeConfig cfg = {});

    // Feed one symbol, extending every context that ends at the previous step.
    void observe(std::uint32_t symbol);

    // Advance the history WITHOUT learning from it. This exists for honest
    // evaluation: reading held-out text as you predict it is an advantage a
    // fixed n-gram table does not get, so a head-to-head comparison has to
    // freeze the model first.
    void advance(std::uint32_t symbol);

    // Predict the next symbol from the current history, longest context first.
    Prediction predict() const;

    // Sequence boundary: forget the history, keep everything learned.
    void reset();

    std::size_t nodes()    const noexcept { return table_.size(); }
    // Counted entries, not allocator overhead, so it can be compared like for
    // like against an n-gram table: 8 bytes of context key, 12 of counters,
    // 8 per stored successor.
    std::size_t bytes() const noexcept;
    std::size_t observed() const noexcept { return observed_; }
    std::size_t evicted()  const noexcept { return evicted_; }
    // How often prediction had to fall back below max_order. A high number is
    // the model reporting that deep context is not paying, which is exactly
    // what the corpus measurement predicts for prose.
    const std::vector<std::size_t>& order_usage() const noexcept { return order_usage_; }

    const ContextTreeConfig& config() const noexcept { return cfg_; }

    // THE SIGNAL THE BURST FRACTION WAS REACHING FOR.
    //
    // TemporalMemory's bursting fraction was meant to report "how much of this
    // did I not see coming", and on prose it reported 0.93 forever because
    // nothing recurs at its depth -- an honest signal about a question with no
    // answer. Backoff depth answers the question that DOES have one: how deep a
    // regularity did this passage let me use?
    //
    // A passage predicted at order 3-5 is one whose specific phrasings this
    // model has seen. A passage that falls to order 0-1 is being read for the
    // first time. Unlike a burst rate, this is calibrated by construction --
    // the order is only used if that exact context genuinely recurred.
    //
    // Returns the mean order used since the last clear, and the fraction of
    // predictions that had to fall all the way to the unigram.
    struct Depth { double mean_order = 0.0; double floor_fraction = 0.0; std::size_t n = 0; };
    Depth depth_signal() const noexcept;
    void  clear_depth_signal() noexcept;

private:
    struct Node {
        // (symbol, count), kept sorted by count descending, capped.
        std::vector<std::pair<std::uint32_t, std::uint32_t>> succ;
        std::uint32_t total   = 0;   // observations through this context
        // Fitness, measured off-policy: every step, every context that could
        // have predicted is graded on whether it WOULD have been right --
        // whether or not it was the one consulted.
        std::uint32_t tries   = 0;
        std::uint32_t correct = 0;
    };

    ContextTreeConfig cfg_;
    std::unordered_map<std::uint64_t, Node> table_;
    std::vector<std::uint32_t> history_;
    std::size_t observed_ = 0;
    std::size_t evicted_  = 0;
    mutable std::vector<std::size_t> order_usage_;
    mutable std::size_t depth_n_ = 0;
    mutable std::size_t depth_sum_ = 0;
    mutable std::size_t depth_floor_ = 0;

    void push_history(std::uint32_t symbol);
    std::uint64_t context_key(std::size_t order) const;
    void bump(std::uint64_t key, std::uint32_t next);
    void score_candidates(std::uint32_t actual);
    const Node* select(std::size_t& order_out) const;
    void evict_if_over_budget();
};

} // namespace khora::cortex
