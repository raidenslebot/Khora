#pragma once

// The Whetstone — Khora's autonomous self-sharpening engine.
//
// Khora does not wait to be taught. The Whetstone holds a set of
// Faculties (trainable cognitive capabilities). Each round it surveys
// its own competence, selects the faculty with the most room to grow,
// generates a challenge at that faculty's current frontier, and attempts
// it. The outcome drives one of two responses, and never a third called
// "failure":
//
//   - competence >= mastery  -> ESCALATE: raise that faculty's difficulty.
//                               What was hard is now easy; reach further.
//   - competence <  mastery  -> EVOLVE: change the faculty's own method
//                               or parameters and try again. Struggle is
//                               the signal to become something that can.
//
// The engine logs a competence-over-time trajectory per faculty: a real
// record of Khora teaching itself, escalating what it has mastered and
// reforging what it has not.

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace khora::whetstone {

struct AttemptOutcome {
    double      score = 0.0;    // 0..1 competence on this challenge
    std::string detail;
};

// A trainable cognitive faculty. Implementations own whatever substrate
// they need (a crucible, a cortex, etc.).
class Faculty {
public:
    virtual ~Faculty() = default;

    virtual std::string name() const = 0;

    // Generate a challenge at the given difficulty, run Khora against it,
    // and return the competence score.
    virtual AttemptOutcome attempt(int difficulty) = 0;

    // Attempt to improve this faculty's method/parameters. Returns true
    // if a change was actually applied (false if already maxed out).
    virtual bool evolve() = 0;

    // Undo the most recent evolve(). The engine calls this when a measured
    // evolution turned out to worsen competence — natural selection:
    // beneficial mutations are kept, harmful ones discarded.
    virtual bool revert() = 0;

    // For reporting.
    virtual int evolution_level() const = 0;
    virtual int max_difficulty()  const { return 1'000'000; }
};

struct FacultyState {
    int         difficulty   = 1;
    double      competence   = 0.0;   // last score
    double      best         = 0.0;   // best score ever at current difficulty
    int         plateau      = 0;     // consecutive evolves without mastery
    std::size_t rounds       = 0;
    bool        maxed        = false; // evolve() returned false and still short
};

struct WhetstoneStep {
    std::size_t round;
    std::string faculty;
    int         difficulty;
    double      score;
    bool        escalated;
    bool        evolved;
    std::string note;
};

class Whetstone {
public:
    explicit Whetstone(double mastery = 0.90) : mastery_(mastery) {}

    void add_faculty(std::unique_ptr<Faculty> f);

    std::size_t faculty_count() const { return faculties_.size(); }
    const FacultyState& state(std::size_t i) const { return states_[i]; }
    const Faculty&      faculty(std::size_t i) const { return *faculties_[i]; }

    // One autonomous round. Returns what happened.
    WhetstoneStep step();

    // Run N rounds.
    std::vector<WhetstoneStep> run(std::size_t rounds);

    double mastery() const { return mastery_; }

private:
    // Select the faculty most worth practising right now.
    std::size_t select_();

    double                                 mastery_;
    std::vector<std::unique_ptr<Faculty>>  faculties_;
    std::vector<FacultyState>              states_;
    std::size_t                            round_ = 0;
};

// --- Concrete faculties (defined in the library) ---

// Relational holographic capacity: difficulty = #records packed into one
// glyph; competence = field-recovery accuracy; evolution = encoding
// redundancy. Built on the Crucible.
std::unique_ptr<Faculty> make_relational_faculty(std::uint64_t seed = 0xBEAC04ULL);

// Sequence induction: difficulty = period of a repeating symbol sequence;
// competence = cortex continuation accuracy; evolution = context window.
std::unique_ptr<Faculty> make_sequence_faculty(std::uint64_t seed = 0x5EE5EEDULL);

} // namespace khora::whetstone
