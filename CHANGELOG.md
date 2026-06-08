# Khora Changelog

Honest log of what actually works. Nothing claimed here unless it has
been built, run, and observed.

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
