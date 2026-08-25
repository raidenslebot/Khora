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
| **The ascent ended on the CURRICULUM, not the solver -- and fixing that took it from tier 15 to tier 20** | Tasks are random compositions of atoms, kept only if they do not collapse to something an atom already computes. The keep rate fell to **0.7% by tier 15** (1,611 drawn, 11 kept) and from tier 12 the generator could not fill a tier at all. Three attempts to push the ceiling from the SOLVER side all landed inside noise: search pool 30k to 60k **+7 of 185**, admitting each answer's largest subexpression **+1**, mining subexpressions recurring across two or more answers **-1**. The cause was absorbing states -- `sum`, `len`, `take3` funnel into the singleton and the empty list, after which `sort`, `rev`, `pos` are identity. Five length-preserving, position-dependent atoms (`rot1 scan altneg idxmul ziprev`) have no absorbing state: **tier 15 to tier 20, 185 to 220 verified**, tiers full through 16 |
| **It can write functions of more than one argument** | Until now a `Case` held one input, so EVERY program this system could express was a unary function of one integer list -- the deepest limit in the tree, and the reason "writes anything" was false for a reason no amount of search speed touches. Arguments are now first-class leaves: `f(x, y) = append(x, y)` is synthesised from four examples, generalises to an unseen pair, and is proven to actually READ the second argument rather than pass by coincidence. It emits with the right parameter list in all 14 backends and is executed in six. The emitter's `op_fn` defaults to `kh_id`, so an unhandled argument node would have bound argument 1 to argument 0 in source that compiles and reads correctly -- handled explicitly instead |
| **ALL FOURTEEN backends executed and byte-identical against the certificate** | Not emitted -- COMPILED, RUN, and diffed line by line against `Recipe::apply`: **Python, JavaScript, TypeScript, Go, Rust, C++, C#, Java, Kotlin, Swift, PHP, Haskell, Lua, Ruby.** 16 recipes x 9 inputs, three recipes taking TWO arguments with argument 1 the NEXT input so a program that ignores it cannot pass by luck, three inputs longer than the 512-element bound where the interpreter clamps. Lua and Ruby run as real Lua 5.4 and CRuby 3.4 compiled to WebAssembly; the rest on native toolchains. Nothing is counted for being emitted -- Go was written as `package kh` and could never run while the harness printed a line count for it, and the `php` on PATH was a different tool wearing the name |
| **It does improve itself -- 22 to 38 against a bar that cannot move** | An evaluation set of 96 tasks drawn ONCE from a fixed seed, never regenerated, never filtered against anything learned, never admitted to a library. Between measurements the system trains on a DISJOINT stream and keeps what it certifies. **22 of 96 at the start, 38 after 864 training tasks -- while the empty-library control sits at exactly 22 at every stage.** Deterministic pipeline, and the flat control proves it, so no interval is needed. **16 gained, 1 lost.** Still climbing at stage 9, where before the regression was removed it went flat at stage 6. By depth: d2 12->13, d3 8->10, d4 2->8, d5 0->5, d6 0->1 |
| **Removing the library's regression was worth more than the library** | A library is a vocabulary AND a haystack -- every entry is another level-0 candidate for a bounded pool -- and `sort.delta`, two operations deep, was solved with an empty library and NOT with a 96-entry one. `construct_best` keeps the better of with-library and without, paying a second search only on tasks that already failed. Losses fell 3 to 1 and the total went **28 to 38**: the fix was worth **+10**, more than the entire library gain before it. The one remaining loss is a program that CERTIFIED -- passed every case and the holdout -- and failed the external 200-probe check, so the fallback never fired |
| **The benchmark's own case lengths were hiding 22% of the capability** | `make()` drew training cases at lengths 1-5 and holdout cases at 7-11 -- **disjoint ranges**. Any program whose behaviour depends on length passed every visible case and failed the holdout BY CONSTRUCTION. Measured on `idxmul`: the search produced `add(x, mul(x, range(5)))`, exactly `x*[1..5]`, exactly right for every length the cases contained. More cases did not help -- they were all short. Widening the cases to lengths 1-12 solved it outright: `add(x, mul(x, range(100)))`, 20/20 and 5/5. Tasks reported as beyond the ceiling were never beyond it. The fixed bar went **18 to 22 at base**, depth-2 from 10 of 16 to **12**, and depth 6 stopped being zero |
| **Solving for the operand instead of enumerating it: 16/20 to 18/20 on 19% fewer nodes** | The binary sweep tries every PAIR -- `end^2 x |ops|` -- which is what puts operation-depth 4 out of reach before a 200,000-node pool fills. But for an invertible operation the second operand is DETERMINED by the target: if the answer is `mul(a, b)` then `b` is `target / a`, and there is nothing to search for. A closing pass computes the required operand for each pool node and looks it up, **O(pool) per operation instead of O(pool^2)**, reaching one level deeper than the sweep that built the pool. On the fixed task set: **18/20 against 16/20, on 137,991 candidates against 169,437**. Every hit is re-verified forward, because `zip` cycles its shorter operand and an inverse that looks exact by construction can still be wrong |
| **Better search made the library a liability** | On that same fixed set the arms INVERTED: plain construction 18/20, construction with a learned library **16/20**. The library had been compensating for weak search, and once the search improved its extra level-0 entries cost more than the vocabulary returned. It still pays on the open-ended bar (+7 there), so this is benchmark-dependent rather than a verdict -- but "vocabulary substitutes for search" is now something measured in both directions |
| **The residual ceiling is ONE wall: search reach, not expressibility** | Breaking the fixed 96-task bar down BY DEPTH: depth 2 solves 10 of 16 and learning adds nothing; depths 3-5 hold every gain (7->9, 1->3, 0->3); depth 6-7 is zero before and after. The shallow misses looked like an expressibility floor -- `altneg` and `idxmul` are index-aware -- and **that was wrong**. `range(len(x))` is index-aware and was always in the set: `idxmul` is `mul(x, mapadd(range(len(x)), 1))`, hand-built and verified at **0 mismatches in 400 random inputs**, and the search finds `altneg` by itself as `mul(x, append(1, -1))` because `zip` cycles the shorter operand. Everything is expressible; the search cannot reach it. A task labelled depth 2 in ATOMS can need six operation nodes, so the benchmark's depth labels understate the real depth by up to 3x |
| **The +39% is a net: 8 gained, 1 LOST** | Itemised, the library breaks a task it could solve without one. `sort.delta` -- literally `Delta(Sort(x))`, two operations both in the set -- is solved with an empty library and unsolved with a 96-entry one, because every entry is another level-0 candidate competing for a bounded pool. The totals hide it; only a per-task diff shows it. Two further apparent losses turned out to be the INSTRUMENT: `holds_up` only runs on solved tasks, so its probe stream advanced differently under each library and a task was checked against different inputs in each arm. Reseeding per task removed them |
| **A learned library is worth about a tenfold search budget -- and then some** | Why the curve plateaus, on the same fixed 96 tasks. **pool 20k: 18 without a library, 25 with. pool 60k: 23 without, 28 with. pool 200k: 26 without.** So a library at a 20,000 pool matches raw search at 200,000 -- roughly 10x the compute. But it is not merely a compute substitute: at 60,000 the library reaches **28, beating raw search at 200,000 on a third of the pool**. The two axes compose, both plateau, and neither alone sets the ceiling -- which is why raising the library budget 5x bought exactly one task |
| **A self-generated curriculum cannot demonstrate unbounded self-improvement** | The ascent poses tasks built from what it has already solved, and a task counts only if it differs from every atom -- so enriching the vocabulary raises the bar for what counts as a problem at the same moment it raises the ability to solve one. Measured three ways: admitting every solved program as a primitive gives **tier 20, 220 vs 191**; admitting only behaviourally NOVEL ones -- better engineering -- gives **tier 16, 130 vs 117**; novel and non-absorbing gives **tier 18, 175 vs 161**. Improving the vocabulary moves the yardstick it is measured against. This is a property of the INSTRUMENT, and it is why no amount of work here will show an unbounded curve: that needs a fixed external task set hard enough to reward a richer vocabulary, which does not yet exist in this tree |
| **A library is a haystack as well as a vocabulary** | A 2x2, each cell the carried-minus-empty gap inside ONE run. 19 atoms: budget 32 gives **185 vs 138**, budget 96 gives **174 vs 138** -- the bigger library makes it WORSE. 24 atoms: budget 32 gives **214 vs 191**, budget 96 gives **220 vs 191** -- the bigger library makes it better. Every entry is another level-0 candidate, so the right size is a function of how diverse the problems are and not a constant to tune once. The richer atom set also DILUTES reuse: per-task library lift falls from 9.1 points to 4.0, because tasks that share less structure reuse less |
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
