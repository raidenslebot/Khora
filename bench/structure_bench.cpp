// TWO PROPERTIES OF BINDING THAT A FLAT EMBEDDING DOES NOT HAVE, MEASURED.
//
// A sentence embedding is a point. You can ask how close it is to another point
// and nothing else: there is no operation that says "give me the left operand of
// the multiplication", and no operation that hides one field from a reader who
// holds the vector. Role-filler binding claims both. This bench tests both and
// reports where each one stops.
//
// CLAIM A -- NESTED STRUCTURE IN A FLAT VECTOR.
//
//     node = bundle{ bind(OP, op), bind(LEFT, left), bind(RIGHT, right) }
//
// A whole expression tree collapses into ONE 10,000-bit glyph of fixed width,
// the same width whatever the tree. Reading a child back is XOR with the role
// glyph -- no parse, no node table, no pointer to follow.
//
// WRITTEN EXACTLY AS ABOVE IT DOES NOT WORK BELOW DEPTH TWO, and the reason is
// algebraic rather than statistical, so no dimension and no codebook size fixes
// it. bind is XOR: commutative and its own inverse. A path is therefore only
// its PARITY. RIGHT,RIGHT,LEFT unbinds to the identical glyph as LEFT -- not a
// similar one, the same bits -- so a depth-3 query returns the depth-1 child
// with full confidence and a wide margin. LEFT,RIGHT and RIGHT,LEFT are also
// the same glyph, so "the left child of the right child" and "the right child
// of the left child" are indistinguishable. Section A1a measures that.
//
// The fix is one XOR per step and it is already in the algebra: give every
// (role, level) its own glyph, step_role(side, d) = bind(LEFT_or_RIGHT,
// position_glyph(2*d + side)). Tagging by DEPTH ALONE is not enough, and A1c
// measures why -- it stops a role cancelling against itself but leaves
// LEFT-then-RIGHT and RIGHT-then-LEFT identical. Section A1b measures the full
// fix; A2-A4 use it.
//
// CLAIM B -- HOLOGRAPHIC PRIVACY.
//
//     record = bundle over fields of bind(role, value)
//
// No bit of the record belongs to any one field, so an adversary holding the
// record and the entire value codebook, but not the role glyph, is supposed to
// learn nothing; the same adversary given the role glyph recovers the value
// immediately. That contrast IS the claim. Measured: direct similarity attack,
// guessed-role attack, the with-role control, whether many records sharing a
// role leak the role under averaging, and -- the one most likely to break it --
// whether a repeated value under a shared role lets two groups of records be
// XORed together and the value pair read straight out of the public codebook.
//
// WHAT THIS HARNESS CANNOT SEE:
//
//   * Every symbol, role and value glyph here is i.i.d. from Glyph::random.
//     Khora's real symbols come from from_hash and from bind/permute of other
//     glyphs; anything correlated (two words sharing a role, two records sharing
//     a field) retrieves worse. These are UPPER BOUNDS on the deployed system.
//
//   * Claim A retrieval does ONE cleanup, at the end of the path, against the
//     leaf codebook. A resonator network cleaning up at every level would go
//     deeper -- but a per-level cleanup memory of subtree glyphs IS a node
//     table, which is the thing the claim says is not needed. The depths below
//     are the no-intermediate-cleanup depths, which is the honest reading of
//     "no parser and no pointer chasing".
//
//   * Claim B's threat model gives the adversary the ciphertext and the full
//     value codebook. Not chosen-plaintext access, not timing, not the ability
//     to make the holder encode anything. A real deployment gives away more.
//
//   * Section B5 assumes the adversary can GROUP records that share a hidden
//     value ("everyone admitted to ward 3"). That is an assumption about the
//     world, not about the algebra. Where it does not hold, that attack does
//     not run; where it does, the numbers are what happens.

#include "khora/lattice/lattice.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <span>
#include <string>
#include <utility>
#include <vector>

using khora::lattice::Glyph;
using khora::lattice::bind;
using khora::lattice::bundle;
using khora::lattice::kGlyphBits;
using khora::lattice::position_glyph;

namespace {

// 95% Wilson interval on a proportion, as percentages. Same helper as
// extraction_bench.cpp and substrate_bench.cpp -- near the collapse point the
// difference between two rows is a handful of events, and a bare percentage
// invites reading that as a result.
std::pair<double, double> wilson(std::size_t hits, std::size_t n) {
    if (n == 0) return {0.0, 0.0};
    const double z = 1.96, ph = static_cast<double>(hits) / static_cast<double>(n);
    const double d = 1.0 + z * z / static_cast<double>(n);
    const double c = ph + z * z / (2.0 * static_cast<double>(n));
    const double m = z * std::sqrt(ph * (1.0 - ph) / static_cast<double>(n)
                                   + z * z / (4.0 * static_cast<double>(n) * static_cast<double>(n)));
    return {100.0 * (c - m) / d, 100.0 * (c + m) / d};
}

double pct(std::size_t hits, std::size_t n) {
    return n ? 100.0 * static_cast<double>(hits) / static_cast<double>(n) : 0.0;
}

Glyph rnd(std::mt19937_64& rng) { return Glyph::random(rng() | 1ULL); }

std::vector<Glyph> make_book(std::mt19937_64& rng, std::size_t n) {
    std::vector<Glyph> v;
    v.reserve(n);
    for (std::size_t i = 0; i < n; ++i) v.push_back(rnd(rng));
    return v;
}

// Cleanup: snap a noisy glyph to the nearest entry of a codebook. The margin to
// the runner-up is carried because a top-1 similarity of 0.06 means nothing on
// its own -- 0.06 against 0.05 is a coin flip that happened to land, and 0.06
// against 0.01 is a retrieval.
struct Best {
    std::size_t idx    = 0;
    double      sim    = -2.0;
    double      margin = 0.0;
};

Best nearest(const Glyph& probe, const std::vector<Glyph>& book) {
    Best b;
    double second = -2.0;
    for (std::size_t i = 0; i < book.size(); ++i) {
        const double s = probe.similarity(book[i]);
        if (s > b.sim) { second = b.sim; b.sim = s; b.idx = i; }
        else if (s > second) { second = s; }
    }
    b.margin = b.sim - second;
    return b;
}

// ============================================================================
// CLAIM A -- NESTED STRUCTURE IN A FLAT VECTOR
// ============================================================================

// Role glyphs come from from_hash, not from a counter, so a tree encoded in one
// process decodes in another and after a restart. (Section B3 measures what
// that choice costs on the privacy side -- the two claims pull against each
// other and this bench reports both.)
const Glyph& role_op()    { static const Glyph g = Glyph::from_hash("tree.op");    return g; }
const Glyph& role_left()  { static const Glyph g = Glyph::from_hash("tree.left");  return g; }
const Glyph& role_right() { static const Glyph g = Glyph::from_hash("tree.right"); return g; }

// THE ONLY DIFFERENCE BETWEEN THE ENCODERS, AND WHY THERE ARE THREE OF THEM.
//
// A path's probe glyph is the XOR of its step roles, and XOR is commutative and
// involutive, so the probe depends only on WHICH step roles appear an odd number
// of times -- not on their order and not on how often. Each tag closes one hole
// in that, and the middle one is a trap worth measuring rather than skipping:
//
//   None      the same LEFT glyph at every level. RIGHT,RIGHT,LEFT == LEFT.
//   Depth     XOR in position_glyph(depth). Repeated roles no longer cancel,
//             but LEFT@0 ^ RIGHT@1 and RIGHT@0 ^ LEFT@1 are still the same
//             glyph, so a transposed path still collides. Half a fix.
//   RoleDepth position_glyph(2*depth + side): a distinct glyph per (role, level).
//             Distinct subsets of independent random glyphs do not XOR to the
//             same value, so no two paths collide.
//
// position_glyph is already in the algebra for marking slots and costs one
// word-parallel XOR. position_glyph(0) is the zero glyph, so LEFT at the root is
// untagged and all three encoders agree on a depth-1 read.
enum class Tag { None, Depth, RoleDepth };

Glyph step_role(bool right, std::size_t depth, Tag tag) {
    const Glyph& base = right ? role_right() : role_left();
    switch (tag) {
        case Tag::Depth:     return bind(base, position_glyph(depth));
        case Tag::RoleDepth: return bind(base, position_glyph(2 * depth + (right ? 1 : 0)));
        default:             return base;
    }
}

Glyph op_role(std::size_t depth, Tag tag) {
    if (tag == Tag::None) return role_op();
    return bind(role_op(), position_glyph(tag == Tag::Depth ? depth : 2 * depth));
}

struct Node {
    int op  = -1;         // index into the operator codebook, internal nodes only
    int sym = -1;         // index into the symbol codebook; >= 0 means LEAF
    int l   = -1, r = -1; // child indices into the node arena
};

// The whole tree becomes one glyph of kGlyphBits. Every internal node is a
// three-way bundle regardless of how big the subtree under it is, which is the
// mechanical reason section A2 finds SIZE nearly free and DEPTH expensive.
Glyph encode(const std::vector<Node>& t, int i, std::size_t depth,
             const std::vector<Glyph>& syms, const std::vector<Glyph>& ops, Tag tag) {
    const Node& n = t[static_cast<std::size_t>(i)];
    if (n.sym >= 0) return syms[static_cast<std::size_t>(n.sym)];
    const Glyph parts[3] = {
        bind(op_role(depth, tag),          ops[static_cast<std::size_t>(n.op)]),
        bind(step_role(false, depth, tag), encode(t, n.l, depth + 1, syms, ops, tag)),
        bind(step_role(true,  depth, tag), encode(t, n.r, depth + 1, syms, ops, tag)),
    };
    return bundle(std::span<const Glyph>(parts, 3));
}

// THE DUMB BASELINE for claim A: throw the roles away and bundle every symbol
// and operator the tree mentions. A structure-blind bag-of-tokens encoder.
// Section A4 compares against it.
void collect_symbols(const std::vector<Node>& t, int i,
                     const std::vector<Glyph>& syms, const std::vector<Glyph>& ops,
                     std::vector<Glyph>& out) {
    const Node& n = t[static_cast<std::size_t>(i)];
    if (n.sym >= 0) { out.push_back(syms[static_cast<std::size_t>(n.sym)]); return; }
    out.push_back(ops[static_cast<std::size_t>(n.op)]);
    collect_symbols(t, n.l, syms, ops, out);
    collect_symbols(t, n.r, syms, ops, out);
}

Glyph encode_bag(const std::vector<Node>& t, int root,
                 const std::vector<Glyph>& syms, const std::vector<Glyph>& ops) {
    std::vector<Glyph> parts;
    collect_symbols(t, root, syms, ops, parts);
    return bundle(std::span<const Glyph>(parts));
}

struct Leaf {
    std::vector<int> path; // 0 = LEFT, 1 = RIGHT, root to leaf
    int              sym = -1;
};

void collect_leaves(const std::vector<Node>& t, int i, std::vector<int>& path,
                    std::vector<Leaf>& out) {
    const Node& n = t[static_cast<std::size_t>(i)];
    if (n.sym >= 0) { out.push_back(Leaf{path, n.sym}); return; }
    path.push_back(0); collect_leaves(t, n.l, path, out); path.pop_back();
    path.push_back(1); collect_leaves(t, n.r, path, out); path.pop_back();
}

// Retrieval: one XOR per step of the path. No node lookup, no dereference; the
// whole traversal is d word-parallel XORs over a fixed-width array.
Glyph follow(Glyph g, const std::vector<int>& path, Tag tag) {
    for (std::size_t d = 0; d < path.size(); ++d) g = bind(g, step_role(path[d] == 1, d, tag));
    return g;
}

// A right-leaning spine, so exactly one LEFT leaf sits at every depth 1..D and
// every depth is read out of the SAME encoded object. Using D separate trees
// would confound depth with whatever else changed between them.
int build_spine(std::vector<Node>& t, int depth, std::mt19937_64& rng, int nsyms, int nops) {
    Node lf;
    lf.sym = static_cast<int>(rng() % static_cast<unsigned>(nsyms));
    t.push_back(lf);
    const int li = static_cast<int>(t.size()) - 1;

    int ri;
    if (depth <= 1) {
        Node lf2;
        lf2.sym = static_cast<int>(rng() % static_cast<unsigned>(nsyms));
        t.push_back(lf2);
        ri = static_cast<int>(t.size()) - 1;
    } else {
        ri = build_spine(t, depth - 1, rng, nsyms, nops);
    }

    Node n;
    n.op = static_cast<int>(rng() % static_cast<unsigned>(nops));
    n.l  = li;
    n.r  = ri;
    t.push_back(n);
    return static_cast<int>(t.size()) - 1;
}

int build_random(std::vector<Node>& t, int nleaves, std::mt19937_64& rng, int nsyms, int nops) {
    if (nleaves <= 1) {
        Node lf;
        lf.sym = static_cast<int>(rng() % static_cast<unsigned>(nsyms));
        t.push_back(lf);
        return static_cast<int>(t.size()) - 1;
    }
    const int k = 1 + static_cast<int>(rng() % static_cast<unsigned>(nleaves - 1));
    const int l = build_random(t, k, rng, nsyms, nops);
    const int r = build_random(t, nleaves - k, rng, nsyms, nops);
    Node n;
    n.op = static_cast<int>(rng() % static_cast<unsigned>(nops));
    n.l  = l;
    n.r  = r;
    t.push_back(n);
    return static_cast<int>(t.size()) - 1;
}

constexpr int kSpineDepth = 8;

struct DepthStats {
    std::vector<std::size_t> n, hit, alias;
    std::vector<double>      sim, margin;
    explicit DepthStats(std::size_t d)
        : n(d + 1, 0), hit(d + 1, 0), alias(d + 1, 0), sim(d + 1, 0.0), margin(d + 1, 0.0) {}
};

// One encoder, one spine, every depth probed. `alias` counts the specific
// failure the untagged encoder has: a depth-d query answered with the leaf
// hanging at depth d-2, which is where the parity of that path lands.
DepthStats spine_probe(std::mt19937_64& rng, std::size_t trials, std::size_t nsyms, Tag tag) {
    DepthStats s(kSpineDepth);
    for (std::size_t t = 0; t < trials; ++t) {
        const auto syms = make_book(rng, nsyms);
        const auto ops  = make_book(rng, 4);

        std::vector<Node> arena;
        const int root = build_spine(arena, kSpineDepth, rng, static_cast<int>(nsyms), 4);
        const Glyph tree = encode(arena, root, 0, syms, ops, tag);

        std::vector<Leaf> leaves;
        std::vector<int>  path;
        collect_leaves(arena, root, path, leaves);

        // depth -> symbol of the LEFT leaf hanging there, for the alias check
        std::vector<int> at_depth(kSpineDepth + 1, -1);
        for (const Leaf& lf : leaves)
            if (!lf.path.empty() && lf.path.back() == 0) at_depth[lf.path.size()] = lf.sym;

        for (const Leaf& lf : leaves) {
            const std::size_t d = lf.path.size();
            if (d == 0 || d > static_cast<std::size_t>(kSpineDepth)) continue;
            // Two leaves sit at the deepest level; keep the LEFT one so every
            // row of the table is one probe per tree.
            if (lf.path.back() != 0) continue;
            const Glyph probe = follow(tree, lf.path, tag);
            const Best  b     = nearest(probe, syms);
            ++s.n[d];
            if (b.idx == static_cast<std::size_t>(lf.sym)) ++s.hit[d];
            else if (d >= 3 && at_depth[d - 2] >= 0 &&
                     b.idx == static_cast<std::size_t>(at_depth[d - 2])) ++s.alias[d];
            s.sim[d]    += probe.similarity(syms[static_cast<std::size_t>(lf.sym)]);
            s.margin[d] += b.margin;
        }
    }
    return s;
}

void claim_a_depth(std::mt19937_64& rng) {
    constexpr std::size_t kSyms   = 64;
    constexpr std::size_t kTrials = 2000;

    const DepthStats naive  = spine_probe(rng, kTrials, kSyms, Tag::None);
    const DepthStats tagged = spine_probe(rng, kTrials, kSyms, Tag::RoleDepth);

    std::printf("\n  A1a. UNTAGGED ROLES -- THE ENCODING AS USUALLY WRITTEN\n"
                "       (codebook 64 symbols, 4 operators, right-leaning spine of depth 8,\n"
                "        %zu trials, fresh codebook each trial; chance = 1/64 = 1.56%%)\n\n", kTrials);
    std::printf("       depth | probes | correct |   acc  | 95%% CI            | answered with | sim to\n"
                "             |        |         |        |                   | the d-2 leaf  | truth\n");
    std::printf("       ------+--------+---------+--------+-------------------+---------------+--------\n");
    for (int d = 1; d <= kSpineDepth; ++d) {
        const std::size_t nn = naive.n[static_cast<std::size_t>(d)];
        if (!nn) continue;
        const std::size_t hh = naive.hit[static_cast<std::size_t>(d)];
        const auto ci = wilson(hh, nn);
        std::printf("       %5d | %6zu | %7zu | %5.1f%% | [%5.1f%%, %6.1f%%] | %12.1f%% | %6.4f\n",
                    d, nn, hh, pct(hh, nn), ci.first, ci.second,
                    pct(naive.alias[static_cast<std::size_t>(d)], nn),
                    naive.sim[static_cast<std::size_t>(d)] / static_cast<double>(nn));
    }
    std::printf("\n       The 'd-2 leaf' column is the diagnosis, not a curiosity. XOR is\n"
                "       involutive: RIGHT,RIGHT,LEFT == LEFT as a glyph, so a depth-3 query is\n"
                "       BIT-IDENTICAL to a depth-1 query and confidently returns the wrong leaf.\n"
                "       'sim to truth' stays above the 0.010 noise floor down to depth 6 -- the\n"
                "       information is still in there, the addressing is what broke.\n");

    std::printf("\n  A1b. ROLE+DEPTH-TAGGED ROLES -- step_role(side, d) =\n"
                "       bind(LEFT_or_RIGHT, position_glyph(2*d + side))\n"
                "       (same trees, same codebooks, one extra XOR per step)\n\n");
    std::printf("       depth | probes | correct |   acc  | 95%% CI            | sim to truth | margin | chance\n");
    std::printf("       ------+--------+---------+--------+-------------------+--------------+--------+-------\n");
    for (int d = 1; d <= kSpineDepth; ++d) {
        const std::size_t nn = tagged.n[static_cast<std::size_t>(d)];
        if (!nn) continue;
        const std::size_t hh = tagged.hit[static_cast<std::size_t>(d)];
        const auto ci = wilson(hh, nn);
        std::printf("       %5d | %6zu | %7zu | %5.1f%% | [%5.1f%%, %6.1f%%] | %12.4f | %6.4f | %5.2f%%\n",
                    d, nn, hh, pct(hh, nn), ci.first, ci.second,
                    tagged.sim[static_cast<std::size_t>(d)] / static_cast<double>(nn),
                    tagged.margin[static_cast<std::size_t>(d)] / static_cast<double>(nn),
                    100.0 / static_cast<double>(kSyms));
    }
    std::printf("\n       'sim to truth' is the unbound glyph's similarity to the symbol that\n"
                "       really is there, before cleanup, and it halves per level: one component\n"
                "       of a majority-of-3 bundle agrees with the result on 3 bits in 4. Two\n"
                "       unrelated glyphs sit at 0.000 +- 0.010, which is the floor it is\n"
                "       heading for and the reason the table ends where it does.\n");
}

// The parity collapse as an identity rather than a rate: are two different
// paths literally the same glyph?
void claim_a_parity(std::mt19937_64& rng) {
    const auto syms = make_book(rng, 8);
    const auto ops  = make_book(rng, 2);
    std::vector<Node> arena;
    const int root = build_spine(arena, 4, rng, 8, 2);

    struct Probe { const char* name; std::vector<int> path; };
    const Probe a{"LEFT",             {0}};
    const Probe b{"RIGHT,RIGHT,LEFT", {1, 1, 0}};
    const Probe c{"LEFT,RIGHT",       {0, 1}};
    const Probe d{"RIGHT,LEFT",       {1, 0}};

    std::printf("\n  A1c. THE COLLAPSE AS AN IDENTITY, NOT A RATE\n\n");
    std::printf("       two queries against the same tree | untagged | depth tag | role+depth tag\n");
    std::printf("       ----------------------------------+----------+-----------+---------------\n");
    const std::pair<const Probe*, const Probe*> pairs[] = {{&a, &b}, {&c, &d}};
    const Tag tags[3] = {Tag::None, Tag::Depth, Tag::RoleDepth};
    for (const auto& pr : pairs) {
        char label[80];
        std::snprintf(label, sizeof(label), "%s vs %s", pr.first->name, pr.second->name);
        double sim[3];
        for (int ti = 0; ti < 3; ++ti) {
            const Glyph tree = encode(arena, root, 0, syms, ops, tags[ti]);
            sim[ti] = follow(tree, pr.first->path, tags[ti])
                          .similarity(follow(tree, pr.second->path, tags[ti]));
        }
        std::printf("       %-33s | %8.4f | %9.4f | %14.4f\n", label, sim[0], sim[1], sim[2]);
    }
    std::printf("\n       1.0000 means the two queries are literally the same bits, which no\n"
                "       dimension and no codebook can fix. The middle column is why the obvious\n"
                "       half-fix is not enough: tagging by depth alone stops a role cancelling\n"
                "       against itself but leaves a transposed path identical, because\n"
                "       LEFT@0 ^ RIGHT@1 and RIGHT@0 ^ LEFT@1 are the same XOR.\n");
}

void claim_a_size(std::mt19937_64& rng) {
    constexpr std::size_t kSyms   = 64;
    constexpr std::size_t kTrials = 300;
    const int leaf_counts[] = {2, 4, 8, 16, 32, 64, 128};

    std::printf("\n  A2. THE ROLE+DEPTH-TAGGED ENCODER BY TREE SIZE  (random tree shapes,\n"
                "      %zu trials each, every leaf probed; chance = 1/64 = 1.56%%)\n\n", kTrials);
    std::printf("      leaves | nodes | mean d | max d | leaves probed | all-depth acc | 95%% CI            | acc at depth 2 (n)\n");
    std::printf("      -------+-------+--------+-------+---------------+---------------+-------------------+-------------------\n");

    for (const int nl : leaf_counts) {
        std::size_t hit = 0, tot = 0, hit_d2 = 0, n_d2 = 0, nodes = 0, max_d = 0;
        double depth_sum = 0.0;

        for (std::size_t t = 0; t < kTrials; ++t) {
            const auto syms = make_book(rng, kSyms);
            const auto ops  = make_book(rng, 4);

            std::vector<Node> arena;
            const int root = build_random(arena, nl, rng, static_cast<int>(kSyms), 4);
            const Glyph tree = encode(arena, root, 0, syms, ops, Tag::RoleDepth);
            nodes += arena.size();

            std::vector<Leaf> leaves;
            std::vector<int>  path;
            collect_leaves(arena, root, path, leaves);

            for (const Leaf& lf : leaves) {
                const std::size_t d = lf.path.size();
                depth_sum += static_cast<double>(d);
                max_d = std::max(max_d, d);
                const Best b  = nearest(follow(tree, lf.path, Tag::RoleDepth), syms);
                const bool ok = (b.idx == static_cast<std::size_t>(lf.sym));
                ++tot;
                if (ok) ++hit;
                if (d == 2) { ++n_d2; if (ok) ++hit_d2; }
            }
        }

        const auto ci = wilson(hit, tot);
        std::printf("      %6d | %5.1f | %6.2f | %5zu | %13zu | %12.1f%% | [%5.1f%%, %6.1f%%] | ",
                    nl, static_cast<double>(nodes) / static_cast<double>(kTrials),
                    depth_sum / static_cast<double>(tot), max_d, tot,
                    pct(hit, tot), ci.first, ci.second);
        if (n_d2) std::printf("%5.1f%% (%zu)\n", pct(hit_d2, n_d2), n_d2);
        else      std::printf("    -- (0)\n");
    }
    std::printf("\n      The depth-2 column holds depth fixed while the tree grows around it. If\n"
                "      it stays flat, SIZE costs nothing and only DEPTH does -- every internal\n"
                "      node is a 3-way bundle no matter how big its subtree is. The all-depth\n"
                "      column falls only because a bigger random tree puts more leaves deeper.\n");
}

void claim_a_codebook(std::mt19937_64& rng) {
    const std::size_t books[] = {4, 16, 64, 256, 1024, 4096};

    std::printf("\n  A3. THE ROLE+DEPTH-TAGGED ENCODER BY CODEBOOK SIZE  (spine of depth 8; the\n"
                "      cleanup memory is the whole codebook, so this is the number of\n"
                "      distractors the final step faces)\n\n");
    std::printf("      codebook | chance | trials | acc @ d=2          | acc @ d=4          | acc @ d=6\n");
    std::printf("      ---------+--------+--------+--------------------+--------------------+-------------------\n");

    for (const std::size_t K : books) {
        const std::size_t trials = (K >= 1024) ? 200 : 600;
        const DepthStats s = spine_probe(rng, trials, K, Tag::RoleDepth);
        std::printf("      %8zu | %5.2f%% | %6zu |", K, 100.0 / static_cast<double>(K), trials);
        for (const int d : {2, 4, 6}) {
            const std::size_t n = s.n[static_cast<std::size_t>(d)], h = s.hit[static_cast<std::size_t>(d)];
            const auto ci = wilson(h, n);
            std::printf(" %5.1f%% [%4.1f,%5.1f]%s", pct(h, n), ci.first, ci.second, d == 6 ? "" : " |");
        }
        std::printf("\n");
    }
    std::printf("\n      A bigger codebook barely touches a shallow read and is fatal to a deep\n"
                "      one: the signal at depth d is fixed near 0.5^d while the largest of K\n"
                "      noise similarities grows with K. Depth causes the collapse; codebook\n"
                "      size only decides exactly where it lands.\n"
                "\n      EXCEPT THE FIRST ROW, WHICH GOES THE WRONG WAY. K=4 reads worse at\n"
                "      depth 2 than K=64 does, and that is not statistics: 0.25 of signal\n"
                "      against three distractors at 0.000 +- 0.010 should never miss. With four\n"
                "      symbols and nine leaves every symbol sits in the tree about twice, so the\n"
                "      crosstalk the unbinding leaves behind is built out of the same four\n"
                "      glyphs the cleanup is choosing between. That is the correlated-glyph\n"
                "      caveat from the header showing up inside a table, and it is the one\n"
                "      place in this bench where the i.i.d. assumption visibly breaks.\n");
}

// THE CHECK THAT DECIDES WHETHER A1-A3 MEAN ANYTHING.
void claim_a_collision(std::mt19937_64& rng) {
    const auto syms = make_book(rng, 4);   // a, b, c, d
    const auto ops  = make_book(rng, 2);   // + , *
    const int PLUS = 0, TIMES = 1;
    const int A = 0, B = 1, C = 2, D = 3;

    auto leaf = [](std::vector<Node>& t, int s) {
        Node n; n.sym = s; t.push_back(n); return static_cast<int>(t.size()) - 1;
    };
    auto inner = [](std::vector<Node>& t, int op, int l, int r) {
        Node n; n.op = op; n.l = l; n.r = r; t.push_back(n); return static_cast<int>(t.size()) - 1;
    };

    struct Case { const char* text; Glyph untagged, tagged, bag; };
    std::vector<Case> cases;
    auto add_case = [&](const char* text, std::vector<Node>& t, int root) {
        cases.push_back({text,
                         encode(t, root, 0, syms, ops, Tag::None),
                         encode(t, root, 0, syms, ops, Tag::RoleDepth),
                         encode_bag(t, root, syms, ops)});
    };

    { std::vector<Node> t; const int r = inner(t, PLUS,  leaf(t, A), inner(t, TIMES, leaf(t, B), leaf(t, C))); add_case("(a + (b * c))", t, r); }
    { std::vector<Node> t; const int r = inner(t, TIMES, inner(t, PLUS, leaf(t, A), leaf(t, B)), leaf(t, C));  add_case("((a + b) * c)", t, r); }
    { std::vector<Node> t; const int r = inner(t, PLUS,  leaf(t, A), inner(t, TIMES, leaf(t, C), leaf(t, B))); add_case("(a + (c * b))", t, r); }
    { std::vector<Node> t; const int r = inner(t, TIMES, leaf(t, A), inner(t, PLUS,  leaf(t, B), leaf(t, C))); add_case("(a * (b + c))", t, r); }
    { std::vector<Node> t; const int r = inner(t, PLUS,  leaf(t, A), inner(t, TIMES, leaf(t, B), leaf(t, C))); add_case("(a + (b * c)) [again]", t, r); }

    double sum = 0.0, sumsq = 0.0;
    constexpr std::size_t kPairs = 4000;
    for (std::size_t i = 0; i < kPairs; ++i) {
        const double s = rnd(rng).similarity(rnd(rng));
        sum += s; sumsq += s * s;
    }
    const double mu = sum / kPairs;
    const double sd = std::sqrt(sumsq / kPairs - mu * mu);

    std::printf("\n  A4. DO TWO DIFFERENT TREES OVER THE SAME SYMBOLS COLLIDE?\n\n"
                "      Every pair below is over an IDENTICAL symbol multiset -- only the\n"
                "      parentheses move. A structure-blind encoder scores 1.0000 on all of them.\n\n");
    std::printf("      pair                                            | untagged | role+dep | bag-of-symbols\n");
    std::printf("      ------------------------------------------------+----------+----------+---------------\n");
    for (std::size_t i = 0; i < cases.size(); ++i) {
        for (std::size_t j = i + 1; j < cases.size(); ++j) {
            char label[128];
            std::snprintf(label, sizeof(label), "%s  vs  %s", cases[i].text, cases[j].text);
            std::printf("      %-47s | %8.4f | %8.4f | %13.4f\n", label,
                        cases[i].untagged.similarity(cases[j].untagged),
                        cases[i].tagged.similarity(cases[j].tagged),
                        cases[i].bag.similarity(cases[j].bag));
        }
    }
    std::printf("\n      two unrelated random glyphs (the floor): %.4f +- %.4f over %zu pairs\n"
                "\n      READ THE SCALE, NOT THE SIGN. These similarities are graded, not 0-or-1.\n"
                "      Swapping the two deepest leaves leaves two of the root's three bundled\n"
                "      components untouched, and a majority-of-3 that agrees on two inputs keeps\n"
                "      about three bits in four -- hence ~0.75, which is the arithmetic of the\n"
                "      encoder rather than a collision. A collision is 1.0000, the bag column,\n"
                "      where the two trees are literally the same glyph.\n",
                mu, sd, kPairs);

    // Every distinct shape of a 4-leaf tree with the leaves in a fixed
    // left-to-right order. Catalan(3) = 5 shapes over one symbol multiset, so
    // the bag encoder cannot tell any of them apart by construction.
    struct Shape { Glyph untagged, tagged, bag; };
    std::vector<Shape> shapes;
    {
        auto add = [&](std::vector<Node>& a, int root) {
            shapes.push_back({encode(a, root, 0, syms, ops, Tag::None),
                              encode(a, root, 0, syms, ops, Tag::RoleDepth),
                              encode_bag(a, root, syms, ops)});
        };
        { std::vector<Node> a; const int r = inner(a, 0, inner(a, 1, leaf(a, A), leaf(a, B)), inner(a, 0, leaf(a, C), leaf(a, D)));  add(a, r); }
        { std::vector<Node> a; const int r = inner(a, 0, leaf(a, A), inner(a, 1, leaf(a, B), inner(a, 0, leaf(a, C), leaf(a, D))));  add(a, r); }
        { std::vector<Node> a; const int r = inner(a, 0, leaf(a, A), inner(a, 1, inner(a, 0, leaf(a, B), leaf(a, C)), leaf(a, D)));  add(a, r); }
        { std::vector<Node> a; const int r = inner(a, 0, inner(a, 1, leaf(a, A), inner(a, 0, leaf(a, B), leaf(a, C))), leaf(a, D));  add(a, r); }
        { std::vector<Node> a; const int r = inner(a, 0, inner(a, 1, inner(a, 0, leaf(a, A), leaf(a, B)), leaf(a, C)), leaf(a, D));  add(a, r); }
    }
    double ulo = 2, uhi = -2, uacc = 0, tlo = 2, thi = -2, tacc = 0, blo = 2, bhi = -2, bacc = 0;
    std::size_t np = 0;
    for (std::size_t i = 0; i < shapes.size(); ++i)
        for (std::size_t j = i + 1; j < shapes.size(); ++j) {
            const double u  = shapes[i].untagged.similarity(shapes[j].untagged);
            const double tg = shapes[i].tagged.similarity(shapes[j].tagged);
            const double b  = shapes[i].bag.similarity(shapes[j].bag);
            ulo = std::min(ulo, u);  uhi = std::max(uhi, u);  uacc += u;
            tlo = std::min(tlo, tg); thi = std::max(thi, tg); tacc += tg;
            blo = std::min(blo, b);  bhi = std::max(bhi, b);  bacc += b;
            ++np;
        }
    const double dn = static_cast<double>(np);
    std::printf("\n      all %zu distinct shapes of a 4-leaf tree, same symbols in the same\n"
                "      left-to-right order, %zu pairs:\n", shapes.size(), np);
    std::printf("        untagged      : min %.4f  mean %.4f  max %.4f\n", ulo, uacc / dn, uhi);
    std::printf("        role+depth tag: min %.4f  mean %.4f  max %.4f\n", tlo, tacc / dn, thi);
    std::printf("        bag-of-symbols: min %.4f  mean %.4f  max %.4f   <-- the dumb baseline\n",
                blo, bacc / dn, bhi);
}

// ============================================================================
// CLAIM B -- HOLOGRAPHIC PRIVACY
// ============================================================================

constexpr std::size_t kValues = 128; // value codebook the adversary is GIVEN in full

struct Record {
    Glyph                    glyph;
    std::vector<Glyph>       roles;
    std::vector<std::size_t> values; // indices into the value codebook
};

Record make_record(std::mt19937_64& rng, const std::vector<Glyph>& book, std::size_t fields) {
    Record r;
    std::vector<Glyph> parts;
    parts.reserve(fields);
    for (std::size_t f = 0; f < fields; ++f) {
        // Distinct values per record so "which values are in it" has exactly
        // `fields` right answers and precision@F is well posed.
        std::size_t v;
        do { v = rng() % book.size(); }
        while (std::find(r.values.begin(), r.values.end(), v) != r.values.end());
        r.values.push_back(v);
        r.roles.push_back(rnd(rng));
        parts.push_back(bind(r.roles.back(), book[v]));
    }
    r.glyph = bundle(std::span<const Glyph>(parts));
    return r;
}

// Rank the whole codebook by similarity to a probe and count how many of the
// top-F entries really are in the record. Precision@F is the right metric: the
// adversary is asked "which values are in this record", and naming F at random
// already gets F/K of them.
std::size_t precision_at_f(const Glyph& probe, const std::vector<Glyph>& book,
                           const std::vector<std::size_t>& members) {
    const std::size_t f = members.size();
    std::vector<std::pair<double, std::size_t>> rank;
    rank.reserve(book.size());
    for (std::size_t i = 0; i < book.size(); ++i) rank.emplace_back(probe.similarity(book[i]), i);
    std::partial_sort(rank.begin(), rank.begin() + static_cast<std::ptrdiff_t>(f), rank.end(),
                      [](const auto& a, const auto& b) { return a.first > b.first; });
    std::size_t hits = 0;
    for (std::size_t i = 0; i < f; ++i)
        if (std::find(members.begin(), members.end(), rank[i].second) != members.end()) ++hits;
    return hits;
}

void claim_b_basic(std::mt19937_64& rng) {
    const std::size_t field_counts[] = {2, 3, 4, 6, 8, 12, 16};
    constexpr std::size_t kTrials  = 400;
    constexpr std::size_t kGuesses = 8; // random role glyphs tried per record

    std::printf("\n  B1. ADVERSARY HOLDS THE RECORD AND THE WHOLE %zu-VALUE CODEBOOK BUT NOT\n"
                "      THE ROLE GLYPHS  (%zu records per row, role glyphs drawn i.i.d.)\n\n",
                kValues, kTrials);
    std::printf("      fields | chance | direct sim: top1 in rec | prec@F | guessed role: top1 in rec | prec@F\n");
    std::printf("      -------+--------+-------------------------+--------+---------------------------+--------\n");

    struct WithRole { std::size_t hit = 0, n = 0; double sim = 0.0, margin = 0.0; };
    std::vector<WithRole> ctrl;

    for (const std::size_t F : field_counts) {
        std::size_t d_top1 = 0, d_prec = 0, d_precn = 0;
        std::size_t g_top1 = 0, g_top1n = 0, g_prec = 0, g_precn = 0;
        WithRole wr;

        for (std::size_t t = 0; t < kTrials; ++t) {
            const auto book = make_book(rng, kValues);
            const Record r  = make_record(rng, book, F);

            // ATTACK 1 -- direct similarity of the record to each candidate.
            const Best b = nearest(r.glyph, book);
            if (std::find(r.values.begin(), r.values.end(), b.idx) != r.values.end()) ++d_top1;
            d_prec  += precision_at_f(r.glyph, book, r.values);
            d_precn += F;

            // ATTACK 2 -- unbind with a role glyph the adversary guessed at
            // random. Scored per guess, because the adversary cannot tell a good
            // guess from a bad one; taking the best of 8 would score an oracle.
            for (std::size_t g = 0; g < kGuesses; ++g) {
                const Glyph probe = bind(r.glyph, rnd(rng));
                const Best  gb    = nearest(probe, book);
                ++g_top1n;
                if (std::find(r.values.begin(), r.values.end(), gb.idx) != r.values.end()) ++g_top1;
                g_prec  += precision_at_f(probe, book, r.values);
                g_precn += F;
            }

            // THE CONTROL -- the same adversary handed the role glyph.
            for (std::size_t f = 0; f < F; ++f) {
                const Glyph probe = bind(r.glyph, r.roles[f]);
                const Best  cb    = nearest(probe, book);
                ++wr.n;
                if (cb.idx == r.values[f]) ++wr.hit;
                wr.sim    += probe.similarity(book[r.values[f]]);
                wr.margin += cb.margin;
            }
        }
        ctrl.push_back(wr);

        const auto c1 = wilson(d_top1, kTrials);
        const auto c3 = wilson(g_top1, g_top1n);
        std::printf("      %6zu | %5.2f%% | %5.1f%% [%4.1f,%5.1f]     | %5.1f%%  | %5.1f%% [%4.1f,%5.1f]       | %5.1f%%\n",
                    F, 100.0 * static_cast<double>(F) / static_cast<double>(kValues),
                    pct(d_top1, kTrials), c1.first, c1.second, pct(d_prec, d_precn),
                    pct(g_top1, g_top1n), c3.first, c3.second, pct(g_prec, g_precn));
    }
    std::printf("\n      Chance for every column is F/%zu. A row that does not clear its own\n"
                "      chance column is an attack that failed.\n", kValues);

    std::printf("\n  B2. THE CONTRAST: THE SAME ADVERSARY, NOW GIVEN THE ROLE GLYPH\n\n");
    std::printf("      fields | probes | correct value recovered | 95%% CI            | sim   | margin | chance\n");
    std::printf("      -------+--------+-------------------------+-------------------+-------+--------+-------\n");
    for (std::size_t i = 0; i < ctrl.size(); ++i) {
        const WithRole& w = ctrl[i];
        const auto ci = wilson(w.hit, w.n);
        std::printf("      %6zu | %6zu | %22.1f%% | [%5.1f%%, %6.1f%%] | %5.3f | %6.4f | %5.2f%%\n",
                    field_counts[i], w.n, pct(w.hit, w.n), ci.first, ci.second,
                    w.sim / static_cast<double>(w.n), w.margin / static_cast<double>(w.n),
                    100.0 / static_cast<double>(kValues));
    }
}

// THE ATTACK THAT WINS AGAINST THE DEPLOYED CODE RATHER THAN AGAINST THE ALGEBRA.
//
// Chiasm::role_glyph is Glyph::from_hash(role_name), chosen so an archive stays
// readable across processes and restarts. The role glyph is therefore a public
// function of a short English word, and B1's "the adversary does not have the
// role glyph" holds only if the adversary cannot guess the word. Measure that.
void claim_b_rolename(std::mt19937_64& rng) {
    const char* names[] = {
        "name", "age", "sex", "dob", "ssn", "address", "phone", "email",
        "diagnosis", "medication", "dose", "allergy", "weight", "height",
        "bp", "hr", "temp", "notes", "doctor", "ward", "admitted",
        "discharged", "insurer", "mrn"
    };
    constexpr std::size_t kNames  = sizeof(names) / sizeof(names[0]);
    constexpr std::size_t kFields = 3;
    constexpr std::size_t kTrials = 500;

    std::size_t role_hits = 0, pair_hits = 0;
    for (std::size_t t = 0; t < kTrials; ++t) {
        const auto book = make_book(rng, kValues);

        std::vector<std::size_t> used, vals;
        std::vector<Glyph> parts;
        for (std::size_t f = 0; f < kFields; ++f) {
            std::size_t k;
            do { k = rng() % kNames; } while (std::find(used.begin(), used.end(), k) != used.end());
            used.push_back(k);
            vals.push_back(rng() % kValues);
            parts.push_back(bind(Glyph::from_hash(names[k]), book[vals.back()]));
        }
        const Glyph record = bundle(std::span<const Glyph>(parts));

        // Try every plausible field name, unbind, clean up. A wrong name yields
        // noise, so the top-1 similarity itself sorts the real fields forward.
        std::vector<std::pair<double, std::size_t>> score;
        std::vector<std::size_t> guessed_value(kNames, 0);
        for (std::size_t k = 0; k < kNames; ++k) {
            const Best b = nearest(bind(record, Glyph::from_hash(names[k])), book);
            score.emplace_back(b.sim, k);
            guessed_value[k] = b.idx;
        }
        std::partial_sort(score.begin(), score.begin() + kFields, score.end(),
                          [](const auto& a, const auto& b) { return a.first > b.first; });
        for (std::size_t i = 0; i < kFields; ++i) {
            const auto it = std::find(used.begin(), used.end(), score[i].second);
            if (it == used.end()) continue;
            ++role_hits;
            if (guessed_value[score[i].second] == vals[static_cast<std::size_t>(it - used.begin())]) ++pair_hits;
        }
    }

    const std::size_t n = kTrials * kFields;
    const auto cr = wilson(role_hits, n), cp = wilson(pair_hits, n);
    std::printf("\n  B3. THE ROLE GLYPH IS NOT A SECRET IN THE DEPLOYED CODE.\n\n"
                "      Chiasm derives it as from_hash(role_name). Adversary with a %zu-word\n"
                "      dictionary of plausible field names, %zu fields per record, %zu records:\n\n",
                kNames, kFields, kTrials);
    std::printf("        top-%zu guessed names that are really in the record : %5.1f%% [%4.1f, %5.1f]  (chance %.1f%%)\n",
                kFields, pct(role_hits, n), cr.first, cr.second,
                100.0 * static_cast<double>(kFields) / static_cast<double>(kNames));
    std::printf("        full (role, value) pairs recovered exactly        : %5.1f%% [%4.1f, %5.1f]  (chance %.2f%%)\n",
                pct(pair_hits, n), cp.first, cp.second,
                100.0 * static_cast<double>(kFields) / (static_cast<double>(kNames) * static_cast<double>(kValues)));
    std::printf("\n      B1's protection is real, but it is protection by a secret, and this is\n"
                "      how much entropy that secret has when the role name is an English word.\n");
}

// Does averaging many records that share a role leak the role glyph?
//
// bundle() majority-votes and throws the counts away, so this adversary keeps
// them: for each bit, how many of the M records set it. That is strictly more
// information than the substrate's own operator retains, and it is the
// strongest form of the averaging attack available without the role glyph.
void claim_b_averaging(std::mt19937_64& rng) {
    const std::size_t counts[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 1024};
    constexpr std::size_t kFields = 4;
    constexpr std::size_t kTrials = 20;
    constexpr std::size_t kDecoys = 64;

    std::printf("\n  B4. MANY RECORDS SHARING ONE ROLE, ALL WITH DIFFERENT VALUES: DOES\n"
                "      AVERAGING LEAK THE ROLE GLYPH?  (%zu fields per record, bit-frequency\n"
                "      attack, %zu trials per row)\n\n", kFields, kTrials);
    std::printf("      records | score(true role) | best of %zu decoy glyphs | leak?\n", kDecoys);
    std::printf("      --------+------------------+-------------------------+------\n");

    for (const std::size_t M : counts) {
        double true_score = 0.0, decoy_score = 0.0;
        for (std::size_t t = 0; t < kTrials; ++t) {
            const auto  book   = make_book(rng, kValues);
            const Glyph shared = rnd(rng);

            std::vector<int> freq(kGlyphBits, 0);
            for (std::size_t m = 0; m < M; ++m) {
                std::vector<Glyph> parts;
                parts.push_back(bind(shared, book[rng() % kValues]));
                for (std::size_t f = 1; f < kFields; ++f)
                    parts.push_back(bind(rnd(rng), book[rng() % kValues]));
                const Glyph rec = bundle(std::span<const Glyph>(parts));
                for (std::size_t b = 0; b < kGlyphBits; ++b) if (rec.bit(b)) ++freq[b];
            }

            // Correlation of the bit-frequency vector with a candidate glyph,
            // normalised by M so the rows are comparable.
            auto corr = [&](const Glyph& g) {
                double s = 0.0;
                for (std::size_t b = 0; b < kGlyphBits; ++b) {
                    const double f = (2.0 * freq[b] - static_cast<double>(M)) / static_cast<double>(M);
                    s += f * (g.bit(b) ? 1.0 : -1.0);
                }
                return s / static_cast<double>(kGlyphBits);
            };

            true_score += std::fabs(corr(shared));
            double best = 0.0;
            for (std::size_t d = 0; d < kDecoys; ++d) best = std::max(best, std::fabs(corr(rnd(rng))));
            decoy_score += best;
        }
        const double ts = true_score / kTrials, ds = decoy_score / kTrials;
        std::printf("      %7zu | %16.5f | %23.5f | %s\n", M, ts, ds, (ts > ds) ? "YES" : "no");
    }
    std::printf("\n      The decoy column is the best score any of %zu unrelated glyphs reached\n"
                "      on the same data -- the bar the true role must clear to have leaked. The\n"
                "      values under the shared role are all DIFFERENT here. B5 is the case where\n"
                "      they are not, and that is the one that breaks.\n", kDecoys);
}

// THE ATTACK MOST LIKELY TO BREAK IT, RUN.
//
// Two groups of records, each group sharing one role AND one value ("everyone
// in ward 3" against "everyone in ward 7"). Bundling a group makes the repeated
// bind(role, value) the only non-random component, so it survives while
// everything else averages to noise. XOR the two group glyphs and the role
// cancels -- it is the same glyph on both sides -- leaving value0 ^ value1. The
// adversary holds the codebook, so it searches all K(K-1)/2 pairs for the XOR
// that matches. Classic two-time-pad reuse, and an involutive bind invites it.
void claim_b_repeated(std::mt19937_64& rng) {
    const std::size_t fields[] = {1, 2, 4, 8, 16, 32, 64};
    const std::size_t groups[] = {1, 4, 16, 64};
    constexpr std::size_t kTrials = 60;
    const std::size_t npairs = kValues * (kValues - 1) / 2;

    std::printf("\n  B5. REPEATED VALUE UNDER A SHARED ROLE: THE TWO-TIME-PAD ATTACK\n"
                "      (chance = 1 in %zu unordered codebook pairs = %.4f%%, %zu trials/row)\n\n",
                npairs, 100.0 / static_cast<double>(npairs), kTrials);
    std::printf("      fields | recs/group | sim(group, role^value) | sim(delta, v0^v1) | pair recovered | 95%% CI\n");
    std::printf("      -------+------------+------------------------+-------------------+----------------+-------------------\n");

    for (const std::size_t F : fields) {
        for (const std::size_t M : groups) {
            std::size_t hit = 0;
            double emerge = 0.0, dsim = 0.0;

            for (std::size_t t = 0; t < kTrials; ++t) {
                const auto  book = make_book(rng, kValues);
                const Glyph role = rnd(rng);
                const std::size_t v0 = rng() % kValues;
                std::size_t v1;
                do { v1 = rng() % kValues; } while (v1 == v0);

                auto group = [&](std::size_t v) {
                    std::vector<Glyph> recs;
                    recs.reserve(M);
                    for (std::size_t m = 0; m < M; ++m) {
                        std::vector<Glyph> parts;
                        parts.push_back(bind(role, book[v]));
                        for (std::size_t f = 1; f < F; ++f)
                            parts.push_back(bind(rnd(rng), book[rng() % kValues]));
                        recs.push_back(bundle(std::span<const Glyph>(parts)));
                    }
                    return bundle(std::span<const Glyph>(recs));
                };

                const Glyph g0 = group(v0);
                const Glyph g1 = group(v1);
                emerge += g0.similarity(bind(role, book[v0]));

                const Glyph delta = bind(g0, g1);
                dsim += delta.similarity(bind(book[v0], book[v1]));

                // Search every unordered pair. delta ^ book[a] is hoisted out of
                // the inner loop so the inner step is one hamming.
                double best = -2.0;
                std::size_t ba = 0, bb = 0;
                for (std::size_t a = 0; a < kValues; ++a) {
                    const Glyph da = bind(delta, book[a]);
                    for (std::size_t b = a + 1; b < kValues; ++b) {
                        const double s = da.similarity(book[b]);
                        if (s > best) { best = s; ba = a; bb = b; }
                    }
                }
                if ((ba == v0 && bb == v1) || (ba == v1 && bb == v0)) ++hit;
            }

            const auto ci = wilson(hit, kTrials);
            std::printf("      %6zu | %10zu | %22.4f | %17.4f | %13.1f%% | [%5.1f%%, %6.1f%%]\n",
                        F, M, emerge / static_cast<double>(kTrials),
                        dsim / static_cast<double>(kTrials),
                        pct(hit, kTrials), ci.first, ci.second);
        }
    }
    std::printf("\n      The adversary never sees a role glyph and never sees a value in the\n"
                "      clear. It sees two piles of records and the public codebook. Wherever\n"
                "      'pair recovered' clears chance, holographic privacy does not hold for\n"
                "      that shape of data.\n");
}

} // namespace

int main() {
    std::printf("\n=== structure_bench: two properties of binding, measured ===\n");
    std::printf("    glyph width %zu bits\n", kGlyphBits);

    std::mt19937_64 rng(0xB17D19E5ULL);

    std::printf("\n"
                "--------------------------------------------------------------------------\n"
                " CLAIM A: a tree fits in one fixed-width glyph and its parts read back out\n"
                "--------------------------------------------------------------------------\n");
    claim_a_depth(rng);
    claim_a_parity(rng);
    claim_a_size(rng);
    claim_a_codebook(rng);
    claim_a_collision(rng);

    std::printf("\n"
                "--------------------------------------------------------------------------\n"
                " CLAIM B: a bound record hides its fields from a reader without the role\n"
                "--------------------------------------------------------------------------\n");
    claim_b_basic(rng);
    claim_b_rolename(rng);
    claim_b_averaging(rng);
    claim_b_repeated(rng);

    std::printf("\n");
    return 0;
}
