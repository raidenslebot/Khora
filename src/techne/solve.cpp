// Solving to fixpoint â€” the loop in which the system improves itself.
//
// One pass over a task suite certifies whatever the current library can reach.
// Every certified solution enters that library, so the NEXT pass reaches
// further. Repeat until a whole round certifies nothing new.
//
// Two properties make this worth having rather than merely more compute:
//
//   It removes order dependence. In a single pass, a task that is trivial once
//   some component exists will fail if it happens to be attempted first. That is
//   an artefact of scheduling, not a statement about what is solvable, and
//   iterating removes it entirely.
//
//   It terminates. A round that certifies nothing new cannot be followed by one
//   that does, because nothing changed between them. An unbounded improvement
//   loop that provably stops is the only kind that can be run unattended.
//
// The library is shared across every worker as an immutable snapshot behind an
// atomic pointer. Readers take one atomic load and never block. That design
// replaced a reader-writer lock which, measured, cost 10.6x: holding a shared
// lock for the whole of construct meant a single writer stalled every worker,
// giving 1.20x scaling on 24 threads where copy-on-write gives 12.95x.

#include "khora/techne/techne.hpp"
#include "khora/governor/governor.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace khora::techne {
namespace {

using clk = std::chrono::high_resolution_clock;

struct Snapshot {
    std::atomic<std::shared_ptr<const Library>> cur;
    std::mutex writers;                 // serialises writers only; readers never wait

    explicit Snapshot(std::size_t budget) {
        cur.store(std::make_shared<const Library>(budget));
    }
    std::shared_ptr<const Library> read() const { return cur.load(); }

    void admit(const std::string& name, const Recipe& r, std::size_t task) {
        std::lock_guard<std::mutex> g(writers);
        auto next = std::make_shared<Library>(*cur.load());
        next->admit_recipe(name, r, task);
        next->prune();
        cur.store(std::shared_ptr<const Library>(next));
    }
};

// Library primitives inside the answer, over the nodes the root actually
// reaches. Zero across a whole run means later solutions are not built on
// earlier ones, and the whole mechanism is decoration.
std::size_t calls_in(const Recipe& r) {
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

} // namespace

std::size_t worker_threads(double fraction) {
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    if (fraction <= 0.0) return 1;
    if (fraction >= 1.0) return hw;
    // Round DOWN, then keep at least one worker. Rounding up on a 4-core machine
    // would hand back 3 and call it 75% while leaving one core to run an
    // operating system on, which is the case where headroom matters most.
    const std::size_t n = static_cast<std::size_t>(static_cast<double>(hw) * fraction);
    return std::max<std::size_t>(1, n);
}

std::vector<BuildResult> solve_all(const std::vector<Spec>& specs,
                                   SolveConfig cfg, SolveStats* stats) {
    const auto t0 = clk::now();
    std::vector<BuildResult> out(specs.size());

    // GOVERNED, not merely capped.
    //
    // A fixed thread count is a guess about how hot the machine will get. The
    // governor measures instead: it starts at the ceiling and pulls workers back
    // when the firmware reports thermal limiting or a temperature crosses the
    // limit, then lets them return as things cool. Workers PARK rather than
    // exit, so recovery costs nothing.
    //
    // Threads are still spawned at the ceiling -- parking a thread is cheap and
    // respawning one is not, so the pool is sized once and its ACTIVE width is
    // what varies.
    khora::governor::Governor gov;
    gov.start();
    const std::size_t threads = cfg.threads > 0 ? cfg.threads
                                                : khora::governor::Governor::cap_workers(0.90);

    Snapshot lib(cfg.lib_budget);

    // Tasks still without a certificate. A task leaves this list only when it
    // is GENERALISED -- a merely tested result is a program that passes the
    // visible cases and is wrong everywhere they did not look, and admitting one
    // would poison every later search with a primitive that lies.
    std::vector<std::size_t> pending(specs.size());
    for (std::size_t i = 0; i < specs.size(); ++i) pending[i] = i;

    std::atomic<std::size_t> nodes{0};
    double pool_cap = static_cast<double>(cfg.pool_cap);

    for (std::size_t round = 0; round < cfg.max_rounds && !pending.empty(); ++round) {
        std::atomic<std::size_t> cursor{0};
        std::atomic<std::size_t> solved{0};
        std::vector<std::vector<std::size_t>> still(threads);

        auto worker = [&](std::size_t slot) {
            for (;;) {
                // Park before claiming work, never in the middle of it: a task
                // half-done by a parked worker is a task nobody finishes.
                while (gov.park_if_over(slot)) {
                    if (cursor.load(std::memory_order_relaxed) >= pending.size()) return;
                }
                const std::size_t k = cursor.fetch_add(1, std::memory_order_relaxed);
                if (k >= pending.size()) break;
                const std::size_t i = pending[k];

                auto snap = lib.read();      // one atomic load, no lock held

                // FORWARD FIRST, THEN BOTH ENDS. Measured on tasks graded by the
                // depth of their known solution: bidirectional is slightly WORSE
                // at depth 1-2 (~0.9x, goal-pool overhead) and decisive at depth
                // -- max_minus_min falls from 219,799 nodes to 4,001, and at
                // depth 4 forward search solves 0 of 3 where bidirectional solves
                // 3 of 3.
                //
                // So the policy follows the measurement rather than picking a
                // winner: the cheap engine runs first, and the expensive one is
                // spent only on the residue that survived it. Round 0 is where
                // the shallow tasks fall, so the switch happens after it.
                BuildResult b = construct(specs[i], static_cast<std::size_t>(pool_cap),
                                          snap.get());
                if (b.proof != Proof::Generalised && round > 0) {
                    BuildResult d = construct_bidir(specs[i],
                                                    static_cast<std::size_t>(pool_cap),
                                                    snap.get());
                    nodes.fetch_add(d.nodes_considered, std::memory_order_relaxed);
                    if (d.proof == Proof::Generalised ||
                        d.cases_passed > b.cases_passed) {
                        b = std::move(d);
                    }
                }
                // AND WITHOUT THE LIBRARY, if both of those failed.
                //
                // A library is a vocabulary and a haystack at once: every entry
                // is another level-0 candidate competing for a bounded pool.
                // Measured on a fixed ninety-six task bar, that cost three tasks
                // that were solvable with no library at all, and adding this
                // fallback there turned eight-gained-three-lost into
                // sixteen-gained-none-lost. It existed as construct_best and
                // this -- the production solver, the one the throughput
                // benchmark actually runs -- was not using it.
                //
                // The second search runs only for tasks that already failed both
                // engines, which are the ones with budget to spare.
                if (b.proof != Proof::Generalised && snap.get() != nullptr &&
                    snap.get()->size() > 0) {
                    BuildResult bare = construct(specs[i],
                                                 static_cast<std::size_t>(pool_cap), nullptr);
                    nodes.fetch_add(bare.nodes_considered, std::memory_order_relaxed);
                    if (bare.proof == Proof::Generalised ||
                        bare.cases_passed > b.cases_passed) {
                        b = std::move(bare);
                    }
                }
                nodes.fetch_add(b.nodes_considered, std::memory_order_relaxed);

                if (b.proof == Proof::Generalised) {
                    lib.admit(specs[i].name, b.recipe, i);
                    solved.fetch_add(1, std::memory_order_relaxed);
                    out[i] = std::move(b);
                } else {
                    // Keep the best-so-far so a merely tested result is still
                    // reported rather than silently discarded.
                    if (b.cases_passed > out[i].cases_passed) out[i] = std::move(b);
                    still[slot].push_back(i);
                }
            }
        };

        std::vector<std::thread> pool;
        pool.reserve(threads);
        for (std::size_t t = 0; t < threads; ++t) pool.emplace_back(worker, t);
        for (auto& th : pool) th.join();

        const std::size_t got = solved.load();
        if (stats) stats->solved_per_round.push_back(got);
        if (stats) stats->rounds = round + 1;

        pending.clear();
        for (const auto& v : still) pending.insert(pending.end(), v.begin(), v.end());

        // FIXPOINT. Nothing changed, so nothing can change.
        if (got == 0) break;

        // A richer library reaches further per unit of budget, so later rounds
        // are worth deepening. This is bounded by max_rounds and by the fixpoint
        // above, not left to grow without limit.
        pool_cap *= cfg.deepen;
    }

    gov.stop();
    if (stats) {
        stats->thermal_peak_c = gov.peak_celsius();
        stats->min_workers = gov.min_allowed();
        stats->throttle_events = gov.throttle_events();
        stats->attempted = specs.size();
        stats->nodes = nodes.load();
        for (const BuildResult& b : out) {
            if (b.proof == Proof::Generalised) {
                ++stats->certified;
                stats->library_calls += calls_in(b.recipe);
            } else if (b.proof == Proof::Tested) {
                ++stats->memorised;
            }
        }
        stats->library_size = lib.read()->size();
        stats->seconds = std::chrono::duration<double>(clk::now() - t0).count();
    }
    return out;
}

// ---------------------------------------------------------------------------
// Counterexample-guided synthesis
// ---------------------------------------------------------------------------

BuildResult synthesise_verified(Spec spec, std::size_t pool_cap,
                                const Oracle& oracle, const Prober& prober,
                                std::size_t probes, std::size_t rounds,
                                const Library* lib, Verification* out) {
    Verification v;
    BuildResult best;

    // Each counterexample makes the problem strictly harder -- one more
    // constraint the answer must satisfy -- so a fixed budget that sufficed for
    // the first round will not suffice for the fourth. Measured without this,
    // refinement averaged 0.62 rounds against a ceiling of 6: the loop was not
    // giving up because it had verified anything, it was giving up because the
    // re-search could not fit in the same pool.
    double cap = static_cast<double>(pool_cap);

    for (std::size_t round = 0; round <= rounds; ++round) {
        v.rounds = round;
        // Cheap first here too. Mining takes level 0 from 17 entries to about 41
        // and a binary level is quadratic in that, so doing it on every
        // refinement round -- while the pool is ALSO doubling each round -- is
        // how this arm went from 176 seconds to not returning.
        BuildResult b = construct(spec, static_cast<std::size_t>(cap), lib, round > 0);
        if (!b.certified()) {
            if (out) *out = v;
            return (b.cases_passed > best.cases_passed) ? b : best;
        }
        best = std::move(b);

        // THE HUNT. An adversary is handed the candidate and looks for an input
        // on which it disagrees with the reference. Failing to find one after a
        // real search is a far stronger statement than passing a fixed sample,
        // and it is the only statement here that deserves the word guaranteed.
        bool broke = false;
        for (std::size_t i = 0; i < probes; ++i) {
            const Value in = prober(i);
            ++v.probes_run;
            if (best.recipe.apply(in, lib) == oracle(in)) continue;
            v.counterexample = in;
            broke = true;
            break;
        }

        if (!broke) {
            best.proof = Proof::Verified;
            v.verified = true;
            if (out) *out = v;
            return best;
        }
        if (round == rounds) break;

        // THE COUNTEREXAMPLE BECOMES A CONSTRAINT, and this is why the method
        // beats simply drawing more examples. A random case is usually one the
        // candidate already handles and teaches nothing. A counterexample is by
        // construction a case the candidate gets WRONG, so the next search
        // cannot return the same program. Every round strictly narrows the set
        // of behaviours still consistent with the specification.
        spec.cases.push_back(Case{v.counterexample, oracle(v.counterexample)});
        // Doubling six times is 64x the starting pool, and combined with mining
        // that is a cost nobody budgeted for. Growth is capped so a refinement
        // sequence cannot quietly become the most expensive thing in the run.
        cap = std::min(cap * 2.0, static_cast<double>(pool_cap) * 8.0);
    }

    if (out) *out = v;
    return best;
}


// ---------------------------------------------------------------------------
// Exhaustive checking
// ---------------------------------------------------------------------------

Exhaust check_exhaustive(const Recipe& r, const Library* lib, const Oracle& oracle,
                         std::int64_t lo, std::int64_t hi, std::size_t max_len) {
    Exhaust e;
    if (!r.found || hi < lo) return e;

    const std::size_t span = static_cast<std::size_t>(hi - lo + 1);

    // Odometer enumeration: every list of every length up to the bound, in
    // order, with no allocation per candidate beyond the list itself. Written
    // as an odometer rather than recursively because the recursion depth would
    // be the list length and the branching factor the value span, and a stack
    // frame per element is a needless cost on the hot path of a proof.
    Value in;
    for (std::size_t len = 0; len <= max_len; ++len) {
        in.assign(len, lo);
        for (;;) {
            ++e.checked;
            if (r.apply(in, lib) != oracle(in)) {
                e.counterexample = in;
                e.clean = false;
                return e;
            }
            // Advance the odometer. Done when every digit has wrapped.
            std::size_t d = 0;
            for (; d < len; ++d) {
                if (in[d] < hi) { ++in[d]; break; }
                in[d] = lo;
            }
            if (d == len) break;      // wrapped past the last digit
        }
    }
    e.clean = true;
    return e;
}

BuildResult synthesise_exhaustive(Spec spec, std::size_t pool_cap,
                                  const Oracle& oracle,
                                  std::int64_t lo, std::int64_t hi,
                                  std::size_t max_len, std::size_t rounds,
                                  const Library* lib, Exhaust* out) {
    BuildResult best;
    Exhaust last;
    double cap = static_cast<double>(pool_cap);

    for (std::size_t round = 0; round <= rounds; ++round) {
        BuildResult b = construct(spec, static_cast<std::size_t>(cap), lib, round > 0);
        if (!b.certified()) {
            if (out) *out = last;
            return (b.cases_passed > best.cases_passed) ? b : best;
        }
        best = std::move(b);

        last = check_exhaustive(best.recipe, lib, oracle, lo, hi, max_len);
        if (last.clean) {
            // Nothing in the domain disagrees. This is a proof over the domain,
            // not a failure to find a disagreement.
            best.proof = Proof::Exhaustive;
            if (out) *out = last;
            return best;
        }
        if (round == rounds) break;

        // The counterexample becomes a constraint. Unlike a random draw, it is
        // by construction an input the current program gets WRONG, so the next
        // search cannot return the same program -- and because the hunt is
        // complete, the sequence of counterexamples cannot cycle.
        spec.cases.push_back(Case{last.counterexample, oracle(last.counterexample)});
        cap = std::min(cap * 2.0, static_cast<double>(pool_cap) * 8.0);
    }

    if (out) *out = last;
    return best;
}

const std::vector<Value>& default_extremes() {
    // Boundary-value analysis, which is older than any of this and still the
    // highest-yield thing in testing. Ordered cheapest-first so a program that
    // breaks on the empty list is caught before one that breaks at the cap.
    static const std::vector<Value> v = {
        {}, {0}, {1}, {-1}, {0, 0}, {1, 1, 1, 1, 1, 1},
        // lengths past anything a small proof domain contains
        {1, 2, 3, 4, 5}, {1, 2, 3, 4, 5, 6}, {5, 4, 3, 2, 1, 0, -1},
        {9, 8, 7, 6, 5, 4, 3, 2},
        // values past anything a small proof domain contains, and the cap
        {3}, {-3}, {10}, {-10}, {2, 3}, {-3, -2}, {0, 10, 0},
        {kValueCap}, {-kValueCap}, {kValueCap - 1, 1}, {20000, -20000},
        // TWO LARGE VALUES OF THE SAME SIGN, so that a program whose arithmetic
        // is only correct while addition does not saturate is exercised. Added
        // because one was accepted: the self-hosting bench derived
        // pair_min = pair_add(x) - pair_max(x), which is min(a,b) exactly until
        // a+b clamps, folded it into `min`, and the result was wrong on 117 of
        // 252 large-magnitude grading inputs while passing a proof over lists of
        // length 0..4 over -2..2 and every edge above. Nothing in that domain
        // can saturate, and no edge here paired two large same-sign values.
        {kValueCap - 1, kValueCap - 1}, {-(kValueCap - 1), -(kValueCap - 1)},
        {kValueCap / 2, kValueCap / 2, kValueCap / 2},
        {7, 7, 7, 7, 7},
        // LONGER THAN THE PROOF DOMAIN BY AN ORDER OF MAGNITUDE. The bounded
        // proof runs over lists of length 0..4, and nothing above reached past
        // eight, so a program whose behaviour depends on LENGTH was certified by
        // looking only at short lists.
        //
        // That is not hypothetical either, and it is the second time this list
        // has been too small. A fold hands its body an ACCUMULATOR, which grows
        // to the length of the whole input, so proving the body on length 0..4
        // says nothing about the fold. The self-hosting bench rebuilt
        // rev = fold[roll](x) from a roll PROVED on the small domain and it was
        // wrong on 110 of 252 grading inputs, every one of them longer than 20.
        {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
         17, 18, 19, 20, 21, 22, 23, 24},
        {24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9,
         8, 7, 6, 5, 4, 3, 2, 1},
        {5, -3, 12, 0, 7, -8, 3, 11, -2, 9, 4, -6, 1, 15, -4, 2,
         13, -1, 6, 10, -7, 8, 0, 14},
        // ...and one long list of large values, so length and magnitude are not
        // only ever tested apart.
        {4000, -3000, 2500, 1000, -4500, 3200, -1200, 800, 2100, -900,
         1700, -2600, 3900, 600, -1500, 2800, -700, 1300, -3300, 450,
         2200, -1800, 950, 3600},
    };
    return v;
}

BuildResult synthesise_hardened(Spec spec, std::size_t pool_cap,
                                const Oracle& oracle,
                                std::int64_t lo, std::int64_t hi,
                                std::size_t max_len, std::size_t rounds,
                                const Library* lib, Exhaust* out) {
    const std::vector<Value>& edges = default_extremes();

    for (std::size_t round = 0; round <= rounds; ++round) {
        Exhaust ex;
        BuildResult b = synthesise_exhaustive(spec, pool_cap, oracle, lo, hi, max_len,
                                              rounds, lib, &ex);
        if (b.proof != Proof::Exhaustive) {
            // The bounded proof did not even hold. Report what was actually
            // earned rather than dressing it up.
            if (out) *out = ex;
            return b;
        }

        // Proved on the small domain. Now the edges of the large one.
        bool clean = true;
        for (const Value& in : edges) {
            if (b.recipe.apply(in, lib) != oracle(in)) {
                if (round == rounds) {
                    // Out of refinements with a known counterexample in hand.
                    // Downgrade: it is proved on the domain and BROKEN outside
                    // it, and Exhaustive would say the wrong thing.
                    b.proof = Proof::Generalised;
                    if (out) *out = ex;
                    return b;
                }
                spec.cases.push_back(Case{in, oracle(in)});
                clean = false;
                break;
            }
        }
        if (clean) {
            if (out) *out = ex;
            return b;     // proved on the domain AND unbroken at the edges
        }
    }

    return BuildResult{};
}

// One cut rule. `front` counts from the start of the output, otherwise from the
// end, and both are TOTAL: p is clamped to the output length, so append(A, B) is
// the output by construction for every input including the empty one. A rule
// that is undefined anywhere would hand the sub-search a specification the
// combination cannot satisfy.
namespace {

std::size_t cut_at(std::size_t len, bool front, std::size_t k) {
    const std::size_t kk = (k < len) ? k : len;
    return front ? kk : (len - kk);
}

Case cut_case(const Case& c, bool front, std::size_t k, bool take_front) {
    const std::size_t p = cut_at(c.out.size(), front, k);
    Value part = take_front ? Value(c.out.begin(), c.out.begin() + static_cast<long>(p))
                            : Value(c.out.begin() + static_cast<long>(p), c.out.end());
    return Case(c.in, c.extra, std::move(part));
}

Spec cut_spec(const Spec& s, bool front, std::size_t k, bool take_front,
              const char* suffix) {
    Spec o;
    o.name   = s.name + suffix;
    o.banned = s.banned;
    o.cases.reserve(s.cases.size());
    for (const Case& c : s.cases)   o.cases.push_back(cut_case(c, front, k, take_front));
    for (const Case& c : s.holdout) o.holdout.push_back(cut_case(c, front, k, take_front));
    return o;
}

Oracle cut_oracle(const Oracle& f, bool front, std::size_t k, bool take_front) {
    return [f, front, k, take_front](const Value& in) {
        const Value o = f(in);
        const std::size_t p = cut_at(o.size(), front, k);
        return take_front ? Value(o.begin(), o.begin() + static_cast<long>(p))
                          : Value(o.begin() + static_cast<long>(p), o.end());
    };
}

// A copy of the caller-s library with two more entries. Indices have to survive
// the copy, because a stored recipe naming Call 3 means the third entry.
Library extend(const Library* lib, const Recipe& a, const Recipe& b, bool& ok) {
    Library out(64);
    ok = true;
    if (lib != nullptr) {
        for (std::size_t i = 0; i < lib->size(); ++i) {
            if (!out.admit_recipe(lib->at(i).name, lib->at(i).recipe, 0)) ok = false;
        }
    }
    out.admit_recipe("_split_a", a, 0);
    out.admit_recipe("_split_b", b, 0);
    return out;
}

}  // namespace

BuildResult synthesise_split(Spec spec, std::size_t pool_cap,
                             const Oracle& oracle,
                             std::int64_t lo, std::int64_t hi,
                             std::size_t max_len, std::size_t rounds,
                             const Library* lib,
                             std::size_t max_depth) {
    // The ordinary answer first. Splitting is what happens when there is not one.
    BuildResult direct =
        synthesise_hardened(spec, pool_cap, oracle, lo, hi, max_len, rounds, lib);
    if (direct.proof == Proof::Exhaustive || max_depth == 0) return direct;

    // Cut one element off the front, then two, then the same from the back. Those
    // are the shapes that occur -- peeling an element off an end is how a list is
    // built -- and each extra rule costs two more sub-searches.
    struct Rule { bool front; std::size_t k; };
    static const Rule rules[] = {{true, 1}, {false, 1}, {true, 2}, {false, 2}};

    for (const Rule& r : rules) {
        // A cut that takes everything or nothing on every case has not split it.
        bool useful = false;
        for (const Case& c : spec.cases) {
            const std::size_t p = cut_at(c.out.size(), r.front, r.k);
            if (p != 0 && p != c.out.size()) { useful = true; break; }
        }
        if (!useful) continue;

        const BuildResult a = synthesise_split(
            cut_spec(spec, r.front, r.k, true, "_a"), pool_cap,
            cut_oracle(oracle, r.front, r.k, true), lo, hi, max_len, rounds, lib,
            max_depth - 1);
        if (a.proof != Proof::Exhaustive) continue;

        const BuildResult b = synthesise_split(
            cut_spec(spec, r.front, r.k, false, "_b"), pool_cap,
            cut_oracle(oracle, r.front, r.k, false), lo, hi, max_len, rounds, lib,
            max_depth - 1);
        if (b.proof != Proof::Exhaustive) continue;

        // Both halves in hand. Put the ORIGINAL specification back through the
        // ordinary search with them available: it now has append(libA, libB) as a
        // three-node candidate and proves it exactly as it proves anything else.
        // Nothing is accepted because a half was solved.
        bool copied = true;
        const Library ext = extend(lib, a.recipe, b.recipe, copied);
        if (!copied) continue;   // an index moved; the entries would not mean what they say
        BuildResult joined =
            synthesise_hardened(spec, pool_cap, oracle, lo, hi, max_len, rounds, &ext);
        if (joined.proof == Proof::Exhaustive) {
            joined.recipe = inline_calls(joined.recipe, ext);
            return joined;
        }
    }
    return direct;
}

BuildResult construct_split(const Spec& spec, std::size_t max_pool,
                            const Library* lib, std::size_t max_depth) {
    BuildResult direct = construct_best(spec, max_pool, lib);
    if (direct.proof == Proof::Generalised || max_depth == 0) return direct;

    struct Rule { bool front; std::size_t k; };
    static const Rule rules[] = {{true, 1}, {false, 1}, {true, 2}, {false, 2}};

    for (const Rule& r : rules) {
        bool useful = false;
        for (const Case& c : spec.cases) {
            const std::size_t p = cut_at(c.out.size(), r.front, r.k);
            if (p != 0 && p != c.out.size()) { useful = true; break; }
        }
        if (!useful) continue;

        const BuildResult a = construct_split(cut_spec(spec, r.front, r.k, true, "_a"),
                                              max_pool, lib, max_depth - 1);
        if (a.proof != Proof::Generalised) continue;
        const BuildResult b = construct_split(cut_spec(spec, r.front, r.k, false, "_b"),
                                              max_pool, lib, max_depth - 1);
        if (b.proof != Proof::Generalised) continue;

        bool copied = true;
        const Library ext = extend(lib, a.recipe, b.recipe, copied);
        if (!copied) continue;
        BuildResult joined = construct_best(spec, max_pool, &ext);
        if (joined.proof == Proof::Generalised) {
            // INLINE BEFORE RETURNING. The two halves live in a library that dies
            // with this call, so a recipe still naming Call 7 would mean nothing
            // to the caller -- and the caller admits what it is given.
            joined.recipe = inline_calls(joined.recipe, ext);
            return joined;
        }
    }
    return direct;
}

} // namespace khora::techne
