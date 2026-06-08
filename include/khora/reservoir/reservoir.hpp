#pragma once

// The Reservoir — Khora's liquid knowledge pool.
//
// Source texts (Tomes) live here, distilled and compressed, under a hard
// byte cap (~20 GB by default). This is explicitly NOT Khora's knowledge:
// it is the *material* knowledge is distilled from. What Khora knows lives
// in the Lattice / Cortex / Lexicon. A Tome can be evicted once Khora has
// learned enough from it, and re-acquired later if needed — liquid.
//
// The Reservoir is always aware of its contents and can name the
// least-valuable Tome to evict when full: low learning-yield + high
// mastery + stale + large = first to go.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace khora::reservoir {

struct Tome {
    std::string   title;
    std::string   topic;
    std::string   source_url;       // for re-acquisition after eviction
    std::uint64_t original_bytes  = 0;
    std::uint64_t stored_bytes    = 0;
    std::uint8_t  method          = 0;   // 0 = raw, 1 = lzss
    std::uint32_t times_read      = 0;
    double        learning_yield  = 0.0; // cumulative competence gain credited to this Tome
    double        mastery         = 0.0; // 0..1 estimate of how fully Khora has absorbed it
    std::uint64_t admit_seq       = 0;   // recency: higher = more recent
};

struct AdmitResult {
    bool        ok = false;
    std::string title;
    std::uint64_t original_bytes = 0;
    std::uint64_t stored_bytes   = 0;
    double      compression_ratio = 1.0;
    bool        verified_lossless = false;
    std::vector<std::string> evicted;   // titles evicted to make room
    std::string error;
};

class Reservoir {
public:
    // cap_bytes default ~20 GB.
    explicit Reservoir(std::filesystem::path dir,
                       std::uint64_t cap_bytes = 20ull * 1024 * 1024 * 1024);

    // Admit raw acquired bytes: distill -> compress -> verify -> store,
    // evicting lowest-value Tomes if over cap. Re-admitting an existing
    // title replaces it.
    // do_distill=false stores the bytes verbatim (no prose distillation) —
    // used for material that is not book prose, e.g. source code, where
    // distillation would strip the very structure that matters.
    AdmitResult admit(const std::string& title,
                      const std::string& topic,
                      const std::string& source_url,
                      const std::string& raw_bytes,
                      bool do_distill = true);

    // Read a Tome's distilled text (decompressing as needed). Bumps the
    // read counter. nullopt if absent.
    std::optional<std::string> read(const std::string& title);

    bool        has(const std::string& title) const;
    bool        evict(const std::string& title);
    std::string evict_lowest_value();          // returns evicted title ("" if none)

    // Credit a Tome with a learning outcome (competence gained, and an
    // updated mastery estimate in [0,1]).
    void record_learning(const std::string& title, double yield_delta, double mastery);

    // Awareness.
    std::vector<Tome> catalog() const;
    std::uint64_t total_stored_bytes() const noexcept { return total_stored_; }
    std::uint64_t cap_bytes()          const noexcept { return cap_bytes_; }
    std::size_t   count()              const noexcept { return tomes_.size(); }
    double        keep_value(const Tome& t) const;

    // Persist / restore the catalog index.
    void save_catalog() const;
    void load_catalog();

private:
    std::filesystem::path tome_path_(const std::string& title) const;
    bool write_tome_file_(const std::string& title, std::uint8_t method,
                          std::uint64_t orig_len, const std::vector<std::uint8_t>& payload);
    void enforce_cap_(std::vector<std::string>& evicted);

    std::filesystem::path           dir_;
    std::uint64_t                   cap_bytes_;
    std::uint64_t                   total_stored_ = 0;
    std::uint64_t                   next_seq_     = 1;
    std::vector<Tome>               tomes_;
};

} // namespace khora::reservoir
