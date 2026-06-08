#include "khora/cortex/predictive_column.hpp"

#include "khora/lattice/persistence.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace khora::cortex {

using khora::lattice::Glyph;
using khora::lattice::bundle;
using khora::lattice::permute;

PredictiveColumn::PredictiveColumn(std::size_t context_window)
    : context_window_(context_window == 0 ? 1 : context_window) {}

Glyph PredictiveColumn::current_context_() const {
    if (recent_.empty()) return Glyph::zero();

    // Build a position-aware context glyph by permuting each member of
    // the sliding window by a position-specific amount and bundling.
    // Permutation (rather than shared-position XOR) is used here on
    // purpose: it decorrelates differently per element, which keeps the
    // stored context keys cleanly separable for single-shot prediction.
    std::vector<Glyph> ordered;
    ordered.reserve(recent_.size());
    int pos = 0;
    for (auto it = recent_.rbegin(); it != recent_.rend(); ++it, ++pos) {
        ordered.push_back(permute(*it, pos * 137));
    }
    return bundle(std::span<const Glyph>{ordered.data(), ordered.size()});
}

Glyph PredictiveColumn::predict() const {
    if (ctx_keys_.size() == 0) return Glyph::zero();
    const Glyph ctx = current_context_();
    const auto matches = ctx_keys_.query(ctx, 1);
    if (matches.empty()) return Glyph::zero();
    const auto val = ctx_vals_.recall(matches[0].label);
    return val.value_or(Glyph::zero());
}

PredictiveColumn::StepResult PredictiveColumn::step(const Glyph& input) {
    StepResult r;
    r.actual = input;

    // 1. Predict BEFORE this input is incorporated.
    r.predicted        = predict();
    r.prediction_error = r.predicted.hamming(input);
    r.similarity       = r.predicted.similarity(input);

    if (ctx_keys_.size() > 0) {
        const Glyph ctx = current_context_();
        const auto matches = ctx_keys_.query(ctx, 1);
        r.novel_context = matches.empty() || (matches[0].similarity < 0.3);
    } else {
        r.novel_context = true;
    }

    // 2-3. Learn the association and advance the window.
    store_and_advance_(input);

    // 4. Track recent accuracy.
    recent_sims_.push_back(r.similarity);
    if (recent_sims_.size() > kRecentWindow) recent_sims_.pop_front();

    return r;
}

void PredictiveColumn::store_and_advance_(const Glyph& input) {
    // Associate current context with this input as the "next" glyph.
    if (!recent_.empty()) {
        const Glyph ctx = current_context_();
        const std::string label = "ctx_" + std::to_string(next_assoc_id_++);
        ctx_keys_.store(label, ctx);
        ctx_vals_.store(label, input);
        assoc_order_.push_back(label);

        // Bounded associative memory: forget the oldest when over cap.
        if (max_associations_ != 0) {
            while (assoc_order_.size() > max_associations_) {
                const std::string& old = assoc_order_.front();
                ctx_keys_.erase(old);
                ctx_vals_.erase(old);
                assoc_order_.pop_front();
            }
        }
    }

    // Advance the sliding window.
    recent_.push_back(input);
    if (recent_.size() > context_window_) recent_.pop_front();
    ++observations_;
}

void PredictiveColumn::learn(const Glyph& input) {
    // Fast path: store the association and advance, no k-NN prediction.
    store_and_advance_(input);
}

std::size_t PredictiveColumn::prune_associations(std::size_t target) {
    std::size_t removed = 0;
    while (assoc_order_.size() > target) {
        const std::string& old = assoc_order_.front();
        ctx_keys_.erase(old);
        ctx_vals_.erase(old);
        assoc_order_.pop_front();
        ++removed;
    }
    return removed;
}

double PredictiveColumn::recent_accuracy() const {
    if (recent_sims_.empty()) return 0.0;
    double sum = 0.0;
    for (double s : recent_sims_) sum += s;
    return sum / static_cast<double>(recent_sims_.size());
}

namespace {
constexpr char kCortexMagic[12]   = {'K','H','O','R','A','C','O','R','T','E','X','\0'};
constexpr std::uint32_t kCortexFormatVersion = 1;
} // namespace

void PredictiveColumn::save(const std::filesystem::path& prefix) const {
    namespace fs = std::filesystem;
    if (prefix.has_parent_path()) fs::create_directories(prefix.parent_path());

    auto hdr_path = prefix; hdr_path += ".cortex";
    std::ofstream os(hdr_path, std::ios::binary | std::ios::trunc);
    if (!os) throw khora::lattice::PersistError("cannot open cortex header for write: " + hdr_path.string());

    os.write(kCortexMagic, sizeof(kCortexMagic));
    os.write(reinterpret_cast<const char*>(&kCortexFormatVersion), sizeof(kCortexFormatVersion));

    const std::uint32_t cw  = static_cast<std::uint32_t>(context_window_);
    const std::uint64_t obs = static_cast<std::uint64_t>(observations_);
    const std::uint64_t nai = static_cast<std::uint64_t>(next_assoc_id_);
    os.write(reinterpret_cast<const char*>(&cw),  sizeof(cw));
    os.write(reinterpret_cast<const char*>(&obs), sizeof(obs));
    os.write(reinterpret_cast<const char*>(&nai), sizeof(nai));

    const std::uint32_t rcount = static_cast<std::uint32_t>(recent_.size());
    os.write(reinterpret_cast<const char*>(&rcount), sizeof(rcount));
    for (const auto& g : recent_) {
        os.write(reinterpret_cast<const char*>(g.words().data()),
                 static_cast<std::streamsize>(sizeof(khora::lattice::Glyph::Word) * khora::lattice::kGlyphWords));
    }

    const std::uint32_t scount = static_cast<std::uint32_t>(recent_sims_.size());
    os.write(reinterpret_cast<const char*>(&scount), sizeof(scount));
    for (double s : recent_sims_) {
        os.write(reinterpret_cast<const char*>(&s), sizeof(s));
    }

    if (!os) throw khora::lattice::PersistError("cortex header write failed mid-stream");
    os.close();

    auto keys_path = prefix; keys_path += ".keys.klat";
    auto vals_path = prefix; vals_path += ".vals.klat";
    khora::lattice::save(ctx_keys_, keys_path);
    khora::lattice::save(ctx_vals_, vals_path);
}

void PredictiveColumn::load(const std::filesystem::path& prefix) {
    auto hdr_path = prefix; hdr_path += ".cortex";
    std::ifstream is(hdr_path, std::ios::binary);
    if (!is) throw khora::lattice::PersistError("cannot open cortex header for read: " + hdr_path.string());

    char magic[12]{};
    is.read(magic, sizeof(magic));
    if (!is || std::memcmp(magic, kCortexMagic, sizeof(magic)) != 0) {
        throw khora::lattice::PersistError("bad cortex header magic");
    }
    std::uint32_t version = 0;
    is.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (version != kCortexFormatVersion) {
        throw khora::lattice::PersistError("unsupported cortex version " + std::to_string(version));
    }

    std::uint32_t cw  = 0;
    std::uint64_t obs = 0;
    std::uint64_t nai = 0;
    is.read(reinterpret_cast<char*>(&cw),  sizeof(cw));
    is.read(reinterpret_cast<char*>(&obs), sizeof(obs));
    is.read(reinterpret_cast<char*>(&nai), sizeof(nai));
    if (!is) throw khora::lattice::PersistError("short read in cortex header");

    context_window_   = static_cast<std::size_t>(cw == 0 ? 1 : cw);
    observations_     = static_cast<std::size_t>(obs);
    next_assoc_id_    = static_cast<std::size_t>(nai);

    std::uint32_t rcount = 0;
    is.read(reinterpret_cast<char*>(&rcount), sizeof(rcount));
    if (!is) throw khora::lattice::PersistError("short read at recent_count");

    recent_.clear();
    for (std::uint32_t i = 0; i < rcount; ++i) {
        khora::lattice::Glyph::Storage storage{};
        is.read(reinterpret_cast<char*>(storage.data()),
                static_cast<std::streamsize>(sizeof(khora::lattice::Glyph::Word) * khora::lattice::kGlyphWords));
        if (!is) throw khora::lattice::PersistError("short read in recent[]");
        recent_.emplace_back(storage);
    }

    std::uint32_t scount = 0;
    is.read(reinterpret_cast<char*>(&scount), sizeof(scount));
    if (!is) throw khora::lattice::PersistError("short read at sims_count");
    recent_sims_.clear();
    for (std::uint32_t i = 0; i < scount; ++i) {
        double d = 0.0;
        is.read(reinterpret_cast<char*>(&d), sizeof(d));
        if (!is) throw khora::lattice::PersistError("short read in recent_sims[]");
        recent_sims_.push_back(d);
    }

    auto keys_path = prefix; keys_path += ".keys.klat";
    auto vals_path = prefix; vals_path += ".vals.klat";
    ctx_keys_ = khora::lattice::load(keys_path);
    ctx_vals_ = khora::lattice::load(vals_path);

    // Rebuild the FIFO eviction order from the loaded keys. Exact original
    // order is lost across save/load, but the cap still bounds memory.
    assoc_order_.clear();
    for (const auto& [label, _g] : ctx_keys_) assoc_order_.push_back(label);
}

} // namespace khora::cortex
