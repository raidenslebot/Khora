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
    Member,    // dst = [ x in b ? 1 : 0 for x in a ]     set membership
    Until,     // dst = prefix of a before the first element equal to b[0]
    Delta,     // dst = [ a[i+1] - a[i] ]                 neighbour comparison
    Call,      // dst = library[b](a)    a learned primitive
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
    std::string render() const;
    std::size_t size() const;      // nodes actually reachable from the root
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
BuildResult construct(const Spec& spec, std::size_t max_pool, const Library* lib = nullptr);

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

const char* lang_name(Lang l);
const char* lang_ext(Lang l);

// The operation set in the target language. Emit once per file.
std::string prelude(Lang l);

// Splice every library call into the recipe, so the result depends on nothing
// outside itself.
//
// This exists because library indices are NOT STABLE. Library::prune sorts by
// usage and truncates, which renumbers the survivors -- so a stored recipe
// holding `Call 3` silently comes to mean a different function after any prune.
// A self-contained recipe cannot rot that way, and it is also the only form that
// can be emitted as source that stands alone.
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
