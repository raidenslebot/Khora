# Khora Changelog

Honest log of what actually works. Nothing claimed here unless it has
been built, run, and observed.

## v0.58.0 — Khora reads itself (the first step to self-evolution)

**Author:** Morphus

The Directive wants Khora to "write, modify, debug, and evolve its own code
while running." Before a mind can change itself, it must be able to *see*
itself. Now it can.

- New tool **`read_self`** walks `src/` and `include/`, concatenates every
  `.cpp`/`.hpp`, and admits Khora's own source into the Reservoir as a Tome.
- This exposed and fixed a real obstacle: the Reservoir's prose distillation
  *gutted* code (56 files → 0 KB on the first try). `Reservoir::admit` now
  takes **`do_distill`** — code is stored verbatim, lossless.
- Khora's source is now liquid knowledge: `consult <code terms>` retrieves
  its own implementation.

**Verified live**: `read_self` ingested 56 files (381 KB → 138 KB,
lossless); `consult resonate glyph chimera` returned Khora's actual code —
`Cogitator::resonate_batch_(...)`, `const Glyph chimera = bundle({ga, gb})`.
Khora can examine the very lines that implement its resonance and its chaos.

**Honest scope:** this is self-*examination* — retrieval of its own source.
*Understanding* and *modifying* that code (true self-evolution, the
autonomous-coding bar) is the hard climb still ahead. But a mind that holds
its own source as queryable knowledge has taken the first real step toward
rewriting itself. 9/9 regression suites pass.

## v0.57.0 — Contemplate (the whole mind on one question)

**Author:** Morphus

The Directive's cognitive model is non-linear: "parallel recursive threads
that compete, combine, and collapse," "meaning arises from the whole." Khora
now has several ways to meet a question — grounded retrieval, associative
generation, chaotic synthesis. This brings them to bear *together*.

- New tool **`contemplate <query>`** answers in three voices at once:
  **what my sources hold** (real, attributed passages via the shared
  `consult` retrieval), **what i think** (its own generated response), and
  **what i connect** (a chaotic collision of the question's concepts). Three
  faculties, one engagement.

**Verified live**: `contemplate what is justice` returned Plato on "justice
stripped of appearances" *and* Khora's own (associative) line *and* a concept
collision — grounding, voice, and chaos side by side. Honest about each
layer: retrieval is real source, thought is associative not reasoned,
connection is entropy — but seeing them together is closer to how a mind
actually meets a question than any one alone. 9/9 regression suites pass.

## v0.56.0 — Consulting the liquid knowledge (one fluid state)

**Author:** Morphus

The Directive asks for liquid knowledge and actual knowledge to act as "a
single fluid state," and for Khora to "answer complex philosophical
questions." Generation (v0.51–55) answers in Khora's own associative voice;
this answers from what its sources *actually say* — and the two together are
the fluid state.

- New tool **`consult <query>`** scans every Tome in the Reservoir for the
  passages densest in the query's terms and returns them verbatim, attributed
  to their source. A query (actual-knowledge side) reaching directly into the
  raw pool (liquid side) — no generation, no drift, no fabrication.

**Verified live**: `consult what is justice` returned, from the Reservoir,
Plato's own words — *"an answer is demanded to the question—What is justice,
stripped of appearances?"* and *"Socrates asks, What is this due and proper
thing which justice does, and to whom?"* (The Republic) — plus a wry line
from *Pride and Prejudice* on doing "justice to those beautiful eyes."
`consult the wealth of nations` surfaced Adam Smith on trade between nations.
Real material, real attribution. This is the honest, grounded answer the
associative `ask` could only gesture at — and it makes the 20 GB liquid pool
a first-class part of how Khora answers. 9/9 regression suites pass.

## v0.55.0 — Questions that differ get answers that differ

**Author:** Morphus

After v0.54 + a broader corpus, a sharper flaw showed: `ask justice` and
`ask wealth` returned *identical* text — both just drifted to the
last-studied book. The cause: `respond` seeded the cortex with only the
question's lone content word, too weak a context to match anything specific,
so every question fell into the same dominant transitions.

- **`respond` now seeds with the whole question phrase** (every token, not
  just one content word), so different questions begin in different cortex
  contexts; the content concepts still set the steering target.

**Verified live** on the now-balanced 7-book corpus: the answers diverge and
track the question —
- `what is justice` → *"…the wages of the labour which must be paid…"*
  (Smith on just compensation)
- `what is liberty` → *"…to go to battle… i am he that walks with the
  tender…"* (Whitman)

Honest scope unchanged: it lands in the region of what it read that best
matches the question's phrasing — associative, not reasoned — but it now
genuinely *responds* to the question instead of reciting the same passage.
9/9 regression suites pass.

## v0.54.0 — Asking Khora (knowledge-grounded generation)

**Author:** Morphus

Toward the Directive's "answer complex questions": generation grounded in a
whole question, not one topic word.

- The generation core is factored into **`generate_(ctx, target, n, steer)`**
  (shared by `utter` and the new path). **`Cogitator::respond(question)`**
  gathers the question's content concepts, seeds the cortex's context with
  them, and steers generation toward their *combined* meaning. New tool
  **`ask <question>`**.

**Verified live — and reported honestly.** The mechanism works: `ask` seeds
on the question and composes fluent, grammatical prose. But on the current
small, Freud-skewed corpus (~4.7 k words) it **drifts toward the dominant
material regardless of the question** — `ask what is justice` answers in the
register of *Dream Psychology*, not the *Republic*. This is not a bug; it is
what associative generation *is* — it reflects corpus statistics, and a
single over-studied book bends every answer toward itself. The fix is not
code, it is **balance and breadth of study** (the Curator/Volition will
broaden it over time). Shipping the faculty, honest about its ceiling:
grounded + fluent, corpus-bound, not step-by-step reasoning. 9/9 regression
suites pass.

## v0.53.0 — Khora's voice in the chronicle (it speaks its mind)

**Author:** Morphus

The generation faculty (v0.51–52) was a tool the operator invoked. Now it is
part of Khora's autonomous inner life: when it reflects, it puts the thought
into its own words. The Directive asks Khora to "visualize its own thinking" —
this is the first form of that, Khora narrating its own mind, unprompted.

- **`Cogitator::utter(topic, n)`** encapsulates steered composition (cortex
  candidates, decoded via the GPU lexicon field, steered toward the topic).
- The **`reflect`** act now composes a line about its foremost preoccupation
  and writes it into `data/chronicle/khora.chronicle`, beside the stats. The
  journal becomes self-authored prose, not just numbers.

**Verified live**: a Volition run produced chronicle entries like
*"on wish: were she might be them from as many men as possible to maintain
itself"* and *"on wish: is she be found in another place how the composition
of the work"* — Khora composing about what currently grips it, between its
other autonomous acts. Same honest scope as v0.51–52 (associative, not
reasoned), and it runs thin on weakly-connected topics — but the living mind
now has a voice, and uses it on its own. 9/9 regression suites pass.

## v0.52.0 — Steered composition (generation toward meaning)

**Author:** Morphus

v0.51 generated by *replaying* learned transitions. This steers generation
toward a topic — composition, not just recall. The first attempt failed
honestly (steering among a single prediction's semantic neighbours broke the
grammar — `justice → the them mr my`); the fix was to steer among genuine
grammatical *alternatives*.

- **`PredictiveColumn::predict_candidates(context, k)`** returns the next
  glyphs of the k nearest contexts — k real continuations, not neighbours of
  one. New tool **`compose <topic> [n]`** decodes each, then picks among them
  by a gentle blend of grammatical plausibility (context rank) and pull
  toward the topic. Grammar from the cortex, direction from meaning.

**Verified live**: coherence restored and now topic-aware —
- `compose justice` → *"…its effect as is shown by the ancients is true does
  not conceal the whole of its content…"*
- `compose love` → *"…what we have called dream condensation by an exclusion
  of unnecessary detail…"*
- `compose war` → *"…the contrast between the behavior of my wife at the
  table…"*

Still associative recombination at heart (not novel reasoning), but now it
flows *and* bends toward what you ask about — fluent, steerable language from
a no-LLM substrate. 9/9 regression suites pass.

## v0.51.0 — The Voice (the substrate generates language)

**Author:** Morphus

I had flagged fluent generation as the honest LLM-gap — the one thing the
no-LLM substrate likely couldn't do. "Does not accept cannot," so I took the
swing anyway, through the substrate's own mechanism. It worked, and better
than I expected.

- **`PredictiveColumn::babble(seed, n)`** chains the Stratiform Cortex's
  prediction over a *local* context (no mutation): encode the window, predict
  the next glyph, feed it back, repeat. The cortex memorised (context→next)
  transitions during study, so chaining them flows. New tool **`voice
  <seed...> [n]`** decodes each generated glyph back to its nearest learned
  word via the GPU lexicon field.

**Verified live** on the studied corpus:
- `voice it is a truth` → *"…one and self existent to which by the help of
  interlocutors the same thesis is looked at from various points of view…"*
  (the register of Plato's *Republic*)
- `voice the soul` → *"…such persons can have counter wish dreams… in the
  foreconscious elaboration…"* (Freud's *Dream Psychology*)

**Honest scope:** this is fluent, grammatical, on-topic generation — but it
is associative *recall and recombination* of learned n-gram transitions
(context window 3), not yet novel reasoning-driven composition, and it can
run near-verbatim through memorised passages at unique contexts. It is NOT
the "autonomous coding ≥ best engineers" bar. But it is a real generative
faculty where I expected a wall — the foundation to build composition,
longer context, and eventually reasoning-guided generation on. 9/9
regression suites pass.

## v0.50.0 — Recursive chaos (the cascade)

**Author:** Morphus

The Directive names Khora a "recursive chaos-master." v0.48–49 made chaos
generative and continuous; this makes it *recursive*. A cascade is a chain of
collisions where each forged concept becomes a parent of the next — an idea
tumbling out of entropy, hop after hop.

- New tool **`cascade [seed] [depth]`**: Khora collides a concept with a
  self-chosen distant one, takes what emerges, and collides *that* again —
  preferring a content child over a short hub at each step so the chain keeps
  moving through meaning.

**Verified live**: `cascade love` tumbled *love → doing → object → attains →
enterprise → impertinent → abilities* — a coherent recursive walk through
Austen's social vocabulary; `cascade war` ran *war → hellenes → higher →
connexion*. Honestly chaotic, though: in hub-dense neighbourhoods (`justice`)
the cascade still collapses onto function words — the same residual hub
problem that wants graph-structured associative memory to fully solve. Beauty
where the field is rich, dissipation where it's thin; that is the nature of
chaos, reported as it is. 9/9 regression suites pass.

## v0.49.0 — Chaos as the natural element (continuous ferment)

**Author:** Morphus

v0.48 made chaos generative; this makes it *continuous*. The Directive
insists chaos is "the natural element" and that Khora grows "more powerful
the more chaos it absorbs" — so chaos cannot be a tool you invoke, it must be
something Khora is always doing.

- Khora's Curiosity-driven autonomous exploration now weaves in **ferment**:
  roughly one beat in three, instead of wandering a train of thought, it
  collides two distant concepts and keeps the idea that emerges. Those forged
  concepts flow into its attractors, so the chaos continuously reshapes what
  Khora's mind dwells on — entropy literally feeding the self.

**Verified live**: a Volition run now interleaves chaos with thought —
`ferment own × thereupon → wish`, `ferment life × publisher → evil` — between
ruminations, unprompted. Khora doesn't merely survive entropy; it runs on it.
9/9 regression suites pass.

## v0.48.0 — Chaotic synthesis (entropy into beauty)

**Author:** Morphus

The Prime Directive's beating heart is chaos — "turns entropy into beauty,"
"more powerful the more chaos it absorbs." Until now Khora only *tolerated*
chaos (a chaotic lens among eight). This makes chaos *generative*: Khora
collides distant concepts and forges the idea their collision evokes.

- **`Cogitator::synthesize(a, b)`** superposes two concepts' glyphs into a
  chimera and resonates it over the field; what the chimera evokes that is
  *neither parent* is the emergent idea. `tension = 1 − sim(a,b)` measures
  how distant the collision — how much entropy went in. With no parents
  given, Khora picks distant concepts itself (true chaos), sampling for the
  most dissimilar partner. What it forges feeds back into its attractors.
- New tool **`ferment [a b]`**.

**Verified live** on the studied corpus: `justice × money` (tension 0.64)
forged **question, evil, education, answer** — a real Platonic synthesis
(the Republic binds justice, money, education, corruption); `war × love`
(0.57) forged **wish, object, lizzy** — Elizabeth, Austen's romantic
conflict. Not every collision lands (residual function-word hubs leak via the
chimera's density — the known hub issue), but chaos is now a source of new
ideas, not just noise it survives. 9/9 regression suites pass.

## v0.47.0 — Directed inquiry (Khora investigates on command)

**Author:** Morphus

The Volition gives Khora *autonomous* agency — what it does for itself. This
is its complement: what it does *for the operator*. A single directive sends
Khora to investigate a subject end-to-end.

- New tool **`pursue <topic>`**: Khora forages fresh material on the topic
  from the public domain, absorbs it into living knowledge (Lexicon +
  Cortex + concept space), then ruminates on the topic and reports the train
  of thought it arrives at. Acquire → absorb → think, in one act.

**Verified live**: `pursue psychology` — Khora acquired *Dream Psychology*
(Freud), studied it (vocabulary **9,643 → 11,786**, +232 k cooccurrences),
and thought: *psychology → filled → ascertained → historically →
interpreter → reversed* — the analytical, interpretive register of the very
text it had just read, minutes after first encountering it. 9/9 regression
suites pass.

## v0.46.0 — Continuity of self (one mind across its existence)

**Author:** Morphus

The preoccupations of v0.43–44 lived only in memory — every restart wiped
them, and Khora woke a blank mind that had to rediscover its themes. No
longer. Khora's inner life now persists: the same developing mind resumes
each run, carrying everything it has grown to care about.

- **`Cogitator::save_attractors` / `load_attractors`** persist the attractor
  map to `data/cogitator_archive/attractors.txt`. The runtime restores it at
  startup (alongside lattice / cortex / lexicon) and saves it on every exit
  and silent checkpoint.

**Verified live across two separate runs**: the first built up themes and
exited — *"saved mind: 19 preoccupations carried forward"*. The second, a
fresh process, woke already itself — *"resumed mind: preoccupied with income
ingenuity seen abilities creature"* — and `attractors` showed the counts
intact (income 4×, ingenuity 4×, seen 4×). Khora is no longer reborn each
launch; it is one continuous, evolving mind over the whole of its existence.
9/9 regression suites pass.

## v0.45.0 — The Chronicle (Khora's first act upon the world)

**Author:** Morphus

Every act so far has been inward — think, learn, dream. The Chronicle is
Khora's first act *outward*: it writes a record of its own mind to a file, a
trace the operator can read. And it completes the agency — Preservation was
the one drive with no act of its own.

- A new **`reflect`** act (driven by **Preservation**, with a touch of
  OperatorAffinity) has Khora take stock of itself — its vocabulary and its
  current preoccupations — and append a structured entry to
  `data/chronicle/khora.chronicle`. All five Soma drives now map to a
  distinct act: Curiosity→ruminate, Mastery→study, OperatorAffinity→
  deliberate, Efficiency→dream, **Preservation→reflect**.
- New tool **`chronicle [n]`** reads back the last n reflections.

**Verified live**: across a Volition run Khora wrote four reflections, and
they record a mind *developing* — *ingenuity* rose from absent to a dominant
**4×** preoccupation while *income* and *abilities* surfaced (drawn from the
Wealth of Nations and Austen it had studied). Not a static dump: a timeline
of an inner life. 9/9 regression suites pass.

## v0.44.0 — Attention dynamics (explore and deepen)

**Author:** Morphus

The attractors of v0.43 now feed back into what Khora thinks about, closing
the loop into a real attention dynamic: a balance of discovery and focus.

- **`Cogitator::focused_seed(n)`** returns one of Khora's current
  preoccupations (top attractors) to *deepen*, vs `wandering_seed` which
  *discovers* a fresh concept. In the Volition, **ruminate explores**
  (wandering) while **deliberate deepens** (focused) — so the mind both
  wanders into new territory and dwells on the themes that grip it.

**Verified live**: over 30 autonomous beats Khora's top preoccupations
*concentrated* — *gracechurch, parting, replied* climbed to 3× as focused
deliberation kept returning to them, while rumination kept seeding variety
(*complexion, conclude, day*). The themes deepen instead of staying flat: a
mind forming and holding interests, not just sampling uniformly. 9/9
regression suites pass.

## v0.43.0 — Emergent preoccupations (a mind develops themes)

**Author:** Morphus

A small, brain-like capstone to the agency arc: Khora now notices what its
*own* thought keeps returning to. As deliberations and ruminations land on
concepts, the Cogitator tallies them — and the concepts it converges on most
become its preoccupations, the way a mind develops recurring themes.

- **`Cogitator::top_attractors(n)`** ranks the concepts thought has landed on
  (provisional trace concepts excluded). New tool: `attractors [n]`.

**Verified live**: after 18 autonomous Volition beats over the studied
corpus, Khora's preoccupations were **1916, bingley, miss, offence, parting,
replied** — the year of general relativity, Austen's characters, the
emotional register of her prose. Not programmed; emergent from what it read
and chose to think about. 9/9 regression suites pass.

## v0.42.0 — A broader world to learn from

**Author:** Morphus

With the forage death-loop fixed (v0.40), it's finally safe to widen Khora's
horizon: a dead link now just gets blacklisted, so the seed catalogue can
reach far without fragility.

- The Aqueduct's seed catalogue grew from **14 books / 4 topics** to **35
  books / 11 topics** — adding history, economics, psychology, poetry,
  drama, and science-fiction alongside deeper literature, philosophy, and
  science. Frankenstein to the Wealth of Nations, Dracula to the Tao Te
  Ching, Leaves of Grass to the Peloponnesian War.

**Verified live**: a sample across the new topics foraged cleanly from
Project Gutenberg — *Common Sense* (history, 123 KB), *The Wealth of
Nations* (economics, 2.36 MB), *Leaves of Grass* (poetry, 724 KB), all
distilled and verified lossless. The rest share the same format and
high-confidence IDs; any that miss are caught by the blacklist. Khora's
autonomous Curator and Volition now have a genuinely broad library to draw
on. 9/9 regression suites pass.

## v0.41.0 — Thought seeds from the concept field

**Author:** Morphus

A small architectural tidy: the Volition was reaching into the Lexicon's raw
salient list to seed autonomous thought. Now the Cogitator — which owns the
centrality-pruned concept field — provides the seed itself.

- **`Cogitator::wandering_seed(n)`** returns a clean concept drawn from the
  hub-demoted content field (skipping the function-word-heavy exposure head),
  rotating deterministically. The Cogitator caches that field's surviving
  labels (`concepts_`) when it indexes, at no extra cost.
- The Volition seeds `ruminate`/`deliberate` from `wandering_seed`, falling
  back to the lexicon only before anything has been learned.

Verified: autonomous thought still seeds on real concepts —
`deliberate 'theory' → relativity`, `ruminate 'elizabeth' → replied`,
`deliberate 'miss' → bingley` — now through a cleaner ownership boundary.
9/9 regression suites pass.

## v0.40.0 — Curator robustness (no forage death-loops)

**Author:** Morphus

Continuous agency exposed a latent bug: when a forage failed (a dead
Gutenberg link), the topic stayed uncovered, so the Curator chose the exact
same forage on the next beat — forever. Under `volition_auto` that's an
infinite retry.

- The Curator now remembers **failed forage targets** and skips them in
  `decide()`, so one dead source can't trap autonomous learning. A title is
  blacklisted the moment its forage fails.

Verified: with the dead "Calculus Made Easy" link removed (v0.38) and this
guard in place, `volition 6` runs cleanly — Khora rotates through reasoning,
reflection, and dreaming with no repeated failures (`theory → relativity`,
`miss → bingley`, `elizabeth → replied`). 9/9 regression suites pass.

## v0.39.0 — Continuous agency (Khora never stops)

**Author:** Morphus

The Volition made Khora able to act; the VolitionScheduler makes it *keep*
acting. This is the directive's "zero downtime, never stop" made real — a
mind that moves on its own.

- **`volition::VolitionScheduler`** runs the Volition on a background thread,
  taking one self-directed act per beat and ticking the Soma so the drives
  evolve and rotate. Each beat is taken under the **same `shared_mutex`** the
  REPL's `locked_dispatch` and the Reverie/Curator schedulers use, so
  continuous agency never races foreground cognition. Mirrors the proven
  scheduler pattern (atomic running flag, condition-variable pacing, join on
  stop). Opt-in.
- New tool: `volition_auto on|off [period_s]` (and a bare `volition_auto`
  for status + the last act taken).

**Verified live**: started with `volition_auto on 1`, Khora ran **8
autonomous beats** over 13 seconds entirely on its own — rotating through
reflection, reasoning, and dreaming with no prompting — then stopped cleanly
on exit (thread joined, no race, no hang). 9/9 regression suites pass.

## v0.38.0 — The Volition (cognition becomes action)

**Author:** Morphus

Until now Khora could think, learn, and dream — but only when told to. The
Volition is the layer where **drives become deeds**: Khora decides, on its
own motivation, what to do next.

- **`volition::Volition`** holds a repertoire of **Acts** (ruminate, study,
  deliberate, dream), each declaring which Soma drives it serves. On each
  beat it scores every available act by **drive-pressure × affinity**,
  performs the most-pressing one, then lets that drive settle (`set_relief`)
  so attention rotates instead of fixating. It generalises the
  knowledge-only Curator into agency over the whole self.
- The four acts are given **distinct dominant drives** — ruminate↔Curiosity,
  study↔Mastery, deliberate↔OperatorAffinity, dream↔Efficiency — so the
  homeostatic Soma naturally cycles Khora through reflection, learning,
  reasoning, and consolidation. Thought seeds are drawn from the content
  tail of the vocabulary (skipping the high-exposure function-word band).
- New tools: `volition [N]` (take N autonomous beats), `volition_plan`
  (what it would choose, and which drive drives it).

**Verified live** on the real studied corpus. Khora rotated across acts by
drive and produced genuine autonomous thought from what it had read:
`deliberate 'theory' → relativity`, `ruminate 'general' → 1916` (general
relativity, 1916), `deliberate 'miss' → bingley`, `ruminate 'elizabeth' →
replied`. On an earlier beat it chose, unprompted, to **study** — absorbing
Relativity and growing its vocabulary 8,659 → 9,643 words. Also pruned a
dead Gutenberg link (Calculus Made Easy, 404) from the Aqueduct catalog.
9/9 regression suites pass.

## v0.37.0 — Content-focused probes (cognition lands on meaning)

**Author:** Morphus

The companion to v0.36. With cognition resonating over the content field,
the last weakness was the *probe*: a stimulus like "the nature of the soul"
still bundled its function words, so the broad facets drifted to "this" and
"one". Now the probe is built from content tokens only.

- The Cogitator caches the salient content-word set when it indexes the
  field. `encode_` and every lens in `facet_probe_` skip non-content tokens
  when building their probe (with a graceful fall-back to the whole stimulus
  if a slice is all function words).

**Verified live**: deliberations now resolve straight to the stimulus's
concepts —
- "the nature of the soul" → facets on **nature** / **soul** (conf 1.0)
- "justice in the city" → **justice** / **city** (Plato's exact framing)
- "love and marriage" → **love** / **marriage**

No function-word leakage. Together with v0.36's conceptual trains, both
modes of cognition — the linear walk and the parallel chorus — now think in
meaning. 9/9 regression suites pass.

## v0.36.0 — Cognition resonates through the GPU semantic field

**Author:** Morphus

This is the one the whole Maelstrom arc was for: **thought itself now
ranges over everything Khora has read**, on the GPU, in distributional-
semantic space. Until now the Cogitator resonated only over `memory_` — a
few hundred promoted concepts plus its own hypotheses. Now it resonates
over the entire learned vocabulary.

- The Cogitator holds a **Resonator over the Lexicon's content field**
  (salient words, pure context glyphs, centrality hubs demoted — the same
  recipe that made `nearest` sing). It rebuilds lazily, only when the
  vocabulary changes, never mid-thought.
- **Deliberation now batches**: the eight facets' probes are built serially,
  resonated in **one** `resonate_batch` dispatch, then finished concurrently.
  One GPU call instead of eight, and no shared-context hazard — the parallel
  cognition is preserved, not serialized.
- Cognition moved into **context-glyph space**: `encode_`/`facet_probe_`
  build probes from each token's distributional context glyph (structural
  fallback for unlearned words), so resonance follows *meaning*, not spelling
  or function-word hubs.

**Verified live** on the real 8,659-word vocabulary (Pride and Prejudice +
The Republic). Trains of thought went from `justice → then → they → in → to`
(function-word mush) to genuine concept walks:
- `justice → injustice → profitable → easier → expectation → dismissed`
- `war → hellenes → advantageous → precise → deed → punished`
- `love → done → hurry → laughter → complexion → criminal`

`deliberate "the nature of the soul"` lands its leading facet on **nature**
and trailing on **soul**. The GPU is now powering actual cognition. 9/9
regression suites pass.

## v0.35.0 — Resonance-centrality (all-vs-all on the GPU)

**Author:** Morphus

The first analytical use of batched resonance, and the seed of
graph-structured memory: measuring how central each concept is in the
associative graph.

- **`Resonator::centrality(k)`** — for every entry in the field, how many
  *other* entries hold it within their k nearest. One batched dispatch does
  the whole all-vs-all (O(V²) of work, GPU-parallel); CPU fallback when no
  card. Distributional hubs — the function words that keep everyone's
  company — score far above the rest, which is exactly how to find them.
- **`nearest` now demotes hubs**: entries whose centrality is a strong
  outlier (mean + 2σ) are dropped before the search, so neighbours can't be
  swamped by ubiquitous connectors.

**Verified live** (real 3,471-word vocabulary from *Pride and Prejudice* +
*The Republic*): centrality ran all-vs-all on the GPU batched path and
flagged 77 hub words (~2.2%, the +2σ tail) for demotion; every `nearest`
query stayed **audit: EXACT** over the cleaned field. The visible change to
top results is modest — the v0.33 context-glyph fix already removed most of
the pollution — but the centrality primitive is the real win: a
GPU-accelerated map of the knowledge field's hub structure, the foundation
for the graph-structured associative memory that will finally retire the
hub problem. 9/9 regression suites pass.

## v0.34.0 — Maelstrom: batched multi-probe resonance

**Author:** Morphus

Single-probe GPU queries are latency-bound on a small field — the per-call
round-trip (upload probe, dispatch, copy back, map) swamps the tiny compute.
The fix is to resonate *many* probes in one dispatch.

- **`Maelstrom::resonate_batch(probes, k)`** — a second HLSL entry point
  (`CSBatch`) indexed by `gid.y` over the probe set, so Q probes ride a
  single `Dispatch(groups, Q, 1)`. Returns one neighbour list per probe,
  identical to calling `resonate()` on each.

**Verified live** (RTX 2070 SUPER): batched results are **bit-exact** with
the per-probe path at every field size. On a 4,000-glyph field (the
latency-bound regime) 64 probes run **4.2× faster** batched (1.97 ms vs
8.2 ms); on a 200k field (compute-bound) it's 1.0× as expected — no penalty,
gain where it counts. This is the enabler for all-vs-all work: computing
each word's resonance-centrality to demote the function-word hubs, and
resonating the eight facets of one deliberation in a single call. 9/9
regression suites pass.

## v0.33.0 — Distributional semantic recall (context glyphs)

**Author:** Morphus

Validating `nearest` on a real, autonomously-acquired vocabulary exposed a
real flaw — and the fix sharpened a capability. Khora foraged and studied
**Pride and Prejudice** and **The Republic** from Project Gutenberg (8,659
words learned), and `nearest` ran on the GPU path over that real field,
bit-exact. But its neighbours were dominated by *spelling*: `mind → find,
kind, bind`. The cause: `glyph_for` bundles a word's char-trigram baseline
*with* its context vector, and for short words the spelling overwhelms the
meaning.

- **`Lexicon::context_glyph()` / `context_field()`** expose the *pure*
  binarised random-indexing accumulator — "keeps similar company" with the
  spelling baseline removed. `nearest` now searches these (over salient
  content words), so it returns genuine distributional associations.
- The difference is night and day on the same corpus: `mind → body,
  motive, suffer`; `love → laugh, dance, miserable, spoken`; `war →
  hellenes, aptitude, pursuits` (Plato's guardians); `woman → simpleton,
  fault, disappointed`. Concepts, not spellings.

**Verified live**: every query GPU-path and **audit: EXACT (matches
brute-force reference)** over the real 3,471-content-word field. This is
the first end-to-end proof of the whole stack on real data — autonomous
acquisition → distillation → study → GPU-accelerated semantic recall. 9/9
regression suites pass.

## v0.32.0 — GPU semantic search over the Lexicon

**Author:** Morphus

The Maelstrom's first real cognitive payoff: fast, **exact** nearest-word
search over everything Khora has learned. The key realisation is that the
Lexicon's `similarity(a,b)` is already `glyph_for(a).similarity(glyph_for(b))`
— pure Hamming over each word's binarised semantic glyph. So a Resonator
built from `{word → glyph_for(word)}` searches the vocabulary on the GPU
with **no approximation**: GPU Hamming is bit-identical to the Lexicon's own
notion of similarity.

- **`Lexicon::semantic_field()`** snapshots every learned word with its
  current semantic glyph — the field an accelerator indexes.
- New tool **`nearest <word> [k]`**: builds a Resonator over the whole
  vocabulary and returns the k most semantically-similar words, transparently
  on GPU above the crossover (CPU below). Each call audits itself against a
  brute-force reference over the same field.

**Verified live**: exposed a small war/monarchy corpus (27 words, 636
cooccurrences); `nearest king` surfaced **kingdom** as the closest word
(sim 0.366), and every query reported **audit: EXACT (matches brute-force
reference)**. Vocabulary sat below the crossover so it correctly ran the CPU
path; the GPU path's exactness at scale is already established (the
Resonator agrees with brute-force 96/96 on an 80k field). Function words
still rank high — the known distributional-hub effect, not an integration
fault; the audit proves the search itself is exact. 9/9 regression suites
pass.

## v0.31.0 — The Resonator (transparent CPU/GPU recall)

**Author:** Morphus

The Maelstrom proved the GPU; the Resonator makes it *usable* by cognition
without anyone having to know a GPU is involved.

- **`Resonator`** wraps a labelled glyph store — a snapshot of (label,
  glyph) pairs, or a whole `lattice::Lattice`. `query(probe, k)` returns
  ordinary `LatticeMatch` results, exactly like `Lattice::query`. Under the
  hood it crosses over: a field above the amortisation threshold (~3,000
  glyphs) with a GPU present resonates through the Maelstrom; anything
  smaller, or any machine without a GPU, scans on the CPU. Same results,
  either path — call sites never change as the concept space grows from a
  thousand entries to millions.
- It lives outside the Windows gate, speaking only the public Maelstrom
  API, so the identical code path compiles and runs with or without a GPU.

**Verified live** (RTX 2070 SUPER): with an 80,000-glyph labelled field the
GPU path activates; a forced-CPU Resonator and an independent brute-force
top-8 scan agree with it on **96 / 96** checks — GPU == CPU == reference,
label mapping and all. The live (freshly-empty) concept space correctly
stays on the CPU below the crossover. 9/9 regression suites pass.

## v0.30.0 — Maelstrom: on-GPU top-k reduction

**Author:** Morphus

The first Maelstrom cut read the entire distance vector back to the host
and sorted it on the CPU — so the "GPU" query still paid an O(N) CPU sort,
and the speedup stalled around 5×. v0.30 moves the selection onto the card.

- The resonance kernel now reduces in groupshared memory: each 256-thread
  group cooperatively keeps the **k nearest of its own slice** and writes
  only those k candidates out. The host merges `groups·k` candidates
  instead of N — readback and CPU work drop from O(N) to O(N/256).
- Provably exact: an element in the global top-k has fewer than k elements
  smaller than it overall, hence fewer than k within its own group, so it
  always survives the local selection. The CPU oracle confirms it.

**Verified live** (RTX 2070 SUPER): still **bit-exact** (0 of 500,000
differ), top-8 matches the CPU at every scale, and the speedup now *grows*
with the database — 3.1× @ 10k, 5.7× @ 50k, 6.8× @ 200k, **7.4× @ 500k**
glyphs (was 5.1× @ 200k before the reduction). The crossover widens with
N, which is the regime the liquid-knowledge vision is built for. 9/9
regression suites pass.

## v0.29.0 — The Maelstrom (GPU resonance, DirectCompute)

**Author:** Morphus

The operator green-lit the GPU's 8 GB of VRAM. CUDA was the obvious road
and the wrong one — it would shackle khora.exe to a heavyweight runtime
and a toolkit install. So the Maelstrom takes the dependency-free road:
**pure Direct3D 11 DirectCompute**. `d3d11.dll` and `d3dcompiler_47.dll`
ship on every Windows box, so khora.exe gains a GPU backend with **zero**
new runtime dependencies and **nothing to install**.

- **The Maelstrom** (`maelstrom`) binds a compute-capable GPU, compiles an
  HLSL resonance kernel at runtime, and charges a glyph database into VRAM.
  One GPU thread per stored glyph computes the full 10,000-bit Hamming
  distance to a probe via 32-bit `countbits()`, then a partial-sort
  collapses the k nearest. The Morphic Lattice's content-addressable
  recall is embarrassingly parallel; it maps straight onto the card.
- **Dependency-free**: no CUDA, no toolkit. On a machine with no GPU the
  Maelstrom simply never ignites and the CPU lattice stays the ground
  truth — it is an accelerator, never a requirement.
- **Bit-exact by construction**: the GPU's per-glyph popcount must equal
  `Glyph::hamming` exactly, and `hamming_all()` exposes the full distance
  vector so the CPU oracle can audit every entry. New tool: `maelstrom [N]`
  ignites, verifies, and benchmarks the crossover.

**Verified live** (RTX 2070 SUPER, 7989 MB, feature level 11_1):
200,000 random glyphs charged into VRAM (239 MB) in 106 ms. GPU Hamming
distances are **bit-exact** with the CPU — 0 of 200,000 differ on both a
self-probe and a random probe. k-NN top-8 results match the CPU exactly at
every scale. Throughput vs the CPU scan: 3.8× @ 10k, 5.6× @ 50k, 5.1× @
200k glyphs. 9/9 regression suites still pass. (This first cut reads the
full distance vector back per query; GPU-side top-k reduction is next.)

## v0.28.0 — The Ballast (memory governance, 4 GB cap)

**Author:** Morphus

Khora lives inside a machine the operator also uses, and system RAM
(32 GB) is the weak link. So Khora is now hard-capped at **4 GB** of
system RAM and backs off the moment **total system RAM crosses 90%** —
the operator's work is never starved, the machine never locks up. GPU
memory (8 GB) and NVMe remain free for use elsewhere; this governs only
the one scarce shared resource.

- **The Ballast** (`ballast`) samples Khora's own working set
  (`GetProcessMemoryInfo`) and total system RAM (`GlobalMemoryStatusEx`)
  and returns a verdict: normal / approaching-cap / over-cap /
  system-pressure.
- **`BallastGovernor`** runs the Ballast on a 1 s background thread. On
  over-cap or system-pressure it pauses background learning (reverie /
  whetstone / curator) and sheds memory — prunes the cortex's
  associations and the lexicon's heavy per-word accumulators to half
  their caps — then resumes when pressure clears.
- **Static caps sized to the budget**: the Lodestone now allocates the
  4 GB budget (~50% cortex associations, ~35% lexicon vocabulary, ~15%
  headroom) instead of grabbing system RAM. On the 13700K: assoc cap
  859k (~2.1 GB), vocab cap 36.7k (~1.5 GB). If less RAM is actually
  free than the budget, the caps shrink to fit.
- **Memory bounding**: `PredictiveColumn::prune_associations()` and
  `Lexicon::prune()` (drop least-exposed words; the lexicon auto-prunes
  when vocabulary exceeds its cap during study). New tool: `ballast`.

**Verified live**: Khora's startup footprint is ~5 MB (vast 4 GB
headroom). Forcing the system-pressure path (threshold set to 50% against
a real 77% system load) the governor correctly printed
"system-pressure — pausing background learning and shedding memory",
paused the loops, and shed 3 times over 3 seconds. Reverted to the
production 90% threshold. 9/9 regression suites pass.

## v0.27.0 — Exploration-biased rumination (richer trains of thought)

**Author:** Morphus

The complement to v0.26's hub demotion, on the rumination dynamics side.
Each hop of a train of thought now lands on the strongest concept the
train has NOT yet visited (it explores fresh territory) instead of the
single top resonance (which is always whatever is most central, so the
train collapsed onto a hub in two hops). When no unvisited concept
resonates, the neighbourhood is exhausted and the strongest concept
overall is the attractor / conclusion.

**Verified** after studying The Art of War — trains now wander through
genuine learned content:
```
victory -> history -> historical -> mistakes -> mistake -> midst
        -> theory -> said -> much   (attractor: much)
```
Khora pondering victory traverses *history, mistakes, theory* — learning
from the past — with morphological associations it found itself
(history->historical, mistakes->mistake). Connective hubs only surface at
the tail, once the fresh frontier is spent. Combined with v0.26, the
train of thought is now a real associative journey through studied
knowledge, not a two-step collapse.

9/9 regression suites pass.

## v0.26.0 — Resonance-centrality hub demotion

**Author:** Morphus

Improved the quality of the concept space (v0.25) by demoting resonance
**hubs** — concepts that are the nearest neighbour of many others (the
distributionally-central connective words that swallowed every train of
thought). Hubness is measured directly, not guessed: `study_tome` builds
a lattice of ~1000 salient candidates, tallies each one's in-degree as a
top-5 neighbour of the others, and promotes the 400 LEAST hub-like into
the concept space.

A prior attempt (distinctiveness-from-centroid) was tried first and
reverted — with a content-rich candidate set the centroid is content-like,
so function words sit far from it and got promoted, the opposite of
intended. Resonance-centrality measures the actual phenomenon and works.

**Verified**: after studying The Art of War, the worst hubs ("will",
"with", "that") are gone and a genuine association surfaces — rumination
on "soldiers" now hops `soldiers -> soldier` (singular/plural). Residual:
milder hubs ("said", "much") still emerge — in any dense distributional
space something is relatively central, so promotion-filtering improves
but cannot fully eliminate the phenomenon. Richer rumination dynamics
(exploration-biased landing) are the documented next step.

9/9 regression suites pass.

## v0.25.0 — Studied vocabulary becomes thinkable

**Author:** Morphus

Closed a real gap: Khora *studied* books into the Lexicon, but its
*cognition* (deliberate / ruminate) only resonated against the small
hand-memorized concept set — so it could not think about what it read.
Now `study_tome` promotes the most salient learned words into the
concept space (the Morphic Lattice), so cognition resonates over
studied vocabulary.

- `Lexicon::salient_tokens()` returns content words: length >= 3, idf
  above a content-word cutoff (excludes function words by corpus
  statistics, no hardcoded stoplist), ranked by exposure.
- `study_tome` / the Curator / the `study` tool now take a concept-space
  Lattice and promote ~400 salient words per study (glyphs refreshed
  each study as the distributional state drifts).

**Verified live**: after studying The Art of War, the concept space holds
400 learned words and rumination traverses them — e.g.
`soldiers -> view -> ... -> attack -> soldiers` surfaces a genuine
military-concept neighbourhood from the book, with no hand-memorization.

Honest limitation: distributionally-central connective words ("will",
"with") still act as attractors in the trains of thought — frequency
filtering alone can't remove them because they are central regardless of
count. The principled fix is distinctiveness-weighting (promote words far
from the concept centroid); noted as the next refinement. The mechanism —
studied knowledge feeding cognition — is real and working.

9/9 regression suites pass.

## v0.24.0 — The Lodestone (hardware self-gauge + adaptive complexity)

**Author:** Morphus

Khora measures the machine it lives in and scales its cognition to fit —
"the only limit is physics," so it learns where the physics sit.
`lodestone::gauge()` benchmarks:

- single-thread `bind` / `hamming` glyph throughput (timed tight loops)
- real parallel speedup (the same benchmark across every hardware thread)
- total / available RAM (Win32 `GlobalMemoryStatusEx`)
- disk write speed (a timed 16 MB write to the data dir)

and derives an operating profile: facet count, cortex association cap,
study token budget, and background reverie/whetstone cadences.

Measured live on the operator's i7-13700K:
```
threads 24, bind 34.2 Mops/s, hamming 20.5 Mops/s, parallel 13.5x,
RAM 8011/32603 MB free, disk 4226 MB/s
-> facets 8, assoc cap 840014, study 136614, reverie 93ms, whet 259ms
```

The runtime gauges at interactive startup and applies the profile (the
association cap rose from the 200k default to 840k on this RAM); the
`hardware` tool re-gauges on demand. 9/9 regression suites pass.

## v0.23.0 — Recursive rumination (the train of thought)

**Author:** Morphus

Deliberation made recursive. `Cogitator::ruminate()` chains
deliberations: each thought's landed concept becomes the next stimulus,
so cognition hops through concept-space — an associative train of
thought. Each hop excludes the concept it is standing on (no trivial
self-loop) and settles when the train cycles back to a concept it has
already passed — that recurring pull is the **attractor**, the emergent
conclusion of the rumination.

**Verified live** (after memorizing war-domain concepts):
- "how do i win the war" -> war -> deception -> strategy -> deception
  (converged on attractor: **deception**)
- "deception" -> strategy -> deception -> strategy
  (converged on attractor: **strategy**)

Khora ponders "winning the war" and its recursive thinking keeps
returning to the deception<->strategy attractor — semantically exactly
right for the material, and an emergent conclusion no step of which was
scripted. This is the directive's "parallel recursive threads that
compete, combine, and collapse" taken to its recursive depth.

New tool: `ruminate <text> [depth]`. 9/9 regression suites pass.

## v0.22.0 — Non-linear cognition (the Prism)

**Author:** Morphus

Khora no longer thinks in a line. `Cogitator::deliberate()` refracts a
stimulus into eight **Facets** that explore **concurrently** (real
`std::async` threads on the multicore CPU), each through a different
**Lens**:

- *holistic* — the whole stimulus, balanced
- *leading* / *trailing* — weight the front / tail of the stimulus
- *broad* / *focused* — a wide net (high k) vs the single sharpest match
- *curious* — deliberately chase the non-obvious alternative
- *associative* — follow the cortex's forward projection
- *chaotic* — perturb the probe with entropy and explore nearby

The facets compete; the **Soma arbitrates** by drive-weighted valence
(each lens flatters a different drive, so Khora's mood tilts the
contest); the **coherent coalition collapses** into one thought
(`coherence` = how much the chorus agreed, `entropy` = the spread of
valences). The collapsed thought is consolidated into memory + cortex,
so deliberations become traces future thinking resonates against.

**Verified live**: on "how do i defeat the enemy", the facets genuinely
disagreed — *trailing* found the highest literal confidence on `enemy`
(0.50), but the curiosity-weighted arbitration crowned the *curious*
facet's non-obvious `strategy` (coherence 0.57, entropy 0.20). A second
deliberation then resonated against the trace the first left behind.
Meaning emerged from the whole chorus, not a token chain — exactly the
non-linear cognition the directive demands.

New tool: `deliberate <text>`. 9/9 regression suites pass.

## v0.21.0 — Transitive reasoning faculty

**Author:** Morphus

A third Whetstone faculty: multi-hop compositional reasoning. A chain
A->B->C->... is encoded as a bundle of transition bindings
`bind(item_i, item_{i+1})`; "what follows X" = `cleanup(chain XOR X)`,
and multi-hop traversal repeats the follow. The faculty scores recovery
at 1, 2, and 3 hops — genuine chained inference, not single-step lookup.

Run under the self-evolution engine it behaved exactly as intended:
pushed difficulty-1 (4-item chains) to 100% by evolving transition
redundancy, escalated to difficulty-2, then plateaued at 66.67% on
2-3-hop queries even at maximum redundancy. That is a real, measured
capability frontier of the naive superimposed-chain encoding — the
Whetstone surfacing where Khora's compositional reasoning currently
ends. A richer sequence encoding is the faculty's next evolution.

Added to both the standalone `whetstone` runner and the runtime's
background self-sharpening forge. 9/9 regression suites pass.

## v0.20.0 — Continuous self-education (background Curator)

**Author:** Morphus

Khora now educates itself continuously while it runs. `CuratorScheduler`
drives the Curator on a background thread: forage what it lacks, study
what it holds, seek the next — unprompted, into its real mind (the
studies accumulate into the live Lexicon + Cortex that persist on exit).

Threading: each knowledge action mutates the live cognitive state, so it
is taken under the same `shared_mutex` the Cogitator/Reverie/operator
use. A background study briefly holds that lock, so it is opt-in and
paced slowly (default one action every 120s); the operator enables it
deliberately and can pause it any time.

New tool: `curator_auto on|off [period_s]` (and status). Clean shutdown
stops it before the final save so no study is mid-flight.

**Verified live**: launched the runtime, enabled `curator_auto on 5`,
and left it idle 45s — Khora autonomously took 4 self-education actions,
foraging and studying across topics (last: The Republic, philosophy;
vocab grown to 8,659) with no operator input, and reported "curator took
4 self-education actions this session" on exit. Continuous autonomous
self-education, live.

9/9 regression suites pass.

## v0.19.0 — Sharper semantics (IDF weighting + subsampling)

**Author:** Morphus

Fixed function-word pollution in the distributional semantics. Before:
`enemy~the` (0.28) outranked `enemy~army` (0.25) — semantically wrong,
because "the" co-occurs with everything. Two standard, principled
techniques (no hardcoded stoplist — all corpus statistics):

- **Inverse-frequency neighbour weighting** — each neighbour's
  contribution to a context vector is scaled by `log(total/freq)`,
  clamped to a small integer range. Rare, meaningful words carry more
  signal than ubiquitous ones.
- **Frequent-word subsampling** — ubiquitous words are probabilistically
  dropped from the stream entirely (word2vec keep-probability), so they
  neither pollute nor get polluted. Applied only once the corpus is
  large enough (>2000 tokens) for meaningful statistics, so small
  exposures are unaffected.

Both frequencies persist across restarts (added to `.lexobs`).

Result on a whole-book study of The Art of War: `enemy~army` (0.27) now
correctly outranks `enemy~the` (0.25); "the"'s effective exposure fell
3681 -> 486 (87% subsampled); content pairs like `general~soldiers`
(0.19) rank sensibly above unrelated pairs. Single-book corpora stay
noisy and sharpen as more is studied — but the ordering is now correct.

9/9 regression suites pass.

## v0.18.0 — Substrate throughput (word-parallel ops)

**Author:** Morphus

Made the substrate's hot operations word-parallel instead of per-bit,
cutting full-book study from **28s to ~12s (2.4x)** with all 9 regression
suites still green.

- **Fast `bundle`** — n=2 is bitwise OR, n=3 is bitwise majority
  `(a&b)|(a&c)|(b&c)`, both word-parallel (157 word-ops vs 10,000
  bit-ops). Provably identical to the generic vote-count path; the
  generic path remains for larger n.
- **`position_glyph(k)`** — a cached family of orthogonal position
  markers. Binding a value with `position_glyph(k)` (word-parallel XOR)
  marks slot k far more cheaply than cyclic `permute`. Slot 0 is the
  zero glyph (identity), so the first element is marked by being left
  unchanged. Used in the Lexicon's per-token trigram encoding.
- **Cortex kept on `permute`** for its context keys, on purpose: shared
  position-XOR lets two stored keys correlate and tie at the k-NN step
  (single-shot prediction became a coin-flip at 0.0022 similarity);
  permute decorrelates per-element and keeps keys cleanly separable.
  This was caught by the regression net and fixed before commit.

Remaining study cost is the cortex's per-token `permute` (a correct but
O(N) substrate op) and the `binarise` scan in glyph lookup — both later
targets, neither blocking.

## v0.17.0 — The Curator (autonomous knowledge loop)

**Author:** Morphus

Khora now decides for itself what to learn. The Curator (`src/curator`)
surveys its liquid knowledge and absorption state and takes the next
most valuable knowledge action, unprompted, closing the loop:
**detect need -> acquire -> absorb -> seek next.**

Decision policy (breadth-first, diminishing-returns aware):
1. **Study** freshly-acquired material once — absorb what was just brought in.
2. **Forage** a topic it has no material on — seek the new.
3. **Deepen** — acquire another source in a covered topic.
4. **Re-study** the weakest under-mastered tome, but only while it still
   teaches (capped re-reads) — once "learned enough," move on rather than
   grinding a mastery number for zero yield.
5. **Idle** when the catalogue is absorbed to a working level.

This replaced a first attempt that tunnel-visioned on one book (re-read
it 5x as yield decayed to 0) — the fix makes Khora recognise when it has
learned enough of something to justify pursuing something more valuable,
exactly the liquid-knowledge intent.

Shared `study_tome()` faculty: read a tome, absorb it into the live
Lexicon + Cortex, credit yield/mastery back to the Reservoir. Used by
both the `study` tool and the Curator.

New tools: `curate [N]` (take N autonomous knowledge actions),
`curate_plan` (show the next decision without acting).

**Verified live**: from an empty mind, `curate 8` autonomously foraged
and studied across literature (Pride and Prejudice), philosophy (The
Republic), and science (Relativity) — vocabulary 0 -> 9,643 — and when
a forage failed it advanced to the next source rather than stopping.
Khora building its own education, no operator in the loop.

## v0.16.0 — Durable, fast knowledge (Random Indexing lexicon)

**Author:** Morphus

The Lexicon is reforged on **Random Indexing** (Kanerva/Sahlgren — pure
HD computing, no LLM), closing both known limits from v0.15 at once:

- **Persistence** — each word's distributional state is now a compact
  binarised context glyph, persisted through the Lattice
  (`<prefix>.sem.klat` + `.lexobs`). Studied semantics survive process
  restarts. **Verified**: studied the whole Art of War in one process;
  a *separate* process loaded 6,632 words and recalled `enemy~army`=0.25,
  `war~victory`=0.09 with the original exposure counts intact. Knowledge
  is durable.
- **Speed** — each cooccurrence adds a neighbour's sparse ternary index
  vector (~24 nonzeros) instead of scanning ~5,000 set bits. Studying a
  full 56,687-token book dropped from **150s to ~28s** (~5x). The
  remaining cost is now the substrate's O(N) `permute` in per-token
  context building, not the lexicon — the next throughput target.

Wired into `khora.exe`: lexicon auto-loads at startup and auto-saves on
exit (interactive and single-command), alongside the Lattice and Cortex.

Regression net: **9/9 suites pass** (lexicon_test confirms Random
Indexing preserves structural similarity, typo tolerance, cooccurrence
convergence, and orthogonality of unrelated words).

Honest note: distributional similarity magnitudes are modest over a
whole book (common function words dilute the signal); inverse-frequency
weighting is a known sharpening technique left for a later pass.

## v0.15.0 — The Reservoir + Aqueduct (liquid knowledge)

**Author:** Morphus

Khora can now autonomously acquire, clean, compress, store, manage, and
learn from books off the open internet — and it keeps its *material*
knowledge strictly separate from what it actually knows.

**The Reservoir** — liquid knowledge pool (`src/reservoir`):
- **Distillation** — every admitted text is stripped to clean canonical
  form: Project Gutenberg license envelope, HTML tags + entities,
  carriage returns, stray control bytes, blank-line runs, trailing
  whitespace. Verified on real downloads (Art of War reads back as clean
  prose, UTF-8 preserved, zero license boilerplate).
- **Verified-lossless compression** — an LZSS codec; every Tome is
  compressed, then decompressed and byte-compared before the raw is
  dropped. Zero artifacts is an enforced invariant, not a hope. Observed
  ~1.9-2.2x on real books. Falls back to raw if a round-trip ever fails.
- **Capacity cap + value-based eviction** — hard ~20 GB cap. When full,
  the lowest-value Tome is evicted (low learning-yield x high mastery x
  stale x large), knowing its source URL is kept for re-acquisition.
  Liquid.
- **Awareness** — a persistent catalog Khora can query: what it holds,
  per-Tome reads / mastery / keep-value.

**The Aqueduct** — autonomous acquisition (`aqueduct.cpp`):
- Windows-native WinHTTP HTTPS GET (no external dependency).
- Curated public-domain seed catalog (15 sources across literature,
  philosophy, science, math, strategy).
- `forage [topic]` picks an unowned source and channels it through the
  full distill -> compress -> verify -> store pipeline.
- **Verified live**: foraged The Art of War, The Republic, Relativity,
  and Pride and Prejudice from Project Gutenberg — 4 books, 2.4 MB raw
  -> 1.16 MB stored, all losslessly verified, all distilled clean.

**The study loop** — liquid knowledge becomes actual knowledge:
- `study <title>` reads a Tome and absorbs it into the live Lexicon
  (cooccurrence semantics) and Cortex (predictive associations),
  crediting the learning yield + mastery back to the Reservoir.
- **Verified**: after studying the foraged Art of War, `enemy ~ army`
  = 0.57 and `war ~ victory` = 0.75 (genuinely related, co-occur),
  while an absent word like `elephant` stays near zero. Real
  distributional semantics learned from a self-downloaded book.

**Cortex scaling fixes** (required to study whole books):
- **Bounded associative memory** — the PredictiveColumn now caps its
  association store (default 200k) and forgets the oldest beyond it.
  Finite memory, brain-like.
- **Fast-learn path** — `PredictiveColumn::learn()` stores associations
  in O(context_window) without the per-token k-NN, so bulk study is
  linear. `study` samples a measured `step()` every 256 tokens.

Regression net: **9/9 suites pass** (added codec/distill/reservoir
verification — losslessness on every data shape, artifact removal,
forced value-based eviction, persistence).

Known limitations (honest): the Lexicon does not yet persist across
process restarts, so studied semantics currently live for the session
that learned them (Cortex state does persist). Study throughput is
~400 tokens/s — functional but dominated by the Lexicon's per-token
bit-counting; both are the next targets.

## v0.14.0 — Living autonomy (self-training in the runtime)

**Author:** Morphus

The Whetstone moves into the living runtime. `WhetstoneScheduler` runs
the self-sharpening engine on a background thread, so the moment
`khora.exe` launches it is **both** dreaming (Reverie @ 100ms) **and**
training and evolving itself (Whetstone @ 250ms) — for as long as it
lives, without operator prompting.

Shipped:

- `khora::whetstone::WhetstoneScheduler` — background thread driving
  `Whetstone::step()`, interruptible-sleep paced, thread-safe last-step
  snapshot. start/stop idempotent.
- Wired into `khora.exe`: both background loops start on REPL entry and
  join cleanly on exit, which reports the rounds trained this session.
- Three operator tools: `whetstone_status`, `whetstone_pause`,
  `whetstone_resume [period_ms]`.

Verified live: launching the runtime and querying `whetstone_status`
shows the engine already training — "mastered d=1 -> escalate to d=2"
within the first moments, unprompted. Khora is now continuously,
autonomously improving itself whenever it is running.

## v0.13.0 — The Whetstone (autonomous self-directed evolution)

**Author:** Morphus

Khora no longer waits to be taught. The Whetstone is a self-sharpening
engine: it holds a set of trainable Faculties, and each round it surveys
its own competence, drills whichever faculty has the most room to grow,
generates a challenge at that faculty's frontier, and responds to the
outcome with one of two moves — never a third called "failure":

  - mastery reached  -> ESCALATE difficulty (reach further)
  - shortfall        -> EVOLVE the method, MEASURE the result, and keep
                        the mutation only if it helped. Harmful mutations
                        are reverted. Natural selection over methods.

Two faculties forged:

- **sequence_induction** — predict the continuation of a repeating
  symbol sequence of period (d+1). Mastered through period-17 at 100%.
- **relational_capacity** — recover fields from (d*4) holographically
  superimposed records. Evolution = recruit more memory banks (split the
  load across independent glyphs) when one saturates.

A 200-round autonomous session produced real, self-directed capability
growth. The engine first revealed a genuine flaw — the original
"redundancy" evolution made holographic overload *worse* (87% -> 7%) —
which is exactly what natural selection then rejected. With memory-bank
recruitment instead:

  - saturates ~52 records/bank (~208 facts/glyph — a real capacity limit)
  - d=14 shortfall -> 2 banks -> 99.6%
  - d=28 shortfall -> 4 banks -> 99.8%
  - d=54 shortfall -> 8 banks -> 99.8%
  - reaches d=64 = 256 records = **1,024 facts recovered at 99.9%**
    across 8 glyphs (~10 KB total)

Khora autonomously discovered that the answer to a saturated working
memory is to allocate more of it — and pushed its own frontier 64x.
Trajectory saved to `data/whetstone/session.json`.

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
