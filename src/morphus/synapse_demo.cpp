// Demonstrates the Synapse Bus carrying typed glyph pulses between
// subsystems running on separate threads.
//
// One producer thread publishes 100 pulses to "ping" carrying random
// glyphs. The main thread subscribes, polls, and prints latency stats.

#include "khora/synapse/synapse_bus.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

using namespace std::chrono_literals;

int main() {
    using namespace khora::synapse;
    using khora::lattice::Glyph;

    SynapseBus bus;
    auto h = bus.subscribe("ping", /*capacity=*/256);

    std::thread producer([&bus] {
        for (int i = 0; i < 100; ++i) {
            bus.publish("ping", Glyph::random(0x100 + i));
            std::this_thread::sleep_for(1ms);
        }
    });

    int got = 0;
    std::chrono::nanoseconds total_latency{0};
    std::chrono::nanoseconds max_latency{0};

    while (got < 100) {
        auto p = bus.poll(h, 500ms);
        if (!p) break;
        const auto now = std::chrono::steady_clock::now();
        const auto lat = now - p->timestamp;
        total_latency += lat;
        if (lat > max_latency) max_latency = lat;
        ++got;
    }
    producer.join();

    std::printf("Synapse Bus demo\n");
    std::printf("  pulses received : %d / 100\n", got);
    std::printf("  total published : %llu\n",
                static_cast<unsigned long long>(bus.total_published()));
    std::printf("  total dropped   : %llu\n",
                static_cast<unsigned long long>(bus.total_dropped()));
    if (got > 0) {
        const double avg_us = static_cast<double>(total_latency.count()) / got / 1000.0;
        const double max_us = static_cast<double>(max_latency.count()) / 1000.0;
        std::printf("  avg latency     : %.2f us\n", avg_us);
        std::printf("  max latency     : %.2f us\n", max_us);
    }
    return (got == 100) ? 0 : 1;
}
