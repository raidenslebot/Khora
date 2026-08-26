#include "khora/telos/telos.hpp"

#include <cmath>
#include <fstream>
#include <limits>

namespace khora::telos {

namespace {
constexpr Estimate kEmpty{};
}

Valuer::Valuer(std::size_t contexts, std::size_t actions) { fit(contexts, actions); }

void Valuer::fit(std::size_t contexts, std::size_t actions) {
    if (contexts <= contexts_ && actions <= actions_) return;
    const std::size_t nc = std::max(contexts, contexts_);
    const std::size_t na = std::max(actions, actions_);
    // Re-index rather than resize: the table is row-major, so growing the row
    // width moves every element. Getting this wrong would silently attribute one
    // action's history to another, which is the kind of defect that looks like
    // bad learning rather than a bug.
    std::vector<Estimate> grown(nc * na);
    for (std::size_t c = 0; c < contexts_; ++c)
        for (std::size_t a = 0; a < actions_; ++a)
            grown[c * na + a] = cell_[c * actions_ + a];
    cell_ = std::move(grown);
    contexts_ = nc;
    actions_  = na;
}

Estimate& Valuer::at(std::size_t c, std::size_t a) { return cell_[c * actions_ + a]; }
const Estimate& Valuer::at(std::size_t c, std::size_t a) const {
    return cell_[c * actions_ + a];
}

void Valuer::observe(std::size_t context, std::size_t action, double reward) {
    fit(context + 1, action + 1);
    Estimate& e = at(context, action);
    // Incremental mean. Storing a running sum instead would drift for long-lived
    // agents, and this thing is meant to run for the life of the process.
    ++e.count;
    e.mean += (reward - e.mean) / static_cast<double>(e.count);
}

Estimate Valuer::estimate(std::size_t context, std::size_t action) const {
    if (context >= contexts_ || action >= actions_) return kEmpty;
    return at(context, action);
}

std::size_t Valuer::total(std::size_t context) const {
    if (context >= contexts_) return 0;
    std::size_t n = 0;
    for (std::size_t a = 0; a < actions_; ++a) n += at(context, a).count;
    return n;
}

double Valuer::confidence_bound(std::size_t context, std::size_t action,
                                double c) const {
    if (context >= contexts_ || action >= actions_) {
        return std::numeric_limits<double>::infinity();
    }
    const Estimate& e = at(context, action);
    // NEVER TRIED IS INFINITELY ATTRACTIVE. This is the whole reason to prefer
    // UCB here: with a few hundred decisions to spend, an action that has never
    // been attempted must be attempted, and no exploration RATE gets that right
    // without also wasting pulls on actions already known to be poor.
    if (e.count == 0) return std::numeric_limits<double>::infinity();
    const std::size_t n = total(context);
    if (n == 0) return std::numeric_limits<double>::infinity();
    return e.mean + c * std::sqrt(2.0 * std::log(static_cast<double>(n)) /
                                  static_cast<double>(e.count));
}

namespace {

// TIES WERE GOING TO THE LOWEST INDEX, AND THAT IS A POLICY.
//
// Both selectors used a strict >, so among equal candidates the first one
// registered always won. That is invisible until you look for it and then it is
// everywhere: in a gridworld bench the bandit's learned policy came out
// IDENTICAL TO AN "ALWAYS GO UP" CONTROL to three decimal places, because 96% of
// its states ended with the top two arms numerically tied and "up" was action
// zero. In Volition the same rule means "whichever act was registered first",
// silently, whenever two acts have paid equally -- which with a binary yield is
// most of the time.
//
// The fix is reservoir sampling over the tied set: the k-th candidate that ties
// the leader replaces it with probability 1/k, which is uniform over all ties.
//
// The randomness is DERIVED, not drawn. A mutable RNG in a const method would be
// a data race waiting to happen and would make two identical queries disagree;
// hashing (context, action, k, how much evidence this context has) gives a
// choice that is uniform across ties, stable for a given state of knowledge, and
// free of any dependence on registration order.
std::uint64_t mix(std::uint64_t x) {
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

// True with probability 1/k, deterministically for a given (ctx, a, k, n).
bool take_tie(std::size_t ctx, std::size_t a, std::size_t k, std::size_t n,
              std::uint64_t salt) {
    if (k <= 1) return true;
    const std::uint64_t h = mix(0x9E3779B97F4A7C15ULL ^ salt * 0xD6E8FEB86659FD93ULL
                                ^ (std::uint64_t)ctx * 0x100000001B3ULL
                                ^ (std::uint64_t)a * 0x9E3779B1ULL
                                ^ (std::uint64_t)n * 0x85EBCA77ULL
                                ^ (std::uint64_t)k);
    return (h % (std::uint64_t)k) == 0;
}

} // namespace

std::size_t Valuer::choose(std::size_t context, double c,
                           const std::vector<std::size_t>& allowed) const {
    const std::size_t n = actions_ == 0 ? 1 : actions_;
    std::size_t best_a = allowed.empty() ? 0 : allowed.front();
    double best_v = -std::numeric_limits<double>::infinity();
    std::size_t ties = 0;
    const std::size_t seen = total(context);
    auto consider = [&](std::size_t a) {
        const double v = confidence_bound(context, a, c);
        if (v > best_v) { best_v = v; best_a = a; ties = 1; return; }
        // Equal bounds are the common case, not the rare one: with an untried
        // arm scoring infinity, EVERY untried arm ties, and after learning any
        // two arms with the same mean and the same count do too.
        if (v == best_v) { ++ties; if (take_tie(context, a, ties, seen, salt_)) best_a = a; }
    };
    if (allowed.empty()) { for (std::size_t a = 0; a < n; ++a) consider(a); }
    else                 { for (std::size_t a : allowed) consider(a); }
    return best_a;
}

std::size_t Valuer::best(std::size_t context,
                         const std::vector<std::size_t>& allowed) const {
    const std::size_t n = actions_ == 0 ? 1 : actions_;
    std::size_t best_a = allowed.empty() ? 0 : allowed.front();
    double best_v = -std::numeric_limits<double>::infinity();
    std::size_t ties = 0;
    const std::size_t seen = total(context);
    auto consider = [&](std::size_t a) {
        const Estimate e = estimate(context, a);
        // Unvisited actions have no evidence, so they are not candidates for a
        // question that asks what the learner has CONCLUDED.
        if (e.count == 0) return;
        if (e.mean > best_v) { best_v = e.mean; best_a = a; ties = 1; return; }
        if (e.mean == best_v) { ++ties; if (take_tie(context, a, ties, seen, salt_)) best_a = a; }
    };
    if (allowed.empty()) { for (std::size_t a = 0; a < n; ++a) consider(a); }
    else                 { for (std::size_t a : allowed) consider(a); }
    return best_a;
}

bool Valuer::save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << "khora-telos 1\n" << contexts_ << ' ' << actions_ << '\n';
    for (std::size_t c = 0; c < contexts_; ++c) {
        for (std::size_t a = 0; a < actions_; ++a) {
            const Estimate& e = at(c, a);
            if (e.count == 0) continue;           // the table is mostly empty
            f << c << ' ' << a << ' ' << e.count << ' ' << e.mean << '\n';
        }
    }
    f << "end\n";
    return static_cast<bool>(f);
}

bool Valuer::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::string magic; int version = 0;
    f >> magic >> version;
    if (magic != "khora-telos" || version != 1) return false;
    std::size_t nc = 0, na = 0;
    if (!(f >> nc >> na)) return false;
    contexts_ = 0; actions_ = 0; cell_.clear();
    fit(nc, na);
    std::string tok;
    while (f >> tok) {
        if (tok == "end") return true;
        std::size_t c = 0, a = 0, n = 0; double m = 0;
        try { c = static_cast<std::size_t>(std::stoull(tok)); } catch (...) { return false; }
        if (!(f >> a >> n >> m)) return false;
        if (c >= contexts_ || a >= actions_) return false;
        at(c, a).count = n;
        at(c, a).mean  = m;
    }
    return false;   // ran out before "end": the file is truncated
}

} // namespace khora::telos
