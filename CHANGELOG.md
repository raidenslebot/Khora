# Khora Changelog

Honest log of what actually works. Nothing claimed here unless it has
been built, run, and observed.

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
