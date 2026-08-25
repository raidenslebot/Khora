// HOW FAST DOES IT WRITE CERTIFIED CODE?
//
// The target is 10,000 lines in 30 seconds: 333 lines per second. This measures
// it, and measures it in a way that cannot be gamed by the three obvious cheats.
//
//   NOT boilerplate. The prelude -- the operation set each backend needs -- is
//   emitted ONCE per file and counted separately. Only function bodies count
//   toward the rate, and only the nodes the answer actually reaches are emitted,
//   so dead code cannot pad the number.
//
//   NOT unverified. Every line counted comes from a recipe that passed every
//   visible case AND every held-out case it was never scored on. An uncertified
//   result contributes zero lines. This is the number that matters: a language
//   model emits thousands of lines a second and knows nothing about whether any
//   of them are right.
//
//   NOT one language. The same recipes are emitted to fourteen targets from the
//   same certificate, because a backend per target is what "any language" has
//   to mean if it is going to mean anything. Each backend reproduces the value
//   cap, the empty-list results, the zero-guard on division and the cycling
//   shorter operand in that language's own arithmetic -- Python, Ruby, Lua and
//   Haskell have to be told to truncate rather than floor, and getting that
//   wrong would make a certificate a lie on exactly the negative inputs.
//
// CPU IS A TOOL TO BE ABUSED: tasks are independent, so the work is taken from a
// single atomic cursor by every hardware thread, and the library they learn into
// is shared. Three arms are run -- one thread, all threads sharing, all threads
// in isolation -- so the scaling and the value of sharing are both visible
// rather than assumed.
//
// RAM IS PRECIOUS: peak pool bytes are reported per shard. The pool is the only
// structure that grows with the search, so it is the only one worth watching.

#include "khora/techne/techne.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <mutex>
#include <memory>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

using namespace khora::techne;
using clk = std::chrono::high_resolution_clock;

namespace {

// HOW MANY VISIBLE CASES A SPECIFICATION CARRIES.
//
// This is the quality knob, and it is not a detail. A program that satisfies six
// examples can be wrong everywhere those six did not look -- measured, 333 of
// 2,000 tasks produced exactly that and were correctly refused as memorisation.
// Every additional case is another constraint the answer has to survive, so the
// set of behaviours consistent with the specification shrinks and the false
// positives go with it.
std::size_t g_visible = 6;

std::uint64_t mix(std::uint64_t& s) {
    std::uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// A GENERATED TASK FAMILY, so the suite can be scaled to any size without a
// human writing each one. Every task is a composition of primitives applied to
// a list, with the parameters drawn per task -- which means the search is
// solving problems nobody hand-picked to be solvable.
struct Gen {
    int shape;            // which composition
    std::int64_t k;       // the parameter
    std::string name() const {
        static const char* n[] = {"scale", "shift", "filter_sum", "count_over",
                                  "sorted_take", "range_scaled", "tail_sum",
                                  "rev_take", "sq_sum", "minmax"};
        return std::string(n[shape % 10]) + "_" + std::to_string(k);
    }
    // THE ORACLE MUST RESPECT THE SAME VALUE BOUND THE INTERPRETER DOES.
    //
    // These generators computed x*k and running sums in unbounded int64 while
    // every value the system produces saturates at kValueCap. On the 1e9 edge
    // probe that is a guaranteed disagreement: mul(x, 5) is the CORRECT program
    // and the oracle demanded 5e9 from an interpreter that cannot represent it.
    // Thirty programs a run were counted as "certified but a counterexample
    // exists" on the strength of that -- the same defect as clamp_len, where the
    // interpreter bounded every result and the reference did not, and it is the
    // reference that is wrong both times.
    static Value capped(Value v) {
        for (auto& x : v) x = cap_value(x);
        return v;
    }
    Value operator()(const Value& v) const { return capped(raw(v)); }
    Value raw(const Value& v) const {
        Value o;
        switch (shape % 10) {
            case 0: for (auto x : v) o.push_back(x * k); return o;
            case 1: for (auto x : v) o.push_back(x + k); return o;
            case 2: { std::int64_t s = 0; for (auto x : v) if (x > k) s += x; return Value{s}; }
            case 3: { std::int64_t n = 0; for (auto x : v) if (x > k) ++n; return Value{n}; }
            case 4: { Value t = v; std::sort(t.begin(), t.end());
                      const std::size_t n = std::min<std::size_t>(t.size(), static_cast<std::size_t>(std::max<std::int64_t>(0, k)));
                      return Value(t.begin(), t.begin() + static_cast<std::ptrdiff_t>(n)); }
            case 5: { if (v.empty()) return {};
                      const std::int64_t n = std::max<std::int64_t>(0, std::min<std::int64_t>(v[0], 64));
                      for (std::int64_t i = 0; i < n; ++i) o.push_back(i * k); return o; }
            case 6: { if (v.size() < 2) return {};
                      std::int64_t s = 0; for (std::size_t i = 1; i < v.size(); ++i) s += v[i];
                      return Value{s}; }
            case 7: { Value t(v.rbegin(), v.rend());
                      const std::size_t n = std::min<std::size_t>(t.size(), static_cast<std::size_t>(std::max<std::int64_t>(0, k)));
                      return Value(t.begin(), t.begin() + static_cast<std::ptrdiff_t>(n)); }
            case 8: { std::int64_t s = 0; for (auto x : v) s += x * x; return Value{s}; }
            default: { if (v.empty()) return {};
                       const auto mm = std::minmax_element(v.begin(), v.end());
                       return Value{*mm.second - *mm.first}; }
        }
    }
};

Spec make(const Gen& g, std::uint64_t& seed) {
    Spec s;
    s.name = g.name();
    auto draw = [&](std::size_t len) {
        Value v;
        for (std::size_t i = 0; i < len; ++i) {
            v.push_back(static_cast<std::int64_t>(mix(seed) % 40) - 15);
        }
        return v;
    };
    // DISJOINT CASE AND HOLDOUT LENGTHS HERE TOO -- and fixing it does not help,
    // which is worth recording because it did help in the other three.
    //
    // Cases run 2-6 and the holdout 7-10, the same shape that was hiding a fifth
    // of the capability in ascent_bench, fixedbar_bench and selfhost_bench.
    // Widening the cases to 1-11 with the holdout at 13-16 was measured here:
    // certification fell from 1,700 of 2,000 to 1,510, and the count of programs
    // carrying a certificate AND a counterexample did not move -- 30 against 31.
    //
    // So the thirty are not length overfits. Whatever makes them wrong survives
    // a holdout drawn well past anything the cases showed, and widening the
    // cases only makes every task harder for no gain in quality. The defect
    // class is real and its impact is bench-specific; this is the bench where it
    // is not the cause.
    for (std::size_t i = 0; i < g_visible; ++i) s.cases.push_back({draw(1 + i % 11), {}});
    for (std::size_t i = 0; i < 4; ++i) s.holdout.push_back({draw(13 + i), {}});
    for (auto& c : s.cases)   c.out = g(c.in);
    for (auto& c : s.holdout) c.out = g(c.in);
    return s;
}

struct Shard {
    std::size_t solved = 0, attempted = 0;
    std::size_t body_lines = 0;
    std::size_t peak_bytes = 0;
    std::size_t lib_calls = 0;
    std::string source;
};

// A SHARED library and a DYNAMIC queue.
//
// The first version gave each thread its own library and a fixed slice of the
// tasks, and both were wrong in ways the measurement showed plainly. Per-shard
// libraries meant 24 workers each rediscovering the same primitives, and the
// parallel arm certified FEWER tasks than the single thread -- 1001 against
// 1100 -- because each shard learned in isolation. Fixed slices meant the
// thread that drew the expensive tasks ran long after the others were idle,
// which is why 24 threads returned 8.17x rather than something near 20x.
//
// Now: one library every worker can read, and a single atomic cursor so a
// worker that finishes early takes the next task instead of waiting.
//
// COPY-ON-WRITE, not a reader-writer lock.
//
// The shared_mutex version held a READ lock for the whole of construct, which
// is the expensive part, so a single writer wanting to admit a recipe stalled
// every worker behind it. Measured: 76.95 s on 24 threads against 9.36 s with
// no sharing at all -- 1.20x scaling, i.e. effectively serialised. The sharing
// was worth having (1090 certified against 990, 827 library calls against 586)
// and the lock was eating all of it and more.
//
// So readers never lock. The library is an immutable snapshot behind an atomic
// pointer: a reader loads it once and uses it for the whole search, and a writer
// copies, admits, and swaps the pointer. A concurrent admission can be lost when
// two writers race, and that is fine -- this is a cache of learned functions
// where a lost entry costs one rediscovery, not a ledger where it costs
// correctness.
struct SharedLib {
    std::atomic<std::shared_ptr<const Library>> snap;
    std::mutex write_gate;                 // serialises writers only
    std::size_t budget;
    explicit SharedLib(std::size_t b) : budget(b) {
        snap.store(std::make_shared<const Library>(b));
    }
    std::shared_ptr<const Library> read() const { return snap.load(); }
    void admit(const std::string& name, const Recipe& r, std::size_t task) {
        std::lock_guard<std::mutex> g(write_gate);
        auto next = std::make_shared<Library>(*snap.load());
        next->admit_recipe(name, r, task);
        next->prune();
        snap.store(std::shared_ptr<const Library>(next));
    }
};

void run_worker(const std::vector<Spec>& specs, std::atomic<std::size_t>& cursor,
                std::size_t pool_cap, Lang lang, SharedLib* shared,
                std::size_t solo_budget, Shard& out) {
    // With no shared library a worker keeps its own, which is the arm that shows
    // what the sharing is worth.
    Library solo(solo_budget);
    for (;;) {
        const std::size_t i = cursor.fetch_add(1, std::memory_order_relaxed);
        if (i >= specs.size()) break;
        ++out.attempted;

        // Mining is always on and kept small (8 constants) rather than made
        // conditional. A cheap-then-mined retry was tried and was worse: ~45% of
        // tasks fail, so nearly half the workload paid for both attempts.
        BuildResult b;
        std::shared_ptr<const Library> snap;
        const Library* use = &solo;
        if (shared != nullptr) {
            snap = shared->read();                    // one atomic load, no lock
            use = snap.get();
        }
        b = construct(specs[i], pool_cap, use);
        out.peak_bytes = std::max(out.peak_bytes, b.distinct_behaviours * 64);
        if (b.proof != Proof::Generalised) continue;

        ++out.solved;
        std::size_t lines = 0;
        (void)emit(b.recipe, lang, "kh_" + specs[i].name, &lines,
                   shared ? snap.get() : &solo);
        out.body_lines += lines;
        // Library primitives INSIDE the answer, over the nodes the root reaches.
        // If this is zero across a whole run, later solutions are not built on
        // earlier ones and the sharing is buying nothing.
        {
            std::vector<bool> seen(b.recipe.pool.size(), false);
            std::vector<std::size_t> st{b.recipe.root};
            while (!st.empty()) {
                const std::size_t u = st.back();
                st.pop_back();
                if (u >= b.recipe.pool.size() || seen[u]) continue;
                seen[u] = true;
                if (b.recipe.pool[u].op == Op::Call) ++out.lib_calls;
                if (b.recipe.pool[u].a >= 0) st.push_back(static_cast<std::size_t>(b.recipe.pool[u].a));
                if (b.recipe.pool[u].b >= 0) st.push_back(static_cast<std::size_t>(b.recipe.pool[u].b));
            }
        }

        if (shared != nullptr) {
            shared->admit(specs[i].name, b.recipe, i);
        } else {
            solo.admit_recipe(specs[i].name, b.recipe, i);
            solo.prune();
        }
    }
}


// STAGE MARKERS THAT FLUSH.
//
// This benchmark stopped finishing and I diagnosed the cause three times by
// reasoning about it -- Member being quadratic, mining being on, the refinement
// pool doubling -- and was wrong or incomplete each time, because stdout is
// buffered and a hang shows nothing at all. An unobservable program invites
// exactly that kind of confident guessing. One flushed line per arm turns it
// into a measurement.
static void stage(const char* what) {
    std::printf("  [stage] %s\n", what);
    std::fflush(stdout);
}

} // namespace

int main(int argc, char** argv) {
    const std::size_t ntask    = (argc > 1) ? std::stoul(argv[1]) : 2000;
    const std::size_t pool_cap = (argc > 2) ? std::stoul(argv[2]) : 3000;
    const std::size_t lib_bud  = (argc > 3) ? std::stoul(argv[3]) : 24;
    g_visible                  = (argc > 4) ? std::stoul(argv[4]) : 6;

    // CAPPED AT A FRACTION OF THE MACHINE, not all of it. A stale run of this
    // very benchmark reached 77 minutes and 14,297 CPU-seconds on a box someone
    // was trying to work on. A benchmark that makes the machine unusable is one
    // that gets killed rather than finished, so its numbers are worth less than
    // the slightly smaller numbers it would have produced with headroom.
    //
    // The scaling arm below now measures scaling to the CAP, and the table says
    // so, because reporting "24 threads" while running 18 would be a lie about
    // the axis the measurement is against.
    const unsigned hw = static_cast<unsigned>(worker_threads());
    std::printf("How fast does it write CERTIFIED code?\n\n");
    std::printf("  target: 10,000 lines in 30 s = 333.3 lines/s\n");
    std::printf("  %zu generated tasks, pool cap %zu, library budget %zu, %u hardware threads\n\n",
                ntask, pool_cap, lib_bud, hw,
                std::max(1u, std::thread::hardware_concurrency()));

    std::uint64_t seed = 0xC0FFEEULL;
    std::vector<Spec> specs;
    std::vector<Gen> gens;
    specs.reserve(ntask);
    gens.reserve(ntask);
    for (std::size_t i = 0; i < ntask; ++i) {
        Gen g{static_cast<int>(mix(seed) % 10),
              static_cast<std::int64_t>(mix(seed) % 9) + 1};
        gens.push_back(g);
        specs.push_back(make(g, seed));
    }

    auto sweep = [&](unsigned threads, bool share) {
        std::vector<Shard> shards(threads);
        SharedLib shared(lib_bud);
        std::atomic<std::size_t> cursor{0};
        const auto t = clk::now();
        {
            std::vector<std::thread> pool;
            for (unsigned k = 0; k < threads; ++k) {
                pool.emplace_back(run_worker, std::cref(specs), std::ref(cursor),
                                  pool_cap, Lang::Cpp, share ? &shared : nullptr,
                                  lib_bud, std::ref(shards[k]));
            }
            for (auto& th : pool) th.join();
        }
        Shard total;
        for (const Shard& s : shards) {
            total.solved += s.solved;
            total.attempted += s.attempted;
            total.body_lines += s.body_lines;
            total.lib_calls += s.lib_calls;
            total.peak_bytes = std::max(total.peak_bytes, s.peak_bytes);
        }
        return std::make_pair(total, std::chrono::duration<double>(clk::now() - t).count());
    };

    stage("1-thread arm");
const auto one  = sweep(1, true);
    stage("parallel shared arm");
const auto many = sweep(hw, true);
    stage("parallel isolated arm");
const auto solo = sweep(hw, false);

    // ---- TO FIXPOINT --------------------------------------------------------
    //
    // Every arm above attempts each task exactly once, in whatever order the
    // queue hands it out, so a task that is trivial once some component exists
    // fails when it happens to come first. That is a fact about scheduling, not
    // about solvability, and iterating removes it.
stage("fixpoint arm");
    SolveConfig sc;
    sc.pool_cap = pool_cap;
    sc.lib_budget = lib_bud;
    SolveStats st;
    const auto results = solve_all(specs, sc, &st);
    std::size_t fx_lines = 0;
    for (const BuildResult& b : results) {
        if (b.proof != Proof::Generalised) continue;
        std::size_t n = 0;
        (void)emit(b.recipe, Lang::Cpp, "f", &n, nullptr);
        fx_lines += n;
    }

    const Shard single = one.first;   const double s1 = one.second;
    const Shard all    = many.first;  const double sN = many.second;

    std::printf("  arm            | certified | body lines | seconds | lines/s\n");
    std::printf("  ---------------+-----------+------------+---------+---------\n");
    std::printf("  1 thread       | %5zu/%-5zu| %10zu | %7.2f | %8.1f\n",
                single.solved, single.attempted, single.body_lines, s1,
                single.body_lines / std::max(1e-9, s1));
    std::printf("  %2u thr (cap)   | %5zu/%-5zu| %10zu | %7.2f | %8.1f\n",
                hw, all.solved, all.attempted, all.body_lines, sN,
                all.body_lines / std::max(1e-9, sN));
    std::printf("  %2u thr, no share| %5zu/%-5zu| %10zu | %7.2f | %8.1f\n",
                hw, solo.first.solved, solo.first.attempted, solo.first.body_lines,
                solo.second, solo.first.body_lines / std::max(1e-9, solo.second));
    std::printf("  %2u thr, FIXPOINT| %5zu/%-5zu| %10zu | %7.2f | %8.1f\n",
                hw, st.certified, st.attempted, fx_lines, st.seconds,
                fx_lines / std::max(1e-9, st.seconds));
    std::printf("\n  scaling: %.2fx on %u threads\n", s1 / std::max(1e-9, sN), hw);
    // ---- COUNTEREXAMPLE-GUIDED, the arm that attacks memorisation -----------
    //
    // 249 of 2,000 tasks produce a program that passes every case it was shown
    // and is wrong on a held-out one. More examples helps and cannot GUARANTEE,
    // because any finite sample leaves behaviours consistent with all of it and
    // wrong everywhere else. This hunts instead: synthesise, look for an input
    // where the program disagrees with the reference, feed that input back as a
    // constraint, repeat. A counterexample is by construction a case the current
    // program gets WRONG, so the next search cannot return the same program.
    {
stage("counterexample arm");
        // Sized against the run, not fixed at 400. This arm does up to six
        // refinements per task with a doubling pool and 300 probes each, so it
        // is the most expensive thing here by an order of magnitude; a fixed 400
        // meant a 300-task run spent nearly all its time in one arm and never
        // reached the language table.
        const std::size_t sample = std::min<std::size_t>(specs.size(), 60);
        // Only VERIFIED recipes enter this library. A merely certified one is
        // wrong somewhere the sample did not look, and admitting it would put a
        // primitive that lies underneath every later search.
        Library cegis_lib(lib_bud);
        // SPLIT BY WHAT THE PROOF ACTUALLY CLAIMED. certified() is true for
        // Proof::Tested, which means "passed the visible cases" and nothing
        // more -- the system's own word for a result it does not trust. Counting
        // those beside Generalised ones under one heading overstates the defect,
        // because only a GENERALISED result ever claimed to hold on inputs it
        // was not shown.
        std::size_t verified = 0, certified_only = 0, unsolved = 0;
        std::size_t gen_wrong = 0, tested_wrong = 0;
        std::size_t total_rounds = 0, total_probes = 0;
        const auto tv = clk::now();
        for (std::size_t i = 0; i < sample; ++i) {
            const Gen g = gens[i];
            Oracle oracle = [g](const Value& in) { return g(in); };
            auto ps = std::make_shared<std::uint64_t>(0xA11CEULL + i);
            Prober prober = [ps](std::size_t k) -> Value {
                // The edges first, then random. A prober that only draws
                // uniformly misses exactly the shapes that break a fitted
                // program.
                static const Value edges[] = {
                    {}, {0}, {1}, {-1}, {7, 7, 7}, {1, 2, 3, 4, 5}, {5, 4, 3, 2, 1},
                    {1000000000}, {-1000000000}, {0, 0, 0, 0},
                };
                if (k < sizeof(edges) / sizeof(edges[0])) return edges[k];
                Value v;
                const std::size_t len = mix(*ps) % 11;
                for (std::size_t j = 0; j < len; ++j) {
                    v.push_back(static_cast<std::int64_t>(mix(*ps) % 50) - 20);
                }
                return v;
            };
            Verification ver;
            // The library was nullptr here, which meant the one arm measuring
            // real correctness was also the only arm denied the compounding
            // every other arm gets.
            const BuildResult b = synthesise_verified(specs[i], pool_cap, oracle,
                                                      prober, 300, 6, &cegis_lib, &ver);
            if (b.proof == Proof::Verified) {
                cegis_lib.admit_recipe(specs[i].name, b.recipe, i);
                cegis_lib.prune();
            }
            total_rounds += ver.rounds;
            total_probes += ver.probes_run;
            if (b.proof == Proof::Verified) ++verified;
            else if (b.certified()) {
                // LOOK AT THEM. Thirty programs carry a certificate and a
                // counterexample at the same time, after six rounds of refinement
                // against three hundred probes. Counting them for several cycles
                // told me nothing; the counterexample itself is right here and was
                // never printed.
                if (certified_only < 6) {
                    const Value& ce = ver.counterexample;
                    const Value want = oracle(ce);
                    const Value got  = b.recipe.apply(ce, &cegis_lib);
                    std::printf("    [ce] %-22s len=%2zu  want %zu vals, got %zu  %s\n",
                                specs[i].name.c_str(), ce.size(), want.size(), got.size(),
                                b.recipe.render().c_str());
                }
                ++certified_only;
                if (b.proof == Proof::Generalised) ++gen_wrong; else ++tested_wrong;
            }
            else ++unsolved;
        }
        const double tvs = std::chrono::duration<double>(clk::now() - tv).count();
        std::printf("\n  COUNTEREXAMPLE-GUIDED (%zu tasks, 300 probes, <=6 refinements)\n",
                    sample);
        std::printf("    VERIFIED, no counterexample found : %zu\n", verified);
        std::printf("    certified but a counterexample exists: %zu\n", certified_only);
        std::printf("      of those, GENERALISED and still wrong : %zu\n", gen_wrong);
        std::printf("      merely Tested, never claimed to hold  : %zu\n", tested_wrong);
        std::printf("    not solved at all                 : %zu\n", unsolved);
        std::printf("    %.2f refinements and %.0f probes per task, %.1f s total\n",
                    static_cast<double>(total_rounds) / sample,
                    static_cast<double>(total_probes) / sample, tvs);
        std::printf("    Of the ones it solves, %.1f%% survive an adversarial hunt.\n",
                    100.0 * verified / std::max<std::size_t>(1, verified + certified_only));
    }

    std::printf("\n  ITERATING TO FIXPOINT -- newly certified per round:\n   ");
    for (const std::size_t k : st.solved_per_round) std::printf(" %zu", k);
    std::printf("\n  %zu rounds, %zu certified (%.1f%%), %zu memorised and rejected,\n",
                st.rounds, st.certified,
                100.0 * st.certified / std::max<std::size_t>(1, st.attempted),
                st.memorised);
    std::printf("  library holds %zu primitives, %zu calls inside answers.\n",
                st.library_size, st.library_calls);
    std::printf("  A round that certifies nothing new ends it: nothing changed, so\n");
    std::printf("  nothing can change. That is why an unbounded loop terminates.\n");
    std::printf("  one shared library against one per worker: %zu certified vs %zu,\n",
                all.solved, solo.first.solved);
    std::printf("  and %zu library calls inside answers vs %zu.\n",
                all.lib_calls, solo.first.lib_calls);

    const double rate = all.body_lines / std::max(1e-9, sN);
    std::printf("  peak pool: %.1f MB per shard\n", all.peak_bytes / 1048576.0);
    std::printf("\n  AGAINST THE TARGET (333.3 lines/s of CERTIFIED code)\n");
    if (rate >= 333.3) {
        std::printf("    %.0f lines/s -- MET, at %.1fx the target.\n", rate, rate / 333.3);
        std::printf("    10,000 lines would take %.1f s.\n", 10000.0 / rate);
    } else {
        std::printf("    %.0f lines/s -- NOT met, %.1fx short.\n", rate, 333.3 / rate);
        std::printf("    10,000 lines would take %.1f s against a 30 s budget.\n", 10000.0 / rate);
    }

    // ---- the same certificates, four languages ------------------------------
    //
    // Emitted from ONE certificate each. The certificate is language-independent
    // because it is a statement about behaviour, not about syntax -- which is
    // what makes a new backend a day of work rather than a new organ.
    std::printf("\n  THE SAME CERTIFICATES, EMITTED TO FOURTEEN LANGUAGES\n");
    std::printf("  language     | prelude | bodies | total lines\n");
    std::printf("  -------------+---------+--------+------------\n");
    Library demo_lib(lib_bud);
    std::vector<Recipe> certified;
    for (std::size_t i = 0; i < std::min<std::size_t>(specs.size(), 200); ++i) {
        const BuildResult b = construct(specs[i], pool_cap, &demo_lib);
        if (b.proof != Proof::Generalised) continue;
        certified.push_back(b.recipe);
        demo_lib.admit_recipe(specs[i].name, b.recipe, i);
        demo_lib.prune();
    }
    // The body count is IDENTICAL across every row, and that is the check
    // rather than a coincidence: a recipe is a statement about behaviour, so
    // the number of synthesised lines is a property of the recipe and not of
    // the target. A row whose body count differs has a backend that is emitting
    // something other than the recipe.
    for (const Lang l : {Lang::Cpp, Lang::Python, Lang::JavaScript, Lang::Rust,
                         Lang::Go, Lang::Java, Lang::CSharp, Lang::TypeScript,
                         Lang::Ruby, Lang::Lua, Lang::Haskell, Lang::Swift,
                         Lang::Kotlin, Lang::Php}) {
        std::size_t pre = 1, bodies = 0;
        const std::string p = prelude(l);
        for (const char ch : p) if (ch == '\n') ++pre;
        for (std::size_t i = 0; i < certified.size(); ++i) {
            std::size_t n = 0;
            (void)emit(certified[i], l, "f" + std::to_string(i), &n, &demo_lib);
            bodies += n;
        }
        std::printf("  %-12s | %7zu | %6zu | %11zu\n",
                    lang_name(l), pre, bodies, pre + bodies);
    }

    // A sample, so the output can be read rather than trusted.
    if (!certified.empty()) {
        std::printf("\n  SAMPLE (%s), one certified function:\n\n", lang_name(Lang::Python));
        std::size_t n = 0;
        std::printf("%s", emit(certified.front(), Lang::Python, "solved_0", &n, &demo_lib).c_str());
    }
    return 0;
}
