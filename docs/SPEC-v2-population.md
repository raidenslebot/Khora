# SPEC v2 — a population, not a mind

**Status: DEAD.** Built, measured, and rejected on the same day it was proposed.
Every kill criterion below fired, and independently the prior art says the whole
design is a rediscovery of a 1995 algorithm whose known failure modes it walked
into. Kept as the record of why, because the reasoning was sound and the answer
was still no.

## VERDICT (measured before anything was wired in)

24,000 tokens of real prose, one shared budget of 400,000 segments, N = 8:

```
arm            | train burst | held-out burst | segments (budget) |  time
A monolithic   |       0.985 |          0.978 |  401476 / 400000  |  116 s
B partition    |       1.000 |          0.992 |  408128 / 400000  |   38 s
C competition  |       1.000 |          0.998 |  405065 / 400000  |  212 s
D selection    |       0.993 |          0.985 |  401223 / 400000  |  260 s
```

**The monolith wins.** Every population arm predicts WORSE and costs 2–7× the
time. Competition (C) is the worst of the four despite routing cleanly — claims
were balanced 290/288/271/438/355/403/272/255, so this is not expert collapse,
it is competition buying nothing because there is no signal to route on.
Selection (D) fired no births at all: with every organism bursting at ~1.0 there
was never a fitness gap wide enough to justify a death.

Against the criteria written before the run: B ≈ C, so competition is deleted.
C ≈ D, so selection is deleted. No arm bounds — every arm simply hit the
imposed cap, which is a bound by fiat rather than a bound that was earned. And
the fourth criterion applies to all of them: at burst ≈ 1.0 nothing here
predicts anything, so none of it is a cognitive architecture.

## WHY IT WAS ALWAYS GOING TO FAIL

A survey of the prior art, run in parallel with the build, returned two things
that between them close the question.

**First, this is XCS.** Wilson 1995, Michigan-style learning classifier systems,
item for item: accuracy-based fitness rather than payoff-based, covering on
failure, a niche GA operating inside the match set, a fixed population with
niche-balanced deletion. Thirty years of theory and failure data exist on it.
Butz et al. 2004 identifies the *covering–deletion cycle* — classifiers created
and immediately deleted, forever — as what happens when a hard budget meets
inputs too specific to match, which is precisely this workload.

**Second, and more damaging: the diagnosis was wrong.** The 93% burst is not a
one-module failure. It is ART's *category proliferation*, a property of the
MATCH CRITERION rather than of how many allocators there are. Splitting one
allocator into eight changes how many things allocate; it does not change the
probability that a given context matches something already stored. And
splitting the stream makes it worse — c-BTM measured the crossover directly:
past an optimal cluster count each expert is data-starved and performs worse.
Their budgets ran to 168 billion tokens. This experiment had 24,000.

The deepest version, and the one worth carrying forward: **a system that only
specialises when it fails to predict is a ratchet with no pawl.** XCS bounds its
population because the `#` wildcard plus accuracy-based fitness creates an
opposing GENERALISATION pressure. This proposal had allocation-on-failure and
nothing pushing the other way. Under a fixed budget that does not converge, it
merely converts unbounded growth into unbounded churn.

---

*Original proposal follows, unedited.*

---

## 0. Where this came from

The operator's framing: *what if the brain could create — model its whole
environment, and build and deploy its own organisms into it?* Alongside it, a
sketch of a synthetic-biology analogue: a streamlined genome as the "weights",
a chemical environment as the input, replication as the copy operation, and
directed evolution — organisms that fail are starved, organisms that work
divide — replacing gradient descent.

Khora is a C++ program, so the transfer is architectural: what a genome, an
environment, replication and selection correspond to in software. That is the
whole of it, and it transfers because it answers a failure this project has
already measured rather than because it sounds impressive.

## 1. The measured failure it answers

`TemporalMemory` allocates on surprise: a minicolumn driven with nothing primed
BURSTS, and one of its cells grows a distal segment to learn the context it did
not recognise. On repeating sequences that is exactly right, and it is why the
module beats the dense chain 100% to chance on sequences sharing a middle.

On natural language it does not converge. Measured, 24,000 tokens of real prose:

```
tokens | segments | marginal seg/token | burst frac |   RAM   | ms/step
   512 |   104850 |              201.9 |      0.999 |  20.6 MB |   1.549
  4096 |   688285 |              153.9 |      0.956 | 135.2 MB |   7.544
 24000 |  2907349 |               98.1 |      0.929 | 571.0 MB |  25.376
```

Burst fraction never leaves 0.93. It is not learning to predict prose; it is
memorising, and it will memorise until it exhausts the machine. Khora's real
corpus is 2.67M observations — extrapolated, roughly 55 GB.

The reason is structural. An eight-word window of English essentially never
recurs, so every step is novel, so every step allocates. One model of
everything, on a stream whose contexts are near-unique, is the wrong shape.

## 2. What transfers from the sketch

| Sketch | Khora |
|---|---|
| streamlined genome | a small heritable parameter vector per organism: its NICHE, activation threshold, synapses grown per step, permanence decay |
| chemical environment | the token stream, and the Plexus graph already learned from the corpus — Khora's existing model of its environment |
| specialised cell | one small `TemporalMemory` claiming a narrow region of input space |
| replication | a successful organism spawns a mutated variant with a nearby niche |
| **starved of nutrients** | **a FIXED GLOBAL SEGMENT BUDGET.** Organisms that predict well earn budget; ones that burst constantly are starved and die |

The last row is the one that matters, and it is why this is worth trying rather
than merely evocative. The resource that is *actually* scarce here — memory —
becomes the selection pressure. The measured failure is unbounded growth; a
design in which growth must be *won* from a fixed pool addresses that failure
directly rather than by analogy.

Set aside entirely: organoids, wetware, DNA storage, anything wet. Those are
decoration for a program that runs on a CPU.

## 3. The experiment, defined before the mechanism

Same 24,000-token prose stream. **Same total segment budget for every arm** —
otherwise the comparison is meaningless.

| Arm | What it is | What it tests |
|---|---|---|
| **A** | one monolithic TemporalMemory | the baseline, already measured: burst 0.93, 2.9M segments, does not converge |
| **B** | N small TMs, inputs assigned **at random** | does PARTITIONING alone do the work? |
| **C** | N small TMs, inputs assigned by **competition** — each input goes to whichever organism predicts it best | does SPECIALISATION earn its place over random partitioning? |
| **D** | C, plus **replication and starvation** under the fixed budget | does SELECTION earn its place over static specialisation? |

Reported for each: burst fraction (does it learn?), total segments (does it
bound?), ms/step (does it stay affordable?), and predictive accuracy on held-out
prose from the same books.

**Arm B is the arm that matters.** It is entirely possible that splitting one
model into N smaller ones bounds growth by itself, because each organism sees
less of the stream and its contexts recur more within its own slice. If B ≈ C ≈
D then specialisation and evolution are decoration and the finding is
"partition it", which is a perfectly good finding and a great deal less code.

## 4. Kill criteria, written now

- **If B matches C**, competition is deleted. Random partitioning is simpler and
  the honest answer.
- **If C matches D**, replication and starvation are deleted. A static set of
  specialists is simpler than an evolving one.
- **If no arm bounds** — if total segments still grow linearly in the corpus
  across every arm — the whole direction is wrong, and the problem is the input
  encoding rather than the architecture. In that case the answer is upstream:
  give words similarity-preserving codes so that contexts recur in code space
  even when the exact words never recur, and the population idea is abandoned.
- **If an arm bounds but predicts no better than a trigram table**, it is not a
  cognitive architecture, it is a lossy hash table with extra steps. That
  baseline has already beaten this project once and it must be reported beside
  every arm.

## 5. Open, pending prior art

This proposal has obvious cousins and the honest position is that it may be a
rediscovery. Mixture-of-experts is the ML version and its routers are trained
by gradient descent, which Khora does not have. Growing Neural Gas and Adaptive
Resonance Theory are the non-gradient precedents for allocate-on-failure.
MAP-Elites maintains a population of specialists across a behaviour space, which
is very close to what is described here.

If this turns out to be one of those under a different name, the useful outcome
is to say so plainly and take the known result rather than re-derive it badly.
That question is being researched before any of it is built.
