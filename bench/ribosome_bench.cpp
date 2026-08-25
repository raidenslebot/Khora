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

#include <algorithm>
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

struct Result {
    const char* name;
    double accuracy;
    const char* note;
};

} // namespace

int main(int argc, char** argv) {
    const std::string plexus_prefix = (argc > 1) ? argv[1] : "data/plexus_archive/main";
    const std::string wn_path       = (argc > 2) ? argv[2] : "data/eval/wn_categories.tsv";
    const std::size_t max_cats      = (argc > 3) ? std::stoul(argv[3]) : 60;
    const std::size_t generations   = (argc > 4) ? std::stoul(argv[4]) : 300;

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
    auto intern = [&](const std::string& w) -> std::size_t {
        const auto it = slot.find(w);
        if (it != slot.end()) return it->second;
        const std::size_t i = cb.size();
        cb.add(w, Glyph::from_hash(w));
        slot.emplace(w, i);
        return i;
    };
    for (const auto& c : usable) {
        intern(c.name);
        for (const auto& m : c.members) intern(m);
    }

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
    for (const auto& c : usable) {
        const std::size_t cat_slot = slot[c.name];
        for (std::size_t k = 0; k < c.members.size(); ++k) {
            const std::size_t m = slot[c.members[k]];
            const bool is_held = (k % 5 == 0);

            Assay h;
            h.from = cb.at(m);
            h.to = cb.at(cat_slot);
            h.to_index = cat_slot;
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
            (is_held ? cohyp.held : cohyp.train).push_back(o);
        }
    }
    std::printf("  hypernym   : %zu train / %zu held out\n", hyper.train.size(), hyper.held.size());
    std::printf("  co-hyponym : %zu train / %zu held out\n\n", cohyp.train.size(), cohyp.held.size());

    // ---- baselines ----------------------------------------------------------
    auto score_direct = [&](const std::vector<Assay>& pairs,
                            const std::function<Glyph(const Glyph&)>& f) {
        if (pairs.empty()) return 0.0;
        std::size_t right = 0;
        for (const Assay& a : pairs) {
            if (cb.nearest_index(f(a.from)) == a.to_index) ++right;
        }
        return 100.0 * static_cast<double>(right) / static_cast<double>(pairs.size());
    };

    // The textbook VSA answer, built from the TRAINING pairs only.
    auto vsa_role = [&](const std::vector<Assay>& train) {
        std::vector<Glyph> xs;
        xs.reserve(train.size());
        for (const Assay& a : train) xs.push_back(khora::lattice::bind(a.from, a.to));
        if (xs.empty()) return Glyph::zero();
        return khora::lattice::bundle(std::span<const Glyph>(xs));
    };

    auto run_relation = [&](const char* label, Split& sp) {
        std::printf("  %s\n", label);
        std::printf("    predictor            | held-out accuracy | what it is\n");
        std::printf("    ---------------------+-------------------+------------\n");
        std::printf("    chance               |       %6.3f%%     | 1 / %zu\n",
                    100.0 / static_cast<double>(cb.size()), cb.size());

        const double id = score_direct(sp.held, [](const Glyph& g) { return g; });
        std::printf("    identity             |       %6.3f%%     | output = input\n", id);

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
        std::size_t maj_right = 0;
        for (const Assay& a : sp.held) if (a.to_index == majority) ++maj_right;
        const double maj = sp.held.empty() ? 0.0
            : 100.0 * static_cast<double>(maj_right) / static_cast<double>(sp.held.size());
        std::printf("    majority class       |       %6.3f%%     | always answer \"%s\"\n",
                    maj, std::string(cb.name_at(majority)).c_str());

        const double top = score_direct(sp.held, [&](const Glyph& g) {
            const std::size_t i = cb.nearest_index(g);
            const auto& l = cb.links(i);
            return l.empty() ? g : cb.at(l[0]);
        });
        std::printf("    top Plexus associate |       %6.3f%%     | no search at all\n", top);

        const Glyph role = vsa_role(sp.train);
        const double vsa = score_direct(sp.held, [&](const Glyph& g) {
            return khora::lattice::bind(g, role);
        });
        std::printf("    VSA role vector      |       %6.3f%%     | THE textbook answer\n", vsa);

        ChamberConfig cfg;
        cfg.population = 300;
        cfg.genome_codons = 5;
        cfg.mutation_rate = 0.03;
        Chamber ch(cfg, &cb, 20260824);
        const Vm scorer(&cb);
        const Genome seed_genome = Genome::random(5, 7);
        const double rand0 = score_direct(sp.held, [&](const Glyph& g) {
            return scorer.run(seed_genome, g);
        });
        for (std::size_t g = 0; g < generations; ++g) (void)ch.step(sp.train);
        // Reported as PLAIN accuracy so it sits on the same scale as every
        // baseline above. Fitness is balanced accuracy internally, which is a
        // different number and is printed separately -- quoting the training
        // measure inside the comparison table would be scoring the champion on
        // a yardstick none of the baselines ever faced.
        const double evolved = score_direct(sp.held, [&](const Glyph& g) {
            return scorer.run(ch.best().genome, g);
        });
        const double balanced = 100.0 * ch.evaluate(ch.best().genome, sp.held);

        std::printf("    random genome        |       %6.3f%%     | the floor for this method\n", rand0);
        std::printf("    Ribosome (evolved)   |       %6.3f%%     | %zu births, %zu generations\n",
                    evolved, ch.births(), ch.generations());
        std::printf("    (balanced accuracy, the measure it was selected on: %.3f%%)\n", balanced);

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

        const double best_baseline = std::max({id, top, vsa, maj});
        if (evolved > best_baseline) {
            std::printf("    -> evolution BEATS every baseline, by %.2f points over the best.\n",
                        evolved - best_baseline);
        } else {
            std::printf("    -> evolution LOSES to the best baseline by %.2f points.\n",
                        best_baseline - evolved);
        }
        std::printf("    what it found:\n");
        std::string dis = ch.best().genome.disassemble();
        std::istringstream ds(dis);
        std::string ln;
        while (std::getline(ds, ln)) std::printf("      %s\n", ln.c_str());
        std::printf("\n");
    };

    run_relation("HYPERNYM  (member -> its category)  -- vertical, and there is no", hyper);
    run_relation("CO-HYPONYM (member -> a sibling)    -- horizontal, what a co-occurrence"
                 "\n             graph is actually supposed to carry", cohyp);

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
