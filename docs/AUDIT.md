# Correctness audit — `arena/exactness-and-status-audit`

The project's stated rule is that a solver may fail, but it may never lie: an
honest `INFEASIBLE` / `UNBOUNDED` / `ITERATION_LIMIT` / `FEASIBLE` /
`NUMERICAL_FAILURE` / `UNKNOWN` always beats a fabricated optimum.  This branch
audited every solver against that rule.

Six classes of defect were found.  All six could make psolve **return a
confidently wrong answer** or crash on input a host program can legitimately
produce.  Each fix ships with a differential or property test that fails on the
old binary and passes on the new one, and each of those tests is wired into
`test.sh`.

| # | Area | Symptom | Rate before | After |
|---|------|---------|-------------|-------|
| 1 | LP status (`src/solver.c`) | false `UNBOUNDED` / `INFEASIBLE` / bogus `OPTIMAL` | 26 / 150 status mismatches vs GLPK | 0 / 400 |
| 2 | Exact-rational LP (`src/fx.c`) | silently wrong "exact" answers from `int64` overflow | 10 wrong / 200 | 0 / 200 |
| 3 | PGS kernels (`src/pgs_fixed.c`) | `SIGFPE` on a zero diagonal; caller-sized `alloca`; `__int128` truncation | crash on a degenerate contact row | no crash |
| 4 | Allocation (`src/*.c`, drivers) | `SIGSEGV` / `abort()` under memory pressure | 115 crashes across the injection sweep | 0 / 19,073 |
| 5 | MIP (`src/mip.c`, `tools/mipsolve.c`) | suboptimal point reported `OPTIMAL`; fractional value for an integer variable | 33–45 wrong / 400 | 0 / 2,400 |
| 6 | QP (`src/qp.c`) | infeasible / non-stationary point reported solved; optimum reported for an unbounded QP | 10–12 wrong / 200 | 0 / 1,000 |

---

## 1. LP reported `UNBOUNDED` or `INFEASIBLE` for problems that were neither

`LP_INF` (`1e30`) is a *sentinel*, not a bound, but the ratio test treated it as
one: a basic variable with `u = 1e30` produced `theta = (1e30 - x)/v`, a huge
but finite step, so a genuinely unbounded LP was pushed out to `x = 1e30` and
came back `OPTIMAL` or `NUMERICAL_FAILURE`.  `pick_entering()` had no dual
tolerance, so reduced costs at rounding level (~1e-17) counted as improving and
produced spurious rays and cycling.  Phase I could report `UNBOUNDED`, which is
mathematically impossible for it (its objective, `-sum(artificials)`, is
bounded above by 0), and a refactorization could drive a basic artificial
negative so that `sum(artificials) > 1e-6` read a negative sum as feasible —
Phase II then reported `UNBOUNDED` for an infeasible LP.

**Fix.** Skip basic variables at infinite bounds in both Harris passes; add
`TOL_DJ = 1e-9` to the Bland and steepest-edge entering rules; make Phase I
*certify* its answer (worst bound violation plus the artificial sum) and, if it
cannot, restart once from a fresh basis under Bland's rule before giving up
with `NUMERICAL_FAILURE`.

**Evidence.** GLPK differential: `STATUS_MISMATCH` 26/150 → 0/400.  Canonical
sweep 119/119.  `tools/verify.py` also gained a relative row tolerance, and
`tools/difftest.py` now maps GLPK's "no dual feasible solution" to
`UNBOUNDED_OR_INFEASIBLE` instead of a definite `UNBOUNDED`.

## 2. `fxsolve` produced silently wrong "exact" answers

The exact rational core computed products in `__int128` and then truncated them
to `int64` with no overflow check.  Coefficient growth wrapped around silently:
an infeasible LP came back `OPTIMAL 0/1`, bounds were violated, and objectives
were junk (`-508940592513290470/348473746286513981` where the true value is
`509633234/430425`).  `fx_fmt` overflowed in its long division (a digit of −1
printed as `/`), and `fx_from_str` silently truncated values that did not fit.

**Fix.** `src/fx_core.inc` templates the simplex core and instantiates it twice:
an `int64` fast path with `__int128` intermediates, and an `__int128` wide
retry.  Every add/sub/mul/div/compare is checked with `__builtin_*_overflow`
with gcd-reduce and cross-cancel slow paths; an overflow sets a thread-local
flag and abandons the solve.  `fx_solve()` runs the fast core and retries in
128-bit on overflow, and only then reports `FX_OVERFLOW` — never a wrong
answer.  New status enum, `fx_status_name()`, and `res->width` so callers can
see which arithmetic produced the result.

**Evidence.** `tools/fx_exact_test.py` checks feasibility and the objective in
Python `Fraction`s, compares the 64-bit and 128-bit cores, and cross-checks the
double solver.  Old binary: 10 wrong / 200.  New: 0.

## 3. PGS kernels crashed on degenerate contact rows

`pgsf_solve()` divided by `A[i][i]` unconditionally, so a zero diagonal — an
inactive or degenerate contact row, which a physics host will hand it — raised
`SIGFPE`.  Both kernels also pre-computed the diagonal into
`__builtin_alloca(n * 8)` sized by the *caller's* `n`, a stack-overflow
primitive in a library, and the fixed-point kernel truncated its `__int128`
residual and SOR update back to `int64` with wraparound.

**Fix.** A non-positive diagonal now yields no step (mirroring the float
reference's `1/d -> 0` guard); the diagonal is read from `A` inside the sweep
so there is no allocation of any kind; narrowing goes through a saturating
`sat64()` and a `div_round_sat()` that keeps the 64-bit fast path.  Benchmarks
unchanged (n=4: 84 ns vs 82.8 ns; n=64: 5.77 µs vs 5.61 µs).

## 4. "Allocations are checked" was not true

The README's security section claimed allocations were checked.  48 of the 65
allocation sites in `src/fzn.c`, plus sites in `solver.c`, `splu.c`,
`parser.c`, `mip.c` and `qp.c`, used raw `malloc`/`calloc`/`realloc` and
dereferenced the result.  `tools/qpsolve.c` had no `psolve_try()` handler at
all, so an out-of-memory inside the QP core reached `psolve_fail()` with no
handler installed and `abort()`ed.  The last crash source was `strndup()`: it
allocates *inside libc*, so its failure cannot be routed through
`psolve_fail()`, and the FlatZinc tokenizer used it eight times.

**Fix.** Everything allocates through the `psolve_*` helpers; `psolve_strndup()`
added; the QP driver installs the handler.

**Evidence.** `tools/oomlib.c` is an `LD_PRELOAD` shim that fails the Nth and
every later allocation, can census a run's total allocations, and can print a
symbolised backtrace of the resulting crash.  `tools/oom_test.py` censuses each
case and then replays it once per injection point across that whole census; a
run passes only if the process exits by itself.  115 crashes → 0 across all
19,073 injection points in 19 cases.

## 5. MIP labelled suboptimal points `OPTIMAL`

Two independent causes, both producing a wrong answer on roughly 10% of random
instances:

1. `tools/mipsolve.c` filled in the `MIP` fields it cared about and left the
   rest — including `stop_at_feasible` — uninitialised.  A garbage nonzero
   value stopped branch-and-bound at the first integer-feasible point, and
   because nothing set `limit_reached`, that point was reported as a proven
   optimum.  A 3-variable minimisation returned −4.843 where the true optimum
   is −6.217.
2. The LP-rounding heuristic rounded a relaxation value and clamped it into the
   node box against the **raw** bounds.  With a fractional bound on an integer
   variable (`u = 1.875`) the clamp produced `x = 1.875` for a variable
   declared integer; it satisfied every row, so it became the incumbent and was
   reported as the optimum.

**Fix.** `mip.h` documents that the struct must be zeroed and the drivers do it;
`MIPResult` gained `proven_optimal`, set only when the tree was exhausted, and
`mipsolve` prints `FEASIBLE` rather than `OPTIMAL` when it is clear; the
FlatZinc bridge refuses to print an objective for an optimisation model without
it.  The heuristic clamps to the lattice and fails if the node box holds no
lattice point; integer bounds are rounded inward once at the root; accepted
incumbents are snapped to the lattice with the objective recomputed from the
point actually returned, so `res->obj == c . res->x`.

**Evidence.** `tools/mip_diff.py` generates small fully bounded all-integer
models — half feasible by construction, a third with fractional bounds — and
checks status, objective and the validity of the returned point against
exhaustive enumeration.  33–45 wrong / 400 → 0 over 2,400.

It also replaced `if [ -f /tmp/mip_verify.py ]` in `test.sh`, a guard on a path
that never exists, under which the MIP verification had silently never run.

## 6. QP returned infeasible and non-optimal points as solved

`qp.h` promises symmetric **PSD** Q, but `tools/qp_gen.py` only ever built a
strictly positive-definite one, so singular Q, `Q = 0`, `m = 0` and duplicated
rows went untested.  About 5% of those were answered wrongly, including points
violating a constraint by 3e11 while reporting `STATUS 0`.

- The **ratio test could take a negative step**: an already-violated constraint
  gives `-resid/ap < 0`, which was accepted as the blocking step and moved the
  iterate backwards along `p` — uphill and further outside the feasible region.
- **Singular KKT systems were not detected**: `lu_factor()` does not always fail
  on one, and a tiny pivot's wildly inaccurate solve was taken at face value.
- **Success was certified from stationarity alone**, so a stationary point that
  violated a constraint (worst on parallel rows, where only one of the pair is
  rank-independent enough to enter the working set) passed.
- The **stationarity tolerance scaled with the objective value**: on an
  unbounded QP the iterate reached 1e26 and the objective 1e36, dragging the
  `1e-6*(1+|obj|)` tolerance along with it, so a meaningless point passed.
- **Unbounded QPs ground to the iteration limit** instead of saying so.

**Fix.** Clamp negative ratios to a zero-length blocking step; verify the KKT
solve against the original matrix and regularize + refactorize if the residual
is large; require primal feasibility and complementary slackness before
certifying, and verify `find_feasible()`'s Phase-I point against the original
rows; scale the stationarity tolerance by the terms being cancelled and add a
divergence guard; add a recession-direction certificate (`Q d = 0`, `A d <= 0`,
`g.d < 0`) for honest `UNBOUNDED`.  The certificate's tolerances are one-sided
on purpose — a row with even a rounding-level positive slope disqualifies the
ray — so a failed certificate degrades to the iteration limit, which is never
wrong.  `solve_kkt()` also went from three scratch allocations per call to one.

**Evidence.** `tools/qp_diff.py` certifies answers with the KKT conditions
themselves (necessary *and* sufficient for a convex QP, so it does not depend
on a second optimizer converging), plus an exact LP recession test for
boundedness and a HiGHS feasibility test for the "no feasible start" status.
It also splits iteration limits into "the problem really is unbounded" and "a
real capability gap".  10–12 wrong / 200 → 0 over 1,000.

---

## Still open

- **Leaks on out-of-memory paths.**  The processes exit cleanly, but a library
  host that installs its own `psolve_try()` and *recovers* will leak the
  partially built problem (`lp_read()` is the clearest case).  Worth fixing for
  the embedded use case.
- **`psolve_fail()` still `abort()`s when no handler is installed.**  That is
  the documented protocol, but the ROADMAP's non-goals say no `abort()` in
  library code reachable from host input.
- **QP capability gap on singular Q.**  A handful of bounded instances per
  hundred still end in `ITERATION_LIMIT` or `KKT_FAIL` rather than an answer
  (`qp_diff` reports them as `limit-bounded`).  These are honest failures, not
  wrong answers, but a null-space-aware step would close the gap.
- [x] **Q is never checked for symmetry or convexity.**  Fixed in `1b0df30`: `qp_solve()` now checks Q for symmetry and 1x1 / 2x2 principal-minor positive semi-definiteness before iterating, returning status 4 (`QP_NON_CONVEX`) immediately on indefinite or non-symmetric input.
