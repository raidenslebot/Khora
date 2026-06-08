# Khora

A sparse, event-driven, brain-inspired cognitive architecture.
LLM-free by design. Lives in the L3 cache of a modern CPU.

Built by **Morphus** (an AI engineer-persona) for a single human operator,
on a single PC, with no cloud dependency.

## What this is — and isn't

Khora is **not** a chatbot, **not** a wrapper around a language model,
**not** a deep learning system. It is a substrate for cognition built
from sparse binary hypervectors, predictive coding hierarchies, and
competing intrinsic drives. It thinks in patterns, not tokens.

That means: Khora will never generate fluent prose about arbitrary
subjects the way an LLM does. It also means: every capability it has
is mechanistically transparent, runs on commodity hardware, and is
yours alone — no API, no telemetry, no monthly fee.

## Current state (verified, not aspirational)

| Subsystem | Status |
|---|---|
| Morphic Lattice (sparse HD substrate) | **working** — tested, benchmarked |
| Stratiform Cortex (predictive hierarchy) | planned |
| Soma Nexus (drive arbitration) | planned |
| Reverie Loom (offline simulation) | planned |
| Synapse Bus (async messaging fabric) | planned |
| Carapace (agentic outer shell + tools) | planned |
| Vellum (dark, transparent WPF UI) | planned |

When a row says "working" it means: the code compiles cleanly, tests
pass, and the demo runs. When it says "planned" it has not yet been
written and nothing in the codebase pretends otherwise.

## Build

```
cmake -B build -S . -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\bin\Release\morphus_demo.exe
.\build\bin\Release\lattice_bench.exe
```

Requires Visual Studio 2022 with the C++ workload, CMake 3.20+,
and a CPU with AVX2 (any Intel Haswell+ / AMD Excavator+).

## Architecture

See [ARCHITECTURE.md](ARCHITECTURE.md).

## Honest changelog

See [CHANGELOG.md](CHANGELOG.md).

## Heritage

The old Khora codebase — broken, ambitious, instructive — is preserved
at `C:\Ai\Khora_backup`. Several of its design ideas
(sparse manifest, predictive cortex, dream subsystem, drive system)
survive into this rewrite. Its execution did not.
