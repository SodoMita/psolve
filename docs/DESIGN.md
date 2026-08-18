# Design and performance notes

## 1. What it is

`AVX512-Simplex` implements the **two-phase bounded-variable primal revised
simplex method**.  It is a complete, from-scratch LP solver written in C:

- **Phase I** uses artificial variables to find an initial feasible basis.
  For the common case where all constraints are `<=` with `b >= 0`, the slack
  basis is used directly and Phase I is skipped entirely.
- **Phase II** optimizes the true objective from that basis.
- Supports lower/upper bounds per variable (finite or infinite), boxed
  variables, and `<`, `>`, `=` constraints.

The core per-iteration work is the textbook revised-simplex kernel:

| Step  | Operation | Cost |
|-------|-----------|------|
| PRICE | reduced costs `c̄ = c_N − c_BᵀB⁻¹N` (BTRAN + sparse dot products) | O(nnz) |
| FTRAN | `d = B⁻¹ a_q` for the entering column | O(nnz) |
| CHUZR | Harris two-pass ratio test (bounds + ties) | O(m) |
| UPDATE| product-form-of-the-inverse basis update + steepest-edge weights | O(nnz) |
| INVERT| periodic refactorization of the basis (sparse or dense LU) | varies |

## 2. Hardware-utilization techniques

**AVX-512 FMA vectorization (`src/kernels.c`).**  The most-travelled inner
loops — `daxpy` (used throughout the LU elimination, eta-file application, and
hyper-sparse solves), `ddot`, and a sparse-vector dot product — are hand-written
with AVX-512 FMA intrinsics (`_mm512_fmadd_pd`, 8 doubles/cycle), with
AVX2/SSE/scalar fallbacks selected by `__AVX512F__` / `__AVX2__`.

**Cache-friendly sparse layout.**  The constraint matrix is column-compressed
sparse (CSC) with contiguous `double` value arrays and `int` row/column arrays.
The basis is factored directly from its sparse CSC columns.

**Sparse LU factorization (`src/splu.c`).**  The basis matrix is factorized as
`P·B·Q = L·U` with:
- a **fill-reducing column ordering** (increasing column degree — a cheap
  Markowitz approximation),
- **partial row pivoting** for numerical stability,
- factors stored sparsely (L by columns, U by rows and columns) and solved with
  **hyper-sparse forward/back substitution** that skips operations on
  (near-)zero entries.

This replaces the dense O(m²) per-solve / O(m³) per-INVERT work with O(nnz)
solves on sparse bases, which is what makes large sparse LPs fast.

**Steepest-edge (Goldfarb–Reid) pricing.**  Instead of Dantzig's largest-cost
rule, the entering variable is chosen by `max |c̄_j|/√w_j`, where `w_j` tracks
the squared norm of the edge direction `B⁻¹a_j`, updated with the exact
Goldfarb–Reid recurrence after each pivot.  This dramatically reduces the
degenerate pivots that cause Dantzig pricing to stall or cycle on real LPs.

**Sparse/dense dispatch with safety fallbacks.**  Sparse LU is used only when
it is worth it and safe:
- a global density check selects sparse for genuinely sparse problems (density
  ≲ 5%); on denser bases the fill-in makes dense LU faster and more stable;
- a per-basis check and relative-pivot / multiplier-growth detection inside the
  sparse factorization fall back to dense if a basis is ill-conditioned;
- the driver verifies the final primal feasibility and, if the sparse solve
  drifted, transparently re-solves from scratch with dense LU.

**Hyper-sparsity-aware PRICE.**  The reduced-cost vector `y = B⁻ᵀc_B` is often
very sparse; `k_dsdot_sparse` skips rows where `|y[row]|` is below a threshold.

**Cheap feasible start.**  For `<=`-only problems the identity slack basis is
used and the artificial phase is avoided.

## 3. Measured performance

Well-conditioned bounded LPs, cross-checked against GLPK:

```
             n     m    d     mine(s)  glpk(s)  speedup  correct
   dense     100   60  0.20    0.004    0.006    1.6×    ✓
   dense     300  180  0.10    0.031    0.019    0.6×    ✓   (dense basis)
   sparse    500  300  0.05    0.029    0.030    1.0×    ✓
   sparse   1500  900  0.012   0.088    0.115    1.3×    ✓
   sparse   3000 1800  0.005   0.237    0.361    1.5×    ✓
```

- **Large sparse LPs: ~1.3–1.5× faster than GLPK**, correct objectives.
- Small dense LPs: faster (SIMD + lean layout).
- Medium dense LPs: correct but slower than GLPK — the dense basis is O(m²)
  per solve, which is the remaining algorithmic gap for dense problems.

## 5. Quadratic programming (src/qp.c)

A **primal active-set method** for convex QPs:

```
minimize  1/2 x^T Q x + c^T x
subject to       A x  <=  b      (Q symmetric PSD)
```

- Maintains a rank-managed working set of active inequalities (dependent rows
  are skipped via Gram–Schmidt) so the KKT system `[Q A^T; A 0]` is never
  singular.
- Solves the KKT system with the dense LU code, taking a step to the blocking
  constraint or dropping the most-negative multiplier.
- A **Phase-I feasibility** step (minimize sum of artificial slacks, `sum s`,
  with a tiny quadratic regularization to keep it strictly convex) finds a
  feasible point when the origin is infeasible, and correctly reports an
  infeasible QP.
- Verified against scipy (SLSQP) on randomized convex QPs including
  infeasible-origin cases and genuinely infeasible instances.

## 6. Incremental solving (warm starts)

The LP solver exposes a warm-start API so a perturbed problem can be re-solved
from the previous basis instead of from scratch:

- `solver_set_objective` / `solver_set_bounds` + `solver_warm_solve`:
  objective changes never affect feasibility, so the previous basis warm-starts
  Phase II directly.  Bound changes can make a basic variable leave its bounds;
  `warm_solve` detects an infeasible warm start and falls back to a clean
  re-solve (via `solver_refresh`, which reconstructs the LP and re-solves).
  A bound update that changes a variable between fully free and bounded also
  triggers that clean rebuild, because its internal `x⁺ − x⁻` normalization
  changes the column count.
- `solver_add_row` appends a constraint; it reconstructs the LP including the
  new row and does a clean re-solve, guaranteeing the result matches a
  from-scratch solve.

All three operations are verified against from-scratch solves on 200+ random
bounded LPs (`tools/incr_rand.c`), and the round-trip LP reconstruction is
validated (it re-scales the equality-form coefficients back to the original
problem).

## 6b. Mixed-integer programming, sensitivity, robustness

- **MIP (`src/mip.c`)** — branch-and-bound over the simplex LP relaxation:
  best-bound node ordering, fractional-integer-variable branching, and
  bound-based pruning.  Verified against brute-force enumeration on random
  small all-integer problems.
- **Sensitivity** — the optimal dual (shadow-price) vector `B⁻ᵀc_B` and reduced
  costs are exposed via `solver_duals` / `solver_reduced_costs`.
- **Iteration limit** — `solver_solve` honours `s->iteration_limit` and returns
  status `ITERATION_LIMIT` rather than cycling forever on pathological input.
- **Error protocol (`src/err.c`)** — allocation failure (and internal solver
  errors) now unwind through a setjmp/longjmp handler a caller installs with
  `psolve_try()`, instead of calling `exit(1)`.  The CLI reports a clean error.

### Correct initial basis (boxed variables)

A subtle correctness fix: the initial slack/artificial basis is now computed
from the *true* residual of each row, accounting for the contributions of
nonbasic variables at their starting (e.g. positive lower-bound) values, rather
than assuming every nonbasic is 0.  This makes the solver correctly respect
positive variable lower bounds and correctly detect infeasible starts (previously
an infeasible node in branch-and-bound could be mis-reported as a feasible
fractional point). A caller-visible fully free variable is first normalized as
`x = x⁺ − x⁻`, with both components at lower bound zero, so it follows the same
feasible-start path without weakening the original model.

## 7. Honest status and known limitations

- **Correctness:** verified against GLPK on hundreds of random instances —
  100% objective agreement on well-conditioned problems (all sizes/densities),
  correct OPTIMAL/INFEASIBLE/UNBOUNDED classification on the vast majority of
  general instances.  A few failures remain only on numerically pathological
  random LPs (ill-conditioned bases from random negative bounds + equality
  rows), where even GLPK sometimes fails or returns a degenerate status.
- **Large sparse:** the intended target is handled well — faster than GLPK and
  correct, with the sparse LU + steepest-edge combination.
- **Dense / medium problems:** correct but not faster than GLPK, because the
  basis representation for non-sparse problems is still dense.  This is the
  clear remaining step.
- This is **not** "faster than every solver on every problem".  It is faster
  than a widely used reference in the important sparse regime, uses the
  hardware well, and is transparent about where and why a production solver
  would pull ahead.

## 8. Tolerance semantics sheets (roadmap 6.8)

Every literal tolerance in `src/` is inventoried here, with its **direction
of safety**, **what it protects**, and **what it may never justify**.  The
motivating incident was the 2026-08-15 `mip_diff` flip (seed 12345,
WRONG=5): an *exact* activity prune that ignored the engine's own margin —
a semantics bug wearing a numerics costume.  The rule that fell out of it:
a Verdict tolerance (one adjacent to OPTIMAL / INFEASIBLE / UNSAT /
UNBOUNDED) may never decide a verdict alone; it feeds a certificate that
stands without it (directed rounding, exact-rational re-check, or a
printed bound), or the status degrades to the honest numerical-failure
class.

**Machine-checked closure.**  Every site carries a `/* TOLSHEET <ID> */`
comment in the source; `tools/tolsheet_check.py` (a hard `test.sh` gate)
greps `src/` for the canonical tolerance pattern — decimal exponent
literals `[0-9]e±N`, `0x1pN` hex-floats, `DBL_EPSILON`, and the tolerance
macro definitions — and requires: every hit tagged, every tag indexed
here, every indexed ID present in the source at the recorded file:line.
Named macros are documented at their definition; uses resolve by name.

**Classes.**  V = verdict-adjacent (certificate rule above applies);
C = convergence/heuristic switch (may cost iterations or honesty of
*convergence claims*, never answer content); D = honest-decline guard
(doubt → UNKNOWN/unsupported/failure, never a guess); R = recognition of
flattener-exact data (near-1.0 treated as 1.0 etc. — sound because
flattened FlatZinc emits exact values; the acceptance *width* is the
documented modeling tolerance for hand-written input); S = sentinel
magnitude (bound floors, caps) and pure division guards (stability, no
semantics); E = exact module, no numeric margin at all.

### 8.1 LP core — `src/solver.c`, `src/lu.c`, `src/splu.c`, `src/kernels.c`

| ID | site | value | class | direction of safety / protects / may-never |
|---|---|---|---|---|
| TOL-LP-FEAS | solver.c:12 (`TOL_FEAS`) | 1e-9 | C | fixed-var width (`u−l ≤ tol`) for pricing eligibility only; protects degenerate pivots; never a verdict |
| TOL-LP-PIV | solver.c:13 (`TOL_PIV`) | 1e-12 | S | ratio-test pivot acceptability; protects division stability; never skips a *real* blocking bound without the post-solve re-check |
| TOL-LP-DJ | solver.c:18 (`TOL_DJ`) | 1e-9 | V | reduced-cost sign test, i.e. the optimality certificate margin; anti-cycling hysteresis; may never *prove infeasibility*, and a missed tiny negative dj ends in the final-verify re-check, not a fabricated status |
| TOL-LP-STARTSLACK | solver.c:287, 731 | 1e-12 | C | slack-nonneg ⇒ feasible-start *shortcut*; the Phase machinery re-verifies whatever it is handed |
| TOL-LP-SPLUPIV | solver.c:66, 487; splu.c:101 | 1e-13 / 1e-14 | S | sparse-LU pivot floor / zero-fill drop; factorization refusal surfaces as NUMERICAL, never a quiet bad basis |
| TOL-LP-WGUARD | solver.c:836, 839, 946 | 1e-30 | S | steepest-edge weight division floors |
| TOL-LP-RELAX | solver.c:890 | 1e-9·(1+\|θ\|) | C | Harris-style ratio relaxation; any overshoot is caught by the final bounds re-check (TOL-LP-FINALBOX) |
| TOL-LP-AQGUARD | solver.c:928 | 1e-300 | S | division floor in the ratio denominators |
| TOL-LP-FLAT | solver.c:1003 | 1e-9·(1+\|obj\|) | C | stall detector feeding anti-cycling; changes which *rule* runs, never what is reported |
| TOL-LP-ARTPIN | solver.c:1080 | 1e-9 | C | pins only redundant artificials already at \|x\| ≤ tol (pinning a nonzero one would hide row violation — comment block at site); Phase-II checks stand |
| TOL-LP-P1CERT | solver.c:1211 | 1e-6·(1+\|artsum\|) | V | Phase-I certificate admissibility (max bound violation); failure → one exact-basis Bland restart → SOLVE_NUMERICAL; never INFEASIBLE |
| TOL-LP-P1SUM | solver.c:1214 | 1e-6 absolute | V | certified-infeasible artificial-sum threshold; the verdict it gates is *re-proven* by the directed-rounding Farkas check (TOL-LP-FARKAS) or downgraded via TOL-LP-SHAKY; may never print INFEASIBLE alone |
| TOL-LP-FINALBOX | solver.c:1547 | 1e-6·(1+\|obj\|) | V | final point vs bounds re-check; failure ⇒ no solution leaves the solver |
| TOL-LP-FINALROW | solver.c:1562 | 1e-5·(1+\|b_i\|) | V | final equality-residual re-check; same rule |
| TOL-LP-FARKAS | solver.c:657 (`mar = tol·(1+\|R\|)` inside `solver_farkas_boxcert`); margin args at main.c:134, fzn.c:3990 (1e-6), mip.c:295 (MIP_TOL) | caller-set | V | directed-rounding (FE_UPWARD/FE_DOWNWARD) proof that `min_box(yᵀA)x > yᵀb + margin`; protects every printed INFEASIBLE/UNSAT verdict; may never be weakened to RN sums, may never fire on a ray that failed the sign repair, may never be bypassed when the exposure frontier (TOL-LP-SHAKY) is crossed — then the honest class is SOLVE_NUMERICAL |
| TOL-LP-SHAKY | main.c:139, mip.c:443 | 5e-7 = ½·1e-6 | V | exposure frontier `E·DBL_EPSILON ≥ 5e-7`: the double verdict cannot distinguish infeasibility from rounding noise past this line ⇒ NUMERICAL |
| TOL-LP-BIGCAP | solver.c:614, 671 | 1e29 | S | box-certificate big-M sentinel; a box this wide ⇒ boxcert *declines* (honest), exposure caps there |
| TOL-LU-SING | lu.c:26 | 1e-300 | S | dense LU singular-pivot floor; failure ⇒ refactorization error path, not a wrong solve |
| TOL-SPLU-PIVREL | splu.c:131 | 1e-9·maxA | S | relative pivot refusal; same NUMERICAL surfacing rule |
| TOL-SPLU-GROWTH | splu.c:132 | 1e10 | S | factor norm-growth watchdog; crossing it fails the factorization, which surfaces as NUMERICAL |
| TOL-SPLU-SOLVE0 | splu.c:264 | 1e-14 | S | sparse-solve zero test |
| TOL-LP-INF | solver.h:11 | 1e30 | S | "infinite" bound encoding inside the LP data structures; structural sentinel, never compared for equality against data |
| TOL-LP-WCAP | solver.c:947 | 1e18 | S | steepest-edge weight normalization cap (overflow guard on the score path; pairs with TOL-LP-WGUARD) |
| TOL-KRN-YTOL | kernels.c:78–86 (fed by `hyper_tol`, default **0.0** = exact) | 0.0 | C | hyper-sparse PRICE skip threshold; 0 means no dual is ever silently dropped; a caller-set positive value is a pricing heuristic only — the DJ certificate is recomputed exactly at optimality |
| TOL-LP-HYPERDEF | solver.c:55, 782 | 0.0 | C | the default itself: exactness until changed deliberately |

### 8.2 MIP / branch-and-bound — `src/mip.c`

| ID | site | value | class | direction of safety / protects / may-never |
|---|---|---|---|---|
| TOL-MIP-INT | mip.c:11 (`MIP_TOL`) | 1e-6 | V | integrality + incumbent feasibility acceptance (verify fn: bounds, rows, integrality); protects every integer optimum ever printed; may never *round a farther-than-tol value* into an integer assignment, never accept a violating row |
| TOL-MIP-PROGRESS | mip.c:130, 131 | 1e-9 | C | B&B bound-progress test (node bookkeeping); no verdict content |
| TOL-MIP-FARKAS | mip.c:240 (`mar`), 295 (arg) | 1e-6·(1+\|rhs\|) | V | node prune Farkas margin; the box comment at mip.c:169-186 records the 6.8 incident — may never prune a node whose exact-rational or exposure check disagrees |
| TOL-MIP-SHAKY | mip.c:443 | 5e-7 | V | exposure frontier (as TOL-LP-SHAKY) for *uncertified* node infeasibility ⇒ SOLVE_NUMERICAL, never UNSAT-from-noise |
| TOL-MIP-PROPRND | mip.c:597–613 | 1e-9 | V | bound-tightening hysteresis: `floor(ub+1e-9)`, `ceil(lb−1e-9)` — always rounds *outward* only within the margin; protects propagation strengthening; may never cross an integer the LP bound is on the other side of by more than tol |
| TOL-MIP-CROSS | mip.c:620 | 1e-9 | V | propagated-domain crossing ⇒ node infeasible; hysteretic (a crossing must exceed tol), erring toward keeping the node — keeping a dead node costs time, pruning a live one fabricates UNSAT |
| TOL-MIP-GAP | mip.c:652, mip.h:43; fzn default fzn.c:3951 | 1e-4 rel | V | early-stop relative gap; protects reported bound honesty; a gap-stopped result is reported *with its bound*, may never be called an exact optimum, and the gap may never touch feasibility of the incumbent (already verified by TOL-MIP-INT) |
| TOL-MIP-FBBT | mip.c:476–557 (hysteresis slacks 1e-9) | 1e-9 | V | activity bounds under directed rounding — the replacement for the 6.8-incident RN prune; no coefficient is dropped (mip.c:463-466 comment); may never justify UNSAT without the post-prune certificate path |
| TOL-MIP-BIGCAP | mip.c:44, 46, 191, 506 | 1e29 | S | infinite-box sentinel at the fx/Farkas bridges; ≥ cap means "no finite bound" — a classification, and the ray paths it feeds re-verify |
| TOL-MIP-INFBOUND | mip.c:664, 666, 667 | ±1e30 | S | "no incumbent / no finite bound yet" sentinels; may never be *reported* — the result path replaces them with the honest no-solution statuses |

### 8.3 QP (active-set + PSD gate) — `src/qp.c`

| ID | site | value | class | direction of safety / protects / may-never |
|---|---|---|---|---|
| TOL-QP-INITRES | qp.c:100 | 1e-8·(1+\|rhs\|+\|x\|) | C | phase-0 KKT solve acceptance; failure only triggers the regularization ladder |
| TOL-QP-REG | qp.c:107 | 1e-8 / 1e-6 / 1e-4 | C | KKT-matrix regularization ladder for singular PSD Q; perturbs a *work copy*; the result is re-proven against the unperturbed system by TOL-QP-KKT/COMP/PRIMAL |
| TOL-QP-NRMDIV | qp.c:127, 139 | 1e-9 | S | normalization division guard |
| TOL-QP-RANK | qp.c:159 | 1e-9 | C | rank test for the orthonormal active-set basis |
| TOL-QP-ACTIVE | qp.c:163 | 1e-7 | C | active-set candidacy `resid > −tol`; errs toward *including* a constraint (conservative for stationarity) |
| TOL-QP-MU | qp.c:207, 210 | 1e-9·scale | C | multiplier zero/sign tests steering the drop step |
| TOL-QP-KKT | qp.c:231 | 1e-7·(1+gmax+tmax) | V | final stationarity certificate; failure ⇒ QP_KKT_FAIL status, never a printed optimum |
| TOL-QP-COMP | qp.c:238 | 1e-7·(1+\|b_i\|) | V | complementarity certificate; same rule |
| TOL-QP-PRIMAL | qp.c:250, 404 | 1e-7·(1+\|b_i\|) | V | primal feasibility certificate (accumulated drift); same rule |
| TOL-QP-UNBDIR | qp.c:275 | 1e-9·scale | V | descent-direction sign test gating UNBOUNDED claims — the curvature check stands behind it |
| TOL-QP-CURV | qp.c:283 | 1e-12·(1+\|Q\|)·p² | V | zero-curvature test (flat-direction ⇒ unbounded/indefinite edge); may never declare curvature where a real positive step exists beyond tol |
| TOL-QP-STEP | qp.c:313, 322, 327 | 1e-12, 1e-10 | C | line-search guards; a wrong near-1 step is caught by the terminal KKT re-check |
| TOL-QP-FEASROW | qp.c:360 | 1e-8 | C | feasibility recheck feeding the phase-1 route |
| TOL-QP-PERTURB | qp.c:364 (`eps`) | 1e-6 | C | feasibility-QP quadratic perturbation weight; the perturbation may *never decide* feasibility — only the recheck (TOL-QP-PH1SUM/PRIMAL) may |
| TOL-QP-PH1SUM | qp.c:397 | 1e-7 | V | feasibility-phase slack-sum verdict threshold |
| TOL-QP-SYM | qp.c:460 | 1e-8·(1+qscale) | V | PSD-gate symmetry test; asymmetric beyond tol ⇒ the convexity proof route is *refused* (honest decline), never silently symmetrized |
| TOL-QP-PSD | qp.c:497 | 1e-9·(1+qscale) | V | pivot `≥ −tol` accepted as PSD; protects the convexity certificate (short witnesses enumerated at qp.c:443-486); may never accept a pivot `< −tol` — that path proves indefiniteness instead (2×2 witness at qp.c:474-483) |
| TOL-QP-DIVERGE | qp.c:205 | 1e14 | D | iterate-magnitude divergence cap ⇒ QP_ITERATION_LIMIT (honest non-answer), never a "solution" assembled from a diverging iterate |

### 8.4 Boxed-QP physics kernels (PGS) — `src/pgs.c`, `src/pgs_fixed.c`

| ID | site | value | class | direction of safety / protects / may-never |
|---|---|---|---|---|
| TOL-PGS-CONV | pgs.c:39, 80, 91 (=`opt->tol`, pgs.h:31) | caller-set | C | convergence stop for an iterative contact solver; `0` = run all iterations; protects termination, and the *status* byte distinguishes "converged" from "iteration cap" — a capped run is never reported as converged |
| TOL-PGS-DIAG | pgs.c:69 | 1e-14 | S | near-zero diagonal guard (row skipped; deterministic, documented) |
| TOL-PGSF-CONV | pgs_fixed.c:63 (pgs_fixed.h:43) | caller-set (int64 units) | C | fixed-point analog of TOL-PGS-CONV; saturating arithmetic documented at pgs_fixed.c:8 |
| TOL-PGSF-SAT | pgs_fixed.c:8 (policy comment) | exact | E | no float margins: saturation, not rounding, is the overflow policy |
| TOL-PGS-INF | pgs.h:62 | 1e30 | S | box sentinel "no bound" for the kernel input; structural, never compared for equality |

### 8.5 FlatZinc front-end — `src/fzn.c`

Recognition tolerances (class R) accept flattener-exact structure
(`coef == 1.0`, `const == 0.0`, integral constants) with width 1e-9…1e-12;
decline guards (class D) degrade to UNKNOWN instead of acting on
non-lattice data.  The R width is a documented modeling tolerance: only
hand-written input with deliberate sub-width perturbations observes it.

| ID | site | value | class | direction |
|---|---|---|---|---|
| TOL-FZN-ALIAS | fzn.c:329, 572, 590, 704, 1162 | 1e-12 | R | unit-coefficient/zero-constant recognition for aliases & array-decls |
| TOL-FZN-CONSTBOOL | fzn.c:2524, 2593 | 1e-12 | R | constant-relation fold accepting ≈0/≈1 as false/true |
| TOL-FZN-INTCONST | fzn.c:119, 2416, 2448 | 1e-12 | D | near-integer constant requirements; miss ⇒ parse/predicate declines (never rounds data into a model) |
| TOL-FZN-BIGBOUND | fzn.c:657 (`FZ_BIG_BOUND`) | 1e9 | S | the ±sentinel box itself; may only ever be *hit-tested* (TOL-FZN-BIGHIT), never treated as a real bound by the verifier |
| TOL-FZN-BIGHIT | fzn.c:658 (`FZ_BIG_HIT_TOL`), 896–899 | 1e-5 | D | big-M sentinel *hit* test ⇒ downgrade to UNKNOWN; may only ever downgrade |
| TOL-FZN-OBJZERO | fzn.c:896 | 1e-14 | R | objective coefficient ≈0 skip in the sentinel scan |
| TOL-FZN-LATTICE | fzn.c:748 | 1e-9 | D | fractional part ≈0/≈1 snapped to the lattice; non-uniform lattice ⇒ honest decline (comment at fzn.c:740-744 records the int_le_reif(0.6) regression) |
| TOL-FZN-LATTICECAP | fzn.c:746, 752 | 1e15 | D | wild-magnitude coefficients ⇒ decline (no exact unit step exists) |
| TOL-FZN-BOOLBOX | fzn.c:778, 829 | 1e-9 | R | [0,1]-box recognition widened by tol for float-boxed bools |
| TOL-FZN-TBLCOEF | fzn.c:1225 | 1e-9 | R | integer-coefficient match for table rows; miss ⇒ predicate unsupported |
| TOL-FZN-FIXED | fzn.c:1941 | 1e-9 | R | lo≈hi fixed-var fold in `int_pow`; the folded value is exactly re-verified (rint checks on base and pow result) |
| TOL-FZN-DIVGUARD | fzn.c:1986 | 1e-15 | D | divisor |const| < tol ⇒ decline (near-0 division normalizes nothing honestly) |
| TOL-FZN-CUMUL | fzn.c:2791, 2810 | 1e-9 | D | `cumulative` duration/resource args must be near-integer; else decline |
| TOL-FZN-BOUNDINIT | fzn.c:2215, 2752, 2796, 3358, 3419 | ±1e18 | S | min/max accumulator initializers across the bounds-scan paths (send(bounds|disjunctive|cumulative); sentinels, never compared against data for equality — a model constant wider than 1e18 ends in the honest-decline lattice guards, not here) |
| TOL-FZN-POWCAP | fzn.c:1866, 1870, 1933, 1944, 1963, 2101, 2690 | 0x1p53 | D | exact-integer mantissa-width cap on folded values (pow / related exact folds); past it ⇒ decline to UNKNOWN, never round |
| TOL-FZN-FARKAS | fzn.c:3990 | 1e-6 | V | boxcert margin arg for fznsolve's Farkas re-verify (see TOL-LP-FARKAS) |
| TOL-FZN-SHAKY | fzn.c:3996 | 5e-7 = ½·1e-6 | V | fznsolve's own exposure frontier on an uncertified UNSAT ⇒ honest downgrade (same family as TOL-LP-SHAKY) |
| TOL-FZN-MIPGAP | fzn.c:3951 | 1e-4 | V | default `mip_gap` for the fznsolve MIP route (see TOL-MIP-GAP) |

### 8.6 CP engine — `src/fz_cp.inc`

| ID | site | value | class | direction |
|---|---|---|---|---|
| TOL-CP-UNIT | fz_cp.inc:222, 223 | 1e-9 | R | unit-term recognition (±1 coef, 0 const) in `cp_term_from_lin`; a miss makes the *constraint* decline to the honest path — a widened accept only fires on flattener-exact data |

The CP engine is otherwise integer-exact: domain stores are `int64` sets
and the propagators (incl. the functional-graph family) compare exactly.

### 8.7 Exact module — `src/fx.c`, `src/fx_core.inc`

Class E by construction: rational/nextafter lattice arithmetic — **no
decision margins anywhere** (the comment at fx.c:30 records why even a
1e-9 *display* rounding was a model-changing bug).  The only two literal
magnitudes in the module are representability *guards*, class D: input or
intermediate past the guard makes the exact path refuse the data, so the
caller declines honestly instead of solving a lossy encoding.

| ID | site | value | class | direction |
|---|---|---|---|---|
| TOL-FX-RANGE | fx.c:34 | ±0x1p63 | D | lattice-representability range of the binary form; outside ⇒ refuse (the *model* is too wide for the exact encoding, and the honest path is the numeric engines + their own verdict tolerances) |
| TOL-FX-DECCAP | fx.c:109 | 9e17 | D | decimal-integer parse cap while encoding user data; overflow ⇒ refuse rather than truncate |

### 8.8 Non-tolerance constants swept by the closure grep

The checker (§8 preamble) is deliberately total over exponent literals; a
few hits are not tolerances at all.  They are indexed here so that the
closure stays complete and their classification is a decision, not an
oversight.

| ID | site | value | class | why it is not a tolerance |
|---|---|---|---|---|
| TOL-SYS-NSEC | main.c:109 | 1e9 | — | nanosecond→second unit conversion for wall-clock reporting; steers no verdict, bound, or branch |
