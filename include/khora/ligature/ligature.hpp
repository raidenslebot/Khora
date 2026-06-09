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

// A DERIVED fact — one Khora reasoned, not one it read. The chain that produced
// it (`via`) makes it explainable; `support` (the weakest link's count) is its
// confidence. This is deduction: knowledge that was implicit in the relations
// made explicit, the thing association alone could never give.
struct Inference {
    Relation                 relation;   // the derived relation
    std::string              object;     // the derived object
    std::vector<std::string> via;        // the intermediate concepts of the derivation
    std::uint32_t            support = 0;
};

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

    // DEDUCE — derive facts about `subject` that are NOT directly asserted:
    //   (a) property inheritance: subject is-a A, A has/causes Z  =>  subject has/causes Z
    //   (b) causal chaining:      subject causes Y, Y causes Z     =>  subject causes Z
    // Returns the novel derivations, strongest support first, each with its chain.
    // Real inference over the structured layer — new knowledge from old, the thing
    // a pure association graph (the Plexus) provably could not produce.
    std::vector<Inference> deduce(const std::string& subject, int max_depth = 3) const;

    // OBJECTIVE self-measurement of the DEDUCTION faculty. Constructs facts that are
    // genuinely derivable (X is-a A, A rel Z, with X rel Z NOT directly asserted) and
    // returns the fraction that deduce() actually recovers. A fitness signal for
    // reasoning over structure — the closed loop, reaching past inference into
    // deduction so a second faculty becomes measurable (and, in time, evolvable).
    double benchmark_deduction(std::size_t n = 200, std::uint64_t seed = 0) const;

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
