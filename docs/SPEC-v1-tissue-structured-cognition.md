# KHORA ARCHITECTURE SPECIFICATION v1 — Tissue-Structured Cognition

**Status:** proposal, unimplemented. Nothing below is claimed as working.
**Audit basis:** read of `C:\Khora` at commit `c7b471c`, 2026-08-19. 23 modules, 17,412 lines, 14 tests.

---

## 0. CORRECTIONS TO THE BRIEF (read this first)

The brief's audit is stale by four commits. A plan built on it would waste its first month. Verified against the working tree:

| Brief claims | Reality at `c7b471c` |
|---|---|
| `bundle()` threshold is `(n+1)/2`, ties toward SET, bundle-of-2 is OR | **Fixed** (`0f6b631`). `C:\Khora\src\lattice\glyph.cpp:154-190` — even-arity ties broken by `tiebreak_for()`, a glyph derived by XOR-folding the operands: deterministic, commutative, decorrelated across operand sets. |
| Plexus PPMI has a normalisation bug starving Crystallize | **Fixed** (`11e6e66`). `C:\Khora\src\plexus\plexus.cpp:53-80` normalises by `Z = Σ occ^α`, not `N^α`. |
| 13 tests, 11 green | **14 tests, 14 green** (`0eaa083` fixed the Bulwark canary). |
| Crucible redundancy escalation is a working evolution path | **Measured false** (`c7b471c`): R=1 → 100%, R=4 → 97.0%, R=6 → 61.5%, R=8 → 28.7%. Redundancy *hurts*. `evolve_structured` is dead machinery. |

Two claims in the brief **are** confirmed and remain load-bearing:

- `Glyph::sparse()` (`glyph.cpp:48`) exists with one caller, a test. Every production glyph is ~50% dense.
- `SynapseBus` (`C:\Khora\src\synapse\synapse_bus.cpp`, 147 lines) has zero non-test users. `grep -rl synapse src/ include/` returns only its own three files.

Substrate blast radius, measured: 31 files touch `Glyph`; call sites — `bind(` 27, `bundle(` 44, `similarity(` 33, `hamming(` 18, `Glyph::random` 63, `permute(` 12, `.query(` 29.

---

## 1. THE CENTRAL ARCHITECTURAL THESIS

### 1.1 What is worth taking from tissue

Three things, and they are not the three things brain-inspiration usually takes.

**(a) The proximal/distal split with a subthreshold predictive state.** In a cortical pyramidal cell, feedforward input onto proximal dendrites decides *whether* the cell can fire. Input onto distal basal segments — 8–20 co-active synapses within ~40 µm, producing an NMDA plateau worth 3–23 mV at the soma, lasting 50–100 ms (Antic et al. 2010, J Neurosci Res 88:2991; Major et al. 2008, J Neurophysiol 99:2584) — cannot fire the cell at all. It can only *prime* it. Two input classes, two semantics, structurally unable to substitute for each other.

This is not decoration. It is the only known mechanism that lets one population represent "B in the context of A" and "B in the context of X" as *different states* without duplicating B. And it is the one brain-derived idea in this whole literature with an independent benchmark win under someone else's rules: Iyer et al. 2022 (Front Neurorobot 16:846219) grafted dendritic context gating onto ordinary gradient-trained sparse nets and beat dense MLPs on Meta-World MT10 (87.5% vs 76.6%) and permutedMNIST at 100 tasks (81.4%). Note carefully: that win came from *abandoning HTM's learning rule and keeping the gating*.

**(b) Subsampled matching instead of global distance.** A dendritic segment stores ~20 synapses drawn from a ~40-active-bit pattern and fires at θ≈13. It never sees the whole pattern. This buys: false-match probability ~1e-20 (Hawkins & Ahmad 2016), tolerance of 40% unit death with near-zero degradation, and — critically — *flat* performance against unions, where a global distance metric degrades as the union grows.

**(c) Complementary systems at two rates and two densities.** A fast, sparse, pattern-separating store that writes once and interferes little; a slow, dense, overlapping store updated only through replay (McClelland, McNaughton & O'Reilly 1995). Rat DG runs at 1–4% active feeding CA3; CA1 runs at ~40% (Chawla et al. 2005, Hippocampus 15:579). The density difference *is* the mechanism.

Everything else follows: bursting as a principled "I don't know", the anomaly score for free, least-used-cell allocation as O(1) structure learning, replay prioritisation, the encode/retrieve phase gate.

### 1.2 What is NOT worth taking — be ruthless

**Spikes and spike timing.** Thirty years, several billion dollars of silicon, zero cognitive capabilities demonstrated to require them. Every competitive SNN result is a converted ANN or a surrogate-gradient (i.e. backprop through a rate proxy) — capped by the network it came from, at lower accuracy and higher cost. IBM's own neuromorphic group dropped spikes for NorthPole and got a better chip. Khora runs on a CPU with `__popcnt64`. Spikes buy nothing.

**Oscillations as a clock, and binding-by-synchrony.** Burns, Xing & Shapley 2011 (J Neurosci 31:9658) showed macaque V1 gamma bursts are statistically indistinguishable from filtered broadband noise — there is no autocoherent carrier. Lisman & Jensen concede the gamma period varies cycle to cycle. And Shadlen & Movshon's arithmetic (Neuron 24:67, 1999) is unanswered: a cortical neuron receives several hundred input spikes per output spike, so in any 5–10 ms window *all* spikes are synchronous with some other spikes — synchrony cannot be a distinctive tag. Direct tests in MT are negative (Thiele & Stoner 2003, Nature 421:366; Palanca & DeAngelis 2005, Neuron 46:333). Meanwhile Khora's tick counter is free and exact, and VSA superposition already holds ~100 items against biology's 4–8 slots. Time-multiplexing across phase slots would be a strict downgrade.

**Communication-through-coherence as routing.** Attention shifts V1–V4 gamma coherence from ~0.10 to ~0.18. That is the whole effect. The leading current account (Schneider et al. 2021, Neuron 109:4050) says coherence is the *consequence* of transmission, not its cause. The digital equivalent of CTC is an `if` statement, deterministic and debuggable.

**The laminar connectivity matrix as a routing prior.** This is the seductive one, so it gets the sharpest rejection. Potjans & Diesmann's Table 5 is the best-fused 8×8 laminar matrix in existence and it is a measurement of *rat cortex under an explicit Gaussian model*. Copying `L2/3e ← L4e = 0.0437` into a hypervector router is decoration: those numbers are meaningful only inside a spiking dynamical system with a learning rule that makes connection probability *mean* something. Khora has neither. The same applies to the Blue Brain pathway JSONs, the Markov FLN matrix, and every connectome on the data-availability list. Shiu et al. 2024 (Nature) had the *complete* fly wiring diagram and still had to guess every synaptic sign and weight, because a connectome gives topology and nothing else. Lappalainen et al. 2024's connectome-constrained model only predicted neural activity *after being trained by gradient descent on a task*. Structure constrains the hypothesis space; the objective and the learning rule do all the work. Khora should take **one** number from this literature — the ~5% figure for how much of L4's excitatory input is thalamic (Binzegger et al. 2004), which says sensory input must be a *minority* term in any state vector — and nothing else.

**Detailed simulation of anything.** HBP: ~€600M–1B, ten years, an open letter from ~800 neuroscientists, executive committee dissolved, and Nature's 2023 retrospective could not find a major contribution. Blue Brain closed after twenty years having produced a *description* of a microcircuit and no learning algorithm. Hinton's diagnosis is the whole lesson: they had no clue how to make it learn.

**Biological sparsity levels for their own sake.** The measured "cortical sparsity" numbers disagree by an order of magnitude *between methods on the same tissue* (calcium imaging detects <10% of isolated single spikes — Huang et al. 2021, eLife 10:e51675; extracellular electrodes miss the dark neurons — Shoham et al. 2006). Sparsity in Khora must be justified by the false-positive tables, not by matching a number a mouse produced.

**The Free Energy Principle as an organising ontology.** It is a reformulation, not an algorithm. Twenty years, no SOTA on any contested benchmark. Its danger is specific: it makes every design decision *feel* justified, which stops you testing anything. Take the epistemic-value term as a component; reject the framing.

### 1.3 The one structural commitment

> **Khora commits to a fixed-weight sparse code matched by subsampled overlap counting, with a hard type-level separation between the input that decides *whether* a unit activates and the input that decides *which variant* activates.**

Everything downstream depends on it, and the reason is a single number.

Computed from Ahmad & Hawkins 2016 Eq. 4, for a 24-element subsample matched at θ=12:

| density | n=2,000 | n=4,000 | n=16,384 | n=65,536 |
|---|---|---|---|---|
| **50% (Khora today)** | **0.581** | **0.581** | **0.581** | **0.581** |
| 25% | 0.0072 | 0.0071 | 0.0071 | 0.0070 |
| 2% | 1.6e-15 | — | — | 8.4e-15 |
| 1.22% | 8.3e-19 | — | — | 2.4e-17 |

A dense code has a **58% false-match rate on a subsampled probe at every dimension**. Raising n from 2,000 to 65,536 changes it in the fourth decimal place. Sparsity — not dimensionality — is what makes subsampling work, and subsampling is what makes distal context, unions, and 40%-loss tolerance possible.

So: no sparse code, no subsampled matching. No subsampled matching, no distal context that survives a union of simultaneous predictions. No distal context, no high-order sequences, no bursting, no anomaly signal, no allocation policy. The whole plan is downstream of one 15-order-of-magnitude number.

---

## 2. THE SUBSTRATE DECISION

### 2.1 Decision

**Yes, sparse — but as a second, additional type, not a migration.** Dense `Glyph` stays exactly as it is and keeps the relational algebra. A new `Sdr` type carries everything that needs subsampling, unions, or pattern separation. A one-way projection connects them. `Glyph::sparse()` is deleted.

### 2.2 Why not migrate everything

Three reasons, in order of weight.

**XOR's exactness is a genuine advantage that sparse binding does not improve on.** XOR is an involution on (ℤ/2)^D and a Hamming isometry. Unbinding introduces *zero* noise — all retrieval error in the current Crucible comes from the bundle, none from the unbind. HRR/MAP cannot say this. And the pairwise false-positive rate at n=10,000 is already ≤1e-52 at θ=w/2. There is no measured defect in the dense relational path. `c7b471c` proved structured unbind is at 100.00% with R=1.

**The prior art's clearest lesson is that architecture-first rewrites kill these projects.** OpenCog: 20+ years, two full rewrites, still self-labelled "active pre-alpha", zero externally-cited benchmark. Sigma: a *better* idea than OpenCog — one factor-graph substrate unifying rules, probability, perception and learning — and it died anyway, because architectural uniformity is a tax levied on every feature and repaid only if the unification eventually yields a capability none of the parts had. It never did.

**The measured cost.** 31 files, ~250 substrate call sites, 14 currently-green tests, and a working Crucible reasoning suite. Converting all of it buys nothing for the relational algebra and risks everything.

### 2.3 The counter-case, taken seriously

The strongest argument for full migration: *two substrates means two mental models, two sets of bugs, and a boundary that will leak.* This is real, and it is Risk #1 in §5.

The rebuttal is that this boundary is not arbitrary — it is exactly the CLS boundary. Sparse `Sdr` is the fast, pattern-separating, episodic/predictive store (DG/CA3 analogue). Dense `Glyph` is the slow, overlapping, semantic store (cortex analogue). Biology runs *the same split* at 1–4% and ~40% density respectively. The two-code design is not a compromise; it is the finding.

Second counter-case: *why not make `Sdr` the only type and re-express the relational algebra in it?* Because block-code binding at L=2 **is** XOR — see below — so the dense path is already the degenerate case of the sparse one. Nothing is lost by keeping the specialised, faster, exact implementation of that case.

### 2.4 The Sdr type: concrete specification

**Sparse block code** (Laiho et al. 2015, IEEE BioCAS; Frady, Kleyko & Sommer, IEEE TNNLS 2023 / arXiv:2009.06734). Partition `n` positions into `B` blocks of size `L`, exactly one active element per block.

```
n = 16384,  B = 256,  L = 64
w = 256 active bits,  density = 1.5625%
storage: std::array<uint8_t, 256>  — one block index per block
```

| Property | Value | Note |
|---|---|---|
| **bind** | per-block `(i + j) mod L` | Local circular convolution. Exactly invertible, exactly sparsity-preserving. |
| **unbind** | per-block `(i − j) mod L` | `L=64` is a power of two → `& 63`, no division. |
| **similarity** | `overlap = count of equal block indices` | O(B) = 256 byte compares, SIMD-friendly. |
| **bundle** | `Trace`: `int16 counts[16384]`, per-block accumulate | Never binarise a loaded bundle. |
| **binarise** | per-block argmax, on commit only | |
| **k-WTA** | free — enforced by construction | The block constraint *is* the inhibition. |

**Why block size 64.** Storage 256 B vs 2048 B for a dense bitset (8×). Argmax-binarisation SNR degrades with L (simulated: L=8→5.72, L=64→3.72, L=256→2.38 at M=100), so 64 is near the knee. Counts-based SNR is `√(B(L−1)/M) = √(16128/M)` — 12.7 at M=100 — essentially `√(n/M)`, i.e. the ideal, independent of L.

**Why this is not a foreign algebra.** BSC is the `L=2` special case: each bit is a block of two with one implicit active index, and `(i+j) mod 2` is exactly XOR; per-block argmax at L=2 is exactly the majority rule. The migration is a generalisation of the existing algebra, not a replacement for it.

**Capacity numbers to design against** (all at n=16384):

- Pairwise FP at θ=w/2, w=200: 2.0e-146. Never the binding constraint.
- Subsample FP, s=24/θ=12: **1.9e-17** (vs 0.581 dense — the decision).
- Union at θ=w/2, ε=1e-6: w=64 → M=64 items; w=200 → M=33. *Fewer active bits means more items per union* — counterintuitive and load-bearing.
- Willshaw half-full capacity, w=200: 4,652 stored pairs. Treves–Rolls gives ~76,000 for the same net. **They disagree 16×. Budget with Willshaw, instrument for the truth, and never quote a single capacity number in a design doc.**

**The bridge: a DG-analogue projector.** `Sdr project(const Glyph&)` — a fixed seeded random projection of the 10,000 dense bits to 256 block-scores, then per-block argmax. One-way by design; there is no `Sdr → Glyph`. This is the mossy-fibre path: 4× wider, 32× sparser, used for any operation requiring pattern separation.

### 2.5 Migration cost

**Net new code:** `C:\Khora\include\khora\lattice\sdr.hpp` + `C:\Khora\src\lattice\sdr.cpp`, est. 400 lines. **Existing code changed: zero, except one deletion.**

`Glyph::sparse()` is deleted, not fixed. It is a live trap: XOR drives density 0.02 → 0.039 → 0.075 → 0.139 → 0.240 → 0.365 → 0.463 → 0.497 → 0.500 in eight binds (`p ↦ 2p(1−p)` has an attracting fixed point at 0.5), and majority-bundling three 2%-dense glyphs gives density 0.0012 — a near-zero vector that is roughly equidistant from everything and will silently match garbage. One caller, a test. Delete both.

Add to `bundle()` and `bind()` a debug-build assertion `density ∈ [0.40, 0.60]`, so the trap cannot be re-set.

---

## 3. STAGED IMPLEMENTATION PLAN

Rules for every stage: build green, `ctest` green, independently testable, one measurable capability the previous stage lacked, one cited biological finding. **No stage is justified by "it is more brain-like."** Each stage names the metric that would prove it worthless.

---

### S0 — Substrate telemetry and honest baseline

**Modules touched:** `C:\Khora\src\lattice\glyph.cpp`, `C:\Khora\tests\lattice_test.cpp`. **New:** `C:\Khora\bench\substrate_health.cpp`.

**Biological basis:** Ahmad & Hawkins 2016 (arXiv:1601.00720), Discussion: *"The equations in this paper assume random and decorrelated neural activity… increased correlation will lead to higher than random probability of overlap."* Real populations are lognormal — 13–16% of units produce 50% of spikes (Mizuseki & Buzsáki 2013, Cell Rep 4:1010). Every closed-form bound in this spec is invalid if Khora's encoders produce correlated codes.

**Algorithm:** Three continuously-logged metrics.
1. **Measured mean pairwise overlap vs theoretical `w²/n`.** For dense `Glyph` at n=10,000: expect 2500 ± 50. For `Sdr` at B=256/L=64: expect `B/L = 4.0 ± 1.99`. Excess measured overlap means hub bits, and every FP number in this document becomes optimistic.
2. **Density at every layer boundary**, asserted.
3. **The one self-check that catches four bug classes at once:** bundle M=50 random glyphs from a 1000-atom lexicon; assert all 50 recovered in the top 50 by Hamming; assert measured SNR within 10% of `√D·δ(50)` = `100 × 0.1128` = **11.3**. This fails on a density bug, a threshold bug, a permutation bug, or a popcount bug.

**Metric:** Baseline recorded for all three. Then inject a deliberate density bug (revert `tiebreak_for` to tie-toward-SET) and confirm check 3 fails. If it does not fail, the harness is worthless and S0 is not done.

**Kill criterion:** none — this is the instrument every later stage is read through.

---

### S1 — The `Sdr` substrate

**New:** `sdr.hpp`, `sdr.cpp`, `tests/sdr_test.cpp`. **Touched:** delete `Glyph::sparse()`; add debug density asserts.

**Biological basis:** DG/CA3 pattern separation — rat DG at 1–4% active feeding CA3, with 93.4% of granule cells holding ≤1 place field (Senzai & Buzsáki 2017, Neuron 93:691, n=586 putative GCs). Near-one-item-per-cell coding is what pattern separation looks like mechanically.

**Algorithm:** As §2.4. Plus `project(Glyph) → Sdr`; `Trace` (int16 counts) with `add(Sdr)`, `binarise() → Sdr`, and `density()`; `overlap(Sdr, Sdr)`; `Segment { uint8 block_ids[24]; uint8 perms[24]; }` with `matches(const Sdr&, uint8 theta)`.

**Metrics (all three required):**
1. **Bind/unbind exactness:** 10⁶ random `(a,b)` pairs; `unbind(bind(a,b), b) == a` at **100.00%**, and `popcount == 256` after any chain of 32 binds. The dense path fails the density half at bind 8.
2. **Subsample FP:** measure directly. Target ≤1e-15 at s=24/θ=12 against 10⁶ random distractors; the same measurement on the dense path gives ~0.58. **This 15-order-of-magnitude gap is the entire justification for the substrate decision, and it must be measured, not cited.**
3. **Throughput:** `Sdr::overlap` vs `Glyph::hamming`, ns/op. If `Sdr` is slower than 157-word AND+popcount, S2 does not start until it isn't.

**Kill criterion:** if metric 2 does not reproduce within one order of magnitude, the block-code implementation is wrong; do not proceed.

---

### S2 — Temporal memory: proximal/distal, cells, bursting, allocation

**New:** `C:\Khora\include\khora\cortex\temporal_memory.hpp` + `src\cortex\temporal_memory.cpp`. **Touched:** `predictive_column.*` gains a delegation path; nothing deleted yet.

**Biological basis:** Hawkins & Ahmad 2016 (Front Neural Circuits 10:23). Distal NMDA plateaus depolarise the soma 3–23 mV and last 50–100 ms without firing the cell (Major et al. 2008; Antic et al. 2010) — that is precisely a predictive state that cannot masquerade as activation.

**Algorithm.**
- 2048 minicolumns × 32 cells. 40 active columns (2%) supplied by the proximal path (Khora's existing encoder → `project()` → k-WTA on columns; **do not build a Spatial Pooler** — Numenta's own evaluation says SP-with-learning-without-boosting is roughly a random static projection).
- Each cell holds up to 128 distal segments; each segment holds up to 40 potential synapses as sorted `uint32` presynaptic cell indices with a parallel `uint8` permanence array. **initial 54 (0.21), connected 128 (0.50), ±26 (0.10).** Connectivity is binary; permanence *never* scales the contribution.
- Segment fires → cell predictive, when connected-synapse overlap with the previous active-cell set ≥ **θ=13**.
- Active column with ≥1 predictive cell → only those cells fire. Active column with none → **all 32 burst.**
- Learning on a correct prediction: increment synapses whose presynaptic cell was active, decrement the rest on that segment, grow up to 20 new synapses from the previous winner set. On a burst: pick the cell with the best segment above `minThreshold=10`, else the cell with the **fewest** segments. On a segment that predicted a column that did not activate: apply `predictedSegmentDecrement`.
- **`predictedSegmentDecrement` must default non-zero.** Both reference implementations ship 0.0, which means false predictions are never unlearned. The research digest flags this explicitly as a real bug for noisy or branching streams with no principled value in the literature. Start at 0.01 and expose it.
- **Index segments by presynaptic cell.** Per cell, a list of segment IDs it projects to; each step, iterate only the previously-active cells and increment per-segment counters. This turns O(cols × cells × segs × syns) into O(active_cells × mean_fanout) and is the difference between real-time and not.
- Predictive state is a `uint8` countdown, not a bool — biology's plateau lasts 50–100 ms, i.e. more than one tick, and the BAC coincidence window tolerates σ=6 ms jitter (Larkum, Zhu & Sakmann 1999, Nature 398:338).

**Metrics.**
1. **The high-order sequence test — the stage's reason to exist.** Train on interleaved `ABCD` and `XBCY`. Assert: after `X,B,C` the system predicts `Y` and not `D`; after `A,B,C` it predicts `D` and not `Y`. **The current `PredictiveColumn` cannot do this.** It encodes context as a position-permuted bundle of the last K inputs (`predictive_column.cpp:33`) and stores `context→next` in a `Lattice`; once the window slides past the disambiguating element, the two contexts are the same glyph. This is a capability the previous stage structurally lacks, not a quantitative improvement.
2. **Robustness:** kill 40% of cells at random, re-run test 1, pass. This is what verifies the matching is *actually* subsampled (Hawkins & Ahmad report near-zero degradation at 40% loss).
3. **Anomaly score:** `fraction of active columns that burst`, one pass, no extra model. Assert it spikes at a sequence boundary and decays monotonically over ≥5 repetitions of a novel sequence.
4. **Regression:** all 14 existing tests green; Crucible faculty scores unchanged within ±0.01.

**Kill criterion — stated in advance.** HTM's honest record is parity with LSTM, and Struye & Latré 2020 (Neurocomputing 396:291) found plain MLPs stripped of temporal mechanisms matched their temporally-aware counterparts. **If metric 1 passes but no Crucible faculty moves and no downstream stage consumes the anomaly score, S2 is a science experiment and gets reverted.** S4 is the consumer; if S4 slips, S2's justification slips with it.

---

### S3 — The phase gate

**Touched:** `cogitator.cpp`, `reverie_loom.cpp`, `crystallize.cpp`, `plexus.cpp` (write path). **New:** a `CyclePhase` enum and one assertion helper.

**Biological basis:** Hasselmo, Bodelón & Wyble 2002 (Neural Computation 14:793) — within each 100–300 ms theta cycle, one phase has strong entorhinal drive and strong LTP but weak CA3 recurrent transmission (encode without interference); the opposite phase has strong recurrent transmission and depotentiation (retrieve and extinguish). Causally confirmed by Siegle & Wilson 2014 (eLife 3:e03061): closed-loop CA1 inhibition at the theta *peak* improved encoding, at the *trough* improved retrieval.

**Why this stage is first-class and not a footnote.** Khora's own changelog documents exactly the pathology this prevents. v0.113: the abstraction tower reached depth 148 with coherence flat at ~0.80 — *retrieved* content being re-encoded as if it were new evidence. v0.114: it still climbed 24→32 in a single cycle. The fix shipped was a saturation gate — a content-level heuristic patching a control-flow defect. The control-flow defect is that read and write are not separated, so a retrieved item can be evidence for its own reinforcement. Reverie's documented 100% dream retention is the same defect wearing a different hat.

**Algorithm.** Each cognitive cycle has two strictly ordered phases.
- **Phase A (read).** All queries, all recalls, all graph traversals. A global `writes_enabled = false`. Any write attempt in phase A is an assertion failure in debug and a dropped write plus a counter increment in release.
- **Phase B (write).** Commits computed **only from the snapshot captured in phase A.** No reads of mutated state.
- Invariant, asserted: an item retrieved in phase A of cycle *t* may not appear in its own evidence set for reinforcement in phase B of cycle *t*.

This is a boolean and an ordering constraint. It is not an oscillator, and no clock is derived from content.

**Metrics.**
1. **Runaway reproduction.** Replay the v0.113 self-similar theme that drove depth 24→32 in one cycle. With the gate: assert tower depth growth per cycle ≤ 1 and coherence variance > 0 across levels. *This is a regression test for a bug the project has already been bitten by twice.*
2. **Reverie retention rate** falls from the documented 100% to a measured, non-degenerate value with a reported distribution. Any figure is acceptable except 100% and 0%.
3. **Dropped-write counter** at zero in steady state — a non-zero count means a subsystem is reading and writing in the same breath and needs restructuring, not suppressing.
4. **Lock contention:** the read phase needs no exclusive lock. Measure wall-clock of a 1000-cycle run before and after; report the number. Do not claim a speedup that is not measured.

**Kill criterion:** if metric 1 shows the gate does not prevent the runaway, the diagnosis was wrong and the saturation gate was addressing something else.

---

### S4 — Inverted-index retrieval

**Touched:** `C:\Khora\src\lattice\lattice.cpp` (add index path, keep the linear scan as oracle), `maelstrom.cpp`.

**Biological basis:** not a biological stage. It is included because it *gates* S5, and because pretending otherwise would violate this document's own rule. The honest justification: `Lattice::query` (`lattice.hpp:28`) is a brute-force linear scan over an `unordered_map`, non-deterministic in ties. At 157 XOR+popcount word-ops per candidate, K=10⁶ costs 1.57×10⁸ word-ops per query. S5's replay budget (20 bursts/cycle × 5–20 items) is not affordable at that cost.

**Algorithm.** For the `Sdr` store: an inverted index from `(block, index)` → posting list of item IDs, plus a per-candidate overlap counter. Retrieval of all items with overlap ≥ θ costs O(w × mean_posting_length) instead of O(K × n). For the dense `Glyph` store: 32 fixed random 64-bit bit-subsets as LSH keys, prefilter then exhaustive verify. **The linear scan stays as the correctness oracle** — exactly the contract `maelstrom.hpp` already states for the GPU path, and it should be extended, not replaced.

**Metrics.**
1. **Bit-identical results** vs the linear oracle on 10⁵ random probes at K=10⁶. Any mismatch is a bug, not a tuning parameter.
2. **Recall latency** at K = 10⁴/10⁵/10⁶, reported as a table. Target: sub-millisecond at 10⁶.
3. **Determinism:** tie-broken deterministically; two identical runs produce identical output.

---

### S5 — Two-store CLS with prioritised sequential replay

**New:** `C:\Khora\include\khora\hippocampus\episodic.hpp` + `src\hippocampus\episodic.cpp`. **Rewritten:** `reverie_loom.cpp`. **Touched:** `plexus.cpp` (downscaling), `crystallize.cpp` (gating).

**Biological basis:** McClelland, McNaughton & O'Reilly 1995 (Psych Rev 102:419) for the two-rate architecture. For prioritisation: Mattar & Daw 2018 (Nat Neurosci 21:1609) — `EVB = Gain × Need`, where Need is a row of the successor representation `(I − γT)⁻¹`. For the online tagging stage: Yang et al. 2024 (Science 383) — the awake-replay distribution predicts the subsequent sleep-replay distribution at **R = 0.86, P < 10⁻³⁶**. For downscaling: de Vivo et al. 2017 (Science 355:507) — axon–spine interface −18.9% after sleep vs spontaneous wake (n = 6,920 synapses), with **~20% of synapses (the largest) spared**.

**Algorithm.**
- **Fast store.** `Sdr` keys via `project()`, one-shot writes, at ~1.5% density. Bounded, with a `reactivation_count` and a `last_replayed` tick per item.
- **Slow store.** Plexus + the dense `Glyph` lexicon. Written **only** through replay, at ≥30× fewer writes per item.
- **Awake tagging.** During idle micro-pauses, run tagging passes at roughly half the offline rate that write a tag histogram. The offline pass samples from *that histogram*, never from the raw event log.
- **Priority.** `P ∝ (Gain × Need × novelty × recency)^0.6`. Gain = the Soma valence delta the item would produce. Need = a row of the row-normalised Plexus graph raised to the 20th power by iteration — **Plexus already is T; this term costs no new state.** Novelty multiplier ≈6 decaying over 2–3 exposures (Cheng & Frank 2008, Neuron 57:303 — novel-arm pairs were ~6× more likely to co-activate on day 1, gone by day 3). Recency `exp(−Δt/30 min)` (Kudrimoti et al. 1999, J Neurosci 19:4090). Exponent 0.6 from PER (Schaul et al. 2016).
- **Coverage, not recency.** Gupta et al. 2010 (Neuron 65:695) falsifies the naive recency model: rats replayed *rarely-taken* trajectories more than well-practised ones and constructed never-traversed paths. So: `priority *= (1 + λ/(1 + replay_count))`, plus a refractory mask suppressing re-selection for N bursts.
- **Bursts, not i.i.d. samples.** One burst = 5–20 items in temporal order, encoded as a permutation-chained sequence and written once. Derived from Davidson et al. 2009 (Neuron 63:497): ~8 m/s replay against 0.5 m/s running = 15–20× compression, ~50 cm of track per ripple. **This is the largest divergence between ML replay buffers and biology and the cheapest to fix.**
- **Direction.** Reverse replay on outcome (85% of biological reverse events are post-run; Diba & Buzsáki 2007), forward before acting (95% of forward events are pre-run). Overall budget 2:1 forward:reverse.
- **Interleaving.** Every batch mixes 1 part recent to 3 parts long-term. The 1995 result is that *interleaving*, not replay per se, prevents catastrophic interference. A replay pass containing only recent experience reproduces the pathology with extra steps.
- **Downscaling.** Per cycle, multiply all Plexus edge weights by **0.82**, sparing the top 20% by weight. Both numbers traceable to a 6,920-synapse EM measurement; both exposed as knobs.
- **Reality-check gate.** A dream may only *strengthen an existing* Plexus edge or *survive* downscaling. It may never create an edge. Crystallize's is-a voting runs on replayed-but-observed content, never on dream-only content. (Tononi & Cirelli 2014's down-selection model: spontaneous sleep activity must produce no new memories in the absence of external input.)

**Metrics.**
1. **Catastrophic interference — the AB/AC test.** Train corpus A, record Crucible faculty scores. Train conflicting corpus B. Re-measure A. Report retention under three conditions: no replay, uniform-random replay, prioritised interleaved replay. **The claim is a number: retention of A after B.** If prioritised replay does not beat uniform-random, the priority machinery is deleted and recency is kept — that is a real result about the algorithm, and it mirrors an open question in the biology.
2. **Bounded growth:** Plexus edge count reaches a plateau with faculty scores flat. This generalises v0.114's one-shot prune (298 → 89 abstractions, depth 32 → 7, faculties −0.007) into a continuous mechanism.
3. **Instrumentation, logged per cycle:** burst count, sequence-significance rate (expect ~1/3 usable — Yang et al. 2024 report ~33% of candidate awake events are significant replays), forward/reverse ratio, coverage entropy over the item set, mean edge weight before/after downscaling, and the fast→slow write ratio. **That last one is the parameter most likely to be wrong.** Biology's hippocampo-cortical coordination rate is ~2–7% (Ji & Wilson 2007: 9 coordinated events out of 366 cortical + 121 hippocampal significant replays).

**Kill criterion:** metric 1, condition 3 ≤ condition 2. Then keep the two-store split and the interleaving, delete the priority calculation.

---

### S6 — Neuromodulation as four scalars

**New:** `C:\Khora\include\khora\neuromod\neuromod.hpp` (~150 lines). **Touched:** `soma_nexus.cpp`, the k-WTA threshold in `sdr.cpp`, the bundling mix in `cogitator.cpp`.

**Biological basis:** Yu & Dayan 2005 (Neuron 46:681) — ACh reports expected uncertainty, NE unexpected uncertainty, and both down-weight top-down expectation relative to bottom-up input. Linster, Wyble & Hasselmo 1999 (J Neurophysiol 81:2737) supplies measured weights *in vivo*.

**Algorithm.** Seven floats, four nuclei, **non-overlapping time constants** — that separation is the load-bearing claim, since it is the only reason four scalars can control one system without mutual interference.

```
da_phasic  150 ms window, 75 ms post-event   (Bayer & Glimcher 2005)
da_tonic   minutes — reward rate / vigor     (Mohebi et al. 2019)
ach_fast   ~20 ms latency, 3 ms jitter       (Hangya et al. 2015)
ach_slow   t50 = 3.17 s                      (Parikh et al. 2007)
ne_phasic  hundreds of ms
ne_tonic   minutes + 0.01–1 Hz drift band    (Totah et al. 2018)
ht_tonic   minutes — patience / waiting
```

Three couplings, no more:
1. **Gain = the k-WTA threshold.** There is no f–I slope in a binary code. Raising gain lowers `w` (sharper, more selective); lowering it raises `w` (broader, more generalising). Equivalently, gain is the softmax temperature of the cleanup readout — `p_i ∝ exp(G · sim_i)`, one line.
2. **Bundling mix.** `w_td = round(16 × (1−ACh)(1−NE))`, `w_bu = 16 − w_td`, then majority-bundle with integer replication. Uncertainty literally controls how much prior survives into the new state.
3. **Recurrent suppression** at the measured weights: recurrent 0.78, afferent 1.00, downstream-afferent boost 1.69, resting recurrent gain **0.68** (blocking muscarinic receptors raises intrinsic EPSP slope to 146% of baseline — so the neutral state is already partial encoding mode, not neutral). Drive it with `max(ach_slow, ne_tonic)`, **never a sum** — the two are sub-additive (Hasselmo et al. 1997: combined perfusion ≈ single agonist at equivalent concentration).

Plus the context-reset trigger: fire iff `NE > ACh/(0.5 + ACh)`, with a 10-cycle refractory after a detected switch.

**Metrics.**
1. **Change-point task.** Input distribution switches at a known tick. Measure detection latency (ticks to reset) and post-switch prediction accuracy recovery, against a fixed-parameter baseline. Two numbers, both must improve.
2. **Negative-RPE handling.** Biology compresses negative RPE into a firing pause (Bayer & Glimcher: no modulation below ≈−0.1 normalised). Assert Khora's signed path does *not* silently reproduce this — scale negative updates 0.3–0.5× explicitly, and test that a punished association actually weakens.
3. **Ablation, per scalar.** Disable each of the four and report the delta on metric 1. **Any scalar whose ablation costs nothing is deleted.**

**Kill criterion:** metric 3 shows ≥2 scalars with no measurable effect. Then this is four magic numbers, not a control system.

---

### S7 — Criticality controller (conditional)

**New:** `criticality.hpp/cpp`. **Touched:** the recurrent gain set by S6.

**Biological basis:** Ma et al. 2019 (Neuron 104:655) — DCC < 0.2 at baseline in freely-behaving rats, tripled to ≈0.6 within 4 h of monocular deprivation, restored by 48 h, more than 30 h *before* firing-rate homeostasis completed. Wilting & Priesemann 2018 (Nat Commun 9:2325) — subsampling-corrected in vivo branching ratio **m̂ = 0.984**, where the naive estimator on the same data returned **0.271**.

**Algorithm.** Measure the branching ratio of activation cascades through Plexus using the **subsampling-invariant multistep-regression estimator** — mandatory, because Khora inevitably observes a fraction of its own state and the naive estimator is off by a factor of 3.6 in exactly that regime. Control the recurrent gain to hold `m̂ ∈ [0.96, 0.99]`. Autocorrelation time `τ = −Δt/ln(m)` ≈ 250 ms-equivalent at m=0.984 — a reverberating working memory that costs no extra state.

**Metrics — three independent diagnostics, all required, and they are expected to sometimes disagree.**
1. MLE fit of the cascade-size distribution with a KS statistic and a likelihood-ratio test against lognormal *and* exponential (Clauset et al. 2009). Many published power laws fail this.
2. The crackling relation `β = (τ_dur − 1)/(τ_size − 1)` against measured mean-size-vs-duration; require **DCC < 0.2**.
3. `m̂` from the subsampling-invariant estimator.
4. **Payoff metric:** dynamic range `Δ = 10·log₁₀(S₀.₉/S₀.₁)`, controlled vs uncontrolled. Shew et al. 2009 predict ~10 dB loss for a 30% error in σ. If Δ does not move, the controller does nothing.

**Kill criterion, stated up front.** Destexhe & Touboul 2021 (eNeuro 8) showed a Brunel network *and* a plain Ornstein–Uhlenbeck process both pass the crackling test, and a lattice of neon glow lamps at a demonstrably **first-order** transition reproduces α≈3/2 and ≈2. **Power laws are weak evidence for criticality.** If metric 4 shows no dynamic-range gain, S7 is deleted regardless of how good metrics 1–3 look. This stage is last for exactly that reason.

---

## 4. WHAT TO DELETE

Development is largely pruning. Each item below has a stated basis; none is deleted on aesthetics.

| Target | Location | Basis for deletion |
|---|---|---|
| **`Glyph::sparse()`** | `glyph.cpp:48`, `glyph.hpp:32` | One caller, a test. Actively dangerous: a sparse vector in the dense type reaches 50% density in 8 XORs and near-zero density in a 3-way majority bundle. Delete both the function and its test; replace with a debug density assert so it cannot return. |
| **`SynapseBus` in its entirety** | `src\synapse\` (147 lines), `include\khora\synapse\` (103), `tests\synapse_test.cpp` (182) | Zero non-test users after however many releases. And a single global broadcast bus is the *wrong* design anyway: LC shows only 15% pairwise synchrony across 3,164 unit pairs (Totah et al. 2018) and one nigrostriatal DA axon covers 2.7 ± 1.5% of its target volume (Matsuda et al. 2009). The one thing that genuinely must be global is the S3 phase flag, which is a boolean. 432 lines removed. |
| **Crucible redundancy escalation** — `evolve_structured`, `set_redundancy`, `unbind_redundant_`, `EvolutionStep` | `crucible.hpp:88-96`, `crucible.cpp:52-58,124` | Measured worthless by the project's own commit `c7b471c`: R=1 → 100.00%, R=4 → 97.02%, R=6 → 61.47%, R=8 → 28.66%. It is an evolution path that only degrades. Keep `TrialResult` and the three trials — they are the metric harness S2/S5 report against. |
| **`permute()` / `permute_inplace()` from the general API** | `glyph.cpp:90-104` | `ρ(a⊕b) = ρ(a)⊕ρ(b)` — cyclic shift is an *automorphism* of the XOR group, so `permute(x, k)` and `bind(x, position_glyph(k))` are algebraically interchangeable. Running both creates aliasing between position schemes with no added expressive power. The implementation is a per-bit loop over 10,000 bits, self-described in the source as "Correct, not yet fast", versus a word-parallel XOR. **Migrate the 4 real users** (`predictive_column.cpp` ×4, `whetstone.cpp` ×4, `crucible.cpp` ×2 — the latter dies with redundancy) to `position_glyph`, then delete. Keep exactly one asymmetry-breaking permute if `whetstone.cpp:280`'s transition encoding measurably needs it, confined to that file. |
| **Reverie's perturb-and-bundle-two-random-memories** | `reverie_loom.cpp` | No biological analogue exists for an unconstrained bundle of two random memories, and it manufactures spurious associations — precisely the failure mode down-selection is designed to prevent. Replaced wholesale in S5. Keep the *generative* character (biological replay reconstructs — it covers never-traversed paths, Gupta 2010); constrain it, do not make it verbatim playback. |
| **The abstraction tower's generative machinery above the measured honest depth** | `cogitator.cpp` | The project has already measured this: pruning 298 → 89 abstractions and depth 32 → 7 changed faculty scores by **−0.007** (inference 0.985, deduction 1.0, abstraction 0.929 → 0.922). 210 deleted abstractions contributed nothing. Cap generation at the measured saturation depth and delete the climb machinery above it; re-measure faculties. If they do not move, delete the rest of it too. |
| **`Lattice::query`'s linear scan as the *only* path** | `lattice.hpp:28` | Demoted, not deleted, in S4 — it becomes the correctness oracle, exactly the contract `maelstrom.hpp` already states. Also fix its non-determinism in ties while touching it. |
| **The process-wide `shared_mutex` as the concurrency architecture** | wherever the eight background threads contend | Not a feature deletion — an architecture deletion. S3's read phase requires no exclusive lock. Report the measured wall-clock delta; do not claim a speedup that is not measured. |

**Under measurement, not yet condemned:** `Whetstone::evolve()`/`revert()`. If evolution means tuning scalar parameters rather than changing structure, it is a hill-climber with a grand name. Instrument what it actually changes over 100 rounds and decide from the log.

**Net:** ~700–900 lines deleted before ~1,500 are added. The ratio matters.

---

## 5. THE HONEST RISK REGISTER

**R1 — The two-substrate boundary leaks.** *Most likely to actually happen.* A function takes a `Glyph`, returns an `Sdr` without going through `project()`, someone adds a reverse projection "just for debugging", and within three months there are two half-correct algebras and nobody knows which invariants hold where.
*Prior art:* Khora-specific, but the general shape is Sigma inverted — Sigma paid a uniformity tax on every feature; a two-code system pays a boundary tax on every interface.
*Early warning:* any `Sdr → Glyph` function; a density assertion firing in the `Glyph` path; a `Trace` binarised while still loaded.
*Mitigation:* no reverse projection, ever. Distinct namespaces. Assertions in debug builds at every boundary.

**R2 — Temporal memory beats nothing Khora actually does.** S2 passes the high-order sequence test and no Crucible faculty moves.
*Prior art:* HTM, precisely. Best honest result was ~parity with LSTM; Struye & Latré 2020 found plain MLPs matched temporally-aware models on the same tasks. Parity is a reason for nobody to switch.
*Early warning:* metric 1 green, metrics against Crucible flat, and S5 (the only consumer of the anomaly score) is not yet consuming it.
*Kill criterion:* pre-committed in S2. Revert.

**R3 — Someone proposes a hierarchy.** Stacking `TemporalMemory` instances.
*Prior art:* the H in HTM was never delivered. No published working hierarchy, no credit assignment across regions, and Numenta archived the entire codebase read-only on 2023-09-01.
*Early warning:* any design document containing the word "region 2".
*Mitigation:* build **one** region and get compositional structure from the VSA algebra — exactly the capability HTM lacked and Khora already has.

**R4 — Prioritised replay loses to uniform random.** The Gain×Need machinery is elaborate and does nothing.
*Prior art:* the digest itself flags this as an open question in the biology.
*Early warning:* the computed priority correlates > 0.95 with plain recency; or coverage entropy collapses.
*Mitigation:* S5 metric 1 compares three conditions by construction. Delete the machinery, keep recency, report the negative result.

**R5 — The plan eats the project.** Seven stages, months of work, and the thing that used to run stops running.
*Prior art:* OpenCog (20+ years, two rewrites, still "pre-alpha", zero externally-cited results). Sigma (better idea, dead). HBP (€600M–1B, executive committee dissolved at year 2).
*Early warning, quantified:* more than **two consecutive stages** where `ctest` is not green at the stage boundary; or any stage taking more than **3×** its predecessor's elapsed time.
*Mitigation:* every stage leaves the build green by construction. Stages S1, S4, S6, S7 are individually revertible with no downstream dependency loss.

**R6 — Sparse block codes are slower than dense on real silicon.** 256-byte gather-and-compare versus 157 words of AND+POPCNT.
*Early warning:* S1 metric 3.
*Mitigation:* it is a gate on starting S2, not a discovery made afterwards.

**R7 — Criticality is unmeasurable at Khora's scale.**
*Prior art:* Destexhe & Touboul 2021 — Brunel networks and OU processes pass the crackling test; a neon-lamp lattice at a first-order transition gives the "critical" exponents. And there is not a single study demonstrating power laws for spikes in awake animals.
*Early warning:* the three diagnostics disagree.
*Mitigation:* S7 is last, conditional, and its kill criterion is the *payoff* metric (dynamic range), not the diagnostic metrics.

**R8 — Neuromodulation becomes seven magic numbers requiring per-corpus tuning.**
*Prior art:* ACT-R's predictive power comes partly from fitting free parameters per task, which is why it is excellent psychology and not AI.
*Early warning:* any parameter re-tuned per corpus.
*Mitigation:* S6 metric 3 (per-scalar ablation) deletes anything that does not earn its place.

**R9 — "Revolutionary" pulls the project toward unfalsifiable claims.** The single largest risk in this document, because it is a risk about *reporting*, not code.
*Prior art:* HBP promised to simulate a brain in ten years. Markram's 2009 TED talk was not approached.
*Early warning:* any claim in `CHANGELOG.md` without a number next to it; any metric invented after the result was seen.
*Mitigation:* §6 fixes the target in advance, with numbers, before any of it is built. The project's own rule — *nothing claimed unless built, run and observed* — is the correct rule and it should be extended: **no metric may be defined after the result is known.**

---

## 6. WHAT "REVOLUTIONARY" WOULD ACTUALLY MEAN

### 6.1 The gap

An LLM cannot: learn one fact at inference time and still know it after ten thousand more arrive; tell you, calibrated, that it has never seen a specific thing; name the ≤5 stored items that produced a given answer such that deleting exactly those changes it; or run in 512 MB on one core.

Soar and ACT-R cannot: acquire knowledge without a human authoring it. Forty years and competence still scales linearly with human hours.

HTM cannot: compose. Its semantic generalisation is entirely whatever the encoder gives it.

Classical VSA cannot: carry learned similarity. Random symbols make "cat" as far from "dog" as from "Tuesday".

Khora is the only system with all four ingredients present *today*: an exact VSA algebra (composition, exact unbinding, structured query — Crucible), a graph learned from corpus statistics (Plexus, PPMI), one-shot writes (Lattice), and a predictive column. That combination is the claim.

### 6.2 The target: the Continual Relational Benchmark

A single, dated, falsifiable specification. Written before implementation begins.

**Setup.** A stream of **N = 10,000 structured facts** across **T = 20 disjoint domains**, arriving one at a time, **each seen exactly once**. No retraining pass. No gradient. No pretrained weights. No network access.

**Five numbers, all required:**

| # | Criterion | Target | Measured by |
|---|---|---|---|
| **a** | **Retention.** Structured-unbind accuracy on domain 1, measured *after* all 19 subsequent domains have streamed past. | **≥ 0.90** | `RelationalCrucible::trial_structured_unbind` (exists) |
| **b** | **Composition.** Cross-domain analogies never presented during the stream. | **≥ 0.80** | `RelationalCrucible::trial_analogy` (exists) |
| **c** | **Calibrated novelty.** 1,000 probes, 500 of them held-out facts never streamed. Separate seen from unseen. | **AUC ≥ 0.90** | S2's bursting fraction |
| **d** | **Budget.** Whole run, end to end. | **< 512 MB resident, < 60 s wall, 1 CPU core, no GPU** | |
| **e** | **Provenance.** For any answer, emit ≤ 5 stored items. Deleting exactly those items must change the answer. | **100% of sampled answers** | new; validity depends on S3 |

**Why each is hard for the incumbents.**

- **(a)** An LLM fine-tuned sequentially on this stream loses domain 1 catastrophically; an LLM given the facts in context cannot hold 10,000 of them, and cannot be asked which it hasn't seen. This is the Barnes–Underwood AB/AC paradigm at scale, and it is the exact thing CLS was formulated to explain.
- **(c)** **This is the one an LLM structurally cannot do.** It has no mechanism that reports "this specific fact was never in my input" — its confidence is a function of the output distribution, not of retrieval. Bursting is a *count of columns with no matching distal segment*. It is a fact about the system's own memory, not an inference about the world.
- **(e)** Falsifiable causal explanation, not post-hoc rationalisation. An LLM's chain-of-thought is not causally load-bearing; deleting the cited source does not change the answer. Here it must. Note that (e) is **only valid if S3 holds** — without the phase gate, an item can be evidence for its own reinforcement and the provenance chain is circular.
- **(a) + (c) + (e) together** is the combination. No LLM has it. No cognitive architecture has it. HTM has (c) and not (a) or (b). VSA has (b) and not (c).

**Stage-to-criterion mapping** — every stage must move a specific number, or it does not ship:

```
S1 → (b) headroom, (d)          S2 → (c)
S3 → (e) validity                S4 → (d)
S5 → (a)                         S6 → (c) calibration
S7 → nothing yet named — which is why it is conditional
```

### 6.3 What does NOT count

Stated explicitly, because the failure mode is drift toward the easy claim.

- **"It generates fluent text."** No. That is the incumbent's game, played worse. `PredictiveColumn::babble`'s own doc comment is already honest about this: *"it tends to replay the (context→next) transitions it memorised, so output is locally coherent but not novel."*
- **"It reached depth 148 of abstraction."** No. The project has already been burned by this once and correctly identified the honest depth as 7.
- **"It is more brain-like."** No. Not once, anywhere, as a justification.
- **"It matches an LLM on X."** No. Parity is a reason for nobody to switch. The bar for a non-mainstream architecture is a capability the mainstream cannot deliver at all.
- **Any number produced by a benchmark Khora invented and scores itself on, without a dumb baseline reported alongside.** NAB was Numenta's dataset *and* Numenta's scoring function, and independent analysis showed near-trivial detectors scored comparably. That single choice is a large part of why HTM was never taken seriously. The CRB must ship with: a random baseline, a recency-only baseline, and a no-replay baseline, all reported in the same table as Khora's number.

### 6.4 The honest statement of what this is

This is an engineered algebra that borrows tissue motifs where they earn their place, not a brain model. No codebook has ever been measured in cortex; the field's own review calls neural variable binding "completely unsolved" (Feldman 2013); and measured representational drift across hippocampus, posterior parietal and piriform cortex is precisely the failure mode a code with exact inverses cannot tolerate.

The four things borrowed — proximal/distal separation, subsampled matching, two stores at two rates, and the encode/retrieve phase gate — are borrowed because each has a measurable consequence and a metric that would falsify it. Everything else in the research digest is left on the shelf, on purpose, with a reason.

---

## APPENDIX A — Files

**New:**
```
C:\Khora\include\khora\lattice\sdr.hpp
C:\Khora\src\lattice\sdr.cpp
C:\Khora\include\khora\cortex\temporal_memory.hpp
C:\Khora\src\cortex\temporal_memory.cpp
C:\Khora\include\khora\hippocampus\episodic.hpp
C:\Khora\src\hippocampus\episodic.cpp
C:\Khora\include\khora\neuromod\neuromod.hpp
C:\Khora\bench\substrate_health.cpp
C:\Khora\tests\{sdr,temporal_memory,phase_gate,crb}_test.cpp
```

**Deleted:**
```
C:\Khora\src\synapse\  C:\Khora\include\khora\synapse\  C:\Khora\tests\synapse_test.cpp
Glyph::sparse()                                   glyph.cpp:48, glyph.hpp:32
Glyph::permute_inplace / permute                  glyph.cpp:90-104 (after 4 call sites migrate)
RelationalCrucible::evolve_structured + redundancy crucible.hpp:88-96, crucible.cpp:52-58,124
```

**Heavily modified:** `reverie_loom.cpp` (rewritten), `lattice.cpp` (+index), `plexus.cpp` (+downscaling), `cogitator.cpp` (phase gate, tower cap), `predictive_column.cpp` (delegate then retire).

## APPENDIX B — Parameters, with citations, all runtime-tunable

Every value below is a measurement from a specific preparation at a specific temperature, and several are contested. None may be a compile-time literal; each carries its citation in a comment.

```
Sdr:  n=16384  B=256  L=64  w=256  density=1.5625%
      derived from union/subsample tables, Ahmad & Hawkins 2015/2016

TM:   columns=2048  cells=32  active_cols=40 (2%)
      theta=13  minThreshold=10  maxNewSynapseCount=20
      perm: init=54 connected=128 delta=26  (uint8, = 0.21/0.50/0.10)
      predictedSegmentDecrement=0.01   [NOT 0.0 — see S2]
      maxSegmentsPerCell=128  predictionLifetime=3 ticks
      nupic-legacy / htm.core defaults; Hawkins & Ahmad 2016

CLS:  episodic_density=1.5%  semantic_density=~50%
      write_rate_ratio >= 30:1        Squire et al. 2015
      interleave new:old = 1:3        McClelland et al. 1995
      novelty_multiplier=6, decay 2-3 exposures   Cheng & Frank 2008
      recency tau=30 min              Kudrimoti et al. 1999
      priority exponent alpha=0.6     Schaul et al. 2016
      burst=5-20 items, 15-20x compressed         Davidson et al. 2009
      forward:reverse=2:1             Diba & Buzsaki 2007
      downscale=0.82, spare top 20%   de Vivo et al. 2017

Mod:  recurrent=0.78 afferent=1.00 downstream=1.69 resting=0.68
                                      Linster et al. 1999
      mix = (1-ACh)(1-NE); reset iff NE > ACh/(0.5+ACh)
                                      Yu & Dayan 2005
      RPE baseline EMA alpha=0.7, window 150ms @ +75ms
                                      Bayer & Glimcher 2005
      negative RPE scale 0.3-0.5      [explicit, not inherited]

Crit: m_target in [0.96, 0.99]        Wilting & Priesemann 2018
      DCC < 0.2                       Ma et al. 2019
```