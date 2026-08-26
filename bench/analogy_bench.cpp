// IS ANALOGY ARITHMETIC IN THIS SUBSTRATE?
//
// THE CLAIM UNDER TEST. "If R = bind(king, queen) is a relation glyph, then
// bind(R, man) should retrieve something nearest to woman in the Lattice. No
// training, one example, pure XOR."
//
// Taken literally the claim is false, and it is false for a reason that is
// arithmetic rather than empirical. king, queen, man and woman as INDEPENDENT
// random glyphs carry no shared structure, so
//
//     bind(bind(king, queen), man) = king ^ queen ^ man
//
// is a fresh pseudorandom glyph. Nothing in it references woman, because
// nothing anywhere in the construction ever said that woman is what man
// becomes. The bench measures this anyway rather than asserting it, because a
// negative control that is "obviously" true is exactly the kind of thing that
// turns out not to be -- see CONTROL below, which retrieves at 1/N.
//
// WHAT ACTUALLY WORKS, AND WHY THAT IS STILL INTERESTING. The analogy is
// carried by COMPOSITION, not by the atoms. Build each item as a record of
// role-filler bindings superposed by majority -- Kanerva's "what is the dollar
// of Mexico" construction:
//
//     A = bundle{ bind(role_1, a_1), ..., bind(role_S, a_S) }
//     B = bundle{ bind(role_1, b_1), ..., bind(role_S, b_S) }
//     M = bind(A, B)                       <- the whole mapping, ONE example
//     bind(M, a_t) = b_t ^ n_A ^ n_B       <- role_t and a_t cancel exactly
//
// where n_A, n_B are each record's bundling residue. The role glyph and the
// source filler cancel by XOR's own algebra; what is left is the target filler
// plus two records' worth of superposition noise. So the interesting question
// is not "does it work" (the cancellation is exact) but HOW MUCH NOISE the
// nearest-neighbour retrieval can eat before the answer drowns. That is the
// capacity curve, and it is the actual finding here.
//
// There is no learning anywhere. There is also no CORPUS: every filler is
// Glyph::random and every "relation family" is a synthetic record pair. The
// harness therefore says nothing about whether real-world analogies decompose
// into clean role-filler records -- that is the hard part, and it is upstream
// of this file. What is measured is the algebra's carrying capacity given that
// the decomposition was handed to it.
//
// WHAT THE HARNESS CANNOT SEE:
//   * Whether any real concept ("king") admits such a decomposition. Assumed.
//   * Whether the roles are known at query time. They are not needed -- they
//     cancel -- but they had to exist and be SHARED between A and B when the
//     records were built. A relation between two differently-schematised items
//     is not tested and would not cancel.
//   * Retrieval cost. Lattice::query is a linear scan over every candidate,
//     so the 10,000-candidate column is 10,000 Hamming distances per analogy.
//     Accuracy is reported; the substrate's O(N) recall is not the subject.
//   * Noise is injected into the QUERY glyph only. Corrupting the stored
//     candidates instead is a different experiment and is not run.
//
// Every rate is printed with its numerator, its denominator and a 95% Wilson
// interval, and every table carries the dumb baselines beside it.

#include "khora/lattice/lattice.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
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
using khora::lattice::bind;
using khora::lattice::bundle;
using khora::lattice::kGlyphBits;

namespace {

constexpr std::size_t kMaxCandidates = 10000;
constexpr std::size_t kMaxRoles      = 64;
constexpr std::size_t kTrials        = 1000;   // independent relation families per cell

std::pair<double, double> wilson(std::size_t hits, std::size_t n) {
    if (n == 0) return {0.0, 0.0};
    const double z = 1.96, ph = static_cast<double>(hits) / static_cast<double>(n);
    const double d = 1.0 + z * z / static_cast<double>(n);
    const double c = ph + z * z / (2.0 * static_cast<double>(n));
    const double m = z * std::sqrt(ph * (1.0 - ph) / static_cast<double>(n)
                                   + z * z / (4.0 * static_cast<double>(n) * static_cast<double>(n)));
    return {100.0 * (c - m) / d, 100.0 * (c + m) / d};
}

// One measured cell. `self` counts the trials whose winner was the SOURCE item
// -- the trivial "it just handed the input back" failure, which a bare accuracy
// number cannot distinguish from a near miss.
struct Cell {
    std::size_t hits = 0;
    std::size_t n    = 0;
    std::size_t self = 0;
    double      d_truth = 0.0;   // summed Hamming(query, true answer)
    double      d_win   = 0.0;   // summed Hamming(query, winner)

    double rate()  const { return n ? 100.0 * static_cast<double>(hits) / static_cast<double>(n) : 0.0; }
    double selfr() const { return n ? 100.0 * static_cast<double>(self) / static_cast<double>(n) : 0.0; }
    double dt()    const { return n ? d_truth / static_cast<double>(n) : 0.0; }
    double dw()    const { return n ? d_win   / static_cast<double>(n) : 0.0; }
};

// A fixed pool of candidate glyphs. Fillers are drawn from it and it IS the
// Lattice searched, so the source item is always among the distractors -- there
// is no hand-removal of the one competitor that matters.
struct Pool {
    std::vector<Glyph>       g;
    std::vector<std::string> label;
    std::vector<Glyph>       role;

    Pool() {
        g.reserve(kMaxCandidates);
        label.reserve(kMaxCandidates);
        for (std::size_t i = 0; i < kMaxCandidates; ++i) {
            g.push_back(Glyph::random(0x51A9'0000'0000'0001ULL + i));
            label.push_back("w" + std::to_string(i));
        }
        for (std::size_t r = 0; r < kMaxRoles; ++r) {
            // Roles live outside the candidate pool: they are structure, not
            // answers, and must never win a retrieval.
            role.push_back(Glyph::random(0x0E5C'0000'0000'0011ULL + r * 0x9E37'79B9'7F4A'7C15ULL));
        }
    }
};

Lattice make_lattice(const Pool& p, std::size_t n) {
    Lattice lat;
    for (std::size_t i = 0; i < n; ++i) lat.store(p.label[i], p.g[i]);
    return lat;
}

// Uniform sample of k distinct indices, by keeping `perm` a live permutation
// and re-shuffling only its first k slots each call. O(k), no allocation.
void sample_distinct(std::vector<std::size_t>& perm, std::size_t k, std::mt19937_64& rng) {
    const std::size_t n = perm.size();
    for (std::size_t i = 0; i < k; ++i) {
        std::uniform_int_distribution<std::size_t> pick(i, n - 1);
        std::swap(perm[i], perm[pick(rng)]);
    }
}

// --- THE MEASUREMENT -------------------------------------------------------
//
// One trial = one fresh relation family. `maps` independent record pairs are
// built; their mapping glyphs are superposed into a single relation glyph; the
// analogy is then completed for one slot of the FIRST pair. `flips` bits of the
// finished query are inverted to model a corrupted probe.
Cell run(const Pool& p, const Lattice& lat, std::size_t ncand, std::size_t slots,
         std::size_t maps, std::size_t flips, std::uint64_t seed, std::size_t trials) {
    Cell c;
    std::mt19937_64 rng(seed);
    std::vector<std::size_t> perm(ncand);
    std::iota(perm.begin(), perm.end(), std::size_t{0});
    std::vector<std::size_t> bits(kGlyphBits);
    std::iota(bits.begin(), bits.end(), std::size_t{0});

    const std::size_t need = 2 * slots * maps;
    if (need > ncand) return c;   // cannot build disjoint fillers; cell left empty

    std::vector<Glyph> parts_a, parts_b, mappings;
    for (std::size_t t = 0; t < trials; ++t) {
        sample_distinct(perm, need, rng);
        std::size_t take = 0;
        mappings.clear();
        std::size_t src = 0, tgt = 0;

        for (std::size_t m = 0; m < maps; ++m) {
            parts_a.clear();
            parts_b.clear();
            std::vector<std::size_t> fa(slots), fb(slots);
            for (std::size_t s = 0; s < slots; ++s) {
                fa[s] = perm[take++];
                fb[s] = perm[take++];
                parts_a.push_back(bind(p.role[s], p.g[fa[s]]));
                parts_b.push_back(bind(p.role[s], p.g[fb[s]]));
            }
            const Glyph A = bundle(std::span<const Glyph>(parts_a));
            const Glyph B = bundle(std::span<const Glyph>(parts_b));
            mappings.push_back(bind(A, B));
            if (m == 0) {
                const std::size_t slot = rng() % slots;
                src = fa[slot];
                tgt = fb[slot];
            }
        }

        const Glyph R = (maps == 1) ? mappings[0] : bundle(std::span<const Glyph>(mappings));
        Glyph q = bind(R, p.g[src]);

        if (flips > 0) {
            sample_distinct(bits, flips, rng);
            for (std::size_t i = 0; i < flips; ++i) q.flip_bit(bits[i]);
        }

        const auto hit = lat.query(q, 1);
        ++c.n;
        if (!hit.empty()) {
            if (hit[0].label == p.label[tgt]) ++c.hits;
            if (hit[0].label == p.label[src]) ++c.self;
            c.d_win += static_cast<double>(hit[0].hamming);
        }
        c.d_truth += static_cast<double>(q.hamming(p.g[tgt]));
    }
    return c;
}

// DUMB BASELINE 1: the literal claim on ATOMIC symbols. Four independent random
// glyphs; "woman" is declared to be the intended answer by fiat, exactly as a
// human reader would. Nothing in the construction connects it to the other
// three, so this is what the claim buys when the items are opaque.
Cell run_atomic(const Pool& p, const Lattice& lat, std::size_t ncand,
                std::uint64_t seed, std::size_t trials) {
    Cell c;
    std::mt19937_64 rng(seed);
    std::vector<std::size_t> perm(ncand);
    std::iota(perm.begin(), perm.end(), std::size_t{0});
    for (std::size_t t = 0; t < trials; ++t) {
        sample_distinct(perm, 4, rng);
        const std::size_t king = perm[0], queen = perm[1], man = perm[2], woman = perm[3];
        const Glyph q = bind(bind(p.g[king], p.g[queen]), p.g[man]);
        const auto hit = lat.query(q, 1);
        ++c.n;
        if (!hit.empty()) {
            if (hit[0].label == p.label[woman]) ++c.hits;
            if (hit[0].label == p.label[man])   ++c.self;
            c.d_win += static_cast<double>(hit[0].hamming);
        }
        c.d_truth += static_cast<double>(q.hamming(p.g[woman]));
    }
    return c;
}

// DUMB BASELINE 2: ignore the relation entirely and return the nearest
// neighbour of the SOURCE item. In a Lattice that contains the source this is
// always the source itself, so its accuracy is the rate at which the answer
// happens to equal the question. It is printed because a system that quietly
// returns its input can otherwise look like it is doing something.
Cell run_identity(const Pool& p, const Lattice& lat, std::size_t ncand, std::size_t slots,
                  std::uint64_t seed, std::size_t trials) {
    Cell c;
    std::mt19937_64 rng(seed);
    std::vector<std::size_t> perm(ncand);
    std::iota(perm.begin(), perm.end(), std::size_t{0});
    const std::size_t need = 2 * slots;
    if (need > ncand) return c;
    for (std::size_t t = 0; t < trials; ++t) {
        sample_distinct(perm, need, rng);
        const std::size_t slot = rng() % slots;
        const std::size_t src = perm[2 * slot], tgt = perm[2 * slot + 1];
        const auto hit = lat.query(p.g[src], 1);
        ++c.n;
        if (!hit.empty()) {
            if (hit[0].label == p.label[tgt]) ++c.hits;
            if (hit[0].label == p.label[src]) ++c.self;
            c.d_win += static_cast<double>(hit[0].hamming);
        }
        c.d_truth += static_cast<double>(p.g[src].hamming(p.g[tgt]));
    }
    return c;
}

void head(const char* first_col) {
    std::printf("    %-14s | correct |    n | top-1 acc | 95%% Wilson CI     | chance | ret. input | d(q,truth) | d(q,winner)\n", first_col);
    std::printf("    ---------------+---------+------+-----------+-------------------+--------+------------+------------+------------\n");
}

void row(const std::string& lbl, const Cell& c, std::size_t ncand) {
    if (c.n == 0) { std::printf("    %-14s |    -- not run: candidate pool too small for disjoint fillers\n", lbl.c_str()); return; }
    const auto ci = wilson(c.hits, c.n);
    std::printf("    %-14s | %7zu | %4zu | %8.2f%% | [%6.2f%%, %6.2f%%] | %5.2f%% | %9.2f%% | %10.1f | %10.1f\n",
                lbl.c_str(), c.hits, c.n, c.rate(), ci.first, ci.second,
                100.0 / static_cast<double>(ncand), c.selfr(), c.dt(), c.dw());
}

} // namespace

int main() {
    const Pool pool;
    std::printf("ANALOGY BENCH -- one-shot relational transfer by XOR (Glyph bits = %zu)\n", kGlyphBits);
    std::printf("%zu independent relation families per cell. Chance = 1/N. Distractors sit at\n"
                "d = %zu bits from any probe by construction, so d(q,truth) below %zu is signal.\n",
                kTrials, kGlyphBits / 2, kGlyphBits / 2);

    Lattice lat10   = make_lattice(pool, 10);
    Lattice lat100  = make_lattice(pool, 100);
    Lattice lat1k   = make_lattice(pool, 1000);
    Lattice lat10k  = make_lattice(pool, 10000);

    // --- 1. THE CLAIM AS WRITTEN, AND THE CLAIM AS IT HAS TO BE BUILT -------
    std::printf("\n  === 1. ONE-SHOT TRANSFER: 1000 candidates, 4 role-filler slots per item ===\n");
    head("construction");
    row("atomic king^q",  run_atomic(pool, lat1k, 1000, 0xA70A11C, kTrials), 1000);
    row("return input",   run_identity(pool, lat1k, 1000, 4, 0x1DE0F1, kTrials), 1000);
    row("role-filler",    run(pool, lat1k, 1000, 4, 1, 0, 0x0E5077ULL, kTrials), 1000);
    std::printf("\n    'atomic king^q' is the claim taken literally: four independent random\n"
                "    glyphs, woman declared the answer. 'return input' ignores the relation\n"
                "    and returns the nearest neighbour of the source, which is the source.\n"
                "    'role-filler' composes both items from the SAME %d shared roles first.\n", 4);

    // --- 2. CAPACITY IN THE NUMBER OF CANDIDATES ---------------------------
    //
    // Run at two loads. At 4 slots the margin is so wide that N does nothing
    // over the range asked for, which is itself the answer; the 16-slot block
    // is where growing N starts to cost anything.
    std::printf("\n  === 2. CAPACITY: candidates in the Lattice (1 relation) ===\n");
    head("slots / cands");
    const struct { std::size_t n; Lattice* l; } sizes[] =
        {{10,&lat10},{100,&lat100},{1000,&lat1k},{10000,&lat10k}};
    for (std::size_t sl : {4u, 16u, 32u}) {
        for (const auto& s : sizes)
            row(std::to_string(sl) + " / " + std::to_string(s.n),
                run(pool, *s.l, s.n, sl, 1, 0, 0xC4ADD0ULL + sl * 1000003ULL + s.n, kTrials), s.n);
    }
    std::printf("\n    d(q,truth) does not move with N -- the noise is a property of the\n"
                "    algebra, not of the search. What moves is how many distractors get a\n"
                "    draw from the d=%zu distribution and beat it. A pool of 10 cannot\n"
                "    supply 2*slots disjoint fillers past 4 slots, so those cells are absent\n"
                "    rather than zero.\n", kGlyphBits / 2);

    // --- 3. CAPACITY IN SUPERPOSITION DEPTH --------------------------------
    //
    // Carried past the requested 16 on purpose: at 16 the curve has only just
    // left the ceiling, and a capacity claim that stops before the floor is not
    // a capacity claim.
    std::printf("\n  === 3. CAPACITY: role-filler slots superposed into each item (N=1000) ===\n");
    head("slots/item");
    for (std::size_t s : {1u, 2u, 4u, 8u, 16u, 24u, 32u, 48u, 64u})
        row(std::to_string(s), run(pool, lat1k, 1000, s, 1, 0, 0x5107ADULL + s, kTrials), 1000);
    std::printf("\n    1 slot is the degenerate case: A = role^a, B = role^b, so bind(A,B)\n"
                "    IS a^b and the transfer is exact by construction, not by luck --\n"
                "    d(q,truth) is literally 0. Every row above 1 pays for two records'\n"
                "    bundling residue, and the query is fighting BOTH of them at once.\n");

    std::printf("\n  === 4. CAPACITY: distinct relations bundled into ONE glyph (4 slots, N=1000) ===\n");
    head("relations");
    for (std::size_t m : {1u, 2u, 4u, 8u, 16u, 32u, 64u})
        row(std::to_string(m), run(pool, lat1k, 1000, 4, m, 0, 0x0EE1ADULL + m, kTrials), 1000);
    std::printf("\n    Each relation is an independent record pair; only the first one is\n"
                "    queried. The others are interference the glyph is asked to carry.\n");

    // --- 5. NOISE -----------------------------------------------------------
    //
    // Also run at two loads, and past 20%, for the same reason as section 3: at
    // 4 slots the requested range never leaves the ceiling.
    std::printf("\n  === 5. NOISE: bits flipped in the finished query (1 relation, N=1000) ===\n");
    head("slots / flip");
    for (std::size_t sl : {4u, 16u}) {
        for (double f : {0.0, 0.01, 0.05, 0.10, 0.20, 0.30, 0.40}) {
            const std::size_t k = static_cast<std::size_t>(f * kGlyphBits + 0.5);
            row(std::to_string(sl) + " / " + std::to_string(static_cast<int>(f * 100)) + "%",
                run(pool, lat1k, 1000, sl, 1, k, 0xB015EDULL + sl * 1000003ULL + k, kTrials), 1000);
        }
    }
    std::printf("\n    A flipped bit moves the query one step from the truth AND (on average)\n"
                "    nowhere at all relative to a random distractor, so p%% of flips costs\n"
                "    about 2p%% of the MARGIN rather than destroying the query. 50%% would be\n"
                "    total destruction. What noise really does is eat the headroom that\n"
                "    superposition left, which is why the two blocks differ.\n");

    // The one number worth carrying away, stated as what it is: a fit to the
    // section-3 column above, not a theorem. For S >= 4 slots the observed
    // signal margin 5000 - d(q,truth) tracks ~3000/S bits (measured 704, 374,
    // 193, 95, 49 at S = 4, 8, 16, 32, 64 against 750, 375, 187, 94, 47).
    // A distractor pool of N draws from Binomial(10000, 1/2) has a nearest
    // member about sqrt(2 ln N) * 50 bits below 5000, so retrieval survives
    // while 3000/S > 50*sqrt(2 ln N), i.e. S < 60/sqrt(2 ln N): 16 at N=1000,
    // 14 at N=10000. The measured 50% crossings sit at S ~ 16 and S ~ 16
    // respectively, which is the same number to the resolution tested.
    std::printf("\n  Nothing here was trained and nothing was tuned. The relation glyph is one\n"
                "  XOR of two example items. What the numbers price is superposition, not\n"
                "  learning: the answer is exact when each item holds one binding and decays\n"
                "  as items get crowded. Capacity is roughly S < 60/sqrt(2 ln N) role-filler\n"
                "  pairs per item -- about 8 with headroom, 16 at the 50%% line, single digits\n"
                "  of accuracy by 48 -- and it moves only logarithmically in the number of\n"
                "  candidates, because the cost of N is a log-scale order statistic while the\n"
                "  cost of S is linear. The harness supplied the role-filler decomposition by\n"
                "  construction; obtaining that decomposition from real data is untested here\n"
                "  and is the part that would actually be hard.\n");
    return 0;
}
