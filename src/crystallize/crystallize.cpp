#include "khora/crystallize/crystallize.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace khora::crystallize {
namespace {

struct Vote {
    std::uint32_t support = 0;
    double evidence = 0.0;
    std::vector<std::string> witnesses;
};

} // namespace

Crystal infer_isa(const khora::plexus::Plexus& plex,
                  const khora::ligature::Ligature& lig,
                  const std::string& subject,
                  const Options& options) {
    using R = khora::ligature::Relation;

    const std::uint32_t min_support = std::max<std::uint32_t>(1, options.min_support);
    if (subject.empty() || !plex.has(subject)) return {};

    const auto kin = plex.associates(subject, std::max<std::size_t>(options.associates, min_support));
    if (kin.size() < min_support) return {};

    std::unordered_map<std::string, Vote> votes;
    for (const auto& [k, weight] : kin) {
        if (k == subject) continue;
        for (const auto& [parent, n] : lig.objects(R::IsA, k, options.parent_fanout)) {
            (void)n;
            if (parent == subject) continue;

            auto& v = votes[parent];
            ++v.support;
            v.evidence += weight;
            if (v.witnesses.size() < 8) v.witnesses.push_back(k);
        }
    }

    Crystal best;
    for (auto& [parent, vote] : votes) {
        if (vote.support < min_support) continue;
        if (lig.count(R::IsA, subject, parent) > 0) continue;
        if (lig.is_a(parent, subject)) continue;
        if (plex.affinity(subject, parent) <= 0.0) continue;

        if (!best || vote.support > best.support ||
            (vote.support == best.support && vote.evidence > best.evidence)) {
            best.subject = subject;
            best.parent = parent;
            best.support = vote.support;
            best.evidence = vote.evidence;
            best.witnesses = std::move(vote.witnesses);
        }
    }
    return best;
}

std::size_t commit(khora::ligature::Ligature& lig, const Crystal& crystal) {
    if (!crystal) return 0;
    if (lig.count(khora::ligature::Relation::IsA, crystal.subject, crystal.parent) > 0) return 0;
    if (lig.is_a(crystal.parent, crystal.subject)) return 0;
    lig.add(khora::ligature::Relation::IsA, crystal.subject, crystal.parent, crystal.support);
    return 1;
}

std::vector<Crystal> infer_many(const khora::plexus::Plexus& plex,
                                const khora::ligature::Ligature& lig,
                                const std::vector<std::string>& subjects,
                                std::size_t max_results,
                                const Options& options) {
    std::vector<Crystal> out;
    if (max_results == 0) return out;

    std::unordered_set<std::string> seen_subjects;
    std::unordered_set<std::string> seen_edges;
    for (const auto& subject : subjects) {
        if (out.size() >= max_results) break;
        if (!seen_subjects.insert(subject).second) continue;

        Crystal c = infer_isa(plex, lig, subject, options);
        if (!c) continue;
        const std::string edge = c.subject + "\n" + c.parent;
        if (!seen_edges.insert(edge).second) continue;
        out.push_back(std::move(c));
    }
    return out;
}

std::size_t commit_many(khora::ligature::Ligature& lig,
                        const std::vector<Crystal>& crystals) {
    std::size_t written = 0;
    for (const auto& c : crystals) written += commit(lig, c);
    return written;
}

} // namespace khora::crystallize
