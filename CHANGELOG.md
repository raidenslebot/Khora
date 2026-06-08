# Khora Changelog

Honest log of what actually works. Nothing claimed here unless it has
been built, run, and observed.

## v0.12.0 — The Morphic Cogitator + The Crucible

**Author:** Morphus

Two arrivals: a thought cycle that never accepts failure, and a forge
that proves Khora can *reason* — not retrieve.

### The Morphic Cogitator — recursive thought, no-surrender

`think(stimulus)` is no longer a single forward pass. It is a resolve
loop built on one principle: failure is the trigger for the next
attempt, never a verdict. Each pass: encode → resonate → if a memory
fires strongly, resolve to it; otherwise spike Curiosity, decompose the
stimulus into fragments, resonate each alone, synthesize a hypothesis
from the partial knowledge + the cortex's projection, **consolidate
that hypothesis into memory and the cortex**, enrich the probe toward
it, and re-attempt. Even at the attempt cap it never returns "no
answer" — it returns its best hypothesis and leaves Curiosity elevated
so the background Reverie keeps working the problem. Every act of
thought leaves Khora having learned something it did not know.

Wired into the runtime: `think`, `cogitator_stats`. Verified: with
"install" and "configure" in memory, `think "instal the sytem"`
(two typos) resolves to **install** in one pass via the Lexicon.

### The Crucible — relational reasoning forge (no more demos)

The demo paradigm is retired entirely (all `*_demo` sources deleted).
In its place: the Crucible, a serious trial-and-evolution harness that
drives the substrate against hard cognition and evolves it on shortfall.

First faculty forged: **Vector Symbolic reasoning**. A 32-nation
knowledge base (currency / capital / language / continent per nation)
is encoded by binding role glyphs to filler glyphs (XOR) and bundling
the pairs into holographic record glyphs (majority). Khora then
*reasons* — the answers fall out of the algebra, no lookup table holds
them:

- **Structured query** — "currency of mexico?" → unbind(CURRENCY),
  clean up against the filler codebook → **peso**. 128/128 = 100%.
- **Analogy** — "as dollar is to USA, ? is to Mexico" → **peso**.
  First pass scored 45%; observing the failure, the method evolved to
  clean up the recovered *role* against the role codebook before
  applying it. Result: 3968/3968 = **100%**.
- **Holographic capacity** — pack K records into ONE 10,000-bit glyph
  and recover their fields. First pass cliffed (62% at K=2 → 18% at
  K=32). Observing it, the method evolved to bind each record by a
  subject-key before bundling, making records individually addressable.
  Result: **K=32 → 99.2%** (127/128 facts recovered from a single
  1,250-byte vector).

Both improvements are the operator's principle made literal: fail,
study, retry, evolve. The trajectory is written to
`data/crucible/relational_evolution.json`.

Substrate regression net intact: **8/8 suites pass.**

## v0.11.0 — The Lexicon (semantic encoding)

**Author:** Morphus

Replaces random-hash token encoding with two-layer semantic glyphs:

- **Structural baseline** — every token encoded as the bundle of its
  position-permuted character trigrams, with `^` / `$` sentinels.
  Produces real overlap for related forms:
  - `cat ~ cats` = +0.37 (shared trigrams `^ca`, `cat`)
  - `install ~ instal` = +0.52 (typo tolerance)
  - `install ~ isntall` = +0.29 (transposition)
  - `aardvark ~ zephyr` = +0.07 (correctly orthogonal)
- **Cooccurrence accumulator** — every cooccurrence within a window
  contributes votes to per-bit counters; reading thresholds the
  counters against the observation count. Words that share contexts
  drift toward similar glyphs over exposure. Verified on a tiny corpus:
  - `cat ~ purr` = +0.50  (was 0.003 before fix)
  - `cat ~ feline` = +0.52
  - `dog ~ bark` = +0.46
  - `dog ~ canine` = +0.38
  - `cat ~ zephyr` (unseen) = +0.16 — correctly low

Wired into the runtime: `memorize`, `query`, `recall`, `learn`, `train`
all encode tokens through the Lexicon when available. The `train` tool
exposes the Lexicon to the same corpus it feeds the Cortex, so
semantic and predictive learning happen in parallel. Three new
carapace tools: `lex_stats`, `lex_sim`, `lex_expose`.

Live demo across separate process invocations after wiring:
```
> khora memorize cat
> khora memorize cats
> khora memorize installation
> khora query kats      -> #1 cats           sim=+0.44
> khora query instal    -> #1 installation   sim=+0.45
```

Real fuzzy retrieval on the substrate. No LLM.

Tests passing: **8/8 ctest suites, 71 assertions total** (+7 new
lexicon assertions).

## v0.10.0 — Reverie consolidation (dreams train cortex)

**Author:** Morphus

The inner loop closes. When `consolidation` is enabled, every retained
dream is fed back into the cortex via `cortex.step(dream)` — so
synthesised glyphs become training signal. Khora learns from its own
dreams.

Shipped:

- `ReverieLoom::set_consolidation(bool)` — toggles dream→cortex feedback.
- `ReverieLoom::consolidations()` — count of dreams that became training.
- `reverie_consolidate on|off` carapace tool.
- Two new reverie tests:
  - With consolidation on, `cortex.observations()` grows by exactly
    `loom.consolidations()` after `dream_n(50)`.
  - With consolidation off (default), cortex stays untouched.

This is the loop the original Khora docs envisioned but never built:
experience → memory → perturbation → synthesis → satisfaction-gated
retention → training signal. All on the substrate, all transparent,
all without an LLM.

Tests passing: **7/7 ctest suites, 64 assertions total** (+5 new
consolidation assertions).

## v0.9.0 — Background reverie (autonomy)

**Author:** Morphus

Khora now dreams continuously in a background thread while the operator
interacts with the foreground shell. First piece of true autonomy.

Shipped:

- `khora::reverie::ReverieScheduler` — owns a worker thread that calls
  `ReverieLoom::dream_once()` on a configurable period. Coordinates
  with the foreground via an externally-owned `std::shared_mutex`:
  both the scheduler (unique lock during a cycle) and operator tool
  dispatch (unique lock around `shell.dispatch`) take it, so memory
  mutations never race. Interruptible sleep via condvar so `stop()`
  wakes the thread immediately. start/stop are idempotent.
- `khora.exe` runtime starts a 100 ms-period reverie loop on REPL
  entry, joins it on exit. Every operator command runs under the
  same shared mutex so Khora's dreaming pauses for the duration of
  each user invocation only.
- Three new carapace tools: `reverie_status`, `reverie_pause`,
  `reverie_resume [period_ms]`.
- Two new reverie test groups: scheduler start/cycles/stop with
  loose Windows-friendly timing (300 ms wall-clock, expect ≥ 3
  cycles), and start/stop idempotency.

Live verification: after `memorize` six labels and re-entering REPL,
`reverie_status` immediately reports `scheduler cycles : 1, dream
lattice : 1 glyphs` — the background thread dreamed once before the
first operator command was processed. Khora is now genuinely
autonomous.

Tests passing: **7/7 ctest suites, 59 assertions total** (+3 new
reverie scheduler assertions).

## v0.8.0 — Cortex persistence + training pipeline

**Author:** Morphus

Khora now learns across sessions. The Stratiform Cortex serialises its
full state to disk and reloads on next launch; a new `train` tool feeds
a text file char-by-char or word-by-word into the cortex.

Shipped:

- `PredictiveColumn::save(prefix)` / `load(prefix)` — writes three files
  under the given prefix:
    - `<prefix>.cortex` — small binary header (magic, version,
      context_window, observations, next_assoc_id, sliding-window
      glyph buffer, recent-sims buffer).
    - `<prefix>.keys.klat` — context-key Lattice (via existing v0.2
      persistence).
    - `<prefix>.vals.klat` — next-value Lattice.
  Throws `lattice::PersistError` on magic / version / glyph-bit mismatch.
- New `train` tool in `register_cortex_tools`:
  `train <path> [per_char|per_word] [max_tokens]`. Default `per_char`,
  `max_tokens=20000`. Reports duration, tokens/sec, accuracy delta,
  resulting associations.
- `khora.exe` auto-loads both Lattice and Cortex archives at startup
  and auto-saves them on exit (both interactive and single-command).
  Lattice: `data/lattice_archive/main.klat`. Cortex:
  `data/cortex_archive/main.*`.

Verified live across five separate process invocations:
```
Round 1: train 239 chars  -> recent_acc 0.0 -> 0.547
Round 2: cortex_stats     -> [loaded cortex state: 239 obs, 238 assoc, recent_acc=0.547]
Round 3: train 239 chars  -> recent_acc 0.547 -> 0.985
Round 4: cortex_stats     -> [loaded cortex state: 478 obs, 477 assoc, recent_acc=0.985]
```

Real cross-process training. Cortex state on disk: 4 KB header + 600 KB
lattices for ~477 associations.

Tests passing: **7/7 ctest suites, 56 assertions total** (4 new cortex
roundtrip assertions).

## v0.7.0 — Carapace v0.1 + the khora runtime

**Author:** Morphus

The version that makes Khora usable. A real interactive shell that
brings up all five subsystems, persists Lattice state across runs, and
exposes 19 tools to the operator.

Shipped:

- `khora::carapace::Carapace` — registry of named Tools dispatched by
  Intent. Whitespace + double-quoted-string parser, throw-safe
  dispatch, alphabetised tool listing.
- 19 built-in tools across four registration helpers:
  - **core**: help, echo, now, pwd, ls, cat, stat, write
  - **memory**: memorize, recall, query
  - **cortex**: learn, predict, cortex_stats
  - **soma**: mood, stimulate
  - **runtime (khora_main)**: stats, dream, save
- `khora.exe` — actual user-facing runtime. Auto-loads any prior
  Lattice state from `data/lattice_archive/main.klat` at startup,
  auto-saves on exit (both interactive REPL and single-command
  invocation). Two operating modes:
  - **Interactive**: `khora.exe` opens a REPL prompt.
  - **Single-command**: `khora.exe <verb> [args...]` runs one tool
    and exits, saving state along the way.
- `carapace_test` — ten assertions covering whitespace + quoted parsing,
  empty input, unknown verb, echo, help, memory round-trip, cortex/soma
  wiring, and exception-safe dispatch.

Verified end-to-end across multiple process invocations:
```
> khora memorize alpha   ->  lattice size = 1
> khora memorize bravo   ->  [loaded 1 glyphs from ...]  size = 2
> khora query alpha 2    ->  1. alpha  sim=1.000   2. bravo  sim=-0.011
```

Tests passing: **7/7 ctest suites, 52 assertions total.**

## v0.6.0 — Reverie Loom

**Author:** Morphus

First emergent composition. The Reverie Loom takes the Morphic Lattice,
the Stratiform Cortex, and the Soma Nexus as collaborators and produces
synthetic glyphs ("dreams") that weren't directly observed.

Shipped:

- `khora::reverie::ReverieLoom` — takes references to a memory Lattice,
  a PredictiveColumn, and a SomaNexus. Each `dream_once()` picks two
  random memories, applies bit-flip perturbation, bundles them into a
  dream glyph, computes familiarity as `cosine(dream, cortex.predict())`,
  and asks the Soma Nexus to score the dream via an Affinity that
  modulates Mastery by familiarity. Dreams above the satisfaction
  threshold are retained in an internal dream Lattice. Deterministic
  under fixed seed.
- `reverie_test` — five assertions covering empty-memory no-op, mass
  retention under permissive threshold, zero retention under unattainable
  threshold, dream-vs-memory inequality (every dream is genuinely new),
  and seed-determinism.
- `reverie_demo` — builds a 200-glyph memory, trains a cortex column on
  a repeating phrase, biases drives toward Curiosity+Mastery, runs 1,000
  dream cycles. Observed: 1,000 dreams retained at mean satisfaction
  0.90, each dream ~0.49 similar to its nearest source memory
  (exactly the bundle-of-two-perturbed-memories signature).

Tests passing: **6/6 ctest suites, 42 assertions total.**

## v0.5.0 — Soma Nexus

**Author:** Morphus

Khora's drive arbitrator. Multiple competing intrinsic objectives with
homeostatic dynamics, replacing a single reward function with a
multi-pole equilibrium.

Shipped:

- `khora::soma::SomaNexus` with five drives: Curiosity, Preservation,
  Mastery, Efficiency, OperatorAffinity. Each has a current strength
  in [0, 1], a setpoint (the "personality"), and a per-second
  exponential decay rate. Default personality leans toward serving the
  operator and being moderately curious/cautious.
- `stimulate(d, delta)` / `tick(dt)` / `evaluate(affinity)` /
  `choose_best(candidates)`. Thread-safe under concurrent stimulators.
- `soma_test` — seven assertions covering default state, stimulate
  clamping, tick decay toward setpoint, weighted evaluation, action
  arbitration, drive-state-changes-choice, and concurrent-stimulator
  stress (8 threads × 1,000 ops with no out-of-range drives).
- `soma_demo` — simulated day: drive state shifts move the chosen action
  across the menu. Observed: novel input → "explore unknown" (valence
  +0.98); operator command → "serve operator" (+0.99); resource pressure
  → "consolidate memory" (+1.10); idle → back to "serve operator".

Tests passing: **5/5 ctest suites, 37 assertions total.**

## v0.4.0 — Stratiform Cortex (PredictiveColumn)

**Author:** Morphus

First subsystem that actually *learns*. Online predictive coding on top
of the Morphic Lattice.

Shipped:

- `khora::cortex::PredictiveColumn` — sliding context window of K
  recent input Glyphs, encoded as a position-aware bundle (each
  remembered input permuted by a stride of `pos * 137`). Associates
  every (context → next) pair it observes into an internal pair of
  Lattices. On each step, predicts the next Glyph from current context
  by k-NN lookup; reports prediction, actual, hamming error, similarity,
  and a novelty flag. Tracks recent-accuracy over a 64-step window.
- `cortex_test` — six assertions covering cold-start, single-shot
  pattern memorization, multi-cycle convergence on a 5-element loop,
  novelty detection on out-of-distribution context, predict() purity,
  and zero-prediction on cold column.
- `cortex_demo` — trains a column on the 44-char phrase
  "the quick brown fox jumps over the lazy dog " for 50 cycles
  (2,200 observations). Observed learning curve:
    - cycle 1: recent_acc = 0.11   (still guessing)
    - cycle 2: recent_acc = 0.73
    - cycle 3: recent_acc = 0.97
    - stable: ~0.96–1.00
  Live, demonstrable, observable convergence.

Tests passing: **4/4 ctest suites, 30 assertions total.**

## v0.3.0 — Synapse Bus

**Author:** Morphus

Shipped:

- `khora::synapse::SynapseBus` — typed async message fabric. Many-to-many
  publish/subscribe over string topics, with `Pulse` envelopes carrying
  Glyph payloads plus monotonic sequence number and timestamp. Per-subscriber
  bounded queues with drop-oldest overflow and per-handle drop counters.
  Thread-safe; shared_ptr-managed Subscriber lifetimes prevent unsubscribe
  races with blocked pollers.
- Documented ordering semantics in the header: same-thread publishes
  arrive in order; cross-thread publishes interleave; sequences are
  globally unique and assigned in publish order.
- `synapse_test` — eight test groups covering single-stream order,
  topic isolation, fan-out, drop-on-overflow, unsubscribe, timeout,
  4-publisher × 1000-pulse concurrent stress (4,000 unique sequences,
  zero loss, zero duplicates), and same-thread strict ordering.
- `synapse_demo` — producer thread fires 100 pulses to "ping",
  subscriber polls and reports per-pulse latency. Observed:
  avg 18 us / max 77 us on i7-13700K.

Tests passing: **3/3 ctest suites, 24 assertions total.**

## v0.2.0 — Lattice persistence

**Author:** Morphus

Shipped:

- `khora::lattice::save(L, path)` / `load(path)` — binary serialization
  of a Lattice to disk. Self-describing format
  (magic + version + glyph-bits + count + entries + footer magic).
  Versioned, validated on read, throws `PersistError` on corruption.
- `persistence_test` — five assertions covering empty round-trip,
  full bit-identical round-trip over 500 glyphs, query-result
  equivalence after disk round-trip, bad-magic rejection, truncated-file
  rejection, and non-ASCII label survival.
- `persistence_demo` — two-phase demo proving substrate state survives
  process death. Save phase writes 1,000 glyphs (1.27 MB) to
  `data/lattice_archive/demo.klat`; load phase reads them back and runs
  a bundled-probe query. Results are bit-identical to the in-memory
  query (Hamming distances exactly: 2463, 2471, 2501).
- All Khora runtime data now lives at `C:\Ai\Khora\data\` (per
  operator directive, D: is slow and out of scope).

Tests passing: **2/2 ctest suites, 16 assertions total.**

## v0.1.0 — Morphic Lattice substrate (in progress)

**Author:** Morphus
**Hardware:** i7-13700K, 32 GB RAM, RTX 2070S, Win 11

Shipped in this version:

- `Glyph` — 10,000-bit sparse binary hypervector with bind/bundle/permute
  algebra. Self-inverse XOR binding, majority-sum bundling,
  distance-preserving cyclic permutation. Deterministic seeded RNG via
  SplitMix64; deterministic string-to-glyph hashing via FNV-1a.
- `Lattice` — labelled associative store with k-nearest-neighbour
  content-addressable recall.
- `lattice_test` — eleven assertions covering orthogonality of random
  glyphs, density of sparse glyphs, self-inverse of bind, distance
  preservation of permute, bundle similarity to constituents,
  determinism of from_hash, lattice store/recall, and end-to-end
  constituent recovery from a bundled probe.
- `lattice_bench` — throughput numbers for popcount, hamming, bind,
  and full lattice query against a 1,000-glyph store.
- `morphus_demo` — populates a 1,000-glyph lattice, bundles three known
  glyphs into a blind probe, and prints whether the top-3 nearest
  matches are exactly those three. Pass/fail at the command line.

Not in this version (explicitly):

- AVX2-intrinsic hot loops. The current code uses portable
  `std::popcount` and word loops; the compiler auto-vectorises adequately
  for v0.1. A hand-vectorised path is planned for v0.2 once we have
  measured a bottleneck.
- The Cortex, Soma Nexus, Reverie Loom, Synapse Bus, Carapace, Vellum,
  and Sigilline. All planned, none built.
- Persistence. Lattices are in-memory only this version.
