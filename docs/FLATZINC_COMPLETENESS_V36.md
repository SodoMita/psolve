# FlatZinc Completeness v3.6 — real MiniZinc compatibility, native-routed globals, variable-exponent `int_pow` (2026-10-27)

**Branch:** `feature/flatzinc-v3-lex-argmin`
**Solver version:** `1.7.0` → `1.8.0` (`share/minizinc/solvers/psolve.msc`)
**Status:** landed. `make -s` clean; full `test.sh` rc=0; **`mzn_real_diff` 38/38 real MiniZinc models match Gecode exactly (WRONG=0 UNKNOWN=0 SKIP=0)**; `pow_var_verify` 120/120 (+ 750 extra seed cases) WRONG=0; `mzn_diff.py` 33/33; `mzn_bench.py` 77/77; ASan/UBSan/LSan clean on 100 `int_pow` runs and all 38 real-`mzn2fzn` outputs.

---

## 1. The bug that motivated this release

All v3.0–v3.5 work was validated by feeding **hand-written `.fzn`** directly to
`./fznsolve`. Nobody had run the actual MiniZinc compiler against the shipped
solver configuration. Installing a real toolchain (MiniZinc 2.10.1 + Gecode
6.4.0 + Chuffed + CP-SAT) exposed a **complete integration failure**:

```
$ minizinc --solver psolve t1.mzn
Multiple type errors:
Error: type error: Type array[int,int] of var int is not allowed in as a
FlatZinc builtin argument, arrays must be one dimensional
.../psolve/fzn_lex.mzn:...
```

`share/minizinc/psolve/{fzn_lex,fzn_regular}.mzn` declared **body-less**
predicates taking 2D arrays (`fzn_lex2`, `fzn_lex_chain_*`, `fzn_regular`,
and their `_reif`/`_imp` forms). In MiniZinc, a body-less predicate in a
solver's `redefinitions.mzn` means "this solver implements this FlatZinc
builtin natively"; FlatZinc builtins may only take **one-dimensional** arrays.
MiniZinc ≥ 2.8 type-checks every such declaration at load time, so the
shipped config could not compile **any** model — trivial arithmetic included.

Two secondary config defects were found at the same time:

* `mznlib: "psolve"` resolved relative to the `.msc` directory to
  `share/minizinc/solvers/psolve`, which does not exist (the library lives at
  `share/minizinc/psolve`). Gecode uses `"../gecode"` for exactly this layout.
* `make install_mzn` copied only `psolve.msc` into `~/.minizinc/solvers/` and
  never copied the mznlib directory, so an installed config had no library at
  all.

## 2. What shipped

### 2.1 Correct native routing via file-name shadowing

MiniZinc resolves `include "fzn_X.mzn"` against the solver mznlib **first**.
Gecode and Chuffed exploit this to replace a stdlib decomposition with their
own native propagator. v3.6 adopts the same mechanism.

`redefinitions.mzn` now declares only legal **1D** body-less builtins:

* `fzn_value_precede_chain.mzn` — enforced / `_reif` / `_imp` chain and
  `fzn_seq_precede_chain_int` family.
* `fzn_lex.mzn` — 1D `fzn_lex_lesseq_int` / `fzn_lex_less_int` / bool forms
  and their reified / half-reified variants.
* `fzn_arg_min_max.mzn`, `fzn_min_max.mzn` — unchanged (already 1D).

Twelve new **shadow files** are named exactly like the stdlib decomposition
files they replace (`fzn_lex_lesseq_int_reif.mzn`,
`fzn_value_precede_chain_int_reif.mzn`,
`fzn_seq_precede_chain_int_reif.mzn`, etc.). Each contains only a 1D body-less
declaration, so real `mzn2fzn` emits the native predicate instead of
decomposing it. This matters especially for reified
`value_precede_chain`/`seq_precede_chain`: the stdlib implementations are
literally `abort("Reified ... is not supported")`, so **Gecode itself cannot
compile those models** while psolve now solves them natively.

`fzn_regular.mzn` follows the Gecode pattern:

```minizinc
predicate psolve_regular(array[int] of var int: x, int: Q, int: S,
                         array[int] of int: d, int: q0, set of int: F);
predicate fzn_regular(array[int] of var int: x, int: Q, int: S,
                      array[int,int] of int: d, int: q0, set of int: F) =
  psolve_regular(x, Q, S, array1d(d), q0, F);
```

The file deliberately does **not** set `fzn_regular_is_decomposed = true`, so
the stdlib's `fzn_regular_reif.mzn` takes its augmented-DFA branch and posts a
non-reified `fzn_regular` on the enlarged automaton — which lands on the
native propagator. `src/fzn.c` now dispatches `psolve_regular` /
`psolve_regular_reif` to the existing regular handler (which already parses a
flat transition table via `table_values`).

`psolve.msc` is now `1.8.0` with `mznlib: "../psolve"`, and `make install_mzn`
copies the whole library to `~/.minizinc/psolve`. Verified end-to-end with a
simulated `HOME`.

### 2.2 Variable-exponent `int_pow`

Real `mzn2fzn` emits `constraint int_pow(x, e, y)` with all three operands
unit vars on small finite domains whenever a model uses `pow(x,e)` with a
decision-variable exponent. v3.5 returned `UNKNOWN` for the whole family;
Gecode solves it. v3.6 adds a bounded-lattice table encoding in `src/fzn.c`,
mirroring the v3.3 `int_times` / `int_div` encodings:

* Enumerate every `(base, exp)` lattice point (`na≤32`, `nb≤32`,
  `na·nb≤1024`), compute the exact result, and encode feasible triples with
  selector binaries plus three linking equalities.
* Semantics verified against Gecode 6.4.0 and
  `docs.minizinc.dev/en/stable/lib-flatzinc-int.html`:
  `z = x^y` for `y≥0` (with `0^0 = 1`), and `z = 1 div pow(x,|y|)` for
  `y<0` (trunc toward zero). For `y<0` that means `1` for `x=1`,
  `(-1)^|y|` for `x=-1`, `0` for `|x|≥2`.
* `x=0, y<0` is undefined (`1 div 0`). MiniZinc's `pow_t` wrapper adds the
  totality guard `x != 0 ∨ y >= 0`, so the cell never appears in real models;
  the encoding skips it rather than fabricating a value. An overflowed power
  (`|base^exp| > 2^53`) is likewise skipped as infeasible, since the result
  variable's domain is capped well below that (`|z| ≤ 1e9`). Caps keep the
  MIP honest; larger lattices stay `UNKNOWN`.
* Constant-exponent fast paths (v3.3) are unchanged and still take priority.

### 2.3 Real-MiniZinc differential harness (new, hard gate)

`tools/mzn_real_diff.py` builds a temp solver config from the **shipped**
`.msc` + mznlib, compiles a 38-model corpus of real `.mzn` sources, and
compares against Gecode:

* `solve satisfy` → exact `-a` **solution-set equality**.
* `solve minimize/maximize` → `--output-objective`, compare the optimum
  (chosen over the old `%%mzn-stat` parsing, which is solver-specific and
  caused a false WRONG on `arg_max`).
* Covers linear arithmetic, all_different, element, table, circuit,
  cumulative, regular + reified regular, lex_lesseq/less, lex2, lex_chain,
  value_precede_chain, value_precede, seq_precede_chain, arg_min/arg_max,
  min/max, nvalue, among, global_cardinality, bin_packing, inverse,
  sliding_sum, increasing, bool clauses, reified linears, set_in, `int_pow`
  (constant **and variable** exponent), int_times, int_div, int_mod, plus
  three UNSAT models.

`tools/mzn_setup.sh` downloads and SHA-256-verifies the MiniZinc 2.10.1 CLI
bundle into `~/.cache/psolve-mzn/` (idempotent). The harness and `test.sh`
skip gracefully when no toolchain is present, so a zero-dependency build is
unchanged. Both `pow_var_verify.py` and `mzn_real_diff.py` are wired into
`test.sh`.

### 2.4 Tolerance-sheet closure repair

`tools/tolsheet_check.py` was already failing on `main` (four untagged
tolerance literals plus two undocumented `TOL-LP-DUAL*` tags). v3.6 tags the
new `0x1p53` sites with the existing `TOL-FZN-POWCAP`, tags the two `1e18`
sentinels with the existing `TOL-FZN-BOUNDINIT`, and adds the two missing
`docs/DESIGN.md` § 8 rows. `tolsheet_check` now reports
`OK (108 ids, 187 tagged sites, 108 documented rows)`.

## 3. Verification

| check | result |
|---|---|
| `make -s` (clean rebuild) | OK |
| `test.sh` (full) | **rc=0** |
| `mzn_real_diff.py` (real MiniZinc 2.10.1 vs Gecode) | **OK=38 WRONG=0 UNKNOWN=0 SKIP=0** |
| `pow_var_verify.py 120 20261027` | OK=120 WRONG=0 UNKNOWN=0 |
| `pow_var_verify.py 150` × 5 other seeds | 750/750 OK |
| `mzn_diff.py` (legacy, real toolchain) | 33/33 |
| `mzn_bench.py` (77-model suite) | 77/77 |
| `fzn_output_check 200/42`, `500/123` | OK=200/500, HONEST=0 WRONG=0 |
| `fgraph_verify`, `cumulative_verify`, `divmod_verify` | 0 WRONG |
| `lex_reif_verify`, `lex_reif_differential`, `vpc_reif_verify` | 0 WRONG |
| `16/16 examples/fzn/*.fzn` | OK |
| `tolsheet_check` | OK (108 ids) |
| ASan/UBSan/LSan on 100 `int_pow` runs + 38 real `.fzn` | 0 issues |
| installed layout (`make install_mzn` into fake HOME) | compiles + solves |

## 4. Honesty / caps

Unchanged from v3.5: unsupported shapes return `=====UNKNOWN=====`, never a
fabricated SAT/OPTIMUM. New caps: `int_pow` lattice
`na≤32, nb≤32, na·nb≤1024, |base|≤1e6, |exp|≤1000, |z|≤1e9`, and exact
power magnitude `≤ 2^53`.

## 5. Remaining

* `fzn_value_precede_chain_int_opt` / `..._set` (var opt int / var set).
* `element` sparse via big-M (`>2048` array → UNKNOWN); `circuit` n>64;
  `fzn_regular_nfa*` / regular with opt alphabet.
* Native 2D `lex2`/`lex_chain` handlers in `src/fzn.c` remain reachable from
  hand-written `.fzn`, but real `mzn2fzn` routes those globals through the
  stdlib decomposition into 1D native lex comparisons.

## 6. How to run

```sh
make -s
bash test.sh                                  # rc=0
sh tools/mzn_setup.sh                         # one-time: fetch MiniZinc 2.10.1
python3 tools/mzn_real_diff.py                # 38/38 vs Gecode
python3 tools/pow_var_verify.py 120 20261027  # brute-force int_pow
minizinc --solver psolve examples/mzn/knap_lin.mzn
```
