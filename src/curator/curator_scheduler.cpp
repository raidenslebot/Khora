#include "khora/curator/curator_scheduler.hpp"

namespace khora::curator {

CuratorScheduler::CuratorScheduler(Curator& curator, std::shared_mutex& mu)
    : curator_(curator), shared_mu_(mu) {}

CuratorScheduler::~CuratorScheduler() { stop(); }

void CuratorScheduler::start(std::chrono::milliseconds period, std::size_t study_tokens) {
    if (running_.exchange(true)) return;
    period_       = period;
    study_tokens_ = study_tokens;
    thread_ = std::thread([this] { thread_main(); });
}

void CuratorScheduler::stop() {
    if (!running_.exchange(false)) return;
    wake_cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

std::string CuratorScheduler::last_account() const {
    std::lock_guard<std::mutex> lk(account_mu_);
    return last_account_;
}

void CuratorScheduler::thread_main() {
    while (running_.load(std::memory_order_acquire)) {
        std::string account;
        {
            // Each knowledge action mutates the live cognitive state; take
            // the shared lock for its duration so foreground work doesn't
            // race with it.
            std::unique_lock<std::shared_mutex> lk(shared_mu_);
            account = curator_.act(study_tokens_);
        }
        {
            std::lock_guard<std::mutex> lk(account_mu_);
            last_account_ = account;
        }
        actions_.fetch_add(1, std::memory_order_relaxed);

        std::unique_lock<std::mutex> lk(wake_mu_);
        wake_cv_.wait_for(lk, period_, [this] {
            return !running_.load(std::memory_order_acquire);
        });
    }
}

} // namespace khora::curator
