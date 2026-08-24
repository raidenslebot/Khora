// IS THE 93% BURST AN ARCHITECTURE NUMBER OR A REPRESENTATION NUMBER?
//
// This measurement comes before any more building, because it decides whether
// there is anything to build.
//
// The temporal memory allocates a segment whenever a driven column has nothing
// primed. On 24k tokens of prose it bursts on 93% of columns and grows to 2.9M
// segments -- unbounded, and not converging. The proposed answer was a
// population of specialists competing for a fixed budget.
//
// A survey of the prior art says that answer is misdiagnosed, and names the
// prior art it is a rediscovery of: Wilson's XCS (1995), item for item --
// accuracy-based fitness, covering-on-failure, a niche GA, a fixed population
// with niche-balanced deletion. It also says the failure being chased is not a
// one-module failure at all but ART's CATEGORY PROLIFERATION, which is a
// property of the MATCH CRITERION rather than of how many allocators there are.
// Splitting one allocator into eight changes how many things allocate; it does
// not change the probability that a given context matches something already
// stored. If 93% of contexts fail to match in one memory, they fail to match
// inside a specialist too.
//
// So the diagnostic, in three parts:
//
//   1. HOW UNIQUE ARE THE CONTEXTS, really? If essentially every n-token window
//      in the corpus occurs exactly once, then no matching rule can find a
//      recurrence that is not there, and the ceiling is in the data.
//
//   2. BURST AGAINST VIGILANCE. activation_threshold IS the vigilance
//      parameter: how many connected synapses must agree before a cell counts
//      as primed. Lower it and matching gets permissive. If burst falls sharply
//      as it drops, the 93% was a threshold choice and is fixable. If burst
//      stays flat across the whole sweep, it is not.
//
//   3. WHAT IT COSTS. A permissive threshold that stops bursting by matching
//      everything has not learned anything -- it has only stopped complaining.
//      So segments and false priming are reported alongside.
//
// The literature's prediction, stated before the run: burst stays near 93%
// across the sweep, because a segment keys on an exact conjunction of winner
// cells and the contexts genuinely do not recur. If that holds, the work is in
// the CONTEXT REPRESENTATION -- generalisation, backoff, variable order -- and
// the population idea is dead. Numenta reached the same conclusion when they
// went at natural language, and their answer was semantic folding: make similar
// words share bits so that similar contexts overlap.

#include "khora/cortex/temporal_memory.hpp"
#include "khora/lattice/sdr.hpp"
#include "khora/reservoir/reservoir.hpp"

#include <cctype>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace khora::cortex;
using khora::lattice::Sdr;

namespace {

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

} // namespace

int main(int argc, char** argv) {
    const std::string dir = (argc > 1) ? argv[1] : "data/reservoir";
    const std::size_t cap = (argc > 2) ? std::stoul(argv[2]) : 24000;

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
    std::printf("Is the 93%% burst architecture, or representation?\n\n");
    std::printf("  %zu tokens of real prose\n", stream.size());

    // ---- 1. How unique are the contexts? -----------------------------------
    //
    // If an n-token context occurs exactly once, nothing can predict from it,
    // and every occurrence must allocate. This is the ceiling in the DATA, and
    // it is set before any architecture is chosen.
    std::printf("\n  CONTEXT RECURRENCE IN THE CORPUS ITSELF\n");
    std::printf("    n | distinct | occurs once | recurring | max repeats\n");
    std::printf("   ---+----------+-------------+-----------+------------\n");
    for (const std::size_t n : {1u, 2u, 3u, 4u, 5u, 6u, 8u}) {
        std::unordered_map<std::string, std::size_t> counts;
        for (std::size_t i = 0; i + n <= stream.size(); ++i) {
            std::string key;
            for (std::size_t k = 0; k < n; ++k) { key += stream[i + k]; key += '\x1f'; }
            ++counts[key];
        }
        std::size_t once = 0, most = 0;
        for (const auto& [k, c] : counts) { if (c == 1) ++once; if (c > most) most = c; }
        const double once_frac = counts.empty() ? 0.0
            : static_cast<double>(once) / static_cast<double>(counts.size());
        std::printf("   %2zu | %8zu |   %6.2f%%    |  %6.2f%%  | %10zu\n",
                    n, counts.size(), 100.0 * once_frac, 100.0 * (1.0 - once_frac), most);
    }

    // ---- 2 and 3. Burst against vigilance, and what it costs ---------------
    //
    // activation_threshold is vigilance. Sweeping it is the ART diagnostic:
    // does permissive matching buy prediction, or only silence?
    std::printf("\n  BURST AGAINST VIGILANCE (activation_threshold)\n");
    std::printf("  Trained on 6000 tokens, 8-word windows. 'primed cols' is how\n");
    std::printf("  often a driven column had SOMETHING primed -- the complement of\n");
    std::printf("  burst -- and 'segments' is what that cost.\n\n");
    std::printf("    theta | burst | primed cols | segments | seg/token\n");
    std::printf("   -------+-------+-------------+----------+----------\n");

    const std::size_t train = std::min<std::size_t>(6000, stream.size());
    for (const int theta_i : {2, 4, 6, 8, 10, 13, 16, 20}) {
        const std::uint8_t theta = static_cast<std::uint8_t>(theta_i);
        TemporalMemoryConfig cfg;
        cfg.activation_threshold = theta;
        // min_threshold must stay at or below activation, or the bursting cell
        // can never find a partly-matching segment to teach.
        cfg.min_threshold = static_cast<std::uint8_t>(theta > 3 ? theta - 3 : 1);
        TemporalMemory tm(cfg);

        double burst = 0.0;
        std::size_t n = 0;
        for (std::size_t i = 0; i + 8 <= train; i += 8) {
            tm.reset();
            for (std::size_t k = 0; k < 8; ++k) {
                const auto st = tm.compute(Sdr::from_hash("w:" + stream[i + k]), true);
                if (k > 0) { burst += st.anomaly; ++n; }
            }
        }
        const double b = n ? burst / n : 1.0;
        std::printf("   %6u | %.3f |    %6.1f%%   | %8zu | %8.1f\n",
                    static_cast<unsigned>(theta), b, 100.0 * (1.0 - b),
                    tm.segment_count(),
                    static_cast<double>(tm.segment_count()) / train);
    }

    std::printf("\n  WHAT TO CONCLUDE\n");
    std::printf("    If burst is flat across the theta sweep, the 93%% is not a\n");
    std::printf("    threshold choice and no population of allocators will fix it:\n");
    std::printf("    a segment keys on an exact conjunction of winner cells, and\n");
    std::printf("    the contexts do not recur, so there is nothing to match. The\n");
    std::printf("    work would then be in the CONTEXT REPRESENTATION -- backoff,\n");
    std::printf("    variable order, or codes that let similar contexts overlap --\n");
    std::printf("    and SPEC-v2's population proposal is dead on arrival.\n");
    std::printf("\n    If burst falls steeply with theta, the diagnosis was wrong and\n");
    std::printf("    the module was simply too strict. Then read the segment column:\n");
    std::printf("    a threshold that stops bursting by matching everything has not\n");
    std::printf("    learned, it has only stopped complaining.\n");
    return 0;
}
