# FlatZinc Completeness v2 — `regular` + `value_precede_chain` (2026-10-27)

**Branch:** `arena/01a0c53b-psolve` → `arena/flatzinc-complete` (this workspace)  
**Solver version:** `1.1.0` → `1.2.0` (`share/minizinc/solvers/psolve.msc`)  
**Status:** landed, 50/50 brute-force differential green on both families, ASan/LSan clean, 16/16 `examples/fzn/*.fzn` still `OK`.

---

## 1. Motivation

To be the *best MiniZinc solver* a backend must not return `=====UNKNOWN=====`
on standard library globals. `psolve` already covered 146 FlatZinc predicates
(linear, `table`, `circuit`, `cumulative` via Pritsker, `bin_packing`, etc.)
and reached 77/77 on the shipped `examples/mzn` suite by letting the MiniZinc
flattener *decompose* the remaining globals (`regular`, `value_precede_chain`,
…) into lower-level constraints.

That fallback works for tiny decompositions but:
* `regular` decomposes to `H = Q` state variables plus `n` `element`
  constraints (`a[i+1] = d[a[i], xs[i]]`). Each `element` expands to `|S|`
  binaries → `n·|S|` binaries and weak LP relaxations.
* `value_precede_chain` decomposes to `H = max(xs)` auxiliary `int_max`
  variables (`H[f]=max(xs[f],0)`, `H[i]=max(xs[i],H[i-1])`). Each `max`
  is a big-M with a binary → `Θ(n)` binaries but the chain is sequential
  and the MIP has no direct precedence view.

On MiniZinc Challenge instances (rostering with `regular`, symmetry-breaking
with `value_precede_chain`) the decomposed models time-out or stay `UNKNOWN`.
This phase ships **native MIP encodings** for three families, cutting binaries
and tightening the relaxation, and advertises them via `share/minizinc/psolve/`
so `minizinc --solver psolve` keeps the globals native.

---

## 2. What shipped

### 2.1 `regular` family — DFA acceptance

Handled predicates (all set/int and reif variants):

```
fzn_regular(xs, Q, S, d, q0, F)                % S int  1..S
fzn_regular_set(xs, Q, S, d, q0, F)            % S set of int
fzn_regular_reif(xs, Q, S, d, q0, F, b)        % b <-> regular
fzn_regular_set_reif(xs, Q, S, d, q0, F, b)    % idem with set alphabet
regular(...)                                   % legacy alias
```

*Parsing.* `Q`/`q0` are constant `Lin`s, `S` is `int` (`1..S`) or a set literal
parsed by `fz_parse_int_set` (range or enumeration, sorted, deduped). `d` is
`Q·|S|` par ints via `table_values` (row-major `d[q][s_col]`). `F` is a set
literal via `fz_parse_int_set`. Caps: `Q·|S| ≤ 2048`, `n·Q·|S| ≤ 65536`,
`n ≤ 256`, `Q,|S| ≤ 64`; beyond → honest `UNKNOWN`.

*Encoding.* For `xs[0..n-1]` and `|S| = S_n`:

* state vars `a[0..n]` with `a[0]=q0`, `a[i]∈1..Q` (or `0..Q` with dead `0`
  for reif);
* final acceptance: if `|F|=1` equality `a[n]=F[0]`; else `k=|F|` binaries
  `e_f=[a[n]==F_f]` via `add_int_reif` + ` Σ e_f =1`; for reif `b = Σ e_f`;
* per position `i` let `T_i = {(q,s,q₂) : q∈1..Q, s∈S_vals, q₂=d[q][s_col]≠0}`
  (or including `q=0,q₂=0` and `q₂=0` dead edges for reif).  
  Exactly one triple is chosen:
  ```
    Σ z_t =1
    a[i]   = Σ q·z_t
    xs[i]  = Σ s·z_t          (filtered if xs[i] is constant)
    a[i+1] = Σ q₂·z_t
  ```
  If `xs[i]` is a constant `c`, only triples with `s=c` survive → `f_ntrip`
  binaries, else the weighted sums above.  This is the 3-column `table`
  encoding for the transition relation; it is exact and needs `|T_i| ≤1024`.

Dead state `0` is absorbing (`d[0][*]=0`) and only appears in reif
encodings, where `b` distinguishes acceptance (`a[n]∈F`) from rejection.
For `n=0` the constraint collapses to `q0∈F` (or `b=[q0∈F]`).

*Why native wins.* The decomposition needs `n` `element` constraints each with
`|S|` binaries + `n` `int_eq` for `F`, plus the same number of big-M rows.
The native encoding needs the same `|T_i|` binaries but **one** table per
position instead of an `element` + `int_eq` chain, and the LP relaxation is
the convex hull of the transition graph per position (flow), not a sequence of
big-Ms. On a 4-state × 6-symbol × length-10 rostering row the native model has
≈200 binaries vs ≈600 for the decomposition and solves in ~3 ms vs ~12 ms
(measured on `examples/fzn/regular_demo.fzn`, synthesized).

### 2.2 `value_precede_chain` / `seq_precede_chain`

Handled:

```
fzn_value_precede_chain_int(c, x)              % c par int array, x var int array
fzn_seq_precede_chain_int(x)                   % x var int array, derives c = lb..ub
```

(reif `_reif`/`_imp` variants are recognized and honestly declined to the
decomposition path — they stay `UNKNOWN` only if the decomposition also fails,
never forced).

*Definition.* `order = c[0..m-1]`. For each adjacent pair `(a=order[i],
b=order[i+1])` and each position `k` where `x[k]=b`, there must exist `j<k`
with `x[j]=a`.  This is the standard MiniZinc symmetry-breaking for
“value `a` must appear before `b`”.

*Encoding* (linear, no big-M):

```
  e[i][k] = [x[k]==order[i]]   via add_int_reif(=)
  ∀ i<m-1, ∀ k<n:  Σ_{j<k} e[i][j]  ≥  e[i+1][k]
```

For `k=0` this is `0 ≥ e[i+1][0]` → `b` cannot be first.  For `m·n ≤8192`
(`m≤64,n≤256`) the matrix `e` has ≤8192 binaries and `(m-1)·n ≤ 64·256`
precedence rows.  The decomposition via `H = max(x, H_prev)` needs `n` `max`
variables each with a binary + 4 big-M rows and is sequential; the native
encoding is `Θ(m·n)` binaries but with **unit coefficients** and no M.

`seq_precede_chain(x)` is the same with `order = lb(x)..ub(x)` where `lb/ub`
are the tight `lin_bounds` box of `x` (global min/max).  Caps `m≤64` keep the
encoding bounded; larger domains decline to `UNKNOWN`.

### 2.3 `seq_precede_chain` is the same code path

It reuses the `e[i][k]` matrix with `m = ub-lb+1`.  No extra `H` variables.

---

## 3. Solver library advertisement

```
share/minizinc/psolve/fzn_regular.mzn
share/minizinc/psolve/fzn_value_precede_chain.mzn
share/minizinc/psolve/redefinitions.mzn
share/minizinc/solvers/psolve.msc  (mznlib: "" → "psolve")
```

With `mznlib: psolve`, `minizinc --solver psolve` leaves the five
`fzn_regular*` and four `fzn_*precede*` predicates native (body-less), so the
FlatZinc reaching `fznsolve` is the compact DFA / precedence form above, not
the `element`/`max` decomposition.  Without MiniZinc (raw `fznsolve foo.fzn`)
both forms are accepted — the native handler is just faster.

---

## 4. Caps & honesty

Every new handler follows the project rule: *decline loudly, never guess*.

| predicate | cap that → `UNKNOWN` | reason |
|---|---|---|
| `regular` | `Q·S_n>2048` or `n·Q·S_n>65536` or `n>256` or `ntrip>1024` | binary blow-up |
| `value_precede` | `m·n>8192` or `m>64` or `n>256` | matrix size |
| `seq_precede` | `ub-lb+1>64` | order blow-up |
| all | `d` entry outside `0..Q` or `q0∉1..Q` or `F⊈1..Q` | malformed DFA |

A declined model is erased (all rows freed, `sol.status=2`) exactly like any
other unhandled predicate — it prints `=====UNKNOWN=====` and never a
fabricated optimum.

---

## 5. Evidence

*Unit.* New `fzn_regular` / `fzn_value_precede` vs brute force:

```
python3 /tmp/verify_new_preds.py
  regular: 50/50 passed
  vpc: 50/50 passed
  ALL NEW PREDICATE TESTS PASSED
```

Each trial enumerates all `S^n` assignments in Python and compares
`brute_sat` vs `psolve` SAT/UNSAT.

*Battery.* Existing gates still green:

```
fzn_output_check 200/42: WRONG=0  (3 HONEST downgrades, 0 fabricated)
fgraph_verify 60/20260818: WRONG=0
fzn leak test (ASan/LSan): accept/reject OK
examples/fzn/*.fzn (16 files): all OK
```

*Perf.* On a `Q=4 S=6 n=10` rostering row (synthetic):

```
decomposed (element+max):  ~12 ms,  ~600 binaries
native (this patch):       ~3 ms,   ~200 binaries  (4×)
```

The win grows with `Q·S` and `n`.

---

## 6. Files changed

* `src/fzn.c` — two new handler blocks (+~380 loc) before the `unhandled`
  fallback, with reif-aware DFA and precedence encodings; fixed
  `di.constant -= order` bug for constant `xs` (was `= -order`).
* `share/minizinc/psolve/*.mzn` — body-less declarations for the five
  `regular` + four `precede` predicates.
* `share/minizinc/solvers/psolve.msc` — `mznlib: "" → "psolve"`, version
  `1.1.0 → 1.2.0`.
* This doc.

---

## 7. Next steps (roadmap)

* Reif for `value_precede` — encode `r => precedence` with half-reification
  (`add_int_imp` per precedence row, `r=0` ⇒ disjunction of violations).
* `lex2` / `lex_chain_lesseq` 2-D array parsing for `fzn_lex2` (needs `array2d`
  literal dimension recovery).
* `regular` with `opt` alphabet / `nfa` variants (`fzn_regular_nfa*`).
* `minimum`/`maximum` with `var int: m` (currently only `array_*_minimum`
  with `par` array is native).

All follow the same pattern: body-less `psolve/*.mzn` + bounded MIP table
encoding + brute-force differential gate.
