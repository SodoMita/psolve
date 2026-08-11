# psolve — audit & hardening notes

> **2026-08-11 (2) — constant/affine-argument audit, phase 2:** a 70-probe
> sweep of every FlatZinc handler with par (constant) and affine arguments
> (the branch-review-driven hunt after the `set_in_reif` find) exposed four
> further wrong-answer classes, all fixed and regression-locked:
> `bool_clause`/`bool_clause_reif` silently skipped par literals
> (`bool_clause([true],[])` claimed UNSAT; `bool_clause([], [false])` too),
> inverted negated-literal signs in the reified big-M row, and divided by
> zero (NaN row) on all-par clauses; `subcircuit` dropped par successor
> values (constant-overwrite) AND its MTZ subtour elimination had no anchor
> node, so every real circuit — even a 2-cycle with variables — was
> infeasible (rewritten with a single-anchor MTZ: n=3 now enumerates the
> exact 6 successor mappings); `array_bool_and`/`array_bool_or` skipped par
> literals leaving r unconstrained; `int_min`/`int_max`/`int_abs` rejected
> constants (UNKNOWN) and silently dropped affine constant terms (e.g.
> `int_min(x+1, 5, m)`).  New `lin_materialize()` helper pins constants and
> affine forms as exact alias vars wherever handlers index bounds directly;
> `int_negate` alias added; `among` folds par elements and the empty set.
> New `test_constant_arguments()` in `fzn_semantics_test.py` (fails 10+
> assertions against the pre-fix binary); full `test.sh` green; ASan/UBSan
> corpus sweep clean.

> **2026-08-11 — branch review + set-membership constant fold:** all remote
> branches were re-surveyed from `feat/flatzinc-complete`.  Finding: the
> `arena/continue-hardening` "floor division" change to `int_div`/`int_mod`
> must **not** be ported — the MiniZinc Handbook ("Basic Modelling",
> §2.1.2, and the language spec's arithmetic-operations section) defines
> `a mod b` with the sign of the **dividend** and `a div b` by truncation
> toward zero, i.e. exactly C's `/` and `%`, which the current code
> implements; `divmod_verify.py` pins this.  A genuinely unported bug class
> was found instead: `set_in_reif`/`set_in` with a constant or affine LHS.
> `int_eq_reif(5,5,r)` worked, but `set_in_reif(7,{3,7,9},r)` bound `r=false`
> and non-reified `set_in(265,1..280)` was UNKNOWN — the reified encodings
> overwrote (`=`) instead of accumulating (`-=`) the difference form's
> constant term after `lin_into`.  Fixed, folded, and regression-locked
> (truth tables in `fzn_semantics_test.py`, edges + randomized constant-LHS
> instances in `divmod_verify.py`; full `test.sh` green, ASan/UBSan corpus
> sweep clean).  Branch: `arena/fzn-set-const-ops`.

> **2026-08-08 follow-up:** the remaining remote branches were re-audited from
> `arena/unmerged-audit-and-correctness`. Safe changes were ported, several
> wrong-answer cases were repaired, and the unsound branch-and-clip commit was
> rejected. See [`docs/BRANCH_AUDIT.md`](docs/BRANCH_AUDIT.md) for branch-by-
> branch disposition, counterexamples, and regression coverage.

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
bounds into an exact FxLP. Only exactly integral `double` values are accepted;
duplicate CSC cells are summed with checked rational arithmetic. When the exact
solve succeeds its verdict wins; otherwise the honest double verdict is kept.

Impact: `cumulative_verify.py 200 777` went from **OK=124 UNKNOWN=76** on
`main` to **OK=200 UNKNOWN=0 MISMATCH=0**. The false-INFEASIBLE→UNSAT bug is
fixed, with a deterministic regression in `examples/fzn/cumulative_exact.fzn`.

### DONE — CLI `-a` (all solutions) and FlatZinc constraint expansion
Implemented standard `-a` / `--all-solutions` for `fznsolve`, `fz_solve`, and `mip_solve`:
- In satisfaction problems (`solve satisfy`), `-a` traverses the branch-and-bound
  tree and prints distinct **visible output tuples**, de-duplicating alternative
  auxiliary-selector assignments. Continuous output spaces return `UNKNOWN`
  rather than falsely printing a finite completion marker.
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

The port was hardened for negative division/modulo, variable count values,
proven element big-M bounds, exact-range exponentiation, and output-level
all-solutions de-duplication. See `docs/BRANCH_AUDIT.md`. All new handlers and
options have dedicated regressions in `tools/fzn_semantics_test.py`.

### DONE — public C API invalid-input audit

Public LP, MIP, QP, exact LP, PGS, FlatZinc, LU, and sparse-LU entry points now
check null/structurally invalid inputs before dereferencing. Invalid models have
dedicated statuses rather than being mislabeled infeasible. `tools/api_test.c`
is wired into `test.sh`; details and differences from remote commit `2253a93`
are recorded in `docs/BRANCH_AUDIT.md`.

### DONE — batch PGS entry points

`pgs_batch_solve` and `pgsf_batch_solve` provide checked sequential traversal of
independent contact systems with aggregate statistics. The global arena half of
remote commit `6ebf11d` was rejected for ownership/alignment/thread-safety bugs;
see `docs/BRANCH_AUDIT.md`.

### DONE — FlatZinc set-literal + status honesty (branch `arena/fzn-set-status-ports`)

Follow-up merge of the still-unmerged parts of `arena/continue-hardening`,
after verifying each of its claims empirically against the MiniZinc
specification (see `docs/BRANCH_AUDIT.md` for the full review):

- **Set-literal parsing no longer truncates silently** (`fz_parse_int_set`):
  `set_in(y,{1,...,280})` with `y=265` had printed `UNSATISFIABLE`; a
  singleton set had overwritten the declared domain (`var 0..5` + `{8}` had
  printed `x = 8`); `among` double-counted duplicate set members.  All three
  fixed and covered by the new brute-force differential
  `tools/divmod_verify.py` (0 wrong answers; wired into `test.sh`).
- **UNSAT is only certified inside the synthetic box**: an infeasible
  verdict on a model containing an undeclared (`var int:`/`var float:`)
  variable is downgraded to UNKNOWN.
- **GLPK differential re-run and green** (sweep 119/119, four-way difftest
  0 mismatches) — glpsol was obtained for this environment.
- That branch's `int_div`/`int_mod` floor-semantics rewrite was **rejected**:
  MiniZinc defines `mod` with the dividend's sign (truncation, C semantics),
  which `main` already implements.  The verifier it ships now encodes the
  spec-correct reference semantics explicitly (with citations), so a future
  contributor cannot make the same mistake either direction.

## Not done (recommended next steps, in priority order)
1. Re-run the MiniZinc differential suite (`tools/mzn_diff.py`) when
   `minizinc` is installed (not packaged for this host's distro).
   The GLPK side is done.
2. Redesign the global `setjmp` allocation-error protocol so a recovering,
   multi-threaded library host can own cleanup without process-global state.
