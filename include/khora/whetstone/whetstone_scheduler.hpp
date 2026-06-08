#pragma once

// WhetstoneScheduler — runs the autonomous self-sharpening engine on a
// background thread, so Khora is training and evolving itself the moment
// it is launched and for as long as it lives. The operator's foreground
// work and Khora's background self-improvement proceed at once.

#include "khora/whetstone/whetstone.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>
#include <vector>

namespace khora::whetstone {

class WhetstoneScheduler {
public:
    explicit WhetstoneScheduler(Whetstone& ws);
    ~WhetstoneScheduler();

    WhetstoneScheduler(const WhetstoneScheduler&)            = delete;
    WhetstoneScheduler& operator=(const WhetstoneScheduler&) = delete;

    void start(std::chrono::milliseconds period);
    void stop();

    bool        is_running() const noexcept { return running_.load(); }
    std::size_t rounds_run() const noexcept { return rounds_.load(); }

    // Snapshot of the most recent step (thread-safe copy).
    WhetstoneStep last_step() const;

private:
    void thread_main();

    Whetstone&                 ws_;
    std::thread                thread_;
    std::atomic<bool>          running_{false};
    std::atomic<std::size_t>   rounds_{0};
    std::chrono::milliseconds  period_{250};

    mutable std::mutex         step_mu_;
    WhetstoneStep              last_step_{};

    std::mutex                 wake_mu_;
    std::condition_variable    wake_cv_;
};

} // namespace khora::whetstone
