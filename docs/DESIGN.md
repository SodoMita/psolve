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
