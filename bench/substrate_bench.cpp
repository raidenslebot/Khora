// TWO PROPERTIES THE SUBSTRATE IS SUPPOSED TO HAVE FOR FREE, MEASURED.
//
// The pitch for a 10,000-bit binary hypervector substrate is that graceful
// degradation and federated merge fall out of the representation rather than
// being engineered in. Neither had ever been measured here. This bench measures
// both, plus the number both of them stand on -- how many glyphs fit inside one
// glyph before a component stops being findable.
//
// WHAT IS BEING MEASURED, IN ONE LINE EACH:
//
//   A. Corrupt a glyph at a known bit-flip rate and ask whether nearest-
//      neighbour retrieval still returns the right label.
//   B. Build two memories separately, combine them with one elementwise
//      operation, and ask what that cost -- first over disjoint item sets,
//      then over overlapping sets with unequal observation counts.
//   C. Bundle M glyphs into one and ask whether each of the M is still
//      distinguishable from a non-member.
//
// WHAT THE HARNESS CANNOT SEE:
//
//   Every glyph here is drawn i.i.d. from Glyph::random. Real Khora glyphs come
//   from from_hash and from bind/permute of other glyphs, and anything the
//   pipeline produces that is CORRELATED -- two words that share a role glyph,
//   two records that share a field -- will retrieve worse than these numbers.
//   These are therefore upper bounds on the real system, measured on the
//   friendliest possible data. They are still worth having: if the substrate
//   failed here it would have no chance on correlated data.
//
//   Lattice::query sorts by Hamming distance with std::sort over an
//   unordered_map traversal. Exact ties are therefore broken by hash order,
//   which is arbitrary but fixed within a run. Ties only matter at the collapse
//   point, where the answer is chance anyway.

#include "khora/lattice/lattice.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <random>
#include <span>
#include <string>
#include <utility>
#include <vector>

using khora::lattice::Glyph;
using khora::lattice::Lattice;
using khora::lattice::bundle;
using khora::lattice::kGlyphBits;

namespace {

// 95% Wilson interval on a proportion, returned as percentages. Same helper as
// extraction_bench.cpp: at the rates near collapse the difference between two
// rows is a handful of events and a bare percentage invites over-reading it.
std::pair<double, double> wilson(std::size_t hits, std::size_t n) {
    if (n == 0) return {0.0, 0.0};
    const double z = 1.96, ph = static_cast<double>(hits) / static_cast<double>(n);
    const double d = 1.0 + z * z / static_cast<double>(n);
    const double c = ph + z * z / (2.0 * static_cast<double>(n));
    const double m = z * std::sqrt(ph * (1.0 - ph) / static_cast<double>(n)
                                   + z * z / (4.0 * static_cast<double>(n) * static_cast<double>(n)));
    return {100.0 * (c - m) / d, 100.0 * (c + m) / d};
}

// Corruption flips EXACTLY k distinct bits rather than each bit independently
// with probability p.
//
// Why: the whole claim turns on what happens at p = 0.5, and a Bernoulli
// process there leaves the realised flip count wandering by +-50 bits, which is
// the same order as the signal being measured. Exact-count corruption removes
// that variance so the collapse point can be read off.
//
// What that costs, since the table cannot show it: a glyph with exactly 5000
// bits flipped sits at similarity exactly 0.0 to its original, whereas an
// independent random glyph sits at 0.0 +- 0.01. The 50% row is therefore very
// slightly harsher than true chance, so the true chance line is measured
// separately with a fresh random query and printed beside it.
class Corrupter {
public:
    Corrupter() : idx_(kGlyphBits) { std::iota(idx_.begin(), idx_.end(), 0u); }

    Glyph operator()(const Glyph& g, std::size_t k, std::mt19937_64& rng) {
        Glyph out = g;
        const std::size_t n = idx_.size();
        for (std::size_t i = 0; i < k && i < n; ++i) {
            std::uniform_int_distribution<std::size_t> pick(i, n - 1);
            std::swap(idx_[i], idx_[pick(rng)]);
            out.flip_bit(idx_[i]);
        }
        return out;
    }

private:
    std::vector<std::uint32_t> idx_;
};

const std::vector<double>& kRates() {
    static const std::vector<double> r{0.0, 0.01, 0.02, 0.05, 0.10, 0.20, 0.30, 0.40, 0.50};
    return r;
}

std::string label_of(std::size_t i) { return "i" + std::to_string(i); }

Lattice build_lattice(std::size_t first, std::size_t count, std::uint64_t seed_base) {
    Lattice L;
    for (std::size_t i = first; i < first + count; ++i)
        L.store(label_of(i), Glyph::random(seed_base + i));
    return L;
}

// One retrieval outcome: was the top-1 label right, and how far apart were the
// true match and the best wrong one.
struct Probe {
    bool   hit = false;
    double sim_true = 0.0;
    double sim_false = 0.0;
};

Probe probe_lattice(const Lattice& L, const Glyph& q, const std::string& want) {
    Probe p;
    const auto m = L.query(q, 2);
    if (m.empty()) return p;
    p.hit = (m[0].label == want);
    if (p.hit) {
        p.sim_true  = m[0].similarity;
        p.sim_false = (m.size() > 1) ? m[1].similarity : -1.0;
    } else {
        p.sim_false = m[0].similarity;
        const auto g = L.recall(want);
        p.sim_true  = g ? q.similarity(*g) : -1.0;
    }
    return p;
}

struct Row {
    std::size_t hits = 0, n = 0;
    double      st = 0.0, sf = 0.0;
    void add(const Probe& p) { hits += p.hit ? 1u : 0u; ++n; st += p.sim_true; sf += p.sim_false; }
};

void print_row(const char* lead, const Row& r) {
    const auto ci = wilson(r.hits, r.n);
    std::printf("  %-14s | %6zu/%-6zu | %7.2f%% | [%6.2f%%, %6.2f%%] | %+7.4f | %+7.4f | %+7.4f\n",
                lead, r.hits, r.n, r.n ? 100.0 * static_cast<double>(r.hits) / static_cast<double>(r.n) : 0.0,
                ci.first, ci.second,
                r.n ? r.st / static_cast<double>(r.n) : 0.0,
                r.n ? r.sf / static_cast<double>(r.n) : 0.0,
                r.n ? (r.st - r.sf) / static_cast<double>(r.n) : 0.0);
}

void print_header() {
    std::printf("  rate           |    hits/n     |  top-1    | 95%% Wilson          | sim true | sim best| margin\n");
    std::printf("                 |               |  accuracy |                     |          |  false  |\n");
    std::printf("  ---------------+---------------+-----------+---------------------+----------+---------+--------\n");
}

// ---------------------------------------------------------------------------
// A. GRACEFUL DEGRADATION
// ---------------------------------------------------------------------------

void claim_a_query(std::size_t N, std::size_t trials) {
    std::printf("\n=== A1. CORRUPT THE QUERY ===\n");
    std::printf("  %zu glyphs stored, %zu queries per rate. Chance (1/N) = %.3f%%.\n\n",
                N, trials, 100.0 / static_cast<double>(N));
    const Lattice L = build_lattice(0, N, 1000);
    Corrupter corrupt;
    print_header();
    for (const double p : kRates()) {
        const std::size_t k = static_cast<std::size_t>(std::llround(p * static_cast<double>(kGlyphBits)));
        std::mt19937_64 rng(0xA1ULL ^ static_cast<std::uint64_t>(k));
        Row row;
        for (std::size_t t = 0; t < trials; ++t) {
            const std::size_t which = rng() % N;
            const auto g = L.recall(label_of(which));
            row.add(probe_lattice(L, corrupt(*g, k, rng), label_of(which)));
        }
        char lead[32];
        std::snprintf(lead, sizeof lead, "%4.1f%% (%4zu b)", 100.0 * p, k);
        print_row(lead, row);
    }
    // The dumb baseline. A query that carries no information about the store
    // must land at 1/N. If this row is above chance the harness is wrong.
    {
        std::mt19937_64 rng(0xBADC0DEULL);
        Row row;
        for (std::size_t t = 0; t < trials; ++t) {
            const std::size_t which = rng() % N;
            row.add(probe_lattice(L, Glyph::random(rng()), label_of(which)));
        }
        print_row("random query", row);
    }
    std::printf("\n  The sim-true column is 1 - 2p at every row, to four decimals, because\n"
                "  Hamming distance IS the flip count. That is the quantity degrading\n"
                "  linearly. Top-1 accuracy does not: it is flat at 100%% and then a cliff.\n"
                "  That is better than linear, not worse, but it is not the claim as stated.\n");
}

void claim_a_store(std::size_t N, std::size_t stores, std::size_t queries_per_store) {
    std::printf("\n=== A2. CORRUPT THE STORE (query is clean) ===\n");
    std::printf("  %zu glyphs, all of them corrupted, %zu independent stores x %zu clean\n"
                "  queries each. The samples inside one store share its corruption, so the\n"
                "  Wilson intervals here are narrower than the truth -- treat them as a\n"
                "  lower bound on the uncertainty, not a real 95%%.\n\n",
                N, stores, queries_per_store);
    std::vector<Glyph> clean;
    clean.reserve(N);
    for (std::size_t i = 0; i < N; ++i) clean.push_back(Glyph::random(1000 + i));
    Corrupter corrupt;
    print_header();
    for (const double p : kRates()) {
        const std::size_t k = static_cast<std::size_t>(std::llround(p * static_cast<double>(kGlyphBits)));
        std::mt19937_64 rng(0xA2ULL ^ static_cast<std::uint64_t>(k));
        Row row;
        for (std::size_t s = 0; s < stores; ++s) {
            Lattice L;
            for (std::size_t i = 0; i < N; ++i) L.store(label_of(i), corrupt(clean[i], k, rng));
            for (std::size_t q = 0; q < queries_per_store; ++q) {
                const std::size_t which = rng() % N;
                row.add(probe_lattice(L, clean[which], label_of(which)));
            }
        }
        char lead[32];
        std::snprintf(lead, sizeof lead, "%4.1f%% (%4zu b)", 100.0 * p, k);
        print_row(lead, row);
    }
}

// The interesting part of A is not the flat stretch, it is where the cliff is
// and whether it moves with how much is stored. Coarse rates cannot see it: the
// whole transition happens in the last two percent.
void claim_a_cliff() {
    std::printf("\n=== A3. WHERE THE CLIFF ACTUALLY IS ===\n");
    std::printf("  Top-1 accuracy at rates near 50%%, 200 queries per cell.\n\n");
    const std::vector<double> fine{0.40, 0.44, 0.46, 0.47, 0.48, 0.49, 0.50};
    std::printf("      N   | chance ");
    for (const double p : fine) std::printf("| %5.0f%% ", 100.0 * p);
    std::printf("\n  --------+--------");
    for (std::size_t i = 0; i < fine.size(); ++i) std::printf("+--------");
    std::printf("\n");
    Corrupter corrupt;
    for (const std::size_t N : {std::size_t{16}, std::size_t{256}, std::size_t{4096}}) {
        const Lattice L = build_lattice(0, N, 7000);
        std::printf("  %6zu  | %5.2f%% ", N, 100.0 / static_cast<double>(N));
        for (const double p : fine) {
            const std::size_t k = static_cast<std::size_t>(std::llround(p * static_cast<double>(kGlyphBits)));
            std::mt19937_64 rng(0xA3ULL ^ (static_cast<std::uint64_t>(k) << 8) ^ N);
            std::size_t hits = 0;
            const std::size_t trials = 200;
            for (std::size_t t = 0; t < trials; ++t) {
                const std::size_t which = rng() % N;
                const auto g = L.recall(label_of(which));
                hits += probe_lattice(L, corrupt(*g, k, rng), label_of(which)).hit ? 1u : 0u;
            }
            std::printf("| %5.1f%% ", 100.0 * static_cast<double>(hits) / static_cast<double>(trials));
        }
        std::printf("\n");
    }
}

// ---------------------------------------------------------------------------
// C. CAPACITY -- printed before B because B's numbers only make sense with it
// ---------------------------------------------------------------------------
//
// A hit means: the bundle is closer to this member than to ANY of D fresh
// non-members. That is the question a memory has to answer -- is this thing in
// here -- and it is harder than ranking the members against each other.
void capacity(std::size_t D, std::size_t trials) {
    std::printf("\n=== C. CAPACITY OF ONE GLYPH ===\n");
    std::printf("  bundle(M random glyphs), then ask of each member: is it closer to the\n"
                "  bundle than all %zu fresh non-members? %zu trials per M, at most 32\n"
                "  members probed per trial. Members inside one trial share a distractor\n"
                "  set, so the intervals are again slightly narrow.\n\n", D, trials);
    std::printf("     M | members  | recognised | 95%% Wilson          | sim true | sim best| margin | theory\n");
    std::printf("       | probed   |            |                     |  member  |  false  |        | sqrt(2/piM)\n");
    std::printf("  -----+----------+------------+---------------------+----------+---------+--------+--------\n");
    for (const std::size_t M : {2u, 4u, 8u, 16u, 32u, 64u, 128u, 256u, 512u, 1024u}) {
        std::mt19937_64 rng(0xC0ULL ^ M);
        std::size_t hits = 0, n = 0;
        double st = 0.0, sf = 0.0;
        for (std::size_t t = 0; t < trials; ++t) {
            std::vector<Glyph> members;
            members.reserve(M);
            for (std::size_t i = 0; i < M; ++i) members.push_back(Glyph::random(rng()));
            const Glyph b = bundle(std::span<const Glyph>(members));

            double best_false = -2.0;
            for (std::size_t i = 0; i < D; ++i)
                best_false = std::max(best_false, b.similarity(Glyph::random(rng())));

            const std::size_t probes = std::min<std::size_t>(M, 32);
            for (std::size_t i = 0; i < probes; ++i) {
                const double s = b.similarity(members[i]);
                hits += (s > best_false) ? 1u : 0u;
                ++n;
                st += s;
                sf += best_false;
            }
        }
        const auto ci = wilson(hits, n);
        std::printf("  %4zu | %8zu | %9.2f%% | [%6.2f%%, %6.2f%%] | %+7.4f | %+7.4f | %+7.4f | %+7.4f\n",
                    M, n, 100.0 * static_cast<double>(hits) / static_cast<double>(n),
                    ci.first, ci.second, st / static_cast<double>(n), sf / static_cast<double>(n),
                    (st - sf) / static_cast<double>(n),
                    std::sqrt(2.0 / (3.14159265358979 * static_cast<double>(M))));
    }
    std::printf("\n  The theory column is the known expected cosine of a majority bundle to\n"
                "  one of its M components. It is printed as a check on the harness, not as\n"
                "  a result: if the measured column drifts off it, the bench is wrong.\n"
                "  Even M is expected to sit slightly below it -- bundle() breaks even-arity\n"
                "  ties with a pseudo-random glyph, and a coin flip carries no signal.\n");
}

// ---------------------------------------------------------------------------
// B. FEDERATED MERGE
// ---------------------------------------------------------------------------

void claim_b_disjoint_lattice(std::size_t N) {
    std::printf("\n=== B1. MERGING TWO LATTICES OVER DISJOINT ITEMS ===\n");
    std::printf("  A holds items 0..%zu, B holds %zu..%zu. State plainly what the merge is:\n"
                "  a Lattice is a hash map from label to glyph, so merging two of them is a\n"
                "  map union. There is no elementwise operation involved and nothing can be\n"
                "  lost, so the only real question is what DOUBLING THE DISTRACTORS costs at\n"
                "  retrieval time. Clean, then at the three rates where that cost is visible\n"
                "  at all -- every rate below 46%% is 100%% for both.\n\n",
                N - 1, N, 2 * N - 1);

    Lattice A = build_lattice(0, N, 5000);
    Lattice B = build_lattice(N, N, 5000);
    Lattice M = A;
    for (const auto& [lab, g] : B) M.store(lab, g);

    Corrupter corrupt;
    std::printf("  memory                    | items | corrupt |    hits/n     |  top-1    | 95%% Wilson\n");
    std::printf("  --------------------------+-------+---------+---------------+-----------+--------------------\n");
    auto run = [&](const char* name, const Lattice& L, std::size_t first, std::size_t count, double p) {
        const std::size_t k = static_cast<std::size_t>(std::llround(p * static_cast<double>(kGlyphBits)));
        std::mt19937_64 rng(0xB1ULL ^ static_cast<std::uint64_t>(k) ^ (first << 16));
        std::size_t hits = 0;
        const std::size_t trials = 400;
        for (std::size_t t = 0; t < trials; ++t) {
            const std::size_t which = first + rng() % count;
            const auto g = L.recall(label_of(which));
            const Glyph q = (k == 0) ? *g : corrupt(*g, k, rng);
            hits += probe_lattice(L, q, label_of(which)).hit ? 1u : 0u;
        }
        const auto ci = wilson(hits, trials);
        std::printf("  %-25s | %5zu | %6.0f%% | %6zu/%-6zu | %7.2f%% | [%6.2f%%, %6.2f%%]\n",
                    name, L.size(), 100.0 * p, hits, trials,
                    100.0 * static_cast<double>(hits) / static_cast<double>(trials), ci.first, ci.second);
    };
    for (const double p : {0.0, 0.46, 0.48, 0.49}) {
        run("A alone, own items", A, 0, N, p);
        run("B alone, own items", B, N, N, p);
        run("merged, A's half", M, 0, N, p);
        run("merged, B's half", M, N, N, p);
    }
}

void claim_b_disjoint_bundle(std::size_t N, std::size_t D, std::size_t trials) {
    std::printf("\n=== B2. MERGING TWO BUNDLED MEMORIES WITH ONE ELEMENTWISE OP ===\n");
    std::printf("  This is the version where the claim means something: memory A is ONE\n"
                "  glyph holding %zu items in superposition, likewise B, and the merge is a\n"
                "  single elementwise majority -- bundle({A, B}). Compared against the same\n"
                "  %zu items bundled in one pass, which is what a centralised build would\n"
                "  have produced. %zu trials, %zu non-members.\n\n", N, 2 * N, trials, D);

    std::mt19937_64 rng(0xB2ULL);
    std::size_t hits_m = 0, hits_u = 0, n = 0;
    double st_m = 0.0, sf_m = 0.0, st_u = 0.0, sf_u = 0.0, div = 0.0;
    for (std::size_t t = 0; t < trials; ++t) {
        std::vector<Glyph> all;
        all.reserve(2 * N);
        for (std::size_t i = 0; i < 2 * N; ++i) all.push_back(Glyph::random(rng()));
        const Glyph a = bundle(std::span<const Glyph>(all.data(), N));
        const Glyph b = bundle(std::span<const Glyph>(all.data() + N, N));
        const Glyph merged = bundle({a, b});
        const Glyph unioned = bundle(std::span<const Glyph>(all));
        div += merged.similarity(unioned);

        double bf_m = -2.0, bf_u = -2.0;
        for (std::size_t i = 0; i < D; ++i) {
            const Glyph d = Glyph::random(rng());
            bf_m = std::max(bf_m, merged.similarity(d));
            bf_u = std::max(bf_u, unioned.similarity(d));
        }
        for (std::size_t i = 0; i < 2 * N; ++i) {
            const double sm = merged.similarity(all[i]);
            const double su = unioned.similarity(all[i]);
            hits_m += (sm > bf_m) ? 1u : 0u;
            hits_u += (su > bf_u) ? 1u : 0u;
            ++n;
            st_m += sm; sf_m += bf_m; st_u += su; sf_u += bf_u;
        }
    }
    const auto cm = wilson(hits_m, n);
    const auto cu = wilson(hits_u, n);
    std::printf("  memory                       |    hits/n     | recognised | 95%% Wilson          | sim true | sim best\n");
    std::printf("  -----------------------------+---------------+------------+---------------------+----------+---------\n");
    std::printf("  bundle({A, B})  (merged)     | %6zu/%-6zu | %9.2f%% | [%6.2f%%, %6.2f%%] | %+7.4f | %+7.4f\n",
                hits_m, n, 100.0 * static_cast<double>(hits_m) / static_cast<double>(n), cm.first, cm.second,
                st_m / static_cast<double>(n), sf_m / static_cast<double>(n));
    std::printf("  bundle(all %3zu)  (one pass)  | %6zu/%-6zu | %9.2f%% | [%6.2f%%, %6.2f%%] | %+7.4f | %+7.4f\n",
                2 * N, hits_u, n, 100.0 * static_cast<double>(hits_u) / static_cast<double>(n), cu.first, cu.second,
                st_u / static_cast<double>(n), sf_u / static_cast<double>(n));
    std::printf("\n  similarity between the merged glyph and the one-pass glyph: %+.4f\n",
                div / static_cast<double>(trials));
    std::printf("  A two-way majority passes each half's signal at 1/2 of what that half\n"
                "  held, while one pass over 2N items holds 1/sqrt(2) of what N held. The\n"
                "  predicted merged/one-pass signal ratio is therefore (1/2)/(1/sqrt(2)) =\n"
                "  0.707; measured %.3f. Read it as capacity: merging two %zu-item memories\n"
                "  leaves a glyph carrying the signal of a one-pass bundle of about %zu\n"
                "  items, not %zu. Pairwise merge costs a doubling of effective load on top\n"
                "  of the items actually merged.\n",
                st_u != 0.0 ? st_m / st_u : 0.0, N, 4 * N, 2 * N);
}

// The honest case. Two sites see the same items different numbers of times.
// Majority is not associative and it does not carry multiplicity, so a memory
// built from 256 observations and one built from 8 merge as equals.
void claim_b_overlap() {
    std::printf("\n=== B3. OVERLAPPING ITEMS, UNEQUAL EVIDENCE ===\n");
    std::printf("  Site A observed items 0..7, 32 times each (256 observations).\n"
                "  Site B observed items 4..11, once each (8 observations).\n"
                "  One pass over the union is 264 observations: items 4-7 at 33, items 0-3\n"
                "  at 32, items 8-11 at 1. Merging the two site memories cannot know that.\n\n");

    std::mt19937_64 rng(0xB3ULL);
    std::vector<Glyph> item;
    for (std::size_t i = 0; i < 12; ++i) item.push_back(Glyph::random(rng()));

    std::vector<Glyph> obsA, obsB, obsU;
    for (std::size_t i = 0; i < 8; ++i)
        for (int r = 0; r < 32; ++r) obsA.push_back(item[i]);
    for (std::size_t i = 4; i < 12; ++i) obsB.push_back(item[i]);
    obsU = obsA;
    obsU.insert(obsU.end(), obsB.begin(), obsB.end());

    const Glyph A = bundle(std::span<const Glyph>(obsA));
    const Glyph B = bundle(std::span<const Glyph>(obsB));
    const Glyph merged = bundle({A, B});
    const Glyph unioned = bundle(std::span<const Glyph>(obsU));

    std::printf("  item | obs in A | obs in B | obs in union | sim(one pass) | sim(merged) | delta\n");
    std::printf("  -----+----------+----------+--------------+---------------+-------------+--------\n");
    double worst = 0.0;
    for (std::size_t i = 0; i < 12; ++i) {
        const int ca = (i < 8) ? 32 : 0;
        const int cb = (i >= 4) ? 1 : 0;
        const double su = unioned.similarity(item[i]);
        const double sm = merged.similarity(item[i]);
        worst = std::max(worst, std::fabs(sm - su));
        std::printf("  %4zu | %8d | %8d | %12d | %+13.4f | %+11.4f | %+7.4f\n",
                    i, ca, cb, ca + cb, su, sm, sm - su);
    }
    std::printf("\n  hamming(merged, one pass) = %zu of %zu bits, similarity %+.4f\n",
                merged.hamming(unioned), kGlyphBits, merged.similarity(unioned));
    std::printf("  largest per-item divergence in similarity: %.4f\n", worst);
    std::printf("  Items 8-11 were seen ONCE across 264 observations. In the one-pass memory\n"
                "  they are near zero, correctly. Read their merged column: the merge gave\n"
                "  site B's eight observations the same vote as site A's 256.\n");

    // Associativity, since a real federation merges more than two sites.
    // Site C is given its own content -- items 8..11 seen 16 times each. An
    // earlier version of this made C a copy of B, which is degenerate:
    // bundle({B, B}) == B exactly, so the "third site" was not one.
    std::vector<Glyph> obsC;
    for (std::size_t i = 8; i < 12; ++i)
        for (int r = 0; r < 16; ++r) obsC.push_back(item[i]);
    const Glyph C = bundle(std::span<const Glyph>(obsC));
    const Glyph left  = bundle({bundle({A, B}), C});
    const Glyph right = bundle({A, bundle({B, C})});
    const Glyph flat  = bundle({A, B, C});
    std::printf("\n  Three sites, and the merge is not associative:\n");
    std::printf("    sim((A+B)+C, A+(B+C)) = %+.4f\n", left.similarity(right));
    std::printf("    sim((A+B)+C, A+B+C)   = %+.4f\n", left.similarity(flat));
    std::printf("    sim(A+(B+C), A+B+C)   = %+.4f\n", right.similarity(flat));
    std::printf("  A federation therefore has to agree on merge ORDER, or fan every site\n"
                "  into one bundle() call, which is no longer pairwise merging.\n");

    // Commutativity, which the tiebreak was written to preserve.
    const Glyph ab = bundle({A, B}), ba = bundle({B, A});
    std::printf("\n  Commutativity of the pairwise merge: bundle({A,B}) == bundle({B,A}) is %s.\n",
                (ab == ba) ? "TRUE (bit-identical)" : "FALSE");
}

} // namespace

int main() {
    std::printf("SUBSTRATE BENCH -- graceful degradation, federated merge, and the capacity\n");
    std::printf("that both of them rest on. %zu-bit glyphs, i.i.d. random.\n", kGlyphBits);

    claim_a_query(1000, 500);
    claim_a_store(1000, 40, 25);
    claim_a_cliff();
    capacity(500, 400);
    claim_b_disjoint_lattice(1000);
    claim_b_disjoint_bundle(64, 500, 300);
    claim_b_overlap();

    std::printf("\nREAD THE MARGIN COLUMN, NOT THE ACCURACY COLUMN. Accuracy is a step\n"
                "function and says nothing until it moves; the margin between the true match\n"
                "and the best wrong one is what is actually degrading, and it is what will\n"
                "run out first on correlated real glyphs.\n");
    return 0;
}
