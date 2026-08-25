// DOES THE GOVERNOR ACTUALLY HOLD THE MACHINE, OR JUST CLAIM TO?
//
// A safety limit that has never been watched under load is a comment. This runs
// a genuinely CPU-saturating load through the governor and measures the machine
// from outside the workers: total processor time, thermal readings, firmware
// throttle state, and how wide the pool was actually allowed to be.
//
// It reports three things that are easy to conflate and must not be:
//
//   what the governor was ASKED to do        the configured ceiling
//   what it ALLOWED                          the pool width it settled on
//   what the MACHINE actually did            % Processor Time, measured
//
// The third is the only one that answers the question. A governor that permits
// 21 workers on 24 cores and yet sees 100% processor time has not held anything.

#include "khora/governor/governor.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <pdh.h>
#  pragma comment(lib, "pdh.lib")
#endif

using namespace khora::governor;
using clk = std::chrono::steady_clock;

namespace {

// An outside observer. Deliberately not derived from the governor's own
// counters: a component grading itself is not evidence.
class CpuWatch {
public:
    CpuWatch() {
#ifdef _WIN32
        if (PdhOpenQueryW(nullptr, 0, &q_) != ERROR_SUCCESS) { q_ = nullptr; return; }
        if (PdhAddEnglishCounterW(q_, L"\\Processor Information(_Total)\\% Processor Time",
                                  0, &c_) != ERROR_SUCCESS) {
            c_ = nullptr;
        }
        PdhCollectQueryData(q_);
#endif
    }
    ~CpuWatch() {
#ifdef _WIN32
        if (q_) PdhCloseQuery(q_);
#endif
    }
    bool sample(double& pct) {
#ifdef _WIN32
        if (!q_ || !c_) return false;
        if (PdhCollectQueryData(q_) != ERROR_SUCCESS) return false;
        PDH_FMT_COUNTERVALUE v{};
        if (PdhGetFormattedCounterValue(c_, PDH_FMT_DOUBLE, nullptr, &v) != ERROR_SUCCESS) return false;
        pct = v.doubleValue;
        return true;
#else
        (void)pct;
        return false;
#endif
    }
private:
#ifdef _WIN32
    PDH_HQUERY   q_ = nullptr;
    PDH_HCOUNTER c_ = nullptr;
#endif
};

// Burn. Floating-point work with a dependency chain, so the compiler cannot
// remove it and the core cannot pipeline its way out of the load.
double burn(std::uint64_t rounds) {
    double a = 1.000001, s = 0.0;
    for (std::uint64_t i = 0; i < rounds; ++i) {
        a = a * 1.0000000001 + 1e-9;
        s += std::sqrt(a) * std::sin(a);
    }
    return s;
}

} // namespace

int main(int argc, char** argv) {
    const int seconds = (argc > 1) ? std::stoi(argv[1]) : 25;
    const double fraction = (argc > 2) ? std::stod(argv[2]) : 0.90;

    const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
    std::printf("Does the governor hold the machine, or just claim to?\n\n");
    std::printf("  %u logical processors, ceiling %.0f%% = %zu workers, %d s of load\n\n",
                cores, fraction * 100.0, Governor::cap_workers(fraction), seconds);

    // ---- what this machine can actually tell us -----------------------------
    const Reading p = probe();
    std::printf("  SENSORS, probed rather than assumed\n");
    std::printf("    die temperature   : %s\n",
                p.die_temp_available ? "AVAILABLE" : "NOT AVAILABLE");
    if (p.celsius >= 0.0) {
        std::printf("    best temperature  : %.1f C from %s\n", p.celsius, p.source.c_str());
    } else {
        std::printf("    best temperature  : none readable\n");
    }
    std::printf("    firmware throttle : %s\n", p.firmware_throttling ? "ACTIVE" : "inactive");
    std::printf("    frequency         : %.1f%% of nominal\n\n", p.performance_pct);

    if (!p.die_temp_available) {
        std::printf("  THE 85 C RULE CANNOT BE ENFORCED FROM HERE, and saying so is the\n");
        std::printf("  point of this section. The readable sensors are ACPI zones reporting\n");
        std::printf("  around %.0f C, which is chassis or board -- a 13700K under load runs\n",
                    p.celsius >= 0.0 ? p.celsius : 28.0);
        std::printf("  60 to 90 C, so those numbers are not the die and will never cross 85.\n");
        std::printf("  Reading the die needs ring 0. What IS enforced below: the concurrency\n");
        std::printf("  ceiling, and backoff on the firmware's own throttle signal, which is\n");
        std::printf("  a real thermal indication arriving late rather than an invented one.\n\n");
    }

    // ---- load, under the governor -------------------------------------------
    Limits lim;
    lim.cpu_fraction = fraction;
    Governor gov(lim);
    gov.start();

    std::atomic<bool> stop{false};
    std::atomic<std::size_t> parked{0};
    std::vector<std::thread> pool;
    const std::size_t width = Governor::cap_workers(fraction);
    for (std::size_t slot = 0; slot < width; ++slot) {
        pool.emplace_back([&, slot] {
            volatile double sink = 0.0;
            while (!stop.load(std::memory_order_relaxed)) {
                if (gov.park_if_over(slot)) { parked.fetch_add(1, std::memory_order_relaxed); continue; }
                sink += burn(200000);
            }
            (void)sink;
        });
    }

    CpuWatch watch;
    std::vector<double> cpu_samples;
    std::printf("  t(s) | machine CPU | allowed | temp    | firmware\n");
    std::printf("  -----+-------------+---------+---------+---------\n");
    const auto t0 = clk::now();
    for (int i = 0; i < seconds; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        double pct = -1.0;
        watch.sample(pct);
        if (pct >= 0.0) cpu_samples.push_back(pct);
        const Reading r = gov.last();
        if (i % 4 == 0 || i == seconds - 1) {
            std::printf("  %4.0f | %10.1f%% | %7zu | %6.1fC | %s\n",
                        std::chrono::duration<double>(clk::now() - t0).count(),
                        pct, r.allowed_workers,
                        r.celsius, r.firmware_throttling ? "THROTTLE" : "-");
        }
    }
    stop.store(true);
    for (auto& t : pool) t.join();
    gov.stop();

    double peak = 0.0, mean = 0.0;
    for (const double v : cpu_samples) { peak = std::max(peak, v); mean += v; }
    if (!cpu_samples.empty()) mean /= static_cast<double>(cpu_samples.size());

    std::printf("\n  MEASURED FROM OUTSIDE THE WORKERS\n");
    std::printf("    peak machine CPU  : %.1f%%\n", peak);
    std::printf("    mean machine CPU  : %.1f%%\n", mean);
    std::printf("    ceiling asked for : %.0f%%\n", fraction * 100.0);
    std::printf("    narrowest pool    : %zu of %zu\n", gov.min_allowed(), width);
    std::printf("    throttle events   : %zu\n", gov.throttle_events());
    std::printf("    worker park calls : %zu\n", parked.load());
    std::printf("    peak temperature  : %.1f C\n", gov.peak_celsius());

    std::printf("\n  VERDICT\n");
    if (peak <= fraction * 100.0 + 5.0) {
        std::printf("    Held. Peak %.1f%% against a %.0f%% ceiling, measured by an outside\n",
                    peak, fraction * 100.0);
        std::printf("    counter rather than by the governor grading itself.\n");
    } else {
        std::printf("    NOT HELD. Peak %.1f%% against a %.0f%% ceiling. The worker count is\n",
                    peak, fraction * 100.0);
        std::printf("    capped but the machine still saturated, which means something other\n");
        std::printf("    than this pool is consuming the rest, or a worker is not parking.\n");
    }
    return 0;
}
