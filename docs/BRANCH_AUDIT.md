# Remote branch audit (2026-08-08)

This pass compared every remote branch against `main` before selecting work for
`arena/unmerged-audit-and-correctness`.

## Branch inventory

| Remote branch | State relative to `main` | Result |
|---|---:|---|
| `arena/exactness-and-status-audit` | fully merged; no exclusive commits | No action required. Its status/OOM hardening is already on `main`. |
| `arena/flatzinc-float-correctness` | fully merged; no exclusive commits | No action required. |
| `fzn-table-constraint` | fully merged; no exclusive commits | No action required. |
| `arena/audit-hardening` | four exclusive commits, plus one newer `main` commit missing | Reviewed commit by commit; safe/high-value parts were ported and hardened. The final branch-and-clip commit was rejected as unsound. |

## Ported work

### Exact-rational fallback for MIP relaxations

Ported the fallback that retries an integral MIP relaxation with `fx_solve`
when the double simplex reports `SOLVE_NUMERICAL` or `INFEASIBLE`. This removes
the dominant source of `UNKNOWN` results in big-M FlatZinc models:
`cumulative_verify.py 200 777` improves from 124 solved / 76 unknown on `main`
to 200 solved / 0 unknown, with no mismatches.

The original branch implementation needed three correctness fixes before it
was safe to trust as an exact certificate:

1. `fx_from_double` accepted values within `1e-9` of an integer and rounded
   them. The fallback could therefore certify a different model. Conversion is
   now accepted only when the binary `double` value is exactly integral.
2. Duplicate CSC entries overwrote one another in the dense exact matrix.
   Duplicate `(row,column)` triplets now use checked rational addition in both
   `fx_read` and the MIP fallback.
3. Dense `m*n` allocation arithmetic was unchecked. It is now overflow-guarded,
   and malformed CSC row/pointer data makes the fallback decline rather than
   omit coefficients.

### FlatZinc `-a` and additional handlers

Ported the all-solutions option and the additional element, set, arithmetic,
count, extrema, and boolean-table handlers, then fixed the following confident
wrong-answer cases found during review:

- `-a` enumerated auxiliary selector binaries as if they were user solutions.
  A table with two identical rows printed the same visible solution twice;
  `int_abs(0,0)` did likewise through its ambiguous sign selector. Visible
  output tuples are now de-duplicated.
- `-a` printed one LP vertex and then `==========` for a continuous satisfaction
  domain, falsely claiming an infinite space was exhausted. Such requests now
  return `UNKNOWN`.
- `int_div` / `int_mod` did not enforce the sign of a variable remainder. For
  `a=-5, k=2`, `int_mod(a,k,r)` could return `r=-9`. The encoding now enforces
  MiniZinc's truncation-toward-zero identity and remainder sign for positive or
  negative constant divisors.
- Variable-array element used a magic `M=1000`. An unselected value at
  `1,000,000` made a valid selection of another entry report UNSAT. Every
  indicator now uses the proven bounds of the complete `value-array[i]`
  expression; unbounded cases return `UNKNOWN`.
- `fzn_count_eq(xs, value, count)` ignored a variable `value` and counted zero.
  It now reifies `xs[i]-value == 0` directly.
- `int_pow` could insert rounded or non-finite `pow()` results into an integer
  model. Results that cannot be represented exactly in the solver's double
  integer range now return `UNKNOWN`.

## Rejected work: branch-and-clip / reduced-cost fixing

Commit `24985ad` was deliberately **not** ported. Its propagation drops every
coefficient with `abs(a) < 1e-12`, which is not a semantics-preserving operation
when variable magnitudes are large. This deterministic counterexample has the
true optimum `y=2`, while that branch reports `OPTIMAL y=1`:

```text
maximize
2 1
0 1
1
<
-10000000000000 -10000000000000
0 10
2
0 0 0.0000000000001
0 1 1
```

The row is `1e-13*x + y <= 1`; fixed `x=-1e13` contributes `-1`, so `y<=2`.
Ignoring the small coefficient changes the row to `y<=1` and removes the true
optimum. The implementation also uses absolute activity tolerances under large
cancellation and its reduced-cost path assumes original minimization signs,
while `solver_reduced_costs` currently exposes the internal maximization-form
sign. Bound propagation can be reconsidered only with outward-rounded activity
bounds, no coefficient dropping, a documented reduced-cost convention, and
adversarial differential tests.

## Additional `main` flaws fixed during the audit

- The latest parser cleanup declared temporary pointers below early `goto err`
  paths. GCC correctly warned they could be uninitialized; the error label then
  passed them to `free()`. All cleanup-owned pointers are now initialized before
  any jump and released at one cleanup point.
- LP objective, RHS, and matrix values accepted NaN/Infinity. A one-variable LP
  with objective `nan` was printed as `status: OPTIMAL`, `objective: nan`.
  LP and QP front ends now reject all non-finite model data, with fixed sanitizer
  regressions.
