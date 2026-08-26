#include "khora/chiasm/chiasm.hpp"

#include <algorithm>

namespace khora::chiasm {

using khora::lattice::Glyph;

const Glyph& Chiasm::role_glyph(const std::string& role) {
    auto it = role_glyph_.find(role);
    if (it != role_glyph_.end()) return it->second;
    // from_hash rather than a counter, so "sound" is the same glyph in every
    // process and after a restart. A role identified by insertion order would
    // mean a memory saved today is unreadable tomorrow.
    return role_glyph_.emplace(role, Glyph::from_hash(role)).first->second;
}

const Glyph* Chiasm::role_glyph_if(const std::string& role) const {
    const auto it = role_glyph_.find(role);
    return it == role_glyph_.end() ? nullptr : &it->second;
}

std::size_t Chiasm::known(const std::string& role) const {
    const auto it = cleanup_.find(role);
    return it == cleanup_.end() ? 0 : it->second.size();
}

void Chiasm::remember(const std::vector<Field>& fields) {
    if (fields.empty()) return;
    std::vector<Glyph> bound;
    bound.reserve(fields.size());
    for (const Field& f : fields) {
        bound.push_back(khora::lattice::bind(role_glyph(f.role), f.value));
        // Cleanup memory. Storing under the label means two observations of the
        // same thing overwrite rather than accumulate, which is what "the word
        // 'bell'" should mean -- one entry, not one per sighting.
        cleanup_[f.role].store(f.label, f.value);
    }
    records_.push_back(khora::lattice::bundle(std::span<const Glyph>(bound)));
}

Recall Chiasm::unbind_and_clean(const Glyph& record,
                                const std::string& want_role) const {
    Recall out;
    const Glyph* rg = role_glyph_if(want_role);
    if (!rg) return out;
    const auto cit = cleanup_.find(want_role);
    if (cit == cleanup_.end()) return out;

    // XOR is its own inverse, so this recovers the stored value exactly PLUS the
    // crosstalk of every other field in the record. It is close to the truth and
    // not equal to it, which is the entire reason the next line exists.
    const Glyph noisy = khora::lattice::bind(record, *rg);

    const auto hits = cit->second.query(noisy, 2);
    if (hits.empty()) return out;
    out.label      = hits[0].label;
    out.confidence = hits[0].similarity;
    // THE MARGIN IS THE HONEST NUMBER. A confidence of 0.31 means nothing on its
    // own; 0.31 against a runner-up of 0.30 is a coin flip that happened to land,
    // and 0.31 against 0.02 is a retrieval. Reporting only the first is how a
    // capacity limit gets missed.
    out.margin = (hits.size() > 1) ? (hits[0].similarity - hits[1].similarity)
                                   : hits[0].similarity;
    return out;
}

Recall Chiasm::recall(const std::string& cue_role, const Glyph& cue,
                      const std::string& want_role) const {
    Recall out;
    const Glyph* cg = role_glyph_if(cue_role);
    if (!cg || records_.empty()) return out;

    // FIND THE RECORD. bind(role, value) is one of the components bundled into
    // the record it came from, and a bundle stays similar to its components, so
    // the right record is the one most similar to the cue's bound pair. No index
    // and no scan of fields -- one similarity per record.
    const Glyph probe = khora::lattice::bind(*cg, cue);
    std::size_t best = 0;
    double best_sim = -2.0;
    for (std::size_t i = 0; i < records_.size(); ++i) {
        const double s = records_[i].similarity(probe);
        if (s > best_sim) { best_sim = s; best = i; }
    }
    return unbind_and_clean(records_[best], want_role);
}

} // namespace khora::chiasm
