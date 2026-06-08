#include "khora/ballast/ballast.hpp"

#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

namespace khora::ballast {

const char* pressure_name(Pressure p) noexcept {
    switch (p) {
        case Pressure::Normal:         return "normal";
        case Pressure::ApproachingCap: return "approaching-cap";
        case Pressure::OverCap:        return "over-cap";
        case Pressure::SystemPressure: return "system-pressure";
        default:                       return "?";
    }
}

MemoryStatus Ballast::sample() const {
    MemoryStatus s;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        s.khora_rss_mb = pmc.WorkingSetSize / (1024 * 1024);
    }
    MEMORYSTATUSEX ms; ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        s.system_total_mb  = ms.ullTotalPhys / (1024 * 1024);
        s.system_avail_mb  = ms.ullAvailPhys / (1024 * 1024);
        s.system_used_frac = (ms.ullTotalPhys > 0)
            ? 1.0 - static_cast<double>(ms.ullAvailPhys) / static_cast<double>(ms.ullTotalPhys)
            : 0.0;
    }
#endif
    return s;
}

Pressure Ballast::verdict(const MemoryStatus& s) const {
    // System-wide pressure is the gravest concern: the machine must not
    // lock up, so it takes precedence over Khora's own budget.
    if (s.system_used_frac >= system_pressure_frac_) return Pressure::SystemPressure;
    if (s.khora_rss_mb >= khora_cap_mb_)             return Pressure::OverCap;
    if (s.khora_rss_mb >= (khora_cap_mb_ * 85) / 100) return Pressure::ApproachingCap;
    return Pressure::Normal;
}

Pressure Ballast::verdict() const { return verdict(sample()); }

bool Ballast::must_shed() const {
    const Pressure p = verdict();
    return p == Pressure::OverCap || p == Pressure::SystemPressure;
}

std::string Ballast::summary() const {
    const auto s = sample();
    const auto p = verdict(s);
    std::ostringstream os;
    os << "Ballast (memory governor):\n"
       << "  Khora working set : " << s.khora_rss_mb << " / " << khora_cap_mb_ << " MB cap\n"
       << "  system RAM        : " << (s.system_total_mb - s.system_avail_mb) << " / "
       << s.system_total_mb << " MB used ("
       << static_cast<int>(s.system_used_frac * 100.0) << "%)\n"
       << "  system avail      : " << s.system_avail_mb << " MB\n"
       << "  pressure threshold: " << static_cast<int>(system_pressure_frac_ * 100.0) << "%\n"
       << "  verdict           : " << pressure_name(p);
    return os.str();
}

void BallastGovernor::start(std::chrono::milliseconds period) {
    if (running_.exchange(true)) return;
    period_ = period;
    thread_ = std::thread([this] { thread_main(); });
}

void BallastGovernor::stop() {
    if (!running_.exchange(false)) return;
    wake_cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void BallastGovernor::thread_main() {
    while (running_.load(std::memory_order_acquire)) {
        const MemoryStatus s = ballast_.sample();
        const Pressure p = ballast_.verdict(s);
        last_.store(p, std::memory_order_relaxed);
        if (on_tick_) on_tick_(s, p);

        std::unique_lock<std::mutex> lk(wake_mu_);
        wake_cv_.wait_for(lk, period_, [this] {
            return !running_.load(std::memory_order_acquire);
        });
    }
}

} // namespace khora::ballast
