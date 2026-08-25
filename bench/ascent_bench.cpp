// DOES IT GET BETTER AT ITS JOB, OR ONLY FINISH ITS JOB?
//
// Everything measured so far is capability at a fixed difficulty. Solving 173 of
// 300 tasks says what the system can do; it says nothing about whether doing it
// makes the next thing easier. That is the difference between a tool and
// something that improves.
//
// So: tasks in TIERS by composition depth. Tier 1 is one operation, tier 2 is two
// composed, and so on. The library persists across tiers, and the question is
// whether tier N+1 becomes reachable because tier N was solved.
//
// THE CONTROL IS THE WHOLE EXPERIMENT. Each tier is run TWICE from identical
// specifications: once with the library carried forward from every earlier tier,
// once with an empty library. If the carried run does no better, the library is
// storage rather than learning, and the word "compounding" has been doing work
// the measurements do not support.
//
// IT TERMINATES, and that is a feature rather than a shortfall. The loop stops
// when a tier certifies nothing new, because nothing changed and so nothing can.
// An improvement loop that provably halts is the only kind that can be left
// running unattended; one that cannot halt is not unlimited, it is unfalsifiable.
//
// Everything is verified against the reference on inputs the search never saw,
// because a tier that certifies a wrong program has not learned anything and
// would poison every tier after it.

#include "khora/techne/techne.hpp"
#include "khora/governor/governor.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <numeric>
#include <string>
#include <vector>

using namespace khora::techne;
using clk = std::chrono::steady_clock;

namespace {

std::uint64_t rs = 0xA5CE27ULL;
std::uint64_t rnd() {
    std::uint64_t z = (rs += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

using Fn = std::function<Value(const Value&)>;

// The atoms tasks are composed from. Each is one operation deep, so a task built
// by chaining k of them has a known solution depth of k -- which is what makes
// "tier" mean something rather than being a label.
struct Atom { const char* name; Fn f; };

std::vector<Atom> atoms() {
    return {
        {"rev",    [](const Value& v) { return Value(v.rbegin(), v.rend()); }},
        {"sort",   [](const Value& v) { Value o = v; std::sort(o.begin(), o.end()); return o; }},
        {"tail",   [](const Value& v) { return v.size() > 1 ? Value(v.begin()+1, v.end()) : Value{}; }},
        {"dbl",    [](const Value& v) { Value o; for (auto x : v) o.push_back(x * 2); return o; }},
        {"inc3",   [](const Value& v) { Value o; for (auto x : v) o.push_back(x + 3); return o; }},
        {"pos",    [](const Value& v) { Value o; for (auto x : v) if (x > 0) o.push_back(x); return o; }},
        {"sq",     [](const Value& v) { Value o; for (auto x : v) o.push_back(x * x); return o; }},
        {"drop1",  [](const Value& v) { return v.size() > 1 ? Value(v.begin()+1, v.end()) : Value{}; }},
    };
}

struct Task {
    std::string name;
    Fn f;
    std::size_t depth;
};

// A tier-d task is d atoms composed. Drawn at random rather than hand-picked, so
// the suite is not a set of problems chosen because they are solvable.
Task compose(std::size_t depth) {
    const auto a = atoms();
    std::vector<std::size_t> pick;
    std::string name;
    for (std::size_t i = 0; i < depth; ++i) {
        const std::size_t k = rnd() % a.size();
        pick.push_back(k);
        name += (i ? "." : "");
        name += a[k].name;
    }
    Fn f = [pick, a](const Value& in) {
        Value v = in;
        for (const std::size_t k : pick) v = a[k].f(v);
        return v;
    };
    return Task{name, f, depth};
}

Spec make(const Task& t) {
    Spec s;
    s.name = t.name;
    auto draw = [&](std::size_t len) {
        Value v;
        for (std::size_t i = 0; i < len; ++i) {
            v.push_back(static_cast<std::int64_t>(rnd() % 24) - 10);
        }
        return v;
    };
    for (std::size_t i = 0; i < 10; ++i) { Value in = draw(1 + i % 5); s.cases.push_back({in, t.f(in)}); }
    for (std::size_t i = 0; i < 5; ++i)  { Value in = draw(7 + i);     s.holdout.push_back({in, t.f(in)}); }
    return s;
}

// An independent check on fresh inputs, because a certificate is the system
// judging itself and a wrong program admitted to the library corrupts every
// later tier.
bool holds_up(const Recipe& r, const Library* lib, const Fn& ref) {
    for (std::size_t k = 0; k < 200; ++k) {
        Value in;
        const std::size_t len = rnd() % 12;
        for (std::size_t j = 0; j < len; ++j) {
            in.push_back(static_cast<std::int64_t>(rnd() % 40) - 18);
        }
        if (r.apply(in, lib) != ref(in)) return false;
    }
    return true;
}

struct TierResult { std::size_t solved = 0, verified = 0, total = 0; double secs = 0.0; };

TierResult run_tier(const std::vector<Task>& tasks, const std::vector<Spec>& specs,
                    Library* lib, std::size_t pool_cap, bool admit) {
    TierResult r;
    r.total = tasks.size();
    const auto t0 = clk::now();
    for (std::size_t i = 0; i < tasks.size(); ++i) {
        BuildResult b = construct(specs[i], pool_cap, lib);
        if (b.proof != Proof::Generalised) {
            // The residue gets the expensive engine, which is where it earns its
            // place: at depth 4 forward search solved 0 of 3 and bidirectional
            // solved 3 of 3.
            BuildResult d = construct_bidir(specs[i], pool_cap, lib);
            if (d.proof == Proof::Generalised) b = std::move(d);
        }
        if (b.proof != Proof::Generalised) continue;
        ++r.solved;
        if (!holds_up(b.recipe, lib, tasks[i].f)) continue;
        ++r.verified;
        if (admit && lib != nullptr) { lib->admit_recipe(tasks[i].name, b.recipe, i); lib->prune(); }
    }
    r.secs = std::chrono::duration<double>(clk::now() - t0).count();
    return r;
}

} // namespace

int main(int argc, char** argv) {
    const std::size_t per_tier = (argc > 1) ? std::stoul(argv[1]) : 24;
    const std::size_t max_tier = (argc > 2) ? std::stoul(argv[2]) : 6;
    const std::size_t pool_cap = (argc > 3) ? std::stoul(argv[3]) : 20000;

    std::printf("Does it get better at its job, or only finish its job?\n\n");
    std::printf("  %zu tasks per tier, tiers 1..%zu by composition depth, pool %zu.\n",
                per_tier, max_tier, pool_cap);
    std::printf("  Each tier is run TWICE from identical specifications: once with the\n");
    std::printf("  library carried from every earlier tier, once from empty. The gap is\n");
    std::printf("  the whole result -- without it, \"compounding\" is a word.\n\n");

    const auto probe = khora::governor::probe();
    std::printf("  governor: ceiling %zu workers, die temperature %s\n\n",
                khora::governor::Governor::cap_workers(0.90),
                probe.die_temp_available ? "available" : "NOT available");

    Library carried(32);
    std::printf("  tier | tasks | carried library | empty library | library | seconds\n");
    std::printf("  -----+-------+-----------------+---------------+---------+--------\n");

    std::size_t total_carried = 0, total_empty = 0;
    for (std::size_t tier = 1; tier <= max_tier; ++tier) {
        // Identical tasks and identical specifications for both arms: the only
        // difference permitted is what the system already knows.
        rs = 0xA5CE27ULL + tier * 7919;
        std::vector<Task> tasks;
        for (std::size_t i = 0; i < per_tier; ++i) tasks.push_back(compose(tier));
        rs = 0xA5CE27ULL + tier * 7919 + 13;
        std::vector<Spec> specs;
        for (const Task& t : tasks) specs.push_back(make(t));

        const TierResult with = run_tier(tasks, specs, &carried, pool_cap, true);
        Library empty(32);
        const TierResult without = run_tier(tasks, specs, &empty, pool_cap, false);

        total_carried += with.verified;
        total_empty += without.verified;

        std::printf("  %4zu | %5zu | %6zu verified  | %5zu verified | %7zu | %6.1f\n",
                    tier, tasks.size(), with.verified, without.verified,
                    carried.size(), with.secs + without.secs);

        // FIXPOINT. A tier that verifies nothing new cannot be followed by one
        // that does, because the library did not change and the tasks only got
        // harder. Stopping there is the honest end of the ascent, not a give-up.
        if (with.verified == 0 && without.verified == 0) {
            std::printf("  -- tier %zu verified nothing in either arm; the ascent ends here.\n", tier);
            break;
        }
    }

    std::printf("\n  TOTAL verified: %zu carrying the library, %zu from empty each time\n",
                total_carried, total_empty);
    if (total_carried > total_empty) {
        std::printf("  The library is worth %zu additional verified programs across the\n",
                    total_carried - total_empty);
        std::printf("  ascent. Solving problems made later problems solvable, which is the\n");
        std::printf("  only sense in which this system improves itself that can be measured.\n");
    } else if (total_carried == total_empty) {
        std::printf("  IDENTICAL. The library changed nothing: every task that was solvable\n");
        std::printf("  with everything learned was solvable without it. That is storage, not\n");
        std::printf("  learning, and it is the honest reading rather than a hedge.\n");
    } else {
        std::printf("  WORSE WITH THE LIBRARY, by %zu. A library that makes the search harder\n",
                    total_empty - total_carried);
        std::printf("  is a haystack: more level-0 entries mean a quadratically larger first\n");
        std::printf("  level, and the budget goes on candidates nobody needed.\n");
    }
    std::printf("\n  library holds %zu learned functions, %zu evicted.\n",
                carried.size(), carried.evicted());
    return 0;
}
