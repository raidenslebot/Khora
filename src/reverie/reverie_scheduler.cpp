#include "khora/reverie/reverie_scheduler.hpp"

namespace khora::reverie {

ReverieScheduler::ReverieScheduler(ReverieLoom& loom, std::shared_mutex& mu)
    : loom_(loom), shared_mu_(mu) {}

ReverieScheduler::~ReverieScheduler() {
    stop();
}

void ReverieScheduler::start(std::chrono::milliseconds period) {
    if (running_.exchange(true)) return;  // already running
    period_ = period;
    cycles_.store(0);
    thread_ = std::thread([this] { thread_main(); });
}

void ReverieScheduler::stop() {
    if (!running_.exchange(false)) return;  // already stopped
    {
        std::lock_guard<std::mutex> lk(wake_mu_);
    }
    wake_cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void ReverieScheduler::thread_main() {
    while (running_.load(std::memory_order_acquire)) {
        {
            // Hold the shared mutex (in unique mode) while we mutate the
            // loom's internal dream lattice and read from the operator's
            // memory. This is brief — a single dream cycle.
            std::unique_lock<std::shared_mutex> lk(shared_mu_);
            loom_.dream_once();
        }
        cycles_.fetch_add(1, std::memory_order_relaxed);

        // Interruptible sleep so stop() can wake us promptly.
        std::unique_lock<std::mutex> lk(wake_mu_);
        wake_cv_.wait_for(lk, period_, [this] {
            return !running_.load(std::memory_order_acquire);
        });
    }
}

} // namespace khora::reverie
