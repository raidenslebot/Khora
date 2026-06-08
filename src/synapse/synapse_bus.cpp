#include "khora/synapse/synapse_bus.hpp"

#include <utility>

namespace khora::synapse {

SynapseBus::SynapseBus()  = default;
SynapseBus::~SynapseBus() = default;

std::uint64_t SynapseBus::publish(Topic topic, khora::lattice::Glyph payload) {
    const auto seq = next_sequence_.fetch_add(1, std::memory_order_relaxed);
    const auto ts  = std::chrono::steady_clock::now();

    // Snapshot subscribers under shared lock so the topology can mutate
    // freely after we release. We hold strong refs so the Subscriber
    // can't be destroyed mid-delivery.
    std::vector<std::shared_ptr<Subscriber>> targets;
    {
        std::shared_lock<std::shared_mutex> lock(topology_mu_);
        auto it = topic_handles_.find(topic);
        if (it != topic_handles_.end()) {
            targets.reserve(it->second.size());
            for (Handle h : it->second) {
                auto sub_it = subscribers_.find(h);
                if (sub_it != subscribers_.end()) {
                    targets.push_back(sub_it->second);
                }
            }
        }
    }

    for (auto& s : targets) {
        std::unique_lock<std::mutex> lk(s->mu);
        if (s->queue.size() >= s->capacity) {
            s->queue.pop_front();
            ++s->dropped;
            total_dropped_.fetch_add(1, std::memory_order_relaxed);
        }
        s->queue.push_back(Pulse{topic, payload, seq, ts});
        lk.unlock();
        s->cv.notify_one();
    }

    total_published_.fetch_add(1, std::memory_order_relaxed);
    return seq;
}

Handle SynapseBus::subscribe(Topic topic, std::size_t capacity) {
    const Handle h = next_handle_.fetch_add(1, std::memory_order_relaxed);

    auto sub = std::make_shared<Subscriber>();
    sub->topic    = topic;
    sub->capacity = (capacity == 0) ? 1 : capacity;

    std::unique_lock<std::shared_mutex> lock(topology_mu_);
    topic_handles_[topic].insert(h);
    subscribers_[h] = std::move(sub);
    return h;
}

void SynapseBus::unsubscribe(Handle h) {
    std::shared_ptr<Subscriber> dead;
    {
        std::unique_lock<std::shared_mutex> lock(topology_mu_);
        auto it = subscribers_.find(h);
        if (it == subscribers_.end()) return;
        auto th = topic_handles_.find(it->second->topic);
        if (th != topic_handles_.end()) {
            th->second.erase(h);
            if (th->second.empty()) topic_handles_.erase(th);
        }
        dead = it->second;
        subscribers_.erase(it);
    }
    {
        // Mark dead under the subscriber's own mutex so any poller waking
        // up sees a consistent (queue, alive) snapshot.
        std::lock_guard<std::mutex> lk(dead->mu);
        dead->alive.store(false, std::memory_order_release);
    }
    dead->cv.notify_all();
    // `dead` drops here. Any blocked poller holds its own shared_ptr
    // copy via poll(); the Subscriber survives until they're done.
}

std::optional<Pulse> SynapseBus::poll(Handle h, std::chrono::milliseconds timeout) {
    std::shared_ptr<Subscriber> s;
    {
        std::shared_lock<std::shared_mutex> lock(topology_mu_);
        auto it = subscribers_.find(h);
        if (it == subscribers_.end()) return std::nullopt;
        s = it->second;
    }

    std::unique_lock<std::mutex> lk(s->mu);
    const bool got = s->cv.wait_for(lk, timeout, [&s] {
        return !s->queue.empty() || !s->alive.load(std::memory_order_acquire);
    });
    if (!got) return std::nullopt;       // timed out
    if (s->queue.empty()) return std::nullopt; // dead and drained

    Pulse p = std::move(s->queue.front());
    s->queue.pop_front();
    return p;
}

std::optional<Pulse> SynapseBus::try_pop(Handle h) {
    std::shared_ptr<Subscriber> s;
    {
        std::shared_lock<std::shared_mutex> lock(topology_mu_);
        auto it = subscribers_.find(h);
        if (it == subscribers_.end()) return std::nullopt;
        s = it->second;
    }
    std::lock_guard<std::mutex> lk(s->mu);
    if (s->queue.empty()) return std::nullopt;
    Pulse p = std::move(s->queue.front());
    s->queue.pop_front();
    return p;
}

std::uint64_t SynapseBus::total_published() const noexcept {
    return total_published_.load(std::memory_order_relaxed);
}
std::uint64_t SynapseBus::total_dropped() const noexcept {
    return total_dropped_.load(std::memory_order_relaxed);
}

std::size_t SynapseBus::subscriber_count(const Topic& topic) const {
    std::shared_lock<std::shared_mutex> lock(topology_mu_);
    auto it = topic_handles_.find(topic);
    return (it == topic_handles_.end()) ? 0 : it->second.size();
}

std::uint64_t SynapseBus::dropped_for(Handle h) const {
    std::shared_ptr<Subscriber> s;
    {
        std::shared_lock<std::shared_mutex> lock(topology_mu_);
        auto it = subscribers_.find(h);
        if (it == subscribers_.end()) return 0;
        s = it->second;
    }
    std::lock_guard<std::mutex> lk(s->mu);
    return s->dropped;
}

} // namespace khora::synapse
