#pragma once

// Persistence for the Morphic Lattice.
// Binary format: "KHORALAT" magic + version + glyph_bits + count
// + repeated (label_len, label, glyph_words) + "KHORAEND" trailer.

#include "lattice.hpp"

#include <cstdint>
#include <filesystem>
#include <stdexcept>

namespace khora::lattice {

struct PersistStats {
    std::size_t    glyph_count;
    std::uintmax_t bytes_written;
};

class PersistError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

PersistStats save(const Lattice& lat, const std::filesystem::path& path);
Lattice      load(const std::filesystem::path& path);

} // namespace khora::lattice
