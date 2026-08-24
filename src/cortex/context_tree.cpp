#include "khora/cortex/context_tree.hpp"

#include <algorithm>

namespace khora::cortex {
namespace {

// A context is identified by a hash of (order, the last `order` symbols).
// Storing the symbols themselves would cost more than the counts they key.
// Collisions merge two contexts, which is a small, bounded error in a model
// that is already approximate -- and it keeps a node to a fixed size.
inline std::uint64_t mix(std::uint64_t x) noexcept {
    x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 27; x *= 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

} // namespace

ContextTree::ContextTree(ContextTreeConfig cfg) : cfg_(cfg) {
    order_usage_.assign(cfg_.max_order + 1, 0);
}

void ContextTree::reset() { history_.clear(); }

std::size_t ContextTree::bytes() const noexcept {
    std::size_t e = 0;
    for (const auto& kv : table_) e += kv.second.succ.size();
    return table_.size() * 20 + e * 8;
}

ContextTree::Depth ContextTree::depth_signal() const noexcept {
    Depth d;
    d.n = depth_n_;
    if (depth_n_ == 0) return d;
    d.mean_order = static_cast<double>(depth_sum_) / static_cast<double>(depth_n_);
    d.floor_fraction = static_cast<double>(depth_floor_) / static_cast<double>(depth_n_);
    return d;
}

void ContextTree::clear_depth_signal() noexcept {
    depth_n_ = depth_sum_ = depth_floor_ = 0;
}

std::uint64_t ContextTree::context_key(std::size_t order) const {
    // order 0 is the empty context: the unigram distribution.
    std::uint64_t h = mix(0x9E3779B97F4A7C15ULL + order);
    for (std::size_t k = 0; k < order; ++k) {
        h = mix(h ^ history_[history_.size() - 1 - k]);
    }
    return h;
}

void ContextTree::bump(std::uint64_t key, std::uint32_t next) {
    Node& n = table_[key];
    ++n.total;
    for (auto& s : n.succ) {
        if (s.first == next) {
            ++s.second;
            // Keep the list ordered by count so predict() reads succ.front()
            // and the Space-Saving replacement below hits the true minimum.
            std::sort(n.succ.begin(), n.succ.end(),
                      [](const auto& a, const auto& b) { return a.second > b.second; });
            return;
        }
    }
    if (n.succ.size() < cfg_.max_successors) {
        n.succ.emplace_back(next, 1u);
    } else {
        // SPACE-SAVING (Metwally, Agrawal & El Abbadi 2005). Replace the
        // smallest counter and INHERIT its count, so a genuinely frequent
        // successor can always break into a full list.
        //
        // This replaced a rule that only displaced entries with count <= 1, and
        // the difference was not cosmetic. Once four slots reached count 2, that
        // rule locked the list forever against every later word however common
        // -- so the order-0 node, which sees every word in the corpus, was stuck
        // on whatever arrived first and predicted at 0.00% where the
        // most-frequent-word baseline scores 7.87%. A bounded counter set needs
        // an algorithm with a guarantee, not a plausible heuristic.
        n.succ.back() = {next, n.succ.back().second + 1};
    }
    std::sort(n.succ.begin(), n.succ.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
}

// EVERY CANDIDATE IS SCORED, EVERY STEP -- not just the one that was used.
//
// This is off-policy evaluation, and it is what makes reliability-based
// selection possible at all. If a node could only accrue evidence when it was
// selected, and selection required evidence, no deep node would ever get to
// prove itself. Instead each order that has a node records whether ITS top
// successor was the symbol that actually arrived, whether or not it was the one
// consulted. Fitness is measured for the whole population, continuously.
void ContextTree::score_candidates(std::uint32_t actual) {
    const std::size_t upto = std::min(history_.size(), cfg_.max_order);
    for (std::size_t order = 0; order <= upto; ++order) {
        const auto it = table_.find(context_key(order));
        if (it == table_.end()) continue;
        Node& n = it->second;
        if (n.succ.empty()) continue;
        ++n.tries;
        if (n.succ.front().first == actual) ++n.correct;
    }
}

// THE ORDER IS CHOSEN BY MEASURED RELIABILITY, NOT BY DEPTH.
//
// The first version of this took the longest context that had been seen twice,
// on the assumption that deeper context predicts better. Measured on 400,000
// tokens of real books, that assumption is false exactly where it matters:
//
//     order 1   46.3% of predictions   14.88% accurate
//     order 2   35.0%                  13.87%
//     order 3    8.6%                  11.87%
//
// Accuracy FALLS through the region carrying 90% of the traffic, so the rule
// was systematically replacing a working order-1 guess with a worse deeper one
// -- which is why a thirty-line bigram table beat it, 14.22% to 13.02%.
//
// So each node now carries its own hit rate, and the estimate at order k is
// shrunk toward the estimate at order k-1. That is hierarchical shrinkage, the
// same shape as the discounting in Kneser-Ney and hierarchical Pitman-Yor: a
// deeper context has to EARN the right to override a shallower one that is
// already working, and with no evidence it simply inherits its parent's rate
// and loses the tie to the simpler model.
const ContextTree::Node* ContextTree::select(std::size_t& order_out) const {
    const std::size_t upto = std::min(history_.size(), cfg_.max_order);
    const Node* best = nullptr;
    const Node* floor = nullptr;
    double best_score = -1.0;
    double prior = cfg_.prior_rate;
    std::size_t best_order = 0;
    for (std::size_t order = 0; order <= upto; ++order) {
        const auto it = table_.find(context_key(order));
        if (it == table_.end()) continue;
        const Node& n = it->second;
        if (n.total < cfg_.min_count || n.succ.empty()) continue;

        const double w = cfg_.prior_weight;
        const double score = (static_cast<double>(n.correct) + w * prior) /
                             (static_cast<double>(n.tries) + w);
        // Strictly greater, so a tie goes to the shallower context. Untested
        // depth never displaces a shallower model that is already working.
        if (score > best_score) { best_score = score; best = &n; best_order = order; }
        if (order == 0) floor = &n;
        prior = score;   // the next order down is shrunk toward this one
    }
    // The empty context wins only when it is the ONLY context. See the note on
    // floor_is_last_resort: its hit rate is measured over every position but it
    // is only ever selected on the hard ones, so the comparison it wins is one
    // it should never have been entered into.
    if (cfg_.floor_is_last_resort && best == floor && best != nullptr) {
        const Node* deeper = nullptr;
        std::size_t deeper_order = 0;
        double deeper_score = -1.0;
        double p2 = cfg_.prior_rate;
        for (std::size_t order = 0; order <= upto; ++order) {
            const auto it = table_.find(context_key(order));
            if (it == table_.end()) continue;
            const Node& n = it->second;
            if (n.total < cfg_.min_count || n.succ.empty()) continue;
            const double sc = (static_cast<double>(n.correct) + cfg_.prior_weight * p2) /
                              (static_cast<double>(n.tries) + cfg_.prior_weight);
            p2 = sc;
            if (order > 0 && sc > deeper_score) { deeper_score = sc; deeper = &n; deeper_order = order; }
        }
        if (deeper != nullptr) { best = deeper; best_order = deeper_order; }
    }
    order_out = best_order;
    return best;
}

// The budget, enforced by UTILITY rather than by age.
//
// GNG-U's lesson: evict by what a node contributes, not by how old it is. The
// first version scored utility by how often a node was CONSULTED, which is the
// same broken fitness function the population experiment had -- a node that is
// consistently wrong is consulted exactly as often as one that is always right.
// Fitness is now `correct`: predictions actually got right. A context earns its
// memory by being RIGHT, and that is a selection pressure rather than a
// popularity contest.
void ContextTree::evict_if_over_budget() {
    if (table_.size() <= cfg_.max_nodes) return;

    // Drop the least useful quarter, so eviction is amortised rather than run
    // on every insert.
    const std::size_t target = cfg_.max_nodes * 3 / 4;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> scored;  // (score, key)
    scored.reserve(table_.size());
    for (const auto& [key, n] : table_) {
        // Correct predictions dominate; evidence breaks ties, so a
        // well-attested context that has simply not been right yet is not
        // thrown away ahead of a singleton.
        const std::uint64_t score =
            (static_cast<std::uint64_t>(n.correct) << 20) |
            std::min<std::uint32_t>(n.total, 0xFFFFF);
        scored.emplace_back(score, key);
    }
    std::nth_element(scored.begin(), scored.begin() + (scored.size() - target), scored.end());
    for (std::size_t i = 0; i + target < scored.size(); ++i) {
        table_.erase(scored[i].second);
        ++evicted_;
    }
}

void ContextTree::push_history(std::uint32_t symbol) {
    history_.push_back(symbol);
    if (history_.size() > cfg_.max_order) {
        history_.erase(history_.begin());
    }
}

void ContextTree::advance(std::uint32_t symbol) { push_history(symbol); }

void ContextTree::observe(std::uint32_t symbol) {
    // Score first, against the state that existed BEFORE this symbol was
    // learned -- otherwise every node grades itself on an answer it has already
    // been told.
    score_candidates(symbol);

    // Every context ending at the previous position gains `symbol` as a
    // successor -- orders 0..max_order, which is what makes backoff possible.
    const std::size_t upto = std::min(history_.size(), cfg_.max_order);
    for (std::size_t order = 0; order <= upto; ++order) {
        bump(context_key(order), symbol);
    }
    push_history(symbol);
    ++observed_;
    evict_if_over_budget();
}

Prediction ContextTree::predict() const {
    std::size_t order = 0;
    const Node* n = select(order);
    if (n == nullptr) {
        // Nothing usable at any order: the deepest possible fall, counted.
        ++depth_n_;
        ++depth_floor_;
        return {};
    }

    Prediction p;
    p.symbol = n->succ.front().first;
    p.order  = order;
    p.confidence = static_cast<double>(n->succ.front().second) /
                   static_cast<double>(n->total);
    p.known = true;
    if (order < order_usage_.size()) ++order_usage_[order];
    ++depth_n_;
    depth_sum_ += order;
    if (order == 0) ++depth_floor_;
    return p;
}

} // namespace khora::cortex
