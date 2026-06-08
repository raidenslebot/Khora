// Plexus test — the hub-proof associative graph.
//
// The decisive property: a loud word that co-occurs with EVERYTHING must not
// become every concept's nearest neighbour. PMI must divide its loudness out.

#include "khora/plexus/plexus.hpp"

#include <cassert>
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
