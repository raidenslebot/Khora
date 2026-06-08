#pragma once

// Soma Nexus — Khora's drive arbitrator.
//
// Maintains a small fixed set of competing intrinsic drives with
// homeostatic dynamics: each drive has a current strength in [0, 1],
// a setpoint to which it decays exponentially, and a per-second decay
// rate. External events stimulate drives up or down. Candidate actions
// are scored by an Affinity vector (per-drive scalar in [-1, +1]); the
// Nexus arbitrates by computing weighted valence = sum(strength * affinity).

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <utility>

namespace khora::soma {

enum class Drive : std::uint8_t {
    Curiosity,         // novelty-seeking; spike on unfamiliar input
    Preservation,      // self-maintenance; spike on errors or resource pressure
    Mastery,           // improve predictive competence; spike on resolved surprises
    Efficiency,        // conserve energy/time; spike on long/expensive ops
    OperatorAffinity,  // serve the operator; spike on direct operator interaction
    _Count
};

inline constexpr std::size_t kDriveCount = static_cast<std::size_t>(Drive::_Count);

const char* drive_name(Drive d) noexcept;

struct Affinity {
    std::array<double, kDriveCount> per_drive{};
};

class SomaNexus {
public:
    SomaNexus();

    SomaNexus(const SomaNexus&)            = delete;
    SomaNexus& operator=(const SomaNexus&) = delete;

    // Tuning (thread-safe)
    void set_setpoint(Drive d, double setpoint);
    void set_decay_rate(Drive d, double per_second);

    // Event API
    void stimulate(Drive d, double delta);   // adds delta, clamps to [0, 1]
    void reset_all();                         // back to all setpoints

    // Time step: exponential decay of every drive toward its setpoint
    void tick(std::chrono::milliseconds dt);

    // Query
    double strength(Drive d) const;
    std::array<double, kDriveCount> snapshot() const;

    // Arbitration
    double evaluate(const Affinity& a) const;
    // Returns {index, valence} of the best of the candidates;
    // if the span is empty returns {SIZE_MAX, 0.0}.
    std::pair<std::size_t, double> choose_best(std::span<const Affinity> candidates) const;

private:
    mutable std::mutex                          mu_;
    std::array<double, kDriveCount>             strengths_;
    std::array<double, kDriveCount>             setpoints_;
    std::array<double, kDriveCount>             decay_per_sec_;
};

} // namespace khora::soma
