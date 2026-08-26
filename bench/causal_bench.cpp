// DOES "CAUSES" IN THIS SYSTEM MEAN ANYTHING CAUSAL?
//
// The Ligature holds fourteen thousand typed relations pulled from real books,
// and one of its three types is CAUSES. `deduce` chains those edges
// transitively, `plan_to` searches backward over them to answer "what would
// bring this about", and both are presented as reasoning about causes.
//
// Nothing in the extractor distinguishes causation from co-occurrence. It fires
// on the surface verb -- "X causes Y", "X leads to Y" -- and that is a claim the
// AUTHOR made, filtered through a pattern match. An audit of this tree found no
// intervention, no counterfactual, no confounding, no adjustment: not one line
// anywhere that knows the difference between seeing and doing.
//
// This measures two separate things.
//
// PART ONE builds the capability and shows why it is needed, on data where the
// truth is known by construction. A confounder makes two variables correlate
// perfectly while neither causes the other; observational statistics cannot tell
// that apart from causation, and the backdoor adjustment can. That is the whole
// of Pearl's point and it takes about forty lines.
//
// PART TWO turns the question on Khora's own graph. There is no interventional
// ground truth for a corpus of Victorian books, so the test has to be one that
// needs none, and there are two:
//
//   ANTISYMMETRY. Causation has a direction. If the graph asserts causes(a,b)
//   and causes(b,a) at any real rate, whatever it is measuring is not direction.
//
//   INDEPENDENCE FROM MERE ASSOCIATION. If the causal edges are predicted by
//   plain co-occurrence, they carry no information that the Plexus did not
//   already have, and calling them causal adds nothing but a name.
//
// Neither test can prove the edges ARE causal. Both can show they are not, which
// is the useful direction.

#include "khora/ligature/ligature.hpp"
#include "khora/plexus/plexus.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

std::uint64_t g_s = 20260827;
std::uint64_t rnd() { g_s ^= g_s << 13; g_s ^= g_s >> 7; g_s ^= g_s << 17; return g_s; }
double uni() { return (double)(rnd() >> 11) / 9007199254740992.0; }
bool coin(double p) { return uni() < p; }

// A 95% Wilson interval, the same convention every other bench here uses.
std::pair<double, double> wilson(std::size_t k, std::size_t n) {
    if (n == 0) return {0.0, 0.0};
    const double z = 1.96, p = (double)k / (double)n;
    const double d = 1.0 + z * z / (double)n;
    const double c = p + z * z / (2.0 * (double)n);
    const double m = z * std::sqrt(p * (1 - p) / (double)n + z * z / (4.0 * (double)n * (double)n));
    return {100.0 * (c - m) / d, 100.0 * (c + m) / d};
}

// --- PART ONE: a world where the answer is known ---------------------------
//
// Z is a common cause of both X and Y. X does NOT affect Y at all. Observing a
// high X tells you Z was high, which tells you Y was high, so X and Y correlate
// strongly -- and setting X changes nothing, because the arrow into Y comes from
// Z. This is the smallest structure where seeing and doing disagree.
struct Sample { int z, x, y; };

Sample observe_confounded() {
    Sample s;
    s.z = coin(0.5) ? 1 : 0;
    s.x = coin(s.z ? 0.9 : 0.1) ? 1 : 0;      // Z -> X
    s.y = coin(s.z ? 0.9 : 0.1) ? 1 : 0;      // Z -> Y,  and NO arrow X -> Y
    return s;
}

// The same world with X held down by force. The arrow into X is cut; everything
// downstream of Z is untouched. This is do(X = v), and it is a different world,
// not a subset of the observations.
Sample intervene_confounded(int v) {
    Sample s;
    s.z = coin(0.5) ? 1 : 0;
    s.x = v;                                   // the arrow into X is severed
    s.y = coin(s.z ? 0.9 : 0.1) ? 1 : 0;
    return s;
}

// A second world, identical in every observable way at the surface, where X
// really does cause Y. If the method cannot tell these two apart it is useless.
Sample observe_real_cause() {
    Sample s;
    s.z = coin(0.5) ? 1 : 0;
    s.x = coin(s.z ? 0.9 : 0.1) ? 1 : 0;
    s.y = coin(s.x ? 0.9 : 0.1) ? 1 : 0;       // Z -> X -> Y
    return s;
}
Sample intervene_real_cause(int v) {
    Sample s;
    s.z = coin(0.5) ? 1 : 0;
    s.x = v;
    s.y = coin(s.x ? 0.9 : 0.1) ? 1 : 0;
    return s;
}

double p_y_given_x(const std::vector<Sample>& d, int x) {
    std::size_t n = 0, k = 0;
    for (const auto& s : d) if (s.x == x) { ++n; k += (std::size_t)s.y; }
    return n ? (double)k / (double)n : 0.0;
}

// P(Y | do(X)) by adjusting for Z: sum over z of P(Y | X, z) P(z). The whole
// adjustment is this one line of arithmetic; what is hard is knowing that Z is
// the thing to sum over, and no amount of data tells you that.
double p_y_do_x_backdoor(const std::vector<Sample>& d, int x) {
    double total = 0.0;
    for (int z = 0; z < 2; ++z) {
        std::size_t nz = 0, n = 0, k = 0;
        for (const auto& s : d) {
            if (s.z == z) ++nz;
            if (s.z == z && s.x == x) { ++n; k += (std::size_t)s.y; }
        }
        if (!n || d.empty()) continue;
        total += ((double)k / (double)n) * ((double)nz / (double)d.size());
    }
    return total;
}

} // namespace

int main(int argc, char** argv) {
    const std::string lig_path = (argc > 1) ? argv[1] : "data/ligature_archive/main";
    const std::string plx_path = (argc > 2) ? argv[2] : "data/plexus_archive/main";

    std::printf("Causal — does \"causes\" in this system mean anything causal?\n\n");

    // =====================================================================
    std::printf("  === PART 1. SEEING IS NOT DOING, ON DATA WHERE THE TRUTH IS KNOWN ===\n\n");
    const std::size_t N = 200000;
    struct World {
        const char* name;
        Sample (*obs)();
        Sample (*act)(int);
        bool   x_causes_y;
    };
    const World worlds[] = {
        {"Z->X, Z->Y (confounded, X does NOT cause Y)", observe_confounded,  intervene_confounded,  false},
        {"Z->X->Y   (X really does cause Y)",           observe_real_cause,  intervene_real_cause,  true},
    };

    std::printf("    world                                        | P(Y|X=1) | P(Y|X=0) | assoc |"
                " truth do(1) | truth do(0) |  true | backdoor(1) | backdoor(0) | adjusted\n");
    std::printf("    ---------------------------------------------+----------+----------+-------+"
                "-------------+-------------+-------+-------------+-------------+---------\n");
    for (const auto& w : worlds) {
        std::vector<Sample> d;
        d.reserve(N);
        g_s = 11111;
        for (std::size_t i = 0; i < N; ++i) d.push_back(w.obs());

        const double a1 = p_y_given_x(d, 1), a0 = p_y_given_x(d, 0);

        // The ground truth: actually run the intervention. Available here only
        // because this is a simulation; in the world it is the expensive thing
        // the adjustment exists to avoid.
        std::size_t k1 = 0, k0 = 0;
        g_s = 22222;
        for (std::size_t i = 0; i < N; ++i) k1 += (std::size_t)w.act(1).y;
        g_s = 33333;
        for (std::size_t i = 0; i < N; ++i) k0 += (std::size_t)w.act(0).y;
        const double t1 = (double)k1 / (double)N, t0 = (double)k0 / (double)N;

        const double b1 = p_y_do_x_backdoor(d, 1), b0 = p_y_do_x_backdoor(d, 0);

        std::printf("    %-44s |   %.3f  |   %.3f  | %+.3f |    %.3f    |    %.3f    | %+.3f |"
                    "    %.3f    |    %.3f    |  %+.3f\n",
                    w.name, a1, a0, a1 - a0, t1, t0, t1 - t0, b1, b0, b1 - b0);
    }
    std::printf("\n    Read the three effect columns. ASSOC is what a correlation sees, TRUE is\n"
                "    what actually happens when you intervene, ADJUSTED is the backdoor estimate\n"
                "    from observation alone. In the confounded world association reports a large\n"
                "    effect and the truth is zero; the adjustment recovers the zero. In the\n"
                "    second world all three agree, which is the check that the adjustment is not\n"
                "    simply always answering nothing.\n\n");

    // =====================================================================
    std::printf("  === PART 2. AND WHAT ABOUT KHORA'S OWN CAUSAL EDGES ===\n\n");
    khora::ligature::Ligature lig;
    lig.load(lig_path);
    if (lig.triple_count() == 0) {
        std::printf("    no ligature archive at %s -- run plexus_forge first\n", lig_path.c_str());
        return 0;
    }

    // --- antisymmetry -----------------------------------------------------
    //
    // Causation has a direction. Nothing in the extractor enforces one, so the
    // question is whether the corpus happens to.
    const auto all = lig.all(1);
    std::size_t causal = 0, both_ways = 0;
    std::unordered_set<std::string> edge;
    for (const auto& t : all)
        if (t.rel == khora::ligature::Relation::Causes)
            edge.insert(t.subject + "\x1f" + t.object);
    for (const auto& t : all) {
        if (t.rel != khora::ligature::Relation::Causes) continue;
        ++causal;
        if (edge.count(t.object + "\x1f" + t.subject)) ++both_ways;
    }
    const auto aci = wilson(both_ways, causal);
    std::printf("    %zu causal edges. Both directions asserted: %zu (%.2f%%) [%.2f, %.2f]\n",
                causal, both_ways, causal ? 100.0 * (double)both_ways / (double)causal : 0.0,
                aci.first, aci.second);

    // DOES THAT TEST HAVE ANY POWER? Zero reciprocity is only evidence of
    // direction if a graph WITHOUT direction would have shown some. At this
    // sparsity it might not: with a few thousand edges over a few thousand
    // words, the chance that any particular reverse edge happens to exist is
    // tiny, so zero is what an undirected process would produce as well.
    //
    // The null is a degree-preserving shuffle: keep every subject and every
    // object exactly as often as they really appear, destroy only the pairing,
    // and count reciprocity in that. If the null also gives zero, the test
    // cannot distinguish anything and the clean result above means nothing.
    {
        std::vector<std::string> ss, oo;
        for (const auto& t : all)
            if (t.rel == khora::ligature::Relation::Causes) { ss.push_back(t.subject); oo.push_back(t.object); }
        double null_mean = 0.0;
        std::size_t null_max = 0;
        const int trials = 200;
        g_s = 4242;
        for (int r = 0; r < trials; ++r) {
            for (std::size_t i = oo.size(); i > 1; --i) std::swap(oo[i - 1], oo[rnd() % i]);
            std::unordered_set<std::string> e2;
            for (std::size_t i = 0; i < ss.size(); ++i) e2.insert(ss[i] + "\x1f" + oo[i]);
            std::size_t rec = 0;
            for (std::size_t i = 0; i < ss.size(); ++i)
                if (e2.count(oo[i] + "\x1f" + ss[i])) ++rec;
            null_mean += (double)rec;
            null_max = std::max(null_max, rec);
        }
        null_mean /= (double)trials;
        std::printf("      the null: same subjects, same objects, pairing shuffled, %d times\n", trials);
        std::printf("        reciprocal edges expected by chance alone: %.2f (worst of %d: %zu)\n",
                    null_mean, trials, null_max);
        // AND IS ZERO ACTUALLY SURPRISING? Under the null the count is roughly
        // Poisson, so the probability of seeing this few by chance alone is what
        // decides it -- not whether the mean happens to exceed one. Saying "the
        // null is above one, so zero is a real signal" was the first thing I
        // wrote here and it is wrong: at a mean of 2 an honest coin gives zero
        // about one time in seven.
        double pois = std::exp(-null_mean), term = pois;
        for (std::size_t k = 1; k <= both_ways; ++k) { term *= null_mean / (double)k; pois += term; }
        std::printf("        probability of seeing this few by chance alone: %.3f%s\n",
                    pois, pois < 0.05 ? "  -- significant" : "  -- NOT significant");
        if (pois >= 0.05)
            std::printf("        So the graph may well carry direction, and this test cannot\n"
                        "        show it. The corpus is too sparse for reciprocity to be\n"
                        "        informative; a stronger test would need pairs where both\n"
                        "        directions had a real chance to appear.\n");
    }
    std::printf("\n");

    // --- is a causal edge anything more than a co-occurrence? -------------
    khora::plexus::Plexus plex;
    plex.load(plx_path);
    if (plex.vocabulary_size() == 0) {
        std::printf("    no plexus archive at %s -- the association comparison is skipped\n",
                    plx_path.c_str());
        return 0;
    }

    // Build a co-occurrence lookup for the pairs we care about.
    std::unordered_map<std::string, std::size_t> id;
    for (std::size_t i = 0; i < plex.vocabulary_size(); ++i)
        id.emplace(std::string(plex.node_name(i)), i);
    auto cooc = [&](const std::string& a, const std::string& b) -> long {
        const auto ia = id.find(a);
        if (ia == id.end()) return -1;
        const auto ib = id.find(b);
        if (ib == id.end()) return -1;
        for (const auto& [n, c] : plex.neighbours(ia->second))
            if (n == ib->second) return (long)c;
        return 0;   // both known, never seen together
    };

    // For each causal edge, is the pair also a strong association? And for a
    // matched set of RANDOM pairs drawn from the same words, what does that rate
    // look like? Without the second number the first means nothing.
    std::size_t checked = 0, assoc = 0;
    std::vector<std::string> subs, objs;
    for (const auto& t : all) {
        if (t.rel != khora::ligature::Relation::Causes) continue;
        const long c = cooc(t.subject, t.object);
        if (c < 0) continue;
        ++checked;
        if (c > 0) ++assoc;
        subs.push_back(t.subject);
        objs.push_back(t.object);
    }
    // The control: the SAME subjects against SHUFFLED objects, so vocabulary and
    // frequency are held fixed and only the pairing is destroyed.
    std::size_t sh_checked = 0, sh_assoc = 0;
    for (std::size_t i = 0; i < subs.size(); ++i) {
        const long c = cooc(subs[i], objs[(i * 7919 + 13) % objs.size()]);
        if (c < 0) continue;
        ++sh_checked;
        if (c > 0) ++sh_assoc;
    }
    const auto e1 = wilson(assoc, checked);
    const auto e2 = wilson(sh_assoc, sh_checked);
    std::printf("    of %zu causal pairs where both words are in the Plexus,\n"
                "      %zu (%.1f%%) [%.1f, %.1f] also co-occur\n",
                checked, assoc, checked ? 100.0 * (double)assoc / (double)checked : 0.0,
                e1.first, e1.second);
    std::printf("    of %zu SHUFFLED pairs over the same words,\n"
                "      %zu (%.1f%%) [%.1f, %.1f] co-occur\n",
                sh_checked, sh_assoc,
                sh_checked ? 100.0 * (double)sh_assoc / (double)sh_checked : 0.0,
                e2.first, e2.second);
    std::printf("      If those two intervals overlap, the causal edges are no more associated\n"
                "      than a random pairing of the same words -- which would mean the extractor\n"
                "      is not even finding co-occurrence, let alone causation. If the first is\n"
                "      far higher, the edges ARE associations, which is necessary and nowhere\n"
                "      near sufficient for calling them causal.\n\n");

    std::printf("  WHAT NEITHER PART SHOWS. Part 1 is a simulation: the adjustment works there\n"
                "  because I told it that Z is the confounder, and discovering WHICH variable to\n"
                "  adjust for is the hard problem that no amount of data solves. Part 2 cannot\n"
                "  show the edges ARE causal -- there is no interventional ground truth for a\n"
                "  corpus of books, and there cannot be. It can only show whether they carry\n"
                "  direction and whether they carry anything association does not.\n");
    return 0;
}
