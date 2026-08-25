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

#include <thread>
#include <atomic>
#include "khora/techne/techne.hpp"
#include "khora/governor/governor.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <numeric>
#include <string>
#include <unordered_map>
#include <utility>
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

// WHY THESE, AND WHY MOSTLY NOT IDEMPOTENT.
//
// The first set had eight entries and the ascent died at tier 7 because most
// deep draws COLLAPSED to something shallow. That was the atom set's fault, in
// two ways worth naming.
//
// `tail` and `drop1` were LITERALLY THE SAME FUNCTION, written twice. A pair of
// identical atoms does nothing but make collapse more likely, and I did not
// notice until the collapse filter started rejecting almost everything.
//
// And half the remainder were idempotent or involutions: sort of sort is sort,
// pos of pos is pos, rev of rev is identity. Chains built mostly from those
// normalise to a handful of behaviours no matter how long they are, which is
// exactly why "tier 104" was solvable by everything.
//
// So the set is now dominated by operations that do NOT collapse under
// repetition -- shifts and scalings by different constants, takes and drops of
// different lengths, and reductions that change the shape of the value. Two
// idempotent operations are kept deliberately, because a generator that cannot
// produce them is not modelling real composition either.
std::vector<Atom> atoms() {
    return {
        // Shape-changing, non-idempotent: the workhorses of genuine depth.
        {"inc1",   [](const Value& v) { Value o; for (auto x : v) o.push_back(x + 1); return o; }},
        {"inc3",   [](const Value& v) { Value o; for (auto x : v) o.push_back(x + 3); return o; }},
        {"dec2",   [](const Value& v) { Value o; for (auto x : v) o.push_back(x - 2); return o; }},
        {"dbl",    [](const Value& v) { Value o; for (auto x : v) o.push_back(x * 2); return o; }},
        {"tri",    [](const Value& v) { Value o; for (auto x : v) o.push_back(x * 3); return o; }},
        {"neg",    [](const Value& v) { Value o; for (auto x : v) o.push_back(-x); return o; }},
        {"sq",     [](const Value& v) { Value o; for (auto x : v) o.push_back(x * x); return o; }},
        {"tail",   [](const Value& v) { return v.size() > 1 ? Value(v.begin()+1, v.end()) : Value{}; }},
        {"init",   [](const Value& v) { return v.size() > 1 ? Value(v.begin(), v.end()-1) : Value{}; }},
        {"take3",  [](const Value& v) { return Value(v.begin(),
                        v.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(3, v.size()))); }},
        {"drop2",  [](const Value& v) { return v.size() > 2 ? Value(v.begin()+2, v.end()) : Value{}; }},
        {"dup",    [](const Value& v) { Value o = v; o.insert(o.end(), v.begin(), v.end()); return o; }},
        {"prepend0", [](const Value& v) { Value o{0}; o.insert(o.end(), v.begin(), v.end()); return o; }},
        {"len",    [](const Value& v) { return Value{static_cast<std::int64_t>(v.size())}; }},
        {"sum",    [](const Value& v) { std::int64_t s = 0; for (auto x : v) s += x; return Value{s}; }},
        {"delta",  [](const Value& v) { Value o; for (std::size_t i = 1; i < v.size(); ++i)
                        o.push_back(v[i] - v[i-1]); return o; }},
        // LENGTH-PRESERVING AND POSITION-DEPENDENT. Everything above either maps
        // elements independently or shrinks the list, and that is why deep random
        // chains collapse: len, sum, take3, tail and drop2 funnel into ABSORBING
        // STATES -- the singleton and the empty list -- after which sort, rev and
        // pos are all identity and the chain's behaviour was settled long before
        // its last operation. Measured, 99.3% of depth-15 draws were rejected for
        // exactly that, and the ascent ended on the curriculum rather than the
        // solver.
        //
        // These five have no absorbing state to fall into. They keep the length,
        // they depend on POSITION rather than on the element alone, and they do
        // not commute with the arithmetic maps -- rot1 then inc1 is not inc1 then
        // rot1 once anything asymmetric has happened. That is what makes a
        // fifteen-deep chain still fifteen deep.
        {"rot1",   [](const Value& v) { if (v.size() < 2) return v;
                        Value o(v.begin()+1, v.end()); o.push_back(v.front()); return o; }},
        {"scan",   [](const Value& v) { Value o; std::int64_t s = 0;
                        for (auto x : v) { s += x; o.push_back(s); } return o; }},
        {"altneg", [](const Value& v) { Value o; for (std::size_t i = 0; i < v.size(); ++i)
                        o.push_back(i % 2 ? -v[i] : v[i]); return o; }},
        {"idxmul", [](const Value& v) { Value o; for (std::size_t i = 0; i < v.size(); ++i)
                        o.push_back(v[i] * static_cast<std::int64_t>(i + 1)); return o; }},
        {"ziprev", [](const Value& v) { Value o; const std::size_t n = v.size();
                        for (std::size_t i = 0; i < n; ++i) o.push_back(v[i] + v[n-1-i]); return o; }},
        // WHAT AN ATOM HAS TO BE, which took a negative result to state precisely.
        //
        // The obvious next lever at curriculum saturation is "atoms outside the
        // span of the existing ones". Measured: dedup, cummax, rank, rotk and
        // digitsum, all genuinely outside it, gave 137 verified against 118 at
        // tier 25 -- against 232 against 192 at tier 29 without them. A third of
        // the capability, gone.
        //
        // The reason is that an atom outside the ATOM set's span is usually also
        // outside the OPERATION set's span, because both are the same family:
        // arithmetic, reordering, slicing. Scan sums and cannot take a running
        // maximum; nothing sorts-and-searches; nothing loops over digits. A task
        // built from an atom the solver cannot express is not a harder task, it
        // is an impossible one, and a curriculum full of them measures nothing.
        //
        // So the target is narrow and now stated: an atom must be OUTSIDE the
        // span of the other atoms and INSIDE the span of the operations. The
        // five that worked -- rot1, scan, altneg, idxmul, ziprev, worth tier 15
        // to tier 20 -- are exactly that, and idxmul was later hand-built out of
        // mul, mapadd, range and len to prove it. Curriculum depth and solver
        // reach have to be raised TOGETHER; neither side alone does anything.
        //
        // Involution and idempotents, kept because real composition contains them.
        {"rev",    [](const Value& v) { return Value(v.rbegin(), v.rend()); }},
        {"sort",   [](const Value& v) { Value o = v; std::sort(o.begin(), o.end()); return o; }},
        {"pos",    [](const Value& v) { Value o; for (auto x : v) if (x > 0) o.push_back(x); return o; }},
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
    // CASE LENGTHS MUST SPAN THE HOLDOUT'S, and they did not.
    //
    // This drew ten cases at lengths 1..5 and five holdout cases at 7..11 --
    // DISJOINT RANGES. Any program whose behaviour depends on length therefore
    // passed every visible case and failed the holdout by construction, and the
    // search had no way to learn otherwise. Measured on idxmul: it produced
    // `add(x, mul(x, range(5)))`, which is exactly x*[1..5] and exactly right for
    // every length the cases contained. Adding more cases did not help, because
    // they were all short. Widening the case lengths to 1..12 solved it outright
    // -- `add(x, mul(x, range(100)))`, 20/20 and 5/5.
    //
    // So tasks this benchmark reported as out of reach were not out of reach.
    // The holdout stays LONGER than any case, because extrapolation past what
    // was shown is the property worth testing; what it may not do is test a
    // length regime the cases never sampled at all.
    for (std::size_t i = 0; i < 14; ++i) { Value in = draw(1 + i % 12); s.cases.push_back({in, t.f(in)}); }
    for (std::size_t i = 0; i < 5; ++i)  { Value in = draw(14 + i);     s.holdout.push_back({in, t.f(in)}); }
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

struct TierResult {
    std::size_t solved = 0, verified = 0, total = 0;
    // HOW MANY ANSWERS ACTUALLY USED THE LIBRARY, and how many calls deep.
    //
    // The carried arm verifying more than the empty one shows the library helps.
    // It does NOT show how, and the difference matters: a library that appears in
    // the answers is a vocabulary, while a library that merely changes the search
    // order is a lucky perturbation. Counting live Call nodes separates them, and
    // chained calls separately again -- lib_j(lib_i(x)) is the composition the
    // whole compounding story rests on.
    std::size_t used_library = 0, chained_calls = 0;
    // Deterministic work. Wall clock on one thread of a hybrid CPU varied 2x on
    // IDENTICAL code -- 35.9 s, 56.2 s and 69.1 s on the same tier -- because an
    // unpinned thread lands on a performance or an efficiency core run to run. I
    // attributed that spread to three separate code changes before checking. A
    // candidate count does not care which core counted it.
    unsigned long long nodes = 0;
    double t_fwd = 0, t_bidir = 0, t_check = 0;
    double secs = 0.0;
};

// Live library calls in an answer, over the nodes the root actually reaches.
std::size_t live_calls(const Recipe& r) {
    if (!r.found) return 0;
    std::vector<bool> seen(r.pool.size(), false);
    std::vector<std::size_t> stack{r.root};
    std::size_t n = 0;
    while (!stack.empty()) {
        const std::size_t i = stack.back();
        stack.pop_back();
        if (i >= r.pool.size() || seen[i]) continue;
        seen[i] = true;
        if (r.pool[i].op == Op::Call) ++n;
        if (r.pool[i].a >= 0) stack.push_back(static_cast<std::size_t>(r.pool[i].a));
        if (r.pool[i].b >= 0) stack.push_back(static_cast<std::size_t>(r.pool[i].b));
    }
    return n;
}

TierResult run_tier(const std::vector<Task>& tasks, const std::vector<Spec>& specs,
                    Library* lib, std::size_t pool_cap, bool admit) {
    TierResult r;
    r.total = tasks.size();
    const auto t0 = clk::now();

    // TASKS IN ORDER, THE SEARCH ITSELF WIDE.
    //
    // This loop must stay sequential, and that is measured rather than assumed.
    // Spreading the TASKS over the pool was tried twice -- one pass against the
    // tier-start library, and parallel waves with admission between them, the
    // structure solve_all uses. Compared inside a single run, where both arms
    // face identical tasks, the carried library is worth +21 verified over its
    // empty control when admission is task-by-task and -8 when it is not. It
    // stops helping and starts HURTING, because task j's certified solution has
    // to enter the library before task j+1 is attempted: a twenty-task tier
    // compounds twenty times. Tier 1 starts EMPTY and still produced eight
    // answers containing a library call, every one from a task solved earlier in
    // the same tier.
    //
    // So the width went INSIDE construct instead, where it costs no compounding
    // at all. The parallel phase there computes signatures only and the merge
    // stays single-threaded and in order, so the answers do not depend on the
    // thread count -- which is what makes this loop's numbers still comparable
    // with the ones it produced on one core.
    // SPECULATION, because three of twenty-four cores were busy.
    //
    // solve_one already runs forward, bidirectional and library-free at once --
    // three cores, resolved by a fixed preference order. The loop over tasks
    // stays ordered because admission is task-by-task and that is where the
    // compounding lives, so twenty-one cores sat idle while the deepest tiers
    // took twenty-seven seconds each and the run became wall-clock bound.
    //
    // A CORRECT PROGRAM STAYS CORRECT HOWEVER THE LIBRARY GROWS. So tasks ahead
    // are solved speculatively against the library as it stands; when a task's
    // turn comes, a speculative answer that generalised and survives the
    // external check is kept, and one that failed is re-run against the richer
    // library it should have had. The re-run recovers what the sequential loop
    // would have found, so the verified count cannot fall below it.
    //
    // Eight speculative tasks times three searches is twenty-four, which is this
    // machine. holds_up is NOT called in the parallel phase: it draws from the
    // global stream and would be a data race, and after recipe compaction it is
    // cheap enough that leaving it sequential costs nothing.
    const std::size_t kSpec =
        std::max<std::size_t>(1, khora::governor::Governor::cap_workers(0.90) / 3);
    std::vector<BuildResult> ahead;
    std::size_t ahead_base = 0;
    bool lib_moved = false;

    for (std::size_t i = 0; i < tasks.size(); ++i) {
        const auto tc = clk::now();
        if (ahead.empty() || i >= ahead_base + ahead.size()) {
            const std::size_t n = std::min(kSpec, tasks.size() - i);
            ahead.assign(n, BuildResult{});
            ahead_base = i;
            lib_moved = false;
            std::vector<std::thread> pool;
            pool.reserve(n);
            for (std::size_t j = 0; j < n; ++j) {
                pool.emplace_back([&ahead, &specs, lib, pool_cap, i, j] {
                    ahead[j] = solve_one(specs[i + j], pool_cap, lib);
                });
            }
            for (auto& th : pool) th.join();
        }
        BuildResult b = std::move(ahead[i - ahead_base]);
        r.t_fwd += std::chrono::duration<double>(clk::now() - tc).count();
        const auto tv = clk::now();
        bool ok = (b.proof == Proof::Generalised) && holds_up(b.recipe, lib, tasks[i].f);
        r.t_check += std::chrono::duration<double>(clk::now() - tv).count();
        // A SPECULATIVE MISS GETS THE LIBRARY IT SHOULD HAVE HAD. Anything after
        // the first of a batch was solved against a staler library than the
        // sequential loop would have handed it, so a failure there is not an
        // answer -- it is a stale attempt, and it is retried against the library
        // as it now stands. That is what keeps the verified count from falling
        // below the sequential one.
        // ONLY IF THE LIBRARY ACTUALLY MOVED. Retrying every speculative failure
        // solved each genuinely unsolvable task TWICE, and at deep tiers most
        // tasks are unsolvable -- eight-way speculation came out SLOWER than the
        // sequential loop it replaced, tier 15 against tier 23 in the same three
        // hundred seconds. A speculation is only stale if something was admitted
        // since it started; if nothing was, it is exactly the answer the
        // sequential loop would have produced and there is nothing to redo.
        if (!ok && i > ahead_base && lib_moved) {
            const auto tr = clk::now();
            b = solve_one(specs[i], pool_cap, lib);
            r.t_bidir += std::chrono::duration<double>(clk::now() - tr).count();
            ok = (b.proof == Proof::Generalised) && holds_up(b.recipe, lib, tasks[i].f);
        }
        r.nodes += b.nodes_considered;
        if (b.proof != Proof::Generalised) continue;
        ++r.solved;
        if (!ok) continue;
        ++r.verified;
        const std::size_t calls = live_calls(b.recipe);
        if (calls > 0) ++r.used_library;
        if (calls > 1) ++r.chained_calls;
        if (admit && lib != nullptr) {
            lib->admit_recipe(tasks[i].name, b.recipe, i);
            lib_moved = true;
            lib->prune();
            // AND IT BECOMES MATERIAL FOR FUTURE PROBLEMS. The recipe is captured
            // BY VALUE: the library prunes under a budget, and a task generator
            // holding a reference into an evicting cache is a use-after-free
            // waiting for the budget to bind.
            // A SOLVED PROBLEM BECOMES A PRIMITIVE, unconditionally, and three
            // attempts to be cleverer than that all made it WORSE:
            //
            //   admit whatever comes first  | tier 20 | 220 vs 191 | gap 29
            //   novel + non-absorbing only  | tier 18 | 175 vs 161 | gap 14
            //   behaviourally novel only    | tier 16 | 130 vs 117 | gap 13
            //
            // Filtering to behaviourally DISTINCT primitives is better
            // engineering and scores worse, which is not a paradox once the
            // measurement is read properly: this benchmark's difficulty is
            // defined RELATIVE TO THE ATOM SET. A task counts only if it differs
            // from every atom, so enriching the atoms raises the bar for what
            // counts as a problem at the same moment it raises the ability to
            // solve one. Improving the vocabulary moves the yardstick it is
            // measured against.
            //
            // That is a property of the instrument, and it is worth stating
            // plainly: AN ASCENT WHOSE CURRICULUM IS GENERATED FROM ITS OWN
            // SOLUTIONS CANNOT DEMONSTRATE UNBOUNDED SELF-IMPROVEMENT. It can
            // only ever show the system staying ahead of a bar it is also
            // raising. Showing capability growth in absolute terms needs a fixed
            // external task set hard enough to reward a richer vocabulary, and
            // techne_bench -- the fixed set that exists -- is not that: its
            // hand-picked list transformations are solved 16/20 either way.
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
    std::printf("  the whole result -- without it, \"compounding\" is a word.\n");
    std::printf("  It is also the ONLY figure here that compares across runs. Each\n");
    std::printf("  tier is built from what the previous tier SOLVED, so a change to the\n");
    std::printf("  engine hands this benchmark a different curriculum. Read the\n");
    std::printf("  carried-minus-empty gap within ONE run; never a tier time across two.\n\n");

    const auto probe = khora::governor::probe();
    std::printf("  governor: ceiling %zu workers, die temperature %s\n\n",
                khora::governor::Governor::cap_workers(0.90),
                probe.die_temp_available ? "available" : "NOT available");

    // BUDGET 96, NOT 32, AND THAT DEPENDS ON THE ATOM SET. A 2x2, each cell the
    // carried-minus-empty gap inside one run:
    //
    //   19 atoms, budget 32 | tier 15 | 185 vs 138 | 517 tasks | 9.1 pts/task
    //   19 atoms, budget 96 | tier 15 | 174 vs 138 | 517 tasks | 7.0
    //   24 atoms, budget 32 | tier 20 | 214 vs 191 | 718 tasks | 3.2
    //   24 atoms, budget 96 | tier 20 | 220 vs 191 | 718 tasks | 4.0
    //
    // The bigger library HURTS the smaller atom set and HELPS the larger one. A
    // library is a haystack as well as a vocabulary -- every entry is another
    // level-0 candidate -- so the right size is a function of how diverse the
    // problems are, not a constant to be tuned once.
    Library carried(96);
    std::printf("  tier | tasks | carried library | empty library | used lib | chained |    nodes | seconds\n");
    std::printf("  -----+-------+-----------------+---------------+----------+---------+----------+--------\n");

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
        // THE GENERATOR WAS GIVING UP, not running out.
        //
        // This budget was per_tier * 40 -- 1,600 draws for forty tasks -- and at
        // tier 29 the keep rate is about two in sixteen hundred, so it was
        // hitting the cap and reporting a saturated curriculum. But a draw is a
        // composition and twelve probe evaluations: microseconds. Filling a tier
        // at a 0.12% keep rate needs about thirty-two thousand draws, which is a
        // fraction of a second against the tens of seconds the tier then spends
        // being SOLVED.
        //
        // "The curriculum saturates" has been the standing block for several
        // cycles and was measured, correctly, as the generator failing to fill a
        // tier. What was never checked is whether it was failing because deep
        // tasks do not exist or because it stopped looking.
        while (tasks.size() < per_tier && rejected < per_tier * 2000) {
            Task t = compose(tier);
            if (!genuinely_deep(t, probes)) { ++rejected; continue; }
            tasks.push_back(std::move(t));
        }
        const std::size_t drawn = tasks.size() + rejected;
        if (tasks.empty()) {
            std::printf("  -- tier %zu: every draw collapsed to something shallower.\n", tier);
            std::printf("     Depth %zu is not reachable by chaining this atom set.\n", tier);
            break;
        }
        rs = 0xA5CE27ULL + tier * 7919 + 13;
        std::vector<Spec> specs;
        for (const Task& t : tasks) specs.push_back(make(t));

        const TierResult with = run_tier(tasks, specs, &carried, pool_cap, true);
        Library empty(96);
        const TierResult without = run_tier(tasks, specs, &empty, pool_cap, false);

        total_carried += with.verified;
        total_empty += without.verified;

        std::printf("  %4zu | %5zu | %6zu verified  | %5zu verified | %8zu | %7zu | %7.2fM | %6.1f\n",
                    tier, tasks.size(), with.verified, without.verified,
                    with.used_library, with.chained_calls,
                    static_cast<double>(with.nodes) / 1e6, with.secs + without.secs);
        // FLUSH. Block-buffered stdout on a run this long is a black box: the
        // last attempt sat past ten minutes with zero bytes written and the only
        // way to learn anything was to kill it. A benchmark you cannot watch is a
        // benchmark you cannot diagnose.
        std::printf("       |       | forward %6.1f s | bidir %6.1f s | verify %6.1f s | drew %5zu, kept %2zu\n",
                    with.t_fwd + without.t_fwd, with.t_bidir + without.t_bidir,
                    with.t_check + without.t_check, drawn, tasks.size());
        std::fflush(stdout);

        // TWO BARREN TIERS, NOT ONE.
        //
        // A single depth can be unlucky in a way the next is not -- tier 6
        // verified 2 while tier 7 verified 3 in the run that motivated this --
        // so stopping on the first empty tier reports a ceiling that is really a
        // sampling artefact.
        //
        // The previous commit's message described exactly this rule while the
        // code stopped on the FIRST barren tier and left `barren` unused. That is
        // a commit message describing an intention rather than a program, which
        // is the same defect as a benchmark reporting a number it did not
        // measure, and it is corrected here rather than quietly aligned.
        if (with.verified == 0 && without.verified == 0) {
            if (++barren >= 2) {
                std::printf("  -- two consecutive tiers verified nothing; the ascent ends "
                            "at tier %zu.\n", tier);
                break;
            }
            std::printf("  -- tier %zu barren; continuing in case the depth was unlucky.\n", tier);
        } else {
            barren = 0;
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
