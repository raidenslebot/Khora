#include "khora/whetstone/whetstone_scheduler.hpp"

namespace khora::whetstone {

WhetstoneScheduler::WhetstoneScheduler(Whetstone& ws) : ws_(ws) {}

WhetstoneScheduler::~WhetstoneScheduler() { stop(); }

void WhetstoneScheduler::start(std::chrono::milliseconds period) {
    if (running_.exchange(true)) return;
    period_ = period;
    thread_ = std::thread([this] { thread_main(); });
}

void WhetstoneScheduler::stop() {
    if (!running_.exchange(false)) return;
    wake_cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

WhetstoneStep WhetstoneScheduler::last_step() const {
    std::lock_guard<std::mutex> lk(step_mu_);
    return last_step_;
}

void WhetstoneScheduler::thread_main() {
    while (running_.load(std::memory_order_acquire)) {
        WhetstoneStep s = ws_.step();
        {
            std::lock_guard<std::mutex> lk(step_mu_);
            last_step_ = s;
        }
        rounds_.fetch_add(1, std::memory_order_relaxed);

        std::unique_lock<std::mutex> lk(wake_mu_);
        wake_cv_.wait_for(lk, period_, [this] {
            return !running_.load(std::memory_order_acquire);
        });
    }
}

} // namespace khora::whetstone
