# KHORA_BACKLOG — development ledger for the Khora Dev Loop

Khora is a sparse, event-driven, brain-inspired cognitive architecture
(LLM-free, runs in L3 cache, C++20). This backlog is the working list the
Khora Dev Loop cycles through. Status: PLANNED / IN PROGRESS / SHIPPED /
BLOCKED. One item per cycle. Foundational first, always.

## Standing directive
Prioritize CORE ARCHITECTURE and FOUNDATIONAL improvements that increase
cognitive capability, intelligence, and efficiency — NOT user-facing
features. Rank: (a) cognitive core (Lattice, Cortex, Nexus, Loom, Bus),
(b) agentic shell (Carapace), (c) quality machinery (tests, benchmarks,
regression detection), only then (d) UI (Vellum).

## Baseline state (2026-08-19, verified — supersedes the 2026-08-15 entry)
- Repo relocated C:\PC Backup\Ai\Khora -> C:\Khora.
- BUILD ENVIRONMENT REPAIRED. The VS Installer was removed from this host, so
  the `Visual Studio 17 2022` generator can no longer resolve C:\BuildTools, and
  vcvars64 leaves the Windows SDK unwired. Everything now goes through
  `tools\khora.ps1` (MSVC + hand-wired SDK + Ninja). See docs/DEVELOPMENT.md.
- Build: clean Ninja build, 96/96 targets. ctest: **11/13 pass** (not 12/14 —
  there are 13 tests).
- RED, with root causes now established (see docs/AUDIT-2026-08-19.md):
  - CrystallizeTest — 6 assertions. NOT unfinished WIP in crystallize.cpp. The
    cause is `Plexus::ppmi_` normalising the smoothed context term by N^0.75
    instead of the sum of c^0.75, deflating every score ~0.901 bits; PPMI clamps
    at zero, so `associates()` returns EMPTY and crystallize is starved of input.
  - BulwarkTest — "runaway not killed by the job". The cage works (launch,
    capture and integrity all pass, token tier 2). The canary `ping` is shadowed
    on this host by C:\Program Files\Python312\Scripts\ping.py, so it exits in
    milliseconds and the timeout never fires. self_check() therefore reports
    tier 0, which hard-gates the Maw off at runtime.
- GitHub remote wired: https://github.com/raidenslebot/Khora (main).
  .gitignore excludes /data/ (multi-GB runtime archives).

## TOP PRIORITY — foundational repair
0. PLANNED: Fix `bundle()`'s tie rule (src/lattice/glyph.cpp:194). Threshold
   (n+1)/2 ties toward SET on even n, so bundle-of-2 is a bitwise OR, not a
   majority vote — contradicting ARCHITECTURE.md §4.2. It corrupts lexicon
   context glyphs, pins reverie's familiarity near zero (100% dream retention),
   and drives cogitator's retry probe to ~99.6% density. Everything measured
   downstream is measured through this, so it comes first. Add tests for even n;
   lattice_test.cpp:82 only ever bundles 3.
1. PLANNED: Fix `Plexus::ppmi_` normalisation (src/plexus/plexus.cpp:53-68).
   Turns CrystallizeTest green and un-starves the whole symbolic layer.
2. PLANNED: Fix the Bulwark canary — replace the PATH-shadowable `ping` at
   tests/bulwark_test.cpp:19 and src/bulwark/bulwark.cpp:281 with a shell-builtin
   spin in a grandchild cmd, which still proves tree-kill. Also fix
   bulwark_probe's exit code: it returns the tier, so full containment (2) exits
   as failure and no containment (0) exits as success.
3. PLANNED: Verify or refute the crucible/whetstone encode/decode inversion
   (permute-then-bind at encode vs bind-then-permute at decode). If real, the
   self-improvement loop has been running backwards. Neither module has a test.

## Cognitive core (foundational)
Items 3-6 of the original list are SHIPPED, not planned — Soma, Cortex, Synapse
and Reverie have all been built and are under ctest. What remains is the gap
between what each one is and what ARCHITECTURE.md says it is:

4. PLANNED: Cortex — make it actually predictive. What ships is exact-context
   memorisation: one fresh key appended per observation, retrieved by linear
   Hamming scan, with no hierarchy, no downward error signal and no model update
   of any kind (§5 describes all three). Memory grows linearly with tokens to a
   hardcoded 200k cap. Largest gap in the repo between stated model and code.
   Also: predict_candidates() returns k duplicates of one context rather than k
   alternatives, which silently makes `compose`'s topic-steering a no-op.
5. PLANNED: Soma — the "homeostatic weight adaptation" of §6 does not exist;
   setpoints and decay rates are constants fixed at construction. Add outcome-
   driven adaptation, bounds-check the Drive index (Drive::_Count indexes out of
   bounds today), and persist the nexus — personality resets on every restart.
6. PLANNED: Synapse — decide its fate. It is built, tested, and used by NOTHING;
   khora_main wires 20+ subsystems by direct reference behind one process-wide
   shared_mutex instead. Either make it the decoupling it was designed to be
   (that global mutex is the throughput ceiling) or delete it.
7. PLANNED: Reverie — the satisfaction gate is a no-op at shipped defaults, so
   100% of dreams are retained forever into an uncapped lattice that Ballast does
   not know about. Blocked on item 0: it is a consequence of bundle-of-2 = OR.
8. PLANNED: Lattice persistence tiering — hot/cold split (in-RAM working
   set vs data/ archive) with atomic flush; inspired by the episodic-memory
   design of the old Raijin project (keep the idea, drop the grandeur).

## Quality machinery (foundational)
8. PLANNED: Regression detector — after each cycle, run the full ctest
   suite and diff against the previous ledger; a newly red test aborts the
   commit (adapted from Raijin's RegressionDetector, which was a good idea
   badly executed).
9. PLANNED: Fitness ledger — record build time, test pass rate, and glyph
   operation throughput per cycle into data/ledger (tiny JSONL, not GBs);
   gives the loop a measurable capability trajectory like Odysseus'
   capability-eval harness.

## Agentic shell + UI (only after the above)
10. PLANNED: Carapace intent parser — structured grammar over operator
    commands (ARCHITECTURE.md §9), discrete intent dispatch with parameter
    slots. No LLM, no prose.
11. PLANNED: Sigilline DSL — declarative glyph-algebra patterns per §11.
12. PLANNED: Vellum UI — only after the core is shipped (WPF, named-pipe
    transport reused from legacy Khora).

## Mined from the old "Raijin AI" project (https://github.com/raidenslebot/AI)
Keep the good ideas, drop the bad habits:
- GOOD: RegressionDetector, FitnessLedger, Curriculum (staged capability
  ramps), EpisodicMemory (tiered), AnomalyDetector (novelty signal feeding
  the curiosity drive), agent-roles pipeline (scout/librarian/implementer/
  verifier/redteam) — adopt as a REVIEW PIPELINE for Khora changes.
- DROP: grandiose unverified claims, no tests, entropy mysticism. Every
  Khora capability must be compiled, tested, and measurable — no
  "consciousness" prose in the changelog.

## Cycle rules (for every cycle)
- Pull + build + test BEFORE editing; a red baseline is fixed first.
- One item per cycle. Never touch data/. Never `git add -A` (stage only
  the files you changed).
- Verify: Release build + targeted ctest + full ctest once, all green.
- Commit + push to main EVERY cycle (user requirement: constant, regular
  commits and pushes).
- Update this ledger + CHANGELOG.md honestly. teach_lesson the durable
  lesson. Reply EXPANDED: ... Then khora_devloop_control action="next".
