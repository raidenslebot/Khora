#include "khora/volition/volition_scheduler.hpp"

namespace khora::volition {

VolitionScheduler::VolitionScheduler(Volition& will, khora::soma::SomaNexus& soma,
                                     std::shared_mutex& mu)
    : will_(will), soma_(soma), shared_mu_(mu) {}

VolitionScheduler::~VolitionScheduler() { stop(); }

void VolitionScheduler::start(std::chrono::milliseconds period) {
    if (running_.exchange(true)) return;
    period_ = period;
    thread_ = std::thread([this] { thread_main(); });
}

void VolitionScheduler::stop() {
    if (!running_.exchange(false)) return;
    wake_cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

std::string VolitionScheduler::last_act() const {
    std::lock_guard<std::mutex> lk(act_mu_);
    return last_act_;
}

void VolitionScheduler::thread_main() {
    while (running_.load(std::memory_order_acquire)) {
        std::string note;
        {
            // One self-directed act, then let the drives evolve a little so
            // the next beat can rotate. Taken under the shared lock so it
            // never races foreground cognition.
            std::unique_lock<std::shared_mutex> lk(shared_mu_);
            note = will_.act();
            soma_.tick(std::chrono::milliseconds(400));
        }
        {
            std::lock_guard<std::mutex> lk(act_mu_);
            last_act_ = note;
        }
        beats_.fetch_add(1, std::memory_order_relaxed);

        std::unique_lock<std::mutex> lk(wake_mu_);
        wake_cv_.wait_for(lk, period_, [this] {
            return !running_.load(std::memory_order_acquire);
        });
    }
}

} // namespace khora::volition
