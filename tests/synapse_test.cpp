// Tests for the Synapse Bus.

#include "khora/synapse/synapse_bus.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

namespace {
int g_total = 0;
int g_failed = 0;
}

#define EXPECT(cond, msg) do { \
    ++g_total; \
    if (!(cond)) { ++g_failed; std::fprintf(stderr, "FAIL: %s  (%s:%d)\n", (msg), __FILE__, __LINE__); } \
} while (0)

using namespace std::chrono_literals;
using khora::lattice::Glyph;

int main() {
    using namespace khora::synapse;

    // 1. Single subscriber receives published pulses in order.
    {
        SynapseBus bus;
        auto h = bus.subscribe("ping");
        for (int i = 0; i < 5; ++i) {
            bus.publish("ping", Glyph::random(i + 1));
        }
        std::vector<std::uint64_t> got;
        for (int i = 0; i < 5; ++i) {
            auto p = bus.poll(h, 100ms);
            if (p) got.push_back(p->sequence);
        }
        EXPECT(got.size() == 5, "subscriber received 5 pulses");
        bool ordered = true;
        for (std::size_t i = 1; i < got.size(); ++i) {
            if (got[i] <= got[i - 1]) { ordered = false; break; }
        }
        EXPECT(ordered, "sequences strictly increasing");
    }

    // 2. Topic isolation: distinct subscribers see only their topics.
    {
        SynapseBus bus;
        auto ha = bus.subscribe("alpha");
        auto hb = bus.subscribe("beta");
        bus.publish("alpha", Glyph::random(1));
        bus.publish("alpha", Glyph::random(2));
        bus.publish("beta",  Glyph::random(3));

        int a_count = 0;
        while (auto p = bus.try_pop(ha)) { (void)p; ++a_count; }
        int b_count = 0;
        while (auto p = bus.try_pop(hb)) { (void)p; ++b_count; }
        EXPECT(a_count == 2, "alpha subscriber gets 2");
        EXPECT(b_count == 1, "beta subscriber gets 1");
    }

    // 3. Fan-out: multiple subscribers to same topic all receive.
    {
        SynapseBus bus;
        auto h1 = bus.subscribe("broadcast");
        auto h2 = bus.subscribe("broadcast");
        bus.publish("broadcast", Glyph::random(42));

        auto p1 = bus.poll(h1, 100ms);
        auto p2 = bus.poll(h2, 100ms);
        EXPECT(p1.has_value() && p2.has_value(), "both subscribers got the pulse");
        EXPECT(bus.subscriber_count("broadcast") == 2, "subscriber_count reports 2");
    }

    // 4. Drop-on-overflow with counter.
    {
        SynapseBus bus;
        auto h = bus.subscribe("noisy", /*capacity=*/3);
        for (int i = 0; i < 10; ++i) bus.publish("noisy", Glyph::random(i + 1));

        int got = 0;
        while (auto p = bus.try_pop(h)) { (void)p; ++got; }
        EXPECT(got == 3, "only capacity-many pulses survive");
        EXPECT(bus.dropped_for(h) == 7, "per-subscriber drop counter == 7");
        EXPECT(bus.total_dropped() == 7, "global drop counter == 7");
    }

    // 5. Unsubscribe stops delivery.
    {
        SynapseBus bus;
        auto h = bus.subscribe("once");
        bus.publish("once", Glyph::random(1));
        EXPECT(bus.poll(h, 100ms).has_value(), "received before unsub");

        bus.unsubscribe(h);
        bus.publish("once", Glyph::random(2));
        EXPECT(!bus.poll(h, 50ms).has_value(), "no delivery after unsubscribe");
        EXPECT(bus.subscriber_count("once") == 0, "subscriber removed from topic");
    }

    // 6. Timeout returns nullopt promptly.
    {
        SynapseBus bus;
        auto h = bus.subscribe("silent");
        const auto t0 = std::chrono::steady_clock::now();
        auto p = bus.poll(h, 50ms);
        const auto dur = std::chrono::steady_clock::now() - t0;
        EXPECT(!p.has_value(), "poll on empty topic times out to nullopt");
        EXPECT(dur < 200ms, "poll honours timeout (within slack)");
    }

    // 7. Concurrent publishers + single subscriber: no losses, all
    //    sequences delivered exactly once. Per the bus's documented
    //    semantics, sequences are *unique* but not necessarily monotonic
    //    at the subscriber when publishers race.
    {
        SynapseBus bus;
        auto h = bus.subscribe("concurrent", /*capacity=*/100000);

        constexpr int N_PUB = 4;
        constexpr int N_PER = 1000;
        std::vector<std::thread> pubs;
        std::atomic<int> published{0};
        for (int t = 0; t < N_PUB; ++t) {
            pubs.emplace_back([&bus, &published, t] {
                for (int i = 0; i < N_PER; ++i) {
                    bus.publish("concurrent", Glyph::random((t + 1) * 1000 + i));
                    published.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
        for (auto& th : pubs) th.join();
        EXPECT(published.load() == N_PUB * N_PER, "all threads completed their publishes");

        constexpr int N_TOTAL = N_PUB * N_PER;
        std::vector<bool> seen(N_TOTAL + 1, false);  // sequences are 1..N_TOTAL
        int received = 0;
        bool duplicate = false;
        bool out_of_range = false;
        while (auto p = bus.poll(h, 100ms)) {
            ++received;
            if (p->sequence == 0 || p->sequence > static_cast<std::uint64_t>(N_TOTAL)) {
                out_of_range = true;
            } else if (seen[p->sequence]) {
                duplicate = true;
            } else {
                seen[p->sequence] = true;
            }
        }
        EXPECT(received == N_TOTAL, "subscriber received all pulses (no loss)");
        EXPECT(!duplicate, "no duplicate sequences delivered");
        EXPECT(!out_of_range, "all delivered sequences within expected range");
        EXPECT(bus.total_published() == static_cast<std::uint64_t>(N_TOTAL),
               "bus total_published count correct");
        EXPECT(bus.total_dropped() == 0, "no drops at adequate capacity");
    }

    // 8. Same-thread ordering IS guaranteed (per documented semantics).
    {
        SynapseBus bus;
        auto h = bus.subscribe("inorder");
        constexpr int N = 200;
        for (int i = 0; i < N; ++i) bus.publish("inorder", Glyph::random(i + 1));

        bool ordered = true;
        std::uint64_t prev = 0;
        int got = 0;
        while (auto p = bus.try_pop(h)) {
            ++got;
            if (p->sequence <= prev) { ordered = false; break; }
            prev = p->sequence;
        }
        EXPECT(got == N, "same-thread publish: subscriber gets all N");
        EXPECT(ordered, "same-thread publish: subscriber sees strict monotone seq");
    }

    std::printf("\nSynapse tests: %d/%d passed (%d failed).\n",
                g_total - g_failed, g_total, g_failed);
    return g_failed == 0 ? 0 : 1;
}
