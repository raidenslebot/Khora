// A POPULATION, NOT A MIND -- the four-arm experiment of SPEC-v2.
//
// The measured problem: one monolithic TemporalMemory does not bound on prose.
// Burst fraction never leaves 0.93 at 24k tokens, 2.9M segments, growth linear
// in the corpus. And the obvious upstream fix already failed: encoding words by
// their strongest associates, so related words overlap in code space, moved
// segments per token from 140.3 to 135.7 and burst from 0.938 to 0.930 -- three
// percent. The memory keys on exact winner-cell conjunctions, so similarity of
// input codes does not make contexts recur. The architecture is the problem.
//
// The proposal under test, from the operator's directive rendered computational:
// a population of small sequence memories under RESOURCE SELECTION. The scarce
// resource -- segments -- is the selection pressure. Predict well, earn budget;
// burst constantly, starve.
//
// Four arms, ONE global segment budget G, same stream, same held-out windows:
//
//   A  monolithic       one TM capped at G. The baseline given a budget.
//   B  partition        N TMs, windows dealt round-robin, each capped at G/N.
//                       Tests whether SPLITTING alone does the work.
//   C  competition      N TMs; each window is claimed by whichever organism
//                       predicts it best (lowest dry-run anomaly). Tests
//                       whether ROUTING earns its place over dealing.
//   D  selection        C plus birth and death: diverse heritable context
//                       depths, a shared budget pool, and each epoch the worst
//                       predictor is killed (segments freed) and the best
//                       spawns a mutated child. Tests whether EVOLUTION earns
//                       its place over static competition.
//
// The ladder is deliberate: each arm adds one mechanism, and any arm matching
// the one below it deletes that mechanism. Kill criteria are in the spec,
// written before this file.
//
// ONE ORGANISM GENE deserves explanation: CONTEXT DEPTH. The temporal memory's
// per-cell context is what makes it beat a pair-encoding on structured
// sequences -- and on prose it is exactly what makes every window unique: after
// eight words of history, no two contexts ever match, so every step allocates.
// An organism with depth k resets its temporal state every k tokens, trading
// order for recurrence. Bigram-depth organisms see contexts that actually
// repeat. If selection in arm D pulls depth DOWN, that is the system
// discovering, by starvation, that high-order context is a liability on this
// stream -- which no amount of designing would have been trusted without the
// measurement.

#include "khora/cortex/temporal_memory.hpp"
#include "khora/lattice/sdr.hpp"
#include "khora/reservoir/reservoir.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace khora::cortex;
using khora::lattice::Sdr;
using clock_t_ = std::chrono::high_resolution_clock;

namespace {

constexpr std::size_t kWin = 8;          // tokens per window, all arms
constexpr std::size_t kHoldEvery = 7;    // every 7th window is held out

std::uint64_t rs = 0xB10B10ULL;
std::uint64_t rnd() {
    std::uint64_t z = (rs += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

std::vector<std::string> tokenize(const std::string& text, std::size_t want) {
    std::vector<std::string> out;
    std::string cur;
    for (const char ch : text) {
        if (std::isalpha(static_cast<unsigned char>(ch))) {
            cur += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        } else if (!cur.empty()) {
            if (cur.size() >= 2) out.push_back(cur);
            cur.clear();
            if (out.size() >= want) return out;
        }
    }
    return out;
}

// An organism: a temporal memory plus its one heritable gene. Depth k means
// the temporal state resets every k tokens -- how much history a context may
// carry before it must stand on its own.
struct Organism {
    std::unique_ptr<TemporalMemory> tm;
    std::size_t depth;
    // Bookkeeping for fitness and reporting.
    double      claimed_anomaly = 0.0;
    std::size_t claimed = 0;

    explicit Organism(std::size_t d) : tm(std::make_unique<TemporalMemory>()), depth(d) {}

    // Feed one window. Returns mean anomaly over the steps that could predict.
    double feed(const std::vector<Sdr>& w, bool learn) {
        tm->reset();
        double sum = 0.0;
        std::size_t n = 0;
        for (std::size_t k = 0; k < w.size(); ++k) {
            if (k > 0 && k % depth == 0) tm->reset();
            const auto st = tm->compute(w[k], learn);
            if (k % depth != 0) { sum += st.anomaly; ++n; }
        }
        return n ? sum / n : 1.0;
    }
    std::size_t segments() const { return tm->segment_count(); }
};

struct ArmResult {
    const char* name = "";
    double train_burst = 0.0;      // mean anomaly, last quarter of training
    double heldout_burst = 0.0;    // mean anomaly on held-out windows
    std::size_t segments = 0;
    double ms_total = 0.0;
    std::string note;
};

void report(const ArmResult& r, std::size_t budget) {
    std::printf("  %-14s | %11.3f | %13.3f | %8zu / %zu | %7.1f s  %s\n",
                r.name, r.train_burst, r.heldout_burst, r.segments, budget,
                r.ms_total / 1000.0, r.note.c_str());
}

} // namespace

int main(int argc, char** argv) {
    const std::string dir    = (argc > 1) ? argv[1] : "data/reservoir";
    const std::size_t cap    = (argc > 2) ? std::stoul(argv[2]) : 24000;
    const std::size_t G      = (argc > 3) ? std::stoul(argv[3]) : 400000;  // global budget
    const std::size_t N      = (argc > 4) ? std::stoul(argv[4]) : 8;

    khora::reservoir::Reservoir res(dir);
    res.load_catalog();
    const auto cat = res.catalog();
    if (cat.empty()) { std::printf("no tomes at %s\n", dir.c_str()); return 1; }

    std::vector<std::string> stream;
    for (const auto& t : cat) {
        if (stream.size() >= cap) break;
        auto text = res.read(t.title);
        if (!text || text->size() < 20000) continue;
        auto ws = tokenize(*text, cap - stream.size());
        stream.insert(stream.end(), ws.begin(), ws.end());
    }

    // Encode once. Every arm sees the same Sdr stream.
    std::vector<std::vector<Sdr>> train, held;
    for (std::size_t i = 0, w = 0; i + kWin <= stream.size(); i += kWin, ++w) {
        std::vector<Sdr> win;
        win.reserve(kWin);
        for (std::size_t k = 0; k < kWin; ++k)
            win.push_back(Sdr::from_hash("w:" + stream[i + k]));
        if (w % kHoldEvery == kHoldEvery - 1) held.push_back(std::move(win));
        else                                  train.push_back(std::move(win));
    }
    std::printf("A population, not a mind -- four arms, one budget\n\n");
    std::printf("  %zu tokens: %zu training windows, %zu held out, budget %zu"
                " segments, N = %zu\n\n",
                stream.size(), train.size(), held.size(), G, N);

    std::printf("  arm            | train burst | held-out burst | segments (budget)"
                "   | time\n");
    std::printf("  ---------------+-------------+----------------+------------------"
                "---+---------\n");

    const std::size_t tail = train.size() / 4;   // last quarter = "end of training"

    // ---- ARM A: monolithic under the budget --------------------------------
    {
        Organism a(kWin);
        double tb = 0.0; std::size_t tn = 0;
        const auto t0 = clock_t_::now();
        for (std::size_t w = 0; w < train.size(); ++w) {
            const bool frozen = a.segments() >= G;
            const double an = a.feed(train[w], !frozen);
            if (w + tail >= train.size()) { tb += an; ++tn; }
        }
        double hb = 0.0;
        for (const auto& w : held) hb += a.feed(w, false);
        ArmResult r{"A monolithic", tn ? tb / tn : 1.0,
                    held.empty() ? 1.0 : hb / held.size(), a.segments(),
                    std::chrono::duration<double, std::milli>(clock_t_::now() - t0).count()};
        report(r, G);
    }

    // ---- ARM B: round-robin partition --------------------------------------
    {
        std::vector<Organism> pop;
        for (std::size_t i = 0; i < N; ++i) pop.emplace_back(kWin);
        const std::size_t share = G / N;
        double tb = 0.0; std::size_t tn = 0;
        const auto t0 = clock_t_::now();
        for (std::size_t w = 0; w < train.size(); ++w) {
            Organism& o = pop[w % N];
            const double an = o.feed(train[w], o.segments() < share);
            if (w + tail >= train.size()) { tb += an; ++tn; }
        }
        // Held-out: scored by the best organism for each window -- the
        // population's answer is its best member's answer.
        double hb = 0.0;
        std::size_t segs = 0;
        for (auto& o : pop) segs += o.segments();
        for (const auto& w : held) {
            double best = 1.0;
            for (auto& o : pop) best = std::min(best, o.feed(w, false));
            hb += best;
        }
        ArmResult r{"B partition", tn ? tb / tn : 1.0,
                    held.empty() ? 1.0 : hb / held.size(), segs,
                    std::chrono::duration<double, std::milli>(clock_t_::now() - t0).count()};
        report(r, G);
    }

    // ---- ARM C: competitive claim ------------------------------------------
    {
        std::vector<Organism> pop;
        for (std::size_t i = 0; i < N; ++i) pop.emplace_back(kWin);
        const std::size_t share = G / N;
        double tb = 0.0; std::size_t tn = 0;
        const auto t0 = clock_t_::now();
        for (std::size_t w = 0; w < train.size(); ++w) {
            // Every organism scores the window dry; the best predictor claims
            // and learns it. Claiming is the niche forming.
            // Claim by best dry-run prediction. Ties are broken RANDOMLY:
            // with the strict < the first index claimed every window while all
            // organisms scored 1.0, and one organism monopolised the stream --
            // the mixture-of-experts "expert collapse" failure, reproduced here
            // on the very first sanity run. Random ties mean routing degrades
            // to dealing when there is no signal, which is the honest floor.
            std::size_t best_i = 0, ties = 0;
            double best_a = 2.0;
            for (std::size_t i = 0; i < pop.size(); ++i) {
                const double a = pop[i].feed(train[w], false);
                if (a < best_a - 1e-9)      { best_a = a; best_i = i; ties = 1; }
                else if (a < best_a + 1e-9) { if (rnd() % ++ties == 0) best_i = i; }
            }
            Organism& o = pop[best_i];
            ++o.claimed;
            const double an = o.feed(train[w], o.segments() < share);
            if (w + tail >= train.size()) { tb += an; ++tn; }
        }
        double hb = 0.0;
        std::size_t segs = 0;
        for (auto& o : pop) segs += o.segments();
        for (const auto& w : held) {
            double best = 1.0;
            for (auto& o : pop) best = std::min(best, o.feed(w, false));
            hb += best;
        }
        std::string claims = "claims:";
        for (auto& o : pop) {
            char buf[12];
            std::snprintf(buf, sizeof buf, " %zu", o.claimed);
            claims += buf;
        }
        ArmResult r{"C competition", tn ? tb / tn : 1.0,
                    held.empty() ? 1.0 : hb / held.size(), segs,
                    std::chrono::duration<double, std::milli>(clock_t_::now() - t0).count(),
                    claims};
        report(r, G);
    }

    // ---- ARM D: competition + birth and death ------------------------------
    {
        // Diverse heritable depths: the population starts spread across the
        // order/recurrence trade and selection decides.
        std::vector<Organism> pop;
        const std::size_t seed_depths[] = {2, 3, 4, 5, 6, 7, 8, 8};
        for (std::size_t i = 0; i < N; ++i)
            pop.emplace_back(seed_depths[i % 8]);

        const std::size_t epoch = std::max<std::size_t>(1, train.size() / 8);
        double tb = 0.0; std::size_t tn = 0;
        std::string history;
        const auto t0 = clock_t_::now();

        for (std::size_t w = 0; w < train.size(); ++w) {
            std::size_t best_i = 0, ties = 0;
            double best_a = 2.0;
            for (std::size_t i = 0; i < pop.size(); ++i) {
                const double a = pop[i].feed(train[w], false);
                if (a < best_a - 1e-9)      { best_a = a; best_i = i; ties = 1; }
                else if (a < best_a + 1e-9) { if (rnd() % ++ties == 0) best_i = i; }
            }
            Organism& o = pop[best_i];
            // The pool is GLOBAL: an organism may grow while the population as
            // a whole is under budget. Death frees its share back to the pool.
            std::size_t total = 0;
            for (auto& p : pop) total += p.segments();
            const double an = o.feed(train[w], total < G);
            o.claimed_anomaly += an;
            if (w + tail >= train.size()) { tb += an; ++tn; }

            // Selection at each epoch boundary: the worst-predicting organism
            // dies (segments freed), the best spawns a mutated child.
            if ((w + 1) % epoch == 0) {
                std::size_t worst = 0, best = 0;
                double worst_f = -1.0, best_f = 2.0;
                for (std::size_t i = 0; i < pop.size(); ++i) {
                    // Unclaimed organisms are useless by definition.
                    const double f = pop[i].claimed >= 5
                        ? pop[i].claimed_anomaly / pop[i].claimed : 1.0;
                    if (f > worst_f) { worst_f = f; worst = i; }
                    if (f < best_f)  { best_f = f;  best = i; }
                }
                if (worst != best && worst_f > best_f + 0.02) {
                    const std::size_t parent_depth = pop[best].depth;
                    // Mutate the depth gene by one step, clamped.
                    std::size_t child_depth = parent_depth;
                    const std::uint64_t r = rnd() % 3;
                    if (r == 0 && child_depth > 2) --child_depth;
                    if (r == 1 && child_depth < 8) ++child_depth;
                    pop[worst] = Organism(child_depth);
                    char buf[32];
                    std::snprintf(buf, sizeof buf, " d%zu->d%zu",
                                  parent_depth, child_depth);
                    history += buf;
                }
                for (auto& p : pop) { p.claimed_anomaly = 0.0; p.claimed = 0; }
            }
        }
        double hb = 0.0;
        std::size_t segs = 0;
        for (auto& o : pop) segs += o.segments();
        for (const auto& w : held) {
            double best = 1.0;
            for (auto& o : pop) best = std::min(best, o.feed(w, false));
            hb += best;
        }
        std::string depths = "depths:";
        for (auto& o : pop) {
            char buf[8];
            std::snprintf(buf, sizeof buf, " %zu", o.depth);
            depths += buf;
        }
        ArmResult r{"D selection", tn ? tb / tn : 1.0,
                    held.empty() ? 1.0 : hb / held.size(), segs,
                    std::chrono::duration<double, std::milli>(clock_t_::now() - t0).count(),
                    depths};
        report(r, G);
        if (!history.empty())
            std::printf("    births:%s\n", history.c_str());
    }

    std::printf("\n  Burst is mean anomaly over the steps that could predict:\n");
    std::printf("  1.000 = every context novel (pure memorisation), lower = the\n");
    std::printf("  stream is being anticipated. Held-out windows come from the\n");
    std::printf("  same books, never trained, scored by each arm's best member.\n");
    std::printf("\n  The kill criteria for these arms were written in\n");
    std::printf("  docs/SPEC-v2-population.md before this file existed.\n");
    return 0;
}
