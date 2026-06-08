#include "khora/lattice/persistence.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>

namespace khora::lattice {
namespace {

constexpr char kHeaderMagic[8] = {'K','H','O','R','A','L','A','T'};
constexpr char kFooterMagic[8] = {'K','H','O','R','A','E','N','D'};
constexpr std::uint32_t kFormatVersion = 1;

template <typename T>
void write_pod(std::ofstream& os, const T& v) {
    os.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

template <typename T>
void read_pod(std::ifstream& is, T& v) {
    is.read(reinterpret_cast<char*>(&v), sizeof(T));
    if (!is) throw PersistError("short read");
}

void read_exact(std::ifstream& is, char* buf, std::size_t n) {
    is.read(buf, static_cast<std::streamsize>(n));
    if (!is || static_cast<std::size_t>(is.gcount()) != n) {
        throw PersistError("short read");
    }
}

} // namespace

PersistStats save(const Lattice& lat, const std::filesystem::path& path) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream os(path, std::ios::binary | std::ios::trunc);
    if (!os) throw PersistError("cannot open file for write: " + path.string());

    os.write(kHeaderMagic, sizeof(kHeaderMagic));
    write_pod<std::uint32_t>(os, kFormatVersion);
    write_pod<std::uint32_t>(os, static_cast<std::uint32_t>(kGlyphBits));
    write_pod<std::uint64_t>(os, static_cast<std::uint64_t>(lat.size()));

    for (const auto& [label, g] : lat) {
        write_pod<std::uint32_t>(os, static_cast<std::uint32_t>(label.size()));
        os.write(label.data(), static_cast<std::streamsize>(label.size()));
        os.write(reinterpret_cast<const char*>(g.words().data()),
                 static_cast<std::streamsize>(sizeof(Glyph::Word) * kGlyphWords));
    }

    os.write(kFooterMagic, sizeof(kFooterMagic));
    if (!os) throw PersistError("write failed mid-stream");
    os.close();

    PersistStats s;
    s.glyph_count   = lat.size();
    s.bytes_written = std::filesystem::file_size(path);
    return s;
}

Lattice load(const std::filesystem::path& path) {
    std::ifstream is(path, std::ios::binary);
    if (!is) throw PersistError("cannot open file for read: " + path.string());

    char header[8];
    read_exact(is, header, sizeof(header));
    if (std::memcmp(header, kHeaderMagic, sizeof(header)) != 0) {
        throw PersistError("bad header magic");
    }

    std::uint32_t version = 0;
    read_pod(is, version);
    if (version != kFormatVersion) {
        throw PersistError("unsupported version: " + std::to_string(version));
    }

    std::uint32_t bits = 0;
    read_pod(is, bits);
    if (bits != static_cast<std::uint32_t>(kGlyphBits)) {
        throw PersistError("glyph bit-width mismatch: file has " + std::to_string(bits)
                           + ", build has " + std::to_string(kGlyphBits));
    }

    std::uint64_t count = 0;
    read_pod(is, count);

    Lattice lat;
    for (std::uint64_t i = 0; i < count; ++i) {
        std::uint32_t label_len = 0;
        read_pod(is, label_len);

        std::string label(label_len, '\0');
        if (label_len > 0) {
            read_exact(is, label.data(), label_len);
        }

        Glyph::Storage storage{};
        read_exact(is, reinterpret_cast<char*>(storage.data()),
                   sizeof(Glyph::Word) * kGlyphWords);
        lat.store(std::move(label), Glyph(storage));
    }

    char footer[8];
    read_exact(is, footer, sizeof(footer));
    if (std::memcmp(footer, kFooterMagic, sizeof(footer)) != 0) {
        throw PersistError("bad footer magic (file truncated or corrupt?)");
    }

    return lat;
}

} // namespace khora::lattice
