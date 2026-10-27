# psolve 1.2.0 — FlatZinc completeness v2 (regular + value_precede)

This workspace continues development of https://github.com/SodoMita/psolve to make it
the best MiniZinc solver. The delta from 1.1.0 (merged PR #4, dual simplex) to
1.2.0 is **FlatZinc completeness v2**: native MIP encodings for the DFA and
precedence globals that dominate rostering and symmetry-breaking.

## What changed (1.2.0)

* **Native `regular` family** — `fzn_regular`, `fzn_regular_set`, `fzn_regular_reif`,
  `fzn_regular_set_reif` (and `regular` alias) via per-position transition-table
  encoding with state variables `a[0..n]`. DFA `Q·S_n ≤2048`, `n·Q·S_n ≤65536`.
  Reif is `b ↔ a[n]∈F` via reified equalities. `n=0` collapses to `q0∈F`.

* **Native `value_precede_chain` / `seq_precede_chain`** —
  `fzn_value_precede_chain_int(c,x)` and `fzn_seq_precede_chain_int(x)` via
  `e[i][k]=[x[k]==order[i]]` and `Σ_{j<k} e[i][j] ≥ e[i+1][k]`. `m·n ≤8192`,
  `m≤64`. Reif variants are recognized and honestly declined.

* **Solver library** — `share/minizinc/psolve/` with body-less predicate
  declarations and `redefinitions.mzn`; `share/minizinc/solvers/psolve.msc`
  `mznlib: "" → "psolve"` so `minizinc --solver psolve` keeps these globals
  native.

All caps are honest `UNKNOWN` — the handlers never allocate unbounded encodings.

## How to verify

```sh
make -s
./fznsolve examples/fzn/*.fzn   # 16/16 still OK
python3 /tmp/verify_new_preds.py  # 50/50 regular + 50/50 vpc vs brute force
python3 tools/fzn_output_check.py 200 42  # WRONG=0
```

See `docs/FLATZINC_COMPLETENESS_V2.md` for the full evidence roll-up,
encoding details, and next steps.
