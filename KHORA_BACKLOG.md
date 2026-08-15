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

## Baseline state (2026-08-15, after relocation to C:\Khora)
- Repo relocated C:\PC Backup\Ai\Khora -> C:\Khora; CMake reconfigured.
- Build: Release build OK. ctest: 12/14 pass.
- RED: CrystallizeTest (6 assertion failures — the consensus/candidate
  commit path is unfinished WIP from before the move), BulwarkTest
  ("contained output not captured" — execute_contained launches the
  process but loses its output).
- GitHub remote wired: https://github.com/raidenslebot/Khora (main).
  .gitignore now excludes /data/ (multi-GB runtime archives).

## TOP PRIORITY — baseline repair
1. PLANNED: Fix BulwarkTest — execute_contained captures no output
   (src/bulwark/bulwark.cpp). The Job-Object cage must still capture
   stdout/stderr while keeping the timeout tree-kill intact.
2. PLANNED: Finish Crystallize consensus commit path
   (src/crystallize/crystallize.cpp) so all 9 crystallize assertions pass:
   association consensus -> candidate -> commit writes one relation with
   witnesses. Use ARCHITECTURE.md §4 and the lattice module as reference.

## Cognitive core (foundational)
3. PLANNED: Soma Nexus v0.1 — drive arbitration per ARCHITECTURE.md §6:
   curiosity/preservation/mastery/efficiency/operator-affinity scalar
   drives + homeostatic weight adaptation. Tests: drive weighing picks the
   dominant drive; weights adapt to outcomes (bounded, never negative).
4. PLANNED: Stratiform Cortex v0.1 — predictive column per §5: glyph
   stream in, next-glyph model, upward prediction, downward error signal,
   bitwise model update (no dense matrices). Tests: prediction improves on
   a repeating sequence; error signal shrinks as the model converges.
5. PLANNED: Synapse Bus — typed async message fabric per §8: lock-free
   ring buffers per subsystem pair + journaled replay. Tests: pub/sub
   delivery, backpressure bound, replay restores state after restart.
6. PLANNED: Reverie Loom v0.1 — offline simulation per §7: sample recent
   lattice experience, perturb glyphs, run through cortex models, promote
   drive-satisfying outcomes. Tests: perturbation is bounded; promoted
   associations are reachable via lattice query.
7. PLANNED: Lattice persistence tiering — hot/cold split (in-RAM working
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
