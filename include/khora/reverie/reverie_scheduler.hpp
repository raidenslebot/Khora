#pragma once

// Reverie Scheduler — runs a ReverieLoom on a background thread so
// Khora dreams continuously while the operator interacts with the
// foreground shell. Uses an externally-owned shared_mutex to coordinate
// with the main thread (operator tools take a unique lock; the
// scheduler also takes a unique lock during each dream_once call so
// memory mutations stay consistent).

#include "khora/reverie/reverie_loom.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <shared_mutex>
#include <thread>

namespace khora::reverie {

class ReverieScheduler {
public:
    // The mutex must outlive the scheduler.
    ReverieScheduler(ReverieLoom& loom, std::shared_mutex& mu);
    ~ReverieScheduler();

    ReverieScheduler(const ReverieScheduler&)            = delete;
    ReverieScheduler& operator=(const ReverieScheduler&) = delete;

    // Start the background thread with the given inter-cycle sleep.
    // Idempotent — calling start while already running does nothing.
    void start(std::chrono::milliseconds period);

    // Stop and join the background thread. Idempotent.
    void stop();

    bool        is_running() const noexcept { return running_.load(); }
    std::size_t cycles_run() const noexcept { return cycles_.load(); }

private:
    void thread_main();

    ReverieLoom&                       loom_;
    std::shared_mutex&                 shared_mu_;
    std::thread                        thread_;
    std::atomic<bool>                  running_{false};
    std::atomic<std::size_t>           cycles_{0};
    std::chrono::milliseconds          period_{100};

    // For sleeping in a way that can be interrupted by stop().
    std::mutex                         wake_mu_;
    std::condition_variable            wake_cv_;
};

} // namespace khora::reverie
