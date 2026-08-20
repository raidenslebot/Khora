// WHERE IS THE CEILING?
//
// A human brain has roughly 86 billion neurons, about 16 billion of them in
// cortex, and on the order of 10^14 synapses. Khora, as configured, has 16,384
// minicolumns of 32 cells -- 524,288 cells -- and grows synapses on demand.
// Five orders of magnitude down on cells, eight on synapses.
//
// The obvious response is to add cells. Both numbers here are compile-time
// constants and could be raised tomorrow. The question this benchmark exists
// to answer is whether that would BUY anything, and the answer is not a matter
// of opinion: load the current architecture until something breaks, and see
// what breaks first.
//
// Three candidate ceilings, and they call for completely different work:
//
//   CAPACITY -- recall degrades as facts accumulate. That is a cell-count
//               ceiling, and more tissue is the answer.
//   MEMORY   -- the process runs out of RAM before recall degrades. That is an
//               implementation ceiling: a segment here reserves 40 synapse
//               slots and typically uses 20.
//   TIME     -- steps per second falls off. That is an algorithmic ceiling.
//
// The Human Brain Project spent on the order of a billion euros establishing
// that simulating more tissue, without a mechanism that uses it, produces a
// description rather than a capability. So this measurement comes first.

#include "khora/cortex/temporal_memory.hpp"
#include "khora/lattice/sdr.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using clock_t_ = std::chrono::high_resolution_clock;
using namespace khora::cortex;
using khora::lattice::Sdr;
using khora::lattice::bind;
using khora::lattice::permute;

namespace {

std::uint64_t rs = 0x5CA1AB1EULL;
std::uint64_t rnd() {
    std::uint64_t z = (rs += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

struct Fact { int s, r, o; };
Sdr atom(const char* k, int i) { return Sdr::from_hash(std::string(k) + std::to_string(i)); }
Sdr key_of(const Fact& f) { return bind(permute(atom("subj", f.s), 1), atom("rel", f.r)); }

double novelty(TemporalMemory& tm, const Fact& f) {
    tm.reset();
    tm.compute(key_of(f), false);
    return tm.compute(atom("obj", f.o), false).anomaly;
}

double auc(const std::vector<double>& pos, const std::vector<double>& neg) {
    std::vector<std::pair<double, int>> all;
    for (const double v : pos) all.emplace_back(v, 1);
    for (const double v : neg) all.emplace_back(v, 0);
    std::sort(all.begin(), all.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    double rank_sum = 0.0;
    std::size_t i = 0;
    while (i < all.size()) {
        std::size_t j = i;
        while (j + 1 < all.size() && all[j + 1].first == all[i].first) ++j;
        const double avg = (static_cast<double>(i + j) / 2.0) + 1.0;
        for (std::size_t k = i; k <= j; ++k) if (all[k].second == 1) rank_sum += avg;
        i = j + 1;
    }
    const double n1 = static_cast<double>(pos.size()), n0 = static_cast<double>(neg.size());
    return (rank_sum - n1 * (n1 + 1) / 2.0) / (n1 * n0);
}

} // namespace

int main() {
    std::printf("Khora scaling study\n");
    std::printf("  architecture: %zu columns x %zu cells = %zu cells\n\n",
                kColumns, kCellsPerCol, kTotalCells);
    std::printf("  facts | AUC     | early recall | segments  |  RAM   | learn ms/step | probe ms\n");
    std::printf("  ------+---------+--------------+-----------+--------+---------------+---------\n");

    for (const int N : {100, 200, 400, 800, 1600}) {
        // Vocabulary scales with N so keys stay unique and the hard version of
        // the novelty task is preserved: held-out facts reuse a SEEN key.
        const int subjects = std::max(40, N / 4);
        const int relations = 8;
        const int objects = 64;

        TemporalMemory tm(TemporalMemoryConfig::episodic());
        std::vector<Fact> seen;
        std::vector<char> used(static_cast<std::size_t>(subjects) * relations, 0);
        while (static_cast<int>(seen.size()) < N) {
            const int s = static_cast<int>(rnd() % subjects);
            const int r = static_cast<int>(rnd() % relations);
            const std::size_t k = static_cast<std::size_t>(s) * relations + r;
            if (used[k]) continue;
            used[k] = 1;
            seen.push_back({s, r, static_cast<int>(rnd() % objects)});
        }

        const auto t0 = clock_t_::now();
        for (const Fact& f : seen) {
            tm.reset();
            tm.compute(key_of(f), true);
            tm.compute(atom("obj", f.o), true);
        }
        const double learn_ms =
            std::chrono::duration<double, std::milli>(clock_t_::now() - t0).count();

        // Probe a sample, so probing cost does not dominate at large N.
        const int sample = std::min(N, 120);
        std::vector<double> nv_seen, nv_held;
        const auto t1 = clock_t_::now();
        for (int i = 0; i < sample; ++i) {
            const Fact& f = seen[static_cast<std::size_t>(i) * seen.size() / sample];
            nv_seen.push_back(novelty(tm, f));
            Fact h = f;
            h.o = (h.o + 1 + static_cast<int>(rnd() % (objects - 1))) % objects;
            nv_held.push_back(novelty(tm, h));
        }
        const double probe_ms =
            std::chrono::duration<double, std::milli>(clock_t_::now() - t1).count() /
            (2.0 * sample);

        // Retention of the OLDEST tenth, after everything else arrived.
        double early = 0.0;
        const int tenth = std::min(40, std::max(1, N / 10));
        for (int i = 0; i < tenth; ++i) early += novelty(tm, seen[i]);
        early /= tenth;

        // Resident cost of the segment store, as actually laid out.
        const double mb =
            static_cast<double>(tm.segment_count()) *
            (sizeof(std::uint32_t) * 40 + sizeof(std::uint8_t) * 40 + 16) / (1024.0 * 1024.0);

        std::printf("  %5d | %.4f  |    %.3f     | %9zu | %5.0fM |     %6.3f    | %6.3f\n",
                    N, auc(nv_held, nv_seen), 1.0 - early, tm.segment_count(), mb,
                    learn_ms / (2.0 * N), probe_ms);
    }

    std::printf("\n  AUC 1.0 = never-seen and seen are perfectly separated.\n");
    std::printf("  early recall 1.0 = the oldest facts survive everything after them.\n");
    return 0;
}
