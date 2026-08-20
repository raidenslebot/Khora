# Khora Architecture

**Author:** Morphus
**Version:** 0.115.0
**Basis:** a read of the working tree at `C:\Khora`. Every structural claim below
was checked against the source; every number was either measured this session or
is a constant lifted from the code it describes.
**Language:** C++20, MSVC v143 primary, Ninja generator
**Target hardware:** Intel Core i7-13700K, 32 GB DDR4, RTX 2070 SUPER, Windows 10/11

---

## 0. How to read this

The previous revision of this file was written at v0.1.0 and never revised. By
v0.114 it described a system that did not exist: it called the Lattice "the only
subsystem currently implemented" when 22 others were built, marked Cortex, Soma,
Reverie, Synapse and Carapace as "planned" when all five were under `ctest`,
pointed at a demo file that had been deleted, and put the data directory on a
drive path that was never used. This revision replaces it.

The project rule is that nothing is claimed unless it has been built, run and
observed. That rule applies to this document too, so the vocabulary is fixed:

| Word | Meaning |
|---|---|
| **built** | The code exists, compiles into the default build, and something exercises it. |
| **wired** | The runtime binary `khora.exe` actually calls it during normal operation. |
| **measured** | A benchmark in `bench/` produced a number for it. The number is quoted. |
| **unverified** | Stated in the source but not checked here. Flagged inline every time. |
| **not built** | No code. Named only so nobody mistakes the intent for the artifact. |

Built and wired are different, and the difference is the most important thing
this document has to say. See §4.

Current shape: **23 modules under `src/`, 20 tests registered with `ctest` (all
passing), 12 benchmark binaries.** 16,877 lines across `src/` and `include/`;
22,503 including `tests/` and `bench/`.

---

## 1. What Khora is

A vector-symbolic cognitive substrate. No transformer, no gradient descent, no
token prediction, no model weights of any kind. The computational primitives are
bitwise operations over fixed-width binary vectors, plus an explicit weighted
graph and a pattern-matching relation extractor.

Four commitments shape everything:

1. **No language model, anywhere in the stack.** Not as a component, not as a
   fallback. Consequence: Khora cannot generate fluent prose on arbitrary
   subjects, and never will. That is the cost, paid deliberately.
2. **One machine.** No cloud, no remote API, no telemetry. Everything runs in one
   process on one PC, under a hard RAM cap the operator sets.
3. **Mechanistic traceability.** Every output reduces to a finite chain of vector
   operations over named, inspectable state. The Temporal Memory takes this
   further and can name the specific stored episodes responsible for a given
   prediction, then delete them and show the prediction change (§6.2).
4. **Measurement over assertion.** A capability is real when a benchmark says so,
   and results that go the wrong way are recorded with the same weight as the
   ones that go the right way. Several are recorded below.

### What Khora is not

- Not a chatbot or an LLM wrapper.
- Not competitive with frontier models on general benchmarks. It is not trying to
  be, and the architecture forecloses it.
- Not a consciousness project. There is no consciousness module, and none is
  planned. Where the source uses words like "mind" or "thought" they are names
  for data structures, not claims about them.

---

## 2. The two-substrate commitment

This is the central architectural decision and the one most likely to be
misunderstood, so it comes first.

Khora carries **two binary vector types with different densities, different
algebras, and a deliberately one-way bridge between them.** This is not
transitional. Neither is scheduled to replace the other.

### 2.1 `Glyph` — the dense code

`include/khora/lattice/glyph.hpp`

| Property | Value |
|---|---|
| Width | 10,000 bits = 157 × `uint64_t` = 1,250 bytes |
| Density | ~50% of bits set |
| **bind** | `a XOR b` — self-inverse, distance-preserving |
| **bundle** | majority vote across inputs; even-arity ties broken by a glyph derived from XOR-folding the operands, so the tiebreak is deterministic, commutative, and decorrelated across operand sets |
| **permute** | cyclic shift by k bits |
| Position marking | `position_glyph(k)` — a cached orthogonal family; binding against it is the word-parallel alternative to `permute` |
| Similarity | Hamming distance, POPCNT |

XOR binding is an involution on (ℤ/2)^D and a Hamming isometry, so unbinding
introduces exactly zero noise. All retrieval error in the relational path comes
from the bundle, none from the unbind. This is a genuine advantage that no
sparse binding operator improves on, and it is why the dense code stays.

A note on history: `bundle()` was for a long time not a majority vote. The
threshold was `(n+1)/2`, which for even arity resolves ties toward SET, and at
n = 2 degenerated into a bitwise OR — so "superposing" two 50%-dense glyphs
produced a 75%-dense one. Three layers inherited the bug (Lexicon, Reverie,
Cogitator). It is fixed. It is recorded here because every measurement taken
before the fix is void, and because the same misunderstanding — *a set is not a
vote* — has now cost this project three separate mechanisms.

### 2.2 `Sdr` — the sparse code

`include/khora/lattice/sdr.hpp`

A sparse **block code** (Laiho et al. 2015; Frady, Kleyko & Sommer, IEEE TNNLS
2023). Positions are partitioned into blocks with exactly one active position per
block, so sparsity is *structural* — it cannot drift, and no inhibition or
k-winners step is needed to enforce it.

| Property | Value |
|---|---|
| Layout | 256 blocks of 64 → 16,384 positions, 256 active = 1.5625% |
| Storage | `std::array<uint8_t, 256>` — one block index per block, 256 bytes total |
| **bind** | per-block `(i + j) mod 64` — a mask, not a division |
| **unbind** | per-block `(i − j) mod 64`. Not self-inverse, so this is a separate operation |
| **permute** | rotate the block *order*, preserving one-active-per-block |
| Similarity | count of blocks whose active index agrees. Two unrelated Sdrs agree on B/L = 4.0 blocks on average |
| **bundle** | `Trace` accumulates `int16` counts and `binarise()` commits by per-block argmax. A loaded bundle must never be binarised mid-accumulation |
| **union** | `SdrUnion` — one `uint64` mask per block, i.e. a *set* of active positions |
| **subsampled match** | `Segment` — 24 (block, expected index) pairs, matching at θ = 12 |

At L = 2 a block is one bit, `(i+j) mod 2` is XOR and per-block argmax is the
majority rule — so the dense algebra is the degenerate case of this one, kept as
its own optimised implementation.

### 2.3 Why both — the one number the split rests on

A dendritic segment stores a small sample of a pattern and fires when a threshold
of those samples agree. It never sees the whole pattern. The false-match
probability of that test against unrelated input is

```
P(Binomial(s, density) >= theta)
```

At s = 24, θ = 12:

| density | false-match rate |
|---|---|
| **50% (dense Glyph)** | **0.58 — at every dimension from 2,000 bits to 65,536** |
| 1.5625% (sparse Sdr) | ~1e-16 |

Measured over 400,000 probes: **dense 0.5764, sparse 0 of 400,000.**

Widening the vector does not help, because a coin-flip bit meets a half-threshold
by chance. **Sparsity, not dimensionality, is what makes subsampling work** — and
subsampling is what makes distal context, unions of simultaneous predictions, and
tolerance of large-scale unit loss possible at all. Everything in §6.2 is
downstream of this single number.

The split follows the complementary-learning-systems boundary rather than cutting
across it: `Sdr` is the fast, sparse, pattern-separating store; `Glyph` is the
slow, dense, overlapping semantic one.

### 2.4 The bridge is one-way, on purpose

`project(const Glyph&) -> Sdr` is a fixed pseudo-random projection to per-block
scores followed by argmax. **There is deliberately no `Sdr -> Glyph`.** The two
codes carry different invariants, and a reverse projection is precisely how a
two-substrate design decays into two half-correct algebras. This is the
architecture's stated top risk; the only mitigation is that the function does not
exist.

### 2.5 Both bind operators are commutative — so neither expresses direction

`bind(a,b) == bind(b,a)` for XOR and for per-block modular addition alike, and
from the result either operand recovers the other. **A chain of bound transitions
is therefore a set of undirected edges, and traversing it is a coin flip between
successor and predecessor.** This is not a defect in one implementation; it is a
property of commutative binding, and it applies to both substrates equally.

Direction requires permuting one operand before binding. This was found the
expensive way: the Whetstone's transitive faculty scored 66.67% — exactly the
chance rate for a three-way coin flip — until the transition encoding was made
directed, after which it scored 100%.

---

## 3. Module map

23 libraries and executables, in the order the root `CMakeLists.txt` adds them.

| Module | What the code does | Wired into `khora.exe`? |
|---|---|---|
| `lattice` | `Glyph`, `Sdr`, labelled store with linear-scan Hamming k-NN, binary persistence | yes (Glyph); **Sdr: no** |
| `synapse` | Topic pub/sub with per-subscriber bounded queues and drop counting | **no — see §9** |
| `cortex` | `PredictiveColumn` (dense, context→next) and `TemporalMemory` (sparse, per-cell context) | PredictiveColumn only; **TemporalMemory: no** |
| `soma` | Five scalar drives with homeostatic decay; dot-product arbitration | yes |
| `reverie` | Background perturb-bundle-evaluate loop over stored glyphs | yes |
| `lexicon` | Char-trigram token glyphs; random-indexing distributional context; sentence-aware tokenizer | yes |
| `plexus` | Weighted co-occurrence graph scored by smoothed PPMI | yes |
| `ligature` | Pattern-extracted typed relations (is-a / causes / has-part) + forward chaining | yes |
| `crystallize` | Votes new is-a relations out of Plexus kin agreement | yes |
| `hand` | Win32 process execution with pipe capture. Uncontained, timeout-governed | yes |
| `bulwark` | Job Object + low-integrity non-admin token cage, fail-closed | yes |
| `maw` | Chaos command exploration, contained; opt-in, dormant by default | yes (dormant) |
| `maelstrom` | D3D11 DirectCompute k-NN over glyphs, CPU fallback | yes |
| `cogitator` | ~2.6k-line cognition facade: resolve loop, parallel facets, cascade, transmute, abstraction tower | yes |
| `crucible` | Role-filler binding, structured unbind, analogy, holographic capacity | via Whetstone |
| `whetstone` | Self-escalating faculties over Crucible + PredictiveColumn | yes |
| `reservoir` | LZSS codec, text distillation, byte-capped tiered store, WinHTTP acquisition | yes |
| `curator` | Study / forage / deepen scheduling over the Reservoir | yes |
| `lodestone` | Hardware gauge → operating profile (caps, cadences) | yes |
| `ballast` | RSS + system-RAM governor; sheds cortex and lexicon under pressure | yes |
| `volition` | Drive-weighted act selection over a repertoire | yes (on demand) |
| `carapace` | Tool registry and command dispatch | yes |
| `morphus` | `khora.exe`, `khora_next`, `plexus_forge`, `bulwark_probe`, `reforge_eval` | — |

Dependency edges are direct C++ references declared in each module's
`CMakeLists.txt`. `lattice` is the only module nothing depends on upward;
`plexus` and `ligature` are pure standard C++ with no substrate dependency at
all.

---

## 4. Data flow — and the divergence that matters most

There are two paths through this codebase, and they do not meet.

### 4.1 The runtime path — what `khora.exe` actually runs

```
    text (Reservoir tome, or operator input)
              |
       tokenize / tokenize_sentences        [lexicon]
              |
      +-------+---------+------------------+
      |                 |                  |
   Lexicon           Plexus             Ligature
 (trigram glyph    (co-occurrence      (typed relations
  + random-index    counts, PPMI)       by pattern)
  context)              |                  |
      |                 +-----> Crystallize +
      |                             (is-a votes)
      v
  Glyph  ---->  Lattice (labelled store, linear k-NN)
      |               ^                    |
      |               |            Maelstrom (GPU k-NN,
      v               |             same distances)
  PredictiveColumn ---+                    |
  (context window 3, ctx->next)            v
      |                                Cogitator
      |                            (resolve loop, facets,
      +--------> Reverie             cascade, abstraction
                (perturb, bundle,     tower, inference)
                 evaluate, retain)         |
                        ^                  v
                    Soma Nexus  <----> Volition ----> Carapace ----> Hand
                   (5 drives)          (acts)         (91 tools)     Bulwark -> Maw
                        ^
                   Whetstone (faculties: relational, sequence, transitive)
                        |
                    Crucible (role-filler algebra)
```

Everything on that diagram is dense `Glyph`.

### 4.2 The measurement path — where the sparse substrate lives

```
    Sdr  ---->  Segment / SdrUnion  ---->  TemporalMemory
                                                |
      tests: sdr_test, temporal_memory_test, novelty_test, provenance_test
      bench: temporal_memory_bench, scaling_bench, curiosity_bench,
             invivo_bench, category_bench, category_eval
```

**State this plainly: `Sdr` and `TemporalMemory` are not called by `khora.exe`.**
Grep is unambiguous — the only non-test, non-bench includes of
`khora/lattice/sdr.hpp` are `sdr.cpp` itself, `glyph.hpp`, and
`temporal_memory.hpp`; the only includes of `temporal_memory.hpp` outside
`tests/` and `bench/` are its own translation unit.

So the newest, best-measured, best-justified machinery in the repository — the
sparse code, subsampled matching, per-cell context, the calibrated novelty
signal, provenance — is built, tested and benchmarked, and the runtime does not
use any of it. The live cognition path still runs on the dense `PredictiveColumn`
that `temporal_memory.hpp`'s own header comment explains is structurally
incapable of high-order sequences.

That gap is the honest state of the architecture at 0.115.0. It is not a
todo-list item hidden in a footnote; it is the shape of the system.

The staged plan in `docs/SPEC-v1-tissue-structured-cognition.md` describes how
the two paths are meant to converge. **That document is a proposal.** Its stages
S1 (the `Sdr` substrate) and S2 (temporal memory) are built. S0 and S3 through S7
— honest baseline telemetry, the encode/retrieve phase gate, inverted-index
retrieval, two-store CLS replay, neuromodulation, the criticality controller —
are not.

---

## 5. The substrate layer

### 5.1 `Lattice`

`unordered_map<string, Glyph>` plus `query(probe, k)`: a linear scan computing
Hamming distance to every stored glyph, returning the k nearest. Content-
addressable recall from a partial, noisy or bundled probe is the operation the
whole dense path is built on.

Two properties worth naming: the scan is O(n) in stored glyphs, and ties in the
ranking are resolved by iteration order over an `unordered_map`, which is not
stable. `Lattice::query` is the correctness oracle for the Maelstrom, which makes
the tie behaviour load-bearing. Fixing it is open.

Persistence is a single binary file: `KHORALAT` magic, format version, glyph
width, count, then `(label_len, label, 157 words)` repeated, then `KHORAEND`.
Width mismatch on load is an error, not a silent reinterpretation.

### 5.2 `Maelstrom`

D3D11 DirectCompute k-NN over a VRAM-resident glyph database. One independent
POPCNT per stored glyph, so the problem is embarrassingly parallel. It ships with
no toolkit dependency — `d3d11.dll` and `d3dcompiler_47.dll` are present on every
Windows install — and falls back to CPU when no compute-capable device binds.

The contract in the header is explicit and worth preserving: the Maelstrom is an
accelerator, `Lattice::query` remains the only correctness oracle, and the GPU
path must return bit-identical Hamming distances or it is wrong. There is no test
binary asserting that contract. **Unverified: the GPU path's agreement with the
CPU oracle has not been checked here.**

---

## 6. The sequence layer

Two implementations, built for the same job, arriving at different answers.

### 6.1 `PredictiveColumn` — dense, wired, and structurally limited

Keeps a sliding window of the last K inputs (default 3), encodes it as a
position-marked bundle, and stores `context -> next` in an internal `Lattice`.
Prediction is a k-NN query over the stored context keys. Bounded by a FIFO
eviction cap (default 200,000 associations) so memory stays finite.

Its limit is not a tuning problem. Encode `A B C D` and `X B C Y`; once the window
slides past the disambiguating element, both contexts are *the same glyph* and the
two futures collide. Widening the window moves the wall without removing it.

Measured against sequences that share a common middle: the dense chain scores
1/N — 50.0% at N=2, 33.3% at N=3, 25.0% at N=4, 16.7% at N=6. That is exactly
chance, and it is what a pair-encoding must score, because a transition is a
*pair*.

It stays because it is what the runtime is wired to, and because `babble()` /
`predict_candidates()` give the Cogitator a generative mechanism that is honest
about what it is: replay of memorised transitions, locally coherent, not novel.

### 6.2 `TemporalMemory` — sparse, built, benchmarked, not wired

`include/khora/cortex/temporal_memory.hpp`. 16,384 minicolumns of 32 cells
(524,288 cells), one column per `Sdr` position, 256 columns active per step.

The mechanism is one idea: **a minicolumn represents *what* is being seen; the
cells inside it represent *which context* it is being seen in.**

- **Proximal (feedforward) input decides *whether* a cell can fire.**
- **Distal segments only *prime*.** A distal NMDA plateau is worth 3–23 mV at the
  soma and lasts 50–100 ms — nowhere near enough to fire the cell. Two input
  classes, two semantics, structurally unable to substitute for each other. The
  primed state is a countdown, not a flag, because the plateau outlasts one tick.
- A column that is driven **and** primed fires only its primed cells: the
  context-specific representation.
- A column that is driven with nothing primed **bursts**, firing every cell —
  "this input, in no context I know".

**Bursting is not a failure mode.** The fraction of active columns bursting is an
anomaly score, computed in the same pass, at no extra cost. Nothing else in the
system reports its own ignorance.

Learning is one number wide. `permanence_initial` below `permanence_connected`
means a synapse contributes nothing until roughly three exposures have carried it
over the line — the slow, statistical, semantic store. `permanence_initial` at or
above `connected` means one exposure suffices — the fast, one-shot, episodic
store. Both configurations ship (`semantic()`, `episodic()`) as one
implementation with the boundary made explicit.

Two capabilities the dense path cannot offer:

- **Provenance.** Every segment is tagged with the id of the episode that grew
  it, so `explain()` names the stored episodes responsible for the current
  prediction and `forget(source)` deletes exactly those. The claim is falsifiable
  in the strongest available way: forget those ids and the prediction must
  change; forget the same number of others and it must not.
- **Lesion tolerance.** `lesion(fraction, seed)` kills cells outright, to check
  that matching really is subsampled rather than quietly depending on every unit.

Measured: **100% on N sequences sharing a common middle at N = 2, 3, 4 and 6**,
against the dense chain's chance-level 1/N.

### 6.3 Where it loses — and this is the useful part

On real books (six Gutenberg texts, trained on the opening 900 words each, then
asked to separate passages it has read from passages further into the same
books), for **exact-match** recall:

| model | novelty: seen | held-out | AUC |
|---|---|---|---|
| trigram table (~30 lines) | 0.000 | 0.968 | **1.0000** |
| temporal memory (894,108 segments) | 0.367 | 0.843 | 0.9981 |

**The baseline wins, and it should.** "Have I seen this exact sequence" is a
membership query and a hash set is the correct data structure for one. The task
was rigged in the baseline's favour by construction, and reporting that is the
point.

The sparse machinery earns its cost only where exact matching cannot go at all —
corrupted input. Replacing k of 8 words with words drawn from elsewhere in the
same corpus:

| corrupted | trigram novelty | temporal-memory novelty |
|---|---|---|
| 0 of 8 | 0.000 | 0.367 |
| 1 of 8 | 0.375 | 0.504 |
| 2 of 8 | 0.599 | 0.596 ← crossover |
| 3 of 8 | 0.757 | 0.693 |
| 4 of 8 | 0.852 | 0.750 |

**The crossover is at ~25% corruption.** Below it, exact matching wins. Above it,
subsampled matching wins — and the reason is the slope, not the level: over the
same range the trigram table's novelty rises 0.852 while the temporal memory's
rises 0.383, less than half as fast. That is graceful degradation, it is what the
false-match mathematics predicts, and it is measured on real prose.

---

## 7. The knowledge layer

### 7.1 `Lexicon` — token encoding

Two layers, both pure hyperdimensional computing.

1. **Structural.** A token's baseline glyph is the bundle of its position-marked
   character trigrams with `^`/`$` sentinels. Deterministic, no training. "cat"
   and "cats" share trigrams and therefore bits; typos stay close.
2. **Distributional, by random indexing.** Each token has a fixed sparse ternary
   index vector; each token also accumulates a context vector into which every
   neighbour's index vector is added. Words that keep similar company converge.
   Because index vectors are sparse, a co-occurrence costs ~K increments rather
   than ~N bit operations.

`glyph_for()` blends both, so its nearest neighbours can be merely
spelling-similar. `context_glyph()` isolates the distributional half, which is
the one that means "keeps similar company".

`tokenize_sentences()` exists beside `tokenize()` and is not cosmetic. The flat
tokenizer discards punctuation, which is right for co-occurrence statistics and
catastrophic for anything that reads a window forward — see §7.3.

### 7.2 `Plexus` — the associative graph

The hub problem defeats every purely distributional substrate: loud words keep
company with everything, so under any overlap metric they sit near everything and
every train of thought collapses into them. Three attempts to fix this inside the
binary glyph substrate failed — strip the common bits (none were concentrated),
force fixed density (surfaced random rare words), normalise by cosine (function
words genuinely do overlap everything, so cosine rewarded them).

The fault was never the metric. **The binary glyph threw away the frequency
information the cure needs.** The Plexus keeps it: an explicit weighted graph,
one node per word, raw counts preserved, affinity computed as positive pointwise
mutual information with context smoothing `P(b)^0.75` and a noise floor under
single-meeting edges. A hub's own loudness divides out of every edge it owns.

Memory is bounded by `max_degree` (default 160 associates per node), and the
graph is an additive commutative monoid, so `plexus_forge` builds it across all
cores by absorbing thread-local partials and pruning once at the end.

`reinforce(a, b, add)` raises the joint count only — which is exactly what lifts
the mutual information — and is intended for verified discoveries. Unverified
write-back is an echo chamber; the runtime's Furnace thread does call it (§8).
**Unverified: whether the Furnace's corroboration standard is strict enough to
avoid that has not been measured here.**

**Categories are overlap, not stored edges.** There is no taxonomy module in this
architecture, deliberately. A category is the subspace its members share:
intersect several members' distributed codes and keep what they hold in common.
Word codes are built from the set of a word's strongest Plexus associates, so
words with similar neighbourhoods share bits by construction, and the structure
enters one level up — from *which* associates a word has.

Evaluated against WordNet 3.1 as an external answer key (3,373 noun categories,
54,135 member words, used only as a key; nothing from it enters any code, graph
or representation), with **frequency-matched negatives**, 207 qualifying
categories, 16 associates, 5 seeds, 60% quorum:

| method | AUC |
|---|---|
| **category code** | **0.6168** |
| raw Plexus affinity | 0.5298 |
| corpus frequency | 0.5199 |
| random floor | 0.4947 |

Paired per category: 161 wins, 42 losses, 4 ties, sign-test **z = 8.35**. Modest
in size, overwhelming in consistency.

Two disclosures belong with that number. An earlier version of this evaluation
used six hand-picked categories and reported a win; at scale the win vanished
(0.4964 against a 0.4889 floor — chance). And with uniformly drawn negatives,
corpus frequency beat every real method at 0.6566, not because frequency is
clever but because well-populated WordNet categories are full of common words.
Frequency-matching the negatives forces that baseline to chance by construction.

### 7.3 `Ligature` — typed relations, and what they are actually worth

The Plexus captures *that* two concepts relate; it cannot capture *how*. The
Ligature extracts typed triples by syntactic pattern — no LLM, the classical
dependency-free way — for three relations: `IS-A`, `CAUSES`, `HAS-PART`. Each
triple carries a count. `is_a()` does transitive reachability; `deduce()` derives
facts not directly asserted, via property inheritance and causal chaining, and
returns the derivation chain with each result.

**The extracted relations were measured, and they are mostly false.** From 19,475
triples over 22 MB of real books:

```
man     is-a: animal(7) creature(4) weber(4) social(3)
time     has: come(10) arrived(4) workers(4)
body    is-a: row(16) matter(2)
```

"time has come" is the English perfect tense read as possession. "weber" is a
proper noun from the *following* sentence. "body is-a row" is asserted sixteen
times. Every derivation chain checked held — 3 of 3 — which is the failure mode
that produces confident falsehood: sound reasoning over garbage premises.
Meanwhile `benchmark_deduction()` reported 1.000, because it constructs its own
cases; a system inventing a benchmark and scoring itself perfectly on it while
emitting visible nonsense on real data.

Two structural causes were fixed. The shared tokenizer discarded sentence
boundaries, so the extractor's five-word forward window walked into the next
sentence — `tokenize_sentences()` now exists and the live study path uses it.
And `has`/`have` did not require a determiner although `is`/`are` did, so the
perfect tense read as possession; it does now.

Mechanically detectable errors fell from 6.6% to 5.3% over 3.19M tokens. **That
is a sliver, and the rest is the real finding.** Adding a corroboration floor:

| min support | surviving | impossible |
|---|---|---|
| 1 | 133 | 7 (5.3%) |
| 2 | 10 | 0 |
| 3 | 3 | 0 |

A floor of 2 removes every detectable error and 92% of everything else with it.
The root cause is a corpus mismatch, not a tuning error: Hearst patterns work on
definitional text, and this corpus is Pride and Prejudice, The Republic and
Leaves of Grass. "X is a Y" in Austen is rhetoric; in Plato it is a position
under examination. The extractor harvests metaphor as fact.

**No corroboration floor is imposed yet**, because imposing one would silently
empty a layer that `deduce()`, Crystallize, the abstraction tower and
`infer_path()` all read. That decision is open and needs to be made deliberately.

### 7.4 `Crystallize` — association to structure

A conservative bridge: propose `subject is-a parent` when several strong Plexus
associates of the subject independently share that parent *and* the parent has
direct positive affinity to the subject. Defaults: 32 associates examined, 6
parent fanout, minimum support 3. The runtime commits only multi-witness
crystals, only where the Ligature does not already know the edge, and only where
it cannot form a cycle.

It stands on §7.3's foundation, and inherits its quality.

---

## 8. Drives, action, and containment

### 8.1 `SomaNexus`

Five drives — Curiosity, Preservation, Mastery, Efficiency, OperatorAffinity —
each a scalar in [0,1] with a setpoint and an exponential decay rate. Candidate
actions carry an `Affinity` vector; arbitration is `sum(strength × affinity)`.

**Curiosity is measured as a trap and has not been replaced.** Novelty-seeking
means going wherever surprise is highest, and the most surprising thing in any
world is the thing that cannot be learned. `bench/curiosity_bench.cpp`: greedy
novelty scores **1.0000 surprise-remaining at every signal-to-noise ratio
tested** — it learns nothing — while spending 92–98% of its attention on an
unlearnable source. Uniform coverage beats it outright. Chasing learning
*progress* instead avoids the starvation but also loses to coverage, because a
constant-but-noisy region manufactures apparent progress out of measurement
variance.

Neither policy is wired. The drive is stimulated by callers and consumed by
nothing. This matters because greedy novelty-seeking was Khora's own stated Soma
design, and it is now a rejected one.

### 8.2 `Volition`, `Curator`, `Reverie`, `Whetstone`

- **Volition** scores a repertoire of Acts by drive pressure × affinity, performs
  the winner, then settles the served drives so attention rotates.
- **Curator** decides what to learn: study a held-but-unabsorbed tome, forage a
  topic it has no material on, deepen, or idle.
- **Reverie** perturbs stored glyphs, bundles pairs into synthetic "dreams",
  scores familiarity against the cortex and satisfaction against the drives, and
  retains what passes. Optionally feeds retained dreams back into the cortex.
  Note the design objection recorded in the spec: an unconstrained bundle of two
  random memories manufactures spurious associations, which is the failure mode
  down-selection exists to prevent.
- **Whetstone** holds trainable Faculties (relational capacity, sequence
  induction, transitive reasoning), surveys competence each round, escalates
  difficulty on mastery and mutates method on shortfall, with `revert()` when a
  measured mutation makes things worse. Whether its `evolve()` changes structure
  or merely tunes scalars is **under measurement, not settled**.

### 8.3 `Crucible`

The relational algebra harness. Role glyphs bound to filler glyphs, bundled into
one holographic record glyph; queries fall out of unbind plus nearest-neighbour
cleanup. Structured unbind measures 100.00% at redundancy 1.

Redundancy escalation — encoding each bound pair as a vote of R permuted copies —
was measured and is worthless: R=1 → 100%, R=4 → 97.0%, R=6 → 61.5%, R=8 →
28.7%. It is an evolution path that only degrades. `evolve_structured` remains in
the source and should not be trusted as one.

### 8.4 `Hand` and `Bulwark` — two effectors, on purpose

`Hand` is the **operator's** effector: real processes, no command filtering, one
governor (a timeout, so a hung command cannot freeze Khora). It is not a sandbox
and does not pretend to be.

`Bulwark` is the **autonomous** path, and everything about it is fail-closed: if
any containment primitive cannot be applied, nothing launches. Every contained
command runs inside a Job Object (kill-on-close, no breakaway, process cap, RAM
cap, CPU cap, idle priority) under a low-integrity non-admin restricted token,
with the cell as working directory and a system-volume free-space floor. The wall
is the OS access check, not a string blocklist — so `del C:\Windows` *executes*,
is *denied*, and Khora observes both. Contain the blast radius, not the
capability.

`self_check()` returns the achieved tier (0 = refuse, 2 = full) and gates the Maw.
`bulwark_probe` is registered with `ctest` so the precondition for autonomy is
actually exercised rather than assumed.

The canary matters more than it looks. It was `ping -n 30 127.0.0.1` until a
Python `ping.py` ahead of System32 on PATH turned the runaway into a
millisecond-long failure — so the timeout never fired, the tree-kill was never
exercised, and containment reported "unproven" because of a PATH collision. The
canary is now a shell builtin spun inside a grandchild named by absolute path.

`Maw` generates commands by entropy and recombination over a curated shell
surface, runs them only through the Bulwark, and accumulates a persisted map of
what it has charted. **Opt-in and dormant by default** — the thread exists, the
exploration does not run until the operator arms it and containment re-proves at
tier 2.

### 8.5 `Carapace`

A registry of named tools and a dispatcher. Parsing is whitespace-split with
double-quote grouping — a discrete verb-and-slots grammar, not completion.

95 registration sites yield **91 distinct tools**. Four names (`spire`,
`cascade`, `contemplate`, `learn`) are registered twice, and `register_tool` does
`tools_[t.name] = ...`, so the later registration silently shadows the earlier.
That is a live bug, not a design.

### 8.6 `Lodestone` and `Ballast`

`Lodestone` benchmarks the actual machine (glyph throughput, real parallel
speedup, RAM, disk write) and derives an operating profile: association cap,
vocabulary cap, study size, background cadences. Caps are sized to Khora's budget,
never to full system RAM.

`Ballast` watches Khora's working set and total system RAM once a second. On
over-cap or system pressure it pauses background learning and sheds memory —
pruning the cortex's associations and the lexicon's least-exposed vocabulary —
then resumes when the pressure clears. The runtime configures it at a 24 GB cap
and a 90% system-pressure threshold. The operator's machine must never lock up;
that is the constraint the whole governor exists to satisfy.

---

## 9. Concurrency — and the Synapse Bus, which nothing uses

### 9.1 What the runtime actually does

`khora.exe` runs up to eight background threads: the Reverie scheduler, the
Whetstone scheduler, the Curator scheduler, the Ballast governor, the Furnace
(parallel abstraction scouting), the Curiosity daemon (gap-finding and
foraging), the Maw (dormant), and the Volition scheduler on demand.

**They are coordinated by exactly one `std::shared_mutex`, declared in `main`.**
Every operator command takes it in unique mode. Every mutating background step
takes it in unique mode. The Furnace's scouting pass is the one place the shared
mode earns its keep: the heavy work is pure reads of immutable state, so it runs
as wide as the hardware allows while writers are excluded.

This is a global lock, and it is the concurrency architecture. It is honest and
it is a ceiling.

### 9.2 The Synapse Bus is built, tested, and dead

`src/synapse/` is a complete typed pub/sub fabric: string topics, per-subscriber
bounded queues, oldest-dropped-and-counted backpressure, documented ordering
guarantees, blocking and non-blocking poll, full stats. `SynapseTest` passes.

**Nothing uses it.** The only files that include `khora/synapse/synapse_bus.hpp`
are `synapse_bus.cpp` itself and `tests/synapse_test.cpp`. All 20-odd subsystems
are wired by direct reference behind the shared mutex described above.

The old version of this document called the Synapse Bus the central fabric of the
architecture, with everything else hanging off it. That was never true of the
code, and it has stayed untrue across 115 releases. The bus is 432 lines
(implementation, header, test) that no execution path reaches, and a single
global broadcast bus is arguably the wrong design regardless — the one thing that
genuinely needs to be global in the staged plan is a boolean phase flag.

It is recorded here as **built and unused**, and the choice is delete it or wire
it. Leaving it in this state is the one thing that should not continue.

---

## 10. Persistence and data layout

All Khora data lives under **`C:\Khora\data\`** (gitignored). Paths in the source
are relative to the working directory; the runtime creates what it needs.

| Directory | Contents |
|---|---|
| `data\lattice_archive\` | `main.klat` — the labelled glyph store |
| `data\cortex_archive\` | `main.cortex`, `main.keys.klat`, `main.vals.klat` — PredictiveColumn header plus its two lattices |
| `data\lexicon_archive\` | `main.sem.klat` (binarised context glyphs), `main.lexobs` (per-token counts) |
| `data\plexus_archive\` | `main.plexus` — the co-occurrence graph |
| `data\ligature_archive\` | `main.lig` — typed relations |
| `data\cogitator_archive\` | `attractors.txt`, `abstractions.txt` — preoccupations and the abstraction tower |
| `data\reservoir\` | `*.tome` — distilled, compressed source texts (59 present; 20 GB cap) |
| `data\ledger\` | `yield.tsv`, `training.tsv`, `reverie.tsv`, `params.txt` — measurement logs and tuned parameters |
| `data\chronicle\` | `khora.chronicle` — self-authored reflection log, append-only |
| `data\crucible\` | `relational_evolution.json` |
| `data\whetstone\` | `session.json` |
| `data\maw\` | charted verbs, flags, seen-index, stats |
| `data\bulwark\` | `cell\` — the disposable working directory for contained execution |
| `data\ascend\` | boot sentinel for binary self-replacement |
| `data\eval\` | `wn_categories.tsv`, `heldout.txt` — external answer keys, regenerable via `tools/fetch_wordnet_categories.py` |

There is **no** `reverie_traces\` and **no** `synapse_journal\`; the previous
version of this document listed both. Retained dreams go into an in-memory
lattice the Ballast governor does not know about, which is a known leak.

---

## 11. Build, test, measure

```powershell
.\tools\khora.ps1 all          # configure + build + test
.\tools\khora.ps1 bench        # run every *_bench.exe
.\tools\khora.ps1 run khora    # the REPL
```

The task runner is not a convenience. The Visual Studio Installer was removed
from this host while the Build Tools payload survives, so CMake cannot resolve a
VS generator instance; `vcvars64.bat` leaves the SDK version empty. The runner
activates MSVC, wires the newest complete Windows SDK by hand, and uses Ninja,
which needs no installer metadata. See `docs/DEVELOPMENT.md`.

`KHORA_USE_AVX2` is ON by default **with no runtime fallback** — the binary
requires AVX2.

Discipline:

- Every test registered with `ctest` carries a wall-clock timeout (60 s; Bulwark
  gets 120 s because it deliberately runs a process to timeout). A hung test with
  no signal is worse than a failing one.
- `CHANGELOG.md` records only what was observed after a demonstrated run.
- Benchmarks that produce a *negative* result are committed with the number, in
  `bench/` and in `docs/SPEC-v1-...` Appendix C, so nobody re-attempts a rejected
  idea on intuition.

---

## 12. Not built

Named so the intent is not mistaken for an artifact.

- **Sigilline** — a declarative DSL for naming glyph-algebra compositions so the
  operator or the Carapace could define new ones without writing C++. No parser,
  no evaluator, no grammar. Nothing exists.
- **Vellum** — a WPF desktop interface over named pipes. Nothing exists. The
  interface is the `khora.exe` REPL and its 91 tools.
- **SPEC stages S0, S3–S7** — honest baseline telemetry, the encode/retrieve
  phase gate, inverted-index retrieval, two-store CLS with prioritised replay,
  neuromodulation as four scalars, the criticality controller. Proposal only.

## 13. Measured and rejected

Kept in the architecture document because a rejection with a number attached is
architectural information.

| Idea | Verdict |
|---|---|
| **Greedy novelty-seeking as the Curiosity drive** | Rejected. 1.0000 surprise-remaining at every SNR, learns nothing, 92–98% of attention on static. Uniform coverage wins. Was Khora's own stated design. |
| **Learning-progress curiosity** | Also loses to coverage: constant-but-noisy regions manufacture progress out of measurement variance. |
| **Synaptic pruning for sub-linear matching** | Rejected in both regimes. Every cap that saved time destroyed recall; every cap that preserved recall made learning *slower*. A listener list is an index, not a contested resource — the biological analogy did not hold. |
| **Crucible redundancy escalation** | Rejected. R=1 → 100%, R=8 → 28.7%. Only degrades. |
| **Sparse vectors inside the dense `Glyph` type** (`Glyph::sparse()`) | Deleted. A sparse vector in the dense type reaches 50% density in 8 XORs. |
| **Hand-picked category evaluation** | Rejected as an artifact. Six self-chosen categories showed a win; 888 WordNet categories showed chance. |
| **Uniformly-drawn negatives in category evaluation** | Rejected as confounded. Corpus frequency beat every real method at 0.6566. |
| **`Ligature` relations as a knowledge base** | Standing, but measured mostly false. A corroboration floor of 2 removes every detectable error and 92% of the rest. |

## 14. Known divergences from the original design intent

| Original intent | Reality |
|---|---|
| Synapse Bus is the central async fabric | Built, tested, used by nothing. One process-wide `shared_mutex` is the real fabric. |
| Stratiform Cortex is a predictive-coding hierarchy with upward predictions and downward error | Not built. `PredictiveColumn` is context→next lookup; `TemporalMemory` is per-cell context. Neither is a hierarchy and neither propagates error downward. |
| Reverie consolidates recent experience through the cortex's predictive models | Perturbs and bundles two random memories, scores, retains. Retention is unbounded and outside the Ballast's view. |
| One substrate, sparse | Two substrates, one dense and one sparse, with a one-way bridge — and the sparse one is not yet wired into the runtime. |
| Carapace plans by forward simulation before acting | Not built. Dispatch is direct verb→handler. |
| Vellum, Sigilline | Not built. |
| Data under `C:\Ai\Khora\` | `C:\Khora\data\`. |

---

*Version 0.115.0. If a claim in this document cannot be traced to source or to a
committed benchmark, it is a defect in the document — report it as one.*
