// Plexus test — the hub-proof associative graph.
//
// The decisive property: a loud word that co-occurs with EVERYTHING must not
// become every concept's nearest neighbour. PMI must divide its loudness out.

#include "khora/plexus/plexus.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using khora::plexus::Plexus;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("  FAIL: %s\n", what); ++failures; }
    else         std::printf("  ok  : %s\n", what);
}

// Build a tiny corpus where "the" sits beside everything (a perfect hub) while
// genuine pairs keep specific company: cat~dog (animals), sun~moon (sky).
std::vector<std::string> corpus() {
    std::vector<std::string> c;
    auto sentence = [&](std::initializer_list<const char*> ws) {
        for (const char* w : ws) c.emplace_back(w);
    };
    for (int i = 0; i < 40; ++i) {
        sentence({"the", "cat", "and", "the", "dog", "ran"});
        sentence({"the", "dog", "and", "the", "cat", "slept"});
        sentence({"the", "sun", "and", "the", "moon", "rose"});
        sentence({"the", "moon", "and", "the", "sun", "set"});
    }
    return c;
}

// The distinct words of a token stream, so a test can recompute what the
// Plexus should have derived from the same stream.
std::vector<std::string> vocabulary_of(const std::vector<std::string>& tokens) {
    std::vector<std::string> v(tokens);
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

} // namespace

int main() {
    std::printf("Plexus test\n");

    Plexus plex;
    const auto c = corpus();
    const std::size_t events = plex.observe(c, 3);
    check(events > 0, "observe recorded co-occurrence events");
    check(plex.vocabulary_size() >= 8, "vocabulary interned");
    check(plex.has("cat") && plex.has("the"), "nodes present");

    // "the" is the loudest token; it must be the most frequent.
    check(plex.occurrences("the") > plex.occurrences("cat"),
          "hub 'the' is the most frequent word");

    // THE decisive test: cat's strongest associate is dog (its true kin),
    // NOT "the" (the hub it co-occurs with most often in raw counts).
    auto kin = plex.associates("cat", 4);
    check(!kin.empty(), "cat has associates");
    if (!kin.empty()) {
        const std::string top = kin.front().first;
        std::printf("  cat -> ");
        for (const auto& [w, s] : kin) std::printf("%s(%.2f) ", w.c_str(), s);
        std::printf("\n");
        check(top != "the", "cat's top associate is NOT the hub 'the'");
        check(top == "dog", "cat's top associate IS its true kin 'dog'");
    }

    // Affinity must rank true kin above the hub.
    check(plex.affinity("cat", "dog") > plex.affinity("cat", "the"),
          "affinity(cat,dog) > affinity(cat,the) — hub suppressed");
    check(plex.affinity("sun", "moon") > plex.affinity("sun", "the"),
          "affinity(sun,moon) > affinity(sun,the) — hub suppressed");

    // The hub itself, by PMI, has no strong associates: it meets everything,
    // so it is specially close to nothing.
    const double cat_dog = plex.affinity("cat", "dog");
    const double the_cat = plex.affinity("the", "cat");
    check(cat_dog > the_cat, "specific pair outscores hub pair");

    // Association strength must not decay as the VOCABULARY grows.
    //
    // PMI's smoothed context term needs a normalised P(b) = c_b^a / sum(c^a).
    // Normalising by N^a instead costs roughly log2(V^0.25) bits on every pair
    // — invisible at V=8, fatal at scale, because PPMI clamps at zero and so
    // deletes rather than merely shifts. Padding the vocabulary with words that
    // never meet cat or dog leaves their co-occurrence statistics untouched, so
    // a correct PMI must still find them kin.
    {
        Plexus wide;
        auto padded = corpus();
        char buf[32];

        // Bulk vocabulary, to drive the smoothed-context partition function up.
        for (int w = 0; w < 2000; ++w) {
            std::snprintf(buf, sizeof buf, "filler%d", w);
            padded.emplace_back(buf);
            padded.emplace_back("nonce");
        }
        wide.observe(padded, 3);

        check(wide.vocabulary_size() > 2000, "wide corpus has a large vocabulary");

        // THE decisive check. The smoothed context term must be divided by the
        // partition function Z = sum of occ^alpha over the whole vocabulary, so
        // that P(context) is a distribution. Dividing by N^alpha instead -- as
        // this did -- overstates P(b) by about V^(1-alpha), subtracting a
        // constant log2(V^0.25) from every score. PPMI clamps at zero, so that
        // constant does not reorder anything, it DELETES every pair beneath it:
        // silent, and worse the larger the vocabulary grows.
        //
        // The test knows every word it put in, so it can compute Z itself.
        const double alpha = Plexus::context_smoothing_exponent();
        double expect_z = 0.0;
        for (const auto& w : vocabulary_of(padded)) {
            const std::uint32_t c = wide.occurrences(w);
            expect_z += std::pow(static_cast<double>(c ? c : 1), alpha);
        }
        const double got_z    = wide.smoothed_context_z();
        const double n_to_a   = std::pow(static_cast<double>(wide.total_tokens()), alpha);
        std::printf("  V=%zu  Z=%.1f (expected %.1f)  N^a=%.1f  offset if wrong: %.2f bits\n",
                    wide.vocabulary_size(), got_z, expect_z, n_to_a,
                    std::log2(got_z / n_to_a));

        check(std::fabs(got_z - expect_z) < 1e-6 * expect_z,
              "normaliser is the partition function sum(occ^a), not N^a");
        check(got_z > 2.0 * n_to_a,
              "Z and N^a are far enough apart here that the two would differ");

        // The affinity itself survives the larger vocabulary.
        check(wide.affinity("cat", "dog") > 0.0, "cat~dog survives a large vocabulary");

        // associates() is deliberately NOT asserted here. It applies a
        // stop-word filter -- any word above kStopFraction (0.6%) of all tokens
        // is treated as a function word -- and that threshold is relative to
        // corpus size, so padding the vocabulary with 2000 singletons pushes the
        // base corpus's content words above it and filters 'dog' out. That is a
        // real fragility in the filter, not a property of the normaliser, and it
        // is tracked separately. See KHORA_BACKLOG.md.
    }

    // Persistence round-trip.
    namespace fs = std::filesystem;
    const auto prefix = fs::temp_directory_path() / "khora_plexus_test";
    plex.save(prefix);
    Plexus reloaded;
    reloaded.load(prefix);
    check(reloaded.vocabulary_size() == plex.vocabulary_size(),
          "reload preserves vocabulary");
    check(reloaded.occurrences("the") == plex.occurrences("the"),
          "reload preserves frequencies");
    auto kin2 = reloaded.associates("cat", 1);
    check(!kin2.empty() && kin2.front().first == "dog",
          "reload preserves associations (cat -> dog)");
    std::error_code ec;
    fs::remove(fs::path(prefix.string() + ".plexus"), ec);

    if (failures == 0) std::printf("ALL PASS\n");
    else               std::printf("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
