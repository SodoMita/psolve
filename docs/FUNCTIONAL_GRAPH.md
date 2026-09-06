# Functional-graph constraint family & the presolve structure-recovery detector

Date: 2026-08-18 — branch `arena/cp-engine-correctness`, roadmap phase 6.9.

A *functional graph* is a map `next : [0,n) → [0,n)` — each state has
exactly one successor.  Every trajectory `s, next[s], next[next[s]], …`
decomposes into a **transient tail** of length `t ≥ 0` followed by a
**cycle** of length `c ≥ 1`; the **orbit length** is `t + c`, the number of
distinct states visited.  This file documents

1. the constraint **`family`** built on one shared per-table digest,
2. the **`presolve` orbit-chain detector** that recovers this structure
   from a stock MiniZinc encoding and rewrites it to the same constraint,
   recovering semantics exactly (never narrowing, never widening),
3. the **engine-level** changes the work forced (each found by
   measurement, not by reading),
4. the measured behaviour, incl. the four memory cliffs.

---

## 1. The family

All members share one digest per transition table (§2).  The digest is
built by walking the functional graph once, O(n) states, and memoizes, for
every state `s`: `tr[s]` (transient length), `cl[s]` (cycle length of the
cycle `s` reaches), `orb[s] = tr[s] + cl[s]` (number of distinct states on
the trajectory).  Invariant `orb ≡ tr + cl` is asserted in-model by the
fuzz gate.

| FlatZinc predicate | arity | semantics (honest; decline = UNKNOWN, never a guess) |
|---|---|---|
| `orbit_len(next, s, l)` | table, state var, int var | `l = orb[s]` |
| `orbit_transient(next, s, t)` | table, state var, int var | `t = tr[s]` |
| `orbit_cycle_len(next, s, c)` | table, state var, int var | `c = cl[s]` (cycle length of the cycle the trajectory reaches) |
| `orbit_on_cycle(next, s, b)` | table, state var, bool var | `b ↔ (tr[s] = 0)`, i.e. `s` lies on a cycle |
| `orbit_len_capped(next, cap, s, l)` | table, **const** cap, state var, int var | `l = min(orb[s], cap)`; declines when `cap > n` (cap beyond the state space is meaningless here — the honest path is `orbit_len`) |
| `array_bool_and(as, b)` | bool array, bool var | `b ↔ ∧ as` (added alongside: stock MiniZinc models emit it and psolve previously declined them UNKNOWN regardless of any detector) |

Values in `next` are **0-based** state ids in `[0,n)` (matching the
upstream procstates table and the flattener's 1-based-index-shifted
element lowering); anything else declines.  Tables larger than
`CP_ORBIT_MAXN` decline.

Propagators are *sustaining* filters: bounds/value pruning from the
memoized facts inside current domains; they never assert a fact the
table walk did not prove.  Implementation note (measured, §4): candidate
keep-lists are built **two-pass, count-then-allocate**, with a no-op
guard when the surviving set is identical to the current domain.

## 2. The shared digest pool

`FZOrbit` digests are search-independent facts, so one digest is shared
by every constraint record whose table is **content-identical**
(`nset` equal + `memcmp` of the entries).  The first record owns the
allocation (`aux_owned`), later records borrow it; teardown frees only
the owner.  The per-state walker `fg_facts` fills `tr/cl/orb` lazily and
is memoized, so a model with `H` orbit constraints over one table pays
one graph analysis, not `H`.

`fg_fact(o, s, mode, cap)` exposes the four readings
(orbit / transient / cycle_len / min(orbit,cap)) behind one entry point.
Leaf verification re-checks every emitted solution against the digest
directly, per record kind.

## 3. The presolve orbit-chain detector

### 3.1 What it recognizes

The natural declarative MiniZinc encoding of "walk `H` steps from `x0`,
then count the prefix of distinct states":

```
x_{i+1} = next[x_i]                         -- i = 0..H-1 (array_int_element
                                            --   with 1-based shift s = x+1)
p_{i,j} ↔ (x_i ≠ x_j)                       -- reified pair bools, CSE-shared
A_k     ↔ ∧_{0≤i<j≤k-1} p_{i,j}             -- array_bool_and prefixes, k = 3..H
dst_k   = bool2int(A_k)                     -- k = 3..H;  k=2: direct from p_{0,1}
len     = 1 + Σ_{k=2..H} dst_k              -- dst_1 ≡ 1 folded as the constant
```

Because prefix-distinctness is monotone, this computes **exactly**
`len = min(orb[x0], H)` — the identity the rewrite installs as one
`orbit_len_capped(next, H, x0, len)` record.  The declared domain of
`len` (canonically `var 1..n`) never cuts: `orb ≤ n` always.

### 3.2 Exactness rules (why detection can never answer wrongly)

The rewrite fires only when **every** link of the lattice is present and
verified against the constant table, and every intermediate is provably
dead afterwards:

* shifts match `s = x + 1` in either polarity; element records share the
  byte-identical constant table; pair bools match `int_lin_ne_reif`
  `[±1,∓1]` rhs 0 over the exact pair `(x_i, x_j)`; each `A_k` matches the
  exact prefix-pair *set* (`k(k-1)/2` members, no duplicates, no extras);
  the sum row matches `len − Σdst = 1` or its full negation — the two
  shapes are told apart by the **rhs sign** (they coincide in plus/minus
  counts at `H=2`; picking the sign wrong was a real bug caught by the
  mutation suite);
* every consumed record is confirmed; every intermediate variable
  (`x_1..x_H`, shift vars, pair bools, AND bools, dst ints) is referenced
  by **no** unconsumed record, is **not** output-pinned, and is **not** in
  the objective — otherwise the detector stays silent;
* the `referenced[]` bookkeeping is recomputed from the surviving records
  after the rewrite (memset + remark), so the invariant the brancher
  relies on is airtight by construction.

On any mismatch the model is left untouched and the generic engine
answers it — detection changes *how fast* the answer is found, never
*what* the answer is.  This is the hard rule for all detection work in
this codebase: **detection never narrows semantics; on doubt, decline
loudly** (solve on, answer UNKNOWN if the instance is genuinely hard).

Multiple independent chains over the same table are recovered one by one
(each is validated against the records the others did not consume).
Trace: `PSOLVE_TRACE_PRESOLVE=1` prints one `presolve: orbit_chain
rewrite: table_n=… chain_H=… start_var=… len_var=…` line per rewrite on
stderr.

### 3.3 Verification

`tools/orbit_detect_verify.py` (hard gate in `test.sh`) emits mutated
copies of the canonical lattice and checks both directions:

* **positives** — plain maximize, minimize, two chains over one table
  with joint objective `len1+len2`, and satisfy `-a` with `(x0,len)`
  output projection: the rewrite must fire (exact count) and the printed
  answer must equal an independent Python evaluation of the *emitted*
  model (orbit facts + model semantics evaluated without any solver);
* **negatives** — extra pin on an intermediate, one AND pair removed,
  mutated reif rhs, mutated sum rhs, mixed tables across the chain,
  broken shift chain, output-pinned intermediate, intermediate in the
  objective: the detector must stay silent and the generic engine must
  still produce the reference answer.

Calibration (required by project rule: the test must fail on the
pre-change binary): pre-change `WRONG=16/40` at seed 20260818 (positives
decline UNKNOWN or grind past the 60 s per-case cap — the binary also
lacks `array_bool_and` entirely); post-change `WRONG=0/100`.

## 4. Engine-level changes this forced (each found by measurement)

Making the *stock 65536-state, H=64 encoding* finish exposed four
independent memory cliffs, plus one honesty regression the first cut
introduced.  Each was found by profiling/OOM-hunting, in this order:

1. **Realloc-per-entry keep lists** (first cut of the new propagators):
   quadratic realloc churn — 1.3 GB RSS at n=16384.  Fixed: two-pass
   count-then-allocate + no-op identical-set guards in
   `cp_prop_orbit_gen`, `cp_prop_oncycle`, `cp_prop_and`.  (Older
   propagators around `src/fz_cp.inc` retain the realloc-per-entry
   pattern; logged in AUDIT (10) as a measured perf risk, not blocking.)
2. **Dead-var domain materialization**: initial domains were built for
   every declared interval ≤ `CP_MAXVALS` — the H=24/n=16384 intermediate
   chain alone meant 803,479 materialized values that no constraint
   reads.  Fixed: only *search* vars (§5) get materialized domains; the
   empty-declared-domain → UNSAT contract is preserved for skipped vars.
3. **Branching over dead vars**: the brancher enumerated every bounded
   var incl. unreferenced intermediates — ~800k nodes × MB-scale
   `cp_copy` per child, linear 600 MB/s RSS growth.  Fixed: branch over
   search vars only.
4. **`cp_total` fixpoint**: the old convergence test summed total domain
   sizes per fixpoint pass per node — O(total values) where O(nodes)
   passes happen.  Replaced by a `long mutations` counter bumped by the
   domain mutators **only on actual change** (`cp_set_vals` same-content
   check after its restrict-intersect stage; `cp_intersect` bounded-path
   no-change check, unbounded-path lo/hi compare + materialization
   change; `cp_remove_val`/`cp_assign` change-only).  Convergence proof
   obligation: every write path bumps — audited (see AUDIT (10)).

Plus a latent gap closed en route: **`nv == 0` satisfy models never
reached the CP engine** (`if(nv>0)` gate in `src/fzn.c`); they are legal
FlatZinc (all-constant models, e.g. pinned lattice probes).  Now
dispatched (`nv>=0`), with `opt.bestx` allocated as `(nv?nv:1)` and the
optimize-only exclusion gate kept.

## 5. What the brancher searches: `searchme = referenced ∪ output`

The first version of fix 3 branched over *referenced* vars only.  The
output-layer fuzz gate (`tools/fzn_output_check.py`, phase 6.7)
immediately flagged **`WRONG=59/2000`**: an *unconstrained but declared
output* var was never branched, stayed unfixed, and printed as `0` —
which can lie **outside its declared domain** (the exact fabrication
class that gate exists to catch); `-a` enumeration also collapsed
(free output vars contribute a cartesian factor).

Fix: the branch predicate is `searchme[v] = referenced[v] OR
output-pinned[v]`.  Output vars are always enumerated (they are printed,
and they count); rewrite-exposed chain intermediates are neither
referenced nor output (the detector refuses output-pinned
intermediates), so they stay out of the brancher — the memory win of
fix 3 is preserved exactly where it was needed.  A searchable var left
unfixed at a leaf (unbounded interval) is an honest decline (`UNKNOWN`),
never a printed guess.

## 6. Measured results

2-vCPU / 2-GB sandbox, MiniZincIDE 2.9.4 bundle, 2026-08-18.
Baselines from `docs/PROCSTATES.md` §5 (same box, 2026-08-16).

| model | engine path | result | wall | peak RSS |
|---|---|---|---|---|
| procstates native `orbit_len` (n=65536, H≤64 window) | dedicated global (phase prior) | optimum **44 proven** (`objectiveBound=44`) | 0.13–0.14 s | 13 MB |
| **procstates stock encoding** (`minizinc --solver gecode`-style flattening; n=65536, H=64) | **detector rewrite → same engine** | optimum **44 proven** (`objective = objectiveBound = 44`) | **1.76–1.89 s** | **103 MB** |
| same stock encoding | Gecode 6.3.0 | no solution at 600 s (`UNKNOWN`, nSolutions=0) | > 600 s | — |
| same stock encoding | Chuffed | OOM-killed during model load (~15 s) | — | > 2 GB |
| same stock encoding | OR-Tools CP-SAT | OOM-killed mid-search (~810 MB climbing) | — | — |
| toy lattice pins (n≤12, H≤8), 100-case mutation suite | detector + engine | all answers equal Python oracle, `WRONG=0` | < 9 s total | — |

Honest framing: the detector recovers one specific, common modelling
pattern exactly; it does not make arbitrary orbit-shaped models fast.
On anything it does not recognize, psolve's behaviour is unchanged
(decline to the generic engine; honest UNKNOWN if that is too weak).

## 7. Evidence roll-up (acceptance)

* new hard gates in `test.sh`: `tools/fgraph_verify.py 120 20260818`,
  `tools/orbit_detect_verify.py 60 20260818` (both proven to fail on the
  pre-change binary — calibrations above and in §3.3);
* full `./test.sh` battery green (rc=0), incl. `fzn_output_check`
  `WRONG=0/2000`, `procstates_orbit_verify` `WRONG=0/120`,
  MiniZinc differential `OK=33 FAIL=0`, 77-instance bench **0 semantic
  diffs** (two search-node telemetry counts changed: `bool_and_sat`
  1→2, `subcircuit_demo` 13→6 — expected from `array_bool_and` parsing
  and the new branch predicate);
* ASan/UBSan/LSan sweep on the sanitizer build: fgraph 60, orbit-detect
  40, procstates 60, output-check 500 — all `WRONG=0`, and the stock
  65536-state rewrite + tmpl lattice run clean with rc=0 and zero
  sanitizer reports;
* pre-existing `realloc`-pattern propagators: unchanged semantics; the
  pattern's measured cost is logged in AUDIT (10) as a future perf item.
