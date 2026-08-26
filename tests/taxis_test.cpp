// A part of speech with no tagged data, and the two rules that were wrong first.
//
// Taxis exists because the relation extractor lands on the last content word of
// a noun phrase and has no way to know whether that word is a noun, which is how
// "man is a social being" became is-a(man, social). It counts what came before
// each word and reads a category off the counts.
//
// The interesting checks here are not that it works but that it fails the two
// ways it failed on real text, both of which look correct when you write them:
//
//   ARGMAX OVER THE THREE COUNTS. Wrong, because an adjective follows a
//   determiner far more often than it follows "very" -- "the best man" beats
//   "very best" -- so the largest count is the determiner one and the adjective
//   is called a noun.
//
//   MARKED EVIDENCE WITH ONLY AN ABSOLUTE FLOOR. Also wrong, the other way: over
//   eight million tokens almost any word follows "so" twice, so this called 38%
//   of words WordNet certifies as nouns adjectives.
//
// What is left is a ratio, and the two cases below pin both sides of it.

#include "khora/taxis/taxis.hpp"
#include "khora/ligature/ligature.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace khora::taxis;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("  FAIL: %s\n", what); ++failures; }
    else       { std::printf("  ok  : %s\n", what); }
}

std::vector<std::string> toks(const char* s) {
    std::vector<std::string> out;
    std::string cur;
    for (const char* p = s; ; ++p) {
        if (*p == 0 || *p == ' ') { if (!cur.empty()) out.push_back(cur); cur.clear(); if (!*p) break; }
        else cur.push_back(*p);
    }
    return out;
}

} // namespace

int main() {
    std::printf("Taxis — a part of speech from distribution alone\n\n");

    // --- THE THREE CONTEXTS ARE COUNTED SEPARATELY ---------------------------
    {
        Taxis t;
        for (int i = 0; i < 3; ++i) t.observe(toks("the sparrow sang"));
        for (int i = 0; i < 3; ++i) t.observe(toks("very anxious birds fled"));
        for (int i = 0; i < 3; ++i) t.observe(toks("he tried to escape"));

        check(t.evidence("sparrow").det == 3,   "a determiner before a word is counted");
        check(t.evidence("anxious").degree == 3, "a degree adverb before a word is counted");
        check(t.evidence("escape").modal == 3,  "an infinitive marker before a word is counted");
        check(t.evidence("sparrow").degree == 0 && t.evidence("anxious").det == 0,
              "and each occurrence lands in exactly one bucket");
        check(t.tag("sparrow") == Tag::Noun,      "so a word after determiners is a noun");
        check(t.tag("anxious") == Tag::Adjective, "one after degree adverbs is an adjective");
        check(t.tag("escape")  == Tag::Verb,      "and one after 'to' is a verb");
    }

    // --- THE FLOOR: ONE SIGHTING DECIDES NOTHING -----------------------------
    {
        Taxis t;
        t.observe(toks("the aardvark slept"));
        check(t.tag("aardvark") == Tag::Unknown,
              "one sighting is a coincidence, so the word gets no category");
        t.observe(toks("the aardvark woke"));
        check(t.tag("aardvark") == Tag::Noun, "two is evidence");
    }

    // --- AND BOTH SIDES OF THE RATIO -----------------------------------------
    //
    // This is the pair that argmax and a bare floor each get wrong. Same
    // adjective evidence, two counts of it, and the answer differs because the
    // question is what SHARE of the noun-phrase evidence is marked.
    {
        Taxis t;
        // A common noun that happens to follow a degree adverb twice, which over
        // a real corpus is almost every noun.
        for (int i = 0; i < 60; ++i) t.observe(toks("the state endured"));
        for (int i = 0; i < 2;  ++i) t.observe(toks("so state was rare"));
        check(t.evidence("state").det == 60 && t.evidence("state").degree == 2,
              "a noun with 60 determiners and 2 degree adverbs");
        check(t.tag("state") == Tag::Noun,
              "  stays a noun -- 2 of 62 is not a share, and a bare floor lost this");

        Taxis u;
        for (int i = 0; i < 10; ++i) u.observe(toks("the wicked prospered"));
        for (int i = 0; i < 4;  ++i) u.observe(toks("very wicked deeds followed"));
        check(u.evidence("wicked").det == 10 && u.evidence("wicked").degree == 4,
              "an adjective with 10 determiners and 4 degree adverbs");
        check(u.tag("wicked") == Tag::Adjective,
              "  is an adjective -- 4 of 14 is, and an argmax lost this");
    }

    // --- WHAT IT WAS BUILT FOR -----------------------------------------------
    //
    // The whole point: the extractor takes the head of the noun phrase and had
    // no way to know it had landed on a modifier.
    {
        Taxis t;
        for (int i = 0; i < 6; ++i) t.observe(toks("very social gatherings"));
        for (int i = 0; i < 6; ++i) t.observe(toks("the sparrow flew"));
        check(t.rejects_noun("social"), "positive evidence that 'social' is not a noun");
        check(!t.rejects_noun("sparrow"), "and none that 'sparrow' is not one");
        check(!t.rejects_noun("nevermentioned"),
              "an unseen word is not rejected -- absence of evidence is not evidence");

        khora::ligature::Ligature open, gated;
        const auto sentence = toks("man is a social being");
        open.extract(sentence);
        gated.extract(sentence, khora::ligature::Ligature::PatAll, &t);
        check(open.count(khora::ligature::Relation::IsA, "man", "social") == 1,
              "ungated, 'man is a social being' asserts is-a(man, social)");
        check(gated.count(khora::ligature::Relation::IsA, "man", "social") == 0,
              "and the tagger vetoes it");
    }

    // --- IT SURVIVES A RESTART -----------------------------------------------
    {
        const auto dir = std::filesystem::temp_directory_path() / "khora_taxis_test";
        std::filesystem::remove_all(dir);
        Taxis a;
        for (int i = 0; i < 4; ++i) a.observe(toks("the harbour froze"));
        for (int i = 0; i < 4; ++i) a.observe(toks("very bitter winds blew"));
        a.save(dir);
        Taxis b;
        b.load(dir);
        check(b.vocabulary() == a.vocabulary(), "a saved tagger loads the same vocabulary");
        check(b.tag("harbour") == Tag::Noun && b.tag("bitter") == Tag::Adjective,
              "and the same categories");
        std::filesystem::remove_all(dir);
    }

    std::printf("\n");
    if (failures == 0) std::printf("ALL PASS\n");
    else               std::printf("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
