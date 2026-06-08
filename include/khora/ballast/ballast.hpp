#pragma once

// The Ballast — Khora's memory governor.
//
// Khora lives inside a machine the operator also uses. It is hard-capped
// at a fixed slice of system RAM (default 4 GB) and must additionally back
// off hard whenever TOTAL system memory pressure crosses a threshold
// (default 90%) — the operator's work must never be starved, the machine
// must never lock up. GPU memory and NVMe are not governed here; they are
// used freely elsewhere. This watches only the one scarce, shared
// resource: system RAM.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace khora::ballast {

struct MemoryStatus {
    std::uint64_t khora_rss_mb     = 0;   // Khora's own working set
    std::uint64_t system_total_mb  = 0;
    std::uint64_t system_avail_mb  = 0;
    double        system_used_frac = 0.0; // 0..1
};

enum class Pressure {
    Normal,          // comfortable
    ApproachingCap,  // Khora is nearing its own RAM cap
    OverCap,         // Khora has exceeded its RAM cap — shed load now
    SystemPressure   // total system RAM is critically high — shed load now
};

const char* pressure_name(Pressure p) noexcept;

class Ballast {
public:
    explicit Ballast(std::uint64_t khora_cap_mb = 4096,
                     double system_pressure_frac = 0.90)
        : khora_cap_mb_(khora_cap_mb), system_pressure_frac_(system_pressure_frac) {}

    MemoryStatus sample() const;
    Pressure     verdict() const;
    Pressure     verdict(const MemoryStatus& s) const;

    // True if Khora should actively shed memory right now.
    bool must_shed() const;

    std::uint64_t cap_mb()      const noexcept { return khora_cap_mb_; }
    double        pressure_frac() const noexcept { return system_pressure_frac_; }
    void set_cap_mb(std::uint64_t mb)        { khora_cap_mb_ = mb; }
    void set_pressure_frac(double f)         { system_pressure_frac_ = f; }

    std::string summary() const;

private:
    std::uint64_t khora_cap_mb_;
    double        system_pressure_frac_;
};

// Runs the Ballast on a background thread, invoking a callback each tick
// with the current status and verdict so the runtime can shed load (prune
// memory, pause background learning) the moment pressure appears.
class BallastGovernor {
public:
    using TickFn = std::function<void(const MemoryStatus&, Pressure)>;

    BallastGovernor(Ballast& ballast, TickFn on_tick)
        : ballast_(ballast), on_tick_(std::move(on_tick)) {}
    ~BallastGovernor() { stop(); }

    BallastGovernor(const BallastGovernor&)            = delete;
    BallastGovernor& operator=(const BallastGovernor&) = delete;

    void start(std::chrono::milliseconds period);
    void stop();

    bool      is_running() const noexcept { return running_.load(); }
    Pressure  last_verdict() const noexcept { return last_.load(); }
    std::uint64_t sheds() const noexcept { return sheds_.load(); }
    void note_shed() noexcept { sheds_.fetch_add(1, std::memory_order_relaxed); }

private:
    void thread_main();

    Ballast&                  ballast_;
    TickFn                    on_tick_;
    std::thread               thread_;
    std::atomic<bool>         running_{false};
    std::atomic<Pressure>     last_{Pressure::Normal};
    std::atomic<std::uint64_t> sheds_{0};
    std::chrono::milliseconds period_{1000};
    std::mutex                wake_mu_;
    std::condition_variable   wake_cv_;
};

} // namespace khora::ballast
