#include "khora/curator/curator.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace khora::curator {

using khora::reservoir::Reservoir;
using khora::reservoir::Aqueduct;
using khora::reservoir::Source;
using khora::reservoir::seed_catalog;

StudyOutcome study_tome(Reservoir& pool, khora::lexicon::Lexicon& lex,
                        khora::cortex::PredictiveColumn& cortex,
                        const std::string& title, std::size_t max_tokens,
                        khora::lattice::Lattice* concept_space) {
    StudyOutcome o;
    o.title = title;
    auto text = pool.read(title);   // bumps times_read
    if (!text) { o.error = "not in pool: " + title; return o; }

    o.acc_before   = cortex.recent_accuracy();
    o.vocab_before = lex.vocabulary_size();

    auto tokens = khora::lexicon::tokenize(*text);
    if (tokens.size() > max_tokens) tokens.resize(max_tokens);
    for (std::size_t ti = 0; ti < tokens.size(); ++ti) {
        const auto g = lex.glyph_for(tokens[ti]);
        if ((ti & 0x3FF) == 0) cortex.step(g);
        else                   cortex.learn(g);
    }
    o.cooccurrences = lex.expose_sequence(tokens, 3);

    o.tokens      = tokens.size();
    o.acc_after   = cortex.recent_accuracy();
    o.vocab_after = lex.vocabulary_size();
    o.yield = std::max(0.0, o.acc_after - o.acc_before) +
              0.0001 * static_cast<double>(o.vocab_after - o.vocab_before);

    // Promote learned words into the thinkable concept space, but demote
    // resonance HUBS — concepts that are the nearest neighbour of many
    // others (the distributionally-central connective words like "will"
    // / "with" that otherwise swallow every train of thought). We measure
    // hubness directly: build a lattice of candidates, tally each one's
    // in-degree as a top-k neighbour, and promote the LEAST hub-like.
    if (concept_space) {
        const std::size_t kKeep = 400;
        auto words = lex.salient_tokens(/*pool*/1000, /*min_exposure*/4);
        if (!words.empty()) {
            khora::lattice::Lattice cand;
            for (const auto& w : words) cand.store(w, lex.glyph_for(w));

            std::unordered_map<std::string, int> indeg;
            for (const auto& w : words) indeg[w] = 0;
            for (const auto& w : words) {
                for (const auto& m : cand.query(lex.glyph_for(w), 5)) {
                    if (m.label != w) ++indeg[m.label];   // w points at m -> m's in-degree
                }
            }
            std::sort(words.begin(), words.end(),
                      [&](const std::string& a, const std::string& b) {
                          if (indeg[a] != indeg[b]) return indeg[a] < indeg[b];  // less hubby first
                          return lex.exposures_for(a) > lex.exposures_for(b);
                      });
            const std::size_t keep = std::min(kKeep, words.size());
            for (std::size_t i = 0; i < keep; ++i) {
                if (auto g = cand.recall(words[i])) concept_space->store(words[i], *g);
            }
        }
    }

    // Mastery from accumulated reads (saturating).
    double reads = 0.0;
    for (const auto& t : pool.catalog()) if (t.title == title) reads = t.times_read;
    o.mastery = 1.0 - 1.0 / (1.0 + 0.25 * reads);
    pool.record_learning(title, o.yield, o.mastery);
    o.ok = true;
    return o;
}

Curator::Curator(Reservoir& pool, Aqueduct& aqueduct,
                 khora::lexicon::Lexicon& lex,
                 khora::cortex::PredictiveColumn& cortex,
                 khora::lattice::Lattice* concept_space)
    : pool_(pool), aqueduct_(aqueduct), lex_(lex), cortex_(cortex),
      concept_space_(concept_space) {}

Decision Curator::decide() const {
    const auto cat = pool_.catalog();
    constexpr std::uint32_t kMaxReReads = 4;  // beyond this, a tome is "learned enough"

    // 1. STUDY freshly-acquired material once — absorb what was just brought in.
    {
        const khora::reservoir::Tome* target = nullptr;
        for (const auto& t : cat) {
            if (t.times_read == 0) { target = &t; break; }
        }
        if (target) {
            Decision d;
            d.kind  = Decision::Study;
            d.title = target->title;
            d.topic = target->topic;
            d.rationale = "newly held \"" + target->title + "\" (" + target->topic +
                          ") not yet absorbed -> study it";
            return d;
        }
    }

    // 2. FORAGE — broaden first: a topic with no material at all.
    {
        std::vector<std::string> have_topics;
        for (const auto& t : cat) have_topics.push_back(t.topic);
        for (const auto& s : seed_catalog()) {
            const bool topic_covered =
                std::find(have_topics.begin(), have_topics.end(), s.topic) != have_topics.end();
            if (!topic_covered && !pool_.has(s.title)) {
                Decision d;
                d.kind  = Decision::Forage;
                d.topic = s.topic;
                d.title = s.title;
                d.rationale = "no material on \"" + s.topic +
                              "\" -> forage \"" + s.title + "\" (seek the new)";
                return d;
            }
        }
    }

    // 3. DEEPEN breadth — acquire another source in an already-covered topic.
    for (const auto& s : seed_catalog()) {
        if (!pool_.has(s.title)) {
            Decision d;
            d.kind  = Decision::Deepen;
            d.topic = s.topic;
            d.title = s.title;
            d.rationale = "all held material absorbed once -> deepen with \"" +
                          s.title + "\" (" + s.topic + ")";
            return d;
        }
    }

    // 4. RE-STUDY the weakest under-mastered tome, but only while it still
    //    has something to teach (read fewer than kMaxReReads times). Beyond
    //    that it is "learned enough" and we do not waste effort on it.
    {
        const khora::reservoir::Tome* target = nullptr;
        for (const auto& t : cat) {
            if (t.mastery >= mastery_target_) continue;
            if (t.times_read >= kMaxReReads)  continue;
            if (!target || t.mastery < target->mastery) target = &t;
        }
        if (target) {
            Decision d;
            d.kind  = Decision::Study;
            d.title = target->title;
            d.topic = target->topic;
            d.rationale = "weakest grasp is \"" + target->title + "\" (mastery " +
                          std::to_string(target->mastery) + ") -> reinforce it";
            return d;
        }
    }

    Decision d;
    d.kind = Decision::Idle;
    d.rationale = "all seed material acquired and absorbed to a working level "
                  "(nothing more valuable to do right now)";
    return d;
}

std::string Curator::act(std::size_t study_tokens) {
    const Decision d = decide();
    std::ostringstream os;
    os << "[curator] " << d.rationale << "\n";

    switch (d.kind) {
        case Decision::Study: {
            const auto o = study_tome(pool_, lex_, cortex_, d.title, study_tokens, concept_space_);
            ++studies_;
            if (o.ok) {
                os << "  STUDIED \"" << o.title << "\": " << o.tokens << " tokens, vocab "
                   << o.vocab_before << "->" << o.vocab_after
                   << ", yield " << o.yield << ", mastery -> " << o.mastery;
            } else {
                os << "  study failed: " << o.error;
            }
            break;
        }
        case Decision::Forage:
        case Decision::Deepen: {
            auto r = aqueduct_.forage(d.topic);
            ++forages_;
            if (r && r->ok) {
                os << "  FORAGED \"" << r->title << "\": "
                   << (r->original_bytes / 1024) << "KB distilled, "
                   << (r->stored_bytes / 1024) << "KB stored, lossless="
                   << (r->verified_lossless ? "yes" : "no");
            } else if (r) {
                os << "  forage failed for \"" << r->title << "\": " << r->error;
            } else {
                // No source in that topic; fall back to any source.
                auto any = aqueduct_.forage("");
                if (any && any->ok) os << "  FORAGED (any) \"" << any->title << "\"";
                else os << "  nothing available to forage";
            }
            break;
        }
        case Decision::Idle:
            os << "  idle — nothing to do right now";
            break;
    }
    return os.str();
}

} // namespace khora::curator
