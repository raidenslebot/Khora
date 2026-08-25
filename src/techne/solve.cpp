// Solving to fixpoint — the loop in which the system improves itself.
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

std::vector<BuildResult> solve_all(const std::vector<Spec>& specs,
                                   SolveConfig cfg, SolveStats* stats) {
    const auto t0 = clk::now();
    std::vector<BuildResult> out(specs.size());

    const std::size_t threads = cfg.threads > 0
        ? cfg.threads
        : std::max<std::size_t>(1, std::thread::hardware_concurrency());

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
                const std::size_t k = cursor.fetch_add(1, std::memory_order_relaxed);
                if (k >= pending.size()) break;
                const std::size_t i = pending[k];

                auto snap = lib.read();      // one atomic load, no lock held
                BuildResult b = construct(specs[i], static_cast<std::size_t>(pool_cap),
                                          snap.get());
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

    if (stats) {
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
        BuildResult b = construct(spec, static_cast<std::size_t>(cap), lib);
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
        cap *= 2.0;
    }

    if (out) *out = v;
    return best;
}

} // namespace khora::techne
