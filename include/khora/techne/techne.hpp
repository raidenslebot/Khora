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
#include <functional>
#include <string>
#include <vector>

namespace khora::techne {

// A value is a list of integers. Scalars are one-element lists, which removes an
// entire class of type errors from the instruction set: every operation is
// defined on every value, so there is no such thing as an ill-typed program.
using Value = std::vector<std::int64_t>;

inline constexpr std::size_t kRegisters   = 4;
inline constexpr std::size_t kMaxListLen  = 512;   // memory bound

// EVERY VALUE IS CAPPED AT THIS, which is what makes the arithmetic total rather
// than carefully case-analysed. It lived in techne.cpp, so a caller writing a
// reference function had no way to respect the semantics its results would be
// compared against -- and throughput_bench's task generators duly did not,
// computing x*5 on the 1e9 edge probe and demanding 5e9 from an interpreter that
// saturates. Thirty programs were reported as "certified but a counterexample
// exists" on the strength of that. An oracle that cannot see the value bound
// cannot be an oracle for a system that has one.
inline constexpr std::int64_t kValueCap = 1'000'000'000;
inline std::int64_t cap_value(std::int64_t x) noexcept {
    return x < -kValueCap ? -kValueCap : (x > kValueCap ? kValueCap : x);
}
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
    // ---- BOUNDED CONDITIONALS -----------------------------------------------
    //
    // Measured, and this is the wall counterexample refinement ran into. 189 of
    // 400 tasks produced a program that is right in general and wrong on an
    // edge -- empty input, singleton input -- and NO amount of refinement fixes
    // them, because expressing "return nothing when the list is too short"
    // requires a conditional and there was not one. Refinement averaged 0.55
    // rounds against a ceiling of 6: it was not stopping because it had
    // succeeded, it was stopping because the answer was unreachable.
    //
    // These two do not reintroduce the halting problem. Neither loops, neither
    // branches control flow, and both evaluate both operands: they select
    // between already-computed values. Termination by construction is untouched.
    Arg,       // dst = argument k        -- a LEAF, like Const
    Guard,     // dst = b.empty() ? [] : a     -- a, but only if b is non-empty
    Else,      // dst = a.empty() ? b : a      -- a, or b when a has nothing
    // ---- THE FOUR THE TEXT BENCH NAMED --------------------------------------
    //
    // Not invented. Four string tasks failed there and I claimed each failure
    // had a specific missing capability behind it -- set membership, a scan that
    // stops at a delimiter, a conditional on a value range, and any comparison
    // between neighbouring elements. That was a DIAGNOSIS, and adding exactly
    // these four tests it: if count_vowels, first_word, title_case and
    // dedup_adjacent still fail, the explanation was wrong.
    //
    // All four are total and single-pass. None introduces a loop whose length is
    // not the length of its input, so termination by construction survives.
    Gt,        // dst = [ x > b[0] ? 1 : 0 for x in a ]   makes conditionals arithmetic
    // The rest of the comparison set. Gt existed alone, which is an odd place to
    // stop: with Eq and Lt, and Mul for masking and Sum for counting, ordinary
    // conditional logic becomes expressible.
    //
    // Added because reach was MEASURED FREE -- ScanMax and ScanMin went into
    // every level of every search and the ascent did not move by one program,
    // while adding a single atom cost twenty-one. Operations are the side of
    // this system that can be enriched without paying for it, so they get
    // enriched whenever there is an obvious hole.
    Eq,
    Lt,
    Member,    // dst = [ x in b ? 1 : 0 for x in a ]     set membership
    Until,     // dst = prefix of a before the first element equal to b[0]
    Delta,     // dst = [ a[i+1] - a[i] ]                 neighbour comparison
    // Prefix sums: the INVERSE of Delta, which the set had without it.
    //
    // Measured on the fixed bar: six of sixteen depth-TWO tasks were unsolved,
    // which cannot be a search problem at that depth -- it is a function the
    // operation set could not express at all. `scan` is one such, and an
    // operation set that carries differences without sums is asymmetric for no
    // reason beyond nobody having noticed.
    Scan,
    // Running maximum and minimum. The set had Sum, Max and Min as aggregators
    // and Scan as the running form of Sum, and no running form of the other two
    // -- the same asymmetry Delta had before Scan existed.
    //
    // These are here to test a criterion rather than to tune a number: an atom
    // helps the curriculum only if it is outside the span of the other ATOMS and
    // inside the span of the OPERATIONS. `cummax` was measured on the wrong side
    // of that line and cost a third of the ascent. This is the operation that
    // moves it to the right side, added together with the atom, because the
    // conclusion was that depth and reach have to move together.
    ScanMax,
    ScanMin,    Call,      // dst = library[b](a)    a learned primitive
    // ---- HIGHER ORDER: operations whose BODY is a learned function ----------
    //
    // Every operation above is one I wrote. `sum` is a primitive because I made
    // it one, and the self-hosting bench duly reported it IRREDUCIBLE -- no
    // composition of the others reproduces it, because none of the others can
    // express "combine every element". That is not a fact about arithmetic, it
    // is a fact about the instruction set having no way to build a LOOP.
    //
    // These two take a library function as their body, so the system composes
    // control structure rather than only values. With FoldF available, `sum` is
    // a fold whose body adds, `max` is a fold whose body takes the larger, and
    // the system can reach aggregations nobody enumerated -- including ones I
    // would not have thought to write.
    //
    // Termination is untouched, which is the whole reason the machine had no
    // loops. A fold visits each element of a list bounded at kMaxListLen exactly
    // once, and the body is depth-limited by kMaxCallDepth. There is no input on
    // which either fails to return.
    MapF,      // dst = [ library[b]([x]) for x in a ]   flattened
    FoldF,     // dst = left fold of library[b] over a, seeded with a[0];
               //       the body receives the pair as a two-element list
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
class Library;

// One node of a constructed expression. `a` and `b` index earlier pool entries;
// -1 means the input itself.
struct Expr {
    Op   op = Op::Mov;
    int  a  = -1;
    int  b  = -1;
    std::uint8_t k = 0;      // constant selector / library index

    // A LITERAL MINED FROM THE SPECIFICATION, when has_lit is set.
    //
    // The fixed constant table is a set I chose with no relationship to any
    // particular problem, and it showed: 32 is not in it, so every text task
    // needing a space had to build mul(4, 8) first. Values that appear in the
    // problem are a far better prior than my taste in round numbers.
    //
    // It is a separate field rather than a wider `k` because EVERY consumer has
    // to honour it -- evaluation, rendering and emission alike. A literal that
    // the search uses and the evaluator ignores would compute one thing during
    // the search and another on re-application, which is the silent divergence
    // this module has already shipped twice.
    std::int64_t lit = 0;
    bool has_lit = false;
};

// A constructed program. This is what the bottom-up engine produces, and it is
// the form the library stores, because the engine that actually solves things
// has to be the engine whose solutions become reusable.
struct Recipe {
    std::vector<Expr> pool;
    std::size_t root = 0;
    bool found = false;

    // `depth` guards library recursion. It is threaded rather than defaulted
    // internally because a recipe can call a library primitive that is itself a
    // recipe, and a depth that resets at each hop is not a bound at all -- the
    // same defect the tape machine had, which was a literal 1 at every call
    // site in a module whose whole claim is termination by construction.
    Value apply(const Value& in, const Library* lib, std::size_t depth = 0) const;
    // The general form. `apply(in, ...)` is this with a single argument, kept
    // because a library body is always called with exactly one.
    Value apply_n(const std::vector<Value>& args, const Library* lib,
                  std::size_t depth = 0) const;
    // Highest argument index the root can reach. 1 for every recipe built before
    // multiple arguments existed.
    std::size_t arity() const;
    std::string render() const;
    std::size_t size() const;      // nodes actually reachable from the root

    // Drop everything the root cannot reach.
    //
    // A recipe comes out of the search carrying THE WHOLE SEARCH POOL -- every
    // behaviour the enumeration ever considered, up to max_pool of them -- and
    // apply() evaluated all of it to return one node. A ten-node answer found in
    // a fifteen-thousand-node pool therefore cost fifteen hundred times what it
    // should, on EVERY application: every verification probe, every library
    // call, every emitted program. Measured on the ascent, verification was 80%
    // of the run and the search 13%.
    //
    // Node order is already topological -- a node only ever references earlier
    // ones -- so keeping the survivors in their existing relative order keeps it
    // topological, and the indices just need renumbering.
    Recipe compact() const;
};

struct Learned {
    std::string name;
    Program     body;          // tape form, from the evolutionary engine
    Recipe      recipe;        // constructed form, from the bottom-up engine
    std::size_t uses = 0;      // appearances in later certified solutions
    std::size_t born = 0;      // which task introduced it
};

class Library {
public:
    explicit Library(std::size_t budget = 32) : budget_(budget) {}

    // Admit a certified program. Returns false when it was rejected as a
    // duplicate of something already held.
    bool admit(std::string name, Program body, std::size_t task);

    // Admit a certified RECIPE. This is the one that matters: the constructive
    // engine is the engine that actually solves things (18/20 against 13/20 for
    // the evolutionary one), so unless its solutions can re-enter the library
    // nothing can ever compound. Before this existed the library was readable
    // and unwritable, and the measured compounding was exactly zero.
    bool admit_recipe(std::string name, Recipe r, std::size_t task);

    Value  call(std::size_t index, const Value& arg, std::size_t depth) const;
    std::size_t size() const noexcept { return items_.size(); }
    const Learned& at(std::size_t i) const { return items_[i]; }

    void note_use(std::size_t i) { if (i < items_.size()) ++items_[i].uses; }

    // The order the SEARCH should try bodies in, most promising first.
    //
    // NOT a reordering of items_. A certified recipe stores its library index in
    // Expr::k and evaluates through it later, so permuting the store would
    // silently change the meaning of every already-certified program holding a
    // Call -- the exact class of defect this module has now shipped twice.
    // Indices stay fixed forever; only the order they are TRIED in moves.
    //
    // Trying bodies 0..7 meant trying the OLDEST eight, which is the worst
    // available prior: old entries were learned from shallow tiers, and deep
    // tasks need deep parts. Uses then accrue only to entries that get tried, so
    // prune() kept whichever eight were visible and discarded the rest
    // unexamined -- utility measured through a window that decided utility.
    //
    // Ties break toward the NEWEST, because an entry admitted at the deepest
    // tier so far is the one most likely to be a part of the next tier down.
    // The order the SEARCH should try bodies in, most promising first.
    //
    // NOT a reordering of items_. A certified recipe stores its library index in
    // Expr::k and evaluates through it later, so permuting the store would
    // silently change the meaning of every already-certified program holding a
    // Call. Indices stay fixed; only the order they are TRIED in moves.
    //
    // Returned BY REFERENCE from a precomputed member. The order changes only
    // when the library does, so it is rebuilt where the library is written --
    // never in the search path, which runs it at every level of every construct
    // on every worker.
    const std::vector<std::size_t>& search_order() const noexcept { return order_; }

    // Evict the least used when over budget. A library that only accretes turns
    // into a haystack the search has to hunt through, which makes later problems
    // HARDER -- the same unbounded-growth failure that made TemporalMemory
    // allocate 2.9M segments for 24k tokens.
    std::size_t prune();

    std::size_t evicted() const noexcept { return evicted_; }

    // PERSIST WHAT IT LEARNED. Without this the library is built, filled and
    // thrown away at process exit, so nothing about PROGRAMMING compounds across
    // runs -- every benchmark in this tree starts from nothing and the system
    // that is supposed to improve itself forgets between sessions.
    //
    // Written as text with operations named rather than numbered. This class has
    // twice shipped a defect caused by an index that meant something different
    // later -- prune permuting items_, and Expr::k pointing at a renumbered
    // entry -- and an enum ordinal in a file on disk is that same mistake with a
    // longer fuse: adding one operation would silently reinterpret every stored
    // program. A name either resolves or fails loudly.
    bool save(const std::string& path) const;
    bool load(const std::string& path);

private:
    std::size_t budget_;
    std::vector<Learned> items_;
    std::size_t evicted_ = 0;
    std::vector<std::size_t> order_;
    void reorder();
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
// One example. `in` is the FIRST argument and `extra` carries the rest.
//
// Until now this was `Value in, out` and nothing else, which meant every program
// this system could express was a UNARY function of one integer list. That is
// the deepest limit in the tree: most programs a person writes take more than one
// argument, so "can write anything" was false for a reason no amount of search
// speed touches.
//
// `extra` is separate rather than folding the first argument into a vector so
// that every existing specification, benchmark and test keeps working unchanged;
// a unary case is one with `extra` empty, which is what all of them are.
struct Case {
    Value in, out;
    std::vector<Value> extra;

    Case() = default;
    Case(Value a, Value o) : in(std::move(a)), out(std::move(o)) {}
    Case(Value a, std::vector<Value> rest, Value o)
        : in(std::move(a)), out(std::move(o)), extra(std::move(rest)) {}

    std::size_t arity() const noexcept { return 1 + extra.size(); }
    const Value& arg(std::size_t i) const { return i == 0 ? in : extra[i - 1]; }

    // Every argument, in order. The form Recipe::apply_n wants.
    std::vector<Value> args() const {
        std::vector<Value> v;
        v.reserve(arity());
        v.push_back(in);
        for (const Value& e : extra) v.push_back(e);
        return v;
    }
};

struct Spec {
    std::string name;
    std::vector<Case> cases;          // what the caller asked for
    std::vector<Case> holdout;        // cases the search never sees

    // OPERATIONS THE SEARCH MAY NOT USE.
    //
    // This exists for self-hosting. Asking the system to synthesise `sum` while
    // `sum` is available is answered by `sum(x)` and proves nothing at all. Ban
    // the primitive and the answer has to be a composition of the others, which
    // is the only version of the question worth asking: can this system rebuild
    // its own operations out of its remaining ones?
    std::vector<Op> banned;
    bool allows(Op o) const {
        for (const Op b : banned) if (b == o) return false;
        return true;
    }
};

// HOW A RESULT WAS VERIFIED. A caller gets this or nothing.
enum class Proof {
    None,        // no program satisfied the specification
    Tested,      // passes every visible case
    Generalised, // passes every visible case AND every held-out case
    // EXHAUSTIVE: checked on EVERY input in a stated finite domain, so within
    // that domain there is nothing left to find.
    //
    // This is the only level that is a proof rather than evidence. Verified
    // below means an adversary searched and failed, which is strong and is still
    // sampling: it cannot distinguish "no counterexample exists" from "none was
    // drawn". Enumerating a bounded domain can. Lists of length up to 4 over
    // five distinct values is 781 inputs -- small enough to check completely,
    // large enough to contain every shape that breaks a fitted program.
    //
    // The domain is part of the claim and is always reported with it. A program
    // exhaustive over short lists of small integers is proved for short lists of
    // small integers, and nothing more is asserted.
    Exhaustive,
    // VERIFIED: an adversary was given the program and could not find an input
    // on which it disagrees with the oracle.
    //
    // Generalised is a statement about a fixed sample. Verified is a statement
    // about a search: a counterexample hunter ran against the candidate and
    // failed. That is strictly stronger, and it is the only level that can
    // honestly be called guaranteed -- over the domain the hunter covers.
    Verified
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
// BOTTOM-UP CONSTRUCTION, because a mutation search over whole programs could
// not solve compositional tasks at the budget it was given.
//
// THIS PARAGRAPH USED TO SAY "a gradient search CANNOT solve compositional tasks
// and no amount of tuning will change that", and that is too strong. It was
// written from one measurement of one algorithm. A later bench put hill climbing
// WITH NEUTRAL-MOVE ACCEPTANCE against this enumerator on fifteen tasks judged by
// this module own generalisation rule, and at a hundred thousand candidates each
// the climber scored 36/45 [66.2, 89.1] against enumeration 11/15 [48.0, 89.1].
// The intervals overlap: no difference is established, so "cannot" and "never"
// are not supported.
//
// The landscape argument below is still right about WHY the original search
// failed, and the flatness is real -- 92.7% of a hundred thousand uniform tapes
// sit at a single modal score. What it got wrong is the conclusion. A climber
// that ACCEPTS NEUTRAL MOVES walks across a plateau instead of stalling on it,
// which is the one thing the mutation search was not doing.
//
// What enumeration actually keeps is EFFICIENCY, and that is measured and large:
// at a budget matched to enumeration own node count it wins 11/15 against the
// climber 16/45, with intervals that clear, and it spends a median of 340 nodes
// where the climber needs a hundred thousand candidates to draw level. The
// currency is biased against enumeration on top of that -- one node applies a
// single operation to cached operand behaviours, one tape candidate runs a whole
// program on every case.
//
// Fifteen tasks at depth one to three cannot rank two search strategies, and
// neither engine reached depth six. The honest claim is "far cheaper per solution
// on these tasks", not "the only thing that works".
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
// which is what a mutation operator has to stumble onto instead.
// ---------------------------------------------------------------------------

struct BuildResult {
    Recipe recipe;
    Proof  proof = Proof::None;
    std::size_t cases_passed = 0, cases_total = 0;
    std::size_t holdout_passed = 0, holdout_total = 0;
    std::size_t distinct_behaviours = 0;   // the pool the search actually explored
    std::size_t nodes_considered = 0;      // candidates before dedup
    bool certified() const noexcept { return proof != Proof::None; }
};

// ---------------------------------------------------------------------------
// BIDIRECTIONAL SEARCH — attacking the exponent instead of the constant.
//
// Bottom-up construction explores every behaviour reachable from the input, one
// level at a time. Its cost is exponential in depth, and every previous cycle in
// this project responded to that by buying a bigger pool: depth 3 needed 36,854
// entries for `max_minus_min`, so the cap went up. That is optimising the
// CONSTANT FACTOR ON AN EXPONENTIAL, and it does not end well -- depth 4 needs
// millions, and the bounded conditionals added for edge cases turned out to be
// unreachable in principle at any pool size this machine will hold.
//
// The way out is not more budget. It is to stop searching in only one direction.
//
// OPERATIONS ARE INVERTIBLE. If the target is add(A, B) and one operand is known,
// the other is DEDUCED: B = sub(target, A). If the target is rev(A) then
// A = rev(target), exactly. If it is mapmul(A, 3) then A = target/3 wherever
// that divides. These are not guesses; they are the unique preimages.
//
// So run two searches. FORWARD from the input, accumulating behaviours that are
// reachable. BACKWARD from the target, accumulating GOALS -- behaviours which,
// if some expression achieved them, would yield the target after a known
// wrapper. A solution exists the moment a forward behaviour equals a backward
// goal, and the program is read off by composing the two halves.
//
// For a solution of depth d, forward reaches d/2 and backward reaches d/2, so
// the cost falls from O(b^d) to roughly O(b^(d/2)). That is a win on the
// EXPONENT, which is a different kind of thing from a bigger pool.
//
// SOUNDNESS IS NOT ASSUMED. An inverse that is only partially valid -- division
// where the target does not divide evenly, sort whose preimage is any
// permutation -- can propose a goal that is wrong. Every assembled candidate is
// therefore re-checked against every case before it is returned, so an unsound
// inverse costs wasted work and can never produce a wrong answer.
// ---------------------------------------------------------------------------
BuildResult construct_bidir(const Spec& spec, std::size_t max_pool,
                            const Library* lib = nullptr);

// Build a program by composition. `max_pool` bounds the number of DISTINCT
// behaviours retained, which is the real memory cost.
// `mine_constants` decides whether level 0 is seeded with values drawn from the
// specification as well as the fixed table.
//
// A knob rather than always-on, because it is not free. Mining takes level 0
// from 17 entries to about 41, and a binary level is quadratic in that: ~5,200
// candidates becomes ~30,000, roughly six times the work per level. Measured,
// that turned a throughput run finishing in minutes into one that did not finish
// at all.
//
// The policy that follows is the one that already worked for the two search
// engines: run the cheap configuration first, spend the expensive one only on
// what survives it. Most tasks never need a mined constant, and the ones that do
// are exactly the ones a first pass fails.
BuildResult construct(const Spec& spec, std::size_t max_pool,
                      const Library* lib = nullptr,
                      bool mine_constants = true);

// With the library, and then WITHOUT it if that failed.
//
// A library is a vocabulary and a haystack at once: every entry is another
// level-0 candidate competing for a bounded pool. That is not hypothetical --
// `sort.delta`, which is `Delta(Sort(x))` and two operations deep, is solved
// with an empty library and NOT solved with a ninety-six entry one. On a fixed
// ninety-six task bar, three tasks were lost that way while eight were won, and
// the totals hid it because the wins are larger.
//
// Keeping the better of the two answers cannot be worse than either. The second
// search runs only for tasks that already failed -- the ones with budget to
// spare -- and it converts "the library usually helps" into "the library cannot
// hurt". That difference matters more than the average does.
BuildResult construct_best(const Spec& spec, std::size_t max_pool,
                           const Library* lib = nullptr,
                           bool mine_constants = true);

// EVERY INDEPENDENT ATTEMPT AT ONCE, resolved by a fixed preference order.
//
// A task is currently attempted forward, then bidirectionally if that failed,
// then forward without the library if that failed too. Three searches, run one
// after another, on one core -- and they do not depend on each other at all.
// Each reads the same const Library and writes nothing shared.
//
// Running them concurrently and choosing by a FIXED ORDER rather than by who
// finishes first keeps the answer identical to the sequential version: the
// result is a function of the specification, not of the scheduler. It costs
// three cores instead of one and returns in the time of the slowest rather than
// the sum, which on this workload is where capability comes from -- the ascent
// lost three tiers inside a fixed time budget when the fallback was added, and
// this is that budget back.
BuildResult solve_one(const Spec& spec, std::size_t max_pool,
                      const Library* lib = nullptr);

// ---------------------------------------------------------------------------
// EMISSION: a recipe becomes real source in a real language.
//
// Until this existed the organ produced expression trees, and "10,000 lines of
// code" was not a measurable claim about it -- a recipe is not lines. Emission
// makes the throughput target countable, and it is also the honest form of
// "every language": a backend per target, each defining the same operations in
// that language's own idiom, rather than an assertion that the design is
// language-agnostic.
//
// Each backend emits a PRELUDE (the operation set, written once per file) and a
// FUNCTION per recipe, in static-single-assignment form so every step is a
// readable line rather than one unreadable nested expression. The prelude is
// fixed cost; the function is the synthesised part, and the two are counted
// separately so the line rate cannot be inflated by boilerplate.
// ---------------------------------------------------------------------------
// A backend is a claim, not a formality: each one has to reproduce the value
// cap, the empty-list results, the zero-guard on division and the cycling
// shorter operand in that language's own arithmetic. Most of these targets get
// truncating division from the language; Python, Ruby, Lua and Haskell all
// FLOOR by default and have to have it built, which is a difference that stays
// invisible until a negative dividend shows up.
enum class Lang {
    Cpp, Python, JavaScript, Rust,
    Go, Java, CSharp, TypeScript, Ruby, Lua, Haskell, Swift, Kotlin, Php
};

// Operation names, and back again. The name is the stable identifier; the
// enum ordinal is not, and must never reach a file or a wire.
// WHICH OPCODES READ A SECOND OPERAND. One definition, because there were two.
//
// The interpreter used a `binary()` in an anonymous namespace to decide how many
// registers apply_op reads, and the emitter kept its own `is_binary()` with a
// comment saying it mirrored that list and that "an operation classified
// differently in the two places would emit a call with the wrong arity". Eq and
// Lt were added to the interpreter and not to the emitter, so every emitted
// program containing either called a two-argument function with one argument --
// in all fourteen languages, for two years of operations being added. A comment
// asking two lists to agree is not a mechanism that makes them agree.
bool reads_two_operands(Op o);

const char* op_name(Op o);
bool op_from_name(const std::string& n, Op& out);

const char* lang_name(Lang l);
const char* lang_ext(Lang l);

// The operation set in the target language. Emit once per file.
std::string prelude(Lang l);

// Splice every library call into the recipe, so the result depends on nothing
// outside itself.
//
// A self-contained recipe is the only form that can be emitted as source that
// stands alone -- emitted code cannot reach back into a Library that exists only
// in this process.
//
// It also used to be the only form that could not ROT. Library::prune sorted
// items_ in place and truncated, renumbering the survivors, so a stored recipe
// holding `Call 3` silently came to mean a different function after any prune.
// That is fixed -- prune keeps the age-ordered prefix and remaps every surviving
// recipe's indices -- but inlining is still what makes an emitted program depend
// on nothing outside itself.
Recipe inline_calls(const Recipe& r, const Library& lib);

// One recipe as one function. `lines` receives the number of body lines, which
// is the synthesised output rather than the boilerplate.
//
// Pass the library whenever the recipe might contain Op::Call. Emission inlines
// them; it does not have the option of ignoring them, because a call that is
// dropped makes the emitted source a different program from the certified one.
std::string emit(const Recipe& r, Lang l, const std::string& fn,
                 std::size_t* lines = nullptr, const Library* lib = nullptr);

// A COMPLETE COMPILABLE UNIT: the operation set, then every library body the
// recipe transitively needs, then the recipe itself.
//
// This exists because emit() hands back ONE function, and a higher-order node
// does not name a value -- it names a BODY. `fold[lib3](x)` is only meaningful
// beside the source of lib3, so emit() refuses MapF and FoldF and this does not.
// Each needed body is emitted as `kh_lib<i>`, where i is the library index the
// interpreter would resolve (`k % lib->size()`), and the bodies come out before
// anything that folds over them.
//
// `lines` receives the synthesised line count -- every emitted body plus the
// recipe, prelude excluded, on the same terms emit() counts by.
//
// Returns the EMPTY STRING rather than partial source when the unit cannot be
// built honestly:
//   * a needed primitive is a tape program rather than a recipe, which has no
//     source form here;
//   * or the nesting reaches kMaxCallDepth, where the interpreter yields an
//     empty list and no single emitted function can reproduce a result that
//     depends on how deep its caller was.
std::string emit_unit(const Recipe& r, Lang l, const std::string& fn,
                      const Library* lib, std::size_t* lines = nullptr);

// ---------------------------------------------------------------------------
// SOLVING TO FIXPOINT: the loop that makes the system improve itself.
//
// Measured: a single pass over 2,000 generated tasks certifies 1,100 of them.
// The 900 failures are not all unreachable -- many are compositions of things
// the system learns LATER in the same pass, and were attempted before the
// component existed. Order of attempt should not decide what is solvable.
//
// So the pass repeats. Every certified solution enters the library, and every
// task that failed is tried again against the larger library, until a whole
// round produces nothing new. That fixpoint is the honest definition of "as much
// as this system can currently do", and the per-round curve is the evidence of
// compounding: a flat curve means the library is not helping, and a curve that
// keeps producing means capability is still growing.
//
// This is also the shape of unbounded self-improvement, with the one property
// that makes it safe to run unattended -- it terminates. A round that certifies
// nothing new cannot be followed by a round that does, because nothing changed.
// ---------------------------------------------------------------------------
// HOW MANY WORKERS TO RUN, AND WHY IT IS NOT ALL OF THEM.
//
// Saturating every core makes the machine unusable while a benchmark runs, and a
// benchmark that has to be babysat is one nobody leaves running. A stale
// throughput run here reached 77 minutes and 14,297 CPU-seconds before it was
// noticed, on a box someone was trying to work on.
//
// The cap is a FRACTION of the cores, not a fixed count, so it travels to
// machines with a different core count without becoming either wasteful or
// oppressive. 0.75 leaves a quarter of the machine for everything else, which
// on 24 cores is six cores of headroom -- enough that an editor, a compiler and
// a shell all stay responsive.
//
// This is a ceiling, not a target. A caller that wants fewer says so.
inline constexpr double kCpuFraction = 0.75;

std::size_t worker_threads(double fraction = kCpuFraction);

struct SolveConfig {
    std::size_t pool_cap   = 3000;
    std::size_t lib_budget = 24;
    std::size_t threads    = 0;      // 0 = every hardware thread
    std::size_t max_rounds = 16;     // a ceiling, not the usual stopping reason
    // Later rounds may deepen the pool: a task that failed at 3,000 behaviours
    // with an empty library can be worth more budget once the library is rich,
    // because the same budget now reaches further.
    double      deepen     = 1.5;
};

struct SolveStats {
    // Thermal behaviour of the run, so a run that throttled and a run that never
    // did cannot look identical afterwards.
    double      thermal_peak_c  = -1.0;
    std::size_t min_workers     = 0;    // narrowest the pool was driven
    std::size_t throttle_events = 0;

    std::size_t rounds = 0;
    std::vector<std::size_t> solved_per_round;
    std::size_t certified = 0, attempted = 0, memorised = 0;
    std::size_t library_size = 0, library_calls = 0;
    std::size_t nodes = 0;
    double seconds = 0.0;
};

// ---------------------------------------------------------------------------
// COUNTEREXAMPLE-GUIDED SYNTHESIS.
//
// Measured: at 12 visible cases, 249 of 2,000 tasks produced a program that
// passed every case it was shown and failed a held-out one. Adding cases helps
// (333 at six cases, 249 at twelve) but it can never GUARANTEE, because it is
// still sampling: any finite set of examples leaves behaviours consistent with
// all of them and wrong everywhere else.
//
// So stop sampling and start ADVERSARIALLY SEARCHING. Synthesise a candidate,
// then hunt for an input on which it disagrees with the oracle. If one is found
// it becomes a new constraint and the search runs again; if the hunt fails, the
// program is Verified.
//
// This is legitimate rather than a shortcut, because real programming HAS an
// oracle. Tests, assertions, type checks and property predicates are all things
// that can say "wrong on this input" without being able to say what the right
// program is. That is exactly the interface below.
//
// An Oracle gives the reference behaviour for an input. That is what a test
// suite, a specification, a property checker or a previous implementation all
// are -- something that can say what SHOULD happen without being able to say
// what program should do it.
//
// A Prober supplies inputs to try. It is where knowledge about hard cases
// lives, and a lazy one that only draws uniformly at random will miss the edges
// every time -- empty, singleton, all-equal, sorted, reverse-sorted and extreme
// inputs are exactly where a program fitted to mid-sized random lists breaks.
using Oracle = std::function<Value(const Value& in)>;
using Prober = std::function<Value(std::size_t i)>;

struct Verification {
    bool verified = false;
    std::size_t probes_run = 0;
    std::size_t rounds = 0;              // counterexamples fed back
    Value counterexample;                // the last one found, if any
};

// ---------------------------------------------------------------------------
// EXHAUSTIVE CHECKING over a finite domain.
//
// Every input of length 0..max_len drawn from the value range [lo, hi]. The
// count is (hi-lo+1)^0 + ... + (hi-lo+1)^max_len, reported alongside the result
// so the strength of the claim is never left implicit.
//
// Used as the counterexample hunter this makes refinement COMPLETE over the
// domain: a round that finds nothing has established that nothing is there,
// which no amount of random probing can establish.
// ---------------------------------------------------------------------------
struct Exhaust {
    bool clean = false;              // agreed with the oracle on every input
    std::size_t checked = 0;         // size of the domain actually enumerated
    Value counterexample;            // the first disagreement, if any
};

Exhaust check_exhaustive(const Recipe& r, const Library* lib, const Oracle& oracle,
                         std::int64_t lo, std::int64_t hi, std::size_t max_len);

// Synthesise, then refine against counterexamples drawn from the whole finite
// domain rather than sampled from it. When it returns Proof::Exhaustive, the
// program has been checked on every input in that domain.
BuildResult synthesise_exhaustive(Spec spec, std::size_t pool_cap,
                                  const Oracle& oracle,
                                  std::int64_t lo, std::int64_t hi,
                                  std::size_t max_len, std::size_t rounds,
                                  const Library* lib = nullptr,
                                  Exhaust* out = nullptr);

// THE STRONGEST VERIFICATION THIS MODULE OFFERS, and the reason it is not just
// the exhaustive one.
//
// A proof over a bounded domain does not survive outside that bound, and the
// measurement is not subtle. On 36 tasks whose behaviour changes only outside a
// domain of every list of length 0..4 over -2..2 -- take the fifth element, clamp
// at three, keep values past two -- exhaustive checking accepted 36 of 36 and got
// ZERO right on held-out inputs. It was not lying: it genuinely checked all 781
// inputs it said it checked. The domain simply did not contain the inputs the
// program would meet.
//
// Worse, it lost to random probing there, 0/36 against 6/36, for one reason: a
// prober draws {1000000000} and a proof over -2..2 never looks there.
//
// So this keeps the exhaustive pass and adds a hunt over the EXTREMES of the
// range the program is actually for -- zero, one, both signs, values past the
// domain, the value cap, and lengths past every example it was shown -- refining
// on anything found and RE-PROVING the small domain each time, so the two
// guarantees hold together rather than in sequence.
//
// Measured over 276 tasks against a wilderness of 213 held-out inputs outside the
// domain in both directions:
//
//     certified (Generalised)   271 accepted   84.1% right
//     + 300 random probes       272 accepted   89.0% right
//     + exhaustive proof        274 accepted   86.1% right
//     + proof AND extremes      260 accepted  100.0% right   [98.5, 100.0]
//
// It accepts FEWER and is right about everything it accepts. That is the trade,
// and it is the right way round: a synthesiser that says yes to 274 things and is
// wrong about 38 of them is worse than one that says yes to 260 and is wrong
// about none, because only the second can be built on.
//
// IT IS STILL NOT A PROOF and must not be described as one. It is a bounded proof
// plus a strictly better sample. The honest claim is "proved on the small domain
// and unbroken at the edges of the large one". A real guarantee needs a decision
// procedure over the program TEXT rather than its inputs.
//
// Returns Proof::Exhaustive only when BOTH halves pass. Anything less is reported
// as whatever the weaker check earned.
BuildResult synthesise_hardened(Spec spec, std::size_t pool_cap,
                                const Oracle& oracle,
                                std::int64_t lo, std::int64_t hi,
                                std::size_t max_len, std::size_t rounds,
                                const Library* lib = nullptr,
                                Exhaust* out = nullptr);

// The extremal inputs the hardened check hunts over: the boundary cases a fitted
// program breaks on. Exposed because a caller whose deployment range is not this
// one should supply its own, and because a hidden list is a hidden assumption.
const std::vector<Value>& default_extremes();

// Synthesise, then refine against counterexamples until none can be found.
// `probes` bounds the hunt; `rounds` bounds how many counterexamples are fed
// back before giving up.
BuildResult synthesise_verified(Spec spec, std::size_t pool_cap,
                                const Oracle& oracle, const Prober& prober,
                                std::size_t probes, std::size_t rounds,
                                const Library* lib = nullptr,
                                Verification* out = nullptr);

// Solve every specification, to fixpoint, across every core.
std::vector<BuildResult> solve_all(const std::vector<Spec>& specs,
                                   SolveConfig cfg, SolveStats* stats = nullptr);

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
