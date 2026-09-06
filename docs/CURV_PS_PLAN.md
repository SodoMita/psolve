# Improving psolve for `curv-ps` (browser Curv + `solve { }` layout blocks)

Status: analysis + prioritised change list. Produced by reading
[`SodoMita/curv-ps`](https://github.com/SodoMita/curv-ps) at `7a02593` (dev
round 11) together with its vendored wasm bridge, and by measuring psolve's own
QP core on that workload (`make ui-probe`, `tools/ui_qp_probe.c`).

Nothing in `src/` is changed by this document. The probe tool is added so the
findings are reproducible and so a fix has an acceptance test.

---

## 0. What curv-ps actually consumes

| psolve file | used by curv-ps | notes |
|---|---|---|
| `src/err.c`, `kernels.c`, `lu.c`, `splu.c`, `solver.c`, `qp.c` | ✅ compiled into `src/psolve/psolve.wasm.b64.ts` | LP + convex QP only |
| `src/mip.c`, `fzn.c`, `fx.c`, `pgs.c`, `pgs_fixed.c` | ❌ not in the wasm build | MIP/CP/exact/physics never ship to the browser |
| `src/parser.c`, `main.c` | ❌ | the browser never parses an `.lp` file; flat arrays are marshalled |

The whole browser surface is **two entry points** (`psolve-src/psolve_web.c`):

```c
psw_lp_solve(n, m, c, colptr, row, val, rel, b, l, u, maximize, x, obj, iters)
psw_qp_solve(n, m, Q, c, A, b, x, obj, iters)     /* qp.x0 = NULL, res.mult dropped */
```

That is the entire contract, and it is where most of the gap lives: psolve's C
API already has warm starts (`QP.x0`, `solver_warm_solve`), duals
(`solver_duals`, `QPResult.mult`), a cooperative stop (`psolve_stop_fn`), a
zero-malloc arena, and reduced costs — **none of it is reachable from JS.**

Their side (`src/psolve/constraints.ts`) then does three things a solver should
do: a Gaussian presolve that substitutes away hard equalities (because `QP`
has no equalities), folding soft equalities into `Q` (because encoding them as
error rows was unreliable), and adding a `1e-7` ridge (because their `Q` is
otherwise singular, which psolve would flag `QP_NON_CONVEX`). None of that is
wrong, but it is ~150 lines of numerics living in the front end, with absolute
tolerances of its own.

### Baseline: how big are their problems, really

Measured headlessly through their own pipeline (`Interp` + wasm, 900×600
viewport, 9 alternating frames per example):

| example | solves | model vars | cons | eliminated | iters | ms first | ms drag avg | status |
|---|---|---|---|---|---|---|---|---|
| dashboard | 9 | 36 | 38 | 35 | 2 | 1.70 | 0.05 | OPTIMAL |
| stacks | 9 | 72 | 74 | 70 | 1 | 0.05 | 0.03 | OPTIMAL |
| buttons | 9 | 24 | 29 | 19 | 8 | 0.12 | 0.11 | OPTIMAL |
| toolbar | 9 | 136 | 148 | 126 | 12 | 1.98 | 0.34 | OPTIMAL |
| wrapfit | 9 | 145 | 156 | 135 | 12 | 0.27 | 0.20 | OPTIMAL |
| tooltip | 9 | 8 | 13 | 6 | 1 | 0.02 | 0.01 | OPTIMAL |
| chart | 9 | 53 | 66 | 52 | 2 | 0.22 | 0.12 | OPTIMAL |

**Conclusion: psolve is not their bottleneck today** (≤ 2 ms/solve, and mostly
cache-hit). The problems are small *because psolve forces them to be*: their
`flow_fit`/`fit_labels` bisection machinery, the "never go infeasible at any
viewport" rule, and the "10 % of toolbar items get dropped" adaptation all
exist to keep the model inside the size and shape that psolve's QP can survive.
That is the thing to fix: not microseconds, but **capacity, robustness and
diagnostics**, so the front end can delete its defensive scaffolding.

---

## 1. Measured capacity/robustness curve (psolve QP core, this problem class)

`tools/ui_qp_probe.c` builds the model their front end emits — a 1-D flow of
`N` chips: `x_0 = 0`, `x_{i+1} = x_i + w_i + gap`, `w_i >= minw`,
`x_{N-1} + w_{N-1} <= maxw`, soft `w_i ~= nat`, plus the `1e-7` ridge. Native
`-O3 -march=native`, 2-core sandbox; expect the same order in wasm.

| N | vars | rows | cold ms/frame | warm ms/frame (`x0` = last frame) | cold doubles JS marshals |
|---|---|---|---|---|---|
| 8 | 16 | 25 | 1.1 | 0.07 | 656 |
| 16 | 32 | 49 | 10.5 | **0.46** | 2 592 |
| 32 | 64 | 97 | 122 | **4.8** | 10 304 |
| 64 | 128 | 193 | > 1 500 (budget) | ~100 | 41 088 |
| 128 | 256 | 385 | ~9 900 | ~2 200 | 164 096 |

Two facts dominate everything below:

1. **Cold start = the dense Phase-I feasibility QP.** With `x0 == NULL` the
   start point `x = 0` violates `w_i >= minw`, so `find_feasible()`
   (`src/qp.c:352`) builds a *dense* `(n+m)×(n+m)` auxiliary QP with `2m` rows
   and runs the active set on it. Its own per-iteration cost then dwarfs the
   real solve. Re-running the identical frame from the previous answer skips it:
   **23–35× faster, `max|Δx| = 0`** (warm and cold agree to the last bit at
   these sizes).
2. **The active-set iteration itself is `O((n+k)³)` from scratch.** Measured
   ms/iteration (cap = 40 iterations, so this is pure marginal cost):
   `0.04 → 0.48 → 3.9 → 32 → 347 → 4569` for `N = 8 → 256`. `solve_kkt()`
   callocs `2·(n+k)²` doubles, copies them, factors, and re-checks residuals on
   *every* iteration; `build_orth()` rebuilds the whole orthonormal working set
   (`O(n·k²)`) after every add/drop. No factorisation updates anywhere.

And the correctness cliff, reproduced at three unrelated model shapes:

```
[1] encoding equivalence -- same QP, three encodings, one verdict
   N   Q-folded  error-var   eq-pairs           obj(Q)       obj(err)   obj(pairs)
   4    OPTIMAL    OPTIMAL    OPTIMAL    -25599.998199       0.001801     0.001801
   8    OPTIMAL    OPTIMAL    OPTIMAL    -51199.984548       0.015452     0.015452
  12    OPTIMAL    OPTIMAL    OPTIMAL    -76799.945505       0.054495     0.054495
  16    OPTIMAL INFEASIBLE INFEASIBLE   -102399.867528       0.000000     0.000000
  FAIL N=16 encoding 1: model is feasible by construction but solver said INFEASIBLE
  FAIL N=16 encoding 2: model is feasible by construction but solver said INFEASIBLE
```

The objective values show all three encodings are the *same* problem, and the
probe proves the verdict is wrong by handing `qp.x0` a feasible point it wrote
down by construction.  Note the shape of that point: every lowered equality is
*tight*, so the feasible set is an affine sheet with empty interior -- the
generic case for a front end that lowers `a == b` into two inequalities, not a
pathological input.  A solver may legitimately answer `ITER_LIMIT` or "no
feasible start found" there; `INFEASIBLE` is a false statement about the model. (`x_i = i·(nat+gap)`, `w_i = nat`) — from that start the
solve is `OPTIMAL` with the identical objective. So the model is feasible, the
solver says `INFEASIBLE`, and the failure is inside Phase-I, not in the
formulation. Section [2] of the probe shows the same model's verdict flipping
with the *unit of measure* (`1e-3` ok, `1e+6` `INFEASIBLE`), which is what an
absolute tolerance does:

* `find_feasible`'s ridge is a hard `eps = 1e-6` on `‖x‖²` while the objective
  is `Σ s`. At pixel scale (`‖x‖ ~ 1e3`) the ridge term is `~1`, competing with
  `Σ s ~ 1e2`, so the slack is never driven to 0 and the `ssum <= 1e-7` test
  rejects the answer.
* The working-set activity test `row_resid > -1e-7` and `tolrank = 1e-9` are
  absolute against rows whose coefficients are `±1` and right-hand sides are
  `1e2..1e6`.

I tried the obvious local repairs (relative activity tests, a scale-derived
`eps`, relative slack budget, Jacobi-equilibrated KKT rows with `μ` unscaled,
a regularisation ladder relative to `‖Q‖∞`). Outcome: **the small-scale
failures disappear, large-scale ones appear** (e.g. `N=16, scale 1e+3`
flipped from `OPTIMAL` to `INFEASIBLE`). Conclusion recorded so nobody re-does
it: tolerance tuning relocates this cliff, it does not remove it. The dense
Phase-I has to go.

---

---

## 1.9 Status of P0.1 and P0.2 on this branch (implemented)

Both are in `src/qp.c` / `src/qp.h` with the LP core now linked into the QP
(`Makefile`: `QPSRC`/`QP_LIB_SRC` gained `src/solver.c src/splu.c`), and both are
gated by `test.sh` step `[5.1/7]`.

**How the implementation differs from the sketch above, and why:**

* **No new `QP_NO_FEAS_START` status.** `tools/qp_diff.py` -- the repo's own
  differential oracle -- already treats `-1` as "no feasible start" and
  cross-checks it against an exact LP; introducing a second code for the same
  fact would have re-classified every `-1` in the existing corpus. Infeasibility
  is therefore *not* a status: it is `status == -1 && res.infeasible_proven`,
  with `res.farkas` the certificate. A caller that wants the old vocabulary
  prints `psw_qp_verdict_name()`, which says `NO_FEASIBLE_START` or
  `INFEASIBLE_PROVEN`.
* **The dense auxiliary QP stays, as a fallback, and runs first.** The order is
  `x0` (verified) -> dense search -> LP. Handing the active set a simplex vertex
  instead of the dense search's well-centred point was measured to be *worse*:
  `N=8` at scale 1e3 went from `OPTIMAL` in 16 iterations to `ITER_LIMIT` in
  8100. The LP's value here is verdicts and rescue, not a faster start.
* **The LP needs a box, and the box must be proven idle.** QP variables are
  free, and free columns reach the simplex as `x+ - x-` at the `LP_INF` (1e30)
  bound, whose difference is `O(1)`: `solver_feasible()`'s own final certificate
  then fails and the LP answers `NUMERICAL_FAILURE` on 11 of 16 models in this
  family. Clipping each column to `1e6*(1+|b|max)/max|A.j|` fixed it (16/16),
  and because the *right* width is data-dependent the search runs a ladder
  (`1e9, 1e6, 1e3, 1e12`). A clipped model cannot certify the unclipped one, so
  a Farkas vector is only read off the LP when the box was untouched at the
  optimum (complementary slackness, checked at 1/4 width); when the box binds,
  the simplex is asked for the certificate *directly* instead, as Farkas'
  alternative itself (`min b'y s.t. A'y = 0, sum y >= 1, 0 <= y <= 1`), which
  needs no box at all. Every candidate is re-verified on the caller's data, in
  any of the four sign conventions the LP driver might be using, before it is
  reported -- so a solver that misbehaves costs a rescue, never a verdict.
* **Warm-start acceptance is relative** (`qp_start_feasible`,
  `1e-11*(1 + |b_i| + |A_i|*|x|)`), with a deliberately looser
  `1e-8*(...)` gate for a point the LP hands over, since the simplex itself only
  answers at `1e-9` absolute.

**Measured, this branch vs `HEAD` (`tools/ui_qp_probe`, `tools/qp_diff.py`):**

| check | before | after |
|---|---|---|
| [1] three encodings, N=4..40 | disagree; `INFEASIBLE` for N>=16 | `OPTIMAL` everywhere, objectives agree to 1e-6 |
| [2] scale sweep (N=8,16; 1e-6..1e6; dup rows) | ~1/3 of points claimed `INFEASIBLE` for feasible models | **no false infeasibility anywhere**; 12 ok, 6 `NO_START`, 6 `KKT_FAIL`, 4 `ITER_LIMIT` |
| [5] infeasible model, N=4..24, both encodings | `-1`, no certificate ("give up") | `-1` + verified Farkas vector, 12/12, `A'y` residual <= 6e-16, certificate names 2N+1 conflict rows |
| [3] 60-frame drag, N=8/16/32 | cold only | cold 1.59/17.55/228.8 ms -> warm 0.08/0.66/7.28 ms, `max|dx| = 0 px` |
| [4] N=64 under a 1500 ms budget | ran past it / no verdict | `STOPPED` at 1509 ms with the incumbent kept |
| `qp_diff.py 200 4242` (their oracle) | `checked=162 WRONG=0` | `checked=165 WRONG=0`; the same two honest `-1` cases, both genuinely infeasible |

**Still open inside P0's scope** (deliberately not papered over): [2]'s
`KKT_FAIL`/`ITER_LIMIT` at 1e3-1e6 and with duplicated rows is the active set
itself -- no null-space step and a dense KKT refactorisation per iteration -- so
it is P1.1/P3 territory, not Phase-I. `NO_START` at a handful of sweep points is
the LP declining to certify; it now says so honestly. And the `n^2` marshalling
per frame (P1.2) is untouched: at N=32 the front end rebuilds 10 304 doubles per
frame, which is a larger per-frame cost than the solve at N=16.

**`tools/psolve_web.c`** (with `psolve_web.h`, `wasm_build.sh`, `psw_test.c`) is
the psolve-owned bridge: `psw_qp_solve2` takes `x0` and returns `max_resid`,
`psw_qp_proven`/`psw_qp_farkas` expose the certificate,
`psw_set_time_budget_ms`/`psw_*_solve_b` give a wall-clock budget that the solver
polls per iteration (the piece a browser host cannot install itself, since
`psolve_stop_fn` is a C pointer), and `psw_qp_status_name`/`psw_qp_verdict_name`
keep "give-up" and "proof" apart in the strings a UI shows. It also documents the
two hazards found while writing it: `qp_result_free()` zeroes the result (so
`status` read after the free is `0` == `OPTIMAL` -- now said in `qp.h`), and an
exhausted arena has no libc fallback and aborts the host, so
`psw_arena_highwater()` exists instead of a closed-form size bound.

---

## 1.10 The frame budget must actually bind (P0.3's first half, done)

`psw_set_time_budget_ms` (and `qpsolve -t`) arm a wall-clock callback that the QP
core polls once per active-set iteration.  That polling is not what limits a
solve; the limit is the longest *uninterruptible* stretch of arithmetic, and
Phase-I had an enormous one.  A cold layout model, one solve, 16 ms budget
(`gcc -O2 -march=native -I src <harness> src/qp.c src/{solver,splu,lu,kernels,err}.c
-lm`, harness = the section-1 model plus a deadline in `psolve_stop_fn`):

| n+m | model | before (dense Phase-I first, no gate) | after (gate at n+m ≤ 600) |
|---|---|---|---|
| 81 | N=16 | 16.3 ms (1.0×) | 16.3 ms |
| 161 | N=32 | 48 ms (3.0×) | 56 ms |
| 321 | N=64 | 723 ms (45×) | 682 ms |
| 641 | N=128 | **11.4 s (715×)** | **178 ms** |
| 1281 | N=256 | **180 s (11 283×)** | **48 ms** |

Cause: the dense auxiliary QP lives in `n+m` variables, so *one* of its
iterations is an O((n+m)^3) dense KKT factorisation that no stop poll can
interrupt.  Above the gate only the sparse LP route is attempted, whose cost per
pivot is O(nnz); the budget then binds within a pivot or two.  Same sweep, no
budget (batch): N=128 went 63.9 s → 4.7 s **with the same answer** (status
OPTIMAL, obj −819126.900598, 510 iterations both ways) — at those sizes the
dense Phase-I *was* the runtime, not the solve.

What is still open in P0.3, with the numbers behind it:

* **N=32..120 still overshoots 3-43×**, entirely inside the dense search (the
  gate is off below 600 on purpose).  Running the LP route first cuts N=64's
  overshoot 682 → 95 ms and makes N=64 fit a 1500 ms solve outright
  (`OPTIMAL`, 283 ms, where dense-first was `STOPPED`), **but it loses rescues**:
  on the scipy differential sweep it checks 158 vs 162 models at n=200 and 293
  vs 306 at n=400, each lost model ending as `NO_FEASIBLE_START` (never a wrong
  verdict).  Buying speed with verified solves is a policy for the caller, so
  this needs an explicit knob (`qp_prefer_lp_start`, or a `QP.start_hint`), not a
  guess.  **Do not key it on whether a stop callback is installed**: `tools/qpsolve.c`
  installs one with no deadline, just to be Ctrl-C-safe, so "armed" ≠ "in a
  hurry" and a batch caller's answers would change underneath them.
* The floor of the residual overshoot is *one main-loop iteration*, not Phase-I:
  with the LP route first (so Phase-I costs one sparse pivot sequence) N=64 still
  lands at 95 ms against a 16 ms budget, because the active set's own dense
  `(n+k)^3` factorisation is ~30-100 ms at those sizes.  An iteration cap on the
  auxiliary solve does not buy this (at 30 ms a time it would need a cap of ~1 to
  bound a frame, which is below what the search needs to converge), so the real
  fix is the sparse KKT factorisation in P1.3, plus the start-order knob above.
* Still open: iteration caps as the deterministic budget (needed for the
  reproducible-drag-film claim) and the persistent handle (P0.2 half).

## 1.11 Reconciling with `arena/cp-engine-correctness`

That branch is where the engine work is happening: **13 commits ahead of `main`,
0 behind** (tip `a468a23`, 2026-08-19), so `main` is a stale base — its tip is one
of that branch's merges.  Counting matters here: of the 16 other branches on the
remote, **11 are already fully merged into `main`** (`exactness-and-status-audit`,
`flatzinc-float-correctness`, `fzn-all-solutions`, `fzn-set-const-ops`,
`fzn-set-status-ports`, `phase4-ports`, `unmerged-audit-and-correctness`,
`finite-domain-cp-engine`, `flatzinc-complete`, `fzn-table-constraint`, `mzfnsh`)
and 3 carry small tails of superseded drafts (`audit-hardening` 3,
`phase4-interactive-hardening` 3, `continue-hardening` 2 — their arena / FBBT /
time-limit / FlatZinc-audit work landed on `main` rewritten, patch-id differs,
content does not).  Nothing on any of them is needed before merging this: `main`
already carries "QP: never report an infeasible or non-optimal point as solved"
(the origin of `tools/qp_diff.py`) and "Fix LP Phase I degeneracy", and no branch
has anything resembling the wasm bridge — `tools/psolve_web.[ch]`,
`tools/wasm_build.sh` and `tools/ui_qp_probe.c` exist on this branch only.  (The
ahead/behind numbers here are from an unshallowed clone: the sandbox clone is
shallow, and a truncated history makes every branch look ~100 commits ahead of
`main`.  Verify with `git rev-list --count origin/main..BR`, not by counting log
lines.)  This branch is 2 commits of QP/bridge work on top of
`main`, deliberately **not** rebased onto their line, so it stays reviewable
against `main`.  What merging takes is mechanical, and it has been measured
rather than predicted: their tree was checked out in a worktree, this branch's
`36e7680` cherry-picked with `-n`, and every gate run on the merged result.

* **6 conflict hunks in 5 files** (`src/qp.c` doc comment + tolerance-tag lines,
  `src/qp.h` two doc paragraphs, `Makefile` the `qpsolve` target and `clean`,
  `test.sh` where the `qp_diff` gate block is appended, `tools/fuzz_inputs.py`
  link list).  Both sides' edits survive in every case; nothing semantic collides.
* Their link lines already include `src/solver.c src/splu.c src/cert.c`, so this
  patch's `test.sh`/Makefile/`fuzz_inputs.py` additions are redundant there (drop
  them in the merge); `src/qp.c`'s new dependency on the LP core is already
  satisfied on their line.
* **Their Phase 6.3 replaced the global `psolve_stop_fn` with
  `psolve_stop_set(fn)`** (per-thread, no getter).  Any tool that saved and
  restored the global has to own its slot instead: `psolve_stop_set(my_hook)` on
  entry, `psolve_stop_set(NULL)` on exit.  Two files here need that one-line
  change (`tools/psolve_web.c`, `tools/ui_qp_probe.c`); after it the bridge test
  is 42 checks, 0 failures on the merged tree.
* **Their `tools/tolsheet_check.py` gate rejects this patch as-authored**: every
  code line in `src/*.{c,h}` carrying a float-exponent literal must be tagged
  `/* TOLSHEET TOL-… */`, and every tag must have a row in `docs/DESIGN.md` §8
  (and vice versa — no stale rows).  The tags are done on this branch (15 sites
  in `src/qp.c`); the rows below are paste-ready for their §8.3 sheet.  Values
  are as measured, and the classes are theirs: `C` steers convergence, `S` is a
  scaling/division guard, `V` gates a verdict or a certificate.

  | ID | site | value | class | direction of safety / protects / may never |
  |---|---|---|---|---|
  | TOL-QP-WARMFEAS | qp.c:69 | 1e-11·(1+\|b_i\|+Σ\|a_ij\|\|x_j\|) | V | accepts the caller's x0 as a start; loose enough to keep usable warm starts, tight enough that the active set never starts infeasible (its own primal gate is the backstop) |
  | TOL-QP-P1HANDOVER | qp.c:90 | 1e-8·row_scale | V | "feasible enough to hand to the active set" for a point the LP produced; deliberately looser than WARMFEAS (different arithmetic), never looser than the terminal primal re-check |
  | TOL-QP-BOX | qp.c:513 | boxmul·(1+max\|b\|) | C | bounds the free variables the Phase-I LP needs; exists only in an auxiliary problem, so it may never be quoted as a bound of the caller's |
  | TOL-QP-BOXLADDER | qp.c:673 | {1e9, 1e6, 1e3, 1e12} | C | box scales tried in turn; any single scale is a coin flip on ill-conditioned rows (12/16 vs 16/16 on the sweep), so no entry is canonical |
  | TOL-QP-BOXDIV | qp.c:518 | 1e-300 | S | row-coefficient guard turning the slack box into a variable bound; protects division only — enlarging it shrinks the box |
  | TOL-QP-BOXCAP | qp.c:520 | 1e100 | S | keeps a derived bound representable; measured: a 1e18 box is worse than 1e12, and ±DBL_MAX on free x must never be used (it makes the LP report infeasible) |
  | TOL-QP-BOXIDLE | qp.c:563 | 0.25 | V | "box untouched, so the boxed optimum says something about the unboxed system" test; errs toward refusing the claim |
  | TOL-QP-FARKASINIT | qp.c:418 | 1e300 | S | sentinel for the max/min scan, never a tolerance |
  | TOL-QP-FARKASSIGN | qp.c:428 | −1e-12·max\|λ\| | V | λ ≥ 0 acceptance; may never be loosened past zero — a certificate with a negative λ proves nothing |
  | TOL-QP-FARKASCOL | qp.c:440 | 1e-7 | V | ‖Aᵀλ‖∞ ≈ 0, recomputed from the caller's own A |
  | TOL-QP-FARKASB | qp.c:443 | −1e-9·(1+Σλ\|b\|) | V | bᵀλ < 0, same source; with the two rows above it is the entire `INFEASIBLE` claim, so `qpsolve` may print it only when all three hold |
  | TOL-QP-P1RIDGE | qp.c:712 | 1e-6 | C | ridge making the dense auxiliary QP strictly convex/bounded; must stay far below the slack objective's scale or it moves the optimum and P1VERIFY rejects the point |
  | TOL-QP-P1SUM | qp.c:745 | 1e-7 (absolute) | V | total slack of the candidate; absolute because the auxiliary problem has its own units — P1VERIFY is what ties it to the caller's |
  | TOL-QP-P1VERIFY | qp.c:752 | 1e-7·(1+\|b_i\|) | V | re-check of the candidate against the caller's rows, in the caller's scale; the reason P1SUM may stay absolute |
  | TOL-QP-P1DENSE | qp.c:802 | n+m ≤ 600 | C | size gate on the dense search (policy threshold, not arithmetic): below it the centred start wins, above it one iteration exceeds a frame budget (11.4 s vs 20 ms measured) |

* **Overlap to resolve in favour of theirs, not by keeping both**: their
  `solver_farkas_duals()` and `solver_farkas_boxcert()` do, inside the LP core,
  what this patch's `farkas_verify()` guesses from the outside (a four-way
  sign/search over the duals) and what `TOL-QP-BOXIDLE` refuses.  On a merged
  line the LP-side helpers should feed the QP certificate, and `src/cert.h`
  should gain `PSVK_QP_INFEASIBLE` carrying `QPResult.farkas` — their kinds list
  has `QP_OPTIMAL`/`QP_UNBOUNDED` but no infeasible QP certificate, which is
  exactly the hole P0.1 filled.
* **Measured on the merged tree** (this patch on their tip): `make all` clean;
  their full `test.sh` green end to end — including `[5.1/7]`, which is this
  branch's probe section 1 + section 5 + the bridge contract test, `[5.2/7]`
  cooperative stop, `[5.3/7]` re-entrant zero-malloc arena, `[5.45/7]` the
  caller-owned error frames that replaced the stop global, `[7/7]` 120 malformed
  inputs under ASan/UBSan, and their own `qp_psd_verify` (242 checks,
  **0 fabricated `status 0`**), `lp_scale_verify` (250 checks, 60 rescued,
  60 promoted to honest), `farkas_verify` (156 checks, farkas fired 23) — with
  the sole exception of `tolsheet_check`, i.e. the tag/doc closure above.  Their
  Phase 7.5 Ruiz equilibration and this Phase-I neither rescue nor break each
  other: the ill-scaled sweep comes out with an identical verdict mix from both
  trees (12 ok / 6 NO_START / 6 KKT_FAIL / 4 ITER_LIMIT over 30 points).
* **This patch is not redundant on their line.** Their `tools/qpsolve.c` no
  longer *labels* an unconstrained start "infeasible" (it prints `STATUS -1`,
  which is honest), but nothing in their tree proves a QP infeasibility or
  re-verifies a Phase-I point — their `cert.h` has no QP-infeasible kind and
  their Phase 6.1 "exact-or-UNKNOWN" work is LP/MIP/FlatZinc only.  The
  certificate, the box ladder and the residual reporting are new there too.

## 2. P0 — three changes that buy the most for the least risk

### P0.1  `INFEASIBLE` must mean *proven* infeasible (kill the dense Phase-I QP)  — implemented, see §1.9

* **Problem.** A feasible model returns `INFEASIBLE` (above). In the browser
  that is not a warning: `src/curv/interp.ts:975` does
  `if (!res.ok) throw err(...)`, so the canvas goes to an error line. It also
  contradicts psolve's own doctrine (the LP side refuses to report
  `OPTIMAL` when `factor_failed`; the QP reports a *definitive* verdict from a
  search that merely gave up).
* **Change.** Route Phase-I through the sparse simplex that is already in the
  wasm build, and only ever claim infeasibility from a certificate:
  *Phase-I* = `min Σ s  s.t.  A x − s ≤ b,  s ≥ 0` (plus the caller's bounds),
  solved with `solver_create/solver_solve`.
  * `status OPTIMAL, Σs ≈ 0` → feasible start (verify per row, relative to
    `|b_i| + |A_i|·|x|`, as today).
  * `status OPTIMAL, Σs > 0` → the LP dual gives a **Farkas certificate**
    (`y ≥ 0, Aᵀy = 0, yᵀb < 0`) → report `QP_INFEASIBLE` and *return the
    certificate* (`psw_qp_infeasibility_proof`).
  * anything else (`ITER_LIMIT`, `STOPPED`, numerical) → new status
    `QP_NO_FEAS_START` ("could not find a feasible point; not proven
    infeasible"), with the least-infeasible iterate in `x`.
  On my ad-hoc LP encoding of the same row set the solve took **4.2 ms at
  N=32 vs 243 ms for the dense QP Phase-I**; my throwaway harness then hit the
  simplex iteration cap at other sizes, so this is a "route it properly with
  the dual simplex" item, not "paste in the LP" item.
* **Acceptance.** `./ui_qp_probe --strict` sections [1] and [2] green; the
  three encodings of every `qp_gen`/`qp_diff` instance agree; a genuine
  infeasible QP ships a checkable Farkas vector (verify with `fxsolve` on the
  dual, matching the repo's "certificates decide" rule).

### P0.2  Warm start through the bridge (`x0`) — the 23–35× one  — implemented, see §1.9

* **Problem.** `psw_qp_solve` hardcodes `qp.x0 = NULL`, so every frame is a
  cold start and pays Phase-I. psolve already accepts `x0`; the front end has
  nothing to hand it (and `constraints.ts` caches results per fingerprint but
  keeps no last-solution state).
* **Change (bridge, ~10 lines, no core change).**
  `psw_qp_solve(..., double *x0, int x0_is_feasible, double *max_resid_out)`.
  When `x0 != NULL` *and* it verifies feasible within
  `tol·(1 + |b_i| + |A_i|·|x|)`, `find_feasible` returns immediately — that
  fast path already exists (`src/qp.c:356`), it just needs the argument.
  Return `max_resid` so the caller can *decide* whether its cached point is
  usable instead of trusting it. Longer term, a persistent handle
  (`psw_qp_create/set_b/solve/destroy`) is what unlocks the real prize:
  reusing the working set and factorisation across frames instead of just the
  point (this is exactly roadmap §13.2 "MPC sequence API", see §4).
* **Acceptance.** probe section [3] shows `warm ≤ cold/10` with
  `max|Δx| = 0`; a curv-ps-side test drags a widget through 30 frames and
  asserts pixel-identical layout vs. per-frame cold solves.

### P0.3  Budgets that actually bind, and determinism-friendly ones  — the binding part is done (see §1.10): the wall-clock budget now survives Phase-I; the start-order knob, iteration caps and the persistent handle are open

* **Problem.** In the browser there is no `SIGALRM`, so the cooperative stop is
  unreachable (`psolve_stop_fn` is a C function pointer — JS cannot set it),
  and the polling granularity is per *iteration*: at `N=128` one iteration
  costs ~350 ms, so a "16 ms" budget overran to 9.8 s in my measurement. A UI
  thread cannot use a guarantee that is 600× off.
* **Change.** (a) `QP.max_iter` (and `LP.iteration_limit`, defaulting to
  2 000 000 today at `src/solver.c:228`) settable across the bridge —
  an *iteration* budget is deterministic across machines, which matters to
  curv-ps because its memo/fingerprint caches and `paramcheck` assume
  reproducibility; (b) keep the wall-clock hook but also poll the stop flag
  inside `solve_kkt` (per LU column) and inside Phase-I, so sub-iteration
  budgets bind; (c) document "STOPPED hands back the best incumbent, `x` may
  be null if it stopped inside Phase-I" (already the behaviour, and already
  tested by `tools/qp_stop_test.c`).
* **Acceptance.** a `qp_stop_test` case at N=128 with `max_iter=50` returns in
  under 2× the measured time of 50 iterations, twice in a row, bit-identically.

---

## 3. P1 — make the model shape the right one

### P1.1 Equalities and variable bounds in the QP (then delete their presolve)

`QP` is `min ½xᵀQx + cᵀx  s.t. Ax ≤ b` — no equalities, no bounds. Consequences
measured in their code:

* hard equalities are eliminated in JS by a dense Gaussian pass
  (`constraints.ts:eliminate`), which is 40 lines of solver in the front end,
  uses an absolute `1e-9` pivot threshold and an absolute `1e-6`
  inconsistency test — i.e. it can declare "INFEASIBLE (equality system)" with
  `amount: Infinity` for a merely ill-conditioned layout, *before psolve is
  ever called*;
* soft equalities must be folded into `Q` because the error-row encoding was
  unreliable (their comment says so; §1 reproduces it);
* every `x ≥ 0` / `w ≤ maxw` box becomes a dense row of `A`, inflating `m` and
  the `(n+k)³` factorisation.

**Change:** add `QP.Aeq, beq, l, u` (or a `QPEx` + `qp_solve_ex`, keeping the
v1 struct for the physics/CLI callers). Equalities enter the working set
permanently and are never dropped; bounds are handled in the ratio test like a
bound-constrained simplex, so they cost **no rows at all**. This removes the
duplicated `≤`/`≥` pairs that are the degeneracy source curv-ps keeps running
into, and shrinks `m` by 2–3× for typical layout models. Cross-check: the same
model solved as {eq-as-pairs} vs {native eq} must agree in verdict and
objective — that becomes a new probe encoding (`ENC_NATIVE_EQ`).

### P1.2 Sparse QP input; stop shipping `n²` doubles per frame

The bridge marshals dense `Q` (`n²`) and dense `A` (`m·n`) every solve, and
`qp_solve` validates all `n²` entries (`src/qp.c:420`). At `N=32` that is
10 304 doubles/frame, at `N=128` 164 096 — and JS allocates them as plain
`number[]` first. Layout `Q` is *never* dense: it is a diagonal (ridge +
per-variable quadratic terms) plus one rank-1 update per soft constraint, which
is exactly how `constraints.ts` builds it (`Q[i*n+j] += 2*w*ti*tj`).

**Change:** accept `Q = D + Σ_k w_k a_k a_kᵀ` (diagonal + rank-1 list) and `A`
in CSC (the LP already takes CSC, so `kernels.c` has the mat-vec patterns to
reuse). Mat-vec becomes `O(nnz)`, the input copy becomes `O(n + nnz)`, and the
`n > 8192` guard (which is meaningless in a 256 MB wasm instance where `Q`
alone would be 536 MB) can be replaced by an arena/byte budget.

### P1.3 Diagnostics that make the front end debuggable

Today the browser gets one `int` status back: `res.mult` is dropped, no
residuals, no conflict set, and `lpsolve` has no `--iis`. So a failed layout
says `solve on line 41 failed: KKT_FAIL (psolve/QP)`.

**Change:** `psw_qp_solve` also writes `mult` (already computed!), the max and
per-row residuals, and the index of the worst-violating row; the LP/QP gain
`solver_conflict_rows()` / `qp_conflict_rows()` (roadmap §17.1's deletion
filter — for the QP, run it on the Phase-I LP where the rows are already
there). curv-ps can then highlight *the three constraints that cannot hold*
instead of a status pill. This is the highest-leverage UX item in the list and
touches no numerics.

---

## 4. P2 — contract, robustness and packaging of the wasm path

| # | item | why it matters for curv-ps |
|---|---|---|
| P2.1 | **Verdict/incumbent/certificate as three separate bits.** `psolve.ts` marks `ok: st===0 \|\| st===2` (so `ITER_LIMIT` counts as fine but `STOPPED`, which *also* hands back a feasible incumbent, does not) and `interp.ts` throws on `!ok`. Add `certified_optimal` + `incumbent_feasible` flags and let the host keep the last good layout while showing "approximate". | their examples are only "never infeasible" because a non-`OPTIMAL` status is fatal to the whole program evaluation |
| P2.2 | **No `exit()`/`abort()` inside the wasm module.** The bridge never arms `psolve_try()`, so `psolve_fail()` reaches `exit(code)` (`src/err.c:33`) → WASI `proc_exit` → the JS shim throws from the middle of a solve and every allocation that solve made is leaked in a 32 MB-initial/256 MB-max linear memory; `sjlj_stub.c`'s `longjmp` is `abort()`, i.e. a trap. Add a `PSOLVE_NO_SJLJ` build (checked-alloc failures propagate as codes, `psolve_code` polled by the bridge → returns `QP_INVALID`/`OOM` status) — mechanical, and it also gives the browser a recoverable OOM path. | one oversized model currently risks permanently poisoning the instance |
| P2.3 | **Arena ownership after `psolve_arena_end()` is a trap.** `psolve_free` treats "not inside the *active* arena" as a libc pointer (`src/err.c:183`), so `qp_result_free()` called after the scope pops does `free()` on an interior arena pointer: ASan `attempting free on address which was not malloc()-ed` (I hit this writing the probe; in wasm it is silent linear-memory corruption, not a crash). Either keep a thread-local registry of *live* arenas so any arena-owned pointer is a no-op free, or add `psolve_arena_release()` and say it in the header. | per-frame zero-malloc solves are exactly what the bridge would want to do next; today it is one natural-looking line from memory corruption |
| P2.4 | **Don't advertise the arena for the QP path as a perf win.** Measured, same model: `N=32` cold 234 ms with libc malloc vs **277 ms** with a 1.5 GB arena; `N=16` 17.2 vs 22.4 ms. The QP callocs `2(n+k)²` fresh doubles per `solve_kkt` call, so a bump allocator just re-starts a bigger zeroing job. Fix the churn (buffer reuse inside `active_set`, which the single-allocation comment in `solve_kkt` was reaching for) rather than the allocator. | prevents a plausible-looking "use the arena in wasm" change that would ship a regression |
| P2.5 | **Reproducible artifact.** `psolve.wasm.b64.ts` (263 KB) is committed with no build script, no CI job and a *branch name* in prose as its provenance (`psolve-src/README.md`). Move the build here: `tools/wasm_build.sh` (exact flags, embeds `git describe`, prints sha256), `make wasm`, a wasm CI cell running `qp_test` + `ui_qp_probe --fast` under wasmtime, and an exported `psolve_version()`. curv-ps then pins the sha256 and asserts it at load. | their "unmodified psolve sources" claim is currently unverifiable, and any drift between blob and sources is silent |

---

## 5. P3 — what to pull forward from the roadmap, and what to leave alone

`docs/ROADMAP_AMBITIOUS.md:821` says the VG/UI track is
*"Track 17.x by host pull — kernels own no frame budget until a consumer
exists"*. **curv-ps is that consumer**, so this document is the pull request
for: §17.1 (IIS → P1.3), §13.1 (proximal/ADMM engine), §13.2 (sequence API →
the persistent-handle version of P0.2), §17.3 (lexicographic multi-objective),
§13.4 (box-QP unification). Two of these deserve a specific note:

* **§13.1 first-order engine is the real cure for the degeneracy class.** An
  OSQP-shaped ADMM makes no rank/nondegeneracy assumption at all: one fixed
  sparse factorisation, `O(nnz)` per iteration, warm start native, always
  returns primal/dual residuals so "KKT_FAIL" becomes "ε-optimal at
  iteration 200". Keep the active set as the *exact* tier (small models,
  certificate) and pick per-size: their `toolbar`/`wrapfit` examples (≈20–40
  reduced vars) stay on the active set; anything with 100+ soft rows goes
  ADMM and gets finished/verified by the active set.
* **Ship `pgs.c`/`pgs_fixed.c` in the wasm build** (`psw_pgs_solve`). It is
  already in-tree, zero-malloc, fixed-budget and (in the `_fixed` twin)
  bit-deterministic — an "animate at any size" tier and a fallback whenever the
  exact solve says `QP_NO_FEAS_START`. It is also the cheapest way to give
  curv-ps the *same* solve on server and client for shared/replayed documents.
* **Priority hierarchy (§17.3) instead of weight arithmetic.** Their handoff
  records the symptom: *"Conflicting slack pulls compromise in the ratio of
  their squared weights: body-hug `weight 4` vs `weak` settles ≈ 15:1"* — a
  UI author should write `required > strong > medium > weak`, not discover
  exponents. `STRENGTH` in `constraints.ts` is already a hierarchy in disguise.
* **Batch entry point** (`psw_qp_solve_batch(k, …)`) for their
  140..900 × 320..700 viewport sweeps: same sparsity pattern across the grid,
  so the factorisation is built once and each viewport becomes one
  `O(nnz)`-per-iteration solve. That turns their soak test from minutes to
  seconds and gives §13.2 its second consumer.

**Leave alone:** the ridge-vs-`QP_NON_CONVEX` tension (a `rank_deficient`
*warning* flag alongside `QP_NON_CONVEX` is the honest version — then their
front end can stop silently regularising); SIMD/`-O3` wasm flag tuning (the
win is structural: skipping Phase-I); `--ffast-math` (kills the residual
checks that are the only thing making `OPTIMAL` meaningful); adding a
`minizinc`-style parser to the wasm build (JS marshals flat arrays already —
keep the browser entry point flat and typed); and "make QP faster for the
current examples" (they are at 0.05–2 ms/solve; §0).

---

## 6. What curv-ps should change regardless (no psolve dependency)

1. **Normalise rows before calling.** Divide each row by `max_j |a_ij|` and
   scale `b` with it (and keep `weight` in the same units). Their variables are
   pixels (`1e0..1e3`) and psolve's working-set tolerances are absolute, so
   ~10 lines in `constraints.ts` move them off every cliff in §1 today.
2. **Recover from `INFEASIBLE` instead of throwing.** `interp.ts:975` — keep
   the previous frame's values, mark the block amber. Their
   `flow_fit`/`fit_labels` bisection is a *good* design (decide numerically,
   solve positions) and should stay, but it should be a quality knob, not the
   thing preventing a red screen.
3. **Keep the JS presolve, but make its thresholds relative** — and when it
   returns `null`, do *not* short-circuit to `INFEASIBLE, amount: Infinity`;
   hand the unreduced system to psolve and let the (P0.1-)certified verdict
   decide. A `1e-6` absolute inconsistency test on pixel-scale data is a
   front-end false negative waiting to happen.
4. **Cache the last solution per fingerprint and pass it as `x0`** (needs the
   10-line bridge param, P0.2). Expect ~25× on drag frames.
5. **Regenerate the wasm blob from a pinned psolve tag** once P2.5 lands, and
   show `psolve_version()` + the sha256 in the SolverPanel — the trace table is
   the right place for "which solver produced this layout".

---

## 7. Reproducing this document's numbers

```sh
# psolve side (native):
make ui_qp_probe && ./ui_qp_probe            # full, minutes: 5 s budget per case
./ui_qp_probe --fast                         # ~8 s, what `make ui-probe` runs
./ui_qp_probe --strict --only 1 --nmax 24    # 0 -- encoding equivalence (P0.1 gate, in test.sh)
./ui_qp_probe --strict --only 5              # 0 -- certified infeasibility (P0.1 gate, in test.sh)
./ui_qp_probe --strict ; echo $?             # 1 while [2] keeps KKT_FAIL/ITER_LIMIT at 1e3-1e6
make bridge-test                            # native build + run of tools/psw_test.c (the wasm ABI)
./tools/wasm_build.sh                       # needs emcc or wasi-sdk; --check works without either

# budget boundedness (section 1.10) is a third variant of the same harness:
# one cold solve per N, psolve_stop_fn polling a CLOCK_MONOTONIC deadline, print
# (elapsed - budget) and the status; compare a build of src/qp.c against a build
# of `git show <before>:src/qp.c` in the same tree.
# marginal cost per active-set iteration, LP-vs-QP Phase-I, and the libc-vs-arena
# comparison in section 1/4 all come from variants of the same harness: replace
# the wall-clock deadline in psolve_stop_fn with a poll counter (a deterministic
# iteration cap) and divide time by the cap.  Those variants are intentionally
# not committed -- ui_qp_probe is the one that should become a permanent test.

# curv-ps side: their own pipeline, headless, through the committed wasm blob.
# Save as scripts/solvebench.ts (it does not exist in that repo yet) and run
# `npx tsx scripts/solvebench.ts` -- it prints the table in section 0:
```ts
import { createCanvas } from "@napi-rs/canvas";
(globalThis as any).document = { createElement: (t: string) =>
  t === "canvas" ? (createCanvas(1, 1) as any) : (() => { throw new Error(t); })() };
import { loadPsolve } from "../src/psolve/psolve";
import { Interp } from "../src/curv/interp";
import { EXAMPLES } from "../src/curv/examples";
import { buildAtlas } from "../src/gpu/atlas";

await loadPsolve();
const atlas = buildAtlas();
console.log("example      solves  vars cons elim iters  ms(first) ms(drag) status");
for (const ex of EXAMPLES) {
  const first: number[] = [], drag: number[] = [];
  let nVars = 0, nCons = 0, elim = 0, iters = 0, status = "no-solve";
  for (let f = 0; f <= 8; f++) {                       // 8 viewport-changed frames
    const VW = 900 - f * 30, VH = 600 + f * 12;
    const it = new Interp(atlas, { viewport: { x: -VW / 2, y: -VH / 2, w: VW, h: VH },
                                   time: f * 0.1, mouse: { x: 0, y: 0, down: false } });
    let r; try { r = it.run(ex.src); } catch (e: any) { status = "THROW: " + e.message; break; }
    const tr = r.traces.filter((t) => t.engine !== "presolve");
    if (!tr.length) break;
    for (const t of tr) {
      (f ? drag : first).push(t.timeMs); iters = t.iterations; status = t.status;
      nVars = Math.max(nVars, t.nVars); nCons = Math.max(nCons, t.nCons); elim = Math.max(elim, t.eliminated);
    }
  }
  const avg = (a: number[]) => (a.length ? a.reduce((x, y) => x + y, 0) / a.length : NaN);
  if (first.length || drag.length)
    console.log(`${ex.id.padEnd(12)} ${(first.length + drag.length).toString().padStart(4)}` +
      ` ${String(nVars).padStart(6)}${String(nCons).padStart(5)}${String(elim).padStart(5)}` +
      `${String(iters).padStart(6)} ${avg(first).toFixed(2).padStart(9)} ${avg(drag).toFixed(2).padStart(9)}  ${status}`);
}
```
```

Baseline provenance: `psolve@main = c34db8c` (merge of
`arena/cp-engine-correctness`), which is exactly the branch curv-ps'
`psolve-src/README.md` says the blob was built from. x86-64 sandbox, 2 cores,
`-O3 -march=native`, gcc 13; browser wasm typically within 1–2× of these
numbers for this double-precision, memory-bound code.
