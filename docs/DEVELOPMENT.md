# Khora — development environment

Everything here is verified on this host, not aspirational. If a command below
does not work, the environment has drifted; fix it and update this file.

## TL;DR

```powershell
.\tools\khora.ps1 all      # configure + build + test
```

## Why there is a task runner at all

The toolchain works, but nothing about it is discoverable:

| Fact | Consequence |
|---|---|
| `cmake`, `ctest`, `ninja`, `cl` are **not on PATH** | Bare `cmake` in any shell fails |
| The **Visual Studio Installer was removed** (`vswhere.exe` is gone), while the Build Tools payload survives at `C:\BuildTools` | CMake's `Visual Studio 17 2022` generator errors with *"instance is not known to the Visual Studio Installer"* — the generator in `README.md` cannot work |
| `vcvars64.bat` runs, but leaves `WindowsSDKVersion` **empty** and omits ucrt/um/shared from `INCLUDE`/`LIB` — the SDK at `C:\Program Files (x86)\Windows Kits\10` is on disk but unregistered | `cl.exe` compiles, then `link.exe` fails: no CRT import libraries, and `rc.exe` / `mt.exe` not found |

`tools\khora.ps1` resolves all three: it activates MSVC from `vcvars64.bat`,
wires the newest complete Windows SDK by hand, puts CMake + Ninja on PATH, and
uses the **Ninja** generator, which needs no installer metadata.

## Tasks

| Command | Does |
|---|---|
| `.\tools\khora.ps1 env` | Print the resolved toolchain versions. Start here when something breaks. |
| `.\tools\khora.ps1 configure` | CMake configure (Ninja, `compile_commands.json` on) |
| `.\tools\khora.ps1 build` | Build; configures first if needed |
| `.\tools\khora.ps1 test` | `ctest --output-on-failure`; **exits with ctest's code** so a loop can gate on it |
| `.\tools\khora.ps1 bench` | Run every `*_bench.exe` |
| `.\tools\khora.ps1 tidy [paths]` | clang-tidy over `src`+`include`, or the given paths |
| `.\tools\khora.ps1 format [paths]` | clang-format in place (needs a `.clang-format`; none yet) |
| `.\tools\khora.ps1 run <exe> [args]` | Run a binary out of `build\bin` |
| `.\tools\khora.ps1 clean` | Delete the build tree |
| `.\tools\khora.ps1 all` | build + test |

Add `-Config Debug` to any of them. Ninja is single-config, so Debug builds into
`build-debug\` and Release into `build\`; they do not clobber each other.

## Toolchain, as resolved on this host

| Tool | Version | Location |
|---|---|---|
| MSVC | 19.44.35228 (14.44.35207) | `C:\BuildTools\VC\Tools\MSVC\14.44.35207` |
| Windows SDK | 10.0.26100.0 | `C:\Program Files (x86)\Windows Kits\10` |
| CMake | 3.31.6-msvc6 | `C:\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin` |
| Ninja | 1.12.1 | `C:\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja` |
| clang-tidy / clang-format | LLVM 19.1.5 | `C:\BuildTools\VC\Tools\Llvm\x64\bin` |

If any of these move, edit the candidate lists at the top of `tools\khora.ps1`.
There is no `vswhere` on this host to discover them automatically.

## Static analysis

`.clang-tidy` at the repo root selects **bug-finding checks only** — `bugprone-*`,
`performance-*`, `portability-*`, `concurrency-*`. Readability/modernize checks
are off on purpose: on a 17k-line existing codebase they bury the real findings.

clang-tidy needs the MSVC environment (in cl driver-mode it reads system headers
from `INCLUDE`), which is why it runs through the task runner rather than directly.

## Editor integration

`configure` writes `build\compile_commands.json`. Point clangd or VS Code's
C/C++ extension at it for accurate completion and diagnostics.

## Baseline (2026-08-19, verified)

- Clean Ninja build: **96/96 targets**, no errors.
- `ctest`: **11 / 13 pass**. Two are red and were red before the toolchain change
  — identical failures under both the old VS build and the new Ninja build:
  - `CrystallizeTest` — 6 assertion failures in the consensus → candidate → commit path.
  - `BulwarkTest` — `runaway not killed by the job`.
- `lattice_bench`: popcount ~21 Mops/s, hamming ~22 Mops/s, bind ~31-40 Mops/s,
  lattice query ~14k qps over 1000 glyphs.

Note: `KHORA_BACKLOG.md` records this as "12/14 pass" and describes the Bulwark
failure as lost output. Both are stale — there are 13 tests, and the failing
Bulwark assertion is the timeout tree-kill.
