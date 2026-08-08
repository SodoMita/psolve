# psolve — audit & hardening notes (branch `arena/audit-hardening`)

This is a working audit document. It records what was reviewed, what was
changed on this branch, and the prioritized list of the most important things to
do next. It is intended to be updated as work progresses.

> **Update — integrated `arena/exactness-and-status-audit`.** This branch has
> been merged with the parallel correctness-hardening effort on
> `arena/exactness-and-status-audit`, which found and fixed six further classes
> of "solver lied" / crash bugs (false LP `UNBOUNDED`/`INFEASIBLE` statuses,
> silently wrong exact-rational answers from `int64` overflow, PGS `SIGFPE` on a
> zero diagonal, un-checked allocations proven by OOM-injection testing, MIP
> labelling suboptimal points `OPTIMAL`, and QP returning infeasible/non-optimal
> points as solved). See `docs/AUDIT.md` (from that branch) for the full
> write-up. The combined branch now has both the new FlatZinc handlers
> (`cumulative`, `array_int_maximum`/`array_int_minimum`) *and* the full
> correctness/OOM hardening. Conflict resolution in `src/solver.c`: kept the
> exactness branch's `build_initial_basis` + Phase-I restart logic alongside
> this branch's dense-LU retry wrapper; routed my `solver_reset_to_initial`'s
> scratch allocation through `psolve_calloc` so the OOM-injection suite passes
> (7,542 injection points, 0 failures).

## Baseline state (start of this pass)

- Clean build with the default flags (`-O3 -march=native`, hardening on,
  `-ffast-math` off). No warnings with `-Wall -Wextra`.
- Full `./test.sh` passes. Two differential suites are environment-gated:
  GLPK (`glpsol`) and MiniZinc (`minizinc`) are not installed here, so those
  are SKIPPED, not run.
- The double revised-simplex LP solver (`src/solver.c`), exact-rational
  fixed-point LP solver (`src/fx.c`), QP, MIP, PGS/PGS-fixed physics kernels,
  and the FlatZinc/MiniZinc bridge (`src/fzn.c`, `fznsolve`) are all present
  and mutually cross-checked (LP vs scipy, QP vs scipy, MIP vs brute force,
  fx vs double, table vs brute force).

## Changes on this branch

### 1. `cumulative` constraint handler (`src/fzn.c`)
New exact handler for the FlatZinc `cumulative(s[], d[], r[], b)` scheduling
constraint (also accepts `fzn_cumulative` / `gecode_cumulative`). It handles
the common, exact case where durations `d`, usages `r`, and the limit `b` are
fixed parameters and each start `s_i` is a bounded integer variable:

- For every task `i` and every integer time `t` in the scheduling horizon
  (derived from the start-variable boxes), it introduces binaries
  `a2=[s_i<=t]`, `a1=[s_i+d_i>t]`, and `active = a1 AND a2`, then constrains
  `sum_i r_i*active(i,t) <= b` at every `t`.
- The reifications reuse the existing exact `add_int_reif` (integer lattice
  +/-1, no invented epsilon). The horizon comes from real variable bounds, not
  a synthetic sentinel.
- Variable durations/usages/limits are **not** silently relaxed — they return
  UNHANDLED so the bridge reports `=====UNKNOWN=====` rather than a wrong
  answer. An over-large model is capped and returns UNKNOWN.
- Verified correct against an independent brute-force enumerator
  (`tools/cumulative_verify.py`): **0 mismatches** (SAT/UNSAT) across hundreds
  of random small instances. New examples `examples/fzn/cumulative_sat.fzn`
  and `cumulative_unsat.fzn` are wired into `test.sh`.

### 2. Sparse→dense retry in the double LP solver (`src/solver.c`)
`refactorize` already fell back to dense LU when `splu_factor` *failed*.
Auditing revealed a subtler failure: on some moderately-sparse big-M bases the
sparse LU factorizes "successfully" but is numerically unstable, so the solve
converges to an infeasible point and the solution certificate correctly returns
`SOLVE_NUMERICAL`.

Fix: `solver_solve` now detects a `SOLVE_NUMERICAL` result on the sparse path,
re-initializes the solver to its starting basis (extracted into
`solver_reset_to_initial`, re-runnable and preserving the objective and
iteration limit), and re-solves once with the robust dense LU. This converts a
would-be `NUMERICAL_FAILURE` into a certified answer and never changes the
result of a solve that already converged. Full `test.sh` still passes.

## Key findings (most important things to do)

### A. The double revised-simplex is numerically fragile on big-M MIP relaxations  — *top finding*
The single biggest issue found. The fz/MIP bridge encodes combinatorial
constraints (table, circuit, all_different, set_in, and now cumulative) as
big-M + binary models, and the resulting LP relaxations frequently fail the
double solver's own certificate: `solver_feasible()` returns false at the end
even though the LP is **provably feasible** (verified with `scipy`/HiGHS and the
exact `fxsolve`).

Evidence (cumulative relaxations): full-rank matrices, no duplicate rows, scipy
= OPTIMAL, `fxsolve` = OPTIMAL, but `lpsolve` = `NUMERICAL_FAILURE`. It is not
an objective problem (fails for zero, minimize, and perturbed objectives) and
not a sparse-only problem (fails with dense LU too on some instances).

Impact: real combinatorial FlatZinc models frequently return
`=====UNKNOWN=====` instead of a solution. This is *honest* (never a wrong
answer) and consistent with the documented "weak relaxations on combinatorial
feasibility models" limitation, but it sharply limits usefulness. The `-a`
(all solutions) CLI feature is also gated on this path being reliable.

Recommended fix (highest value): route MIP relaxations through the exact
rational `fxsolve` (`src/fx.c`) — either always for these integer LPs or as a
fallback when the double solve returns `SOLVE_NUMERICAL`. `fxsolve` provably
handles the instances the double solver cannot. This would robustify the whole
big-M/MIP path (cumulative, table/circuit at scale, `-a` enumeration).

### B. Phase I degeneracy on infeasible LPs (known, re-confirmed)
Some genuinely infeasible degenerate LPs iterate to the Phase-I iteration limit
and report `ITERATION_LIMIT` (honest) rather than `INFEASIBLE`. Not a wrong
answer, but a completeness gap. (Roadmap notes this; no change here.)

### C. Minor cleanup done / noted
- Removed a dead duplicate `-t` branch in `tools/fznsolve.c` arg parsing.
- `-Wshadow` warnings in `src/fzn.c` are benign re-declarations; left as-is.

### DONE — exact-solver fallback for MIP relaxations (was finding A)
Implemented. `src/mip.c` now cross-checks a relaxation with the fixed-point
exact-rational simplex (`src/fx.c`) whenever the double revised-simplex returns
`SOLVE_NUMERICAL` **or** a false `INFEASIBLE` — both of which it does on the
ill-conditioned big-M bases of combinatorial MIPs. `fx_from_double` /
`FX_INF_SENT` are exported; `mip_build_fxlp()` converts the double MIP + per-node
bounds into an exact FxLP. When the data are integral and the exact solve
succeeds its verdict wins; otherwise the honest double verdict is kept.

Impact: `cumulative_verify` went from **OK=145 UNKNOWN=105** to **OK=300
UNKNOWN=0 MISMATCH=0** — every previously-unresolved big-M schedule now solves
correctly, and the false-INFEASIBLE→UNSAT bug is fixed. Deterministic regression
added (`examples/fzn/cumulative_exact.fzn`). All differentials, fuzzing, and the
OOM-injection suite (7,836 points, 0 failures) stay green; ASan/UBSan/leak clean.

### DONE — CLI `-a` (all solutions) and FlatZinc constraint expansion
Implemented standard `-a` / `--all-solutions` for `fznsolve`, `fz_solve`, and `mip_solve`:
- In satisfaction problems (`solve satisfy`), `-a` traverses the branch-and-bound
  tree to enumerate and print all distinct integer satisfying assignments, each followed
  by `----------`, and terminates with `==========` when the complete space has been explored.
- In optimization problems (`solve minimize` / `solve maximize`), `-a` reports all intermediate
  strictly-improving incumbents, each followed by `----------`, and terminates with `==========`
  once optimality is proved.
- In default single-solution mode, optimization problems correctly print the proved optimum
  followed by `----------` and `==========` per the FlatZinc standard.

Expanded native FlatZinc constraint handlers:
- `array_var_int_element`, `array_bool_element`, `array_var_bool_element`,
  `array_float_element`, `array_var_float_element` (exact SOS1 and bounded indicator encodings).
- `array_float_maximum`, `array_float_minimum` (exact selector formulation for float arrays).
- `set_in_reif`, `int_in`, `int_in_reif` (exact reified integer set and range membership).
- `int_div`, `int_mod` (exact linear quotient-remainder formulation for constant divisors).
- `int_pow` (linear exponentiation for integer constants and bounded lattice domains).
- `bool_times` / `int_times` (linear boolean conjunction).
- `count_leq`, `count_geq`, `count_lt`, `count_gt`, `count_ne`, `count_neq`, `among`, `fzn_among`
  (exact reified count and among constraints).
- `table_bool`, `fzn_table_bool`, `gecode_table_bool` (extensional boolean table constraints).
- `int_min_reif`, `int_max_reif`, `int_abs_reif` (reified scalar extrema and absolute value).

All new handlers and options are verified by dedicated regression tests in `tools/fzn_semantics_test.py`.

## Continuation pass — FlatZinc handler & parser correctness (branch `arena/continue-hardening`)

A focused review of the just-landed `-a`/exact-fallback code and the
constraint handlers found **seven wrong-answer / undefined-behaviour bugs**,
all fixed and covered by a new brute-force differential verifier
(`tools/divmod_verify.py`, wired into `test.sh`):

1. **`int_div`/`int_mod` used C truncation, not MiniZinc floor division.**
   `int_div(-7,3,q)` printed `q = -2` (correct: `-3`); `int_mod(-7,3,r)`
   printed `r = -1` (correct: `2`).  Both the constant-fold path and the
   general encoding (remainder bounds admitted lattice-foreign points for
   possibly-negative dividends) were wrong; negative divisors were wrong
   even for non-negative dividends.  Now: exact sign-aware remainder bounds
   (`K>0: rem∈[0,K-1]`, `K<0: rem∈[K+1,0]`) pin `q = floor(a/K)` uniquely;
   constant folding uses floor semantics; non-integral/over-wide divisors
   are rejected as UNHANDLED.
2. **`int_pow` pinned results from inexact doubles.**  No `isfinite`/2^53
   guard: `int_pow(3, 34, z)` reported **UNSATISFIABLE** (rounded double
   vs the synthetic result box), `int_pow(3, 40, z)` likewise.  Now any
   non-finite, >2^53, or non-integral value — and any negative exponent —
   is honest UNHANDLED (UNKNOWN), with all lattice values validated before
   committing rows.
3. **`set_in`/`among` silently truncated sets.**  A 512-byte buffer and a
   256-value cap dropped values without a word: `set_in(y,{1,...,280})`
   with `y=265` printed **UNSATISFIABLE** (265 is in the set).  A shared
   parser (`fz_parse_int_set`) now grows dynamically, deduplicates
   (duplicate members double-counted `among`), and reports UNHANDLED past
   an honest cap (1024) instead of truncating.  Non-reified range sets of
   any width now clamp bounds (a >256-wide range wrongly returned UNKNOWN);
   empty ranges are infeasible, not unhandled.
4. **Singleton `set_in` overwrote the declared domain.**  `var 0..5: x;
   set_in(x, {8, 8})` printed `x = 8` — a value *outside the variable's
   own domain*.  The singleton and range paths now intersect with the
   declared domain (empty intersection = UNSAT).
5. **`count(x, y, c)` with a variable target `y`** (valid FlatZinc)
   silently counted zeros → false UNSAT.  Now UNHANDLED (UNKNOWN).
6. **False UNSATISFIABLE inside the synthetic box.**  `var int:` is
   unbounded in FlatZinc but clamped to ±1e9 by the bridge; a model whose
   only solutions live outside that box (e.g. `int_lin_eq([1],[z],2e10)`)
   was declared **UNSATISFIABLE**.  The optimization side already refused
   synthetic-bound optima; UNSAT is now likewise downgraded to UNKNOWN
   whenever the model contains a synthetically-bounded original variable.
   Bounded models keep exact UNSAT.  (In practice mzn2fzn always emits
   bounded int domains, so this mostly guards hand-written FlatZinc.)
7. **`lp_read` freed indeterminate pointers** (gcc `-Wmaybe-uninitialized`,
   a real bug): the counting-sort buffers were declared mid-function while
   every parse-error `goto err` funnels through `free()`s at the label —
   an early error skipped the initializers and freed stack garbage.  All
   six pointers are now declared NULL at function entry.

### New/extended verification this pass
- `tools/divmod_verify.py`: randomized differential vs a Python
  brute-force enumerator using exact MiniZinc semantics (floor division,
  divisor-signed remainder, deduplicated sets, derived-value domains)
  across div/mod/pow/set_in/among, satisfy+optimize, edge-biased
  instances, plus 10 fixed regressions for the bugs above.  **0 wrong
  answers across 5 seeds (1,000+ instances)**; the only UNKNOWNs are
  synthetic-boxed models, by design.
- `tools/fzn_semantics_test.py`: `-a` now asserts *distinct* solutions and
  the exact count (3 for x<y on a 3×3 lattice), negative-dividend/divisor
  div/mod pins, and the set-outside-domain UNSAT regression.
- GLPK differential finally run in this environment (`glpsol` 5.0):
  canonical sweep **119/119**, four-way differential **0 mismatches**
  (30 optimal + 33 infeasible + 57 unbounded agree).  MiniZinc remains
  environment-gated.
- Full `./test.sh` green incl. OOM injection (7,802 points, 0 failures)
  and ASan/UBSan fuzzing with the parser fix in place.

## Not done (recommended next steps, in priority order)
1. MiniZinc differential suite (`tools/mzn_diff.py`) — ready to run wherever
   `minizinc` is installed (not packaged for this host's distro).
2. Public C API audit for Phase 4 hardening — partially done: the one
   exit-style library path (`psolve_fail` with no handler installed) is now
   an explicit embedding contract in `src/err.h`.  Remaining: a bounds-check
   sweep of `LP`/`MIP` struct inputs (an embedding host must currently pass
   valid CSC arrays).
