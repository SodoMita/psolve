# Remote branch audit (2026-08-08)

This pass compared every remote branch against `main` before selecting work for
`arena/unmerged-audit-and-correctness`.

## Branch inventory

| Remote branch | State relative to `main` | Result |
|---|---:|---|
| `arena/exactness-and-status-audit` | fully merged; no exclusive commits | No action required. Its status/OOM hardening is already on `main`. |
| `arena/flatzinc-float-correctness` | fully merged; no exclusive commits | No action required. |
| `fzn-table-constraint` | fully merged; no exclusive commits | No action required. |
| `arena/fzn-all-solutions` | exact fallback + FlatZinc commits already reviewed here, a merge, and a narrow parser-warning fix | Superseded by this branch's stricter parser and hardened ports. |
| `arena/audit-hardening` | six exclusive commits and three `main` commits missing at final fetch | Reviewed commit by commit; safe/high-value parts were ported and hardened. The branch-and-clip and global arena allocator implementations were rejected as unsound; batch PGS APIs were ported separately. |
| `arena/continue-hardening` | set semantics / status-honesty fixes + verifiers, plus a division-semantics regression | Reviewed empirically against MiniZinc semantics; sound fixes ported on `arena/fzn-set-status-ports` (now on `main`), one part rejected — see below. |

### `arena/continue-hardening` review findings

- **REJECTED: `int_div`/`int_mod` floor-division rewrite.**  The branch
  changed the constant divisor handlers from C truncation to Python-style
  floor semantics (remainder sign of the divisor) and its verifier encoded
  the same assumption.  But MiniZinc spec 4.1.11.2 fixes the modulo sign to
  the *dividend's* (`7 mod -3 = 1`, `-7 mod 3 = -1`) with the identity
  `x = (x div y)*y + (x mod y)` — i.e. truncation toward zero, C semantics.
  `main`'s handlers (with the exact sign-condition encoding) already
  implement this correctly, verified by the ported verifier under corrected
  reference semantics.  The rewrite was therefore not merged.
- **Ported: set-literal honesty.**  `main` still parsed `set_in`/`among` set
  literals through a fixed 512-byte buffer with a silent 256-value cap —
  `set_in(y,{1,...,280})` with `y=265` printed `UNSATISFIABLE` while the
  model is satisfiable — and a singleton set overwrote the variable's
  declared domain (`var 0..5: x; set_in(x,{8})` printed `x = 8`).  Ported
  the branch's dynamic deduplicating parser (`fz_parse_int_set`, honest
  1024-value cap, ranges of any width as bound clamps, singleton/range
  intersection with the declared domain, empty-range infeasibility,
  `among` duplicate-member dedupe and encoding-size cap).
- **Ported: synthetic-box UNSAT honesty.**  `UNSATISFIABLE` is only
  certified inside the bridge's ±1e9 sentinel box; models with undeclared
  (`var int:`/`var float:`) bounds now degrade an infeasible verdict to
  UNKNOWN instead of fabricating UNSAT (e.g. `int_lin_eq z=2e10`).
- **Ported: brute-force verifier + tests.** `tools/divmod_verify.py`
  (reference semantics corrected to truncation as above; duplicates,
  negative divisors/dividends, optimize objectives) and the `-a`
  distinct/exact-count regression; `fznsolve` usage lists `-a`; GLPK
  differential re-run green (sweep 119/119, difftest 0 mismatches).

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

### Public C API guards

The final remote fetch added commit `2253a93`, which attempted a public API
null-safety pass. Its intent was ported, but not verbatim:

- The original returned `FX_INFEASIBLE` for a null or malformed exact LP. That
  is a mathematical claim about a model that was never valid. Dedicated
  `SOLVE_INVALID`, `FX_INVALID`, `QP_INVALID`, `MIP_INVALID`, and `PGS_INVALID`
  statuses now distinguish bad API input from legitimate solver outcomes.
- The original omitted `mip_solve` from its guards even though the audit text
  claimed every public solver entry point was covered. MIP is now covered too.
- The original changed a valid empty sparse-LU factorization (`m=0`) from
  success to failure. Empty LU/SPLU factorization remains a successful identity
  operation; null pointers are rejected only when positive dimensions require
  storage.
- `solver_create` and `mip_solve` now validate CSC monotonicity, row indices,
  required arrays, dimensions, relations, and finite numeric data before any
  dereference. QP, PGS, exact LP, incremental LP, FlatZinc, LU, and SPLU entry
  points have corresponding structural/null guards.
- `lp_read` now parses into local zeroed storage and publishes it only on
  success, so an early parse failure cannot free or partially mutate an
  uninitialized caller output.

`tools/api_test.c`, wired into `test.sh`, locks in invalid-input behavior and
also checks the empty-factorization contract.

### Batch physics API ported; arena allocator rejected

Late commit `6ebf11d` combined two independent features. The sequential
`pgs_batch_solve` / `pgsf_batch_solve` wrappers were small and safe, so they
were ported with clearer contracts: zero jobs succeeds without arrays, invalid
top-level arguments return `-1`, per-job validation uses `PGS_INVALID`, and the
aggregate flop count saturates instead of overflowing `long`.

The global arena allocator was **not** ported. Its `psolve_free()` skipped every
free whenever any arena was active, including heap pointers not owned by that
arena; arena `realloc` blindly read a private header before any input pointer;
and aligning only the offset did not align a caller-supplied unaligned base.
The active arena was process-global rather than thread-local or scoped, and the
solver still contained plain `free()` paths that could receive arena pointers.
Those are invalid-free, leak, alignment-UB, and concurrency hazards. A future
arena needs per-allocation ownership, alignment relative to the absolute base,
checkpoint/rollback scopes, nested/thread-local contexts, and a proof that no
arena pointer reaches libc `free`.

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

**Reconsidered soundly on `arena/phase4-interactive-hardening`.**  The bound
tightening was re-implemented (`fbbt_tighten` in `src/mip.c`) satisfying every
rejected property:

- **No coefficient dropping** — every |a|, however small, is kept.  The audit's
  counterexample now solves to the true optimum `y = 2` (the rejected commit
  reported `y = 1`).
- **Outward-rounded activity bounds** — the "other variables" activity that
  feeds a bound is biased conservatively (small for the `rest_min` used by
  `'<'`/`'='` rows, large for the `rest_max` used by `'>'` rows) and the final
  bound is rounded outward, so a tightened bound never excludes a feasible
  point.
- **Correct extremum** — a `'<'`/`'='` row derives both the upper (a>0) and
  lower (a<0) bounds of `x_j` from the *minimum* activity of the others; a
  `'>'` row from the *maximum*.  (The rejected commit used the wrong extremum
  for the a<0 cases, which could prune feasible points.)
- **Integer-only bound updates** — FBBT tightens only integer-variable bounds
  (snapped to the lattice).  It deliberately does not tighten float-variable
  bounds, which would perturb the reported LP vertex for no pruning benefit.
- **No reduced-cost fixing** — deliberately omitted until the solver's
  reduced-cost sign convention is documented.
- **Adversarial differential tests** — `tools/fbbt_verify.py` (wired into
  `test.sh`) enumerates the true optimum over the integer box for random MIPs
  mixing tiny coefficients (1e-13) with large variable magnitudes and all three
  relation types, plus the audit counterexample.  Zero wrong answers over
  thousands of cases; ASan/UBSan clean.

## Additional `main` flaws fixed during the audit

- The latest parser cleanup declared temporary pointers below early `goto err`
  paths. GCC correctly warned they could be uninitialized; the error label then
  passed them to `free()`. All cleanup-owned pointers are now initialized before
  any jump and released at one cleanup point.
- LP objective, RHS, and matrix values accepted NaN/Infinity. A one-variable LP
  with objective `nan` was printed as `status: OPTIMAL`, `objective: nan`.
  LP and QP front ends now reject all non-finite model data, with fixed sanitizer
  regressions.
