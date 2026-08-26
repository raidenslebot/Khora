// DOES THE EXTRACTOR LEARN TRUE THINGS?
//
// Khora's Ligature holds 19,475 typed relations pulled from 22 MB of real
// books, and the whole symbolic layer -- deduce(), Crystallize, the abstraction
// tower, infer_path -- stands on them. Inspecting them for the first time found
// this:
//
//     man     is-a: animal(7) creature(4) weber(4) social(3)
//     time     has: come(10) arrived(4) workers(4)
//     body    is-a: row(16) matter(2)
//     mind     has: life(4) eyes(4) arms(1)
//     derived: "man has range", "justice has down", "justice has done"
//
// Every derivation's chain HELD. The reasoning was sound; the premises were
// garbage. And deduce()'s own self-benchmark reported 1.000, because it
// constructs its own cases -- a system inventing a benchmark and scoring itself
// perfectly on it while producing visible nonsense on real data.
//
// Two causes, both structural rather than tuning:
//
//   NO SENTENCE BOUNDARIES. The shared tokenizer discards punctuation, and the
//   extractor scans up to five words past a verb for the head of its object. It
//   therefore walks straight into the next sentence. "weber" and "adler" are
//   proper nouns from the following sentence; "row" was asserted 16 times.
//
//   has/have DID NOT REQUIRE A DETERMINER, though is/are did. English's perfect
//   tense then reads as possession: "the time has come" -> HAS-PART(time, come).
//
// This measures both rules on the same real books, before and against after.
//
// IT ORIGINALLY SAID there is no ground truth for "is this relation true", and
// counted instead how many objects are function words or participles that could
// not be a noun. That claim was false when it was written. data/eval/
// wn_categories.tsv -- 3,031 WordNet categories over 35,767 words -- was already
// in this tree, used by two other benches, and IS-A is exactly what it records.
// So the blocklist metric stayed (it is a useful lower bound) and an EXTERNAL one
// was added beside it.
//
// The blocklist alone was reporting 5.3% error on output that reads like this:
//
//     justice  is-a: interest(5) thief(3) order(2)
//     man      is-a: thing(3) social(2) frontispiece(1)
//     body     is-a: disease(1) held(1) box(1)
//
// A hand-written list of 56 words cannot see that "justice is-a interest" is
// wrong, so it scored 94.7% correct on data that is mostly not. That is the same
// defect as deduce() scoring itself 1.000 -- a metric built out of the same
// assumptions as the thing it measures.
//
// WHAT THE EXTERNAL BAR CAN AND CANNOT SAY. A triple is DECIDABLE when the
// subject appears somewhere in WordNet and the object is one of the category
// names; precision is counted over that subset only. WordNet categories here are
// one level deep and badly incomplete -- "chemist is-a person" and "logic is-a
// science" both score WRONG -- so the absolute number is a FLOOR and a low one.
// It is still worth having, because the same incompleteness applies to every
// version measured, so DIFFERENCES between rule sets are real even though the
// level is not. Chance is reported beside it by reshuffling the objects within
// the decidable set, which is the only way to read the level at all.

#include "khora/lexicon/lexicon.hpp"
#include "khora/ligature/ligature.hpp"
#include "khora/reservoir/reservoir.hpp"
#include "khora/taxis/taxis.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <unordered_map>
#include <string>
#include <unordered_set>
#include <vector>

using namespace khora::ligature;

namespace {

// Words that cannot be the object of a taxonomic or part-whole relation. Past
// participles and adverbs, mostly -- the signature of the perfect-tense bug.
// A count of how many objects land in this set is a lower bound on the error
// rate, since it catches only the unambiguous cases.
const std::unordered_set<std::string>& impossible_objects() {
    static const std::unordered_set<std::string> s = {
        "come", "gone", "done", "been", "got", "made", "taken", "given", "seen",
        "known", "found", "said", "told", "put", "kept", "left", "held", "brought",
        "become", "arrived", "passed", "failed", "exhausted", "grown", "deprived",
        "plunged", "occurred", "rendered", "followed", "elicited", "taught",
        "signified", "down", "too", "away", "perhaps", "hitherto", "before",
        "anything", "nothing", "yourself", "himself", "herself", "another",
        "uniformly", "easy", "best", "greatest", "fair", "ordinary", "similar",
        "necessary", "tyrannical", "unsuccessful", "exact", "through", "outside"
    };
    return s;
}

struct Report {
    std::uint64_t triples = 0;
    std::size_t   objects_sampled = 0;
    std::size_t   impossible = 0;
};

Report audit(const Ligature& lig, const std::vector<std::string>& probes) {
    Report r;
    r.triples = lig.triple_count();
    for (const auto& p : probes) {
        for (const Relation rel : {Relation::IsA, Relation::Causes, Relation::HasPart}) {
            for (const auto& [o, c] : lig.objects(rel, p, 8)) {
                (void)c;
                ++r.objects_sampled;
                if (impossible_objects().count(o)) ++r.impossible;
            }
        }
    }
    return r;
}

void show(const Ligature& lig, const std::vector<std::string>& probes, const char* label) {
    std::printf("\n  --- %s ---\n", label);
    for (const auto& p : probes) {
        const auto isa = lig.objects(Relation::IsA, p, 3);
        const auto has = lig.objects(Relation::HasPart, p, 3);
        if (isa.empty() && has.empty()) continue;
        std::printf("  %-11s", p.c_str());
        if (!isa.empty()) {
            std::printf(" is-a:");
            for (const auto& [o, c] : isa) std::printf(" %s(%u)", o.c_str(), c);
        }
        if (!has.empty()) {
            std::printf("  has:");
            for (const auto& [o, c] : has) std::printf(" %s(%u)", o.c_str(), c);
        }
        std::printf("\n");
    }
}

// --- THE EXTERNAL BAR -------------------------------------------------------
//
// wn_categories.tsv is "category<TAB>member member member". Inverted, it answers
// "is Y one of X's categories", which is the IS-A question.
struct WordNet {
    std::unordered_map<std::string, std::unordered_set<std::string>> cats;   // name -> members
    std::unordered_map<std::string, std::unordered_set<std::string>> of;     // word -> categories
    bool ok = false;
};

WordNet load_wordnet(const std::string& path) {
    WordNet wn;
    std::ifstream in(path);
    if (!in) return wn;
    std::string line;
    while (std::getline(in, line)) {
        const auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        const std::string name = line.substr(0, tab);
        std::istringstream ws(line.substr(tab + 1));
        std::string w;
        while (ws >> w) { wn.cats[name].insert(w); wn.of[w].insert(name); }
    }
    wn.ok = !wn.cats.empty();
    return wn;
}

struct Score { std::size_t decidable = 0, correct = 0, chance = 0; };

// The same scorer, but a triple only counts if the OBJECT is a word the
// distributional tagger calls a noun. This is the whole question Taxis exists
// to answer: the extractor takes the last content word of the noun phrase as the
// head, which is right, and has no way to know whether that word is a noun.
Score score_isa_gated(const Ligature& lig, const WordNet& wn,
                      const khora::taxis::Taxis& tx, std::uint32_t min_support,
                      bool gate) {
    Score sc;
    std::vector<std::string> subs, objs;
    for (const auto& [w, cs] : wn.of) {
        (void)cs;
        for (const auto& [o, c] : lig.objects(Relation::IsA, w, 64)) {
            if (c < min_support || !wn.cats.count(o)) continue;
            if (gate && !tx.is_noun(o)) continue;
            ++sc.decidable;
            if (wn.of.at(w).count(o)) ++sc.correct;
            subs.push_back(w);
            objs.push_back(o);
        }
    }
    std::mt19937 rng(20260825);
    std::shuffle(objs.begin(), objs.end(), rng);
    for (std::size_t i = 0; i < subs.size(); ++i)
        if (wn.of.at(subs[i]).count(objs[i])) ++sc.chance;
    return sc;
}

// A 95% Wilson interval on a proportion. At 32 hits in 1,715 the difference
// between the extractor and chance is a couple of dozen events, and a bare
// percentage invites reading that as a result.
std::pair<double, double> wilson(std::size_t hits, std::size_t n) {
    if (n == 0) return {0.0, 0.0};
    const double z = 1.96, ph = static_cast<double>(hits) / static_cast<double>(n);
    const double d = 1.0 + z * z / static_cast<double>(n);
    const double c = ph + z * z / (2.0 * static_cast<double>(n));
    const double m = z * std::sqrt(ph * (1.0 - ph) / static_cast<double>(n)
                                   + z * z / (4.0 * static_cast<double>(n) * static_cast<double>(n)));
    return {100.0 * (c - m) / d, 100.0 * (c + m) / d};
}

// EVERY is-a triple the graph holds, not only the ones under the fifteen probe
// words -- scoring the probes would measure fifteen words rather than the
// extractor.
Score score_isa(const Ligature& lig, const WordNet& wn, std::uint32_t min_support) {
    Score sc;
    std::vector<std::string> subs, objs;
    for (const auto& [w, cs] : wn.of) {
        (void)cs;
        for (const auto& [o, c] : lig.objects(Relation::IsA, w, 64)) {
            if (c < min_support || !wn.cats.count(o)) continue;
            ++sc.decidable;
            if (wn.of.at(w).count(o)) ++sc.correct;
            subs.push_back(w);
            objs.push_back(o);
        }
    }
    // Chance: the same subjects against the same objects, reshuffled. With a
    // ground truth this incomplete the level means nothing without it.
    std::mt19937 rng(20260825);
    std::shuffle(objs.begin(), objs.end(), rng);
    for (std::size_t i = 0; i < subs.size(); ++i)
        if (wn.of.at(subs[i]).count(objs[i])) ++sc.chance;
    return sc;
}

} // namespace

int main(int argc, char** argv) {
    const std::string dir       = (argc > 1) ? argv[1] : "data/reservoir";
    const std::string wn_path   = (argc > 2) ? argv[2] : "data/eval/wn_categories.tsv";
    const std::size_t max_books = (argc > 3) ? std::stoul(argv[3]) : 60;
    const WordNet     wn        = load_wordnet(wn_path);
    khora::reservoir::Reservoir res(dir);
    res.load_catalog();
    const auto cat = res.catalog();
    std::printf("Relation extraction, measured on Khora's own books\n");
    if (cat.empty()) { std::printf("  empty catalog\n"); return 0; }

    const std::vector<std::string> probes = {
        "man", "woman", "government", "money", "light", "time", "water",
        "law", "justice", "labour", "body", "mind", "state", "war", "nature"
    };

    // TWO PASSES, and the first one is why. The noun veto needs a tagger, and a
    // tagger has to have seen the words before it can vote on them, so Taxis
    // reads the whole corpus before a single relation is extracted.
    khora::taxis::Taxis tx;
    {
        std::size_t nb = 0;
        for (const auto& t : cat) {
            if (nb >= max_books) break;
            auto text = res.read(t.title);
            if (!text || text->size() < 40000) continue;
            ++nb;
            for (const auto& sent : khora::lexicon::tokenize_sentences(*text)) tx.observe(sent);
        }
    }

    Ligature flat, split, vetoed;
    std::size_t books = 0, sentences = 0, tokens = 0;
    for (const auto& t : cat) {
        if (books >= max_books) break;
        auto text = res.read(t.title);
        if (!text || text->size() < 40000) continue;
        ++books;

        // OLD: the whole document as one token stream, boundaries invisible.
        const auto flat_toks = khora::lexicon::tokenize(*text);
        tokens += flat_toks.size();
        flat.extract(flat_toks);

        // NEW: one call per sentence, so nothing can read past the full stop.
        for (const auto& s : khora::lexicon::tokenize_sentences(*text)) {
            ++sentences;
            split.extract(s);
            // NEWEST: the same rules, with the tagger allowed to veto an is-a
            // whose head it has positive evidence is not a noun.
            vetoed.extract(s, Ligature::PatAll, &tx);
        }
    }

    std::printf("  %zu books, %zu tokens, %zu sentences (%.1f words each)\n",
                books, tokens, sentences,
                sentences ? static_cast<double>(tokens) / sentences : 0.0);

    const Report a = audit(flat, probes);
    const Report b = audit(split, probes);
    const Report c = audit(vetoed, probes);

    std::printf("\n  rule set                  | triples | objects | impossible objects\n");
    std::printf("  --------------------------+---------+---------+--------------------\n");
    std::printf("  document-at-once (old)    | %7llu | %7zu |  %3zu  (%.1f%%)\n",
                static_cast<unsigned long long>(a.triples), a.objects_sampled,
                a.impossible,
                a.objects_sampled ? 100.0 * a.impossible / a.objects_sampled : 0.0);
    std::printf("  per-sentence + det rule   | %7llu | %7zu |  %3zu  (%.1f%%)\n",
                static_cast<unsigned long long>(b.triples), b.objects_sampled,
                b.impossible,
                b.objects_sampled ? 100.0 * b.impossible / b.objects_sampled : 0.0);
    std::printf("  + hearst + noun veto      | %7llu | %7zu |  %3zu  (%.1f%%)\n",
                static_cast<unsigned long long>(c.triples), c.objects_sampled,
                c.impossible,
                c.objects_sampled ? 100.0 * c.impossible / c.objects_sampled : 0.0);

    // --- SCORED AGAINST WORDNET, NOT AGAINST A LIST I WROTE ------------------
    if (!wn.ok) {
        std::printf("\n  (no %s -- external scoring skipped)\n", wn_path.c_str());
    } else {
        std::printf("\n  === IS-A AGAINST WORDNET (external ground truth) ===\n");
        std::printf("    rule set                | decidable | correct |  prec  | chance\n");
        std::printf("    ------------------------+-----------+---------+--------+-------\n");
        auto row = [](const char* lbl, const Score& s) {
            std::printf("    %-23s | %9zu | %7zu | %5.2f%% | %5.2f%%\n", lbl,
                        s.decidable, s.correct,
                        s.decidable ? 100.0 * s.correct / s.decidable : 0.0,
                        s.decidable ? 100.0 * s.chance  / s.decidable : 0.0);
        };
        row("document-at-once (old)",  score_isa(flat,  wn, 1));
        row("per-sentence + det rule", score_isa(split,  wn, 1));
        row("+ hearst + noun veto",    score_isa(vetoed, wn, 1));

        std::printf("\n    and against a corroboration floor:\n");
        for (const std::uint32_t f : {1u, 2u, 3u}) {
            const Score s = score_isa(split, wn, f);
            std::printf("      support >= %u          | %9zu | %7zu | %5.2f%% | %5.2f%%\n",
                        f, s.decidable, s.correct,
                        s.decidable ? 100.0 * s.correct / s.decidable : 0.0,
                        s.decidable ? 100.0 * s.chance  / s.decidable : 0.0);
        }
        std::printf("\n    DECIDABLE means the subject is a word WordNet knows AND the object\n"
                    "    is one of its category names. Those categories are one level deep\n"
                    "    and incomplete: chemist is-a person, and logic is-a science, both\n"
                    "    score WRONG -- so precision here is a FLOOR, not the true rate.\n"
                    "    Chance is the same pairs reshuffled, and makes the floor readable.\n");
    }

    show(flat,  probes, "BEFORE: document-at-once");
    show(split, probes, "AFTER: per-sentence, has/have needs a determiner");

    // CORROBORATION. A relation asserted once, in one book, by one pattern, is
    // a guess. One asserted repeatedly across a corpus has been seen agreeing
    // with itself. The Plexus already refuses co-occurrences below a floor of
    // three at query time; the Ligature has no such rule and surfaces
    // support-1 triples as facts.
    //
    // This is the cheapest available filter and the question is how much of
    // the noise it removes -- and how much of the signal goes with it.
    std::printf("\n  === CORROBORATION FLOOR ===\n");
    std::printf("    min support | surviving objects | impossible | rate\n");
    std::printf("    ------------+-------------------+------------+-------\n");
    for (const std::uint32_t floor : {1u, 2u, 3u, 5u}) {
        std::size_t kept = 0, bad = 0;
        for (const auto& p : probes) {
            for (const Relation rel : {Relation::IsA, Relation::Causes, Relation::HasPart}) {
                for (const auto& [o, c] : split.objects(rel, p, 8)) {
                    if (c < floor) continue;
                    ++kept;
                    if (impossible_objects().count(o)) ++bad;
                }
            }
        }
        std::printf("    %11u | %17zu | %10zu | %5.1f%%\n", floor, kept, bad,
                    kept ? 100.0 * bad / kept : 0.0);
    }
    std::printf("\n    --- what survives a floor of 3 ---\n");
    for (const auto& p : probes) {
        bool any = false;
        for (const Relation rel : {Relation::IsA, Relation::Causes, Relation::HasPart}) {
            for (const auto& [o, c] : split.objects(rel, p, 8)) {
                if (c < 3) continue;
                if (!any) { std::printf("    %-11s", p.c_str()); any = true; }
                std::printf(" %s-%s(%u)", relation_name(rel), o.c_str(), c);
            }
        }
        if (any) std::printf("\n");
    }

    // --- A PART OF SPEECH, LEARNED FROM WHERE WORDS STAND --------------------
    //
    // The extractor lands on the last content word of the noun phrase and cannot
    // tell whether that word is a noun. Taxis counts what came before each word
    // over the same books and reads a category off the counts, with no tagged
    // corpus anywhere. WordNet is the check: every one of its 35,767 member
    // words is a noun, so the fraction of them Taxis agrees with is recall on a
    // set it never saw.
    if (wn.ok) {
        std::size_t known = 0, as_noun = 0, as_adj = 0, as_verb = 0, unknown = 0;
        for (const auto& [w, cs] : wn.of) {
            (void)cs;
            if (tx.evidence(w).seen == 0) continue;
            ++known;
            switch (tx.tag(w)) {
                case khora::taxis::Tag::Noun:      ++as_noun; break;
                case khora::taxis::Tag::Adjective: ++as_adj;  break;
                case khora::taxis::Tag::Verb:      ++as_verb; break;
                default:                           ++unknown; break;
            }
        }
        std::printf("\n  === TAXIS: a part of speech from distribution alone ===\n");
        std::printf("    %zu word types seen, %zu given a category\n",
                    tx.vocabulary(), tx.tagged());
        std::printf("    of %zu WordNet nouns that appear in these books:\n", known);
        std::printf("      called noun %zu (%.1f%%), adjective %zu, verb %zu, no category %zu\n",
                    as_noun, known ? 100.0 * as_noun / known : 0.0, as_adj, as_verb, unknown);
        std::printf("    every one of those words IS a noun, so anything but the first\n"
                    "    column is an error and the first column is recall.\n");
    }

    // --- WHICH RULE IS THE POISON --------------------------------------------
    //
    // Every rule ran together and the union was the only thing measurable. Each
    // one now runs alone over the identical sentences and is scored on the
    // identical external bar, so the answer is attributable instead of guessed.
    if (wn.ok) {
        struct Row { const char* name; std::uint32_t mask; Ligature lig; };
        std::vector<Row> rows;
        rows.push_back({"copula: X is a Y",      Ligature::PatCopula,    Ligature{}});
        rows.push_back({"hearst: Y such as X",   Ligature::PatSuchAs,    Ligature{}});
        rows.push_back({"hearst: X and other Y", Ligature::PatOther,     Ligature{}});
        rows.push_back({"hearst: Y including X", Ligature::PatIncluding, Ligature{}});
        rows.push_back({"all hearst together",   Ligature::PatHearst,    Ligature{}});
        rows.push_back({"everything",            Ligature::PatAll,       Ligature{}});

        std::size_t nb = 0;
        for (const auto& t : cat) {
            if (nb >= max_books) break;
            auto text = res.read(t.title);
            if (!text || text->size() < 40000) continue;
            ++nb;
            for (const auto& sent : khora::lexicon::tokenize_sentences(*text))
                for (auto& r : rows) r.lig.extract(sent, r.mask);
        }

        std::printf("\n  === PER-PATTERN, SAME SENTENCES, SAME EXTERNAL BAR ===\n");
        std::printf("    pattern                | is-a triples | decidable | correct |  prec  | 95%% CI          | chance\n");
        std::printf("    -----------------------+--------------+-----------+---------+--------+-----------------+-------\n");
        for (auto& r : rows) {
            const Score sc = score_isa(r.lig, wn, 1);
            const auto ci = wilson(sc.correct, sc.decidable);
            std::size_t isa_total = 0;
            for (const auto& [w, cs] : wn.of) { (void)cs; isa_total += r.lig.objects(Relation::IsA, w, 64).size(); }
            std::printf("    %-22s | %12llu | %9zu | %7zu | %5.2f%% | [%5.2f%%, %5.2f%%] | %5.2f%%\n",
                        r.name, static_cast<unsigned long long>(r.lig.triple_count()),
                        sc.decidable, sc.correct,
                        sc.decidable ? 100.0 * sc.correct / sc.decidable : 0.0,
                        ci.first, ci.second,
                        sc.decidable ? 100.0 * sc.chance / sc.decidable : 0.0);
            (void)isa_total;
        }
        std::printf("\n    Chance is that row's own subjects paired with its own objects,\n"
                    "    reshuffled. A pattern is worth having only if its interval clears it.\n");

        // --- AND WHAT THE TAGGER IS WORTH ------------------------------------
        std::printf("\n    with the object required to be a noun Taxis recognises:\n");
        std::printf("    pattern                | gate | decidable | correct |  prec  | 95%% CI\n");
        std::printf("    -----------------------+------+-----------+---------+--------+----------------\n");
        for (auto& r : rows) {
            for (const bool g : {false, true}) {
                const Score sc = score_isa_gated(r.lig, wn, tx, 1, g);
                const auto ci = wilson(sc.correct, sc.decidable);
                std::printf("    %-22s | %4s | %9zu | %7zu | %5.2f%% | [%5.2f%%, %5.2f%%]\n",
                            r.name, g ? "noun" : "  --", sc.decidable, sc.correct,
                            sc.decidable ? 100.0 * sc.correct / sc.decidable : 0.0,
                            ci.first, ci.second);
            }
        }
    }

    // --- WHERE THE NOUN GATE ACTUALLY ACTS, SINCE THE BAR ABOVE CANNOT SEE IT
    //
    // The gated rows above barely move, and that is a limitation of my own
    // harness rather than a result about the gate. A triple is DECIDABLE only
    // when its object is a WordNet category name -- and every WordNet category
    // name is a noun. The decidable subset is therefore nouns by construction,
    // so a rule that removes non-nouns has almost nothing left to remove inside
    // it. is-a(man, social), the case Taxis was built for, was never in it.
    //
    // So the gate is measured against two reference sets it has never seen,
    // over the WHOLE graph rather than the decidable slice:
    //
    //   KNOWN BAD -- the 56 hand-listed objects that cannot be the object of a
    //   relation, the blocklist this bench used to score with. How many does the
    //   gate catch? That is recall on errors somebody else labelled.
    //
    //   KNOWN GOOD -- objects that are WordNet category names, so certainly
    //   nouns. How many does the gate wrongly throw away? That is its cost.
    if (wn.ok) {
        std::unordered_set<std::string> subjects;
        for (const auto& [w, cs] : wn.of) { (void)cs; subjects.insert(w); }
        std::printf("\n  === WHAT THE NOUN GATE REMOVES ===\n");
        for (int mode = 0; mode < 2; ++mode) {
            std::size_t all_obj = 0, all_cut = 0, bad_obj = 0, bad_cut = 0,
                        good_obj = 0, good_cut = 0;
            for (const auto& sub : subjects) {
                for (const auto& [o, c] : split.objects(Relation::IsA, sub, 64)) {
                    (void)c;
                    const bool cut = (mode == 0) ? !tx.is_noun(o) : tx.rejects_noun(o);
                    ++all_obj; if (cut) ++all_cut;
                    if (impossible_objects().count(o)) { ++bad_obj; if (cut) ++bad_cut; }
                    if (wn.cats.count(o))              { ++good_obj; if (cut) ++good_cut; }
                }
            }
            std::printf("\n    %s\n", mode == 0
                ? "REQUIRE a noun -- which refuses everything below the evidence floor too:"
                : "REFUSE only a positive non-noun -- absence of evidence is not evidence:");
            std::printf("      is-a objects examined            : %zu, gate cuts %zu (%.1f%%)\n",
                        all_obj, all_cut, all_obj ? 100.0 * all_cut / all_obj : 0.0);
            std::printf("      of the KNOWN BAD (blocklisted)   : %zu, cut %zu (%.1f%%)  <- recall on errors\n",
                        bad_obj, bad_cut, bad_obj ? 100.0 * bad_cut / bad_obj : 0.0);
            std::printf("      of the KNOWN GOOD (WordNet nouns): %zu, cut %zu (%.1f%%)  <- what it costs\n",
                        good_obj, good_cut, good_obj ? 100.0 * good_cut / good_obj : 0.0);
        }

        // --- AND WHERE TO PUT THE THRESHOLD ----------------------------------
        //
        // Both policies above sit at one setting of one number: how large a
        // share of a word's noun-phrase evidence has to be the marked kind
        // before it is called an adjective. That number is a calibration and
        // pretending otherwise would be the tuned-knob mistake, so the whole
        // curve is printed and the choice is made in the open.
        std::printf("\n    the threshold is one number, so here is the whole curve:\n");
        std::printf("      adj share | cuts overall | KNOWN BAD cut | KNOWN GOOD cut | ratio\n");
        std::printf("      ----------+--------------+---------------+----------------+------\n");
        for (const double r : {0.0, 0.05, 0.10, 0.15, 0.25, 0.40, 0.60, 1.01}) {
            std::size_t ao = 0, ac = 0, bo = 0, bc = 0, go = 0, gc = 0;
            for (const auto& sub : subjects) {
                for (const auto& [o, c] : split.objects(Relation::IsA, sub, 64)) {
                    (void)c;
                    const khora::taxis::Tag t = tx.tag(o, r);
                    const bool cut = (t != khora::taxis::Tag::Noun);
                    ++ao; if (cut) ++ac;
                    if (impossible_objects().count(o)) { ++bo; if (cut) ++bc; }
                    if (wn.cats.count(o))              { ++go; if (cut) ++gc; }
                }
            }
            const double bad  = bo ? 100.0 * bc / bo : 0.0;
            const double good = go ? 100.0 * gc / go : 0.0;
            std::printf("      %9.2f | %11.1f%% | %12.1f%% | %13.1f%% | %5.2f\n", r,
                        ao ? 100.0 * ac / ao : 0.0, bad, good,
                        good > 0.0 ? bad / good : 0.0);
        }
        std::printf("      (1.01 disables the adjective rule entirely -- the last row is the\n"
                    "       determiner-only tagger this started as.)\n");

        std::printf("\n    Neither reference set was available to Taxis, which sees only which\n"
                    "    word came before which over the same books.\n");
    }

    // --- AND DOES THE WEIGHT HELP, ON BOOKS IT WAS NOT DERIVED FROM ----------
    //
    // The 3.6 ratio came off the first half of the catalogue. Applying it there
    // and reporting the result there would be reading a tuned parameter back as
    // a finding, so the weighted graph is scored on the second half, which the
    // ratio never saw. The unweighted comparison is the same sentences with the
    // Hearst assertions counted as one each.
    if (wn.ok) {
        Ligature with_hearst, flat_w;
        std::size_t seen = 0, held = 0;
        for (const auto& t : cat) {
            auto text = res.read(t.title);
            if (!text || text->size() < 40000) continue;
            ++seen;
            if (seen <= max_books) continue;          // calibration half, skipped
            if (held >= max_books) break;
            ++held;
            for (const auto& sent : khora::lexicon::tokenize_sentences(*text)) {
                with_hearst.extract(sent, Ligature::PatAll);
                // The same rules with Hearst worth one: copula-only plus each
                // Hearst frame asserted through a graph that cannot weight them
                // differently is what the system had before.
                flat_w.extract(sent, Ligature::PatCopula | Ligature::PatCausal |
                                     Ligature::PatPossess);
            }
        }
        if (held == 0) {
            std::printf("\n  (no held-out books beyond the first %zu -- weighting not validated)\n", max_books);
        } else {
            std::printf("\n  === HELD OUT: %zu books the weight was not derived from ===\n", held);
            std::printf("    graph                        | floor | decidable | correct |  prec  | 95%% CI\n");
            std::printf("    -----------------------------+-------+-----------+---------+--------+----------------\n");
            auto line = [](const char* lbl, std::uint32_t f, const Score& sc) {
                const auto ci = wilson(sc.correct, sc.decidable);
                std::printf("    %-28s | %5u | %9zu | %7zu | %5.2f%% | [%5.2f%%, %5.2f%%]\n", lbl, f,
                            sc.decidable, sc.correct,
                            sc.decidable ? 100.0 * sc.correct / sc.decidable : 0.0,
                            ci.first, ci.second);
            };
            line("copula+causal+has, as before", 1, score_isa(flat_w,   wn, 1));
            line("copula+causal+has, as before", 2, score_isa(flat_w,   wn, 2));
            line("with the hearst frames",       1, score_isa(with_hearst, wn, 1));
            line("with the hearst frames",       2, score_isa(with_hearst, wn, 2));
            std::printf("\n    A weight of 3 on Hearst sightings was tried here and reverted:\n"
                        "    6.98%% [3.24, 14.40] against 5.88%% [2.72, 12.24] on these same\n"
                        "    held-out books is not a difference, and it would have cost support\n"
                        "    its meaning -- a triple reported at 3 might have been seen once.\n"
                        "    Note what the weight was doing to the row above it: weighted, the\n"
                        "    floor of 2 left 102 decidable triples standing, because a lone\n"
                        "    Hearst sighting cleared it alone. Unweighted it is 32. The gain I\n"
                        "    first recorded as the consolation prize was the reverted change.\n");
        }
    }

    std::printf("\n  'impossible objects' counts objects that cannot be the object of\n"
                "  a taxonomic or part-whole relation -- past participles and adverbs,\n"
                "  the signature of reading perfect tense as possession. It catches\n"
                "  only unambiguous cases, so it is a LOWER BOUND on the error rate.\n");
    return 0;
}
