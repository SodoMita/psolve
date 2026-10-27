# FlatZinc Completeness v3.5 — lex, arg_min/max, 2D arrays, int_times/div/mod, cumulative, alldifferent_except, native reification (2026-10-27)

**Branch:** `main` → `feature/flatzinc-v3-lex-argmin`
**Solver version:** `1.2.0` → `1.7.0` (`share/minizinc/solvers/psolve.msc`)
**Status:** landed. `make -s` clean; `fzn_output_check 200/42` and `500/123` `HONEST=0 WRONG=0`; `fgraph_verify 60 WRONG=0`; `cumulative_verify 200/200`; `divmod_verify 228/228`; `lex_reif_verify 180/180`; `lex_reif_differential 56/56`; `vpc_reif_verify 63/63`; `16/16 examples/fzn/*.fzn` OK.

> **Superseded in part by v3.6** — see
> [`FLATZINC_COMPLETENESS_V36.md`](FLATZINC_COMPLETENESS_V36.md). v3.5's 2D
> body-less mznlib declarations were invalid FlatZinc builtins under real
> MiniZinc, so `minizinc --solver psolve` could not compile any model. v3.6
> replaces them with Gecode-style file-name shadowing, adds a real-MiniZinc
> differential harness, and closes variable-exponent `int_pow`. All v3.5 C
> handlers and verifiers still apply unchanged.

---

## 1. Motivation

v2 closed `regular` + `value_precede_chain`. MiniZinc Challenge still hit `UNKNOWN` on lex_chain/lex2, minimum_arg, bilinear int_times, int_div/mod with variable divisor, cumulative large horizon (>512) and large total_vars (>2048) disjunctive, alldifferent_except with arbitrary except sets, and **reified** lex/value_precede constraints.

v3.5 ships native MIP encodings for all of the above plus native reification.

## 2. What shipped

### 2.1 2D declaration & `parse_array2d`

`FZDecl` 2D, `parse_array2d` via `'('` scan fix.

### 2.2 `lex_post` + `lex_chain` / `lex2`

`e=[x==y]`, `l=[x<y]`, `p_eq`, `b_diff`, final `sum=1`. `lex_chain` per column, `lex2` rows+cols. Fixed `strcmp`→`strstr` for `fzn_lex2_int` (80/80 PASS).

### 2.3 `minimum` / `maximum` aliases

Selector MIP via `fzn_minimum_int`.

### 2.4 `arg_min` / `arg_max`

`m <= x_j`, `b_j=[i==base+j-1]`, `b_j => x_j==m`, tie-break `x_k ≥ m+1` for `k<j`. "Least index breaks ties" confirmed against official MiniZinc docs (subagent audit).

### 2.5 `int_times` bounded enumeration (v3.1→v3.3)

`a·b==c` with `na≤32, nb≤32, na·nb≤1024`: enumerate `da·db==dc`, encode as table. Cap on `nc` removed — product domain may be `-100..100` (`nc=201`) with still 441 rows.

### 2.6 `int_div` / `int_mod` lattice (v3.3)

Constant divisor via `a=K·q+r` with `nonneg ↔ a≥0`. Variable divisor `na≤32,nb≤32,na·nb≤1024`: enumerate `da div db` (trunc-toward-0) and `da mod db`, encode as table.

### 2.7 `cumulative` compressed + disjunctive (v3.2→v3.4)

Pritsker `z[i][k]=[s_i==k]`, `∑r·z ≤ B` at each `t`. Caps `total_vars>2048` or `T>512`. **Compressed fallback:** `T>512` but `total_vars≤2048` → enforce only at `t∈Tvals=∪[lo_i,hi_i]`. **Disjunctive fallback:** `total_vars>2048` but `B==1`, all `r_i==1`, `n≤64` → pairwise big-M:
```
o=1 ⇒ s_j ≥ s_i+d_i  → s_j - s_i - M·o ≥ d_i - M
o=0 ⇒ s_i ≥ s_j+d_j  → s_i - s_j + M·o ≥ d_j
M = (maxEnd-minStart)+maxD+10
```
Verified `0..1000×3 d10 B1 total_vars3003 → SAT`; non-disjunctive `B=3` large → honest `UNKNOWN`.

### 2.8 `alldifferent_except` generic (v3.2)

`alldifferent_except(arr,S)` with `S` literal `{0,1}` or `1..2` via `fz_parse_int_set`; for each `v∉S` in `[dlo,dhi]` (`ndom≤128`): `z_i=[x_i==v]`, `∑z_i≤1`.

### 2.9 Native reification of lex + value_precede (v3.5) — NEW

**Wrong-answer bug fixed:** `fzn_lex2_reif(xs,b)` was swallowed by the `strstr(p,"lex2")` branch, which enforced lex2 UNCONDITIONALLY and ignored `b`. With `b=false` the solver returned a lex2-satisfying matrix — a WRONG answer.

**`lex_core`:** refactored `lex_post` into a shared core. With `S_out!=NULL` it does NOT enforce but returns a fresh 0/1 variable `S` pinned to `S == sum(b_diff)+allow_eq·p_eq[n-1]`. All internal `e/l/p_eq/b_diff` are EXACT integer reifications, so this sum is a deterministic 0/1 function of the two vectors → `S` is a fully-reified success indicator with **no big-M**. The enforce path posts byte-for-byte the historical rows (80/80 gate unaffected).

**Reified lex family** (`fzn_lex_lesseq_int_reif`, `fzn_lex_chain_lesseq_int_reif`, `fzn_lex2_reif`, strict + bool + `_imp` variants):
```
b <-> AND_i S_i :   b - S_i <= 0  (all i)
                    sum(S_i) - b <= K-1
_imp (b -> cond):   only b - S_i <= 0
```
Handles constant/affine reif vars via `lin_materialize` + clamp to [0,1]. `K==0` (1×1) pins `b=1`. Authoritative MiniZinc semantics confirmed: `lex_chain_lesseq` orders adjacent **columns**, `lex2` = columns ∧ rows (transpose).

**`precede_rows`:** shared helper for `value_precede_chain` / `seq_precede_chain`. Each precedence row `sum_{j<k} e[i][j] - e[i+1][k] >= 0` has min `-1` → exactly reifiable with `add_int_reif(which=3, d>=0)`. Full reif = AND of per-row indicators `h_i=[row_i>=0]`; half reif = `add_int_imp` per row.

**Second wrong-answer bug fixed (found by opencode subagent audit):** the vacuous shortcut in value/seq_precede used `nxs<=1`, but a single-element array is NOT vacuous — `value_precede_chain([1,2],[2])` is FALSE (value 2 appears with no earlier 1). Changed to `nxs<=0` in both enforced and reified paths. Fixed `b=false`/UNSAT repros.

`.mzn` library (`fzn_lex.mzn`, `fzn_value_precede_chain.mzn`) now declares all reif/imp forms so MiniZinc routes them natively instead of boolean-decomposing.

### 2.10 Solver library & verification
```
share/minizinc/psolve/*.mzn  (redefinitions, body-less native predicates)
tools/lex_reif_verify.py       (subagent-written; single-sol + brute feasibility, 180 cases)
tools/lex_reif_differential.py (strict/_imp + `-a` full solution-SET equality, 56 cases)
tools/vpc_reif_verify.py       (value/seq_precede reif/imp, `-a` set equality, 63 cases incl n=1)
test.sh                        (all three wired as hard gates)
```

## 3. Evidence

* **Build:** `make -s` green (gcc, no warnings).
* **Brute force / differential:** `lex_chain` 16/16+64/64, `lex2` 16/16+64/64; arg_min 8 fixed; int_times/div/mod lattice; cumulative compressed+disjunctive.
* **Reification:** `lex_reif_verify 180/180`, `lex_reif_differential exact_set=32 unsat=24 WRONG=0 UNKNOWN=0`, `vpc_reif_verify exact_set=63 WRONG=0 UNKNOWN=0`; constant-reif-var edge cases `29/52` lex2-true/false assignments match exactly; shared-bool two-reif case `13/13` exact.
* **Subagents (opencode):** 3 auditors + 1 writer. Confirmed arg_min tie-break matches MiniZinc, found no other correctness bugs, wrote `tools/lex_reif_verify.py`, and the fourth audit found the `nxs<=1` wrong-answer bug (fixed).
* **Battery:** `16/16 examples/fzn/*.fzn`; `fzn_output_check 200/42` & `500/123 WRONG=0 HONEST=0`; `fgraph_verify 60 WRONG=0`; `cumulative_verify 200 MISMATCH=0`; `divmod_verify 228 WRONG=0`.

## 4. Caps & honesty

Honest `return 1 UNKNOWN` when `Q·S>2048`, `nrows·ncols>4096`, `ndom>128`, `total_vars>2048` (unless disjunctive), `na·nb>1024`, `n>256`, `norder>64`, `norder·nxs>8192`, etc. Reif bool clamped to [0,1]. No synthetic `OPTIMUM`.

## 5. Remaining

* `fzn_value_precede_chain_int_opt` / `..._set` (var opt int / var set — absent values via `default def`).
* `element` sparse via big-M (`>2048` array → UNKNOWN); `circuit` n>64; `fzn_regular_reif` with opt alphabet / NFA variants.

## 6. How to run

```
make -s && ./fznsolve examples/fzn/*.fzn
python3 tools/fzn_output_check.py 200 42   # HONEST=0
python3 tools/lex_reif_differential.py     # WRONG=0
python3 tools/vpc_reif_verify.py           # WRONG=0
```
