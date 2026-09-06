# Case study: the "procstates" 16-bit automaton (Unesty/Doing)

Task: model in MiniZinc and solve with psolve the functional-graph orbit
question from `Unesty/Doing/attempts/procstates`, where existing MiniZinc
solvers were reported to fail.  This document pins down the semantics, the
ground truth, the model, and the measured baselines.

## 1. The machine (exact semantics)

A state is a `u16` = four nibbles.  Read as instructions, nibble k =
`(op_k:u2, arg_k:u2)` with opcodes `0=put, 1=loop, 2=inc, 3=dec` and a 2-bit
memory argument.  Read as memory, nibble k is cell `v_k:u4`.  One `execute()`
step (`2bitjmpproc.zig`) maps a state to a state; it is a pure function, and
its subtleties are the whole puzzle:

* all memory READS see the **original** state, all writes go to a copy —
  an `inc` and a later write to the same cell in one pass do **not**
  accumulate, last write wins;
* `loop` never acts inside a pass; after each full 4-slot pass, counters
  decide whether another pass runs.  Counter `J_i = (arg_i+1) · J_{i-1} · … · J_0`
  — so a loop is **inert unless every earlier slot is also a loop**;
  total passes `= 1 + J_0 + J_1 + J_2 + J_3` (up to 65813);
* `put` toggles a load/store flip-flop that **persists across passes**
  within one step: `put` loads `accum = v[arg]`, the next `put` stores
  `v2[arg] = accum`, alternating (an odd number of `put`s makes even and
  odd passes behave differently).

`main.zig`'s `longestPath` then asks: over all 65536 states, what is the
longest walk before the first repeat (functional-graph orbit length =
tail-to-first-entry + cycle length)?  (Yes — the shipping program also
starts with `std.time.sleep(1000000000000000)`, ~11.5 days, before doing
anything.)

## 2. Ground truth (independently verified)

* `tools/procstates_ref.c` — byte-exact C port of `execute()`;
* `tools/procstates_ref.py` — an independent nibble-wise mirror with seven
  hand-derived unit states (caps off-by-one traps: the put flip-flop parity,
  the non-accumulating incs, the inert non-prefix loops, ...);
* the two implementations are compared over **all 65536 states** (mutual
  full-table check passes).

On this transition function:

```
states=65536  components=33710
max orbit = 44   (e.g. start 51641: tail 42 + 2-cycle entering 15679)
cycle census: len1 x2646, len2 x4392, len3 x23, len4 x14, len5 x15,
              len6 x9, len8 x6, len10 x3, len16 x1
```

## 3. The models

* **`examples/procstates/procstates_orbit.mzn`** — the clean model: the
  transition table `next[]` is *data* (compiled by the verified reference
  into `procstates_next.dzn`; re-encoding the VM stepwise would unroll a
  data-dependent number of passes — up to 65813 — that is the formulation
  where nothing survives), and the question is a single global constraint

  ```
  predicate orbit_len(array[int] of int: nxt, var int: start, var int: len);
  var 0..N-1: start;  var 1..N: len;
  constraint orbit_len(next, start, len);
  solve maximize len;
  ```

  `orbit_len(nxt,s,l)` ⇔ `l` = #distinct states visited by `s, nxt[s],
  nxt²[s], …` before its first repeat.  psolve implements the predicate
  natively (FlatZinc builtin, CP engine); with any other solver MiniZinc
  cannot even flatten a call (declaration-only predicates pass through only
  to the solver that owns them).

* **`examples/procstates/procstates_orbit_stock.mzn`** — the same task in
  portable MiniZinc for stock solvers: bounded-horizon unrolling with
  reified distinct-prefix indicators (sound for windows `H > orbit`).  This
  is the encoding where generic solvers grind; data `stock_h64.dzn` (H=64).

## 4. The psolve solver work

Native `orbit_len` global in the CP engine (`src/fz_cp.inc`, `CPK_ORBIT`):

* parse: constant index table, all entries in 0..n−1, n ≤ 4 Mi states;
  anything else declines the whole model (honest UNKNOWN, never a lie);
* propagation: start-domain ∩ [0, n−1]; if start is fixed, len collapses to
  the exact orbit; if len is fixed, the start domain is filtered to the
  exact attainers; otherwise sound min/max orbit bounds over the start
  domain (this drives the B&B objective);
* the analysis is the textbook linear-time functional-graph depth
  computation, **lazy and memoized per solve**: `depth[]` are proven facts
  independent of the search state, hence shared across every branch of the
  DFS with no trail handling.  Total root cost is O(n); afterwards every
  propagation reads warm facts;
* leaf verification rechecks `len == orbit(start)` exactly before any
  answer prints (the project's standing rule).

Result on the real instance: **`start = 51641, len = 44`, optimum PROVEN
(`==========`) in ~0.14 s** — and the printed start is one of the exact
witnesses from the independent C reference.  Any orbit-45 attempt is ruled
out at the root by the global's max bound, not by enumeration luck.

## 5. Measured baselines

Reproduce: `tools/procstates_baseline.sh [LIMIT_MS]` (needs the MiniZinc
bundle with gecode/chuffed/cp-sat on PATH).

Measured 2026-08-16 on a 2-vCPU / 2-GB sandbox with the MiniZincIDE 2.9.4
bundle, 600 s time limit per solver, solving the *maximize* problem on the
real instance through the portable H=64 encoding (`stock_h64.dzn`).
Environment quirks found and worked around honestly: this bundle's stdlib
**emits `gecode_int_element` for element rewrites even when flattening for
chuffed and cp-sat**, and neither bundle fzn executable registers that
predicate (`fzn-chuffed`: "Registry: Constraint gecode_int_element not
found"; `fzn-cp-sat`: "Not supported gecode_int_element" then aborts).  For
those two the flattened file was textually rewritten to equivalent
`array_int_element` + shift rows before running (this is what
`tools/procstates_baseline.sh` documents; `fzn-cp-sat`
additionally needs the bundle `lib/` on `LD_LIBRARY_PATH` and constraints
before the solve item).

| solver | encoding attempted | outcome |
|---|---|---|
| Gecode 6.3.0 | stdlib flattening (accepts `gecode_int_element`) | **no solution at the 600 s limit** (`=====UNKNOWN=====`, nSolutions=0; flattening itself 1.3 s, 5.3 MB fzn) |
| Chuffed | same, after the element rewrite | **OOM-killed during model load** (reproducible in this 2-GB box, killed within ~15 s, no solution) |
| OR-Tools CP-SAT | same, after the element rewrite | **OOM-killed mid-search** (~810 MB RSS and climbing, reaped within ~3 min, no solution) |
| **psolve** | native `orbit_len` global, `procstates_orbit.fzn` | **optimum 44 PROVEN in 0.14 s** (65578 search nodes, `%mzn-stat: objectiveBound=44`, 13 MB peak RSS) |

The machine-level takeaway: any encoding that materializes the
65536-entry transition table plus an unrolled walk window is already
hungry/heavy for general-purpose backends in a small sandbox, and the
unbounded-window question (the actual "how long can orbits get?") is the
part search-based encodings cannot close.  psolve's global answers the
unbounded question natively: the orbit lengths are *computed*, not
searched, so the optimum proof costs one linear-time graph analysis.

Note the honest framing: this does not prove no encoding exists for which
some stock solver succeeds; it quantifies that the natural declarative
encodings grind, while a solver with a native orbit global terminates with
a *certified* optimum immediately.

### 5.1 Addendum 2026-08-18: the stock encoding now solves — via auto-detection

Phase 6.9 (`docs/FUNCTIONAL_GRAPH.md`) generalized the task-specific global
into a constraint family plus a **presolve structure-recovery detector**.
The detector recognizes the exact "H-step walk + prefix-distinct count"
lattice the MiniZinc flattener emits for the stock
`procstates_orbit_stock.mzn` encoding (element chain over one constant
table, CSE-shared reified pair disequalities, `array_bool_and` prefix
bools, `dst_1`-folded objective sum) and rewrites it — semantics preserved
exactly — into one `orbit_len_capped(next, H, start, len)` record on the
same shared digest the dedicated model uses.

Measured on the same box: the **stock flattened model** (n=65536, H=64,
the one in the §5 table) is now auto-detected (one
`presolve: orbit_chain rewrite: table_n=65536 chain_H=64` trace line) and
solved to the **proven optimum 44 in 1.76–1.89 s at 103 MB peak RSS** —
versus Gecode's 600 s UNKNOWN and the Chuffed/CP-SAT OOM kills in the §5
table.  No model change was needed; the gain comes entirely from
recognizing the structure after flattening.  Four memory cliffs had to be
fixed for this to finish (count-then-allocate propagator keep-lists,
dead-var domain skip, search-var-only branching, mutation-counter
fixpoint); each is measured and documented in `docs/FUNCTIONAL_GRAPH.md`
§4, and the one honesty regression the first cut introduced (unconstrained
*output* vars printing as fabricated 0s — caught by the phase-6.7 output
gate) is documented there in §5.

## 6. Verification

`tools/procstates_orbit_verify.py` (hard gate in `test.sh`) checks

* the real instance: printed `(start, len)` against a state-by-state
  brute-force orbit analysis of `procstates_next.dzn` (the claimed optimum
  must be both attained and maximal);
* 8 handcrafted pins (cycles, tails, restricted starts, len-fixed filters,
  UNSAT, n=1, and a decline pin on an out-of-range table that must keep
  answering UNKNOWN rather than fabricating);
* 300 seeded random toy automata (n ≤ 20) in three modes (maximize optima,
  `-a` full (start,len) relations, len-fixed filter sets) against a
  pure-Python oracle.

Pre-change discrimination: without the global, every pin but the decline
pin and 60/60 fuzz cases fail (UNKNOWN instead of answers) — the test
*fails on the pre-change binary*, per project rule.
