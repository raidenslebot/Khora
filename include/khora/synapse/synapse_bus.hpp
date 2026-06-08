#pragma once

// Synapse Bus — Khora's typed async messaging fabric.
//
// Subsystems publish Pulse objects on string topics; other subsystems
// subscribe and poll. Each subscriber has its own bounded queue; on
// overflow the oldest pulse is dropped and counted. Thread-safe:
// many publishers, many subscribers, any combination.

#include "khora/lattice/glyph.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace khora::synapse {

using Topic  = std::string;
using Handle = std::uint64_t;

struct Pulse {
    Topic                                  topic;
    khora::lattice::Glyph                  payload;
    std::uint64_t                          sequence;
    std::chrono::steady_clock::time_point  timestamp;
};

class SynapseBus {
public:
    SynapseBus();
    ~SynapseBus();

    SynapseBus(const SynapseBus&)            = delete;
    SynapseBus& operator=(const SynapseBus&) = delete;

    // Publish one pulse on the given topic. Returns the global sequence
    // number assigned to it. Non-blocking; delivery to slow subscribers
    // drops the oldest queued pulse on their side.
    //
    // Ordering guarantees:
    //  - Sequences are globally unique and monotonic at the moment of
    //    publish (atomic fetch_add).
    //  - For two pulses published *by the same thread*, the subscriber
    //    observes them in publish order — guaranteed.
    //  - For pulses published by *different threads*, subscribers may
    //    observe them in any interleaving. Sequence numbers tell you
    //    the original publish order if you need to reconstruct it.
    //  - Delivery to all subscribers of a given topic happens before
    //    publish() returns, so a subscriber that polls immediately
    //    after a (same-thread) publish will see the new pulse.
    std::uint64_t publish(Topic topic, khora::lattice::Glyph payload);

    // Subscribe to a topic. Returns a handle to poll/unsubscribe.
    Handle subscribe(Topic topic, std::size_t capacity = 1024);

    // Stop delivery to the handle. Any blocked poll() returns nullopt.
    void unsubscribe(Handle h);

    // Blocking pop with timeout. Returns nullopt on timeout or after unsubscribe.
    std::optional<Pulse> poll(Handle h, std::chrono::milliseconds timeout);

    // Non-blocking pop. nullopt if queue empty.
    std::optional<Pulse> try_pop(Handle h);

    // Stats.
    std::uint64_t total_published()   const noexcept;
    std::uint64_t total_dropped()     const noexcept;
    std::size_t   subscriber_count(const Topic& topic) const;
    std::uint64_t dropped_for(Handle h) const;

private:
    struct Subscriber {
        Topic                       topic;
        std::size_t                 capacity;
        mutable std::mutex          mu;
        std::condition_variable     cv;
        std::deque<Pulse>           queue;
        std::uint64_t               dropped = 0;
        std::atomic<bool>           alive{true};
    };

    mutable std::shared_mutex                                topology_mu_;
    std::unordered_map<Topic, std::unordered_set<Handle>>     topic_handles_;
    std::unordered_map<Handle, std::shared_ptr<Subscriber>>   subscribers_;

    std::atomic<Handle>         next_handle_{1};
    std::atomic<std::uint64_t>  next_sequence_{1};
    std::atomic<std::uint64_t>  total_published_{0};
    std::atomic<std::uint64_t>  total_dropped_{0};
};

} // namespace khora::synapse
