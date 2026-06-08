#pragma once

// CuratorScheduler — runs the Curator on a background thread so Khora
// educates itself continuously: foraging, studying, and seeking the next
// most valuable knowledge while it runs, with no operator prompting.
//
// Studies mutate the live Lexicon + Cortex (the same state the Cogitator
// and operator touch), so each action is taken under an externally-owned
// shared_mutex. A background study briefly occupies that lock, so this is
// opt-in and paced slowly by default; the operator can pause it any time.

#include "khora/curator/curator.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>

namespace khora::curator {

class CuratorScheduler {
public:
    CuratorScheduler(Curator& curator, std::shared_mutex& mu);
    ~CuratorScheduler();

    CuratorScheduler(const CuratorScheduler&)            = delete;
    CuratorScheduler& operator=(const CuratorScheduler&) = delete;

    void start(std::chrono::milliseconds period, std::size_t study_tokens = 60000);
    void stop();

    bool        is_running() const noexcept { return running_.load(); }
    std::size_t actions()    const noexcept { return actions_.load(); }
    std::string last_account() const;

private:
    void thread_main();

    Curator&                   curator_;
    std::shared_mutex&         shared_mu_;
    std::thread                thread_;
    std::atomic<bool>          running_{false};
    std::atomic<std::size_t>   actions_{0};
    std::chrono::milliseconds  period_{120000};
    std::size_t                study_tokens_ = 60000;

    mutable std::mutex         account_mu_;
    std::string                last_account_;

    std::mutex                 wake_mu_;
    std::condition_variable    wake_cv_;
};

} // namespace khora::curator
