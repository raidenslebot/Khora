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

namespace khora::taxis { class Taxis; }

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

    // WHICH SURFACE PATTERN produced a relation. Extraction was one function
    // with every rule welded into it, so the only measurable thing was the union
    // of all of them -- and the union scores 1.87% against an external IS-A bar
    // with chance at 0.76%. A mask makes each rule measurable ON ITS OWN, which
    // is the difference between knowing the extractor is poor and knowing which
    // part of it is poor.
    //
    // The distinction that matters is copula against Hearst. "X is a Y" is a
    // predication and only sometimes a taxonomy ("justice is the interest of the
    // stronger"), while "Y such as X" and "X and other Y" are lexico-syntactic
    // frames that are hard to write without meaning the taxonomy. Hearst (1992)
    // is built on exactly that asymmetry, and this repository had one of the
    // frames and not the others.
    enum Pattern : std::uint32_t {
        PatCopula    = 1u << 0,   // X is/are/was/were a Y
        PatSuchAs    = 1u << 1,   // Y such as X
        PatOther     = 1u << 2,   // X and/or other Y
        PatIncluding = 1u << 3,   // Y including/especially X
        PatCausal    = 1u << 4,   // X causes/leads to Y
        PatPossess   = 1u << 5,   // X has a Y / contains Y / made of Y
        PatHearst    = PatSuchAs | PatOther | PatIncluding,
        PatAll       = 0xFFFFFFFFu
    };

    // Extract typed relations from a token sequence (the same tokens the Lexicon
    // and Plexus see). Returns the number of triples asserted.
    // `tx`, when supplied, vetoes any IS-A whose object it has positive evidence
    // is not a noun. The extractor takes the last content word of the noun
    // phrase as the head, which is the right rule, and had no way to know
    // whether that word is a noun -- so "man is a social being" asserted
    // is-a(man, social). Measured against a blocklist of impossible objects the
    // veto removes 51.1% of them and costs 2.9% of the words WordNet certifies
    // as nouns.
    //
    // It requires two passes over the corpus, because the tagger has to have
    // seen the words before it can vote on them. Passing nullptr is the old
    // behaviour and is what a single-pass caller gets.
    std::size_t extract(const std::vector<std::string>& tokens,
                        std::uint32_t patterns = PatAll,
                        const khora::taxis::Taxis* tx = nullptr);

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

    // A PLAN — the chain that would BRING THE GOAL ABOUT.
    //
    // deduce() chains forward: given a subject, what follows from it. That is
    // inference. Planning is the other direction and the system had no way to do
    // it: given a GOAL, what causes it, and what causes that, back to something
    // with no known cause of its own. Ninety-four tools, nineteen thousand typed
    // relations, and nothing that could ask "what would bring this about".
    //
    // Beam search backward over CAUSES, scored by the weakest link in the chain,
    // because a plan is only as good as its least-supported step. Cycles are
    // refused: a chain that revisits a concept is not a plan, it is a loop.
    //
    // This is goal decomposition over knowledge the system read for itself, not
    // a hand-written task hierarchy. What it cannot do is check whether a step is
    // ACHIEVABLE -- there is no world model and no action preconditions here --
    // so it reports the route and leaves the acting to Volition.
    struct Plan {
        std::vector<std::string> steps;      // root cause first, goal last
        std::uint32_t            support = 0;  // the weakest link
    };
    // `min_support` is not a tuning knob, it is the difference between a plan and
    // a coincidence. Relations extracted from prose are dominated by single
    // sightings -- asked to reach "war" on the live graph, the unfiltered planner
    // returns `annual -> cannot -> him -> way -> war`, every link asserted once.
    // Those chains are real paths through the graph and worthless as plans, and
    // requiring every step to have been seen more than once is what separates
    // them from the ones worth acting on.
    std::vector<Plan> plan_to(const std::string& goal,
                              int max_depth = 4,
                              std::size_t beam = 6,
                              std::size_t k = 5,
                              std::uint32_t min_support = 2) const;

    // OBJECTIVE self-measurement of the PLANNING faculty, the same shape as the
    // deduction one: over goals drawn from the graph, what fraction admit a
    // chain of at least `min_steps` causal steps. A capability nobody measures
    // is a capability nobody can tell has regressed.
    double benchmark_planning(std::size_t n = 200, int min_steps = 2,
                              std::uint64_t seed = 0,
                              std::uint32_t min_support = 2) const;

    // OBJECTIVE self-measurement of the DEDUCTION faculty. Constructs facts that are
    // genuinely derivable (X is-a A, A rel Z, with X rel Z NOT directly asserted) and
    // returns the fraction that deduce() actually recovers. A fitness signal for
    // reasoning over structure — the closed loop, reaching past inference into
    // deduction so a second faculty becomes measurable (and, in time, evolvable).
    double benchmark_deduction(std::size_t n = 200, std::uint64_t seed = 0) const;

    // HOW MUCH OF THIS IS EVIDENCE. Counts of distinct triples by support:
    // seen once, twice, three-to-nine times, ten or more.
    //
    // This exists because the planning benchmark fell from 100% of goals to 7%
    // when a support floor of two was applied, which says the causal layer is
    // dominated by single sightings. A relation seen once is a sentence, not a
    // fact, and a reasoner built on top of those produces fluent nonsense --
    // measured: `annual -> cannot -> him -> way -> war`, every link asserted
    // exactly once. Knowing the shape of the distribution is how you tell
    // whether a reasoning result is limited by the reasoner or by what it has
    // to reason over.
    std::array<std::uint64_t, 4> support_profile(Relation r) const;

    // Inspectors.
    // Every triple, for a consumer that needs the whole graph rather than a
    // neighbourhood. The query API answers "what does X cause"; nothing could
    // answer "what is in here", so the relations could not be handed to another
    // reasoner without already knowing every subject to ask about.
    //
    // `min_support` is the same floor every other consumer uses and defaults to
    // it: 14,544 triples become 377 at two, and the ones it drops are the single
    // sightings that make a chain a coincidence.
    struct Triple { Relation rel; std::string subject, object; std::uint32_t support; };
    std::vector<Triple> all(std::uint32_t min_support = 2) const;

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
