#include "khora/reservoir/reservoir.hpp"

#include "khora/reservoir/codec.hpp"
#include "khora/reservoir/distill.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>

namespace khora::reservoir {

namespace fs = std::filesystem;

namespace {

constexpr char kTomeMagic[6] = {'K','T','O','M','E','\0'};
constexpr std::uint32_t kTomeVersion = 1;

std::string fnv_hex(const std::string& s) {
    std::uint64_t h = 0xCBF29CE484222325ULL;
    for (unsigned char c : s) { h ^= c; h *= 0x100000001B3ULL; }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

std::string sanitize_field(std::string s) {
    for (char& c : s) if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    return s;
}

template <typename T> void put(std::ofstream& os, const T& v) {
    os.write(reinterpret_cast<const char*>(&v), sizeof(T));
}
template <typename T> bool get(std::ifstream& is, T& v) {
    is.read(reinterpret_cast<char*>(&v), sizeof(T));
    return static_cast<bool>(is);
}

} // namespace

Reservoir::Reservoir(fs::path dir, std::uint64_t cap_bytes)
    : dir_(std::move(dir)), cap_bytes_(cap_bytes) {
    fs::create_directories(dir_);
    load_catalog();
}

fs::path Reservoir::tome_path_(const std::string& title) const {
    return dir_ / (fnv_hex(title) + ".tome");
}

bool Reservoir::has(const std::string& title) const {
    return std::any_of(tomes_.begin(), tomes_.end(),
                       [&](const Tome& t) { return t.title == title; });
}

bool Reservoir::write_tome_file_(const std::string& title, std::uint8_t method,
                                 std::uint64_t orig_len,
                                 const std::vector<std::uint8_t>& payload) {
    std::ofstream os(tome_path_(title), std::ios::binary | std::ios::trunc);
    if (!os) return false;
    os.write(kTomeMagic, sizeof(kTomeMagic));
    put(os, kTomeVersion);
    put(os, method);
    put(os, orig_len);
    const std::uint64_t comp_len = payload.size();
    put(os, comp_len);
    if (!payload.empty()) os.write(reinterpret_cast<const char*>(payload.data()),
                                   static_cast<std::streamsize>(payload.size()));
    return static_cast<bool>(os);
}

AdmitResult Reservoir::admit(const std::string& title, const std::string& topic,
                             const std::string& source_url, const std::string& raw_bytes,
                             bool do_distill) {
    AdmitResult r;
    r.title = title;

    // 1. Distill to clean canonical text — unless the caller opted out (e.g.
    //    source code, which distillation would gut).
    DistillStats ds;
    const std::string clean = do_distill ? distill(raw_bytes, &ds) : raw_bytes;
    if (clean.empty()) { r.error = "nothing to admit"; return r; }

    const std::vector<std::uint8_t> bytes(clean.begin(), clean.end());

    // 2. Compress and VERIFY lossless. Fall back to raw on any mismatch.
    std::uint8_t method = 0;
    std::vector<std::uint8_t> payload = bytes;
    {
        const auto packed = codec::compress(bytes);
        const auto back   = codec::decompress(packed, bytes.size());
        if (back == bytes && packed.size() < bytes.size()) {
            method  = 1;
            payload = packed;
            r.verified_lossless = true;
        } else {
            // raw storage is trivially lossless
            r.verified_lossless = (method == 0);
        }
    }

    // 3. If replacing an existing Tome, reclaim its bytes first.
    if (has(title)) evict(title);

    // 4. Write the Tome file.
    if (!write_tome_file_(title, method, bytes.size(), payload)) {
        r.error = "failed to write tome file";
        return r;
    }

    Tome t;
    t.title          = title;
    t.topic          = topic;
    t.source_url     = source_url;
    t.original_bytes = bytes.size();
    t.stored_bytes   = (method == 1) ? payload.size() : bytes.size();
    t.method         = method;
    t.admit_seq      = next_seq_++;
    tomes_.push_back(t);
    total_stored_ += t.stored_bytes;

    r.ok = true;
    r.original_bytes    = t.original_bytes;
    r.stored_bytes      = t.stored_bytes;
    r.compression_ratio = t.stored_bytes ? static_cast<double>(t.original_bytes) /
                                            static_cast<double>(t.stored_bytes) : 1.0;

    // 5. Enforce the cap.
    enforce_cap_(r.evicted);
    save_catalog();
    return r;
}

std::optional<std::string> Reservoir::read(const std::string& title) {
    auto it = std::find_if(tomes_.begin(), tomes_.end(),
                           [&](const Tome& t) { return t.title == title; });
    if (it == tomes_.end()) return std::nullopt;

    std::ifstream is(tome_path_(title), std::ios::binary);
    if (!is) return std::nullopt;
    char magic[6];
    is.read(magic, sizeof(magic));
    if (std::memcmp(magic, kTomeMagic, sizeof(magic)) != 0) return std::nullopt;
    std::uint32_t ver; std::uint8_t method; std::uint64_t orig_len, comp_len;
    if (!get(is, ver) || !get(is, method) || !get(is, orig_len) || !get(is, comp_len))
        return std::nullopt;

    std::vector<std::uint8_t> payload(static_cast<std::size_t>(comp_len));
    if (comp_len) is.read(reinterpret_cast<char*>(payload.data()),
                          static_cast<std::streamsize>(comp_len));

    std::string text;
    if (method == 1) {
        const auto back = codec::decompress(payload, static_cast<std::size_t>(orig_len));
        text.assign(back.begin(), back.end());
    } else {
        text.assign(payload.begin(), payload.end());
    }

    ++it->times_read;
    save_catalog();
    return text;
}

bool Reservoir::evict(const std::string& title) {
    auto it = std::find_if(tomes_.begin(), tomes_.end(),
                           [&](const Tome& t) { return t.title == title; });
    if (it == tomes_.end()) return false;
    std::error_code ec;
    fs::remove(tome_path_(title), ec);
    total_stored_ -= std::min(total_stored_, it->stored_bytes);
    tomes_.erase(it);
    return true;
}

double Reservoir::keep_value(const Tome& t) const {
    // Higher = more worth keeping. Evict the minimum.
    //  - learning_yield: material that taught Khora a lot is precious
    //  - (1 - mastery): once fully absorbed, the material is redundant
    //  - recency: recently admitted/used material is more relevant
    //  - size penalty: large Tomes cost more to keep
    const double age = static_cast<double>(next_seq_) - static_cast<double>(t.admit_seq);
    const double recency = 1.0 / (1.0 + 0.05 * age);
    const double read_bonus = 1.0 + 0.1 * static_cast<double>(t.times_read);
    const double size_mb = static_cast<double>(t.stored_bytes) / (1024.0 * 1024.0);
    const double size_penalty = 1.0 + 0.05 * size_mb;
    return (0.2 + t.learning_yield) * (1.05 - t.mastery) * recency * read_bonus / size_penalty;
}

std::string Reservoir::evict_lowest_value() {
    if (tomes_.empty()) return {};
    auto it = std::min_element(tomes_.begin(), tomes_.end(),
                               [&](const Tome& a, const Tome& b) {
                                   return keep_value(a) < keep_value(b);
                               });
    const std::string title = it->title;
    evict(title);
    return title;
}

void Reservoir::enforce_cap_(std::vector<std::string>& evicted) {
    while (total_stored_ > cap_bytes_ && !tomes_.empty()) {
        const std::string gone = evict_lowest_value();
        if (gone.empty()) break;
        evicted.push_back(gone);
    }
}

void Reservoir::record_learning(const std::string& title, double yield_delta, double mastery) {
    auto it = std::find_if(tomes_.begin(), tomes_.end(),
                           [&](const Tome& t) { return t.title == title; });
    if (it == tomes_.end()) return;
    it->learning_yield += yield_delta;
    it->mastery = std::clamp(mastery, 0.0, 1.0);
    save_catalog();
}

std::vector<Tome> Reservoir::catalog() const { return tomes_; }

void Reservoir::save_catalog() const {
    std::ofstream os(dir_ / "catalog.tsv", std::ios::trunc);
    if (!os) return;
    os << "# title\ttopic\tsource_url\torig\tstored\tmethod\treads\tyield\tmastery\tseq\n";
    for (const auto& t : tomes_) {
        os << sanitize_field(t.title) << '\t'
           << sanitize_field(t.topic) << '\t'
           << sanitize_field(t.source_url) << '\t'
           << t.original_bytes << '\t' << t.stored_bytes << '\t'
           << static_cast<int>(t.method) << '\t' << t.times_read << '\t'
           << t.learning_yield << '\t' << t.mastery << '\t' << t.admit_seq << '\n';
    }
}

void Reservoir::load_catalog() {
    tomes_.clear();
    total_stored_ = 0;
    next_seq_ = 1;
    std::ifstream is(dir_ / "catalog.tsv");
    if (!is) return;
    std::string line;
    while (std::getline(is, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        Tome t;
        std::string method_s, orig_s, stored_s, reads_s, yield_s, mastery_s, seq_s;
        std::getline(ss, t.title, '\t');
        std::getline(ss, t.topic, '\t');
        std::getline(ss, t.source_url, '\t');
        std::getline(ss, orig_s, '\t');
        std::getline(ss, stored_s, '\t');
        std::getline(ss, method_s, '\t');
        std::getline(ss, reads_s, '\t');
        std::getline(ss, yield_s, '\t');
        std::getline(ss, mastery_s, '\t');
        std::getline(ss, seq_s, '\t');
        try {
            t.original_bytes = std::stoull(orig_s);
            t.stored_bytes   = std::stoull(stored_s);
            t.method         = static_cast<std::uint8_t>(std::stoi(method_s));
            t.times_read     = static_cast<std::uint32_t>(std::stoul(reads_s));
            t.learning_yield = std::stod(yield_s);
            t.mastery        = std::stod(mastery_s);
            t.admit_seq      = std::stoull(seq_s);
        } catch (...) { continue; }
        // Only keep entries whose backing file still exists.
        if (!fs::exists(tome_path_(t.title))) continue;
        total_stored_ += t.stored_bytes;
        next_seq_ = std::max(next_seq_, t.admit_seq + 1);
        tomes_.push_back(std::move(t));
    }
}

} // namespace khora::reservoir
