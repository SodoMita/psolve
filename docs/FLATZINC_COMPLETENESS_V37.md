# FlatZinc Completeness v3.7 — set variables, native circuit, honest parse failures (2026-10-28)

**Branch:** `feature/flatzinc-v3-lex-argmin` → pushed to `origin/feature/flatzinc-v3-lex-argmin`
**Solver version:** `1.8.0` → `1.9.0` (`share/minizinc/solvers/psolve.msc`)
**Status:** landed. Full `test.sh` rc=0; **`mzn_real_diff` 43/43 real MiniZinc 2.10.1 models match Gecode exactly (WRONG=0 UNKNOWN=0 SKIP=0)**; new `set_var_verify` (680 cases), `circuit_verify` (160 cases) both WRONG=0; ASan/UBSan/LSan clean on 40 set/circuit cases; 77/77 benchmark suite.

---

## 1. What prompted this release

With a real MiniZinc 2.10.1 toolchain now part of the verification loop (v3.6),
probing the remaining documented gaps exposed three concrete defects rather
than hypothetical ones:

1. **Silent verdict.** Any `.fzn` containing a `var set of int` declaration
   made `fz_read()` fail; `tools/fznsolve.c` then did `rc=1; goto done;` with
   **no stdout at all**. MiniZinc sees neither a solution nor a status line,
   which is indistinguishable from an empty SAT set — a certificate-honesty
   violation. Reproduced with the stdlib `value_precede_chain` over
   `array[1..3] of var set of 1..3`.
2. **`circuit` at scale.** `minizinc --solver psolve` decomposed `circuit` via
   stdlib `fzn_circuit.mzn` into ~4830 `int_lin_ne` constraints for n=70 and
   timed out. Gecode ships a `fzn_circuit.mzn` shadow that emits one native
   predicate instead.
3. **Weak native circuit encoding.** Even when handed a native `fzn_circuit`,
   the v3.6 O(n²) assignment + MTZ MIP encoding solved n≤8 and timed out at
   n≥12: the LP relaxation is fast but branch-and-bound explodes.

## 2. What shipped

### 2.1 Honest parse failures

`tools/fznsolve.c` now prints `=====UNKNOWN=====` on stdout whenever
`fz_read()` fails, instead of exiting silently. An unreadable or unsupported
FlatZinc is not a solver failure and must never masquerade as one. Verified:
a `var set of 1..100` universe (beyond the 2^20 mask cap) now returns the
honest UNKNOWN rather than nothing.

### 2.2 `var set of U` decision variables

MiniZinc emits set decision variables for `value_precede_chain` over
`var set of int`, `fzn_regular_set`, and related globals. v3.7 adds:

* **Parsing** (`src/fzn.c`, `FZDecl` gains `is_set`, `set_univ`, `set_card`,
  `set_bitbase` and the new `FZ_K_SET` kind).  Handles `var set of lo..hi`,
  `var set of {a,b,c}`, and `array[...] of var set of int` alias views.
* **Exact encoding**: each set is ONE integer bitmask variable pinned to
  `|U|` fresh 0/1 membership bits (`mask = sum 2^i * bit_i`).  No big-M.
  A universe wider than 20 bits is declined honestly (UNKNOWN), never
  approximated.
* **Membership constraints**: `set_in_reif(k, S, r)` becomes `r = bit_j`
  (or `r = false` when `k` is outside `U`); `set_in(k, S)` pins `bit_j = 1`
  (or makes the model infeasible when `k` is outside `U`).
* **Output**: bitmask → FlatZinc set literal (`{}`, `{1,3}`, `1..3`),
  including arrays of sets whose elements alias separately-declared set
  scalars.  MiniZinc's `showDzn` normalizes these to the same form Gecode
  emits.

Verified: `set_var_verify.py` 680 randomized cases (6 seeds) with exact `-a`
solution-set equality vs brute force, WRONG=0; three real MiniZinc models
(`set_vpc1`, `set_vpc_tight`, `opt_vpc1`) match Gecode's full `-a` sets
(120/120, 20/20, 51/51).

### 2.3 Native circuit with CP subtour elimination

* **`share/minizinc/psolve/fzn_circuit.mzn`** (new shadow file) declares the
  1D body-less `fzn_circuit` builtin, so real `mzn2fzn` emits one
  `fzn_circuit(xs)` constraint instead of the O(n²) stdlib decomposition.
* **`src/fz_cp.inc`** gains `CPK_CIRCUIT`: a proper CP propagator combining
  all-different (Régin maximum matching), self-arc removal, and **chain-following
  subtour elimination** — a walk over fixed successors that rejects any cycle
  closing before all n nodes are visited and prunes the closing value at an
  unfixed tail.  `cp_verify_circuit` re-checks the full single cycle at every
  leaf, so a pruning bug cannot fabricate an answer.
* **`src/fzn.c`** MTZ order variables are now integer (tighter propagation);
  the MIP path remains as an honest fallback for cases CP cannot certify.

Result: n=70 solves in ~0.1s (was a timeout), verified to be a genuine
Hamiltonian cycle.  `circuit_verify.py` checks exact `-a` solution sets for
n=3..8 (including `gecode_circuit(0,x)` 0-based labelling, pins, forbids and
inequalities) and validates n=16/32/64/70 single solutions.  160 cases,
WRONG=0.

### 2.4 Verification loop

* `tools/mzn_real_diff.py` corpus grew 38 → **43** models: added set-based
  `value_precede_chain`, a tightened set model, `var opt int` `value_precede_chain`,
  a 5-node circuit (all 24 cycles, exact `-a` equality), and a 70-node pinned
  circuit (unique solution, exact `-a` equality).
* `tools/set_var_verify.py` and `tools/circuit_verify.py` are new hard gates in
  `test.sh`, alongside the v3.6 `pow_var_verify` and `mzn_real_diff` gates.
* `tools/mzn_setup.sh` (v3.6) was exercised from a cold state after a session
  snapshot: SHA-256-verified download into `~/.cache/psolve-mzn/`, and both the
  harness and `test.sh` auto-locate the toolchain there.

## 3. Verification

| check | result |
|---|---|
| full clean `make -s` | OK |
| `test.sh` (full, with new gates) | **rc=0** |
| `mzn_real_diff.py` (real MiniZinc 2.10.1 vs Gecode) | **OK=43 WRONG=0 UNKNOWN=0 SKIP=0** |
| `set_var_verify.py` (6 seeds, 680 cases) | WRONG=0 UNKNOWN=0 |
| `circuit_verify.py` (4 seeds, 160 cases + n=16/32/64/70) | WRONG=0 |
| `pow_var_verify.py` / `divmod_verify` / `lex_reif_verify` / `vpc_reif_verify` | 0 WRONG |
| `fzn_output_check 200/42` | OK=200 HONEST=0 WRONG=0 |
| `mzn_diff.py` / `mzn_bench.py` (real toolchain) | 33/33 · 77/77 |
| `tolsheet_check` | OK (108 ids, 187 sites, 108 rows) |
| `oom_test` | 5916 injection points, 0 failures |
| ASan/UBSan/LSan on 40 set/circuit cases | 0 issues |

## 4. Honesty / caps

Unchanged principle: unsupported shapes return `=====UNKNOWN=====`, never a
fabricated SAT/OPTIMUM.  New caps: set universe `|U| ≤ 20` (bitmask exactly
representable as a double, `2^20` masks); CP circuit `n ≤ 256`; MIP circuit
`n ≤ 64` (CP path handles larger).  Parse failures are now honest UNKNOWN.

## 5. Remaining

* `fzn_value_precede_chain_int_opt` variants with `var set of int` beyond the
  20-bit universe; `fzn_regular_set` / `fzn_regular_nfa*` (alphabet-set and
  NFA forms) still UNKNOWN.
* `element` sparse big-M for arrays larger than 2048 entries.
* Native 2D `lex2`/`lex_chain` handlers remain reachable from hand-written
  `.fzn`; real `mzn2fzn` routes those globals through the stdlib decomposition
  into 1D native lex comparisons.

## 6. How to run

```sh
make -s
bash test.sh                                   # rc=0
sh tools/mzn_setup.sh                          # one-time MiniZinc fetch
python3 tools/mzn_real_diff.py                 # 43/43 vs Gecode
python3 tools/set_var_verify.py 80 20261028    # brute-force set vars
python3 tools/circuit_verify.py 40 20261028    # brute-force circuit
minizinc --solver psolve examples/mzn/knap_lin.mzn
```
