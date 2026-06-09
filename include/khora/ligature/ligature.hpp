#pragma once

// The Ligature — Khora's STRUCTURED-RELATION layer.
//
// The Plexus captures THAT two concepts relate (association, by mutual
// information). It cannot capture HOW they relate. "energy" and "motion" are
// kin in the Plexus, but the Plexus cannot say "energy CAUSES motion" or
// "kinetic IS-A energy". That missing structure is the deepest ceiling on the
// whole engine: reasoning over association is only path-walking, never
// deduction; answering is only listing kin, never stating a fact; and every
// acquired book yields correlation, not understanding.
//
// The Ligature adds the missing layer: TYPED relations extracted from text by
// syntactic patterns (no LLM) — the classical, dependency-free way. Each triple
// (subject, relation, object) carries a count: a relation asserted across many
// sentences is reliable; a one-off is noise. From these, real inference becomes
// possible — is-a transitivity (kinetic is-a energy, energy is-a quantity =>
// kinetic is-a quantity), causal chains, property inheritance.
//
// Relations extracted (v1, the highest-value for reasoning):
//   IS-A     "X is a/an/the Y", "Y such as X"        -> taxonomy / definition
//   CAUSES   "X causes/produces/creates Y", "X leads to Y"
//   HAS-PART "X has/contains Y"
//
// Pure standard C++. Additive and commutative (counts sum), so it builds across
// all cores the same way the Plexus does, and persists to a compact text file.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace khora::ligature {

enum class Relation : std::uint8_t { IsA = 0, Causes = 1, HasPart = 2, _Count = 3 };

const char* relation_name(Relation r) noexcept;   // "is-a" / "causes" / "has"

class Ligature {
public:
    Ligature() = default;

    // Extract typed relations from a token sequence (the same tokens the Lexicon
    // and Plexus see). Returns the number of triples asserted.
    std::size_t extract(const std::vector<std::string>& tokens);

    // Assert/reinforce a single triple.
    void add(Relation r, const std::string& subj, const std::string& obj,
             std::uint32_t n = 1);

    // Merge another Ligature into this one (additive — for the parallel forge).
    void absorb(const Ligature& other);

    // Queries, strongest (most-asserted) first.
    std::vector<std::pair<std::string, std::uint32_t>>
    objects(Relation r, const std::string& subj, std::size_t k = 8) const;   // subj r ?
    std::vector<std::pair<std::string, std::uint32_t>>
    subjects(Relation r, const std::string& obj, std::size_t k = 8) const;    // ? r obj
    std::uint32_t count(Relation r, const std::string& subj, const std::string& obj) const;

    // is-a transitive reachability: is `x` (transitively) a kind of `y`?
    bool is_a(const std::string& x, const std::string& y, int max_depth = 5) const;

    // Inspectors.
    std::uint64_t triple_count() const noexcept { return triples_; }
    std::uint64_t assertions()   const noexcept { return assertions_; }

    void save(const std::filesystem::path& prefix) const;
    void load(const std::filesystem::path& prefix);

private:
    using Map = std::unordered_map<std::string, std::unordered_map<std::string, std::uint32_t>>;
    std::array<Map, static_cast<std::size_t>(Relation::_Count)> fwd_;  // subj -> obj -> count
    std::array<Map, static_cast<std::size_t>(Relation::_Count)> rev_;  // obj  -> subj -> count
    std::uint64_t triples_    = 0;   // distinct (r,subj,obj)
    std::uint64_t assertions_ = 0;   // total count over all triples
};

} // namespace khora::ligature
