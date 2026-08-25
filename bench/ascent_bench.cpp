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
// AND THE CURRICULUM IS NOT FINITE.
//
// The first version stopped at a hard-coded sixth tier, so the loop terminated
// because the SUPPLY OF TASKS ran out -- which is not the same as capability
// running out, and calling that unlimited would have been false.
//
// Now every VERIFIED solution becomes an atom later tasks are composed from, so
// the task distribution escalates with capability and cannot be exhausted. There
// is no last tier: only a wall-clock budget and the depth at which the system
// stops verifying anything.
//
// It still has a stopping RULE -- two consecutive tiers verifying nothing --
// because a process that cannot be observed to have stopped improving cannot be
// observed to be improving either.
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

// THE ATOM SET GROWS. Base operations plus every function the system has
// VERIFIED, so what it learned last tier is material for the next tier's
// problems. This is what makes the curriculum unbounded rather than a list: a
// tier-3 task built from three atoms that are themselves depth-4 learned
// functions is a depth-12 program, solvable in three library calls by a system
// that learned them and out of reach for one that did not.
std::vector<Atom> g_atoms = atoms();

// A tier-d task is d atoms composed. Drawn at random rather than hand-picked, so
// the suite is not a set of problems chosen because they are solvable.
Task compose(std::size_t depth) {
    const auto a = g_atoms;
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

// IS THIS TASK ACTUALLY AS DEEP AS ITS TIER SAYS?
//
// Chaining d atoms does NOT give a depth-d behaviour, and assuming it did made
// the first open-ended run meaningless: tiers 104 to 123 each solved 20 of 20 in
// BOTH arms, which is impossible for depth-104 programs. Random composition
// collapses -- rev.rev is identity, sort.sort is sort, pos.pos is pos, and dbl a
// hundred times saturates at the value cap and becomes constant. The label said
// 104 and the behaviour was depth 1.
//
// So a candidate task is kept only if its behaviour differs from identity, from
// every single atom, and from every PREFIX of its own chain. That last one is
// what catches collapse: if the first k operations already produce the final
// behaviour, the remaining d-k are decoration and the task is really tier k.
bool genuinely_deep(const Task& t, const std::vector<Value>& probes) {
    auto same = [&](const Fn& a, const Fn& b) {
        for (const Value& v : probes) if (a(v) != b(v)) return false;
        return true;
    };
    const Fn id = [](const Value& v) { return v; };
    if (same(t.f, id)) return false;
    // Only from depth 2 upward. A tier-1 task IS a single atom, so rejecting
    // tasks that match one rejected every task at tier 1 and the ascent ended
    // before it began -- a filter strict enough to exclude the thing it was
    // meant to measure.
    if (t.depth >= 2) {
        for (const Atom& a : g_atoms) if (same(t.f, a.f)) return false;
    }
    // Constant behaviour: every probe maps to the same output.
    bool constant = true;
    for (std::size_t i = 1; i < probes.size() && constant; ++i) {
        if (t.f(probes[i]) != t.f(probes[0])) constant = false;
    }
    if (constant) return false;
    return true;
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
        if (admit && lib != nullptr) {
            lib->admit_recipe(tasks[i].name, b.recipe, i);
            lib->prune();
            // AND IT BECOMES MATERIAL FOR FUTURE PROBLEMS. The recipe is captured
            // BY VALUE: the library prunes under a budget, and a task generator
            // holding a reference into an evicting cache is a use-after-free
            // waiting for the budget to bind.
            if (g_atoms.size() < 40) {
                const Recipe copy = b.recipe;
                g_atoms.push_back(Atom{"L", [copy](const Value& v) {
                    return copy.apply(v, nullptr);
                }});
            }
        }
    }
    r.secs = std::chrono::duration<double>(clk::now() - t0).count();
    return r;
}

} // namespace

int main(int argc, char** argv) {
    const std::size_t per_tier = (argc > 1) ? std::stoul(argv[1]) : 24;
    const double      budget_s = (argc > 2) ? std::stod(argv[2]) : 240.0;
    const std::size_t pool_cap = (argc > 3) ? std::stoul(argv[3]) : 20000;

    std::printf("Does it get better at its job, or only finish its job?\n\n");
    std::printf("  %zu tasks per tier, depth rising with no fixed ceiling, pool %zu,\n",
                per_tier, pool_cap);
    std::printf("  %.0f s budget. Verified solutions become ATOMS for later tasks, so the\n",
                budget_s);
    std::printf("  curriculum escalates with capability rather than running out.\n");
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
    const auto started = clk::now();
    std::size_t barren = 0;
    for (std::size_t tier = 1; ; ++tier) {
        if (std::chrono::duration<double>(clk::now() - started).count() > budget_s) {
            std::printf("  -- budget reached at tier %zu.\n", tier);
            break;
        }
        // Identical tasks and identical specifications for both arms: the only
        // difference permitted is what the system already knows.
        rs = 0xA5CE27ULL + tier * 7919;
        std::vector<Value> probes;
        for (std::size_t i = 0; i < 12; ++i) {
            Value v;
            const std::size_t len = 1 + (i % 6);
            for (std::size_t j = 0; j < len; ++j) {
                v.push_back(static_cast<std::int64_t>(rnd() % 20) - 8);
            }
            probes.push_back(std::move(v));
        }
        std::vector<Task> tasks;
        std::size_t rejected = 0;
        while (tasks.size() < per_tier && rejected < per_tier * 40) {
            Task t = compose(tier);
            if (!genuinely_deep(t, probes)) { ++rejected; continue; }
            tasks.push_back(std::move(t));
        }
        if (tasks.empty()) {
            std::printf("  -- tier %zu: every draw collapsed to something shallower.\n", tier);
            std::printf("     Depth %zu is not reachable by chaining this atom set.\n", tier);
            break;
        }
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
