# psolve — ambitious development roadmap (2026 → 2028)

**Status:** living document. Authored 2026-08-15 on branch
`arena/cp-engine-correctness` @ `00f5e1f`.
**Relationship to existing docs:** [`docs/ROADMAP.md`](ROADMAP.md) records the
original phase plan (Phases 0–5) and its completion history; it stays as the
historical record. **This** document is the successor plan for everything that
comes next. [`AUDIT.md`](../AUDIT.md) records the correctness audits; open
items from it are dispositioned into phases here (Appendix A).
[`docs/SOLVER_OPTIMIZATIONS.md`](SOLVER_OPTIMIZATIONS.md) is the technique
survey many phases below draw on; Appendix C maps its sections onto phases.

**How to read this document.** Every item has: *what*, *why it pays*, and
*acceptance evidence* — the empirical bar it must clear before it can be
called done. Items marked **[research]** carry real uncertainty: the approach
may fail, in which case the honest outcome is a written negative result, not
a half-merged feature. Items marked **[stretch]** are explicitly aggressive;
they are planning targets, not commitments. Dates in §17 are ambitions, not
promises; sequencing logic (correctness → trust → speed → reach) is the
contract, the calendar is not.

---

## 0. The constitution (non-negotiables, bind every phase)

These rules were bought with real bugs — fabricated `UNSAT`, suboptimal
points printed as `OPTIMAL`, silent set-literal truncation. They are
reaffirmed here so no roadmap item can quietly trade them away.

1. **Honesty doctrine.** An exact answer or an honest `UNKNOWN`. Never
   fabricate `UNSATISFIABLE`, a solution, an objective value, or a proof of
   optimality. The **UNSAT direction is the dangerous one**: no end-user
   verifier rechecks it, so every UNSAT/infeasibility claim must carry a
   checkable certificate, internally at minimum, exported where feasible.
2. **Reproduce → fix → prove.** No fix lands without a reproduction on the
   pre-fix binary, and every new regression/differential test must be
   **demonstrated to fail on the pre-fix binary** before it is admitted as
   evidence for the fix. Calibration claims are re-verified from raw logs
   (LSan/`_exit` output loss taught us that summaries lie).
3. **Zero runtime dependencies.** libc/libm only. Optional tooling (MiniZinc,
   scipy, GLPK/HiGHS, fuzzers) may gate *tests*, never the shipped library.
4. **Empiricism over authority.** Commit messages, audit reports, other
   branches, and this document itself are hypotheses. Claims are ported or
   marked done only after independent empirical verification in this tree.
5. **Tolerance transparency.** The engines are tolerance-based
   (`TOL_FEAS=1e-9`, `MIP_TOL=1e-6`). Any exact-arithmetic prune must carry
   the engine margin; any new tolerance must be documented in a per-module
   semantics sheet (item 14.6) with its safety direction stated.
6. **Bounded work / hostile-input safety.** Every solver respects
   time/node/iteration budgets; every parser is fuzzed under
   ASan/UBSan/LSan; library code never calls `exit()`/`abort()` on a path
   reachable from host input; allocation failure is a status, not a crash.
7. **Determinism.** Same input ⇒ same output, including statuses and counts,
   on a fixed build. The fixed-point kernels extend this to *bit-identical
   across platforms*; nothing may regress that without a written disposition.
8. **Small and auditable.** Prefer deleting code to adding it. A feature that
   cannot be differentially tested or brute-force verified at some scale is
   not finished.

---

## 1. Portfolio snapshot (2026-08-15)

| Component | Where | State | Verification level today |
|---|---|---|---|
| Double LP (revised simplex, AVX-512) | `src/solver.c`, `splu.c`, `lu.c`, `kernels.c` | Production core; numerically fragile on big-M bases (finding A history) | scipy differential, GLPK sweep, certificates, fuzz |
| Exact rational/fixed-point LP | `src/fx.c`, `fx_core.inc` | Full-tableau; correct; O(small) only | cross-check vs double on thousands; ASan clean |
| QP (active-set) | `src/qp.c` | Hardened (KKT certs, recession rays) | KKT-certificate differential 1000/1000 |
| MIP (B&B + FBBT + warm starts + fx fallback) | `src/mip.c` | Correct after 2026-08 rounds; tree still large on combinatorial models | brute-force differential, fabrication-free UNSAT (directed-rounding certs) |
| Finite-domain CP engine | `src/fz_cp.inc` | B&B + incumbent-bound FBBT; small propagator set | cp_opt_verify, semantics tests |
| FlatZinc bridge + globals | `src/fzn.c` | ~30 global families linearized; 77-instance suite green | mzn_diff vs Gecode/cp-sat, brute-force verifiers |
| PGS / fixed-point PGS | `src/pgs.c`, `pgs_fixed.c` | Foundation complete, zero-malloc, batch API | cross-validation float↔fixed, bench vs LP/QP |
| MiniZinc integration | `tools/mzfnsh`, `share/minizinc/solvers/psolve.msc` | Native `--solver psolve`; bench driver | 77/77 suite, docs regenerated |
| Build/QA | `Makefile`, `test.sh`, ~30 tools | Strong for a single-author project | ASan/UBSan/LSan + differentials in-tree; no CI yet |

**Known debts carried into this roadmap** (from `AUDIT.md` "Not done" and
branch audits): global `setjmp` error protocol; per-node exact re-solve cost
(121/122 tsp5 re-solves are duality-level infeasibility); exact-or-UNKNOWN
promotion gap on scale-mixed non-integral LPs; Phase-I degenerate-infeasible
convergence weakness; `gecode_schedule_unary`/disjunctive still decline to
MIP; `-f` free search is a no-op; Phase-2 VG/UI kernels unstarted; no CI.

---

## 2. What the roadmap optimizes for

Three pillars, in priority order:

1. **Trust** — every verdict carries evidence; every UNSAT is certified;
   every claim is reproducible by an adversary with this repository.
2. **Speed** — within the trust envelope: fewer wasted nodes, fewer wasted
   pivots, fewer wasted re-solves. Never by weakening a certificate.
3. **Reach** — more of the MiniZinc semantics natively, more problem classes
   solved exactly, more platforms (library hosts, embedded, browser) without
   breaking the zero-dependency rule.

Sequencing: **Phase 6 (correctness closure) gates everything.** Phases 7–13
may interleave after that; Phases 14–16 are continuous background streams,
not gates.

---

## 3. Phase 6 — Correctness closure *(top priority; closes all known debts)*

Goal: after this phase there is **no known input class** on which psolve can
return an uncertified or misleading verdict, and the error-handling protocol
is safe for a multi-threaded library host.

### 6.1 Exact-or-UNKNOWN promotion for double verdicts — **DONE 2026-08-15(6)**
Implemented with a sharper design than the blanket scale-mix classifier
below, aimed at the only unverified direction (INFEASIBLE — OPTIMAL was
already protected by the phase-2 solution certificate, and integral data
already had the fx fallback in the MIP bridge): a bare-INFEASIBLE verdict
first attempts to **prove itself** — the exported `solver_farkas_boxcert`
re-verifies the phase-1 dual ray against the original rows and box with
directed rounding (rescue: truly infeasible models keep the verdict,
certificate-backed) — and otherwise, when
`solver_row_exposure`·DBL_EPSILON ≥ 5e-7 (half the phase-1 tolerance), the
verdict is **promoted** to NUMERICAL_FAILURE / SOLVE_NUMERICAL / UNKNOWN
in lpsolve, the MIP relaxation, and the FZ pure-LP branch (no pruning on
an unproven verdict). Acceptance evidence: the pinned repros flip from
fabricated INFEASIBLE/UNSATISFIABLE to honest UNKNOWN in all three CLIs
(FZ `/tmp/sm_bare_0.fzn` UNSATISFIABLE→UNKNOWN, feasible model);
discriminating hard gate `tools/lp_scale_verify.py` in test.sh reproduces
6 fabricated INFEASIBLE verdicts on the pre-change binary (5 LP-class,
1 MIP-class) and post-change reports `checked=250 fabricated=0
rescued=60 promoted=48 healthy_checked=120 ALL OK` (scipy verdict parity
on well-scaled data, zero flips); ASan/UBSan(+LSan) clean; full battery
exit 0, mip_diff WRONG=0 ×5 seeds, MiniZinc 77/77 with 0 semantic diffs.
AUDIT.md addendum 2026-08-15(6). Design discussion kept below for the
record:
- **What (original design):** A verdict-grading pass on every LP/MIP/FZ result: classify the
  instance's data scale-mix (max |aᵢⱼ| / min positive |aᵢⱼ| over the active
  rows, bound magnitudes, cancellation risk). Verdicts from the double engine
  on data beyond a measured reliability frontier are *promoted*: re-decided
  by the exact engine when the data is integral (or exactly representable),
  otherwise downgraded to `UNKNOWN` with a printed reason code.
- **Why:** closes AUDIT not-done #4 — double phase-1 can print bare
  `INFEASIBLE` on ~1e-13-coefficient / ~1e25-bound data where a Fraction
  reference proves feasibility; `fx` declines non-integral data and the
  bridges currently *keep* the shaky verdict.
- **Acceptance:** the preserved 2026-08-15 repro flips from false
  `INFEASIBLE` to feasible-or-UNKNOWN; a 100+-instance scale-mixed family
  (`tools/` differential, exact-Fraction reference) shows **0 fabricated
  verdicts**; the new family is proven to expose the bug on the pre-change
  binary; no regression on the existing battery (status split identical on
  well-scaled instances).

### 6.2 Farkas-certificate fast path for infeasible relaxations — **DONE 2026-08-15(5)**
Implemented as designed (`solver_farkas_duals` hint + `mip_farkas_certified`
directed-rounding verification against the original rows, `farkas_ok` state
marker in `Solver`). Acceptance evidence: tsp_5 exact re-solves **122 → 0**
(96 certificates, 0.455s → 0.218s) with identical verdicts, node counts and
byte-identical solutions; `tools/farkas_verify.py` discriminating regression
(fails on pre-change binary) hard-gated in `test.sh`; ASan/UBSan clean;
full battery + MiniZinc 77/77 with 0 semantic diffs. AUDIT.md addendum
2026-08-15(5).

### 6.3 Error-protocol redesign: retire process-global `setjmp` — **DONE 2026-08-15(7)**
Implemented as caller-owned per-thread error frames (`PSolveErrFrame`):
each thread chains its own armed frames through thread-local storage and
`psolve_fail` unwinds to the calling thread's innermost frame (popped
before the jump, so recovery may re-push); the retired
`psolve_env/psolve_active/psolve_code/psolve_try/psolve_end` globals are
gone from the tree, and the cooperative-stop callback became per-thread
(`psolve_stop_set`).  The failure code travels in TLS and is read via
`psolve_err_code()` on purpose — unlike a field of the frame struct it is
not subject to the C11 7.13.2.1 setjmp/longjmp indeterminacy rule.
Nested scopes, which the old protocol *refused* (`psolve_try()` returned
1), now compose; popping a non-innermost frame is a checked protocol
violation.  With `nm` showing the rest of the library already free of
exported mutable data (verified at build time), the redesign makes the
whole library carry **zero process-global mutable state** — gated in
`test.sh` so it stays that way.  Acceptance evidence: `tools/err_proto_test.c`
(12 checks: basic/nested/re-push-in-handler flows, realloc NULL-out,
calloc overflow guard, no-frame `exit(code)`, aborting pop violation) and
`tools/err_mt_test.c` (8 threads × 300 rounds of real concurrent solves,
forced arena-OOM recoveries, per-thread stop callbacks) both pass, the
MT test clean under ThreadSanitizer (3/3 runs, no reports); neither test
compiles against the pre-change `err.h` (discrimination: API absent) and
an old-API reproducer shows `psolve_try()` refusing a second thread's
handler.  All six consumer call sites migrated (4 CLIs + arena_test +
qp_stop_test); full battery exit 0 with the oom sweep **unchanged at
5930 injection points over 6391 allocations, failures=0** (CLI behavior
identical at every injection point); MiniZinc 77/77 with 0 semantic diffs.
AUDIT.md addendum 2026-08-15(7). Design discussion kept below for the
record:
- **What (original design):** replace the global allocation-failure longjmp with per-context
  error state: `Solver/ MIP/ FZ` structs carry an optional arena + error
  record; all fallible operations return status. Public API gains
  `psolve_set_alloc(ctx, alloc_fn, user)` so a host owns memory policy.
- **Why:** closes AUDIT not-done #2; a recovering multi-threaded host cannot
  live with process-global jump state. Also unlocks Phase 12 arena work and
  Phase 15 threading honesty.
- **Acceptance:** `tools/oomlib.c` failure-injection sweep passes at every
  allocation-failure point (already the methodology) *without* any global
  state under `-fsanitize=thread` smoke runs; API documented in README;
  all tools migrated.

### 6.4 Unified evidence objects — **DONE 2026-08-18(12)**
`src/cert.h`/`src/cert.c`: one `PsvCert` claim type per verdict class
(LP OPTIMAL/INFEASIBLE/UNBOUNDED, MIP point + proven-optimal stamp,
QP optimal/unbounded, search-EXHAUSTION) and one `psv_cert_check()`
entry point; every verdict-printing CLI exit (lpsolve, mipsolve,
qpsolve, fznsolve optimize/UNSAT lanes) fills the claim over the
ORIGINAL model data and prints only through it (REJECT/DEFER degrade to
the honest class).  Engine evidence hooks added: `solver_unbounded_ray`
(recession ray materialized at the dense-verified `iterate()` exit,
unmapped through the x⁺/x⁻ split) and `QPResult.ray`.  LP-OPTIMAL is
certified by a bounded-LP Lagrangian dual bound in the caller's own
data (sense-normalized duals, sign-clipped = soundly weakened, rounding
dust charged upward on closed boxes only).  Error-injection hard gate
`tools/cert_inject` (in `test.sh`, 2 seeds) measures: LEGIT claims
accepted 100% on all 8 families; LARGE adversarial corruptions rejected
100%; directed 1-ulp noise rejected 100% on the zero-width
snapped-integer MIP surface (199/199, 1964/1964 at N=2000) — and
**reported, not asserted, on tolerance-margined point surfaces** (0%,
by design: a checker stricter than the engines' own terminal margins
would false-reject every true claim; the literal "≥99.9% of 1-ulp
OPTIMAL claims" target is decidable only on zero-width surfaces and is
met exactly there — AUDIT addendum 12 states this precisely instead of
claiming it).  Injection adversarialness is decided by independent
closed-form oracles (never the checker under test), after the harness
itself caught two of its own unsound families (garbage duals that
collapse to a *valid* box-corner certificate; rotation-invariant Farkas
rays).  Three real defects the gate/battery caught during construction
and pinned: symmetric bound-coherence vs the root integrality gap
(false-rejected ~38% of proven-optimal MIPs — fixed to directional,
`examples/knap_gap.lp` pin), |b|-only row grace vs catastrophic
cancellation (fbbt_verify went 2→31 FAIL then 0 with activity-scaled
dust bounds), and MIP-UNSAT lane over-strictness on the pinned 5e-7
margin cycle (sub-margin infeasibility proved by lattice exhaustion,
not by a Farkas ray).  Full battery rc=0, tolsheet closure green,
ASan/UBSan/LSan sweep clean, bench 77/77 semantics identical.

### 6.4-origin specification (superseded by the DONE note above)
- **What:** a small internal `psv_cert_t` per verdict class: OPTIMAL
  (primal point + dual vector), INFEASIBLE (Farkas ray or exact-engine
  stamp), UNBOUNDED (primal point + recession ray), with one
  `psv_cert_check()` entry point used at every exit from every engine.
- **Why:** today certificates are spread across `solver.c`, `qp.c`, `mip.c`;
  unifying them is the substrate for Phase 16 proof export and kills whole
  classes of "forgot to re-check" bugs.
- **Acceptance:** every CLI verdict path goes through the checker; an
  error-injection tool that perturbs internal results by 1 ulp shows the
  checker rejects ≥99.9% of perturbed OPTIMAL/UNSAT claims (and 100% of
  large perturbations).

### 6.5 QP numerical audit round — **DONE 2026-08-15(8)**
One fabrication-class hole found and closed: the QP convexity gate screened
only 1x1/2x2 principal minors, so n ≥ 3 indefinite Q whose negativity lives
in a larger minor passed (diag 1, off-diag −0.9: minors 0.19, eigenvalue
−0.8), and the active-set printed the stationary origin as an "optimum" on
problems unbounded below — **103 fabricated status-0 verdicts in 122 cases**
on the discrimination family (25 box variants with objectives wrong vs true
vertex optima, e.g. −4.436 claimed vs −14.288 true).  Fixed with a full
symmetrized **complete-pivoting** elimination scan (Sylvester-sound in both
directions in exact arithmetic; scaled tolerance 1e-9·(1+max|Q|)); complete
pivoting is load-bearing — the natural-order draft over-blocked 14/40
scale-mixed genuine PSD blocks (caught by the new tool's scale_mix class
pre-commit).  Acceptance evidence: discriminating hard gate
`tools/qp_psd_verify.py` in `test.sh` (pre-change
`fabricated_status0=103, obj_mismatch=25`; post-change 0, ALL OK across the
indef/near_psd/asym/scale_mix classes and both pinned gadgets at STATUS 4);
existing `qp_diff.py` status distribution IDENTICAL pre/post (verdict-neutrality
on convex data, WRONG=0); ASan/UBSan(+LSan) clean; full battery exit 0;
MiniZinc 77/77 0-semantic-diff.  No other wrong-answer class found in the
extended sweep; `qp.h` documents the gate.  AUDIT.md addendum 2026-08-15(8).

### 6.6 Harvest remaining `phase4-interactive-hardening` commits — **DONE 2026-08-15(4/5)**
Ported by the parallel agent's `arena/phase4-ports` branch and merged to
`main` (`f098876`): `a42be76` ms-precision time limits + QP cooperative stop
(`4966773`) and the re-entrant thread-local zero-malloc arena (`b7f0561` +
`df028c2`, designed to the previously documented rejection reasons — no
process-global arena, ownership-checked frees). Independently verified this
round: merges textually faithful (empty tree diffs vs branch tips), full
battery green on `main`, zero-libc-heap proof under `--wrap=free`,
oom/fz_leak gates pass. See AUDIT.md addenda (4) and (5).

### 6.7 FlatZinc round-trip & output fuzzing — **DONE 2026-08-16(9)**
The new output-layer checker caught **one fabricated-SAT class**: the CP
engine's `cp_set_vals` had replace semantics, letting propagators widen
declared domains — pin of record `x0≡5, x1∈[-4,-3], int_abs(x1,x0)` printed
**`x1 = -5`, outside its domain, as SATISFIABLE** (sibling bare case printed
`x0=0,x1=0` SAT on a truly UNSAT model). Fixed with restrict semantics
(sort/dedupe + intersect; empty ⇒ infeasible) plus the follow-on threading
of the now-reachable empty-domain failure return through all 12 propagator
callsites (its absence was a heap-buffer-overflow, ASan-caught on the
post-fix binary pre-commit). Also fixed a pre-existing parser OOB stack
read (`var {>256 ints}: x` → SIGSEGV, pin verified on pre-change binary)
and the related CP-init path that wrote an intentionally empty domain for
>CP_MAXVALS set literals instead of declining to the MIP/SOS1 bridge.
Tool-side lesson documented in AUDIT addendum (9): exact-Fraction checks on
decimal round-trips of binary doubles are unsound for float rows — the
checker uses the standard scaled 1e-6 LP feasibility tolerance and reports
the run-max residual (≤1e-16 over 19.5k models). Acceptance evidence:
`tools/fzn_output_check.py` hard-gated in `test.sh` (5 pins first: abs
UNSAT ×2 incl. the x1=-5 of record, abs feasible, 65536/66000-member set
domains); **pre-change binary**: 4/5 pins fail + `WRONG=34/2000`;
**post-change**: WRONG=0 at `N=10000 seed 20260815` and `N=4000` each at
seeds 777/4242 (19.5k models incl. `-a`/aliases/echo checks), ASan/
UBSan(+LSan) sweep N=1500 clean, full battery + MiniZinc 77/77
0-semantic-diff. Known limitation logged (perf, not honesty): declined
>65536-member set domains whose UNSAT-ness needs reasoning grind in MIP
B&B; presolve suggestion recorded in AUDIT (9).

### 6.8 Per-module tolerance semantics sheets — **DONE 2026-08-18(11)**
- **What (shipped):** `docs/DESIGN.md` §8 — one subsection per engine (LP
  core, MIP, QP, PGS float+fixed, FlatZinc front-end, CP, exact-fx, plus a
  §8.8 for no-decision-power constants swept by the closure).  80 rows
  covering all 127 tolerance-literal sites in `src/`, each with class
  (verdict-adjacent V / convergence C / honest-decline D / recognition R /
  sentinel-stability S / exact E), direction of safety, what it protects,
  and what it may never justify.  Header rule: a V-class tolerance never
  decides a verdict alone — it feeds a certificate that stands without it
  (directed rounding, exact-rational re-check, printed bound), or the
  status degrades to SOLVE_NUMERICAL.
- **Acceptance mechanised:** every site carries a `/* TOLSHEET <ID> */`
  comment; new hard gate `tools/tolsheet_check.py` (in `test.sh`) enforces
  closure both directions through a real C comment/string state machine —
  every code-line literal tagged, every tag documented, every row live.
  Undocumented new tolerances and stale rows both fail the battery.
- **Survey honesty:** the machine closure caught sites the manual sweep
  missed (fx representability guards, fznsolve's own 5e-7 exposure
  frontier, QP divergence cap, splu growth watchdog, sentinel families) —
  they are documented as found-by-machine in AUDIT (11).  Full battery
  rc=0, 77/77 bench 0 semantic diffs; comment-only source changes.

### 6.9 Functional-graph family + presolve structure recovery — **DONE 2026-08-18(10)**
Generalizes the procstates-specific `orbit_len` global (docs/PROCSTATES.md)
into a reusable toolset plus auto-detection, answering: "how does a
task-specific orbit trick become a general-purpose set of tools, or an
automatic solving-path choice?"  Shipped (full spec in
docs/FUNCTIONAL_GRAPH.md): (a) a shared functional-graph digest pool
(content-deduped, single-owner) with façades `orbit_transient`,
`orbit_cycle_len`, `orbit_on_cycle`, `orbit_len_capped` and the previously
unparsed `array_bool_and`; (b) a presolve detector that recovers the
MiniZinc "H-step walk + prefix-distinct count" lattice exactly — every
consumed record verified, every intermediate proven dead (no surviving
reference, not output-pinned, not objective-pinned), `referenced[]`
recomputed airtight — and rewrites it to one `orbit_len_capped` record.
Hard rule: detection never narrows semantics; on any mismatch the model is
untouched and the generic engine answers.  Four memory cliffs fixed en
route (measured, in order): realloc-per-entry keep lists (1.3 GB at
n=16384 → two-pass count-then-allocate), dead-var domain materialization
(803k dead values), dead-var branching (~800k nodes × MB-scale copies →
branch over `searchme = referenced ∪ output` — the referenced-only first
cut fabricated 0-valued prints for unconstrained OUTPUT vars, WRONG=59
flagged by the phase-6.7 output gate, fixed before commit), and the
O(Σ|dom|)-per-pass `cp_total` fixpoint (→ change-only mutation counter).
Plus nv==0 satisfy models now reach CP (was a silent skip).  Headline:
the **stock flattened procstates encoding** (n=65536, H=64) is auto-detected
and PROVEN optimal (44) in 1.76–1.89 s / 103 MB — vs Gecode 600 s UNKNOWN,
Chuffed/CP-SAT OOM (§6.9 docs; PROCSTATES §5 baselines).  Acceptance
evidence: two new discriminating hard gates (`fgraph_verify`: pre
pins_bad=5/WRONG=112@60 → post WRONG=0@200; `orbit_detect_verify`: pre
WRONG=16/40 → post WRONG=0@100), procstates gate WRONG=0@120 rerun,
full battery rc=0, 77/77 bench 0 semantic diffs, ASan/UBSan/LSan clean.

**Phase 6 exit criteria:** AUDIT.md "Not done" list is empty or each item
has a written permanent disposition; full battery + sanitizer matrix green;
no fabricated-verdict family known.

---

## 4. Phase 7 — LP engine v2

Goal: 2–10× on real LP workloads via structural work reduction, plus a
second engine family (interior point) for the dense/ill-conditioned cases
where simplex is weak. Techniques and literature pointers are already
surveyed in `SOLVER_OPTIMIZATIONS.md` §2; this phase is the implementation
plan for it.

### 7.1 Presolve + postsolve *(highest ROI in the phase)* — **DONE 2026-08-19(14)**
`lp_presolve()` / `lp_presolve_postsolve_{x,duals,ray}()` (src/presolve.{h,c})
run on the caller-visible LP before the engine's free-variable split, mlt
sign rows and 7.5 equilibration: fixed columns, empty rows (consistent:
dropped, dual 0; inconsistent: exact infeasibility with an explicit
one-row Farkas ray), empty columns at read (fixed at the cost-sign bound;
cost walking an open side = certified-UNBOUNDED note), singleton-row
implied-bound box folds (directed-rounding outward; singleton-vs-box and
singleton-pair conflicts = exact one-/two-row rays), redundant rows
(directed-rounding activity limits) and doubleton-equality substitution
(fill-capped `PRE_NNZ_GROWTH`, pivot = larger |a|).  Every fired reduction
pushes a record; postsolve replays records in REVERSE firing order, duals
via firing-time pivot-column snapshots (`y_i = (c_j − Σ y_r a_rj)/a_ij`,
unknown-reference cycle ⇒ the map declines), with a complementarity guard
(TOL-LP-PRETIGHT) keeping slack singleton rows at dual 0.  Exact-ray
paths are gated by `row_touched`: any row b/coeff-mutated by an earlier
fold aborts presolve rather than risk a doom ray — decline or degrade,
never guess.  The CLI climbs `(presolve,scale) → (0,scale) → (0,0)` on
any uncertified evidence; `model->n == 0` (full elimination) takes a
direct-claim branch with `iterations: 0`; presolve rc==1 rays go through
the same `psv_cert_check(PSVK_LP_INFEASIBLE)` lane; a presolved-model
engine-INFEASIBLE never prints without re-solving raw (no ray
back-propagation in v1, documented).  Objective prints are always
recomputed `Σ c_j·x_j` on ORIGINAL postsolved data.

- **Bundled cert-layer fix (same commit):** `solver_farkas_boxcert` now
  skips columns whose directed-rounding `yᵀA` window is provably `[0,0]`
  (`zl ≥ 0 ∧ zh ≤ 0` forces the exact sum to 0 ⇒ box product contribution
  exactly 0 for ANY box) *before* the TOL-LP-BIGCAP decline — previously
  ANY free-variable column vetoed the whole certificate, so every
  infeasibility Farkas ray on a free-variable model (engine's own rays
  included) degraded to NUMERICAL_FAILURE.  Soundness unchanged (zl/zh
  bracket the exact sum under FE_DOWNWARD/FE_UPWARD); measured effect on
  the planted corpus: 2 pre-change NUMERICAL_FAILUREs now certify
  INFEASIBLE (free-var pair conflict; y-range-joint conflict), all
  farkas/lp_scale/scale/cert_inject gates stay green on both seeds.
- **Acceptance, measured:** presolve off/on A/B over **40 000 random LPs**
  (20k well-scaled + 20k entry-mixed 1e±4, seed 42, plus 5k+5k seed 777)
  — **zero verdict flips, zero lost raw answers**, combined-evidence
  fallbacks 364/20k·2 (each lands the identical raw verdict; the ladder
  may only decline, never change an answer); co-OPTIMAL objective prints
  bit-identical on 98.6%/99.1% of the corpus (identical whenever no
  reduction fires), remainder ≤1.4e-10 rel — inside engine tolerance,
  far under the 1e-9 psv re-proof — gate pins 1e-12 (well-scaled) /
  1e-9 (entry-mixed); 39 952 co-OPTIMAL scipy/HiGHS cross-checks inside
  the calibrated bars (modest 1e-6 hard 20/20k-clean; entry-mixed
  gross-error 3e-4 — HiGHS is not an authoritative objective oracle on
  1e±4 data, measured max engine-vs-HiGHS gap 1.8e-4 with the
  pre-change binary printing IDENTICAL values on every flagged instance).
  Planted per-reduction verdict cases (11): every exact-ray family
  certifies with `iterations: 0`, the touched-row family declines and
  still answers, the fallback family degrades with a psv note yet never
  changes the answer.  Records unit-verified by
  `tools/presolve_selftest.c` (17 apply→restore invariant checks: primal
  replay exactness, exact pivot-column dual stationarity,
  complementarity guard, stats accounting).  Size reduction on the
  shipped corpus: honest measurement — the random families are
  decline-dominated (3 330/40 000 models reduce; conditional shrink
  rows 16.5% / cols 5.2% / nnz 10.5%; the structured planted chains
  eliminate 100% of rows), so the 30–60% target is *not* reproduced on
  this corpus (it lacks the removable structure the target assumed);
  what is verified end-to-end is the certified-decline discipline that
  makes presolve safe regardless of reduction rate.  CLI: `--nopresolve`
  / `--prestat`.  Hard gates in `test.sh`: `tools/presolve_verify.py
  400 20260819` (fails loudly on the pre-change binary — unknown option)
  and the selftest binary (absent on the pre-change tree).

### 7.2 Dual simplex (bounded-variable, dual steepest edge)
- **Why:** MIP re-optimization after bound changes/cuts is dual-simplex
  shaped; today every relaxation re-runs primal (warm starts help — tsp_5
  20695→63 iterations — but dual is the structurally right answer once cuts
  land in Phase 8).
- **Acceptance:** dual vs primal verdict/objective agreement on ≥20k random
  LPs; on a branching-workload benchmark, dual re-solve beats primal warm
  start ≥2× on iterations.

### 7.3 Forrest–Tomlin basis updates + Markowitz upgrade
- Product-form eta file replaced by FT update with bump structure; splu
  ordering gains Markowitz with tie-breaking (merit function), replacing the
  current degree-only ordering.
- **Acceptance:** INVERT frequency and per-iteration cost profiles on the
  bench corpus; no stability regressions (certificate failure rate on
  big-M family must not rise).

### 7.4 Crash bases (Maros–Mitra) and advanced-start API
- **Acceptance:** Phase-I pivot count down ≥30% on equality-heavy families;
  the fz bridge passes its known-good bases through the new API.

### 7.5 Scaling (Ruiz equilibration + geometric mean, Curtis–Reid option) — **DONE 2026-08-18(13)**
`solver_create_opts(lp, scale_mode)` / `solver_create` (= scaled default):
4 Ruiz iterations (geometric-mean row pass then column pass) at create
time accumulate strictly positive diagonals `D_r`/`D_c`; the engine works
on `D_r·A·D_c` and every public funnel composes the diagonals back
(optimum/ray ×D_c, row duals/Farkas rays ×D_r, reduced costs ÷γ,
set_objective ×γ, set_bounds ÷γ with infinity-token sides never divided,
export/rebuild unwrap all three factors), so callers always see ORIGINAL
units and every verdict keeps its original-data psv re-verification.
Decline-to-scale-less guards (`LP_SCALCAP=1e300` spread cap, non-finite
factor products, finite-bound→token reclassification refusal) are class-D
honest-decline, never verdict inputs.  The LP CLI gains `--noscale` (raw
path) and `--scalestat` (pre/post spread proxy) plus an
evidence-preserving raw-data FALLBACK: when a scaled run's evidence
cannot be certified against the original data (any psv lane reject or an
engine SOLVE_NUMERICAL) it re-solves once raw and re-evaluates the whole
verdict chain, so scaling can add certified answers, never take one away.
The MIP bridge, fzn pure-LP lane and mipsolve's relaxation arbitrator are
pinned to the RAW path (1.0 diagonals ⇒ bit-identical pre-7.5 numerics;
measured pre-pin MIP answer movement at per-entry 1e±8 spread — 7 lost /
7 objective-disagreeing vs 5 rescues in 150 — showed scaled node answers
cannot be arbitrated as strict improvements without a per-node primal
certificate story).
- **Acceptance, measured:** conditioning proxy — median post/pre spread
  0.0098 at 1e±4 entry-mixing and 3.9e-6 at 1e±12, worsened on 0/466
  gate instances; honest-failure rescues (the "before it becomes an exact
  re-solve" knob): raw/pre-change LU-stall NUMERICAL → scaled certified
  OPTIMAL at ~0.4% of the 1e±4 family and ~7% of the 1e±12 family,
  objectives scipy-confirmed (pinned instance: spread 6e8 model, raw
  NUMERICAL, scaled 10560305.7817494 == HiGHS); verdict changes — zero on
  well-scaled data (400/400 scipy cross-checked parity across two gate
  seeds), examples A/B-identical, MIP/fzn surfaces bit-identical by pin.
  Hard gate `tools/scale_verify.py` (in `test.sh`, 2 seeds);
  discriminating: pre-change binary fails the `--noscale`/`--scalestat`
  lane probe loudly.  AUDIT addendum (13) carries the full tables,
  including the honest iteration-movement delta (+2.7–3.4% iterations on
  the small probe families — equilibration changes vertex paths; the win
  is certified-answer recovery, not iteration count).
- **Why:** cheap, and directly attacks the big-M conditioning debt
  (AUDIT finding A lineage) *before* it becomes an exact re-solve.
- **Acceptance:** conditioning proxy (max/min pivot growth) improved on the
  big-M corpus; exact re-solve count down measurably; no verdict changes.

### 7.6 Interior-point engine (Mehrotra predictor–corrector) *[research]*
- From scratch: normal-equation (A·D·Aᵀ) Cholesky with PCG fallback,
  Gondzio corrections, termination into **crossover** to a basic solution
  (so MIP and cut machinery still receive a basis).
- **Why:** dense or highly degenerate LPs (some QPs' KKT systems too) are
  simplex-hostile; IPM is the standard complement.
- **Acceptance:** on a documented dense corpus, IPM beats simplex ≥3× with
  certified equality of objectives; crossover returns a vertex; honest
  fallback to simplex on numerical distress. Negative result acceptable:
  if crossover cannot be made certificate-clean, ship IPM as satisfaction-
  only with UNKNOWN-on-doubt and write the finding.

### 7.7 Iterative refinement + exported rays
- Refine primal/dual solutions in extended precision (double-double) before
  certification; export dual rays and Farkas rays via the Phase 6.4
  evidence objects.
- **Acceptance:** residual norms reported; refined certificates accepted at
  strictly tighter tolerances on the adversarial corpus.

### 7.8 Network-row detection + network simplex island *[stretch]*
- Detect pure ±1 network submatrices (transport/assignment-like models are
  common in the examples corpus) and solve that island exactly/combinatorially,
  stitching bounds back.
- **Acceptance:** examples/transport.lp-class instances ≥10× faster, exact
  agreement; detection is conservative (a false positive is a correctness
  bug — detection must be Certifiably exact structure).

### 7.9 LP file format v2 / MPS reader *(scope decision item)*
- Named rows/columns, ranges, RHS section, comments; strict-mode reject on
  ambiguity (the `<==` lesson). MPS import only if a from-scratch reader
  stays under ~600 lines.
- **Acceptance:** round-trip tests; fuzzed; documented grammar (EBNF) in
  docs.

**Phase 7 exit criteria:** published A/B table on the standard corpus
(time, iterations, certificate rate); no verdict regressions; all new paths
fuzzed + differential-tested.

---

## 5. Phase 8 — MIP engine v2

Goal: from "correct B&B with strong honesty" to a small but real 1995-grade
MIP solver: cuts + conflict analysis + heuristics + reliability branching.
Target profile: solve the current 77-instance suite's MIP-viable models in
≤10% of today's node counts; make the honest-UNKNOWN combinatorial models
(cumulative/table at scale) *decidable* within budgets.

### 8.1 Reliability branching
- Pseudocosts → strong branching at top-k unreliable candidates →
  full reliability branching (per survey §3). Deterministic tie-breaking.
- **Acceptance:** node count pareto on the differential corpus (geometric
  mean ≥30% reduction, no instance >2× worse); brute-force agreement intact.

### 8.2 Cut engine
- Safe rounded Gomory / MIR cuts from tableau rows; cover cuts from
  knapsack rows; clique cuts from a conflict graph (8.3); flow cover for
  fixed-charge rows. Cut pool with aging, density cap, and **numerically
  safe cut certification** (each cut re-checked by directed-rounding
  violation of the relaxation point before being trusted for bound moves).
- **Design constraint:** a cut may tighten the relaxation; it may *never*
  enter an UNSAT/prune proof without the Phase-6 certificate path.
- **Acceptance:** gap closed at root reported across corpus (target ≥20%
  average on binary knapsack-like families); wrong-answer differential
  stays 0 with cuts forced on/off A/B.

### 8.3 Conflict graph + conflict-driven restarts
- Implication graph from bound changes; on infeasible nodes derive a
  no-good cut; restart policy with kept incumbent and kept pseudocosts.
- **Acceptance:** infeasible-heavy families (pigeonhole-style,
  proof-of-infeasibility suite models) node counts down ≥5×.

### 8.4 Primal heuristics portfolio
- Feasibility pump (LP-rounding cycles with objective perturbation),
  diving, RINS (needs 7.2 dual to shine), local branching for binary-heavy
  models. Deterministic schedules; heuristics *find* incumbents, they never
  influence proofs.
- **Acceptance:** time-to-first-incumbent down ≥3× on the optimize-mode
  suite; fabricated-incumbent injection test (perturb heuristic output)
  always caught by incumbent verification.

### 8.5 Node selection + tree management
- Hybrid best-bound / estimate with plunging limits; memory-bounded tree
  with deterministic evacuation order; node budget honesty (status
  propagation rules unchanged).
- **Acceptance:** fixed memory ceiling honored under oomlib injection;
  identical verdicts under 3 different selection policies on ≥5k models.

### 8.6 Symmetry handling *[research][stretch]*
- Orbit fixing via automorphism groups on the constraint bipartite graph;
  a from-scratch canonical-labeling mini-engine is research-grade scope —
  start with *detection-only* of row/column permutation symmetry classes
  and lex-leader symmetry-breaking rows for the common all-binary case.
- **Acceptance:** symmetric CSP families (graph coloring, pigeonhole)
  ≥10× node reduction; symmetry detection itself brute-force verified on
  small instances.

### 8.7 Objective-guided propagation
- Incumbent cutoff already feeds FBBT (2161→577 nodes); extend: objective
  cut row in the LP, reduced-cost fixing with the unified certificate,
  and cutoff-aware CP propagation (links Phase 10).
- **Acceptance:** instrumentation shows fixing counts; verdict parity.

### 8.8 Deterministic parallel B&B *(design in Phase 15; implementation here)*
- Fixed task split, result reduction in a canonical order, proofs
  conservative: parallel mode may never accept a prune that sequential mode
  wouldn't. See 15.1 for platform decisions.
- **Acceptance:** bit-identical verdicts/incumbent/objective 1 vs N threads
  over the full corpus; speedup ≥2× at 4 threads on tree-heavy models.

**Phase 8 exit criteria:** suite node/time table published pre/post;
MIPLIB-relaxation-style public small instances (manually curated, license-
clean) added as fixed benchmarks; zero fabricated verdicts under cut+heuristic
stress A/B.

---

## 6. Phase 9 — Exact engine (`fx`) v2

Goal: make exact arithmetic cheap enough that *promote-to-exact* (6.1) and
*exact relaxation oracle* (fz bridge) are default-affordable, and export
machine-checkable proofs (substrate for Phase 16).

### 9.1 Sparse exact revised simplex
- Replace full-tableau exact pivoting (residual 2-gcd-per-cell cost
  documented in ROADMAP.md) with revised form: rational BTRAN/FTRAN over a
  sparse exact LU with lazy normalization, growth guards, and periodic exact
  refactorization.
- **Acceptance:** ≥5× on the fx_bench corpus at n≥64; objective/status
  agreement with current fx on 100% of the accumulated corpus (fx results
  are the project's ground truth — the new engine must match the old one
  *everywhere*, since the old one is right by construction).

### 9.2 Precision escalation ladder
- i64 → i128 → fixed multi-limb (from-scratch, bounded limbs, explicit
  capacity status — never silent wrap). Overflow beyond capacity is an
  honest status, not a wrong digit.
- **Acceptance:** adversarial coefficient families that overflow i128
  deterministically report the capacity status; golden-digit comparisons vs
  Python `fractions` on the capacity-fitting corpus.

### 9.3 Floating-point-filtered exact arithmetic
- Double arithmetic + error bounds on the fast path (survey §5 "progressive
  precision"): exact operations only when the filter's bound straddles a
  decision. Directed rounding (already in-tree via `mip_box_conflict`) is
  the filter primitive.
- **Acceptance:** ≥10× over always-exact on well-conditioned instances
  with identical outputs; filter-vs-exact disagreement is a build-failing
  bug in the differential harness.

### 9.4 Exact sensitivity / parametrics lite
- Exact optimal basis ⇒ exact ranges for c and b (rational output).
- **Acceptance:** ranges validated by re-solving at interval endpoints.

### 9.5 Exact QP on the fx substrate *[research][stretch]*
- Active set with rational arithmetic; the KKT verification machinery
  already exists to police it.
- **Acceptance:** matches double QP where double is certified; decides
  families where double QP numerically fails.

**Phase 9 exit criteria:** promote-to-exact costs ≤ target budget on the
scale-mixed corpus (documented per instance); fx remains the zero-mismatch
ground truth; capacity semantics documented.

---

## 7. Phase 10 — Finite-domain CP engine v2 (`fz_cp.inc`)

Goal: a real propagation engine, so feasibility-first models stop paying the
big-M + LP-relaxation toll, and `UNKNOWN` zones of the MiniZinc suite shrink.
Today's engine: B&B + domain bitmasks + a handful of propagators, already
correct — this phase is about *strength*.

### 10.1 Propagation infrastructure
- Priority-queued propagator scheduler, watched-variable lists, trail with
  timestamped domain events (for no-goods), fixed-point detection, and
  propagation counters exposed in `%%%mzn-stat`.
- **Acceptance:** domain histories replayable (deterministic trail);
  per-propagator cost profiles documented.

### 10.2 Propagator library upgrades
- `all_different`: bounds-consistency with Hall intervals (Régin domain-
  consistency is the **[stretch]** upgrade — matching-based, from scratch).
- `table`: compact-table (bitset supports + watched tuples).
- `element`: domain-consistent for small tables, bounds otherwise.
- `cumulative`: timetable + **edge finding**; `disjunctive`: edge finding +
  not-first/not-last. **This closes the known decline-to-MIP gap**
  (`gecode_schedule_unary`, open-shop class currently paying MIP prices).
- `circuit`/`subcircuit`: basic pruning (required/forbidden arcs) on top of
  the existing exact encodings.
- `diffn`: sweep + cumulative decomposition consistency.
- **Acceptance per propagator:** brute-force verified on exhaustive small
  domains (the project's established pattern); strength demonstrated by
  pruning-count tables; propagation is *sound by margin* — a propagator may
  never delete a value that participates in any solution (differentially
  enforced).

### 10.3 Search v2
- Activity/impact-based variable selection, Luby restarts, no-good
  recording from failed subtrees (1-UIP-lite over the trail), LNS for
  optimization (relax a random fragment, re-impose incumbent bound).
- **Acceptance:** suite-wide fixpoint counts and times; determinism under
  restarts (identical trails across runs); optimization objective parity
  with the exact MIP reference on all bounded models.

### 10.4 CP/MIP portfolio dispatch
- Feasibility-first: CP races MIP with deterministic arbitration (first
  *certified* verdict wins; both sides keep budgets; loser state discarded).
  Optimization: CP finds incumbents, MIP/exact proves bounds (whose
  certificates remain the LP/fx machinery of Phases 6–9).
- **Acceptance:** no model in the suite gets slower than the better of the
  two engines today by more than 10%; portfolio verdicts identical to both
  solo engines.

### 10.5 Lazy clause generation *[research][stretch]*
- SAT-style explanation recording from propagators; nogood learning across
  restarts. Ambitious but the trail infrastructure (10.1) is chosen to be
  LCG-compatible from the start.
- **Acceptance:** proof-logging hooks compatible with Phase 16; measured on
  no-good-heavy families.

**Phase 10 exit criteria:** scheduling-suite wall time down ≥5× vs the
MIP-decline baseline at equal verdicts; suite UNKNOWN count published and
reduced; every propagator brute-force certified.

---

## 8. Phase 11 — FlatZinc bridge v2 & MiniZinc completeness

Goal: stop paying for flattening. Keep globals *global* end-to-end, complete
the predicate surface, and make psolve a credible MiniZinc citizen measured
the way the community measures.

### 11.1 Native redefinitions library
- Ship `share/minizinc/psolve/` redefinitions so globals reach the bridge
  unflattened (predicate dispatch → CP propagator / MIP encoding / declined
  with honest UNKNOWN, per a documented routing table).
- **Acceptance:** suite diffs prove routing (statistics show propagator
  use); a routing-*table doc* in docs lists every predicate → disposition.

### 11.2 Predicate surface audit vs MiniZinc 2.9.x stdlib
- Mechanical diff of stdlib predicate inventory vs handled set; close the
  cheap gaps first (string/of-predicate-only variants, `among` families on
  bool, `arg_sort`, `sliding_sum` variants, set-domain elements where
  finite).
- **Acceptance:** docs matrix "supported / declined-by-design / UNKNOWN with
  reason" regenerated from code, not by hand.

### 11.3 Search annotations & `-f`
- Honor `int_search`/`bool_search`/`seq_search` variable orders and
  indomain strategies (map to CP/MIP branching hints); give `-f` (free
  search) a real, documented policy instead of today's accepted-no-op.
- **Acceptance:** annotation-driven order changes observable in trails;
  semantics tests lock each strategy.

### 11.4 Incremental / warm-start FlatZinc *[research]*
- Re-solve with added constraints or a changed objective without rebuilding:
  leverages 6.3 contexts and 7.4 advanced starts.
- **Acceptance:** interactive-driver scenario test (add 1 row × 100 steps)
  ≥5× vs cold; verdicts identical to cold solves.

### 11.5 Output & statistics parity
- `-a`/`-i`/`-p` parity with driver expectations, deterministic solution
  order, full `%%%mzn-stat` set MiniZinc Challenge tooling expects
  (propagations, conflicts, restarts when those exist).
- **Acceptance:** `minizinc --solver psolve` driver tests (MiniZinc's own
  driver test-suite subset) pass locally.

### 11.6 Challenge-track benchmarking
- Curate a license-clean benchmark set mirroring MiniZinc Challenge
  categories; track normalized scores vs gecode/chuffed/cp-sat in
  `docs/MINIZINC_BENCHMARK.md` (regeneration rule preserved: results JSON
  under version control, semantic diff enforced).
- **Acceptance:** scoreboard published per release; regressions gated in CI
  (Phase 14).

**Phase 11 exit criteria:** UNKNOWN and decline counts in the suite reduced
versus the 2026-08-15 baseline with witnesses; driver parity green.

---

## 9. Phase 12 — Real-time kernels v2 (physics hot loop)

Goal: honor the project's founding latency promise: deterministic,
zero-malloc, microsecond solves per frame, with the evidence to prove it on
more than one machine.

### 12.1 Arena + budgets (from 6.6 harvest)
- Thread-local arena finalized; per-frame solve budget API
  (`psv_budget{iterations, microseconds}`) on every entry point including
  batch; overrun ⇒ partial-with-status, never blocking.
- **Acceptance:** allocation counters prove 0 malloc/solve; latency
  histograms under worst-case inputs documented.

### 12.2 Contact block solver
- Box2D-style 2×2 (and 3×3 with friction) block specialization inside the
  fixed-point kernel: direct solves for diagonal blocks, PGS across blocks.
- **Acceptance:** iteration-to-tolerance down ≥30% on contact corpora;
  bit-identical across platforms preserved (fixed-point rules unchanged).

### 12.3 SIMD island batch (AVX-512 first, NEON study after)
- Gather/scatter over independent contact systems in SoA layout (survey §4
  "island parallelism"); deterministic reduction order by construction
  (islands independent ⇒ result placement order fixed).
- **Acceptance:** ≥4× on 64+ island batches; bitwise equality vs scalar
  path asserted in tests on every input batch.

### 12.4 Cross-platform bit-determinism harness
- Golden vectors from the fixed-point kernels recorded in-repo; CI runs
  (Phase 14) replay them on baseline x86-64, AVX2, AVX-512, and (via qemu)
  arm64.
- **Acceptance:** the determinism *promise in the README* becomes a tested
  property, not prose.

### 12.5 Host-integration kit
- Island extraction/serialization API (host brings threading; psolve stays
  thread-agnostic per non-goals), replay log format for physics debugging,
  and a minimal headless demo loop (text rendering) in examples/.
- **Acceptance:** an external toy crate (examples/) consumes only the public
  API; replay dumps diff-clean across repeated runs.

### 12.6 Subspace/acceleration study: Chebyshev/accelerated PGS *[research]*
- Per survey §3: subspace acceleration over PGS iterates; must keep
  fixed-point determinism (rational weights) or stay float-reference-only
  with the fixed kernel authoritative.
- **Acceptance:** convergence graphs; determinism statement written.

**Phase 12 exit criteria:** published µs budget table per kernel on 2
machines; zero-malloc proven; determinism harness in CI.

---

## 10. Phase 13 — QP v2

### 13.1 Proximal/ADMM engine (OSQP-style, from scratch)
- First-order method for large sparse convex QPs; fixed tolerance ladder
  with honest statuses; warm-start native (MPC-shaped workloads).
- **Acceptance:** differential vs active-set + scipy on ≥10k QPs;
  certificates (KKT) decide acceptance of its answers, same as today.
### 13.2 MPC sequence API
- Solve-a-sequence with model deltas (c, b, bounds), reusing factorizations
  where the sparsity pattern is unchanged.
- **Acceptance:** amortized cost/lookahead δ demonstrated; identical
  answers to cold solves.
### 13.3 PSD enforcement honesty
- Near-indefinite detection with interval-checked minimum-eigenvalue bound;
  non-PSD inputs get explicit status (never a silent regularize-and-report).
- **Acceptance:** adversarial Q families classified correctly vs exact
  reference.
### 13.4 Box-QP unification study
- PGS fixed-point kernel vs active-set: crossover solve (PGS warm → active
  set finish) on box QPs; documented rule for which engine owns which class.
- **Acceptance:** decision rule + evidence table in DESIGN.md.

---

## 11. Phase 14 — Infrastructure, hardening & QA v2 *(continuous stream)*

### 14.1 CI pipeline (new)
- Matrix: {gcc, clang} × {baseline, AVX2, AVX-512} × {ASan+UBSan, LSan,
  TSan-smoke, MSan where feasible}. The full `test.sh` battery per cell;
  MiniZinc bundle cached for the mzn cells.
### 14.2 Coverage-guided fuzzing infrastructure
- libFuzzer/AFL harnesses for all parsers (deps build-time-only, default
  off); committed seed corpora; crash corpus regression rule (every
  historical fuzz crash becomes a committed seed).
### 14.3 Mutation testing of verdicts
- Deliberate 1-line algorithm mutations (sign flips, off-by-one, margin
  removal) must be caught by the existing battery; mutations that survive
  expose test-suite holes → new tests land.
- **Acceptance:** kill-rate published; ≥95% target on solver-core mutations.
### 14.4 Benchmark gate
- Deterministic perf harness (fixed machine docs), A/B gating in CI:
  >5% regressions fail; results appended to a history file for trend lines.
### 14.5 Release engineering
- Semantic versioning, changelog from AUDIT addenda, umbrella header
  `psolve.h`, **amalgamation target** (single-file `psolve.c` + `psolve.h`
  distribution — the natural shipping format for a zero-dependency C
  library), soname policy for the shared build.
### 14.6 Semantics sheets (backbone for 6.8) + numerical glossary
### 14.7 Developer docs
- `docs/INVARIANTS.md`: the certificate invariants per engine in checklist
  form, linked from every non-obvious prune site.

---

## 12. Phase 15 — Scale & platforms

### 15.1 Threading policy decision (documented)
- Zero-dependency rule vs parallelism: resolution — core stays
  thread-*agnostic* and reentrant (per 6.3); an **opt-in** POSIX-threads
  build flag provides parallel B&B (8.8) and batch islands; embedding hosts
  may always thread around us.
### 15.2 WebAssembly target *[stretch]*
- Emscripten build (build-time toolchain only; runtime stays dependency
  free); browser demo solving MiniZinc/LP examples client-side;
  determinism checked against native golden vectors (fixed-point kernels
  are bit-stable by construction — the float engines get tolerance-checked
  goldens only).
### 15.3 Python binding
- ctypes wrapper + typed convenience layer, examples, and a verification
  notebook that re-checks answers with fractions (echoing 6.1's ethos).
### 15.4 Embedded profile
- `-DPSOLVE_EMBEDDED` build: no stdio kernels, arena-only allocation,
  documented worst-case stack usage per entry point; pgs_fixed + fx only.
### 15.5 Large-model memory diet
- Audited `int32_t` indexing ceilings made explicit with honest overflow
  statuses (no silent truncation at 2³¹); optional 64-bit index build.

---

## 13. Phase 16 — Certificates, proofs, formal trust *(the ambition apex)*

Goal: psolve's verdicts become **machine-checkable artifacts**, verified by
an independent in-tree checker small enough to audit in one sitting.

### 16.1 LP certificates
- Export primal+dual solutions and rays; checker verifies feasibility,
  complementary slackness, duality gap = 0, Farkas conditions — all in
  exact rational arithmetic (fx substrate) or directed-rounding intervals
  with the safe-side rule.
- **Acceptance:** checker (≤1.5k lines, standalone) re-verifies 100% of the
  corpus verdicts; adversarially corrupted certificates rejected 100%.
### 16.2 MIP proof logging (VIPR-style)
- Exact-rational tree certificate: LP relaxations (16.1), branch bounds,
  cut derivations with coefficients, objective cuts. Format inspired by
  VIPR (VERIfied Proof Results); reader+checker in-tree.
- **Acceptance:** end-to-end: solver logs → independent checker validates
  optimal AND infeasible MIPs from the suite; corrupted-log rejection 100%.
### 16.3 CP UNSAT proof logging *[research][stretch]*
- LCG-style clause logs or a simpler domain-splitting tree proof; checker
  replays propagation.
### 16.4 Replay/persistence layer
- Binary snapshot of any problem + verdict + certificate; `psolve-verify`
  CLI replays the check offline (audit-friendly artifact for users filing
  bugs).
### 16.5 Targeted formal verification *[stretch]*
- CBMC/Frama-C on the arithmetic heart (fx rational ops, fixed-point PGS
  division/rounding, fz_cp domain lattice ops): memory safety + absence of
  overflow on bounded inputs, mechanically.
- **Acceptance:** reports archived in docs; any found defect fixed with a
  discriminating regression per the constitution.

---

## 14. Phase 17 — Applications & frontier tracks

Ambitious but bounded; each begins as a **[research]** spike with a written
go/no-go.

| # | Track | Sketch | First deliverable |
|---|-------|--------|-------------------|
| 17.1 | IIS / infeasibility diagnosis | Irreducible infeasible subsets via deletion filter on certified-infeasible rows; bridge prints "why" for failed models | `lpsolve --iis` on the preserved false-infeasible repro |
| 17.2 | Parametric LP/QP + MPC codegen | Exact optimal-basis partitions for small parametric models; export evaluation code as dependency-free C | examples/ + golden-test vs fx |
| 17.3 | Lexicographic multi-objective | Sequenced optima with objective-fixing; honest partial reporting | CLI + differential vs brute force |
| 17.4 | Sensitivity API surface | Shadow prices/ranges through 9.4 exact substrate | documented `psolve.h` section |
| 17.5 | SOS2 / piecewise-linear | Native SOS2 sets (survey §5) for float PWLA constraints; bridge routes MiniZinc PWLA patterns | semantic tests + suite deltas |
| 17.6 | Indicator constraints | ν-form via branching not big-M where M is unprovable | UNKNOWN-count reduction on indicator families |
| 17.7 | Two-stage stochastic LP *[stretch]* | SAA + Benders decomposition on the LP v2 core | spike report |
| 17.8 | Differentiable-layer demo *[stretch]* | Tiny KKT-differentiation example (ML-adjacent), docs-only experiment | notebook in examples/ |
| 17.9 | Global/QCQP | **Remains a non-goal** (see Appendix D) — spatial B&B over quadratics is out of charter unless Phase 13.1+16 land and a host project funds it | n/a |

---

## 15. Cross-cutting process (always on)

1. **Harvest rule.** Every merge window starts by surveying *all* remote
   branches ("see what others do"); unmerged commits get port/reject
   dispositions in AUDIT.md with evidence — never trusted at face value,
   never silently dropped. Current inventory: Appendix B.
2. **Calibration rule.** Every new differential/regression ships with its
   pre-fix failure count in the commit message; absence of that number is a
   review blocker.
3. **Docs regeneration rule.** `tools/mzn_bench.py` results JSON stays under
   version control; semantic fields diffed (status/objective/verdict/
   solutions); benchmark docs regenerated whenever engines change.
4. **Changelog honesty.** Status improvements are reported with both pre
   and post numbers and the methodology; no claim without a replicable
   command line.
5. **Quarterly adversarial review.** One audit-style pass per quarter in the
   established style (baseline → batteries → addendum → roadmap scorecard
   update in §18).
6. **Conflict-merges double-diff rule.** Any hand-resolved merge is diffed
   line-by-line against *both* parents before sign-off (meta-lesson of
   record).

---

## 16. Risk register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Exact bigint blow-up makes promote-to-exact unaffordable | Medium | High (6.1 stalls) | 9.3 filters; capacity statuses; bounded promote budget with UNKNOWN fallback |
| Farkas interval check false-negatives flood exact re-solves | Medium | Medium | Margin studies on the 121/122 corpus; bound-extreme rule is conservative by construction |
| Cuts/heuristics reintroduce wrong answers | Medium | Critical | Constitution 1+2; cut certification gate; A/B forced-on/off differentials per landing |
| Deterministic parallelism subtly diverges | Medium | High | Canonical reduction order; 1-vs-N bitwise corpus gate in CI; parallel never affects proofs |
| MiniZinc stdlib churn breaks bridge | High | Medium | Mechanical predicate diff per release (11.2); pinned version per CI cell |
| Interior point crossover can't be made certificate-clean | Medium | Low (feature is additive) | Ship satisfaction-only or drop with written negative result (constitution 4) |
| Single-maintainer bandwidth | High | High | Phases are independently shippable; riskiest items are gated behind correctness closure, not interleaved with it |
| AVX-512 portability shrinks audience | Medium | Low | Baseline/AVX2 builds in CI gate; kernels.c compile-time fallbacks exist |
| CP propagator strength bugs (unsound value deletion) | Medium | Critical | Brute-force certification per propagator (10.2 rule); sound-by-margin propagation tests |
| Formal-methods tooling (16.5) proves too costly | High | Low | Stretch-marked from day one; partial artifacts (bounded proofs of fx ops) still valuable |

---

## 17. Milestone schedule (ambitious targets)

| Milestone | Contents | Target |
|---|---|---|
| **M1 Trust closure** | Phase 6 complete (6.1–6.8) | 2026 Q4 |
| **M2 Exact muscle** | Phase 9 (9.1–9.4) | 2027 Q1 |
| **M3 LP v2 core** | 7.1–7.5 land; perf table published | 2027 Q2 |
| **M4 MIP v2 core** | 8.1–8.5, 8.7; suite node-count table | 2027 Q3 |
| **M5 CP v2 core** | 10.1–10.3; scheduling decline-gap closed | 2027 Q3 |
| **M6 Bridge v2** | 11.1–11.5; challenge-track scoreboard live | 2027 Q4 |
| **M7 Real-time v2** | Phase 12 + 14.1–14.5 (CI, releases, amalgamation) | 2027 Q4 |
| **M8 Proof-carrying solver** | Phase 16.1–16.2 (VIPR-grade MIP proofs) | 2028 H1 |
| **M9 Scale** | 15.x per resourcing; IPM (7.6) go/no-go | 2028 H1 |
| **M10 Frontier** | 17.x spikes per host-project pull | rolling |

Interleave note: Phases 13/14/15 run as background streams from M2 onward;
nothing in M3–M6 may land without the Phase-14 CI cell being green first
(process rule: infrastructure precedes the features it must police).

---

## 18. Scorecard (reviewed quarterly against §15 rule 5)

| KPI | 2026-08-15 baseline | 2027 target | 2028 ambition |
|---|---|---|---|
| MiniZinc suite pass | 77/77 | 77/77 + annotation parity | + challenge-track score published |
| Suite UNKNOWN/declined count | baseline recorded in bench JSON | −30% | −60% |
| MIP: geomean nodes on differential corpus | 1.0× | ≤0.5× | ≤0.25× |
| LP: time vs 2026 baseline on sparse corpus | 1.0× | ≤0.6× | ≤0.4× (with IPM where it wins) |
| Exact re-solves per tsp5-class model | 122 | ≤12 (Farkas) | ≤12 sustained at scale |
| Verdicts with exported machine-checkable certificate | 0% | LP 100% | MIP 100% of suite models |
| Fabricated-verdict families known open | 0 | 0 | 0 (defined invariant) |
| Fuzz crash-free hours (rolling) | n/a | ≥100 | ≥1000 |
| Mutation kill rate (solver core) | unmeasured | ≥90% | ≥95% |
| Real-time kernels: golden-vector platforms | 1 | 3 (CI) | 4 (+wasm) |
| Docs freshness (% of src/ tolerance literals documented) | partial | 100% | 100% enforced by lint |

---

## Appendix A — Disposition of AUDIT.md "Not done" items

| AUDIT item | Disposition |
|---|---|
| 2. setjmp protocol redesign | **Phase 6.3** (M1) |
| 3. Farkas-certificate fast path | **DONE 2026-08-15(5)** (Phase 6.2): tsp5 exact re-solves 122 → 0, 2.1× wall |
| 4. Scale-mixed non-integral honesty gap | **DONE — Phase 6.1, 2026-08-15(6)** (rescue + exposure-gate promotion; `tools/lp_scale_verify.py`); exact substrate 9.x would make promotion *cheap* (M2) |
| B. Phase-I degenerate-infeasible convergence | **Phase 7.4/7.5** — the 7.5 scaling half **landed 2026-08-18(13)** (rescue contract + CLI fallback); 7.4 crash bases remain, 6.1 stays the honesty backstop |
| CP scheduling globals decline to MIP | **Phase 10.2** (edge finding) — closes `gecode_schedule_unary`/disjunctive gap |
| `-f` free search no-op | **Phase 11.3** |
| Phase-2 VG/UI kernels (unstarted) | **Track 17.x by host pull** — kernels own no frame budget until a consumer exists; physics (12) stays priority |
| Phase-5 leftovers (SIMD batch, Python, CI budget) | **12.3 / 15.3 / 14.4** respectively |

## Appendix B — Live branch inventory & harvest dispositions (as of 2026-08-15)

| Branch | State | Disposition |
|---|---|---|
| `arena/cp-engine-correctness` (HEAD) | active | home of this roadmap; periodically merged into `main` by the integration pass |
| `arena/phase4-interactive-hardening` | fully dispositioned | `3573e3a` FBBT **ported** (`18eb475`); `48712f5` + `a42be76` **ported** via `arena/phase4-ports` → merged in `main` (`f098876`), verified 2026-08-15(5) |
| all other `arena/*`, `feat/*`, `fzn-table-constraint`, `mzfnsh` | fully merged (ahead 0) | none standing; re-survey each window (rule §15.1) |
| `main` | **current** (contains this branch + phase4 ports) | the integration tree; quarterly review target |

## Appendix C — Technique → implementation map

| `SOLVER_OPTIMIZATIONS.md` section | Phase(s) |
|---|---|
| §2.1 presolve | 7.1 |
| §2.2 dual steepest edge | 7.2 |
| §2.3 crash bases | 7.4 |
| §2.4 Forrest–Tomlin sparse LU | 7.3 |
| §3.x conflict graphs, cuts, FP/RINS, reliability branching | 8.1–8.4 |
| §3.x block PGS / subspace / islands (physics) | 12.2–12.6 |
| §5 progressive precision / domain expansion | 9.2–9.3, 6.1 |
| §5 CSE / SOS2 / bound consistency (bridge) | 11.1–11.2, 17.5, 10.2 (Hall intervals) |

## Appendix D — Non-goals reaffirmed (and new ones)

- No general global/QCQP optimization (unless 17.9's preconditions land).
- No GUI, asset pipeline, or threading *framework* (host threads around us;
  opt-in flag only, per 15.1).
- No runtime third-party dependencies — ever (constitution 3). Bindings and
  WASM are packaging, not dependencies.
- No heuristics that can influence a proof (heuristics find, certificates
  decide — the phase-8 design constraint is permanent).
- No untested "ports" from other branches or solvers (harvest rule §15.1).
- No silent precision/semantics changes: any tolerance, margin, or rounding
  change is a documented event with pre/post differential numbers.
