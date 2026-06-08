#pragma once

// VolitionScheduler — runs the Volition on a background thread so Khora is
// ALWAYS acting on its own drives: reflecting, learning, dreaming, with no
// operator prompting. This is the "zero downtime, never stop" of the
// directive made real — a mind that keeps moving on its own.
//
// Each act mutates the live cognitive state (the same Lexicon / Cortex /
// memory the Cogitator and operator touch), so every beat is taken under the
// externally-owned shared_mutex — the same lock the REPL's locked_dispatch
// and the other schedulers use, so nothing races. Opt-in, paced by default.

#include "khora/soma/soma_nexus.hpp"
#include "khora/volition/volition.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>

namespace khora::volition {

class VolitionScheduler {
public:
    VolitionScheduler(Volition& will, khora::soma::SomaNexus& soma, std::shared_mutex& mu);
    ~VolitionScheduler();

    VolitionScheduler(const VolitionScheduler&)            = delete;
    VolitionScheduler& operator=(const VolitionScheduler&) = delete;

    void start(std::chrono::milliseconds period);
    void stop();

    bool        is_running() const noexcept { return running_.load(); }
    std::size_t beats()      const noexcept { return beats_.load(); }
    std::string last_act() const;

private:
    void thread_main();

    Volition&                  will_;
    khora::soma::SomaNexus&     soma_;
    std::shared_mutex&         shared_mu_;
    std::thread                thread_;
    std::atomic<bool>          running_{false};
    std::atomic<std::size_t>   beats_{0};
    std::chrono::milliseconds  period_{5000};

    mutable std::mutex         act_mu_;
    std::string                last_act_;

    std::mutex                 wake_mu_;
    std::condition_variable    wake_cv_;
};

} // namespace khora::volition
