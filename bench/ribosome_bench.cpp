// CAN AN EVOLVED OPERATOR BEAT THE ONE A HUMAN WOULD WRITE?
//
// Ribosome's claim is that a population of byte tapes, selected under a fixed
// budget, can discover a composition of Khora's primitives that computes a
// relation nobody programmed. The unit test shows the machinery works on a
// synthetic relation. This is the test that decides whether it is worth
// anything: real words, a real relation with external ground truth, and the
// hand-designed answer standing next to it.
//
// THE ENVIRONMENT is Plexus -- a PPMI co-occurrence graph over the corpus, the
// world model Khora actually has. An organism senses it through two opcodes,
// Assoc and Neigh, and can compose that with bind, bundle, permute and cleanup.
//
// THE GROUND TRUTH is WordNet, which the corpus never saw. 3,373 categories,
// each listing its members. Two relations are drawn from it, and the contrast
// between them is the point:
//
//   HYPERNYM     member -> its category.  "sparrow" -> "bird"
//                A vertical relation. There is no reason a co-occurrence graph
//                should encode it: a word and its category are not especially
//                likely to appear near each other, and often the category word
//                is rarer than its members.
//
//   CO-HYPONYM   member -> any other member of the same category.
//                A horizontal relation, and the one distributional structure
//                is actually supposed to carry -- words with the same category
//                keep the same company.
//
// If the evolved operator wins on the horizontal relation and loses on the
// vertical one, that is not a mixed result. That is the method correctly
// reporting which relations are present in the environment it was given, which
// is more useful than a single number.
//
// THE BASELINES, and the fourth is the one that matters:
//
//   chance          1 / |codebook|. The floor under everything.
//   identity        output = input. Free, and non-trivial for co-hyponymy
//                   only in that it is always WRONG by construction.
//   top associate   the single most-associated word in Plexus. Thirty lines,
//                   no search, no evolution. The dumb graph baseline.
//   VSA role        r = bundle over training pairs of bind(from, to); predict
//                   cleanup(bind(from, r)). This is THE textbook answer to
//                   "learn a relation in a vector symbolic architecture", it
//                   is what every paper in the field would do here, and it is
//                   strong. If evolution cannot beat this, Ribosome is
//                   ceremony and should be recorded as such.

#include "khora/ribosome/ribosome.hpp"
#include "khora/plexus/plexus.hpp"
#include "khora/lexicon/lexicon.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <span>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace khora::ribosome;
using khora::lattice::Glyph;

namespace {

std::uint64_t rs = 0x51DEULL;
std::uint64_t rnd() {
    std::uint64_t z = (rs += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

struct Category {
    std::string name;
    std::vector<std::string> members;
};

std::vector<Category> load_categories(const std::string& path) {
    std::vector<Category> out;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        const auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        Category c;
        c.name = line.substr(0, tab);
        // Multi-word category names are not words in the corpus, so they cannot
        // be targets. Dropping them is a property of the data, not a filter
        // chosen to flatter the result.
        if (c.name.find('_') != std::string::npos) continue;
        std::istringstream ws(line.substr(tab + 1));
        std::string w;
        while (ws >> w) c.members.push_back(w);
        out.push_back(std::move(c));
    }
    return out;
}

// A PERCENTAGE WITHOUT A COUNT IS NOT A MEASUREMENT.
//
// This bench printed a comparison table to three decimal places over 1,133
// held-out pairs, where "3.18% versus 3.44%" is 36 correct answers against 39
// (two-proportion z = 0.35, p = 0.72) and an earlier "win" of 0.353 against
// 0.177 was 4 answers against 2 (p = 0.41). Nothing it reported was
// distinguishable from anything else it reported, and three separate wrong
// conclusions survived because the interval was never printed beside the point
// estimate.
struct Rate {
    std::size_t hits = 0, n = 0;
    double pct() const { return n ? 100.0 * static_cast<double>(hits) / n : 0.0; }
    // Wilson score interval at 95%, which behaves at the tiny proportions and
    // small counts that a normal approximation gets badly wrong.
    std::pair<double, double> wilson() const {
        if (n == 0) return {0.0, 0.0};
        const double z = 1.96, nn = static_cast<double>(n);
        const double p = static_cast<double>(hits) / nn;
        const double d = 1.0 + z * z / nn;
        const double c = p + z * z / (2 * nn);
        const double h = z * std::sqrt(p * (1 - p) / nn + z * z / (4 * nn * nn));
        return {100.0 * (c - h) / d, 100.0 * (c + h) / d};
    }
};

// Do two rates differ at all? Two-proportion z-test, so the table can say
// "indistinguishable" instead of implying a ranking it cannot support.
inline bool separable(const Rate& a, const Rate& b) {
    if (a.n == 0 || b.n == 0) return false;
    const double p1 = static_cast<double>(a.hits) / a.n;
    const double p2 = static_cast<double>(b.hits) / b.n;
    const double p = static_cast<double>(a.hits + b.hits) / (a.n + b.n);
    const double se = std::sqrt(p * (1 - p) * (1.0 / a.n + 1.0 / b.n));
    if (se <= 0.0) return false;
    return std::abs(p1 - p2) / se > 1.96;
}

} // namespace

int main(int argc, char** argv) {
    const std::string plexus_prefix = (argc > 1) ? argv[1] : "data/plexus_archive/main";
    const std::string wn_path       = (argc > 2) ? argv[2] : "data/eval/wn_categories.tsv";
    const std::size_t max_cats      = (argc > 3) ? std::stoul(argv[3]) : 60;
    const std::size_t generations   = (argc > 4) ? std::stoul(argv[4]) : 300;
    // DOES THE HYPERVECTOR SUBSTRATE EARN ITS PLACE?
    //
    // With Glyph::from_hash every word is a random orthogonal vector, so
    // bind/bundle/permute produce things that are not items and only the GRAPH
    // opcodes do real work. If that is so, "the organism computes in the
    // representation" is decoration and should be dropped.
    //
    // Lexicon's random-indexing context glyphs are the alternative: similar
    // words get similar vectors, so cleanup becomes semantic and the VSA
    // operations have something to be gradient over. Running both arms is the
    // only way to find out which is true, and it strengthens the BASELINES too
    // -- under distributional glyphs the nearest other word to a word is
    // plausibly a co-hyponym, so identity and top-associate get harder to beat.
    const int distributional = (argc > 5) ? std::stoi(argv[5]) : 0;
    const std::string lex_prefix = (argc > 6) ? argv[6] : "data/lexicon_archive/main";

    khora::plexus::Plexus px;
    px.load(plexus_prefix);
    std::printf("Can an evolved operator beat the one a human would write?\n\n");
    std::printf("  environment: Plexus, %zu words, %llu edges, %llu tokens observed\n",
                px.vocabulary_size(),
                static_cast<unsigned long long>(px.edge_count()),
                static_cast<unsigned long long>(px.total_tokens()));
    if (px.vocabulary_size() < 1000) {
        std::printf("  no usable Plexus at %s\n", plexus_prefix.c_str());
        return 1;
    }

    auto cats = load_categories(wn_path);
    std::printf("  ground truth: WordNet, %zu single-word categories\n", cats.size());

    // Keep categories whose NAME and at least 12 of whose members are words the
    // environment actually knows. A relation cannot be discovered between words
    // the world model has never seen.
    std::vector<Category> usable;
    for (auto& c : cats) {
        if (!px.has(c.name)) continue;
        std::vector<std::string> present;
        for (const auto& m : c.members) {
            if (m != c.name && px.has(m)) present.push_back(m);
        }
        if (present.size() < 12) continue;
        c.members = std::move(present);
        usable.push_back(std::move(c));
        if (usable.size() >= max_cats) break;
    }
    std::printf("  usable: %zu categories present in the environment with >=12 members\n\n",
                usable.size());
    if (usable.size() < 4) { std::printf("  too little overlap to measure\n"); return 1; }

    // ---- codebook: every word that can be an input or an answer -------------
    Codebook cb;
    std::unordered_map<std::string, std::size_t> slot;
    khora::lexicon::Lexicon lex;
    if (distributional) {
        lex.load(lex_prefix);
        std::printf("  glyphs: DISTRIBUTIONAL (Lexicon random indexing, %zu tokens)\n",
                    lex.vocabulary_size());
    } else {
        std::printf("  glyphs: HASHED (every word orthogonal to every other)\n");
    }
    std::size_t no_context = 0;
    auto intern = [&](const std::string& w) -> std::size_t {
        const auto it = slot.find(w);
        if (it != slot.end()) return it->second;
        const std::size_t i = cb.size();
        Glyph g = Glyph::from_hash(w);
        if (distributional) {
            const Glyph c = lex.context_glyph(w);
            // A word the Lexicon never saw has a zero context glyph, and every
            // one of those would collide onto the same point. Fall back to the
            // hash so an unknown word stays distinguishable rather than
            // silently merging with every other unknown.
            if (c.popcount() > 0) g = c; else ++no_context;
        }
        cb.add(w, g);
        slot.emplace(w, i);
        return i;
    };
    for (const auto& c : usable) {
        intern(c.name);
        for (const auto& m : c.members) intern(m);
    }

    // Which category each codebook slot belongs to, or -1 for a category name
    // itself. Co-hyponymy is a SET-valued relation -- any sibling is a correct
    // answer -- but an Assay carries one target index, so the scored task is
    // strictly harder than the relation is. This map lets the bench also report
    // the honest measure: did the answer land in the right category at all.
    std::vector<int> cat_of(0);

    // ---- the environment graph: Plexus adjacency, restricted to the codebook -
    //
    // An organism can only reach words that are in the codebook, because the
    // only way a hypervector addresses anything is by cleaning up to an item.
    // Search DEEP in Plexus and keep what lands inside the codebook. The first
    // version asked for only the top 16 associates and kept the survivors,
    // which left 1.4 edges per word and 47% of words with none at all -- the
    // organism had nothing to sense, so the senses were never actually under
    // test. Most of a word's strongest associates are simply not WordNet
    // category members, and cutting the list before intersecting it throws the
    // structure away rather than measuring it.
    std::size_t edges = 0;
    for (std::size_t i = 0; i < cb.size(); ++i) {
        const auto assoc = px.associates(std::string(cb.name_at(i)), 400);
        std::size_t kept = 0;
        for (const auto& [word, weight] : assoc) {
            if (kept >= 32) break;
            const auto it = slot.find(word);
            if (it == slot.end()) continue;
            cb.link(i, it->second);
            ++kept; ++edges;
        }
    }
    if (distributional) {
        std::printf("  %zu of %zu codebook words had no context glyph, fell back to hash\n",
                    no_context, cb.size());
    }
    cat_of.assign(cb.size(), -1);
    for (std::size_t ci = 0; ci < usable.size(); ++ci) {
        for (const auto& m : usable[ci].members) cat_of[slot[m]] = static_cast<int>(ci);
    }

    for (std::size_t i = 0; i < cb.size(); ++i) cb.set_class(i, cat_of[i]);
    cb.precompute_kin();
    std::size_t with_edges = 0;
    for (std::size_t i = 0; i < cb.size(); ++i) if (!cb.links(i).empty()) ++with_edges;
    std::printf("  codebook %zu words, %zu environment edges (Plexus top-32, in-codebook)\n",
                cb.size(), edges);
    std::printf("  %zu of %zu words have at least one edge (%.1f%%) -- when this is low the\n",
                with_edges, cb.size(), 100.0 * with_edges / static_cast<double>(cb.size()));
    std::printf("  organism has nothing to sense and the senses are not under test at all.\n");
    std::printf("  chance = %.3f%%\n\n", 100.0 / static_cast<double>(cb.size()));

    // ---- build the two relations -------------------------------------------
    // Split by MEMBER: the held-out pairs are words never selected against, so
    // an operator that memorised its training pairs scores nothing here.
    struct Split { std::vector<Assay> train, held; };
    Split hyper, cohyp;
    std::vector<std::size_t> cohyp_held_from;   // source slot of each held-out pair
    for (const auto& c : usable) {
        const std::size_t cat_slot = slot[c.name];
        for (std::size_t k = 0; k < c.members.size(); ++k) {
            const std::size_t m = slot[c.members[k]];
            const bool is_held = (k % 5 == 0);

            Assay h;
            h.from = cb.at(m);
            h.to = cb.at(cat_slot);
            h.to_index = cat_slot;
            h.from_index = m;
            (is_held ? hyper.held : hyper.train).push_back(h);

            // Co-hyponymy: the target is another member of the same category.
            // Any of them would be correct, but Assay carries one index, so the
            // scored target is a fixed sibling -- which makes this HARDER than
            // the relation really is, never easier.
            const std::size_t sib = slot[c.members[(k + 1) % c.members.size()]];
            if (sib == m) continue;
            Assay o;
            o.from = cb.at(m);
            o.to = cb.at(sib);
            o.to_index = sib;
            o.from_index = m;
            // SET-VALUED, at last. The chamber now optimises the same thing the
            // bench reports. Selecting on one designated sibling while reporting
            // same-category accuracy meant the two objectives were
            // anti-correlated: the instruction the chamber preferred scored
            // 1.62% on the reported metric, the one it rejected scored 3.36%.
            o.to_class = cat_of[m];
            (is_held ? cohyp.held : cohyp.train).push_back(o);
            if (is_held) cohyp_held_from.push_back(m);
        }
    }
    std::printf("  hypernym   : %zu train / %zu held out\n", hyper.train.size(), hyper.held.size());
    std::printf("  co-hyponym : %zu train / %zu held out\n\n", cohyp.train.size(), cohyp.held.size());

    // ---- baselines ----------------------------------------------------------
    auto score_direct = [&](const std::vector<Assay>& pairs,
                            const std::function<Glyph(const Glyph&)>& f) {
        Rate r;
        r.n = pairs.size();
        for (const Assay& a : pairs) {
            if (cb.nearest_index(f(a.from)) == a.to_index) ++r.hits;
        }
        return r;
    };

    // The textbook VSA answer, built from the TRAINING pairs only.
    auto vsa_role = [&](const std::vector<Assay>& train) {
        std::vector<Glyph> xs;
        xs.reserve(train.size());
        for (const Assay& a : train) xs.push_back(khora::lattice::bind(a.from, a.to));
        if (xs.empty()) return Glyph::zero();
        return khora::lattice::bundle(std::span<const Glyph>(xs));
    };

    // Same-category accuracy: did the answer land in the right category, rather
    // than on one specific sibling. Only meaningful for co-hyponymy.
    auto score_category = [&](const std::vector<Assay>& pairs,
                              const std::function<Glyph(const Glyph&)>& f,
                              const std::vector<std::size_t>& from_slots) {
        Rate r;
        r.n = pairs.size();
        for (std::size_t k = 0; k < pairs.size(); ++k) {
            const std::size_t out = cb.nearest_index(f(pairs[k].from));
            const int want = cat_of[from_slots[k]];
            if (want >= 0 && out < cat_of.size() && cat_of[out] == want &&
                out != from_slots[k]) {
                ++r.hits;
            }
        }
        return r;
    };

    // BALANCED over source categories, and it is not the same number.
    //
    // Plain same-category accuracy has its own majority-class optimum: an
    // exhaustive scan of every one-instruction program found that the top
    // scorers on it are CONSTANTS, reaching 3.97% and beating both the evolved
    // champion (3.18%) and the top-associate baseline (3.44%). The bench printed
    // a majority-class row next to the exact metric, where it reads 0.000% and
    // looks harmless, and printed nothing beside the metric that actually had
    // the problem. Averaging over source categories puts a constant back at 1/k.
    auto score_category_balanced = [&](const std::vector<Assay>& pairs,
                                       const std::function<Glyph(const Glyph&)>& f,
                                       const std::vector<std::size_t>& from_slots) {
        std::unordered_map<int, std::pair<std::size_t, std::size_t>> per;
        for (std::size_t k = 0; k < pairs.size(); ++k) {
            const int want = cat_of[from_slots[k]];
            if (want < 0) continue;
            const std::size_t out = cb.nearest_index(f(pairs[k].from));
            auto& c = per[want];
            ++c.second;
            if (out < cat_of.size() && cat_of[out] == want && out != from_slots[k]) ++c.first;
        }
        if (per.empty()) return 0.0;
        double acc = 0.0;
        for (const auto& kv : per) {
            acc += static_cast<double>(kv.second.first) / static_cast<double>(kv.second.second);
        }
        return 100.0 * acc / static_cast<double>(per.size());
    };

    auto run_relation = [&](const char* label, Split& sp,
                            const std::vector<std::size_t>& held_from) {
        std::printf("  %s\n", label);
        std::printf("    %-20s | %7s | %10s | %11s | %s\n",
                    "predictor", "held-out", "hits/n", "95%% Wilson", "what it is");
        std::printf("    ---------------------+---------+------------+-------------+-----------\n");
        std::printf("    chance               |       %6.3f%%     | 1 / %zu\n",
                    100.0 / static_cast<double>(cb.size()), cb.size());

        auto row = [&](const char* name, const Rate& r, const char* note) {
            const auto ci = r.wilson();
            std::printf("    %-20s | %6.3f%% | %4zu/%-5zu | %5.2f-%5.2f | %s\n",
                        name, r.pct(), r.hits, r.n, ci.first, ci.second, note);
        };

        const Rate id = score_direct(sp.held, [](const Glyph& g) { return g; });
        row("identity", id, "output = input");

        // MAJORITY CLASS -- the baseline missing from the first run of this
        // bench, and its absence produced a number that looked like a discovery.
        // An operator that ignores its input entirely and always answers the
        // commonest target scores exactly this, and that is precisely what
        // selection found: 7.42% on hypernymy against 0.00% for every other
        // baseline, from a program whose output register was never written from
        // its input. A benchmark without the RELEVANT dumb baseline beside it is
        // worth nothing, and chance was not the relevant one.
        std::unordered_map<std::size_t, std::size_t> freq;
        for (const Assay& a : sp.train) ++freq[a.to_index];
        std::size_t majority = 0, mc = 0;
        for (const auto& kv : freq) if (kv.second > mc) { mc = kv.second; majority = kv.first; }
        Rate maj;
        maj.n = sp.held.size();
        for (const Assay& a : sp.held) if (a.to_index == majority) ++maj.hits;
        row("majority class", maj, std::string("always \"" +
            std::string(cb.name_at(majority)) + "\"").c_str());

        const Rate top = score_direct(sp.held, [&](const Glyph& g) {
            const std::size_t i = cb.nearest_index(g);
            const auto& l = cb.links(i);
            return l.empty() ? g : cb.at(l[0]);
        });
        row("top Plexus associate", top, "no search at all");

        // KIN AS A BASELINE, not only as an opcode. Second-order neighbourhood
        // similarity is a strong primitive and the organism is now handed it.
        // If the evolved program merely rediscovers it, this row scores the same
        // and the honest report is "evolution found a primitive it was given",
        // not "the composition works". A powerful primitive must be its own
        // control, or adding it is just writing the answer into the machine.
        const Rate kinb = score_direct(sp.held, [&](const Glyph& g) {
            const std::size_t i = cb.nearest_index(g);
            const std::size_t k = cb.kin(i);
            return (k == static_cast<std::size_t>(-1)) ? g : cb.at(k);
        });
        row("second-order kin", kinb, "the Kin opcode, ALONE");

        const Glyph role = vsa_role(sp.train);
        const Rate vsa = score_direct(sp.held, [&](const Glyph& g) {
            return khora::lattice::bind(g, role);
        });
        row("VSA role vector", vsa, "THE textbook answer");

        ChamberConfig cfg;
        cfg.population = 300;
        cfg.genome_codons = 5;
        cfg.mutation_rate = 0.03;
        Chamber ch(cfg, &cb, 20260824);
        const Vm scorer(&cb);
        const Genome seed_genome = Genome::random(5, 7);
        const Rate rand0 = score_direct(sp.held, [&](const Glyph& g) {
            return scorer.run(seed_genome, g);
        });
        for (std::size_t g = 0; g < generations; ++g) (void)ch.step(sp.train);
        // Reported as PLAIN accuracy so it sits on the same scale as every
        // baseline above. Fitness is balanced accuracy internally, which is a
        // different number and is printed separately -- quoting the training
        // measure inside the comparison table would be scoring the champion on
        // a yardstick none of the baselines ever faced.
        const Rate evolved = score_direct(sp.held, [&](const Glyph& g) {
            return scorer.run(ch.best().genome, g);
        });
        const double balanced = 100.0 * ch.evaluate(ch.best().genome, sp.held);

        row("random genome", rand0, "the floor for this method");
        char note[64];
        std::snprintf(note, sizeof note, "%zu births, %zu gens", ch.births(), ch.generations());
        row("Ribosome (evolved)", evolved, note);
        std::printf("    (selected on balanced accuracy: %.3f%%)\n", balanced);

        // SET-VALUED SCORE. The table above demands one specific sibling; this
        // asks the question the relation actually poses -- did the answer land
        // in the right category at all. Only meaningful where the target is a
        // set, so it is reported for co-hyponymy alone.
        if (!held_from.empty()) {
            auto f_top = [&](const Glyph& g) {
                const std::size_t i = cb.nearest_index(g);
                const auto& l = cb.links(i);
                return l.empty() ? g : cb.at(l[0]);
            };
            auto f_kin = [&](const Glyph& g) {
                const std::size_t i = cb.nearest_index(g);
                const std::size_t k = cb.kin(i);
                return (k == static_cast<std::size_t>(-1)) ? g : cb.at(k);
            };
            auto f_evo = [&](const Glyph& g) { return scorer.run(ch.best().genome, g); };
            // THE CONSTANT, which is what an exhaustive scan says actually wins
            // this metric. Without it printed here the same trap that caught the
            // hypernym run is still open on the co-hyponym one.
            const Glyph konst = cb.at(0);
            auto f_const = [&](const Glyph&) { return konst; };

            const Rate c_top = score_category(sp.held, f_top, held_from);
            const Rate c_kin = score_category(sp.held, f_kin, held_from);
            const Rate c_evo = score_category(sp.held, f_evo, held_from);
            const Rate c_con = score_category(sp.held, f_const, held_from);

            std::printf("\n    SAME-CATEGORY -- any sibling counts, which is how the relation\n");
            std::printf("    is actually posed. Balanced averages over SOURCE categories, so a\n");
            std::printf("    constant scores 1/k instead of the size of the largest class.\n");
            std::printf("    %-20s | %6s%% | %4s/%-5s | %11s | balanced\n",
                        "predictor", "plain", "hits", "n", "95%% Wilson");
            auto crow = [&](const char* name, const Rate& r,
                            const std::function<Glyph(const Glyph&)>& f) {
                const auto ci = r.wilson();
                std::printf("    %-20s | %6.3f%% | %4zu/%-5zu | %5.2f-%5.2f | %6.3f%%\n",
                            name, r.pct(), r.hits, r.n, ci.first, ci.second,
                            score_category_balanced(sp.held, f, held_from));
            };
            crow("constant", c_con, f_const);
            crow("top Plexus associate", c_top, f_top);
            crow("second-order kin", c_kin, f_kin);
            crow("Ribosome (evolved)", c_evo, f_evo);

            // AND WHETHER ANY OF IT IS DISTINGUISHABLE. Three separate wrong
            // conclusions survived in this file because a point estimate was
            // reported without asking whether the sample could support it.
            std::printf("    Ribosome vs top associate: %s (%zu hits vs %zu of %zu)\n",
                        separable(c_evo, c_top) ? "SEPARABLE at 95%%"
                                                : "INDISTINGUISHABLE -- the sample cannot rank them",
                        c_evo.hits, c_top.hits, c_evo.n);
        }

        // DOES THE EVOLVED OPERATOR ACTUALLY READ ITS INPUT?
        //
        // A constant function is a genuine optimum of this fitness and selection
        // will find it. Counting DISTINCT answers across the held-out inputs is
        // the cheapest way to tell a discovered relation from a memorised one,
        // and it belongs printed next to the accuracy rather than left for a
        // reader to infer from a disassembly.
        const Vm probe(&cb);
        std::unordered_set<std::size_t> distinct_outputs;
        for (const Assay& a : sp.held) {
            distinct_outputs.insert(cb.nearest_index(probe.run(ch.best().genome, a.from)));
        }
        std::printf("    -> champion gives %zu distinct answers over %zu held-out inputs",
                    distinct_outputs.size(), sp.held.size());
        if (distinct_outputs.size() <= 1) {
            std::printf(":\n       it IGNORES ITS INPUT -- the majority-class classifier in an\n");
            std::printf("       eight-instruction costume, not an operator.\n");
        } else {
            std::printf(",\n       so it is a function OF the input rather than a constant.\n");
        }

        const Rate* best_b = &id;
        const std::vector<const Rate*> others{&top, &vsa, &maj, &kinb};
        for (const Rate* r : others) if (r->pct() > best_b->pct()) best_b = r;
        if (evolved.pct() > best_b->pct()) {
            std::printf("    -> evolution leads the best baseline by %.2f points, and that is\n",
                        evolved.pct() - best_b->pct());
            std::printf("       %s.\n", separable(evolved, *best_b)
                        ? "SEPARABLE at 95%%" : "INSIDE THE NOISE -- not a win");
        } else {
            std::printf("    -> evolution trails the best baseline by %.2f points (%s).\n",
                        best_b->pct() - evolved.pct(),
                        separable(evolved, *best_b) ? "separable" : "inside the noise");
        }
        std::printf("    what it found -- %zu instructions, %zu live:\n",
                    ch.best().genome.codons(), ch.best().genome.effective_length());
        std::string dis = ch.best().genome.disassemble();
        std::istringstream ds(dis);
        std::string ln;
        while (std::getline(ds, ln)) std::printf("      %s\n", ln.c_str());
        std::printf("\n");
    };

    run_relation("HYPERNYM  (member -> its category)  -- vertical, and there is no", hyper, {});
    run_relation("CO-HYPONYM (member -> a sibling)    -- horizontal, what a co-occurrence"
                 "\n             graph is actually supposed to carry", cohyp, cohyp_held_from);

    std::printf("  HOW TO READ IT\n");
    std::printf("    The VSA role vector is the comparison that matters. It is what a\n");
    std::printf("    practitioner would write, it uses the same training pairs, and it\n");
    std::printf("    costs nothing to compute. Beating chance is not a result. Beating\n");
    std::printf("    the top Plexus associate is barely one. Beating the role vector on\n");
    std::printf("    HELD-OUT words is the only outcome that makes this organ worth\n");
    std::printf("    keeping, and a loss on one relation and a win on the other is a\n");
    std::printf("    statement about the environment, not a hedge.\n");
    return 0;
}
