# SPEC v3 — Ribosome: the organ that constructs

**Status:** substrate built, tested, and benched against real ground truth.
Whether it earns its place is decided in "Results" below, and the answer at the
time of writing is *not yet*.

---

## Why this exists

Khora could do three of the four things a system needs in order to be more than
a model of its input:

| | organ | state |
|---|---|---|
| perceive | Plexus, Lexicon, ContextTree, TemporalMemory | built |
| act | Carapace — 95 reachable tools | built |
| contain | Bulwark — job objects, low-integrity tokens, DACL repair | built |
| **construct** | — | **missing** |

Nothing in Khora could emit a capability that did not previously exist. Every
faculty it has, a human wrote. Ribosome is the organ that closes that gap.

### The specific target

Every vector symbolic architecture in the literature **hand-designs its
composition**. To encode a role-filler pair you bind with a role vector; to
encode a sequence you permute; to encode a set you bundle. Which primitive, in
which order, with which role vector, is always a human decision. That decision
*is* the architecture, and it has never been searched — only chosen.

So the thing worth evolving is not a better predictor. It is the composition
itself: given pairs of words standing in some relation, find the program that
carries the first to the second, in a form that generalises to pairs it was
never shown.

---

## Design

### Genome — a linear byte tape

Four bytes per codon: `(opcode, dst, a, b)`. Every field is masked into range on
decode, so **the decoder is total**: every possible byte string is a running
program.

This is the property the whole design rests on. Tree-based genetic programming
produces invalid offspring that need repair, and the repair rule is a human
prior smuggled into the search. A total decoder over a linear tape needs no
repair, so mutation and crossover are closed operations exactly as they are in
DNA. Any mutation yields an organism that is viable or dead, never malformed.

Verified: 500 random tapes all decode and run; 500 successive replications hold
the reading frame; crossover of any two genomes yields a runnable offspring.

### Instruction set

Registers hold **Glyphs** — 10,000-bit hypervectors — so an organism computes
*in* the representation rather than about it.

| op | effect |
|---|---|
| `copy` | `dst = a` |
| `bind` | `dst = bind(a, b)` — XOR, self-inverse, commutative |
| `bundle` | `dst = bundle({a, b})` — majority vote |
| `perm` | `dst = permute(a, b - 128)` — the only directional primitive |
| `role` | `dst = bind(a, ROLE[b])` — one of 256 fixed role vectors |
| `and`, `or` | bitwise |
| `clean` | nearest item in the cleanup memory |
| `assoc` | **sense:** the b-th associate, in the environment graph, of the item nearest `a` |
| `neigh` | **sense:** bundle of the top `(b mod 8)+1` associates of that item |

Four registers, not eight. A one-instruction solution must name the right opcode
*and* the right destination *and* the right source, so the register count enters
the odds of a blind hit squared. This is a search-budget decision, not a claim
about capacity.

`ROLE[k]` is fixed for the life of the process and identical in every organism,
so a role index means the same thing in every genome. Without that, crossover
would splice in a symbol with a different referent and heredity would be
meaningless.

### Containment — by construction, not by Bulwark

Fixed register file, no memory addressing, no I/O, no loops, instruction ceiling
set by tape length. The VM cannot reach anything outside itself.

This is stronger than a sandbox, and it is why this stage **does not use Bulwark
at all**. Bulwark becomes necessary at the stage where organisms call Carapace
tools and touch the world. Claiming it now would be claiming a safeguard that is
not doing any work.

### The chamber

A fixed population under continuous replacement in the PACE sense: one chamber,
one fixed volume, organisms displaced by better ones as they arrive rather than
at a generation boundary. Nothing is culled on age.

Fitness is evaluated on a **resampled** subset each round, shared by every
organism scored in that round. Resampling is not only a speed measure — scoring
everyone on one fixed set rewards operators that suit *that set*, and the
champion is re-scored on the full training set before it may replace the
incumbent, because keeping the luckiest sample rather than the best organism is
how a sampled fitness quietly becomes a random search.

---

## The four pillars, mapped honestly

| pillar | what is actually built |
|---|---|
| DNA data storage | the genome is a linear byte tape with a total decoder; mutation and crossover are closed |
| Biocomputing | registers hold hypervectors; opcodes are Khora's own tissue operations |
| Synthetic genomics | replication copies the tape with per-byte error, plus whole-codon indels so programs can change length |
| Directed evolution | continuous displacement under a fixed budget; failure is starvation, not a cull |

---

## Three findings that redesigned it

Each was found by measuring, and each is kept as a test rather than quietly
fixed.

### 1. A closed instruction set cannot find anything — the landscape is flat

The first version had no senses: bind, bundle, permute, cleanup, 256 role
vectors. On a relation expressible in **one instruction** — `to = bind(from,
ROLE[57])` — selection found nothing. 2,048 births, **0.000** of a possible 1.0.

Not a tuning problem. In a 10,000-bit XOR space every wrong role is orthogonal
to the right one, so wrong-by-a-hair and wrong-entirely both score zero. The
landscape is perfectly flat with one invisible needle, and no amount of pressure
climbs a flat surface.

**That generalises past the synthetic case, and it is the part that matters:** a
program over random atomic hypervectors cannot express a semantic relation,
because the atoms carry no structure. Two words that mean similar things have
orthogonal glyphs. The relation lives in the **graph**, not in the vectors.

So the organism was given senses. Same search, same 2,048 births, on a relation
it can sense: **1.000** on held-out pairs.

### 2. Similarity is the wrong fitness; accuracy over a set is the right one

Mean similarity to the target is flat for the same reason. Cleanup **accuracy**
over a set of pairs is graded: an operator that carries three pairs out of twenty
scores 0.15, and that is a foothold. Partial credit comes from being right about
part of the world rather than from closeness in a space that has none.

### 3. Per-pair accuracy has a degenerate optimum, and selection finds it

On WordNet hypernymy over 150 categories, *"always answer `person`"* scores
**5.65%**, and any honest operator scores less than that in its first
generations. Selection climbed the constant hill and stayed there: the champion
produced **one distinct answer across 1,133 held-out inputs**, and its output
register was never written from its input at all.

A penalty term would be the wrong fix — a constant would still be a local
optimum with a moat around it. Averaging accuracy over **target classes** rather
than over pairs removes the optimum entirely: a constant is right for one class
out of every class it faces, so it scores `1/k` instead of the size of the
largest class. The deceptive peak is not penalised, it is levelled.

Two harness defects were found alongside it, and both were mine:

- **The majority-class baseline was missing from the bench.** Its absence turned
  a degenerate constant into something that read like a discovery. Chance was not
  the relevant dumb baseline.
- **The environment was starved.** Restricting Plexus adjacency to the codebook
  *after* taking the top 16 associates left 1.4 edges per word and 47% of words
  with none. The organism had nothing to sense, so the senses were never under
  test.

---

## The assay

**Environment:** Plexus — a PPMI co-occurrence graph over the corpus. The world
model Khora actually has.

**Ground truth:** WordNet, which the corpus never saw. Two relations, and the
contrast between them is the point:

- **Hypernym** — member → its category (`sparrow` → `bird`). Vertical. There is
  no reason a co-occurrence graph should encode it.
- **Co-hyponym** — member → a sibling in the same category. Horizontal, and the
  one distributional structure is actually supposed to carry.

A win on the horizontal relation and a loss on the vertical one would not be a
mixed result. It would be the method correctly reporting which relations are
present in the environment it was given.

**Split by member**, so held-out words were never selected against.

**Baselines** — and the last two are the ones that matter:

| baseline | what it is |
|---|---|
| chance | `1 / |codebook|` |
| identity | output = input |
| top Plexus associate | thirty lines, no search |
| **majority class** | always answer the commonest target |
| **VSA role vector** | `r = bundle(bind(from_i, to_i))`, predict `cleanup(bind(from, r))` — *the* textbook answer, same training pairs, free to compute |

If evolution cannot beat the role vector on held-out words, this organ is
ceremony and the module says so.

---

## Results

Recorded in `CHANGELOG.md` and reproduced by:

```bash
build/bin/ribosome_bench data/plexus_archive/main data/eval/wn_categories.tsv 150 60
```

**Status at last measurement: Ribosome does not beat the baselines on real
data, on either relation, once the metric is correct.**

| predictor | hypernym | co-hyponym (one sibling) | co-hyponym (same category) |
|---|---|---|---|
| chance | 0.021% | 0.021% | — |
| majority class | **5.649%** | 0.000% | — |
| top Plexus associate | 0.794% | 0.177% | **3.44%** |
| second-order kin (alone) | 0.177% | 0.000% | 2.91% |
| VSA role vector (textbook) | 0.000% | 0.000% | — |
| **Ribosome (evolved)** | 0.706% | 0.353% | 3.18% |

The last column is the one that counts, and it is the column I had to add after
noticing my own assay understated the task: co-hyponymy is a **set-valued**
relation — any sibling is a correct answer — but the scored target was one fixed
sibling, so returning a *different* correct sibling counted as a miss. On the
correctly specified measure the evolved operator scores **3.18% against a
one-line baseline's 3.44%**. The earlier 0.353%-vs-0.177% "win" was an artifact
of the over-strict target, and it should not have been reported as a win.

Two further negatives from the same run:

- **The added primitives contributed nothing.** `Kin` alone scores 0.000% on
  co-hyponymy, and no champion uses it. The winning genome's entire live body is
  still one instruction, `neigh r0 <- bundle top 3 of r0` — evolution
  rediscovered the trivial baseline and stopped.
- **Hypernymy is confirmed absent, not merely hard.** Adding the primitives moved
  the champion off the constant (436 distinct answers, so it is now a real
  function of its input) and accuracy *fell*, from 1.677% to 0.706%. Under
  class-balanced fitness a constant scores 1/k = 0.667% and the champion scored
  0.793%. There is nothing above the constant floor in a co-occurrence graph.

That is the current state and it is not being dressed as anything else.

---

## The audit: an agent attacked this module and found nine defects

Rather than defend the negative results above, I dispatched an adversarial agent
to find out whether the SEARCH was broken. It came back with an exhaustive scan
of all 384 one-instruction programs and a verdict worth more than anything this
module had produced: **the selection machinery is sound, the objective was
wrong, and the reported numbers were noise.**

### The reported numbers were noise

Held-out is 1,133 pairs. "3.18% versus 3.44%" is **36 correct answers against
39** — two-proportion z = 0.35, p = 0.72. The earlier withdrawn win of 0.353
against 0.177 was **4 answers against 2**, p = 0.41. Nothing this bench printed
was distinguishable from anything else it printed, and three separate wrong
conclusions survived because a point estimate was never shown with its interval.

Every rate now carries `hits/n` and a 95% Wilson interval, and the verdict line
says INSIDE THE NOISE when a z-test cannot separate the leader from the
runner-up.

### The objective was anti-correlated with the reported metric

The chamber selected on one designated sibling while the bench reported
same-category accuracy. Over the range that matters those disagree: the
instruction the chamber preferred scores 1.62% on the reported metric; the one
it rejected scores 3.36%. The champion was **rank 1 of 384** on the target it was
given. Selection never failed — it converged on the global optimum of a
badly-specified fitness.

### The "corrected" metric was also won by a constant — the same trap, twice

Same-category accuracy has its own majority-class optimum at 3.97%, beating both
the champion and the top-associate baseline, and the top scorers on it are all
constants reached through the fixed initial registers. A majority-class row was
printed beside the exact metric, where it reads 0.000% and looks harmless, and
nothing was printed beside the metric that actually had the problem.

### Six more, all confirmed

| defect | effect |
|---|---|
| `sample = 96` | 294 of 300 organisms score zero hits, so fitness is a 1e-6 tiebreak of random sign and the tournament is a coin flip. Separating the difference at 2σ needs ~34,000 samples |
| `Assoc` ill-conditioned | `b % degree`, degree 0–32, so one codon meant a different rank per word — no stable semantics. Only `b==0` meant "top associate" for all words: 0.027 expected copies in a starting population |
| `Common` duplicated `Assoc[0]` | returned the first element of a's list found in b's, so `common(i,i) == links(i)[0]`, bit-identical |
| opcode modulo bias | 256 % 13 = 9, so opcodes 9–12 got 19/256 — and 9–12 are exactly the four with a gradient |
| no elitism | the champion was never re-inserted into the breeding population and could be bred out, surviving only as a reporting artefact |
| performance | 88% of `Vm::run` was regenerating constant registers and re-decoding the tape; the champion admission test triggered 411,012 extra VM runs per round against the 28,800 needed to score the population |

### And the deepest finding, which no fix addressed

Over 200,000 random 5-codon genomes: mean **1.309** live instructions, **27%**
pure identity, only 53% producing output that depends on the input at all. The
behaviourally distinct space was roughly **384 programs** — enumerable
exhaustively in seconds, which makes calling the search "evolution" dishonest.

The response was to change the machine rather than bribe the fitness: **the
output is the last register written**, not a fixed `r0`. That makes the final
instruction live by construction and propagates liveness backwards through its
sources. No prior toward composition is added; the same genomes simply stop
throwing three quarters of their work away. Measured:

| | fixed `r0` | last written |
|---|---|---|
| mean live instructions | 1.309 | **2.241** |
| pure-identity genomes | 27% | **0.0%** |

---

## The honest gap: the objective is still supplied from outside

The claim that makes this organ interesting is that **the system decides for
itself what is worth building**. That is not yet true. Fitness here is accuracy
against WordNet — an external, human-curated ground truth. Strip the framing and
this is supervised program search with a hand-supplied target.

That is a real limitation and it is the difference between a good component and
the thing this organ is supposed to be. Genetic programming is 1992. What would
not be 1992 is a fitness function derived from the system's **own epistemic
state**, with no external answer key anywhere in the loop.

Khora already has the parts:

- `ContextTree::depth_signal()` reports, calibrated by construction, how deep a
  regularity a passage let it use — a direct measure of *how well do I know this
  territory*.
- `Plexus` is a structure that can be added to, and whose value is testable.

So the closed loop is: **an organism proposes structure; the structure is judged
by whether adding it improves Khora's prediction of held-out text it has never
seen.** No WordNet, no human labels, no answer key — the system's own surprise is
the selection pressure, and the organisms that reduce it survive. A relation is
"true" here in the only sense the system can verify: it pays.

That is the version worth building, and it is next.

---

## What comes next, in order

1. **Multi-hop senses.** One `assoc` is one edge. Hypernymy, if it is in the
   graph at all, is a *pattern over neighbourhoods* — the category is the word
   many co-hyponyms share. Nothing in the current instruction set can express an
   intersection over a neighbourhood, which may simply mean the target relation
   is outside the machine's reach rather than outside the data.
2. **A relation known to be in the environment.** Before concluding the method
   fails, run it on something Plexus demonstrably contains, so a loss separates
   "the search is weak" from "the signal is absent".
3. **Self-supervised fitness**, per the section above. This is the one that
   changes what the organ *is* rather than how well it scores.
4. **Bulwark, when it is load-bearing.** Organisms that call Carapace tools need
   real containment; until then the sandbox claim stays unmade.
