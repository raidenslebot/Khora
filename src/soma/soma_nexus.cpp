#include "khora/soma/soma_nexus.hpp"

#include <algorithm>
#include <climits>
#include <limits>

namespace khora::soma {

namespace {
inline double clamp01(double x) noexcept {
    return std::min(1.0, std::max(0.0, x));
}
constexpr std::size_t idx(Drive d) noexcept {
    return static_cast<std::size_t>(d);
}
} // namespace

const char* drive_name(Drive d) noexcept {
    switch (d) {
        case Drive::Curiosity:         return "Curiosity";
        case Drive::Preservation:      return "Preservation";
        case Drive::Mastery:           return "Mastery";
        case Drive::Efficiency:        return "Efficiency";
        case Drive::OperatorAffinity:  return "OperatorAffinity";
        default:                       return "?";
    }
}

SomaNexus::SomaNexus() {
    // Defaults capture an initial "personality" — moderately curious,
    // cautious, eager to help operator, willing to spend some energy.
    setpoints_[idx(Drive::Curiosity)]        = 0.60;
    setpoints_[idx(Drive::Preservation)]     = 0.70;
    setpoints_[idx(Drive::Mastery)]          = 0.50;
    setpoints_[idx(Drive::Efficiency)]       = 0.40;
    setpoints_[idx(Drive::OperatorAffinity)] = 0.80;

    // Decay rates per second — bigger = faster return to setpoint.
    for (auto& r : decay_per_sec_) r = 0.5;  // ~2-second half-life toward setpoint

    strengths_ = setpoints_;
}

void SomaNexus::set_setpoint(Drive d, double setpoint) {
    std::lock_guard<std::mutex> lk(mu_);
    setpoints_[idx(d)] = clamp01(setpoint);
}

void SomaNexus::set_decay_rate(Drive d, double per_second) {
    std::lock_guard<std::mutex> lk(mu_);
    decay_per_sec_[idx(d)] = std::max(0.0, per_second);
}

void SomaNexus::stimulate(Drive d, double delta) {
    std::lock_guard<std::mutex> lk(mu_);
    strengths_[idx(d)] = clamp01(strengths_[idx(d)] + delta);
}

void SomaNexus::reset_all() {
    std::lock_guard<std::mutex> lk(mu_);
    strengths_ = setpoints_;
}

void SomaNexus::tick(std::chrono::milliseconds dt) {
    std::lock_guard<std::mutex> lk(mu_);
    const double dt_s = std::chrono::duration<double>(dt).count();
    for (std::size_t i = 0; i < kDriveCount; ++i) {
        // Exponential decay toward setpoint:
        //   strength <- strength + rate * dt * (setpoint - strength)
        // Clamp the decay coefficient to avoid overshoot at large dt.
        const double coeff = std::min(1.0, decay_per_sec_[i] * dt_s);
        strengths_[i] += coeff * (setpoints_[i] - strengths_[i]);
    }
}

double SomaNexus::strength(Drive d) const {
    std::lock_guard<std::mutex> lk(mu_);
    return strengths_[idx(d)];
}

std::array<double, kDriveCount> SomaNexus::snapshot() const {
    std::lock_guard<std::mutex> lk(mu_);
    return strengths_;
}

double SomaNexus::evaluate(const Affinity& a) const {
    std::lock_guard<std::mutex> lk(mu_);
    double sum = 0.0;
    for (std::size_t i = 0; i < kDriveCount; ++i) {
        sum += strengths_[i] * a.per_drive[i];
    }
    return sum;
}

std::pair<std::size_t, double> SomaNexus::choose_best(std::span<const Affinity> candidates) const {
    if (candidates.empty()) {
        return {std::numeric_limits<std::size_t>::max(), 0.0};
    }
    // Snapshot strengths once so we evaluate consistently across candidates.
    std::array<double, kDriveCount> s;
    {
        std::lock_guard<std::mutex> lk(mu_);
        s = strengths_;
    }
    std::size_t best_idx = 0;
    double      best_val = -std::numeric_limits<double>::infinity();
    for (std::size_t k = 0; k < candidates.size(); ++k) {
        double v = 0.0;
        for (std::size_t i = 0; i < kDriveCount; ++i) {
            v += s[i] * candidates[k].per_drive[i];
        }
        if (v > best_val) {
            best_val = v;
            best_idx = k;
        }
    }
    return {best_idx, best_val};
}

} // namespace khora::soma
