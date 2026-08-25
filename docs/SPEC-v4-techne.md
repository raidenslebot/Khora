# SPEC v4 — Techne: the organ that writes certified code

**Status: the speed target is met; the quality target is not, and the gap is
measured rather than estimated.**

---

## Why this exists

Every earlier organ in this project starved on two properties of natural
language, both measured here rather than assumed:

- **Prose has almost no deep structure.** Across 7.66M tokens, the share of
  n-word contexts that ever recur is 66.9 / 30.7 / 13.5% at n=1/2/3 and **0.32%
  at n=8** — and it *saturates*: a 319-fold increase in corpus moved n=8 from
  0.00% to 0.32%. Distinct contexts per token at n=8 is 0.996, so new contexts
  arrive as fast as tokens do.
- **Prose has no answer key.** The differences worth arguing about came down to
  40 correct answers against 39 out of 1,133 — which no test can separate.

Code inverts both. Structure recurs, and there is an **oracle**: a test passes
or it does not. There is nothing to argue about when the metric is "does it do
what was asked, on every input".

## The contract

**Nothing is returned that is not certified.** A result carries how it was
verified:

| proof | meaning |
|---|---|
| `None` | nothing satisfied the specification — *and nothing is returned* |
| `Tested` | passes every visible case, fails at least one held-out case → **memorisation**, reported as such |
| `Generalised` | passes every visible case **and** every held-out case it was never scored on |

Held-out inputs are drawn *longer* than any visible case, so a program that
memorised a length cannot pass them. Only `Generalised` results count anywhere,
and only `Generalised` results enter the library — admitting a merely tested one
would poison every later search with a primitive that lies.

This is the opposite of a language model's contract, which is to always produce
something and never to know whether it is right.

---

## Architecture

### Total decoder, bounded execution

A program is a linear byte tape. Every byte string decodes to a running program,
so mutation and crossover need no repair rule — and a repair rule is a human
prior smuggled into the search.

There are **no loops and no branches**, only bounded combinators. That is not a
limitation grudgingly accepted: it is what makes every program terminate by
construction. A general loop would put the halting problem in the middle of a
fitness evaluation, and a step limit that kills a program mid-run makes fitness
depend on the limit rather than on the program.

Values are capped at ±1e9, which makes the arithmetic total rather than
carefully-case-analysed. The first version used hand-written saturating add and
multiply, contained undefined behaviour (negating `INT64_MIN`), and killed the
test process immediately.

### Gradient search failed, and the reason is structural

The evolutionary arm solved 13/20 and **every failure was compositional**.
`sum_of_squares` is two instructions and 20,400 candidates never found it. Not a
budget problem: a program that computes the squares and stops produces a *list*
where a *scalar* is wanted, so element-wise partial credit scores it at
essentially zero. Getting halfway is worth nothing, the surface is flat, and
selection has nothing to climb.

### Construction, deduped by behaviour

So: stop climbing, start **building**. Enumerate by size, composing what has
already been built, and dedupe by **observational equivalence** — two
expressions with identical outputs on every case are interchangeable, so the pool
is indexed by *behaviour* rather than syntax and the explosion collapses.

| arm | generalised | candidates |
|---|---|---|
| short enumeration | 12/20 | 155,802 |
| random, same budget | 12/20 | 64,587 |
| evolutionary search | 13/20 | 164,200 |
| **construction** | **18/20** | 695,697 |
| **construction + library** | **19/20** | **54,184** (−92.2%) |

### The library compounds

Certified solutions become callable primitives, seeded at level 0 where
construction can actually reach them. Held under a hard budget with
utility-based eviction — a library that only accretes becomes a haystack that
makes later searches harder, which is the same unbounded-growth failure that made
an earlier organ allocate 2.9M segments for 24k tokens.

`second_largest` exhausts a 60,000-behaviour pool with no library, and becomes a
two-node expression once `sorted_desc` is available.

### Emission

A recipe becomes real source in **C++, Python, JavaScript and Rust**, in
static-single-assignment form. Each backend defines the same operation set with
the *same semantics* as the interpreter — the value cap, the empty-list results,
the zero-guard on division, the cycling shorter operand — because emitted code
that behaves differently from the code that was certified means the certificate
is a lie.

---

## Throughput

**Target: 10,000 lines in 30 s = 333.3 lines/s of certified code.**

| arm | certified | body lines | seconds | lines/s |
|---|---|---|---|---|
| 1 thread | 1100/2000 | 2474 | 68.5 | 36.1 |
| 24 threads, shared library | 1099/2000 | 2609 | **5.79** | **450.4** |
| 24 threads, isolated libraries | 993/2000 | 2883 | 9.37 | 307.7 |

**Met at 1.4×.** Two counts were deliberately *lowered* for integrity: every
emitted function opened with `t0 = kh_id(x)`, a copy of the argument that does
nothing and inflated the figure by one line per function (3575 → 2474, about
30%); and the prelude is counted separately so a fixed operation set cannot be
re-counted per file.

### Copy-on-write beat a reader-writer lock by 10.6×

The first shared library held a `shared_mutex` read lock for the whole of
`construct` — the expensive part — so one writer stalled every worker: **76.95 s
on 24 threads against 9.36 s with no sharing at all**, i.e. 1.20× scaling,
effectively serialised. The library is now an immutable snapshot behind an atomic
pointer; readers take one atomic load and never block.

| | before | after |
|---|---|---|
| scaling | 1.20× | 12.95× |
| wall clock | 76.95 s | 5.79 s |

Sharing then wins on **both** axes, which it could not while holding the lock:
1099 certified against 993, and 873 library calls inside answers against 571.

---

## Solving to fixpoint

A single pass attempts each task once, in whatever order the queue hands it out,
so a task that is trivial once some component exists fails when it happens to
come first. That is a fact about scheduling, not about solvability.

So the pass repeats until a round certifies nothing new. **It terminates**: a
round that certifies nothing cannot be followed by one that does, because nothing
changed. An unbounded improvement loop that provably stops is the only kind that
can be run unattended.

```
newly certified per round:  1100  175  21  0
```

**55.0% → 64.8%** certified from iterating alone.

### Constraints are the quality knob

| visible cases | certified | memorised (rejected) |
|---|---|---|
| 6 | 1296 (64.8%) | 333 |
| 12 | **1404 (70.2%)** | **249** |

A program satisfying six examples can be wrong everywhere those six did not look.
Every additional case is another constraint the answer must survive, so both the
false-positive rate falls *and* the true certification rate rises.

---

## What is NOT met, stated plainly

The goal asks for perfect quality, unlimited self-improvement, every language,
and anything a human can do with a computer ×100. Against that:

| requirement | status |
|---|---|
| 10,000 lines in 30 s | **met**, 1.4× |
| 100% quality | **not met** — 70.2% certified at best measured setting |
| every language | **not met** — 4 backends, though adding one is mechanical |
| self-improvement loop | **partial** — fixpoint iteration terminates by design; `selfhost_bench` tests whether the system can rebuild its own primitives |
| anything a human can do ×100 | **not met** — the domain is list transformation |

The honest characterisation: this is a fast, verified synthesiser for a bounded
combinator language over integer lists, whose capability compounds and whose
throughput target is met. It is not general-purpose programming, and the distance
between the two is not a matter of tuning.
