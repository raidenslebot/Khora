// DOES THE TEMPORAL MEMORY BOUND ON REAL LANGUAGE?
//
// This is the gate on wiring it into khora.exe, and it has to be answered
// before rather than after.
//
// TemporalMemory allocates on failure. A minicolumn that is driven with nothing
// primed BURSTS, and one of its cells grows a new distal segment to learn the
// context it did not recognise. That is the right behaviour and it is what
// makes one-shot sequence learning work -- but it means the segment count is
// driven entirely by how often the system meets a context it has never seen.
//
// On the synthetic sequences every earlier test used, contexts repeat by
// construction, so allocation stops once the sequences are learned. Natural
// language is not like that. Its contexts are close to unique: an eight-word
// window from a book will, in general, never occur again in that exact form.
// If every window is novel then every step bursts and every step allocates, and
// the structure grows linearly with the corpus forever.
//
// There is already a hint that this is happening. The in-vivo benchmark learned
// 5,400 words and finished with 894,108 segments -- about 165 segments per step
// against a theoretical maximum of 256, which means roughly two thirds of all
// columns were bursting even at the end of training. That is not a system that
// has learned to predict; it is a system still memorising.
//
// So: feed real book text and watch the MARGINAL rate. If segments-per-token
// falls as the corpus grows, the structure is converging and wiring it in is
// safe. If the rate holds flat, growth is linear and unbounded, and the module
// needs a capacity bound and an eviction policy before it goes anywhere near a
// process that is meant to run for hours.
//
// The relevant prior is that the cortex it is modelled on does not solve this by
// growing without limit either. Synapse counts peak in early childhood and are
// then cut by roughly half through adolescence; the adult number is stable.
// Allocation without eviction is not the biological arrangement, it is only the
// first half of it.

#include "khora/cortex/temporal_memory.hpp"
#include "khora/lattice/sdr.hpp"
#include "khora/plexus/plexus.hpp"
#include "khora/reservoir/reservoir.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

using namespace khora::cortex;
using khora::lattice::Sdr;
using clock_t_ = std::chrono::high_resolution_clock;

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

// THE SECOND ARM. A word's code as the SET of its strongest associates, which
// is the representation that scored 0.6168 AUC against WordNet where raw
// affinity managed 0.5298. Two words used in similar company come out with
// overlapping codes, so a context can recur in CODE space even when the exact
// words never recur -- which is the only way a sequence memory can generalise
// over prose.
//
// This is also the biologically ordinary arrangement. Cortex is not handed a
// random vector per word; it receives the output of upstream areas that have
// already encoded similarity, and the column only ever sees that.
khora::lattice::Sdr context_code(const khora::plexus::Plexus& plex,
                                 const std::string& w, std::size_t top_k) {
    const auto id = plex.has(w) ? static_cast<long long>(0) : -1;
    (void)id;
    if (!plex.has(w)) return khora::lattice::Sdr::from_hash("w:" + w);
    const auto kin = plex.associates(w, top_k);
    if (kin.empty()) return khora::lattice::Sdr::from_hash("w:" + w);
    khora::lattice::Trace t;
    for (const auto& [name, score] : kin) {
        (void)score;
        t.add(khora::lattice::Sdr::from_hash("ctx:" + name));
    }
    // The word's own identity stays in the code, so two words with similar
    // company are similar without being identical.
    t.add(khora::lattice::Sdr::from_hash("w:" + w));
    return t.binarise();
}

// Rough resident cost of the structure. A segment carries a fixed 40-slot
// synapse array whether it uses it or not, so this is the honest figure rather
// than synapses-times-a-few-bytes.
double approx_mb(std::size_t segments) {
    const double per_segment = 4.0 + 1.0 + 40.0 * 4.0 + 40.0 * 1.0 + 1.0;  // ~206 B
    return segments * per_segment / 1048576.0;
}

// One arm: feed the stream through a fresh TemporalMemory with a given encoder,
// reporting on a geometric schedule because the SHAPE of the curve is the point.
template <class Encode>
void run_arm(const char* label, const std::vector<std::string>& stream,
             std::size_t win, Encode encode) {
    std::printf("\n  %s\n", label);
    std::printf("  tokens | segments | marginal seg/tok | burst frac |   RAM    | ms/step\n");
    std::printf("  -------+----------+------------------+------------+----------+--------\n");

    TemporalMemory tm;
    std::size_t last_tokens = 0, last_segments = 0;
    double burst_sum = 0.0;
    std::size_t burst_n = 0;
    const auto t0 = clock_t_::now();

    for (std::size_t i = 0; i + win <= stream.size(); i += win) {
        tm.reset();
        for (std::size_t k = 0; k < win; ++k) {
            const auto st = tm.compute(encode(stream[i + k]), true);
            if (k > 0) { burst_sum += st.anomaly; ++burst_n; }
        }
        const std::size_t done = i + win;
        if (done >= last_tokens * 2 || done + win > stream.size()) {
            const double ms = std::chrono::duration<double, std::milli>(
                                  clock_t_::now() - t0).count();
            const std::size_t segs = tm.segment_count();
            const double marginal =
                (done > last_tokens)
                    ? static_cast<double>(segs - last_segments) / (done - last_tokens)
                    : 0.0;
            std::printf("  %6zu | %8zu | %16.1f | %10.3f | %6.1f MB | %6.2f\n",
                        done, segs, marginal, burst_n ? burst_sum / burst_n : 0.0,
                        approx_mb(segs), ms / done);
            last_tokens = done;
            last_segments = segs;
            burst_sum = 0.0; burst_n = 0;
        }
    }
    std::printf("  final: %zu segments for %zu tokens = %.1f per token, %.0f MB\n",
                tm.segment_count(), stream.size(),
                static_cast<double>(tm.segment_count()) / stream.size(),
                approx_mb(tm.segment_count()));
}

} // namespace

int main(int argc, char** argv) {
    const std::string dir  = (argc > 1) ? argv[1] : "data/reservoir";
    const std::size_t cap  = (argc > 2) ? std::stoul(argv[2]) : 24000;
    const std::size_t win  = (argc > 3) ? std::stoul(argv[3]) : 8;

    khora::reservoir::Reservoir res(dir);
    res.load_catalog();
    const auto cat = res.catalog();
    if (cat.empty()) { std::printf("no tomes at %s\n", dir.c_str()); return 1; }

    // One long stream of real prose, several books deep.
    std::vector<std::string> stream;
    for (const auto& t : cat) {
        if (stream.size() >= cap) break;
        auto text = res.read(t.title);
        if (!text || text->size() < 20000) continue;
        auto ws = tokenize(*text, cap - stream.size());
        stream.insert(stream.end(), ws.begin(), ws.end());
    }
    std::printf("Does the temporal memory bound on real language?\n\n");
    std::printf("  %zu tokens of real prose, %zu-word windows\n\n", stream.size(), win);
    if (stream.size() < 2000) { std::printf("  not enough text\n"); return 1; }

    // ARM 1: an independent random code per word -- what every earlier test
    // used, and what the in-vivo bench used.
    run_arm("ARM 1 -- independent random code per word",
            stream, win,
            [](const std::string& w) { return Sdr::from_hash("w:" + w); });

    // ARM 2: the same stream, encoded so words used in similar company have
    // overlapping codes.
    khora::plexus::Plexus plex;
    plex.load("data/plexus_archive/main");
    if (plex.vocabulary_size() == 0) {
        std::printf("\n  (no plexus at data/plexus_archive/main -- arm 2 skipped)\n");
        return 0;
    }
    std::printf("\n  (arm 2 encoder: %zu-word learned graph, codes cached)\n",
                plex.vocabulary_size());
    std::unordered_map<std::string, Sdr> cache;
    run_arm("ARM 2 -- code = the set of a word's strongest associates",
            stream, win,
            [&](const std::string& w) -> Sdr {
                auto it = cache.find(w);
                if (it != cache.end()) return it->second;
                return cache.emplace(w, context_code(plex, w, 16)).first->second;
            });

    std::printf("\n  READ THE MARGINAL COLUMN. If it falls, contexts are recurring and\n");
    std::printf("  the structure is converging. If it holds flat, every window is\n");
    std::printf("  novel, every step bursts, and growth is linear in the corpus --\n");
    std::printf("  which for a process meant to run for hours is not a tuning issue\n");
    std::printf("  but a missing mechanism. Cortex does not solve this by growing\n");
    std::printf("  without limit either: synapse density peaks in early childhood\n");
    std::printf("  and is roughly halved through adolescence. Allocation without\n");
    std::printf("  eviction is only the first half of the biological arrangement.\n");
    return 0;
}
