// CAN KHORA DISCOVER STRUCTURE WITH NO ANSWER KEY AT ALL?
//
// The Ribosome bench measures evolved operators against WordNet. That is a real
// external ground truth and it was the right first test, but it means the
// objective is supplied from outside: strip the framing and it is supervised
// program search with a human-curated target. Genetic programming has been doing
// that since 1992.
//
// This is the version that is not that. There is no WordNet here, no labels, no
// curated relation, and nothing a human decided is true. The only judge is the
// corpus itself.
//
// THE TASK: LINK PREDICTION AGAINST THE FUTURE.
//
// Khora reads 80% of its own reservoir and builds Plexus from it. An organism is
// a program that, given a word, proposes another word it believes BELONGS with
// it. The proposal is scored on one question:
//
//     does that pair actually co-occur in the 20% of the corpus that the graph
//     was never built from -- AND is it a pair the graph does not already have?
//
// Both halves matter. Without the first it is not prediction; without the second
// the winning strategy is "propose an edge you can already see", which measures
// nothing but the ability to read memory.
//
// So an organism is fit exactly when it proposes structure that turns out to be
// TRUE OF THE WORLD and that the system did not already know. Selection is by
// contact with reality rather than by agreement with a label. That is the shape
// every one of Khora's own directives has been pointing at: model the
// environment, construct something, deploy it, keep what survives.
//
// It is also, unglamorously, link prediction -- a well-studied problem with
// strong, cheap heuristics. Those heuristics are the baselines, and they are the
// point of the exercise:
//
//   random pair          the floor.
//   top existing edge    proposes what it already knows. Scores ZERO by
//                        construction, since known edges are excluded, and it
//                        is here to prove the exclusion is real.
//   friend-of-a-friend   two hops out. The standard first answer.
//   common neighbours    the pair sharing the most neighbours -- the classic
//                        strong link-prediction heuristic, and the one to beat.
//   Ribosome             an evolved composition over the same primitives.
//
// A win here would mean something a WordNet win does not: that the system found
// a piece of its world worth knowing, judged by its world, with nobody holding
// the answer key.

#include "khora/ribosome/ribosome.hpp"
#include "khora/plexus/plexus.hpp"
#include "khora/reservoir/reservoir.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace khora::ribosome;
using khora::lattice::Glyph;

namespace {

std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (const char ch : text) {
        if (std::isalpha(static_cast<unsigned char>(ch))) {
            cur += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        } else if (!cur.empty()) {
            if (cur.size() >= 3) out.push_back(cur);
            cur.clear();
        }
    }
    return out;
}

inline std::uint64_t pair_key(std::uint32_t a, std::uint32_t b) {
    if (a > b) std::swap(a, b);
    return (static_cast<std::uint64_t>(a) << 32) | b;
}

} // namespace

int main(int argc, char** argv) {
    const std::string dir     = (argc > 1) ? argv[1] : "data/reservoir";
    const std::size_t cap     = (argc > 2) ? std::stoul(argv[2]) : 1500000;
    const std::size_t vocab_n = (argc > 3) ? std::stoul(argv[3]) : 3000;
    const std::size_t gens    = (argc > 4) ? std::stoul(argv[4]) : 60;

    std::printf("Can Khora discover structure with no answer key at all?\n\n");

    // ---- the corpus, split in time -----------------------------------------
    khora::reservoir::Reservoir res(dir);
    res.load_catalog();
    const auto cat = res.catalog();
    if (cat.empty()) { std::printf("  no tomes at %s\n", dir.c_str()); return 1; }

    std::vector<std::string> stream;
    for (const auto& t : cat) {
        if (stream.size() >= cap) break;
        auto text = res.read(t.title);
        if (!text || text->size() < 20000) continue;
        for (auto& w : tokenize(*text)) {
            if (stream.size() >= cap) break;
            stream.push_back(std::move(w));
        }
    }
    if (stream.size() < 100000) { std::printf("  too little text\n"); return 1; }

    const std::size_t split = stream.size() * 4 / 5;
    std::printf("  %zu tokens: %zu to build the world model, %zu held back as THE FUTURE\n",
                stream.size(), split, stream.size() - split);

    // ---- the world model, built only on the past ---------------------------
    khora::plexus::Plexus px;
    {
        std::vector<std::string> past(stream.begin(), stream.begin() + split);
        px.observe(past, 4);
    }
    std::printf("  Plexus: %zu words, %llu edges\n", px.vocabulary_size(),
                static_cast<unsigned long long>(px.edge_count()));

    // ---- the codebook: the most frequent words the model knows -------------
    std::unordered_map<std::string, std::size_t> count;
    for (std::size_t i = 0; i < split; ++i) ++count[stream[i]];
    std::vector<std::pair<std::string, std::size_t>> by_freq(count.begin(), count.end());
    std::sort(by_freq.begin(), by_freq.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    // Skip the very top: the commonest words in English co-occur with
    // everything, so including them would let an organism score by proposing
    // "the" and nothing about structure would be under test.
    const std::size_t skip = 100;

    Codebook cb;
    std::unordered_map<std::string, std::size_t> slot;
    for (std::size_t i = skip; i < by_freq.size() && cb.size() < vocab_n; ++i) {
        if (!px.has(by_freq[i].first)) continue;
        slot.emplace(by_freq[i].first, cb.size());
        cb.add(by_freq[i].first, Glyph::from_hash(by_freq[i].first));
    }
    std::printf("  codebook: %zu words (ranks %zu..%zu by frequency)\n",
                cb.size(), skip, skip + cb.size());
    if (cb.size() < 200) { std::printf("  codebook too small\n"); return 1; }

    // ---- environment graph, and the set of edges ALREADY KNOWN -------------
    // MASKED EDGES: the self-supervised training signal.
    //
    // The organism must be SELECTED on the same skill it is TESTED on. The first
    // version of this selected it to reproduce edges the environment already
    // contains, then scored it only on edges the environment lacks — so
    // selection pushed straight toward the behaviour that scores zero.
    //
    // Instead, some of each word's real associates are withheld from the
    // environment graph and become the training target. The organism is selected
    // on "propose a link that is true but that you cannot see", which is exactly
    // the skill the future exam demands. This is the standard masked-edge
    // protocol for link prediction, and every baseline faces the identical
    // mutilated graph.
    std::unordered_set<std::uint64_t> known;   // everything the SYSTEM has; exam-excluded
    std::vector<Assay> train;
    std::size_t edges = 0, masked = 0;
    for (std::size_t i = 0; i < cb.size(); ++i) {
        const auto assoc = px.associates(std::string(cb.name_at(i)), 400);
        std::size_t rank = 0;
        for (const auto& [word, weight] : assoc) {
            const auto it = slot.find(word);
            if (it == slot.end()) continue;
            known.insert(pair_key(static_cast<std::uint32_t>(i),
                                  static_cast<std::uint32_t>(it->second)));
            if (rank < 24) {
                cb.link(i, it->second);       // visible to the organism
                ++edges;
            } else if (rank < 32) {
                Assay a;                      // withheld: the training target
                a.from = cb.at(i);
                a.to = cb.at(it->second);
                a.to_index = it->second;
                train.push_back(a);
                ++masked;
            }
            ++rank;
        }
    }
    cb.precompute_kin();
    std::printf("  environment: %zu visible edges, %zu MASKED as the training signal\n",
                edges, masked);
    std::printf("  %zu pairs the system already has, all excluded from the exam\n",
                known.size());

    // ---- THE FUTURE: which pairs actually co-occur in unseen text ----------
    std::unordered_set<std::uint64_t> future;
    {
        const std::size_t window = 4;
        std::vector<std::size_t> recent;
        for (std::size_t i = split; i < stream.size(); ++i) {
            const auto it = slot.find(stream[i]);
            if (it == slot.end()) continue;
            for (const std::size_t j : recent) {
                if (j != it->second) {
                    future.insert(pair_key(static_cast<std::uint32_t>(j),
                                           static_cast<std::uint32_t>(it->second)));
                }
            }
            recent.push_back(it->second);
            if (recent.size() > window) recent.erase(recent.begin());
        }
    }
    // The pairs that matter: true in the future, and NOT already known.
    std::size_t novel_true = 0;
    for (const std::uint64_t k : future) if (!known.count(k)) ++novel_true;
    std::printf("  the future holds %zu co-occurring pairs, %zu of them NEW to the model\n",
                future.size(), novel_true);
    const double density = static_cast<double>(novel_true) /
                           (static_cast<double>(cb.size()) * (cb.size() - 1) / 2.0);
    std::printf("  a random guess is right %.4f%% of the time\n\n", 100.0 * density);

    // ---- scoring ------------------------------------------------------------
    //
    // A proposal counts only if it is BOTH true in the future AND absent from
    // what the model already had. Proposing a known edge scores nothing, however
    // true it is.
    auto score = [&](const std::function<std::size_t(std::size_t)>& propose) {
        std::size_t asked = 0, novel = 0, hit = 0;
        for (std::size_t i = 0; i < cb.size(); ++i) {
            const std::size_t j = propose(i);
            if (j == static_cast<std::size_t>(-1) || j == i || j >= cb.size()) continue;
            ++asked;
            const std::uint64_t k = pair_key(static_cast<std::uint32_t>(i),
                                             static_cast<std::uint32_t>(j));
            if (known.count(k)) continue;
            ++novel;
            if (future.count(k)) ++hit;
        }
        // Reported over EVERY word, not over the ones it chose to answer: a
        // predictor that abstains on the hard cases has not solved them.
        return std::make_pair(100.0 * static_cast<double>(hit) / static_cast<double>(cb.size()),
                              novel);
    };

    std::printf("  predictor           | true & NEW | proposals that were new | what it is\n");
    std::printf("  --------------------+------------+-------------------------+-----------\n");

    std::uint64_t rs = 0xBEEF1234ULL;
    auto rnd = [&]() {
        std::uint64_t z = (rs += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    };

    const auto rnd_s = score([&](std::size_t) { return rnd() % cb.size(); });
    std::printf("  random pair         |   %6.3f%%  |        %7zu          | the floor\n",
                rnd_s.first, rnd_s.second);

    const auto top_s = score([&](std::size_t i) {
        const auto& l = cb.links(i);
        return l.empty() ? static_cast<std::size_t>(-1) : static_cast<std::size_t>(l[0]);
    });
    std::printf("  top existing edge   |   %6.3f%%  |        %7zu          | proposes what it knows\n",
                top_s.first, top_s.second);

    const auto fof_s = score([&](std::size_t i) {
        const auto& l = cb.links(i);
        if (l.empty()) return static_cast<std::size_t>(-1);
        const auto& l2 = cb.links(l[0]);
        return l2.empty() ? static_cast<std::size_t>(-1) : static_cast<std::size_t>(l2[0]);
    });
    std::printf("  friend-of-a-friend  |   %6.3f%%  |        %7zu          | two hops out\n",
                fof_s.first, fof_s.second);

    const auto kin_s = score([&](std::size_t i) { return cb.kin(i); });
    std::printf("  common neighbours   |   %6.3f%%  |        %7zu          | THE strong heuristic\n",
                kin_s.first, kin_s.second);

    // ---- Ribosome, selected with no answer key -----------------------------
    //
    // The training assays are the MASKED edges built above -- real structure,
    // withheld from the organism's environment. Nothing from the future is used
    // for selection. The future is the exam, never the study material.
    ChamberConfig cfg;
    cfg.population = 300;
    cfg.genome_codons = 5;
    cfg.mutation_rate = 0.03;
    Chamber ch(cfg, &cb, 20260824);
    for (std::size_t g = 0; g < gens; ++g) (void)ch.step(train);

    const Vm vm(&cb);
    const auto evo_s = score([&](std::size_t i) {
        return cb.nearest_index(vm.run(ch.best().genome, cb.at(i)));
    });
    std::printf("  Ribosome (evolved)  |   %6.3f%%  |        %7zu          | %zu births\n",
                evo_s.first, evo_s.second, ch.births());

    const double best_baseline = std::max({rnd_s.first, top_s.first, fof_s.first, kin_s.first});
    std::printf("\n");
    if (evo_s.first > best_baseline) {
        std::printf("  Evolution BEATS every heuristic by %.3f points, with no answer key\n",
                    evo_s.first - best_baseline);
        std::printf("  anywhere in the loop.\n");
    } else {
        std::printf("  Evolution LOSES to the best heuristic by %.3f points.\n",
                    best_baseline - evo_s.first);
    }
    std::printf("  what it found:\n%s", ch.best().genome.disassemble().c_str());

    std::printf("\n  WHAT THIS MEASURES THAT THE WORDNET BENCH DOES NOT\n");
    std::printf("    Nothing here was labelled by a person. The world model was built\n");
    std::printf("    from one part of the corpus and graded against another, so a\n");
    std::printf("    proposal is right when it turns out to be TRUE OF THE WORLD and\n");
    std::printf("    the system did not already know it. Known pairs score zero however\n");
    std::printf("    true they are -- which is why the \"proposes what it knows\" row is\n");
    std::printf("    there, and it should read 0.000%%.\n");
    return 0;
}
