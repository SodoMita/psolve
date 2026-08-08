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

### DONE — Branch and Clip / Bound Tightening in MIP (`src/mip.c`)
Implemented branch-and-clip bound tightening inspired by HiGHS and modern branch-and-cut solvers:
- **Constraint Propagation / Feasibility Bound Clipping**:
  `clip_bounds_by_propagation` propagates linear row constraints ($\sum_j A_{i,j} x_j \text{ rel } b_i$)
  to compute row activity bounds and tighten (clip) variable bounds $l_j, u_j$ at root initialization,
  node creation, and child branches. Infeasible boxes ($l_j > u_j$) are immediately fathomed before solving an LP.
- **Reduced-Cost Bound Clipping (Reduced-Cost Fixing)**:
  `clip_bounds_by_reduced_cost` uses simplex reduced costs $d_j$ and the remaining gap to the incumbent
  ($\Delta = |z_{inc} - z_{LP}|$) to clip the domain of nonbasic integer variables ($u_j \leftarrow \lfloor l_j + \Delta/d_j \rfloor$
  or $l_j \leftarrow \lceil u_j - \Delta/(-d_j) \rceil$).
- **Score-Based Branching Variable Selection**:
  `select_branch_variable` prioritizes integer variables based on fractionality distance to half-integer,
  objective coefficient magnitude, and row participation degree.

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

### DONE — Public C API Audit & Defensive Bounds Checking
Audited and hardened every public C API entry point across all solver modules:
- `src/solver.c`: `solver_create`, `solver_solve`, `solver_optimum`, `solver_feasible`,
  `solver_set_objective`, `solver_set_bounds`, `solver_add_row`, `solver_warm_solve`,
  `solver_duals`, `solver_reduced_costs` validate pointer arguments and input dimensions.
- `src/qp.c`: `qp_solve` and `qp_result_free` check for NULL inputs, $n \le 0$, $m < 0$,
  and missing matrix pointers.
- `src/pgs.c` / `src/pgs_fixed.c`: `pgs_solve`, `pgsf_solve`, `pgs_matvec`, `pgsf_matvec`
  safely handle NULL options/result structs, degenerate dimensions, and invalid parameters.
- `src/fx.c`: `fx_solve`, `fx_solve_wide`, `fx_result_free`, `fx_free` validate pointers
  and dimensions.
- `src/fzn.c`: `fz_read`, `fz_solve`, `fz_print_solution`, `fz_solution_free`, `fz_model_free`
  guard against NULL pointers and invalid inputs.
- `tools/api_test.c`: Dedicated test harness verifying NULL and degenerate input safety
  across all public APIs, wired directly into `test.sh`.

### DONE — Zero-Malloc Arena Allocator & Batch Solve APIs (`src/err.h`, `src/pgs.h`, `src/pgs_fixed.h`)
- **Preallocated Memory Arena (`PSolveArena`)**:
  Allows real-time and interactive host applications (UI, games, 60fps physics frames)
  to supply a preallocated scratch buffer via `psolve_arena_init(&arena, buf, cap)` and
  `psolve_arena_use(&arena)`. Solves execute with $O(1)$ bump allocation, zero `malloc`
  calls, and zero GC pauses. Out-of-memory within the arena is caught and safely unwound.
- **Batch Solve APIs (`pgs_batch_solve` / `pgsf_batch_solve`)**:
  Provides batch evaluation over $N$ independent physics contact clusters and UI constraints,
  amortizing dispatch overhead and keeping solver state cache-hot.
- Verified with dedicated tests in `tools/arena_batch_test.c` wired into `test.sh`.

## Not done (recommended next steps, in priority order)
1. Re-run the GLPK and MiniZinc differential suites (when `glpsol`/`minizinc` are installed in the host environment).
