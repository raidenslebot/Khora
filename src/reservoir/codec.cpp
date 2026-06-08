#include "khora/reservoir/codec.hpp"

#include <cstring>

namespace khora::reservoir::codec {

namespace {

// LZSS parameters.
constexpr std::size_t kWindowBits = 12;
constexpr std::size_t kWindowSize = 1u << kWindowBits;   // 4096
constexpr std::size_t kMinMatch   = 3;
constexpr std::size_t kMaxMatch   = kMinMatch + 15;      // 18 (4-bit length)
constexpr std::size_t kHashBits   = 15;
constexpr std::size_t kHashSize   = 1u << kHashBits;
constexpr int         kMaxChain   = 128;                 // search depth (ratio vs speed)

inline std::uint32_t hash3(const std::uint8_t* p) {
    const std::uint32_t v = (std::uint32_t(p[0]) << 16) |
                            (std::uint32_t(p[1]) << 8)  |
                             std::uint32_t(p[2]);
    return (v * 2654435761u) >> (32 - kHashBits);
}

} // namespace

std::vector<std::uint8_t> compress(const std::vector<std::uint8_t>& input) {
    std::vector<std::uint8_t> out;
    const std::size_t n = input.size();
    if (n == 0) return out;
    out.reserve(n / 2 + 16);

    const std::uint8_t* data = input.data();

    // Hash chains: head[h] = most recent position with hash h;
    // prev[pos & mask] = previous position in the same chain.
    std::vector<std::int32_t> head(kHashSize, -1);
    std::vector<std::int32_t> prev(kWindowSize, -1);

    std::size_t pos = 0;
    std::size_t flag_pos = 0;
    std::uint8_t flag_bit = 0;

    auto begin_group = [&]() {
        flag_pos = out.size();
        out.push_back(0);       // placeholder flag byte
        flag_bit = 0;
    };
    auto set_flag = [&](bool literal) {
        if (literal) out[flag_pos] |= static_cast<std::uint8_t>(1u << flag_bit);
        if (++flag_bit == 8) begin_group();
    };

    begin_group();

    while (pos < n) {
        std::size_t best_len = 0;
        std::size_t best_off = 0;

        if (pos + kMinMatch <= n) {
            const std::uint32_t h = hash3(data + pos);
            std::int32_t cand = head[h];
            int chain = kMaxChain;
            const std::size_t max_here = std::min(kMaxMatch, n - pos);
            const std::size_t window_floor = (pos > kWindowSize) ? pos - kWindowSize : 0;

            while (cand >= 0 && static_cast<std::size_t>(cand) >= window_floor && chain-- > 0) {
                const std::size_t c = static_cast<std::size_t>(cand);
                // Quick reject on the byte just past current best.
                if (best_len == 0 || data[c + best_len] == data[pos + best_len]) {
                    std::size_t len = 0;
                    while (len < max_here && data[c + len] == data[pos + len]) ++len;
                    if (len > best_len) {
                        best_len = len;
                        best_off = pos - c;
                        if (len >= max_here) break;
                    }
                }
                cand = prev[c & (kWindowSize - 1)];
            }
        }

        if (best_len >= kMinMatch) {
            // Emit match token: two bytes. offset-1 (12 bits) | (len-min)(4 bits)
            const std::uint16_t off1 = static_cast<std::uint16_t>(best_off - 1);
            const std::uint16_t lenc = static_cast<std::uint16_t>(best_len - kMinMatch);
            const std::uint16_t token = static_cast<std::uint16_t>((off1 << 4) | lenc);
            out.push_back(static_cast<std::uint8_t>(token >> 8));
            out.push_back(static_cast<std::uint8_t>(token & 0xFF));
            set_flag(false);

            // Insert all covered positions into the hash chains.
            const std::size_t end = pos + best_len;
            while (pos < end) {
                if (pos + kMinMatch <= n) {
                    const std::uint32_t h = hash3(data + pos);
                    prev[pos & (kWindowSize - 1)] = head[h];
                    head[h] = static_cast<std::int32_t>(pos);
                }
                ++pos;
            }
        } else {
            // Emit literal.
            out.push_back(data[pos]);
            set_flag(true);
            if (pos + kMinMatch <= n) {
                const std::uint32_t h = hash3(data + pos);
                prev[pos & (kWindowSize - 1)] = head[h];
                head[h] = static_cast<std::int32_t>(pos);
            }
            ++pos;
        }
    }

    return out;
}

std::vector<std::uint8_t> decompress(const std::vector<std::uint8_t>& packed,
                                     std::size_t original_size) {
    std::vector<std::uint8_t> out;
    out.reserve(original_size);

    std::size_t ip = 0;
    const std::size_t np = packed.size();

    while (ip < np && out.size() < original_size) {
        const std::uint8_t flags = packed[ip++];
        for (int bit = 0; bit < 8 && out.size() < original_size; ++bit) {
            if (ip >= np && !((flags >> bit) & 1)) break;
            if ((flags >> bit) & 1u) {
                // literal
                if (ip >= np) break;
                out.push_back(packed[ip++]);
            } else {
                // match: two bytes
                if (ip + 1 >= np) break;
                const std::uint16_t token =
                    static_cast<std::uint16_t>((std::uint16_t(packed[ip]) << 8) | packed[ip + 1]);
                ip += 2;
                const std::size_t off = (token >> 4) + 1;
                const std::size_t len = (token & 0x0F) + kMinMatch;
                if (off > out.size()) break;  // corrupt; stop safely
                const std::size_t start = out.size() - off;
                for (std::size_t k = 0; k < len && out.size() < original_size; ++k) {
                    out.push_back(out[start + k]);
                }
            }
        }
    }
    return out;
}

bool verify_roundtrip(const std::vector<std::uint8_t>& input) {
    const auto packed = compress(input);
    const auto back   = decompress(packed, input.size());
    return back == input;
}

} // namespace khora::reservoir::codec
