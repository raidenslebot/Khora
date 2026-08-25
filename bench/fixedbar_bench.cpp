// Does it get better against a bar that DOES NOT MOVE?
//
// ascent_bench cannot answer that, and the reason is structural rather than a
// defect to be fixed. It composes each tier's tasks from what it has already
// solved, and a task counts only if it differs from every atom -- so enriching
// the vocabulary raises the bar for what counts as a problem at the same moment
// it raises the ability to solve one. Measured three ways in that bench:
// admitting every solved program as a new primitive scores 220, admitting only
// behaviourally novel ones -- better engineering -- scores 130. Improving the
// vocabulary moves the yardstick it is measured against.
//
// So a self-generated curriculum can only ever show the system staying ahead of
// a bar it is also raising. It cannot show unbounded improvement, and no amount
// of work on the system will make it do so.
//
// THIS BENCH FIXES THE BAR.
//
//   - An evaluation set is drawn ONCE, from a fixed seed, over the BASE atom
//     set only. It is never regenerated, never filtered against anything the
//     system learns, and nothing from it is ever admitted to a library. It is
//     the same set of problems at every stage, forever.
//   - A disjoint training stream is drawn from a different seed each stage. The
//     system solves it and keeps what it certifies.
//   - After each stage the evaluation set is re-attempted with the library the
//     system has accumulated, admitting NOTHING. That score is the measurement.
//   - A control re-attempts the same set with an EMPTY library every stage. It
//     cannot improve, and it is what says a rising curve is not drift.
//
// WHAT IT MEASURED, 96 fixed tasks, depths 2 to 7, library budget 96:
//
//   stage |  trained | library | fixed-set score | empty-library control
//       0 |        0 |       0 |   18 of 96      |   18 of 96
//       1 |       96 |      30 |   22 of 96      |   18 of 96
//       3 |      288 |      73 |   23 of 96      |   18 of 96
//       6 |      576 |      96 |   25 of 96      |   18 of 96
//       9 |      864 |      96 |   26 of 96      |   18 of 96
//      14 |    1,344 |      96 |   26 of 96      |   18 of 96
//
// EIGHTEEN TO TWENTY-SIX ON PROBLEMS THAT NEVER CHANGED, while the control sits
// at 18 at every single stage. The whole pipeline is deterministic and the
// control proves it, so that difference is not a sample -- it is attributable to
// the library and to nothing else. This is self-improvement measured against a
// bar the system does not get to move.
//
// AND IT PLATEAUS. Flat at 26 from stage 9 while training continues. The library
// hit its budget at stage 5, so the obvious suspect is the budget -- but raising
// it to 512 reaches 27 and flattens anyway, at stage 11, with the library still
// growing (298 entries and rising). Five times the budget buys ONE more task.
//
// WHY IT PLATEAUS, which is the question this bench was built to ask properly.
// Two axes, not one. The SAME fixed 96 tasks throughout:
//
//   pool cap | no library | with a learned library
//     20,000 |     18     |  25
//     60,000 |     23     |  28 at its peak, settling 27
//    200,000 |     26     |
//
// A LEARNED LIBRARY IS WORTH ROUGHLY A TENFOLD SEARCH BUDGET: 25 with a library
// at a 20,000 pool is what raw search reaches at 200,000. If that were all, the
// library would be a compute substitute and nothing more.
//
// It is not all. At a 60,000 pool the library reaches 28, which BEATS raw search
// at 200,000 while using a third of the pool. The two axes compose, so the
// vocabulary buys something that cannot be had by handing search more room.
//
// Both plateau, and neither alone explains the ceiling -- which is why raising
// the library budget five times bought a single task. It was the wrong axis to
// push by itself.
//
// So the curve is real, it is not an artefact, and it decelerates hard. What is
// left over after the budget explanation is the honest open question here, and
// it is a better question than the one this bench was built to answer.

// If the score rises while the control stays flat, that is capability growth in
// absolute terms: the same problems, more of them solved, because of what the
// system taught itself in between. If it does not rise, the compounding the
// ascent reports is an artefact of its moving yardstick -- and this bench is the
// thing entitled to say so.

#include "khora/techne/techne.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace khora::techne;
using clk = std::chrono::steady_clock;

namespace {

using Fn = std::function<Value(const Value&)>;
struct Atom { const char* name; Fn f; };
struct Task { std::string name; Fn f; std::size_t depth; };

std::uint64_t rs = 88172645463325252ULL;
std::uint64_t rnd() {
    rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs;
}

// THE BASE SET, FIXED. Evaluation tasks are composed from exactly these and
// nothing else, which is what keeps the bar constant while the system changes.
std::vector<Atom> base_atoms() {
    return {
        {"inc1",   [](const Value& v) { Value o; for (auto x : v) o.push_back(x + 1); return o; }},
        {"inc3",   [](const Value& v) { Value o; for (auto x : v) o.push_back(x + 3); return o; }},
        {"dec2",   [](const Value& v) { Value o; for (auto x : v) o.push_back(x - 2); return o; }},
        {"dbl",    [](const Value& v) { Value o; for (auto x : v) o.push_back(x * 2); return o; }},
        {"tri",    [](const Value& v) { Value o; for (auto x : v) o.push_back(x * 3); return o; }},
        {"neg",    [](const Value& v) { Value o; for (auto x : v) o.push_back(-x); return o; }},
        {"sq",     [](const Value& v) { Value o; for (auto x : v) o.push_back(x * x); return o; }},
        {"tail",   [](const Value& v) { return v.size() > 1 ? Value(v.begin()+1, v.end()) : Value{}; }},
        {"init",   [](const Value& v) { return v.size() > 1 ? Value(v.begin(), v.end()-1) : Value{}; }},
        {"dup",    [](const Value& v) { Value o = v; o.insert(o.end(), v.begin(), v.end()); return o; }},
        {"prepend0", [](const Value& v) { Value o{0}; o.insert(o.end(), v.begin(), v.end()); return o; }},
        {"delta",  [](const Value& v) { Value o; for (std::size_t i = 1; i < v.size(); ++i)
                        o.push_back(v[i] - v[i-1]); return o; }},
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
        {"rev",    [](const Value& v) { return Value(v.rbegin(), v.rend()); }},
        {"sort",   [](const Value& v) { Value o = v; std::sort(o.begin(), o.end()); return o; }},
        {"pos",    [](const Value& v) { Value o; for (auto x : v) if (x > 0) o.push_back(x); return o; }},
    };
}

const std::vector<Atom> g_base = base_atoms();

Task compose(std::size_t depth) {
    std::vector<std::size_t> pick;
    std::string name;
    for (std::size_t i = 0; i < depth; ++i) {
        const std::size_t k = rnd() % g_base.size();
        pick.push_back(k);
        name += (i ? "." : "");
        name += g_base[k].name;
    }
    Fn f = [pick](const Value& in) {
        Value v = in;
        for (const std::size_t k : pick) v = g_base[k].f(v);
        return v;
    };
    return Task{name, f, depth};
}

// The collapse filter, against the BASE atoms only. It never consults anything
// learned, so a task admitted to the evaluation set stays admitted.
bool genuinely_deep(const Task& t, const std::vector<Value>& probes) {
    auto same = [&](const Fn& a, const Fn& b) {
        for (const Value& v : probes) if (a(v) != b(v)) return false;
        return true;
    };
    const Fn id = [](const Value& v) { return v; };
    if (same(t.f, id)) return false;
    if (t.depth >= 2) {
        for (const Atom& a : g_base) if (same(t.f, a.f)) return false;
    }
    return true;
}

Spec make(const Task& t) {
    Spec s;
    s.name = t.name;
    auto draw = [&](std::size_t len) {
        Value v;
        for (std::size_t i = 0; i < len; ++i) v.push_back(static_cast<std::int64_t>(rnd() % 24) - 10);
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

// An independent check on fresh inputs. A certificate is the system judging
// itself, and on the evaluation set that is exactly the thing not to trust.
bool holds_up(const Recipe& r, const Library* lib, const Fn& ref) {
    for (std::size_t k = 0; k < 200; ++k) {
        Value in;
        const std::size_t len = rnd() % 12;
        for (std::size_t j = 0; j < len; ++j) in.push_back(static_cast<std::int64_t>(rnd() % 40) - 18);
        if (r.apply(in, lib) != ref(in)) return false;
    }
    return true;
}

std::vector<Value> make_probes() {
    std::vector<Value> probes;
    for (std::size_t i = 0; i < 12; ++i) {
        Value v;
        const std::size_t len = 1 + (i % 6);
        for (std::size_t j = 0; j < len; ++j) v.push_back(static_cast<std::int64_t>(rnd() % 20) - 8);
        probes.push_back(std::move(v));
    }
    return probes;
}

struct Drawn { std::vector<Task> tasks; std::vector<Spec> specs; };

Drawn draw_set(std::uint64_t seed, std::size_t per_depth, std::size_t lo, std::size_t hi) {
    rs = seed;
    const std::vector<Value> probes = make_probes();
    Drawn d;
    for (std::size_t depth = lo; depth <= hi; ++depth) {
        std::size_t kept = 0, rejected = 0;
        while (kept < per_depth && rejected < per_depth * 60) {
            Task t = compose(depth);
            if (!genuinely_deep(t, probes)) { ++rejected; continue; }
            d.tasks.push_back(std::move(t));
            ++kept;
        }
    }
    for (const Task& t : d.tasks) d.specs.push_back(make(t));
    return d;
}

// Attempt every task. `lib` is READ ONLY here -- nothing is admitted, which is
// what makes this a measurement rather than another round of training.
// Per-depth, because "26 of 96" cannot distinguish a DEPTH WALL from a set of
// tasks that are simply not expressible in the operation set. If the shallow
// depths are solved and the deep ones are not, the ceiling is search reach; if
// the miss rate is flat across depths, it is expressibility, and no amount of
// pool or vocabulary will touch it.
std::size_t score(const Drawn& d, const Library* lib, std::size_t pool_cap,
                  std::vector<std::size_t>* by_depth = nullptr,
                  std::vector<char>* solved = nullptr) {
    // THE VERIFIER'S INPUTS ARE FIXED TOO, and that is not a detail. holds_up
    // draws from the same global stream that task generation mutates, so
    // without this the probes a task is checked against differ from stage to
    // stage -- and "solved" would quietly mean something slightly different at
    // every measurement, on a bench whose entire purpose is that nothing about
    // the measurement moves.
    std::size_t n = 0;
    for (std::size_t i = 0; i < d.tasks.size(); ++i) {
        BuildResult b = construct(d.specs[i], pool_cap, lib);
        if (b.proof != Proof::Generalised) {
            BuildResult alt = construct_bidir(d.specs[i], pool_cap, lib);
            if (alt.proof == Proof::Generalised) b = std::move(alt);
        }
        if (b.proof != Proof::Generalised) continue;
        // RESEEDED PER TASK, not per call. Seeding once at the top of score()
        // was not enough: holds_up only runs for tasks that were SOLVED, so the
        // stream advances a different number of times under a different library
        // and task i is checked against different probes in each configuration.
        // A task must be checked against the same inputs every time or "solved"
        // is not comparable between the arms this bench exists to compare.
        rs = 0x5C012ULL + i * 7919ULL;
        if (!holds_up(b.recipe, lib, d.tasks[i].f)) continue;
        ++n;
        if (solved != nullptr && i < solved->size()) (*solved)[i] = 1;
        if (by_depth != nullptr) {
            if (by_depth->size() <= d.tasks[i].depth) by_depth->resize(d.tasks[i].depth + 1, 0);
            ++(*by_depth)[d.tasks[i].depth];
        }
    }
    return n;
}

} // namespace

int main(int argc, char** argv) {
    const std::size_t per_depth = (argc > 1) ? std::stoul(argv[1]) : 8;
    const std::size_t stages    = (argc > 2) ? std::stoul(argv[2]) : 6;
    const std::size_t pool_cap  = (argc > 3) ? std::stoul(argv[3]) : 20000;
    const std::size_t budget    = (argc > 4) ? std::stoul(argv[4]) : 96;

    std::printf("Does it get better against a bar that does not move?\n\n");
    std::printf("  An evaluation set drawn ONCE from a fixed seed over the base atoms,\n");
    std::printf("  depths 2 to 7, %zu per depth. Never regenerated, never filtered against\n", per_depth);
    std::printf("  anything learned, and nothing from it is ever admitted to a library.\n");
    std::printf("  The same problems at every stage.\n\n");
    std::printf("  Between stages the system trains on a DISJOINT stream and keeps what it\n");
    std::printf("  certifies. The control re-attempts the same evaluation set with an empty\n");
    std::printf("  library every time, so it cannot improve -- it is what says a rising\n");
    std::printf("  curve is capability and not drift.\n\n");

    const Drawn eval = draw_set(0xE7A1u, per_depth, 2, 7);
    std::printf("  evaluation set: %zu tasks, fixed for the whole run\n", eval.tasks.size());
    // The scores mean nothing without these two, and they turned out to be the
    // two axes the ceiling sits on.
    std::printf("  pool cap %zu, library budget %zu\n\n", pool_cap, budget);

    Library learned(budget);
    const auto t0 = clk::now();

    std::printf("  stage | trained on | library | fixed-set score | empty-library control\n");
    std::printf("  ------+------------+---------+-----------------+----------------------\n");

    const std::size_t base = score(eval, nullptr, pool_cap);
    std::printf("  %5d | %10d | %7d | %4zu of %-4zu    | %4zu of %-4zu\n",
                0, 0, 0, base, eval.tasks.size(), base, eval.tasks.size());
    std::fflush(stdout);

    std::size_t trained = 0, best = base;
    for (std::size_t s = 1; s <= stages; ++s) {
        // TRAIN. A fresh, disjoint stream each stage, so the system keeps
        // meeting problems it has not seen rather than re-solving one set.
        const Drawn train = draw_set(0x7A17u + s * 104729u, per_depth, 2, 7);
        for (std::size_t i = 0; i < train.tasks.size(); ++i) {
            BuildResult b = construct(train.specs[i], pool_cap, &learned);
            if (b.proof != Proof::Generalised) {
                BuildResult alt = construct_bidir(train.specs[i], pool_cap, &learned);
                if (alt.proof == Proof::Generalised) b = std::move(alt);
            }
            if (b.proof != Proof::Generalised) continue;
            if (!holds_up(b.recipe, &learned, train.tasks[i].f)) continue;
            learned.admit_recipe(train.tasks[i].name, b.recipe, i);
            learned.prune();
        }
        trained += train.tasks.size();

        const std::size_t got  = score(eval, &learned, pool_cap);
        Library none(budget);
        const std::size_t ctrl = score(eval, &none, pool_cap);
        best = std::max(best, got);
        std::printf("  %5zu | %10zu | %7zu | %4zu of %-4zu    | %4zu of %-4zu\n",
                    s, trained, learned.size(), got, eval.tasks.size(), ctrl, eval.tasks.size());
        std::fflush(stdout);
    }

    const double secs = std::chrono::duration<double>(clk::now() - t0).count();
    std::vector<std::size_t> got_by_depth, base_by_depth;
    std::vector<char> hit(eval.tasks.size(), 0);
    const std::size_t final_score = score(eval, &learned, pool_cap, &got_by_depth, &hit);
    std::vector<char> hit0(eval.tasks.size(), 0);
    score(eval, nullptr, pool_cap, &base_by_depth, &hit0);

    // WHERE THE MISSES ARE.
    std::vector<std::size_t> total_by_depth;
    for (const Task& t : eval.tasks) {
        if (total_by_depth.size() <= t.depth) total_by_depth.resize(t.depth + 1, 0);
        ++total_by_depth[t.depth];
    }
    std::printf("\n  depth | tasks | solved at the start | solved at the end\n");
    std::printf("  ------+-------+---------------------+-------------------\n");
    for (std::size_t dpt = 0; dpt < total_by_depth.size(); ++dpt) {
        if (total_by_depth[dpt] == 0) continue;
        const std::size_t b0 = dpt < base_by_depth.size() ? base_by_depth[dpt] : 0;
        const std::size_t b1 = dpt < got_by_depth.size()  ? got_by_depth[dpt]  : 0;
        std::printf("  %5zu | %5zu | %9zu           | %9zu\n",
                    dpt, total_by_depth[dpt], b0, b1);
    }

    // WHICH ONES. Four cycles of guessing which atom blocks the shallow misses
    // ended with adding Op::Scan -- the exact inverse of an operation already
    // present, matching an atom by name -- and changing nothing at all. Printing
    // the names costs one line and settles it.
    // LOST BY LEARNING. Same task, solvable with no library and not solvable
    // with one. Every entry is another level-0 candidate, so a big library can
    // push a two-operation answer out of a bounded pool -- a regression the
    // totals cannot show, because it is masked by the tasks learning WINS.
    {
        std::size_t lost = 0, gained = 0;
        for (std::size_t i = 0; i < eval.tasks.size(); ++i) {
            if (hit0[i] && !hit[i]) { ++lost;
                std::printf("    LOST    d%zu  %s\n", eval.tasks[i].depth, eval.tasks[i].name.c_str()); }
            if (!hit0[i] && hit[i]) ++gained;
        }
        std::printf("  %zu tasks LOST to the library, %zu gained.\n", lost, gained);
    }
    std::printf("\n  UNSOLVED at depth 2 and 3, the ones no search depth explains:\n");
    for (std::size_t i = 0; i < eval.tasks.size(); ++i) {
        if (hit[i] || eval.tasks[i].depth > 3) continue;
        std::printf("    d%zu  %s\n", eval.tasks[i].depth, eval.tasks[i].name.c_str());
    }

    std::printf("\n  %.1f s\n\n", secs);
    if (final_score > base) {
        std::printf("  ROSE: %zu of %zu against %zu at the start, on problems that never\n",
                    final_score, eval.tasks.size(), base);
        std::printf("  changed. The system solved things it could not solve before, and the\n");
        std::printf("  only difference is what it taught itself on a disjoint stream in\n");
        std::printf("  between. That is capability growth measured in absolute terms.\n");
    } else if (final_score == base) {
        std::printf("  FLAT: %zu of %zu at the start and at the end. Everything the system\n", base);
        std::printf("  learned bought nothing on problems it had not already solved. The\n");
        std::printf("  compounding an ascent reports is then a property of its moving\n");
        std::printf("  yardstick rather than of the system, which is exactly what this bench\n");
        std::printf("  exists to be able to say.\n");
    } else {
        std::printf("  FELL: %zu of %zu against %zu at the start. A learned library made the\n",
                    final_score, eval.tasks.size(), base);
        std::printf("  same problems HARDER -- every entry is another level-0 candidate, and\n");
        std::printf("  a vocabulary that does not pay for its search cost is a haystack.\n");
    }
    if (best > final_score) {
        std::printf("\n  Peak was %zu at an earlier stage: the curve is not monotonic, so the\n", best);
        std::printf("  library has a useful size and this run went past it.\n");
    }
    return 0;
}
