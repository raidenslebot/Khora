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
| **An evolved operator does NOT beat a one-line baseline** | WordNet co-hyponymy through Plexus, held-out words, scored the way the relation is actually posed (any sibling counts): Ribosome **3.18%** vs top-associate **3.44%**. An earlier 0.353-vs-0.177 win was an artifact of scoring against one fixed sibling. The evolved champion's whole live body is `neigh r0 <- bundle top 3 of r0` — it rediscovered the baseline |
| **Hypernymy is not in a co-occurrence graph** | Under class-balanced fitness a constant scores 1/k = 0.667%; the evolved champion scored **0.793%** against a majority-class baseline of 5.65%. Giving it neighbourhood intersection and second-order kinship moved it off the constant and made accuracy FALL. Nothing above the constant floor exists to be found |
| **A closed instruction set cannot search a hypervector space** | One-instruction target `bind(from, ROLE[57])`: 2,048 births, **0.000** of 1.0. Every wrong role is orthogonal to the right one, so the landscape is flat with one invisible needle. With graph senses, same budget: **1.000** |

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
