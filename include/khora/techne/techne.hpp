#pragma once

// Techne — the organ that writes code, and refuses to hand back code it cannot
// certify.
//
// WHY THIS EXISTS, and it comes out of a chain of measurements rather than a
// preference.
//
// Everything this project built for natural language starved on the same fact:
// prose has no answer key and almost no deep structure. Across 7.66M tokens the
// share of n-word contexts that ever recur is 66.9 / 30.7 / 13.5% at n=1/2/3 and
// 0.32% at n=8, and it SATURATES -- a 319-fold increase in corpus moved n=8 from
// 0.00% to 0.32%. Distinct contexts per token at n=8 is 0.996: new contexts
// arrive as fast as tokens do. Then the second wall: with no ground truth, the
// differences worth arguing about were 40 correct answers against 39 out of
// 1,133, which a two-proportion test cannot separate at all.
//
// Code inverts both properties.
//
//   Deep structure RECURS. Idioms, call sequences and control shapes repeat,
//   which is exactly the regime where allocation-on-failure and deep context
//   pay instead of drowning.
//
//   And there is an ORACLE. A program either compiles or does not; a test
//   either passes or does not. Correctness is machine-decidable rather than a
//   percentage over a sample too small to rank anything.
//
// The oracle is the thing every earlier assay lacked, and it changes what a
// fitness function can be. There is no need to argue about a metric when the
// metric is "does it do what was asked, on every input".
//
// THE RULE THIS ORGAN IS BUILT AROUND: nothing is returned unless it is
// certified. A result carries how it was verified -- EXHAUSTIVE over a finite
// input domain, or TESTED over n cases -- and a search that cannot certify
// returns nothing rather than something plausible. That is the opposite of a
// language model's contract, which is to always produce something and never to
// know whether it is right.
//
// WHAT IS BORROWED, AND WHAT IS NOT.
//
// Borrowed, deliberately, because it was measured to work in this repo: the
// linear byte-tape genome with a TOTAL decoder, where every byte string is a
// running program so mutation and crossover need no repair rule. Tree-based
// program search has to repair invalid offspring, and the repair rule is a human
// prior smuggled into the search.
//
// Not borrowed: the substrate. Ribosome's registers held hypervectors, and the
// measurement said the vector algebra was doing nothing -- only the graph
// opcodes carried a gradient, because random atomic hypervectors have no
// structure for bind/bundle/permute to exploit. Techne's registers hold integer
// lists and its opcodes compute. The substrate is replaced rather than decorated.
//
// THE PART THAT COMPOUNDS, and it is the whole point.
//
// A search that solves a fixed set of primitives solves only what those
// primitives reach. Techne's instruction set GROWS: every certified program can
// become a callable primitive for later searches, so the reachable space widens
// with each solved problem instead of staying fixed.
//
// The library is held under a HARD BUDGET with utility-based eviction, which is
// the one mechanism in this project that has survived every measurement it was
// put through -- a primitive earns its slot by how often it appears in
// certified solutions, and is evicted when it does not. Growth without eviction
// is the failure mode that made TemporalMemory allocate 2.9M segments for 24k
// tokens; a library that only accretes ends up as a haystack that makes the
// search harder rather than easier, and that is measurable either way.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace khora::techne {

// A value is a list of integers. Scalars are one-element lists, which removes an
// entire class of type errors from the instruction set: every operation is
// defined on every value, so there is no such thing as an ill-typed program.
using Value = std::vector<std::int64_t>;

inline constexpr std::size_t kRegisters   = 4;
inline constexpr std::size_t kMaxListLen  = 512;   // memory bound
inline constexpr std::size_t kMaxCallDepth = 3;    // library recursion bound

// ---------------------------------------------------------------------------
// Instruction set.
//
// NO LOOPS AND NO BRANCHES -- only bounded combinators. That is not a
// limitation grudgingly accepted, it is what makes every program terminate by
// construction. A general loop would reintroduce the halting problem into the
// middle of a fitness evaluation, and a step limit that kills a program mid-run
// makes fitness depend on the limit rather than on the program. Fold, map and
// filter express the same computations with termination guaranteed.
// ---------------------------------------------------------------------------
enum class Op : std::uint8_t {
    Nop = 0,
    Mov,       // dst = a
    Const,     // dst = [k]              small integer constant from b
    Add,       // dst = a + b            elementwise, shorter operand cycles
    Sub,
    Mul,
    Div,       // division by zero yields 0 rather than trapping
    Mod,
    Len,       // dst = [len(a)]
    Head,      // dst = [a[0]]           empty yields []
    Tail,      // dst = a[1..]
    Rev,
    Sort,
    Append,    // dst = a ++ b
    Take,      // dst = first b[0] of a
    Drop,
    Index,     // dst = [a[b[0]]]        out of range yields []
    Range,     // dst = [0 .. a[0]-1]    capped at kMaxListLen
    Sum,
    Max,
    Min,
    Filter,    // dst = [x in a : x > b[0]]
    MapAdd,    // dst = [x + b[0] for x in a]
    MapMul,
    Count,     // dst = [#{x in a : x == b[0]}]
    Call,      // dst = library[b](a)    a learned primitive
    kCount
};

struct Instr {
    Op           op  = Op::Nop;
    std::uint8_t dst = 0;
    std::uint8_t a   = 0;
    std::uint8_t b   = 0;
};

// ---------------------------------------------------------------------------
// Program: a linear byte tape, decoded totally.
// ---------------------------------------------------------------------------
class Program {
public:
    Program() = default;
    explicit Program(std::vector<std::uint8_t> tape) : tape_(std::move(tape)) {}

    static Program random(std::size_t instrs, std::uint64_t seed);

    // Every byte string is a valid program. This is what makes mutation and
    // crossover closed operations with no repair step.
    std::vector<Instr> decode() const;

    Program mutate(std::uint64_t seed, double rate) const;
    static Program cross(const Program& x, const Program& y, std::uint64_t seed);

    // The output is the destination of the last executed instruction, not a
    // fixed register. Measured on the sibling organ: with a fixed output
    // register, mean live instructions per genome was 1.309 and 27% of programs
    // were pure identity; reading out the last write took that to 2.241 and 0%,
    // because an instruction no longer has to guess which slot is being read.
    std::size_t output_register() const;
    std::vector<bool> live_mask() const;
    std::size_t effective_length() const;

    const std::vector<std::uint8_t>& tape() const noexcept { return tape_; }
    std::size_t length() const noexcept { return tape_.size() / 4; }

    // A program that is meant to be read. The point of synthesising source
    // rather than fitting weights is that the answer can be inspected, so the
    // disassembly marks which instructions actually reach the output.
    std::string disassemble() const;

private:
    std::vector<std::uint8_t> tape_;
};

// ---------------------------------------------------------------------------
// The library: primitives learned from certified solutions.
// ---------------------------------------------------------------------------
struct Learned {
    std::string name;
    Program     body;
    std::size_t uses = 0;      // appearances in later certified solutions
    std::size_t born = 0;      // which task introduced it
};

class Library {
public:
    explicit Library(std::size_t budget = 32) : budget_(budget) {}

    // Admit a certified program. Returns false when it was rejected as a
    // duplicate of something already held.
    bool admit(std::string name, Program body, std::size_t task);

    Value  call(std::size_t index, const Value& arg, std::size_t depth) const;
    std::size_t size() const noexcept { return items_.size(); }
    const Learned& at(std::size_t i) const { return items_[i]; }

    void note_use(std::size_t i) { if (i < items_.size()) ++items_[i].uses; }

    // Evict the least used when over budget. A library that only accretes turns
    // into a haystack the search has to hunt through, which makes later problems
    // HARDER -- the same unbounded-growth failure that made TemporalMemory
    // allocate 2.9M segments for 24k tokens.
    std::size_t prune();

    std::size_t evicted() const noexcept { return evicted_; }

private:
    std::size_t budget_;
    std::vector<Learned> items_;
    std::size_t evicted_ = 0;
};

// ---------------------------------------------------------------------------
// Execution. Bounded by construction: fixed registers, no loops, list length
// capped, library calls depth-limited. There is no input a program can be given
// that makes it fail to return.
// ---------------------------------------------------------------------------
// `depth` guards library recursion. It is a parameter rather than a constant
// because the first version passed a literal 1 at every call site, so a library
// program containing a call would recurse forever -- in a module whose entire
// contract is termination by construction. It had not fired only because no
// admitted program yet had a live call.
Value run(const Program& p, const Value& input, const Library* lib = nullptr,
          std::size_t depth = 0);

// ---------------------------------------------------------------------------
// Specification and certificate.
// ---------------------------------------------------------------------------
struct Case { Value in, out; };

struct Spec {
    std::string name;
    std::vector<Case> cases;          // what the caller asked for
    std::vector<Case> holdout;        // cases the search never sees
};

// HOW A RESULT WAS VERIFIED. A caller gets this or nothing.
enum class Proof {
    None,        // no program satisfied the specification
    Tested,      // passes every visible case
    Generalised  // passes every visible case AND every held-out case
};

struct Solution {
    Program     program;
    Proof       proof = Proof::None;
    std::size_t cases_passed = 0;
    std::size_t cases_total  = 0;
    std::size_t holdout_passed = 0;
    std::size_t holdout_total  = 0;
    std::size_t candidates_tried = 0;

    bool certified() const noexcept { return proof != Proof::None; }
};

struct SearchConfig {
    std::size_t population   = 400;
    std::size_t generations  = 400;
    std::size_t program_len  = 6;
    double      mutation_rate = 0.06;
    double      crossover     = 0.5;
    std::size_t tournament    = 5;
    std::uint64_t seed        = 20260824;
};

// Search for a program satisfying `spec`. Returns an uncertified Solution when
// nothing satisfies it -- never a plausible guess.
Solution synthesise(const Spec& spec, SearchConfig cfg, Library* lib = nullptr);

// ---------------------------------------------------------------------------
// BOTTOM-UP CONSTRUCTION, because the measurement said a gradient search cannot
// solve compositional tasks and no amount of tuning will change that.
//
// Measured: the evolutionary search solved 13 of 20 tasks and every failure was
// COMPOSITIONAL. `sum_of_squares` is two instructions -- multiply the list by
// itself, then sum -- and 20,400 candidates never found it. The reason is not
// budget. A program that computes the squares and stops produces a LIST where a
// SCALAR is wanted, so element-wise partial credit scores it at essentially
// zero: getting halfway is worth nothing, the surface is flat, and selection has
// nothing to climb. This is the same flat-landscape wall that killed the
// hypervector organ, one level up.
//
// So stop climbing and start BUILDING. Enumerate by size: every value reachable
// in one step, then two, then three, applying operations to what has already
// been built. A two-step solution is found by construction rather than by
// stumbling onto both steps at once.
//
// What makes this tractable is OBSERVATIONAL EQUIVALENCE. Two expressions that
// produce identical outputs on every case are interchangeable for every purpose
// the specification can distinguish, so only one needs keeping. The pool is
// therefore indexed by BEHAVIOUR, not by syntax, and the combinatorial explosion
// collapses onto the far smaller set of distinct behaviours.
//
// And this is where the library rejoins: a learned primitive is just another
// one-step expression seeded into the pool at size 1. It does not need to be
// stumbled upon by a mutation operator -- it is available from the first level,
// which is what the gradient search could never arrange.
// ---------------------------------------------------------------------------

// One node of a constructed expression. `a` and `b` index earlier pool entries;
// -1 means the input itself.
struct Expr {
    Op   op = Op::Mov;
    int  a  = -1;
    int  b  = -1;
    std::uint8_t k = 0;      // constant selector / library index
};

struct Recipe {
    std::vector<Expr> pool;
    std::size_t root = 0;
    bool found = false;

    Value apply(const Value& in, const Library* lib) const;
    std::string render() const;
    std::size_t size() const;      // nodes actually reachable from the root
};

struct BuildResult {
    Recipe recipe;
    Proof  proof = Proof::None;
    std::size_t cases_passed = 0, cases_total = 0;
    std::size_t holdout_passed = 0, holdout_total = 0;
    std::size_t distinct_behaviours = 0;   // the pool the search actually explored
    std::size_t nodes_considered = 0;      // candidates before dedup
    bool certified() const noexcept { return proof != Proof::None; }
};

// Build a program by composition. `max_pool` bounds the number of DISTINCT
// behaviours retained, which is the real memory cost.
BuildResult construct(const Spec& spec, std::size_t max_pool, const Library* lib = nullptr);

// Exhaustive enumeration of programs up to `max_len` instructions. This is the
// dumb baseline the search has to beat, and in this repo the dumb baseline has
// won often enough that it is not a formality: a thirty-line trigram table beat
// a temporal memory, and a one-line graph heuristic tied an evolved operator.
Solution enumerate(const Spec& spec, std::size_t max_len, std::size_t budget,
                   Library* lib = nullptr);

// Fraction of cases satisfied, with partial credit. Exact matches dominate;
// element-level agreement is a thousandth-weight term so that a population which
// solves nothing yet is not on a perfectly flat surface -- the failure that made
// an earlier organ unable to find a one-instruction solution in 2,048 births.
double score(const Program& p, const std::vector<Case>& cases, const Library* lib);

} // namespace khora::techne
