#include "khora/lattice/lattice.hpp"

#include <algorithm>
#include <utility>

namespace khora::lattice {

Lattice::Lattice() = default;

void Lattice::store(std::string label, Glyph g) {
    store_[std::move(label)] = std::move(g);
}

bool Lattice::erase(const std::string& label) {
    return store_.erase(label) > 0;
}

bool Lattice::contains(const std::string& label) const noexcept {
    return store_.find(label) != store_.end();
}

std::optional<Glyph> Lattice::recall(const std::string& label) const {
    auto it = store_.find(label);
    if (it == store_.end()) return std::nullopt;
    return it->second;
}

std::vector<LatticeMatch> Lattice::query(const Glyph& probe, std::size_t k) const {
    std::vector<LatticeMatch> matches;
    matches.reserve(store_.size());
    for (const auto& [label, g] : store_) {
        const std::size_t h = probe.hamming(g);
        const double s = 1.0 - 2.0 * static_cast<double>(h) / static_cast<double>(kGlyphBits);
        matches.push_back({label, h, s});
    }
    std::sort(matches.begin(), matches.end(),
              [](const LatticeMatch& a, const LatticeMatch& b) { return a.hamming < b.hamming; });
    if (k < matches.size()) matches.resize(k);
    return matches;
}

} // namespace khora::lattice
