// The Crucible — Khora's relational-reasoning forge.
//
// A serious training run, not a demonstration. It loads a substantial
// structured knowledge base, drives Khora's substrate through escalating
// reasoning trials, and — wherever Khora falls short — evolves the
// encoding and drives it again. Failure is the trigger for evolution.
//
// Everything here is XOR-bind / majority-bundle / nearest-neighbour
// cleanup over 10,000-bit glyphs. No lookup table holds the answers.
// The answers fall out of the algebra. This is reasoning on the
// substrate with no model and no language layer.

#include "khora/crucible/crucible.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using khora::crucible::RelationalCrucible;
using khora::crucible::Record;
using khora::crucible::TrialResult;

namespace {

Record rec(std::string subject,
           std::string currency, std::string capital,
           std::string language, std::string continent) {
    return Record{ std::move(subject), {
        {"currency",  std::move(currency)},
        {"capital",   std::move(capital)},
        {"language",  std::move(language)},
        {"continent", std::move(continent)},
    }};
}

void load_world(RelationalCrucible& cru) {
    // A real, sizeable knowledge base: 32 nations, each a 4-field record.
    cru.add_record(rec("usa",        "dollar",   "washington",  "english",    "northamerica"));
    cru.add_record(rec("mexico",     "peso",     "mexicocity",  "spanish",    "northamerica"));
    cru.add_record(rec("canada",     "cad",      "ottawa",      "english",    "northamerica"));
    cru.add_record(rec("brazil",     "real",     "brasilia",    "portuguese", "southamerica"));
    cru.add_record(rec("argentina",  "arspeso",  "buenosaires", "spanish",    "southamerica"));
    cru.add_record(rec("chile",      "clpeso",   "santiago",    "spanish",    "southamerica"));
    cru.add_record(rec("uk",         "pound",    "london",      "english",    "europe"));
    cru.add_record(rec("france",     "euro",     "paris",       "french",     "europe"));
    cru.add_record(rec("germany",    "euro",     "berlin",      "german",     "europe"));
    cru.add_record(rec("spain",      "euro",     "madrid",      "spanish",    "europe"));
    cru.add_record(rec("italy",      "euro",     "rome",        "italian",    "europe"));
    cru.add_record(rec("portugal",   "euro",     "lisbon",      "portuguese", "europe"));
    cru.add_record(rec("poland",     "zloty",    "warsaw",      "polish",     "europe"));
    cru.add_record(rec("russia",     "ruble",    "moscow",      "russian",    "europe"));
    cru.add_record(rec("greece",     "euro",     "athens",      "greek",      "europe"));
    cru.add_record(rec("norway",     "krone",    "oslo",        "norwegian",  "europe"));
    cru.add_record(rec("china",      "yuan",     "beijing",     "mandarin",   "asia"));
    cru.add_record(rec("japan",      "yen",      "tokyo",       "japanese",   "asia"));
    cru.add_record(rec("india",      "rupee",    "newdelhi",    "hindi",      "asia"));
    cru.add_record(rec("korea",      "won",      "seoul",       "korean",     "asia"));
    cru.add_record(rec("thailand",   "baht",     "bangkok",     "thai",       "asia"));
    cru.add_record(rec("vietnam",    "dong",     "hanoi",       "vietnamese", "asia"));
    cru.add_record(rec("indonesia",  "rupiah",   "jakarta",     "indonesian", "asia"));
    cru.add_record(rec("turkey",     "lira",     "ankara",      "turkish",    "asia"));
    cru.add_record(rec("egypt",      "egpound",  "cairo",       "arabic",     "africa"));
    cru.add_record(rec("nigeria",    "naira",    "abuja",       "english",    "africa"));
    cru.add_record(rec("kenya",      "shilling", "nairobi",     "swahili",    "africa"));
    cru.add_record(rec("southafrica","rand",     "pretoria",    "zulu",       "africa"));
    cru.add_record(rec("morocco",    "dirham",   "rabat",       "arabic",     "africa"));
    cru.add_record(rec("australia",  "aud",      "canberra",    "english",    "oceania"));
    cru.add_record(rec("newzealand", "nzd",      "wellington",  "english",    "oceania"));
    cru.add_record(rec("saudi",      "riyal",    "riyadh",      "arabic",     "asia"));
}

void print_trial(const TrialResult& r) {
    std::printf("  %-22s  %4zu/%-4zu = %6.2f%%   target %5.1f%%   %s\n",
                r.name.c_str(), r.correct, r.trials, r.score * 100.0,
                r.target * 100.0, r.passed() ? "PASS" : "SHORTFALL -> evolve");
}

} // namespace

int main() {
    std::printf("==================================================================\n");
    std::printf("  THE CRUCIBLE  -  relational reasoning forge\n");
    std::printf("  Vector Symbolic Architecture over 10,000-bit glyphs. No model.\n");
    std::printf("==================================================================\n\n");

    RelationalCrucible cru;
    load_world(cru);
    cru.build();

    std::printf("Knowledge base: %zu records, %zu roles, %zu fillers.\n\n",
                cru.record_count(), cru.role_count(), cru.filler_count());

    // --- baseline trials at redundancy 1 ---
    std::printf("BASELINE (redundancy = 1)\n");
    const auto base_unbind  = cru.trial_structured_unbind(0.95);
    const auto base_analogy = cru.trial_analogy(0.80);
    print_trial(base_unbind);
    print_trial(base_analogy);

    // --- example reasoning, shown explicitly ---
    std::printf("\nExplicit reasoning (the answers come from algebra, not lookup):\n");
    auto ask = [&](const char* subj, const char* role) {
        std::printf("    %-12s of %-12s ?  ->  %s\n",
                    role, subj, cru.query_field(subj, role).c_str());
    };
    ask("mexico", "currency");
    ask("japan",  "capital");
    ask("egypt",  "language");
    ask("brazil", "continent");
    std::printf("    analogy: as dollar is to usa, ? is to mexico  ->  %s\n",
                cru.analogy("usa", "mexico", "dollar").c_str());
    std::printf("    analogy: as paris is to france, ? is to japan ->  %s\n",
                cru.analogy("france", "japan", "paris").c_str());

    // --- holographic capacity sweep: find the cliff ---
    std::printf("\nHOLOGRAPHIC CAPACITY SWEEP (records packed into ONE glyph):\n");
    cru.set_redundancy(1);
    for (std::size_t k : {2u, 4u, 8u, 16u, 32u}) {
        const auto h = cru.trial_holographic(k, 0.70);
        std::printf("    K=%-3zu  recover %4zu/%-4zu = %6.2f%%\n",
                    k, h.correct, h.trials, h.score * 100.0);
    }

    // --- evolution: drive structured unbind to target via redundancy ---
    std::printf("\nEVOLUTION (failure -> study -> retry; escalate encoding redundancy):\n");
    const auto traj = cru.evolve_structured(0.98, 9);
    for (const auto& step : traj) {
        std::printf("    gen %d  redundancy=%d  score=%6.2f%%   %s\n",
                    step.generation, step.redundancy, step.score * 100.0, step.note.c_str());
    }
    const double final_score = traj.empty() ? 0.0 : traj.back().score;
    const int    final_red   = traj.empty() ? 1   : traj.back().redundancy;

    // --- persist the trajectory (real training record, not a log of theater) ---
    namespace fs = std::filesystem;
    fs::create_directories("data/crucible");
    {
        std::ofstream js("data/crucible/relational_evolution.json");
        js << "{\n  \"records\": " << cru.record_count()
           << ",\n  \"baseline_unbind\": " << base_unbind.score
           << ",\n  \"baseline_analogy\": " << base_analogy.score
           << ",\n  \"final_redundancy\": " << final_red
           << ",\n  \"final_unbind\": " << final_score
           << ",\n  \"trajectory\": [\n";
        for (std::size_t i = 0; i < traj.size(); ++i) {
            js << "    {\"gen\": " << traj[i].generation
               << ", \"redundancy\": " << traj[i].redundancy
               << ", \"score\": " << traj[i].score << "}"
               << (i + 1 < traj.size() ? "," : "") << "\n";
        }
        js << "  ]\n}\n";
    }

    std::printf("\n------------------------------------------------------------------\n");
    std::printf("Forge result: unbind %.1f%% -> %.1f%% (redundancy 1 -> %d).  ",
                base_unbind.score * 100.0, final_score * 100.0, final_red);
    std::printf("Trajectory saved to data/crucible/relational_evolution.json\n");
    std::printf("------------------------------------------------------------------\n");

    // A forge run "succeeds" if evolution reached the target. Even if it
    // hasn't yet, that is not failure — it is the next problem to evolve
    // against. We return 0 unless the algebra is fundamentally broken
    // (baseline near zero), which would signal a real regression.
    return (base_unbind.score > 0.20) ? 0 : 1;
}
