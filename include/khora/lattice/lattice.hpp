#pragma once

#include "glyph.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace khora::lattice {

struct LatticeMatch {
    std::string label;
    std::size_t hamming;
    double      similarity;
};

class Lattice {
public:
    Lattice();

    void store(std::string label, Glyph g);
    bool erase(const std::string& label);
    bool contains(const std::string& label) const noexcept;
    std::optional<Glyph> recall(const std::string& label) const;

    // k-nearest neighbour by Hamming distance over all stored glyphs.
    std::vector<LatticeMatch> query(const Glyph& probe, std::size_t k = 5) const;

    // Every label, sorted. The store could be queried and not ENUMERATED, which
    // is the same gap the Ligature had: a caller that wants to persist or hand
    // over the whole thing had to already know every key. Sorted rather than in
    // hash order so an archive written twice is byte-identical.
    std::vector<std::string> labels() const;

    std::size_t size() const noexcept { return store_.size(); }

    auto begin() const noexcept { return store_.cbegin(); }
    auto end()   const noexcept { return store_.cend(); }

private:
    std::unordered_map<std::string, Glyph> store_;
};

} // namespace khora::lattice
