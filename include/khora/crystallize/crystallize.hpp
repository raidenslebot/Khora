#pragma once

// Crystallization: turn associative consensus into structured knowledge.
//
// The Plexus knows that concepts are related; the Ligature knows how they are
// related. This bridge lets Khora infer a conservative new is-a relation when
// several strong Plexus neighbours independently share the same parent and that
// parent also has direct positive affinity to the subject.

#include "khora/ligature/ligature.hpp"
#include "khora/plexus/plexus.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace khora::crystallize {

struct Options {
    std::size_t   associates = 32;
    std::size_t   parent_fanout = 6;
    std::uint32_t min_support = 3;
};

struct Crystal {
    std::string              subject;
    std::string              parent;
    std::uint32_t            support = 0;
    double                   evidence = 0.0;
    std::vector<std::string> witnesses;

    explicit operator bool() const noexcept { return !subject.empty() && !parent.empty(); }
};

Crystal infer_isa(const khora::plexus::Plexus& plex,
                  const khora::ligature::Ligature& lig,
                  const std::string& subject,
                  const Options& options = {});

std::size_t commit(khora::ligature::Ligature& lig, const Crystal& crystal);

std::vector<Crystal> infer_many(const khora::plexus::Plexus& plex,
                                const khora::ligature::Ligature& lig,
                                const std::vector<std::string>& subjects,
                                std::size_t max_results,
                                const Options& options = {});

std::size_t commit_many(khora::ligature::Ligature& lig,
                        const std::vector<Crystal>& crystals);

} // namespace khora::crystallize
