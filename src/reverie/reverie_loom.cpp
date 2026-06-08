#include "khora/reverie/reverie_loom.hpp"

#include <string>
#include <vector>

namespace khora::reverie {

using khora::lattice::Glyph;
using khora::lattice::bundle;
using khora::lattice::kGlyphBits;
using khora::soma::Drive;
using khora::soma::kDriveCount;

namespace {
inline std::uint64_t splitmix64(std::uint64_t& s) noexcept {
    std::uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

void perturb(Glyph& g, std::size_t n_bits, std::uint64_t& rng) {
    for (std::size_t i = 0; i < n_bits; ++i) {
        const std::size_t pos = static_cast<std::size_t>(splitmix64(rng) % kGlyphBits);
        g.flip_bit(pos);
    }
}
} // namespace

ReverieLoom::ReverieLoom(khora::lattice::Lattice&         memory,
                         khora::cortex::PredictiveColumn& cortex,
                         khora::soma::SomaNexus&          soma,
                         std::uint64_t                    seed)
    : memory_(memory)
    , cortex_(cortex)
    , soma_(soma)
    , rng_state_(seed)
{
    // Default dream affinity — dreams are curious + mastery activity.
    dream_affinity_.per_drive[static_cast<std::size_t>(Drive::Curiosity)] = 1.0;
    dream_affinity_.per_drive[static_cast<std::size_t>(Drive::Mastery)]   = 1.0;
}

DreamSample ReverieLoom::dream_once() {
    DreamSample s{};
    ++cycles_;

    if (memory_.size() == 0) {
        return s;
    }

    // Snapshot labels for O(1) random access. (Memory map mutates rarely
    // during reverie, so this is a per-cycle cost we accept for simplicity.)
    std::vector<std::string> labels;
    labels.reserve(memory_.size());
    for (const auto& [label, _g] : memory_) labels.push_back(label);

    const std::size_t ia = static_cast<std::size_t>(splitmix64(rng_state_) % labels.size());
    const std::size_t ib = static_cast<std::size_t>(splitmix64(rng_state_) % labels.size());

    s.seed_a = memory_.recall(labels[ia]).value();
    s.seed_b = memory_.recall(labels[ib]).value();

    if (perturbation_bits_ > 0) {
        perturb(s.seed_a, perturbation_bits_, rng_state_);
        perturb(s.seed_b, perturbation_bits_, rng_state_);
    }

    s.dream = bundle({s.seed_a, s.seed_b});

    // Familiarity: how close is the dream to what the cortex currently expects?
    // A blank cortex returns zero glyph; treat that as zero familiarity.
    const Glyph expected = cortex_.predict();
    s.familiarity = (expected == Glyph::zero()) ? 0.0 : expected.similarity(s.dream);

    // Modulate dream affinity with familiarity for the Mastery channel:
    // a dream that aligns with the cortex's prediction satisfies Mastery.
    khora::soma::Affinity affinity = dream_affinity_;
    affinity.per_drive[static_cast<std::size_t>(Drive::Mastery)] *= s.familiarity;
    s.satisfaction = soma_.evaluate(affinity);

    if (s.satisfaction >= threshold_) {
        const std::string label = "dream_" + std::to_string(retained_count_);
        dreams_.store(label, s.dream);
        s.retained = true;
        ++retained_count_;
    }

    return s;
}

std::size_t ReverieLoom::dream_n(std::size_t n) {
    std::size_t retained_here = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const auto s = dream_once();
        if (s.retained) ++retained_here;
    }
    return retained_here;
}

} // namespace khora::reverie
