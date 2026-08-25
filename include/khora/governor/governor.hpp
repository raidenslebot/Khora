#pragma once

// Governor — how much of the machine this process is allowed to use, right now.
//
// The requirement is to abuse the CPU without cooking it: run hot, up to 90% of
// the cores, and never let the die pass 85 C. Those two are in tension only if
// the load is fixed, so the load is not fixed -- it is a control loop.
//
// WHAT THIS MACHINE ACTUALLY EXPOSES, probed rather than assumed, because a
// safety system built on a sensor that reads the wrong thing is worse than no
// safety system at all:
//
//   MSAcpi_ThermalZoneTemperature   TZ00 27.9 C, TZ10 16.9 C
//   Thermal Zone Information        the same two zones, in Kelvin
//   Throttle Reasons                per zone, non-zero when the firmware is
//                                   already limiting
//   % Processor Performance         frequency against nominal
//
// THOSE ACPI ZONES ARE NOT THE CPU DIE. An i7-13700K under load sits between 60
// and 90 C; a sensor reading 17 C is chassis or board. A governor watching it
// would never throttle and would report that everything is fine while the die
// cooked. That is exactly the shape of defect this project keeps finding in its
// own work -- a value carried in one place and meaning something else -- so the
// die temperature is treated as UNAVAILABLE unless a source that really reports
// it is found, and every report says which sensor is live.
//
// Reading the 13700K's digital thermal sensor needs ring 0. LibreHardwareMonitor
// and HWiNFO ship the driver for it and publish over WMI; neither is installed
// here and installing software is not mine to decide. `probe()` looks for them
// every time it starts, so the moment one exists this becomes a true 85 C limit
// with no code change.
//
// WHAT IT DOES IN THE MEANTIME, stated so nobody mistakes it for the full thing:
//
//   - caps concurrency at a fraction of the cores, so the machine stays usable
//   - reacts to Throttle Reasons, which is the FIRMWARE saying it is already
//     thermally limited -- a real thermal signal, just a late one
//   - reacts to sustained frequency drop, which is what throttling looks like
//     from user space
//   - reacts to any temperature it can read, against the same 85 C limit, so a
//     board sensor that does climb still pulls the load down
//
// THE CONTROL LAW is additive-increase, multiplicative-decrease. Back off fast
// when hot, recover slowly when cool. It is the law TCP uses on congestion for
// the same reason: overshooting the limit is expensive and undershooting it only
// costs time. Hysteresis between the throttle and resume points stops it
// oscillating around the boundary.

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <thread>

namespace khora::governor {

struct Reading {
    bool        die_temp_available = false;   // a sensor that really reads the die
    double      celsius            = -1.0;    // best temperature available, -1 if none
    std::string source             = "none";  // which sensor produced it
    bool        firmware_throttling = false;  // Throttle Reasons non-zero
    double      performance_pct    = 100.0;   // frequency against nominal
    // TOTAL processor time across the machine, which is the quantity a CPU
    // ceiling is actually about. Capping this process's threads is open-loop:
    // measured, 21 workers on 24 cores still let the machine reach 94.7%,
    // because everything else running makes up the difference.
    double      machine_cpu_pct    = -1.0;
    std::size_t allowed_workers    = 0;       // what the governor permits now
};

struct Limits {
    // Ceiling on concurrency as a fraction of logical processors. 0.90 leaves
    // the machine responsive while using nearly all of it.
    double      cpu_fraction   = 0.90;

    // Hard temperature limit, and the point at which load is allowed back up.
    // The gap is deliberate: throttling at 85 and resuming at 85 would oscillate
    // every sample.
    double      throttle_c     = 85.0;
    double      resume_c       = 78.0;

    // Never park every worker -- a governor that can stall the work entirely is
    // a deadlock with a thermometer.
    std::size_t min_workers    = 1;

    // Aim this far below the ceiling. Between samples the load is unobserved, so
    // a loop aiming exactly at the limit crosses it whenever the work gets
    // cheaper -- measured at 92.5% peak against a 90% ceiling with no margin.
    double      headroom_pct   = 4.0;

    // Faster sampling shortens the window in which an overshoot can happen. Half
    // a second costs one counter read and buys a proportionally smaller breach.
    unsigned    sample_ms      = 500;

    // Multiplicative decrease and additive increase.
    double      backoff        = 0.75;   // keep this share when hot
    std::size_t recover_step   = 1;      // add this many when cool
};

class Governor {
public:
    explicit Governor(Limits limits = {});
    ~Governor();

    Governor(const Governor&) = delete;
    Governor& operator=(const Governor&) = delete;

    // Begin sampling. Safe to call once; further calls are ignored.
    void start();
    void stop();

    // How many workers may run right now. Cheap enough to call in a worker loop:
    // a relaxed atomic load, no syscall, no lock.
    std::size_t allowed() const noexcept {
        return allowed_.load(std::memory_order_relaxed);
    }

    // A worker whose slot is beyond the allowance parks rather than exits, so
    // the pool can grow back without respawning threads. Returns true if it
    // waited.
    bool park_if_over(std::size_t slot) const;

    Reading last() const;

    // Highest temperature and lowest allowance seen, for the end-of-run report.
    // A run that never throttled and a run that throttled constantly should not
    // look the same afterwards.
    double      peak_celsius() const noexcept;
    std::size_t min_allowed() const noexcept;
    std::size_t throttle_events() const noexcept;

    // The ceiling, ignoring thermal state.
    static std::size_t cap_workers(double fraction);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<std::size_t> allowed_{0};
};

// One-shot probe, for reporting what is available before a run starts.
Reading probe();

} // namespace khora::governor
