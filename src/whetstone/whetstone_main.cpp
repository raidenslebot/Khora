// The Whetstone — Khora sharpening itself.
//
// An autonomous self-training session: Khora surveys its faculties,
// drills whichever has the most room to grow, escalates difficulty on
// mastery and evolves its method on shortfall, and logs the competence
// trajectory. No operator hand-holding. Khora deciding what to practise.

#include "khora/whetstone/whetstone.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace khora::whetstone;

int main(int argc, char** argv) {
    std::size_t rounds = 120;
    if (argc > 1) { try { rounds = std::stoul(argv[1]); } catch (...) {} }

    std::printf("==================================================================\n");
    std::printf("  THE WHETSTONE  -  Khora sharpening itself (%zu rounds)\n", rounds);
    std::printf("  Self-directed: drill the weakest, escalate the mastered,\n");
    std::printf("  evolve the method on shortfall. Failure is never the verdict.\n");
    std::printf("==================================================================\n\n");

    Whetstone ws(/*mastery=*/0.90);
    ws.add_faculty(make_relational_faculty());
    ws.add_faculty(make_sequence_faculty());
    ws.add_faculty(make_transitive_faculty());

    const auto trajectory = ws.run(rounds);

    // Print escalation / evolution events (the interesting rounds).
    std::printf("Selected rounds (escalations and evolutions):\n");
    for (const auto& s : trajectory) {
        if (s.escalated || s.evolved) {
            std::printf("  r%-4zu %-20s d=%-3d score=%6.2f%%  %s\n",
                        s.round, s.faculty.c_str(), s.difficulty,
                        s.score * 100.0, s.note.c_str());
        }
    }

    std::printf("\nFinal faculty standings:\n");
    for (std::size_t i = 0; i < ws.faculty_count(); ++i) {
        const auto& st = ws.state(i);
        std::printf("  %-22s  reached difficulty %-3d  last competence %6.2f%%  "
                    "evolution level %d%s\n",
                    ws.faculty(i).name().c_str(), st.difficulty, st.competence * 100.0,
                    ws.faculty(i).evolution_level(), st.maxed ? "  [frontier]" : "");
    }

    // Persist the full trajectory.
    namespace fs = std::filesystem;
    fs::create_directories("data/whetstone");
    {
        std::ofstream js("data/whetstone/session.json");
        js << "{\n  \"rounds\": " << rounds << ",\n  \"trajectory\": [\n";
        for (std::size_t i = 0; i < trajectory.size(); ++i) {
            const auto& s = trajectory[i];
            js << "    {\"round\": " << s.round
               << ", \"faculty\": \"" << s.faculty << "\""
               << ", \"difficulty\": " << s.difficulty
               << ", \"score\": " << s.score
               << ", \"escalated\": " << (s.escalated ? "true" : "false")
               << ", \"evolved\": " << (s.evolved ? "true" : "false") << "}"
               << (i + 1 < trajectory.size() ? "," : "") << "\n";
        }
        js << "  ]\n}\n";
    }

    std::printf("\nTrajectory saved to data/whetstone/session.json\n");
    return 0;
}
