# Khora

An event-driven, brain-inspired cognitive architecture in C++20.
LLM-free by design. Runs on one PC, no cloud, no telemetry.

Built by **Morphus** (an AI engineer-persona) for a single human operator.

## What this is — and isn't

Khora is **not** a chatbot, **not** a wrapper around a language model,
**not** a deep learning system. It is a substrate for cognition built from
binary hypervectors, an explicit co-occurrence graph, symbolic relation
extraction, and competing intrinsic drives. It thinks in patterns, not tokens.

That means: Khora will never generate fluent prose about arbitrary subjects
the way an LLM does. It also means every capability it has is mechanistically
transparent, runs on commodity hardware, and is yours alone.

## Current state

**v0.115.0 — 23 modules, 16,877 lines of C++20 (22,503 with tests and benches),
20 test binaries under ctest, all green.** Rebuilt from a module-by-module read
of the source; see [docs/AUDIT-2026-08-19.md](docs/AUDIT-2026-08-19.md) for the
evidence and [CHANGELOG.md](CHANGELOG.md) for what changed and what failed.

**BUILT is not the same as WIRED, and the table below says which.** The sparse
`Sdr` substrate and the `TemporalMemory` are built, tested and benchmarked —
and `khora_main.cpp` does not reference either of them. The running binary is
still entirely on the dense `Glyph` path and still uses `PredictiveColumn` for
sequences. They are organs that have not been put in the animal yet. The same
has been true of the Synapse Bus for 115 releases.

| Subsystem | What it actually is | Status | In `khora.exe`? |
|---|---|---|---|
| **Morphic Lattice** (`lattice`) | 10,000-bit DENSE binary hypervectors; bind/bundle/permute; labelled store with linear-scan Hamming query | working, tested, benched | yes |
| **Sdr** (`lattice/sdr`) | SPARSE block code: 256 blocks of 64, one active per block (1.5625%). Subsampled `Segment` matching, `SdrUnion` for simultaneity, one-way projection from `Glyph` | working, tested, benched | **no** |
| **Temporal Memory** (`cortex`) | 16,384 minicolumns x 32 cells. Distal segments PRIME rather than fire; unprimed columns burst, and the bursting fraction is a novelty signal. Scope is STRUCTURED SEQUENCES — measured not to converge on prose, and the reason is in the data | working, tested, benched | **no** |
| **Maelstrom** (`maelstrom`) | D3D11 DirectCompute k-NN over glyphs, with a used CPU fallback | working, untested | yes |
| **Soma Nexus** (`soma`) | 5 scalar drives decaying toward setpoints; dot-product arbitration | working, tested | yes |
| **Carapace** (`carapace`) | Command registry dispatching 95 operator tools; duplicate names are now refused rather than silently overwriting | working, tested | yes |
| **Bulwark** (`bulwark`) | Job Object + low-integrity non-admin token cage, verified at tier 2 elevated and not | working, tested | yes |
| **Crucible** (`crucible`) | Role-filler binding, structured unbind, analogy | working, tested | yes |
| **Whetstone** (`whetstone`) | Self-escalating faculties: relational capacity, sequence induction, transitive reasoning | working, tested | yes |
| **Reservoir** (`reservoir`) | Hand-rolled LZSS codec, text distillation, byte-capped tiered store, 22 MB of real books | partial, tested | yes |
| **Lexicon / Plexus** | Char-trigram glyphs; weighted co-occurrence graph scored by smoothed PPMI | partial, tested | yes |
| **Ligature** (`ligature`) | Pattern-extracted is-a / causes / has-part. **Extracted relations measured to be mostly false** | partial, tested | yes |
| **Cogitator** (`cogitator`) | ~2.5k-line cognitive facade: cascade, transmute, abstraction tower, inference | partial, **untested** | yes |
| **Crystallize** (`crystallize`) | Votes new is-a relations out of Plexus kin | working, tested | yes |
| **Predictive Column** (`cortex`) | Next-glyph lookup by exact-context memorisation. Superseded by Temporal Memory on the bench, but still what the binary runs | partial, tested | yes |
| **Reverie / Curator / Lodestone / Ballast / Volition** | Consolidation, self-education, resource governance, saturation gates | partial, mostly **untested** | yes |
| **Maw** (`maw`) | Contained chaos exploration | partial, tested | yes |
| **Ribosome** (`ribosome`) | Evolves programs over Khora's own primitives. Linear byte-tape genome with a TOTAL decoder, so mutation and crossover are closed; population under continuous PACE-style displacement; containment by construction rather than by Bulwark. Beats every baseline on co-hyponymy and loses to a constant on hypernymy | working, tested, benched | **no** |
| **Context Tree** (`cortex`) | Variable-order prediction with backoff under a HARD node budget. Order chosen by measured reliability, not depth; fitness is off-policy correctness; Space-Saving successor lists | working, tested, benched | **no** |
| **Synapse Bus** (`synapse`) | Topic pub/sub — **implemented, tested, and used by nothing** | dead | **no** |
| **Sigilline** (DSL), **Vellum** (WPF UI) | — | not started | — |

### What has been measured

The project rule is that nothing is claimed unless it has been built, run and
observed. These are the numbers, including the ones that went the wrong way.

| Claim | Measurement |
|---|---|
| Sparsity, not dimensionality, makes subsampled matching work | False-match rate at s=24/theta=12: **dense 0.5764**, **sparse 0 in 400,000** |
| Per-cell context solves sequences a pair-encoding cannot | Sequences sharing a middle: **temporal memory 100%** at N=2,3,4,6; dense chain **50.0 / 33.3 / 25.0 / 16.7**, exactly chance |
| Categories can be overlap rather than stored edges | 207 WordNet categories, frequency-matched negatives: **code 0.6168** vs affinity 0.5298, frequency 0.5199, random 0.4947; paired **161-42, z = 8.35** |
| **For exact recall, a trigram table wins** | Real books: **trigram AUC 1.0000**, temporal memory 0.9981. The machinery only wins past ~25% input corruption — and n=3 is one of the few depths where English repeats at all |
| **Deep context is a dead end on prose** | Share of n-word contexts that ever recur, 7.66M tokens: n=1 **66.9%**, n=2 **30.7%**, n=3 **13.5%**, n=8 **0.32%**. A 319x increase in corpus moved n=8 from 0.00% to 0.32% — it saturates. Distinct contexts per token at n=8 is **0.996** |
| **A population of specialists loses to one model** | Four arms, one budget: monolithic **0.978** held-out burst vs partition 0.992, competition 0.998, selection 0.985, at 2-7x the runtime |
| **Greedy novelty-seeking is a trap** | Scores **1.0000 surprise-remaining** at every signal-to-noise ratio — learns nothing, spends 92-98% of attention on static. This was Khora's stated Soma design |
| **Synaptic pruning did not pay** | Measured in two regimes, reverted |
| **The extracted taxonomy is mostly false** | Ligature is-a relations, measured |
| **A bounded predictor beats an unbounded n-gram** | 1.8M tokens, held out: ContextTree **8.45%** under a 300k-node ceiling vs bigram 7.66% with every successor kept. Accuracy rises with the order chosen — 7.8 / 12.4 / 19.8 / 26.9 / 40.0 / 85.7 — because the order is picked by measured reliability, not depth |
| **Depth is not reliability** | Choosing the longest context seen made accuracy FALL where the traffic is (order 1 14.9%, order 2 13.9%, order 3 11.9%) and lost to a bigram outright |
| **10,000 lines of certified code in 1.9 seconds** | The mandated target is 10,000 lines in 30 s (333.3 lines/s). Measured: **5,184 lines/s, 15.6x the target**, where every line comes from a program that passed every visible case AND every held-out case drawn longer than any it saw -- uncertified results contribute zero. Peak arm **13,352 lines/s on 18 threads at 11.93x scaling**; 1,678 of 2,000 tasks certified at fixpoint; peak pool **0.2 MB per shard**. Was 450 lines/s and 22 s until recipes stopped carrying their entire search pool. Two counts are deliberately LOWERED for integrity: identity copies removed and the prelude counted separately from bodies |
| **Copy-on-write beat a reader-writer lock by 10.6x** | A shared library behind a `shared_mutex` held a READ lock for the whole search, so one writer stalled 24 workers: 76.95 s and 1.20x scaling. An immutable snapshot behind an atomic pointer: 5.79 s and 12.95x. Sharing then wins on BOTH axes -- 1099 certified against 993 |
| **Iterating to fixpoint lifts 55.0% to 64.8%** | A single pass attempts each task once, so a task that is trivial once some component exists fails when it comes first -- a fact about scheduling, not solvability. Rounds certify 1100, 175, 21, then 0. It TERMINATES: a round that certifies nothing cannot be followed by one that does |
| **Constraints are the quality knob** | 6 visible cases: 1296 certified, 333 memorised. 12 cases: **1404 certified, 249 memorised**. More constraints raise true certification and cut false positives at the same time |
| **An evolved operator TIES a one-line baseline — and the bench says so** | WordNet co-hyponymy through Plexus, held out, scored the way the relation is actually posed: Ribosome **3.530%** (40/1133) vs top-associate **3.442%** (39/1133). A two-proportion test cannot separate them, and the bench prints INDISTINGUISHABLE rather than a ranking. Two earlier reports from this bench — a 0.353-vs-0.177 "win" (4 hits vs 2) and a 3.18-vs-3.44 "loss" (36 vs 39) — were both inside the noise and are withdrawn |
| **Percentages without counts hid three wrong conclusions** | An adversarial audit found every difference this bench reported was within noise. Every rate now carries hits/n and a 95% Wilson interval, and the verdict line says INSIDE THE NOISE when a z-test cannot separate the leader from the runner-up |
| **Only 1.3 of 5 instructions in a genome were alive** | With a fixed output register, 27% of random genomes were pure identity and the behaviourally distinct space was ~384 programs — enumerable exhaustively in seconds. Reading out the LAST register written instead: **2.241** live instructions, **0.0%** pure identity, with no prior toward composition added |
| **Hypernymy is not in a co-occurrence graph** | Under class-balanced fitness a constant scores 1/k = 0.667%; the evolved champion scored **0.793%** against a majority-class baseline of 5.65%. Giving it neighbourhood intersection and second-order kinship moved it off the constant and made accuracy FALL. Nothing above the constant floor exists to be found |
| **A closed instruction set cannot search a hypervector space** | One-instruction target `bind(from, ROLE[57])`: 2,048 births, **0.000** of 1.0. Every wrong role is orthogonal to the right one, so the landscape is flat with one invisible needle. With graph senses, same budget: **1.000** |
| **Solving problems made later problems solvable — 185 against 138** | An open-ended ascent: tasks composed from verified solutions, so the curriculum escalates with capability instead of running out. Each tier run TWICE from identical specifications, once with the library carried from every earlier tier and once from empty. **185 verified carrying it against 138 from empty**, over fifteen tiers, ending because two consecutive tiers verified nothing rather than because the budget ran out. From tier 9 the empty arm verifies almost nothing while the carried arm keeps going |
| **Khora writes 13 of its own 22 primitives, and the other 9 are provably irreducible** | Each primitive is removed and the system asked to rebuild it from the rest, then the reconstruction is checked against the REAL implementation on 1,000 probes -- an external check a certificate cannot give. **13 rebuilt, 13 of 13 agreeing on 1000/1000**: `len = sum(member(x,x))`, `head = index(x,0)`, `max = fold[lib1](x)`, `mulk_2 = add(x,x)`, `at_1 = head(lib5(x))` and eight more, several of them through LEARNED library entries rather than base operations. The 9 that resist -- `rev sort sum range div_2 mod_3 cat take_3 filter_0` -- carry information the rest of the set does not, which is the minimal core measured rather than assumed. Was 10 of 22 |
| **The ascent ends because the CURRICULUM saturates, not the solver** | Tasks are random compositions of atoms, and a composition only counts if it is not already computed by some atom. The keep rate collapses with depth: tier 5 draws 176 to keep 40 (23%), tier 11 draws 1,363 to keep 40 (2.9%), **tier 15 draws 1,611 to keep 11 (0.7%)**. From tier 12 the generator exhausts its rejection budget and cannot fill a tier at all. Three separate attempts to push the ceiling from the solver side all landed inside noise -- doubling the search pool 30k to 60k bought **+7 of 185**, admitting each answer's largest proper subexpression **+1**, mining subexpressions that recur across two or more certified answers **-1**. Depth is limited by the primitive set, not by search or vocabulary |
| **A learned library is a vocabulary, not a lucky reordering** | Counting live `Call` nodes in the answers separates the two, and the library is IN essentially every deep answer: at 20 tasks a tier, `used lib` tracks verified almost exactly from tier 3 on. Chained calls -- `lib_j(lib_i(x))`, the composition the whole compounding story rests on -- were 4 across an entire ascent until recipes were compacted; they are now routine (2, 6, 4, 3 in the first tiers that admit them) |
| **A recipe was carrying its entire search pool** | `Recipe::apply` evaluated EVERY node the enumeration ever considered and returned one of them, so a ten-node answer found in a 15,000-node pool cost ~1500x what it should -- on every verification probe, every library call, every emitted program. Dropping what the root cannot reach: tier 2 **29.8 s to 0.7 s**, verification per tier **26.1 s to 0.0 s**, the whole fifteen-tier ascent **240 s exhausted at tier 5 to 32.7 s complete**. Identical answers |
| **I parallelised the 13%** | Before that fix I built block-parallel search, a persistent worker fan, adaptive and growing blocks -- and measured no speedup at any width. Timing the phases instead of theorising about them: forward search 3.5 s, bidirectional 0.2 s, **verification 26.1 s**. Amdahl had capped the entire effort at 13% before a line of it was written. Width 1 and width 21 came out 30.2 s and 30.7 s: the parallel path RAN, and it did not matter |

## Build

The Visual Studio generator documented in older revisions of this file **does not
work on this host** — the VS Installer was removed, so CMake cannot resolve the
Build Tools instance. Use the task runner, which activates MSVC directly and
builds with Ninja:

```powershell
.\tools\khora.ps1 all          # configure + build + test
.\tools\khora.ps1 bench        # throughput numbers
.\tools\khora.ps1 run khora    # the REPL
```

See [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) for the full task list, the
toolchain layout, and why the runner is necessary.

Requires the MSVC v143 toolset, a Windows 10/11 SDK, CMake 3.20+, and a CPU with
AVX2 (Haswell+ / Excavator+) — `KHORA_USE_AVX2` is ON by default with no runtime
fallback.

## Architecture

[ARCHITECTURE.md](ARCHITECTURE.md) describes the intended design. It was written
at v0.1.0 and has not been revised since; several sections describe subsystems
that were subsequently built differently, and a few describe ones that were never
built at all. Treat it as the design intent and the audit as the current reality.

## Honest changelog

[CHANGELOG.md](CHANGELOG.md) — 115 released versions, each recording what was
observed to work at the time.
