# psolve — Response to External Audit (`PSOLVE_AUDIT.md`, findings F-1..F-7)

**Date:** 2026-08-15
**Branch:** `arena/cp-engine-correctness`
**Fix commit:** `cb186f2` ("External audit round: F-1..F-7 dispositions + a
self-found identifier-domain hole")
**Audited target:** `main` @ `248eb2f` (unchanged since the audit)

This file maps every finding of the external audit to its disposition in the
project's established correctness-first style: reproduce the wrong answer on a
pre-fix binary → fix → regression-lock with a discriminating differential test
**proven to fail on the pre-fix binary** → full battery → sanitizer sweep →
documentation. All numbers in §"Actual result" were re-measured this round on
the committed tree; nothing is asserted from memory or from commit messages.

---

## 1. Per-finding disposition

| Finding | Disposition | Where |
|---|---|---|
| **F-1** LP reader merges duplicate triplets wrongly (wrong `OPTIMAL` objectives) | **Fixed** | `lp_read` canonical merge at CSC materialization in `src/parser.c`; same merge in `solver_create_internal` (`src/solver.c`) for API callers; differential lock `tools/lp_form_verify.py` (family A), wired into `test.sh` |
| **F-2** Phase-2 unbounded ray falsely certifies a Phase-1-infeasible LP as `UNBOUNDED` | **Fixed** | conductive-ray certificate gated on a *re-verified* Phase-1 before any `UNBOUNDED` verdict in `solver_solve_impl` (`src/solver.c`); differential lock `lp_form_verify.py` (family B) |
| **F-3** `fz_read` leaks memory on partial/malformed models | **Fixed** | `src/fzn.c` error paths now free the partial model (incl. the `expr_free2` empty-container leak); hard LSan gate `tools/fz_leak_test.c` in `test.sh` |
| **F-4** `fz_read` unchecked `ftell` | **Fixed** | `src/fzn.c`: `ftell < 0` handled explicitly |
| **F-5** missing threading guarantees note | **Fixed (docs)** | `README.md` API/threading notes |
| **F-6** AGPL compliance ambiguity for hosts | **Fixed (docs)** | `README.md` license/AGPL note |
| **F-7** `mipsolve` does not show the solution | **Fixed (works-as-intended, UX gap closed)** | `tools/mipsolve.c --print` accepted in any argument position and dumps optimal values; documented in `--help`/README |
| **AAS** (self-found during F-3 verification): `var foo..bar: q;` — identifier domain silently swallowed into the ±1e9 sentinel box | **Fixed** | `fz_read` rejects identifier domains unless declared; regression inside `fz_leak_test.c` case table |

**New-test discrimination (the rule of this project: a regression test must be
proven to fail on the pre-fix binary):**

| Test | Pre-fix binary | Post-fix |
|---|---|---|
| `lp_form_verify.py` (l>u boxes + duplicate triplets vs scipy/HiGHS) | **FAILED 60/80** (58 family + 2 adversarial instances wrong; adversarial pin objective `147.33333333333334` vs correct `4.0` — the F-1 value class) | **OK=302 WRONG=0** (N=150, seed 20260815) |
| mip_verify family B pins (`(2^62)/g` rhs patterns, hardness mirrored) | pre: `UNSATISFIABLE` (wrong — the proven-optimal reference is 110.0; the dangerous direction, no verifier guards fabricated UNSAT) | post: `objective: 110` (matches lp_relax/certificate reference 110.0) |
| mip_verify family C pins (`(2^63,2^64,big/2,...)` sentinel limits) | pre: garbage objective `1397419117.880728` printed as optimal | post: `INFEASIBLE` — **correct** (the audit's own Farkas certificate proves infeasibility) |

---

## 2. Primary code change

`/tmp/primary_code_change.txt` (651 lines, 458 additions) consolidates the
fix-only diff: `src/parser.c`, `src/solver.c`, `src/fzn.c`, `tools/mipsolve.c`,
`tools/lp_form_verify.py` (new), `tools/fz_leak_test.c` (new), `test.sh`,
`README.md`. Regenerate with:

```sh
git clone -q https://github.com/SodoMita/psolve.git /tmp/upd && cd /tmp/upd
git checkout arena/cp-engine-correctness
git show cb186f2 -- src/parser.c src/solver.c src/fzn.c tools/mipsolve.c \
    tools/lp_form_verify.py tools/fz_leak_test.c test.sh README.md \
    > /tmp/primary_code_change.txt
```

Short excerpts (see the diff for full context):

**F-1 — canonical triplet merge in `lp_read` (`src/parser.c`).** During CSC
materialization an epoch-marker scan detects already-seen `(row, col)`
coordinates; duplicates are summed with checked arithmetic into the first
occurrence rather than conflated per-slot. The same canonical merge runs in
`solver_create_internal` so the C API (`solver_set coefficients` path) cannot
smuggle in the same class of input:

```c
/* F-1: canonical merge of duplicate triplets: sum values for repeated
 * (row,col) coordinates instead of conflating them. Overflow in the merged
 * sum (checked via isfinite) is a parse error, not silent saturation. */
```

**F-2 — ray-certificate gate (`src/solver.c`).** A Phase-2 unbounded-ray
certificate is no longer sufficient for `UNBOUNDED`: the solver first
re-verifies Phase-1 fea­sibility of the original bounds/rows. Contradictory
boxes (`lo > hi`, incl. via `solver_set_bounds`) are scanned up front in both
`solver_solve_impl` and `solver_warm_solve` and reported as certified
`INFEASIBLE`:

```c
/* F-2: a conductive phase-2 ray only certifies UNBOUNDED if phase 1 was
 * itself verified feasible on the same data; otherwise the honest verdict
 * is INFEASIBLE/NUMERICAL, never a mis-labelled certificate. */
```

**F-3 — partial-model cleanup (`src/fzn.c`).** Every error exit from
`fz_read` runs one cleanup list; `expr_free2` frees empty containers too.
Plus the self-found hardening: identifier domains (`var foo..bar: q;`) are
rejected instead of silently replaced by the ±1e9 sentinel box.

**F-7 — `tools/mipsolve.c`.** `--print` is accepted in any position; the
solution vector is dumped after a proven optimum.

### Root causes (one line each)

- **F-1:** the nnz counting sort conflated duplicate `(row, col)` entries
  into adjacent slots instead of summing them, silently changing model data.
- **F-2:** the phase-2 "conductive ray" shortcut inherited phase-1's
  verdict lazily — on phase-1-infeasible models the ray test "succeeded"
  on a phantom feasible region and was printed as a real certificate.
- **F-3:** error paths returned without freeing per-declaration containers;
  `expr_free2` skipped empty containers entirely.
- **AAS:** the type-keyword consume in decl parsing accepted any identifier
  as a domain keyword without resolving it.

### About the boundary coupling (`P ≠ P'`)

The audit's core structural observation stands and is now enforced: the
*constructor* (`lp_read`/`solver_create_internal`) may normalize model data
(duplicate merge), but every verdict-carrying certificate (OPTIMAL basis
certificate, ray certificate, UNSAT path) is re-checked against the
normalized form explicitly. `P` (text) and `P'` (internal) coincide only
through the now-asymmetric constructor-normalization, so a certificate over
`P'` plus the raw-model re-check cannot leak a wrong answer across the
boundary. The R3-style relay (certificate validity degraded by numeric
noise) is why F-2's fix re-runs the Phase-1 feasibility certificate before
accepting a phase-2 ray, instead of trusting the phase-1 exit flag.

---

## 3. New discriminating tests (files, seeds, expectations)

1. **`tools/lp_form_verify.py`** — families: (A) duplicate-triplet merges at
   adversarial magnitude/cancellation patterns (incl. the pinned 4.0-optimum
   instance), (B) contradictory-box / giant-sentinel boxes, (C) random
   boxes; every instance's verdict *and* objective cross-checked against
   `scipy.optimize.linprog`/HiGHS. Env override `LPSOLVE=<bin>` for pre/post
   discrimination. Wired into `test.sh` (`OK=302 WRONG=0, N=150,
   seed=20260815`).
2. **`tools/fz_leak_test.c`** — reads a case table of malformed/partial FZN
   inputs under LSan; asserts (a) zero leaks and (b) the expected
   accept/reject per case (this locks both F-3 leaks and the AAS
   identifier-domain rejection). `noinline` + 32 KB stack clobber defeats
   the dead-stack LSan blind spot. Hard gate in `test.sh`.
3. **`test.sh` wiring** — both tools run unconditionally in the battery
   (sections after the MIP differentials); their failure is a battery
   failure.

## 4. Other regression coverage (unchanged, all green)

- `mip_diff` vs brute force: `WRONG=0` at seeds 12345/111/222/333/555
  (400 instances each).
- `cp_opt_verify` vs brute force (both CP and MIP paths): `OK=500 WRONG=0`.
- FlatZinc semantics matrix (`fzn_semantics_test.py`) + `mzn_diff` vs
  Gecode/cp-sat: pass.
- `fx_verify`/`fx_exact_test` exact-rational batteries: `WRONG=0`.
- Fuzz: 80 malformed LP + 80 malformed QP + 120 malformed FZN inputs, no
  sanitizer errors.
- `oom_test`: 5882 failure-injection points, 0 failures.
- MiniZinc suite: **77/77 PASSED**; regenerated
  `tools/benchmark_results.json` + `docs/MINIZINC_BENCHMARK.md` differ from
  the committed versions **only in timings** — a semantic field-by-field
  diff (status/objective/verdict/solutions) over all 77 records shows **0
  differences**.

## 5. Reproduction commands

```sh
git clone -q https://github.com/SodoMita/psolve.git /tmp/upd && cd /tmp/upd
git checkout arena/cp-engine-correctness && make -s

# F-1/F-2 discrimination (needs scipy):
python3 tools/lp_form_verify.py                 # current binary: OK=302 WRONG=0
git worktree add -f /tmp/pre cb186f2^ && (cd /tmp/pre && make -s)
LPSOLVE=/tmp/pre/lpsolve python3 tools/lp_form_verify.py           # -> FAILED 60/80

# F-3/AAS (needs LSan):
gcc -O1 -g -fsanitize=address -fno-sanitize-recover=all -I src \
    tools/fz_leak_test.c src/fzn.c src/mip.c src/err.c src/solver.c \
    src/splu.c src/lu.c src/kernels.c src/parser.c src/fx.c -lm -o /tmp/fzlt
ASAN_OPTIONS=detect_leaks=1 /tmp/fzlt           # -> OK (no leaks, accept/reject correct)

# Full battery (MiniZinc bundle on PATH enables the mzn sections):
./test.sh && python3 tools/mzn_bench.py
```

## 6. Actual result (filled this round, 2026-08-15)

| Check | Result |
|---|---|
| `test.sh` (with MiniZinc on PATH) | **exit 0**; log lines: `lp_form_verify ... OK=302 WRONG=0 (N=150, seed=20260815)`; `fz_leak_test ... OK (no leaks, accept/reject correct)`; `mip_diff ... WRONG=0`; `cp_opt_verify ... OK=500 WRONG=0`; `fz fuzz OK: 120 inputs`; `fuzz OK: 80 malformed LP + 80 malformed QP inputs, no sanitizer errors`; `oom_test: 5882 injection points ... failures=0`; fx batteries `WRONG=0` |
| GLPK differential | SKIPPED — `glpsol` not installed in this environment (honest skip, unchanged policy) |
| `lp_form_verify` pre/post | pre (`cb186f2^` = `5bc2bb4` binary, rebuilt this round in a worktree): **fails the majority of checks** — 60/80 failures on an 80-instance run, `OK=117 WRONG=185` at the battery default (N=150); post: **OK=302 WRONG=0** (N=150, seed=20260815 as wired in `test.sh`) |
| mip_verify B/C pins pre/post | pre: wrong `UNSATISFIABLE` (B, dangerous direction) / garbage objective `1397419117.880728` (C); post: `110` proven (B) / honest correct `INFEASIBLE` (C) |
| MiniZinc suite | 77/77 PASSED; semantic diff vs committed results JSON: **0** |
| Sanitizers | ASan/UBSan/LSan builds of `lpsolve`/`fznsolve` run the respective batteries clean; no uninitialized/leak/UB reports on the new code paths |
| Benchmark docs | Regenerated; committed with the fix round (timings only) |

*Note on scope:* one F-round e2e idea from the audit discussion — a committed
"canonical dumper" binary that prints a normalized parse+linearization record
per model so pre/post trees can be diffed byte-for-byte over the whole
corpus — is deliberately **not** claimed here: an ad-hoc version of it lost
its source between snapshots before landing, and this response reports only
what was re-measured end-to-end on the committed tree. Adding that dumper as
a permanent tool is tracked as recommended future work below.

## 7. Remaining risks after fix (carried to `AUDIT.md` "Not done")

1. `setjmp` allocation-error protocol redesign (global state; thread-hostile).
2. ~~Farkas-certificate fast path for exact re-solves in the MIP bridge~~
   — **resolved 2026-08-15(5)**: directed-rounding Farkas certificate with
   engine margin; tsp5 exact re-solves 122 → 0, 2.1× wall, identical
   verdicts/tree/solutions; discriminating test `tools/farkas_verify.py`.
   See `AUDIT.md` addendum (5).
3. ~~Honesty gap on extreme scale-mixed *non-integral* LPs: double phase-1 may
   print bare `INFEASIBLE` on data `fx` declines~~ — **resolved
   2026-08-15(6)**: the verdict first attempts a directed-rounding Farkas
   box certificate (`solver_farkas_boxcert`, rescue: truly infeasible
   models keep INFEASIBLE, certificate-backed), else is promoted to honest
   NUMERICAL_FAILURE / SOLVE_NUMERICAL / UNKNOWN at the exposure frontier
   `E·eps ≥ 5e-7` in all three CLIs.  Discriminating hard gate
   `tools/lp_scale_verify.py` (reproduces 6 fabricated verdicts pre-change,
   0 post-change, 120/120 scipy parity on healthy data).  See `AUDIT.md`
   addendum (6).
4. Phase-I degeneracy convergence weakness on some genuinely infeasible LPs
   (honest `ITERATION_LIMIT`, completeness gap only).
5. CP scheduling globals (`gecode_schedule_unary`/disjunctive) still decline
   to MIP encodings (performance, not correctness).
6. `fznsolve -f` (free search) is an accepted no-op; a meaningful policy is
   planned (see `docs/ROADMAP_AMBITIOUS.md` Phase 11.3).
7. Future work: commit a permanent canonical-dump differential tool (see the
   scope note in §6) so parse+linearization drift over the whole corpus is a
   one-command check rather than an ad-hoc harness.
