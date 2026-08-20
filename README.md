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

**v0.114.0 — 23 modules, ~17k lines of C++20, 13 test binaries under ctest,
11 of them green.** The table below was rebuilt from a module-by-module read
of the source on 2026-08-19; see [docs/AUDIT-2026-08-19.md](docs/AUDIT-2026-08-19.md)
for the evidence behind every row.

| Subsystem | What it actually is | Status |
|---|---|---|
| **Morphic Lattice** (`lattice`) | 10,000-bit binary hypervectors; bind/bundle/permute algebra; labelled store with linear-scan Hamming query; binary persistence | working, tested, benched |
| **Maelstrom** (`maelstrom`) | D3D11 DirectCompute k-NN over glyphs, with a used CPU fallback | working, untested |
| **Soma Nexus** (`soma`) | 5 scalar drives decaying toward setpoints; dot-product arbitration | working, tested |
| **Carapace** (`carapace`) | Command registry dispatching 71 operator tools | working, tested |
| **Hand** (`hand`) | Win32 process execution with pipe capture | working, untested |
| **Reservoir** (`reservoir`) | Hand-rolled LZSS codec, text distillation, byte-capped tiered store | partial, tested |
| **Lexicon** (`lexicon`) | Char-trigram glyphs + random-indexing co-occurrence | partial, tested |
| **Plexus** (`plexus`) | Weighted co-occurrence graph scored by smoothed PPMI | partial, tested |
| **Ligature** (`ligature`) | Pattern extraction of is-a / causes / has-part + forward chaining | partial, **untested** |
| **Stratiform Cortex** (`cortex`) | Next-glyph lookup by exact-context memorization — *not* predictive coding | partial, tested |
| **Reverie Loom** (`reverie`) | Background thread perturbing and consolidating glyphs | partial, tested |
| **Cogitator** (`cogitator`) | ~2.5k-line cognitive facade: cascade, transmute, abstraction tower, infer | partial, **untested** |
| **Crucible / Whetstone** | Capability measurement + self-tuning of encoder genes | partial, **untested** |
| **Curator / Lodestone / Ballast / Volition** | Self-education scheduling, resource governance, saturation gates | partial, mostly **untested** |
| **Bulwark** (`bulwark`) | Job Object + low-integrity non-admin token cage | partial, **test red** |
| **Maw** (`maw`) | Contained chaos exploration — gated off while Bulwark reports tier 0 | partial, tested |
| **Crystallize** (`crystallize`) | Votes new is-a relations out of Plexus kin | **broken**, test red |
| **Synapse Bus** (`synapse`) | Topic pub/sub with backpressure — **implemented, tested, and used by nothing** | dead |
| **Sigilline** (DSL), **Vellum** (WPF UI) | — | not started |

Two tests are red on `main`:

- `CrystallizeTest` — 6 of 9 assertions. Root cause is upstream in `Plexus::ppmi_`,
  not in `crystallize`.
- `BulwarkTest` — the runaway-kill assertion. The cage itself works (launch,
  capture and integrity all pass); the canary command `ping` is shadowed on this
  host by a Python `ping.py` on PATH, so the "runaway" exits instantly and the
  timeout never fires. This caps `self_check()` at tier 0, which gates off the Maw.

Where a row says **untested** it means no test binary exists for it — not that it
fails. Roughly the largest and most-advertised modules are the untested ones.

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
