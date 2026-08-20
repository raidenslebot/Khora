// CALIBRATED NOVELTY — can Khora tell you what it has never seen?
//
// This is the capability that separates a memory from a model, and it is the
// one an LLM structurally cannot provide. An LLM's confidence is a property of
// its output distribution: it is an inference ABOUT THE WORLD, and it is
// famously uncorrelated with whether a specific fact was in its input. There is
// no mechanism in it that reports "this exact thing was never presented to me."
//
// Bursting is that mechanism, and it is not an inference at all. A column
// bursts when no distal segment anywhere in it matches the current context --
// literally a count of "nothing I have stored predicts this". The fraction of
// active columns bursting is therefore a statement about the system's own
// memory, obtained in the same pass as the prediction, with no second model, no
// calibration set, and no threshold fitted after the fact.
//
// THE TEST IS THE HARD VERSION. Unseen facts are built from the SAME subjects,
// relations and objects as the seen ones -- only the combination is new. A
// system that spotted novelty by noticing an unfamiliar word would score zero
// here. It has to report that this particular pairing is one it never had.
//
// Each fact is presented EXACTLY ONCE. No epochs, no replay, no second pass.
// That is what the fast store is for.

#include "khora/cortex/temporal_memory.hpp"
#include "khora/lattice/sdr.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace khora::cortex;
using khora::lattice::Sdr;
using khora::lattice::bind;
using khora::lattice::permute;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("  FAIL: %s\n", what); ++failures; }
    else         std::printf("  ok  : %s\n", what);
}

std::uint64_t rs = 0x51A7E5EEDULL;
std::uint64_t rnd() {
    std::uint64_t z = (rs += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

struct Fact { int s, r, o; };

Sdr atom(const char* kind, int i) {
    return Sdr::from_hash(std::string(kind) + std::to_string(i));
}

// The query key is (subject, relation) bound together. Permuting one operand
// first because block binding, like XOR, is commutative -- without it (s,r) and
// (r,s) would be the same key.
Sdr key_of(const Fact& f) {
    return bind(permute(atom("subj", f.s), 1), atom("rel", f.r));
}

// Present key then object; the anomaly on the SECOND step is the answer. If
// this (subject, relation) has been seen with this object, the object's columns
// were primed and almost nothing bursts. If the pairing is new, they were not.
double novelty_of(TemporalMemory& tm, const Fact& f) {
    tm.reset();
    tm.compute(key_of(f), false);
    return tm.compute(atom("obj", f.o), false).anomaly;
}

// Area under the ROC curve, by the rank-sum identity. 1.0 is perfect
// separation, 0.5 is a coin flip.
double auc(std::vector<double> pos, std::vector<double> neg) {
    std::vector<std::pair<double, int>> all;
    for (const double v : pos) all.emplace_back(v, 1);
    for (const double v : neg) all.emplace_back(v, 0);
    std::sort(all.begin(), all.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    // Average ranks over ties, so a degenerate all-equal score reads 0.5.
    double rank_sum = 0.0;
    std::size_t i = 0;
    while (i < all.size()) {
        std::size_t j = i;
        while (j + 1 < all.size() && all[j + 1].first == all[i].first) ++j;
        const double avg_rank = (static_cast<double>(i + j) / 2.0) + 1.0;
        for (std::size_t k = i; k <= j; ++k) if (all[k].second == 1) rank_sum += avg_rank;
        i = j + 1;
    }
    const double n1 = static_cast<double>(pos.size());
    const double n0 = static_cast<double>(neg.size());
    return (rank_sum - n1 * (n1 + 1) / 2.0) / (n1 * n0);
}

} // namespace

int main() {
    std::printf("Calibrated novelty test\n");

    // A closed vocabulary. Every atom below appears in BOTH the seen and the
    // unseen sets, so only the combination can distinguish them.
    constexpr int kSubjects = 40, kRelations = 8, kObjects = 40;
    constexpr int kSeen = 300, kProbe = 300;

    // --- one-shot learning is a prerequisite --------------------------------
    {
        TemporalMemory slow(TemporalMemoryConfig::semantic());
        TemporalMemory fast(TemporalMemoryConfig::episodic());
        const Fact f{1, 1, 1};
        for (TemporalMemory* tm : {&slow, &fast}) {
            tm->reset();
            tm->compute(key_of(f), true);
            tm->compute(atom("obj", f.o), true);
        }
        const double slow_after_one = novelty_of(slow, f);
        const double fast_after_one = novelty_of(fast, f);
        std::printf("  after ONE presentation: slow store anomaly %.2f, fast store %.2f\n",
                    slow_after_one, fast_after_one);
        check(fast_after_one < 0.1, "the fast store knows a fact after a single exposure");
        check(slow_after_one > 0.9,
              "the slow store does NOT -- it learns what recurs, by design");
    }

    // --- the benchmark -------------------------------------------------------
    TemporalMemory tm(TemporalMemoryConfig::episodic());

    // THE HARD VERSION, and the only one worth reporting.
    //
    // Every held-out fact reuses a (subject, relation) key that WAS streamed,
    // paired with a different object. So the key itself is always familiar: the
    // first step never bursts, and the system cannot answer by noticing an
    // unfamiliar term. It has to report that this SPECIFIC pairing is one it
    // never had -- which is the whole claim.
    //
    // Drawing held-out facts freely from the same vocabulary, as a first
    // version did, is much easier: with 40 subjects and 8 relations there are
    // only 320 keys, so most held-out facts carry a key never seen at all and
    // burst at step one. That version scores AUC 1.0000 and means far less.
    std::vector<Fact> seen, held;
    {
        std::vector<char> used_key(static_cast<std::size_t>(kSubjects) * kRelations, 0);
        while (seen.size() < static_cast<std::size_t>(kSeen)) {
            const int s = static_cast<int>(rnd() % kSubjects);
            const int r = static_cast<int>(rnd() % kRelations);
            const std::size_t k = static_cast<std::size_t>(s) * kRelations + r;
            if (used_key[k]) continue;
            used_key[k] = 1;
            seen.push_back({s, r, static_cast<int>(rnd() % kObjects)});
        }
        // Same keys, different objects.
        for (int i = 0; i < kProbe; ++i) {
            const Fact& base = seen[static_cast<std::size_t>(i) % seen.size()];
            int o = static_cast<int>(rnd() % kObjects);
            if (o == base.o) o = (o + 1) % kObjects;
            held.push_back({base.s, base.r, o});
        }
    }

    // Stream each seen fact EXACTLY ONCE.
    for (const Fact& f : seen) {
        tm.reset();
        tm.compute(key_of(f), true);
        tm.compute(atom("obj", f.o), true);
    }
    std::printf("  streamed %d facts once each over a vocabulary of %d/%d/%d"
                " -> %zu segments\n",
                kSeen, kSubjects, kRelations, kObjects, tm.segment_count());

    // Probe. Low novelty should mean seen, high should mean never seen.
    std::vector<double> nov_seen, nov_held;
    for (const Fact& f : seen) nov_seen.push_back(novelty_of(tm, f));
    for (const Fact& f : held) nov_held.push_back(novelty_of(tm, f));

    const auto mean = [](const std::vector<double>& v) {
        double s = 0.0; for (const double x : v) s += x; return s / v.size();
    };
    const double a = auc(nov_held, nov_seen);   // held-out is the positive class
    std::printf("  mean novelty: seen %.3f   held-out %.3f\n",
                mean(nov_seen), mean(nov_held));
    std::printf("  AUC separating never-seen from seen: %.4f\n", a);

    check(a >= 0.90, "novelty separates seen from never-seen at AUC >= 0.90");
    check(mean(nov_held) > mean(nov_seen) + 0.5,
          "and the gap is large, not a marginal ranking");

    // The retention question: does a fact learned EARLY survive the 299 that
    // followed it? This is the catastrophic-interference failure mode, and a
    // one-shot store is exactly where it should bite hardest.
    {
        const std::size_t first_tenth = seen.size() / 10;
        double early = 0.0, late = 0.0;
        for (std::size_t i = 0; i < first_tenth; ++i) early += nov_seen[i];
        for (std::size_t i = nov_seen.size() - first_tenth; i < nov_seen.size(); ++i)
            late += nov_seen[i];
        early /= first_tenth;
        late  /= first_tenth;
        std::printf("  mean novelty of the FIRST 10%% of facts %.3f,"
                    " of the LAST 10%% %.3f\n", early, late);
        check(early < 0.2, "facts learned first are still known after 270 more arrived");
    }

    // Calibration, not just ranking: a usable threshold must exist, and it
    // should not need to be searched for. Bursting is already a fraction.
    {
        int tp = 0, fp = 0;
        for (const double v : nov_held) if (v > 0.5) ++tp;
        for (const double v : nov_seen) if (v > 0.5) ++fp;
        std::printf("  at the natural threshold of 0.5: %d/%zu held-out flagged,"
                    " %d/%zu seen wrongly flagged\n",
                    tp, nov_held.size(), fp, nov_seen.size());
        check(tp > static_cast<int>(nov_held.size()) * 9 / 10,
              "the natural 0.5 threshold catches over 90% of never-seen facts");
        check(fp < static_cast<int>(nov_seen.size()) / 10,
              "and wrongly flags under 10% of seen ones");
    }

    std::printf("\n");
    if (failures == 0) std::printf("ALL PASS\n");
    else               std::printf("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
