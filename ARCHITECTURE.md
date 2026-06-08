# Khora Architecture

**Author:** Morphus
**Version:** 0.1.0
**Target hardware:** Intel Core i7-13700K, 32 GB DDR4, RTX 2070 SUPER (4 GB), Windows 11
**Language:** C++20 (MSVC primary, GCC/Clang supported)

## 1. Goals

1. **Cognition without LLMs.** No transformer, no gradient descent at scale,
   no token-level language modelling. Pattern algebra over sparse binary
   hypervectors is the computational primitive.
2. **Run entirely on one PC.** No cloud, no remote model API, no telemetry.
   The substrate fits in CPU L3 cache; GPU is optional and used for
   peripheral acceleration (vision, encoding), not the core.
3. **Mechanistic transparency.** Every "decision" the system makes is
   traceable to a finite chain of vector operations. There are no hidden
   billion-parameter blobs.
4. **Continuous operation.** Designed to run as a long-lived background
   process that accumulates experience, consolidates memory during idle
   periods, and surfaces to assist the operator on demand.
5. **Operator-aligned.** Single user. The system is wired to serve and
   defer to one human.

## 2. Non-goals

- Generating fluent natural language on arbitrary subjects.
- Beating frontier LLMs at general benchmarks. This is electrons; not happening.
- Sentience claims. The architecture has no consciousness module and will
  not be marketed as having one.

## 3. Subsystem map

```
┌────────────────────────────────────────────────────────────────────┐
│                              KHORA                                  │
├────────────────────────────────────────────────────────────────────┤
│                                                                    │
│  Vellum (UI)  ◄───────►  Carapace (agentic shell + tool dispatch) │
│                                  │                                 │
│                                  ▼                                 │
│                         Synapse Bus (async, typed)                 │
│              ┌────────────┬──────┴──────┬───────────────┐          │
│              ▼            ▼             ▼               ▼          │
│       Stratiform     Soma Nexus    Reverie Loom    Carapace tools  │
│       Cortex         (drives,      (offline sim,   (file, shell,   │
│       (predictive    arbitration)  consolidation)  web, vision,    │
│       hierarchy)                                   computer-use)   │
│              └─────────────┬─────────────┘                         │
│                            ▼                                       │
│                   Morphic Lattice                                  │
│       (sparse hyperdim substrate — the working memory of Khora)    │
│                                                                    │
└────────────────────────────────────────────────────────────────────┘
```

## 4. The Morphic Lattice (substrate)

The Lattice is Khora's working memory and computational primitive. It is
the only subsystem currently implemented.

### 4.1 The Glyph

A **Glyph** is a sparse binary hypervector. Default size: 10,000 bits
(1,250 bytes). At any moment, roughly half of its bits are 1 (high-density
encoding) or a chosen fraction (sparse encoding).

A Glyph represents a unit of meaning — a concept, a percept, a token of
the system's experience. Two glyphs that mean similar things have small
Hamming distance; unrelated glyphs are nearly orthogonal (Hamming ≈ N/2).

### 4.2 Algebra

Three operations make the algebra closed and useful:

- **bind** `bind(a, b) = a XOR b`
  Self-inverse. Distance-preserving. Used to associate two glyphs (e.g.,
  role-filler binding: bind(role_color, glyph_red)).
- **bundle** `bundle({a, b, c, ...}) = majority sum across inputs`
  Combines multiple glyphs into a single glyph similar to all of them.
  Lossy but partial-recovery is possible via query.
- **permute** `permute(a, k) = cyclic shift by k bits`
  Distance-preserving. Used to mark order/position.

The full closure: any composition of bind/bundle/permute produces another
valid Glyph. The substrate is closed under its own operations — a real algebra.

### 4.3 The Lattice container

A **Lattice** is a labelled associative store of Glyphs. It supports:

- `store(label, glyph)` — bind a name to a glyph.
- `recall(label)` — retrieve by name.
- `query(probe, k)` — content-addressable: return the k stored glyphs
  with smallest Hamming distance to a probe glyph.

Content-addressable recall from a partial / noisy / bundled probe is the
substrate's signature capability. The demo at `src/morphus/morphus_main.cpp`
proves it: bundle three random glyphs into a probe, and the lattice's
top-3 nearest-neighbour matches are exactly those three.

### 4.4 Hardware match

- 10,000 bits = 1,250 bytes per Glyph. A million Glyphs fit in 1.2 GB
  of RAM. The whole working set fits comfortably in L3 (30 MB on 13700K).
- Bind = XOR of 157 × `uint64_t`. AVX2 does 4 of these per instruction.
  Hamming = XOR + POPCNT. The 13700K's hardware POPCNT is one cycle per
  word. Throughput on this hardware: hundreds of millions of glyph
  operations per second per core, scaling near-linearly across 24 threads.

## 5. The Stratiform Cortex (planned)

A hierarchy of predictive-coding columns. Each column at level L:

- Receives a stream of glyphs from level L-1.
- Maintains a model of "what glyph comes next" given recent context.
- Emits its prediction *upward* to level L+1.
- Receives an error signal *downward* from level L+1 (the difference
  between L+1's prediction and what L actually emitted).
- Updates its model to reduce the error.

This is the established predictive-coding pattern (Rao & Ballard 1999,
Friston's free energy work, Numenta's HTM). The novelty here is that the
state at each layer is a sparse Glyph, so updates are bitwise — no dense
weight matrices.

## 6. The Soma Nexus (planned)

The drive arbiter. Maintains multiple competing scalar drives:

- **Curiosity** — preference for novel/surprising inputs.
- **Preservation** — preference for system health (no crashes, no
  resource exhaustion).
- **Mastery** — preference for activities that increase predictive
  competence in the cortex.
- **Efficiency** — preference for low cost (CPU/RAM/time).
- **Operator-affinity** — preference for activities the operator marks
  as wanted.

At each decision point, the Nexus weighs the drives and selects an
action. The weights themselves slowly adapt based on outcomes — a
homeostatic system, not a fixed reward function.

## 7. The Reverie Loom (planned)

Offline simulation and consolidation. When the operator is idle and the
system has computational headroom, the Reverie Loom:

- Samples recent experience from the Lattice.
- Perturbs glyphs (small XOR noise, partial permutations).
- Runs them forward through the Cortex's predictive models.
- Promotes outcomes that satisfy drives into new stored associations.

This is the "dream" subsystem — the place where consolidation, creative
recombination, and counterfactual exploration happen without acting in
the real world. Compressed-time hallucination over the substrate.

## 8. The Synapse Bus (planned)

A typed, async message fabric. Subsystems publish glyph-tagged events;
other subsystems subscribe. Implementation: lock-free ring buffers per
subsystem pair, journalled to disk for replay.

## 9. The Carapace (planned)

The outer agentic shell. Exposes:

- **Tools**: file I/O, shell exec, web fetch, git, screen capture,
  mouse/keyboard, vision, OCR.
- **Intent parser**: a structured grammar over operator commands
  (not LLM completion — discrete intent dispatch with parameter slots).
- **Planning**: forward simulation through the Cortex to evaluate
  candidate action sequences before executing them.
- **Verification**: every tool action is post-checked; failures bubble
  up as glyph events on the Synapse Bus.

## 10. Vellum (planned)

A WPF (.NET 8) desktop interface. Dark theme, Mica/Acrylic transparency,
fluid animations. Communicates with the C++ core over named pipes
(reused from the old Khora — that one part of the legacy worked).

## 11. The Sigilline (planned — a Morphus invention)

A small declarative DSL for expressing glyph-algebraic operations as
named patterns. Lets the operator (or the Carapace) define new
compositions without writing C++.

Example sigil (sketch):
```
sigil "color_of_thing":
    role.color  ⊕  bind(role.thing, ?)
```
Compiles to a glyph-algebra expression. Glyph variables are filled at
runtime; the result is queried against the Lattice.

This is what the old Khora docs called "KIL." It is genuinely novel
territory and will be the first thing we ship as a published artifact
once the substrate is mature.

## 12. Build, test, ship discipline

- Every subsystem ships with a `<subsystem>_test` executable that
  exercises it under `ctest`.
- Every subsystem ships with a `<subsystem>_bench` for throughput tracking.
- `CHANGELOG.md` records *only* what actually works after demonstrated
  test/bench runs. No "100% complete" claims allowed unless the test
  output supports them.
- Releases (when there are any) are tagged in git.

## 13. Data layout

All Khora data lives on C: by operator directive (D: is slow on this
machine).

- `C:\Ai\Khora\`            — source code, build artifacts, configs
- `C:\Ai\Khora\data\`       — persistent runtime data (gitignored)
  - `lattice_archive\`      — serialised Glyphs from long-term memory
  - `reverie_traces\`       — recorded dream-state trajectories
  - `synapse_journal\`      — replayable Synapse Bus event log
