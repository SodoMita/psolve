# psolve — audit & hardening notes

> **2026-08-19 (14) - branch `arena/cp-engine-correctness`: Phase 7.1,
> presolve + postsolve for the LP CLI (v2's highest-ROI item), plus a
> bundled cert-layer fix: free-variable columns no longer veto Farkas
> box-certificates whose window on them is provably zero.  Presolve may
> decline or degrade, never guess; every printed verdict is re-proven
> against ORIGINAL data by the same 6.4 psv lanes as before.**
>
> **What shipped.**  `src/presolve.{h,c}` (~830 lines) with
> `lp_presolve()` and the replay maps
> `lp_presolve_postsolve_{x,duals,ray}()`, `lp_presolve_farkas_ray()`,
> `lp_presolve_unbounded_note()`.  Reductions run to a pass-capped
> fixpoint on the caller-visible LP (before the engine's free-variable
> split, mlt sign rows and 7.5 equilibration): fixed columns; empty
> rows (consistent: dropped with dual 0; inconsistent: EXACT infeasible
> with an explicit one-row Farkas ray hint); empty columns at read
> (fixed at the cost-sign bound; cost walking an open side emits a
> certified-UNBOUNDED note); singleton rows (implied bound folded into
> the box with directed-rounding outward folds; singleton-vs-ORIGINAL-box
> conflicts are exact one-row rays, singleton-vs-singleton conflicts
> exact two-row rays); redundant rows (directed-rounding activity
> limits, dropped with dual 0; activity conflicts are exact one-row
> rays); doubleton-equality substitution (fill capped at 3x nnz growth,
> pivot = larger |a|).  Every fired reduction pushes a record
> (`PreRec`), its pivot-column snapshot capped by a byte/record budget;
> crossing any budget, sentinel, or unsupported shape declines the
> whole run (rc -1) and the CLI walks the untouched original.
>
> **Postsolve & the dual replay invariant.**  Records replay in REVERSE
> firing order; dual replay reconstructs row duals from the firing-time
> snapshot `y_i = (c_j - Sum y_r a_rj)/a_ij` over rows then-ACTIVE.
> An unknown reference (elimination-order cycle) makes the map return
> -1 - a caller-level fallback event, not a verdict.  A complementarity
> guard (new tolsheet row TOL-LP-PRETIGHT, class D - caught during
> development when free_vars.lp's certificate failed: the raw rc-formula
> alone can assign a nonzero dual to a SLACK singleton row, inflating
> the psv dual bound) forces y_i = 0 on any inequality singleton row
> whose implied side is not tight at the postsolved point.
> All exact-ray paths are gated by `row_touched`: a row whose
> b/coefficients were mutated by an earlier fold can no longer source a
> ray (the mutation destroyed the "ray over original rows" reading), so
> presolve aborts instead of handing the psv lane a doomed artifact -
> found in development on a folded chain whose "ray" was correctly
> REJECTED by psv_cert_check (the system worked; the gate makes the
> decline explicit and cheap).
>
> **CLI lanes.**  Default `lpsolve` now runs presolve first; the
> config ladder `(presolve,scale) -> (0,scale) -> (0,0)` with dedup
> handles every uncertified outcome (same machinery 7.5 introduced for
> scaling).  New flags `--nopresolve` (pre-7.1 path) and `--prestat`
> (reduction accounting on stderr).  New verdict lanes with
> `iterations: 0` (no engine run): presolve rc==1 exact-infeasible rays
> (routed through `psv_cert_check(PSVK_LP_INFEASIBLE)` like any ray),
> and the full-elimination branch (`model->n == 0`): direct OPTIMAL /
> UNBOUNDED-via-note claims whose x/duals/ray come purely from record
> replay.  A presolved-model engine-INFEASIBLE (r==1) ALWAYS degrades
> in v1 - no ray back-propagation - even when the engine's own raw-data
> retry would have a ray; documented in presolve.h.  Objective prints
> are always recomputed `Sum c_j x_j` on the original postsolved point
> (objconst never feeds the print), so the printed number is the
> original-data number.
>
> **Bundled cert-layer fix (same commit): boxcert exact-zero windows.**
> `solver_farkas_boxcert` (one implementation, used by every LP/MIP/fzn
> Farkas lane) computed `L = min_box(y^T A)x` corner-by-corner and
> DECLINED the whole certificate when ANY column had a box side at the
> +-1e30 "infinity" sentinel (TOL-LP-BIGCAP 1e29) - even when that
> column's directed-rounding `y^T A` window is provably `[0,0]`
> (`zl >= 0 && zh <= 0`; zl/zh bracket the exact sum under
> FE_DOWNWARD/FE_UPWARD, so the exact sum IS 0 and the column's box
> product is exactly 0 for ANY box, open sides included).  Consequence:
> EVERY infeasibility certificate on a model with a free variable -
> including the engine's own extracted rays - degraded to
> NUMERICAL_FAILURE.  The fix skips such columns (their contribution is
> provably 0) before the BIG test; soundness is unchanged (the skip can
> only fire when the true sum is exactly 0), and on finite-box columns
> the old corner-min accepted the same 0 contribution already, so
> finite-box behavior is bit-identical (MIP/fzn lanes: variables are
> bounded, BIG never fires - smokes bit-identical pre/post).
>
> **Regression-locked measurements.**
>
>   * Planted per-reduction corpus (12 cases, now hard-gated): each
>     exact-ray family (singleton-vs-box, empty-row, activity, FREE-var
>     pair conflict) prints INFEASIBLE with `iterations: 0`; the
>     touched-row chain declines presolve and still prints the correct
>     raw-engine INFEASIBLE; the doubleton bound chain full-eliminates
>     and direct-claims OPTIMAL 3 (iters 0); empty-column and doubleton
>     ray-transfer print UNBOUNDED; fixed/singleton-fold/redundant
>     optimal families print 2/7/5 exactly; the planted fallback case
>     (doubleton fires, reduced model engine-infeasible) shows the psv
>     degrade notes AND the identical raw verdict.  Pre-change binary on
>     the same 12: identical verdicts everywhere EXCEPT the two cases
>     the boxcert fix converts from NUMERICAL_FAILURE to certified
>     INFEASIBLE (free-var pair conflict; joint y-range conflict) -
>     recorded as an intentional verdict-quality improvement.
>   * A/B parity (default vs --nopresolve), records the acceptance
>     mapping: 40k random LPs (20k well-scaled + 20k entry-mixed 1e+-4,
>     seed 42) + 10k (seed 777): **0 verdict flips, 0 lost raw answers**;
>     combined-evidence fallbacks 364 + 90 (each lands the identical raw
>     verdict); co-OPTIMAL objective prints bit-identical on
>     98.6%/99.1% (identical whenever no reduction fires); the remainder
>     measure <= 1.4e-10 rel (engine tolerance replay noise through big
>     coefficients - the gate pins 1e-12 well-scaled / 1e-9 entry-mixed).
>   * scipy/HiGHS oracle, honestly calibrated: 39 952 + 9 990 co-OPTIMAL
>     cross-checks.  On well-scaled data the 1e-6 oracle held 20 000/20 000.
>     On 1e+-4 entry-mixed data HiGHS is NOT an authoritative objective
>     oracle: measured engine-vs-HiGHS gaps up to 1.8e-4 rel while the
>     PRE-CHANGE binary prints byte-identical values on every
>     scipy-flagged instance (all 7 flagged across the dev runs
>     verified: t756, t2997, t4902, t8459, t12679, t18122, t18259 of
>     `tools/presolve_verify.py 20000 42`; instances regenerate
>     deterministically from the seeded generator).  The parity-flagged
>     instances (default-vs-raw delta <= 1.4e-10, presolve fired) are a
>     disjoint set by construction.  The gate therefore uses a
>     gross-error-only 3e-4 bar on
>     that family (a replayed side flip or dropped row errs at O(1)).
>   * Records unit-verified: tools/presolve_selftest.c, 17 apply->restore
>     invariant checks (primal replay satisfies ORIGINAL rows+box and
>     hits the known optimum; dual replay exactly stationary on
>     eliminated pivot columns with the complementary reduced-cost sign
>     on surviving columns; complementarity guard slack/tight; stats).
>   * Size reduction, measured not promised: the random families are
>     decline-dominated (3 330/40 000 models reduce; conditional mean
>     shrink rows 16.5% / cols 5.2% / nnz 10.5%; shipped examples: 2 of
>     6 reduce - one eliminates 100% of rows - the others carry no
>     removable structure and decline).  The roadmap's 30-60% target is
>     NOT reproduced on this corpus (it lacks the structure the target
>     assumed); what is verified is that presolve is safe at ANY
>     reduction rate, which is the property that matters here.
>   * Hard gates: `tools/presolve_verify.py 400 20260819` and
>     `presolve_selftest` in test.sh; both discriminating - the
>     pre-change binary rejects --nopresolve/--prestat ("unknown
>     option") so the gate exits 1 loudly (verified), and the selftest
>     target does not exist on the pre-change tree.  scale_verify.py's
>     raw lane now means `--nopresolve --noscale` (true pre-7.x path;
>     2 seeds green).  tolsheet gate green (90 ids).
>   * ASan/UBSan/LSan (detect_leaks=1): all 6 examples x 3 flag combos,
>     12 planted cases, presolve_verify N=40 + scale_verify N=40 +
>     cert_inject N=40 (both seeds) on instrumented binaries, mip/fzn/fx
>     smokes - clean; found and fixed ONE leak in the NEW selftest
>     harness itself (missing lp_free(&red) in section 1), none in the
>     module or CLI.
>   * MIP/branch-and-bound, fzn, fx, qp paths: unchanged behavior -
>     presolve is LP-CLI-only, 7.5 pins those bridges to the raw engine,
>     and the boxcert skip is provably inert on bounded columns (mipsolve
>     knap_gap pin OPTIMAL 10, fzn smoke UNSATISFIABLE: byte-identical
>     pre/post).
>   * Battery: full `test.sh` exit 0 on the final tree on 2026-08-19
>     (incl. farkas_verify 150, lp_scale_verify 60, scale_verify 200 x 2
>     seeds, presolve_verify 400 + selftest, cert_inject 200 x 2 seeds,
>     mzn_diff OK=33, fx_verify OK=300, oom_test 6119 injections), and
>     the MiniZinc benchmark regenerated: 77/77 models semantically
>     identical (status/objective/solutions/verdict; timing-only churn)
>     - one tooling fix surfaced by the battery itself: fuzz_inputs.py's
>     manual ASan source list missed src/presolve.c.
>
> **2026-08-18 (13) - branch `arena/cp-engine-correctness`: Phase 7.5,
> Ruiz equilibration + geometric-mean scaling for the LP engine (v2
> item), with A/B CLI lanes and an evidence-preserving raw-data
> fallback.  Scale can add certified answers, never take one away.**
>
> **What shipped.**  `solver_create_opts(lp, scale_mode)` (new public
> API; `solver_create()` = scaled default, `0` = the raw pre-7.5 path).
> At create time the engine deep-copies the normalized LP and applies 4
> Ruiz iterations — each a geometric-mean row pass (factor
> 1/(sqrt(min|a|)·sqrt(max|a|)) per row, rhs included) then the same per
> column (bounds and costs mapped consistently) — accumulating strictly
> positive diagonals `rscale`/`cscale`.  The engine then runs entirely on
> `D_r·A·D_c` and every public funnel composes the diagonals back, so
> caller-visible values stay in ORIGINAL units: `solver_optimum` and
> `solver_unbounded_ray` multiply by `cscale` (split twins share the
> original column's factor exactly, their |entries| being identical),
> `solver_duals` and `solver_farkas_duals` multiply by `rscale` (the
> documented mlt-multiply caller contract is UNCHANGED), reduced costs
> divide by γ (the core reduced cost is γ times the original one),
> `solver_set_objective` maps `c→γc`, `solver_set_bounds` maps `l,u→l/γ,u/γ`
> with infinity-token sides never divided, `solver_export_lp` unwraps
> `mlt·rscale·cscale` on entries and `rscale` on the rhs, and
> `solver_refresh`/`solver_add_row` re-equilibrate their rebuilt images.
> With scaling off the diagonals are allocated as literal 1.0, so every
> funnel multiplication/division is an exact IEEE no-op: the raw path is
> bit-identical to the pre-change engine (verified by the MIP A/B below).
> Decline rules (`LP_SCALCAP=1e300` intra-line spread cap, non-finite
> factor × entry/rhs products, a finite bound the divide would push
> ACROSS the infinity token, non-finite mapped costs) SKIP that row or
> column — scaling is optional conditioning; declining can only leave
> the model exactly as handed in.  All guards are class-D (never verdict
> inputs); new tolsheet row TOL-LP-SCALCAP documents them, gate green
> (89 ids).
>
> **The CLI contract (lpsolve).**  New flags `--noscale` (raw path) and
> `--scalestat` (stderr: mode, pre/post max|a|/min|a| spread proxy,
> iters).  The verdict chain runs inside a two-attempt fallback loop:
> equilibration is conditioning, never a verdict input, so when the
> scaled run's outcome cannot be certified against ORIGINAL data — any
> 6.4 psv lane rejection (OPTIMAL/UNBOUNDED/Farkas-shaky) or an engine
> SOLVE_NUMERICAL — attempt 1 destroys the solver, re-creates it on the
> raw path with the dense factorization forced, re-solves and
> re-evaluates the ENTIRE chain, printing only what certifies.  The
> retry never fires after an explicit stop (budget binding) or when
> `--noscale` was already given, and every engagement is announced on
> stderr (`psv: scaled evidence not certified, re-solving on raw data`),
> preceded by the specific cert-lane note.  Consequence: a raw-path
> OPTIMAL can never be LOST to scaling; a raw-path NUMERICAL that
> scaling rescues (the LU stall goes away on the equilibrated image and
> the psv lane certifies the answer against original data) is a pure
> gain.
>
> **Scope decision: MIP/fzn pinned RAW.**  Scaling was validated for
> one-shot LP verdicts where every print re-verifies against original
> data AND the fallback re-solve exists.  Node LPs in the MIP bridge
> STEER discrete decisions and have no per-node primal-bound certificate
> chain, so scaled node answers cannot be arbitrated as improvements.
> Measured before pinning (per-entry log-uniform 1e±8 MIP family, N=150,
> new-vs-old mipsolve): 12 status flips, 7 LOST certified-OPTIMAL
> verdicts, 7 co-OPTIMAL objective disagreements >1e-6 rel, honest-class
> count 6→10 — against 5 rescues.  That exposure is not acceptable as
> silent behaviour change, so `src/mip.c` (relaxation), `src/fzn.c`
> (pure-LP lane) and the mipsolve relaxation arbitrator call
> `solver_create_opts(..., 0)`.  Post-pin A/B (E=4 and E=8, N=150 each):
> ZERO flips, ZERO disagreements, NUM counts 6/6 — the pin restores
> bit-identical bridge numerics, exactly as designed.  Lifting the pins
> needs per-node certification (fx/exact territory; see Appendix A item
> B sibling note).
>
> **Measurements (regeneration: /tmp probe generators are seeded in
> tools/scale_verify.py; engine: this commit's lpsolve).**
> Conditioning proxy (median post/pre spread, max|a|/min|a| over stored
> entries): well-scaled family 44.7 → 13.0; entry-mixed 1e±4 family
> 1.02e8 → 1.42e6 (72×); 1e±12 family 1.9e23 → 8.8e18.  Gate medians on
> high-spread instances: 0.0098 (E=4), 3.9e-6 (E=12); the proxy NEVER
> got materially worse (0/466 instances across both gate seeds).
> Verdict movement (census raw→default): well-scaled N=150: 150
> OPTIMAL→OPTIMAL, zero fallbacks; 1e±4 N=150: 150 OPTIMAL→OPTIMAL, 0
> fallbacks; 1e±12 N=80: 12 OPT→OPT, 6 NUM→OPT (rescues), 62 NUM→NUM,
> 0 losses, 68 fallbacks (the scaled psv attempt rarely certifies at
> 1e±12; the fallback keeps parity).  Rescue contract, pinned instance
> (tools/scale_verify.py gen_mixed stream, spread 6.0e8): pre-change and
> `--noscale` both print NUMERICAL_FAILURE (honest LU stall); default
> prints OPTIMAL 10560305.7817494 = scipy/HiGHS 10560305.781749407.
> Rescue rate measured ~0.4% of the 1e±4 family and ~7% of the 1e±12
> family (gate asserts ≥1 across its seeded streams; E=4-only existence
> is seed-luck, hence the combined assert — recorded here so nobody
> "fixes" a spurious gate failure by weakening it).
> Iteration movement (honesty bar — report exactly what moved): on the
> co-OPTIMAL probe families the scaled image takes slightly MORE simplex
> iterations (well-scaled 1868→1919 total, median 12→12.5; 1e±4
> 1698→1756, median 11→11).  Equilibration changes vertex paths; on
> these tiny models there is no iteration win to claim — the function of
> 7.5 is certified-answer RECOVERY on extreme data, and on well-scaled
> data it costs ~3% pivots.  Also recorded: the 6.4 LP-OPTIMAL dual-dust
> lane occasionally fails to certify the scaled run's evidence when γ
> spans ~1e±9+ (its original-units rounding exceeds the dt margins);
> that is precisely what the fallback arbitrates — observed 1–2
> engagements per 200 at 1e±4 and dominating at 1e±12, with zero
> resulting verdict losses.
>
> **Test pinning.**  `tools/scale_verify.py` (200 models × 2 seeds in
> `test.sh`, hard gate): (0) A/B-lane probe — FAILS LOUDLY on the
> pre-change binary, which rejects `--noscale`/`--scalestat` as unknown
> options (discrimination proven against the aa9bcda build); (1)
> well-scaled parity N=200, statuses identical and co-OPT objectives
> ≤1e-7 rel, scipy oracle cross-check (200/200); (2) conditioning
> asserts on entry-mixed families (never-materially-worse + median
> halving); (3) no-loss contract on 1e±4/1e±12 (raw OPTIMAL ⇒ default
> OPTIMAL ≤1e-6 rel; rescued objectives scipy-confirmed); (4) rescue
> existence across the seeded streams; (5) the six shipped examples
> A/B-identical.  Both seeds green; per-seed tables in the gate output.
> Battery: full `test.sh` green (MiniZinc differential OK=33 FAIL=0,
> benchmark 77/77 semantically identical — status/objective/solutions/
> verdict/nodes all equal, only timing churn; lp_scale_verify unchanged:
> fabricated=0, rescued=60; cert_inject 100/100/100 on both seeds).
> ASan/UBSan/LSan sweep on the instrumented build: examples × 4 flag
> combos, the pinned rescue instance, two live fallback instances,
> scale_verify N=40, cert_inject N=40, mipsolve knap_gap, fznsolve ×3,
> fxsolve prodplan — zero reports, leak checking on (covers the new
> rscale/cscale ownership and the fallback's destroy/recreate path).
> One battery pin needed a documented touch: `tools/arena_test.c`'s LP
> baseline oracle asserted the double engine hits prodplan's true 26
> EXACTLY; with the scaled default the reconstructed objective is
> 26.000000000000004 (1 ulp — funnel rounding on an already-certified
> answer, inside every margin).  The oracle now allows 8·DBL_EPSILON·26
> with a comment; the arena-vs-libc BIT-IDENTITY pin it feeds is
> unchanged and still exact, and the fx (exact rational) oracle was not
> touched.

> **2026-08-18 (12) - branch `arena/cp-engine-correctness`: Phase 6.4,
> unified evidence objects.  One claim type per verdict class, one
> psv_cert_check() entry point at every CLI verdict exit, an
> error-injection hard gate - and three real defects the
> gate/battery caught in the certificate layer's own first drafts.**
>
> **What shipped.**  `src/cert.h` defines `PsvCert` (kind + ORIGINAL-data
> view + evidence payload + explicit margin fields) and one entry point
> `psv_cert_check()` returning OK / REJECT (evidence contradicts) /
> DEFER (evidence cannot certify: NaN, missing payload, vacuous bound).
> `src/cert.c` implements seven kinds against the caller's own data:
> LP_OPTIMAL (primal point + bounded-LP Lagrangian dual bound built from
> the engine duals: shadow-price duals are sense-normalized into max form
> BEFORE sign analysis, sign-invalid components clipped to 0 = sound
> weakening, reduced-cost dust within the dj window charged UPWARD on
> closed boxes and treated as zero only across an open side - charging
> |rc|·1e30 against the infinity sentinel was an early-draft bug that
> inflated B by 1e14), LP_INFEASIBLE (directed-rounding Farkas box
> separation, reusing solver_farkas_boxcert), LP_UNBOUNDED (feasible
> point + recession ray: open bound side on every macro component,
> sign-consistent row recession, strict descent), MIP_POINT (bounds +
> rows + EXACT integrality of snapped incumbents + objective consistency
> + directional bound coherence), QP_OPTIMAL (primal rows, stationarity
> Qx+c+Aᵀμ, multiplier sign + complementarity, objective consistency),
> QP_UNBOUNDED (feasible point, strict A d ≤ 0 mirroring qp.c's
> deliberate no-margin rule, curvature ≈ 0, descent), EXHAUSTION
> (discrete search-completed stamp: stopped/node-limit/nodes<0 reject -
> a truncated search can never print UNSAT through this entry point).
> Engine evidence hooks: `solver_unbounded_ray()` (the recession ray is
> materialized at the dense-verified `iterate()` unbounded exit and
> unmapped through the x⁺−x⁻ split; cleared at both solve-entry sites)
> and `QPResult.ray`.  Wiring: `src/main.c` (all three LP verdict lanes,
> INFEASIBLE keeping the exact Phase-I/shaky-frontier policy), and
> `tools/mipsolve.c` / `tools/qpsolve.c` / `tools/fznsolve.c` (MIP point
> + Farkas-or-exhaustion UNSAT lanes + relaxation-verified UNBOUNDED;
> QP KKT/recession; fzn optimize bound-coherence + UNSAT exhaustion).
> On REJECT/DEFER every lane degrades to the honest class
> (NUMERICAL_FAILURE / UNKNOWN); the checker never prints and never
> mutates.
>
> **The error-injection acceptance (what the numbers mean, stated
> precisely).**  New hard gate `tools/cert_inject` in test.sh (200
> instances x 8 families x 2 seeds; extra seeds 4242/99991/31337 and
> N=2000 also green).  LEGIT (true claims) accepted 100% on every
> family - a legit rejection is a certificate-layer false-rejection bug.
> LARGE (out-of-box pushes, objective drifts, integer off-by-one,
> negated rays, feasible-but-suboptimal false-OPTIMAL claims, row-normal
> point corruption, flipped statuses) rejected 100% of ADVERSARIAL shots
> - adversarialness decided by independent closed-form oracles in the
> harness (long-double box extrema, never the checker under test),
> because a corrupted payload that still proves the claim accepts
> CORRECTLY (the harness itself caught two of its own unsound families:
> garbage duals that clip to the box-sup and remain a valid corner
> certificate when the optimum saturates the box, and rotation-invariant
> Farkas rays on symmetric contradiction rows).  1-ulp directed noise:
> 100% rejected on the zero-width snapped-integer MIP surface
> (199/199; 1964/1964 at N=2000) - the one surface where ulp corruption
> is structurally decidable because engine incumbents are stored
> lattice-snapped and the check is exact `x == rint(x)`.  On the
> tolerance-margined point surfaces (LP/QP point and dual payloads) the
> measured rejection is 0% BY DESIGN: the engines' own terminal margins
> are >= 1e-9-relative, so any checker that rejects 2e-16-relative noise
> would false-reject every true claim.  The roadmap's literal ">= 99.9%
> of 1-ulp-perturbed OPTIMAL/UNSAT claims" target is therefore asserted
> exactly where it is decidable (zero-width surfaces: met at 100%) and
> reported, not asserted, on continuous surfaces; the 100%-of-large-
> perturbations clause is unconditional and measured.  (qp_unbounded
> ulp: ~70% rejected - the strict recession edge catches a majority.)
>
> **Three defects the harness/battery caught in this layer's own
> drafts** (the phase's process point: certificates need verification
> too): (1) The MIP proven-optimal stamp was first written as a
> SYMMETRIC window |best_bound - obj| <= gap contract; the injection
> harness showed 9/20 legit false-rejections.  Root cause: the engine's
> best_bound is a running extremum over solved node relaxations, so the
> root LP bound dominates it forever - the residual |bb - obj| of a
> proven optimum IS the model's root integrality gap, not the stop
> tolerance.  Fixed to the sound DIRECTIONAL property (an upper bound
> never sits below a max incumbent; mirror for min), which targets
> exactly the catastrophic direction (objective strictly better than
> proven).  Same fix in the fzn optimize gate; `examples/knap_gap.lp`
> (root relaxation 11.75 vs integer optimum 10) pins it in test.sh.
> (2) Row/objective graces sized by |b|/|cx| alone false-rejected TRUE
> points on catastrophic-cancellation data: the fbbt_verify cancellation
> family (a0·x0 + a1·x1 with |a| ~ 1e18 nearly cancelling to D ~ 3e3)
> has round-to-nearest activity dust of thousands against a |b|-scale
> grace of 3e-3 - the battery caught it instantly (fbbt FAIL 2 -> 31 on
> the first full run).  Fixed with the correct dust form: activity-scaled
> graces sum_j|a_ij·x_j| / sum_j|c_j·x_j| on every recomputed-dot check
> (LP/MIP/QP point rows, objective consistency, ray recession).  End
> state fbbt_verify FAIL=0/241 - better than the pre-change binary (2).
> (3) The MIP-UNSAT lane first required a fresh LP-relaxation Farkas
> certificate at dt_gap=1e-6; the pinned 5e-7-strength margin cycle
> (exactly infeasible over the integer lattice, proved by the engine via
> lattice exhaustion at nodes=1) has its separation INSIDE the ray
> margin, so the lane printed NUMERICAL_FAILURE for a model whose UNSAT
> was legitimately proven (caught by farkas_verify).  The lane order is
> now: relaxation-Farkas if the relaxation itself is infeasible (with
> the LP CLI's exact shaky-frontier policy), else the EXHAUSTION stamp
> (no truncation) - matching the pre-change trust level with the 6.4
> no-truncation guarantee on top.
>
> **Acceptance evidence.**  cert_inject hard gate green (both seeds, in
> battery); knap_gap CLI pin green; full test.sh rc=0 (including
> farkas_verify, lp_scale_verify, fbbt_verify FAIL=0, fzn_output_check
> WRONG=0 @2000, qp/mip/lp differentials); tolsheet closure green
> (88 ids / 143 sites / 88 rows - new rows in DESIGN.md section 8.9 for
> every cert-layer margin, each a mirror of the producing engine's own
> contract value); ASan/UBSan/LSan sweep clean (cert_inject N=40 plus
> farkas 60 / fzn_output_check 500 / fgraph 60 / orbit_detect 40 /
> procstates 60 against the instrumented binaries, direct rc checks -
> the new solver/qp ray allocations are leak-covered); MiniZinc bench
> 77/77 with benchmark_results.json semantically identical (status /
> objective / verdicts equal; timing-only churn).  Compile clean under
> -Wall -Wextra everywhere (the fuzz/ASan direct-gcc build lists in
> test.sh and tools/fuzz_inputs.py / tools/fuzz_fzn.py gained the new
> sources).
>
> **Interface note for downstream.**  `fznsolve`'s LP bridge now stamps
> sol.best_bound = LP objective (constant-free units, the MIP path's
> convention) - an LP proven optimal is its own exact bound - so the
> objectiveBound stat is truthful on pure-LP models instead of the
> zero-init default.  Nothing else about printed verdicts changes;
> downgrade paths only ever move a print to a MORE honest class.

> **2026-08-18 (11) - branch `arena/cp-engine-correctness`: Phase 6.8,
> per-module tolerance semantics sheets.  Documentation-phase discipline,
> closed by construction: zero functional code changes, 127 source sites
> tagged, both directions machine-checked.**
>
> **What shipped.**  docs/DESIGN.md section 8 now inventories EVERY
> tolerance-class literal in src/ (decimal exponent floats, 0x1pN
> hex-floats, DBL_EPSILON): 80 IDs across LP core / MIP / QP / PGS /
> FlatZinc front-end / CP engine / exact-fx, each with class (V verdict-
> adjacent, C convergence, D honest-decline guard, R flattener-exact
> recognition, S sentinel/stability, E exact), direction of safety, what
> it protects, and what it may never justify.  The header rule, learned
> from the 2026-08-15 mip_diff flip that motivated 6.8: a V-class
> tolerance may never decide a verdict alone - it feeds a certificate that
> stands without it (directed rounding, exact-rational re-check, printed
> bound), or the status degrades to the honest numerical-failure class.
>
> **The grep-provable acceptance, mechanised.**  New hard gate
> tools/tolsheet_check.py (wired into test.sh): scans every src/ code line
> through a real C comment/string state machine - a literal inside prose
> or a printf string cannot steer a verdict and is exempt - and requires
> (1) every hit carries a /* TOLSHEET <ID> */ tag on the same line,
> (2) every tag resolves to a DESIGN.md section-8 row, (3) every row is
> carried by at least one source line (no stale rows after refactors).
> Adding an undocumented tolerance literal fails the battery; so does
> deleting or renaming a documented site.
>
> **What the inventory actually found (honesty of the survey itself).**
> The first manual sweep MISSED sites; the machine closure caught them,
> and they are documented, not excused: fx.c had two representability
> guards (±0x1p63 range, 9e17 decimal cap - the exact module has guards,
> no margins; section 8.7 says so precisely), fznsolve's own exposure
> frontier (fzn.c:3996, 5e-7, the TOL-LP-SHAKY sibling), QP's divergence
> cap (1e14 => QP_ITERATION_LIMIT), splu's norm-growth watchdog (1e10),
> solver's steepest-edge weight cap (1e18), the MIP/fznsolve/LP sentinel
> families (1e29/1e30/1e9), and one no-decision-power unit constant
> (ns->s) filed under section 8.8 so the closure stays total rather than
> carved out.
>
> **Two process notes (both caught pre-commit by the toolchain, not by
> luck).**  (1) Tags on comment-continuation lines must not use /* */
> syntax - the first pass broke the build (nested comment); those two
> sites (mip.c FBBT comment, pgs_fixed.c saturation-policy comment) carry
> bracket tags instead, which the checker detects on raw lines.  (2) Every
> tag site was applied with a content anchor assertion (file, line,
> expected substring) so a drifted line number fails loudly instead of
> tagging the wrong line.
>
> **Acceptance evidence.**  tools/tolsheet_check.py: OK (80 ids, 127
> tagged sites, 80 documented rows).  Full test.sh rc=0 including the new
> gate; compile clean under -Wall -Wextra; 77-instance MiniZinc bench 0
> semantic diffs (status/objective/solutions/verdict identical; only
> time_psolve_ms/time_ref_ms/compile_time_ms churn).  No ASan sweep this
> phase - no functional code changed (comments + docs + one read-only
> checker tool), which the battery's build+run path confirms.

> **2026-08-18 (10) - branch `arena/cp-engine-correctness`: Phase 6.9,
> functional-graph constraint family + presolve structure-recovery
> detector.  One honesty regression introduced by the work itself and
> caught by the phase-6.7 output gate before commit; four measured
> memory cliffs fixed; one latent nv==0 dispatch gap closed.  Full
> reference: docs/FUNCTIONAL_GRAPH.md.**
>
> **What shipped.** Five new FlatZinc predicates on one shared
> per-table functional-graph digest (`orbit_transient`,
> `orbit_cycle_len`, `orbit_on_cycle`, `orbit_len_capped`, plus
> `array_bool_and` which stock models need and psolve previously
> declined), digest content-dedupe with single-owner borrow semantics,
> and a presolve detector that recovers the MiniZinc
> "H-step walk + prefix-distinct count" lattice exactly (guards: every
> record consumed, no intermediate referenced/output-pinned/
> objective-pinned, `referenced[]` fully recomputed from surviving
> records - airtight by construction) and rewrites it to one
> `orbit_len_capped` record.  On ANY mismatch the model is untouched -
> detection never narrows semantics, it only changes how fast the
> (identical) answer is found.
>
> **Regression introduced and caught pre-commit (honest accounting).**
> The first cut of "branch over referenced vars only" fabricated
> answers: an unconstrained *output* var was never branched, stayed
> unfixed, and printed as `0` - which can lie OUTSIDE its declared
> domain (the exact fabrication class phase 6.7 built its gate for),
> and `-a` enumeration collapsed the cartesian factor of free output
> vars.  `tools/fzn_output_check.py` flagged `WRONG=59/2000` on the
> first battery run.  Fix: branch predicate `searchme = referenced OR
> output-pinned`; searchable-but-unfixed at a leaf is an honest
> decline.  Rewrite-exposed chain intermediates are neither (the
> detector refuses output-pinned intermediates), so the memory win
> (cliff 3 below) is preserved exactly where needed.  Post-fix:
> `WRONG=0/2000`, and the stock 65536-state rewrite still finishes
> (1.76 s / 103 MB).
>
> **Four measured memory cliffs fixed (each found by profiling, in
> order):** (1) realloc-per-entry keep lists in the new propagators -
> quadratic churn, 1.3 GB RSS at n=16384, now two-pass
> count-then-allocate + identical-set no-op guards; (2) initial domains
> materialized for every declared interval incl. dead intermediates -
> 803,479 values on the H=24/n=16384 probe, now search vars only
> (empty-declared-domain UNSAT contract preserved); (3) branching over
> unreferenced vars - ~800k nodes x MB-scale `cp_copy`, linear 600 MB/s
> RSS growth, now search vars only; (4) `cp_total` fixpoint summed
> total domain sizes per pass per node - replaced by a `mutations`
> counter bumped by domain mutators ONLY on actual change.
>
> **Mutation-counter convergence (proof obligation audited).**  The
> fixpoint must terminate when and only when propagation stabilised.
> Audit: every domain write goes through `cp_set_vals` (restrict-
> intersect stage runs FIRST; same-content => no bump, verified),
> `cp_intersect` (bounded path: no-change check before bump; unbounded
> path: bump iff lo/hi moved or materialization changed),
> `cp_remove_val` and `cp_assign` (change-only).  An UNDER-bump would
> exit propagation early and could print an unfiltered leaf - but leaf
> `cp_verify` re-checks every record on the emitted point, so even a
> missed propagation cannot fabricate; an OVER-bump only costs
> fixpoint passes (monotone domain shrinkage bounds the loop).
> Direction of safety confirmed: worst case is wasted work, never a
> wrong answer.
>
> **nv==0 dispatch change (behaviour widened, honesty kept).**
> All-constant satisfy models (`nv==0`) now REACH the CP engine (was
> silently skipped by an `if(nv>0)` gate in src/fzn.c): legality per
> the FlatZinc spec, needed by detector lattice probes
> (`opt.bestx` allocated `(nv?nv:1)`; optimize still requires nv>0).
> This changes UNKNOWN -> SAT/UNSAT answers for all-constant models;
> each new answer is verified by `cp_verify` like any other leaf.
>
> **One more detector-side bug the mutation suite caught:** at H=2 the
> sum row `-len + d2 = -1` has one plus AND one minus coefficient, so
> telling the len var by "the unique +1" picked `d2` and the lattice
> correctly (but silently) refused to fire on that polarity.  The two
> dstsum shapes are now told apart by the rhs sign, which cannot
> coincide (they require contradictory rhs).  Caught by
> `tools/orbit_detect_verify.py` mode-0/9 positives (i9/i60), which
> exist precisely because polarity is randomized per case.
>
> **Logged, not fixed (perf risk, not honesty):** older propagators in
> `src/fz_cp.inc` (the int_lin family ~lines 412-688) still use the
> realloc-per-entry keep-list pattern measured as cliff (1).  They were
> not hot in any profile this session (their candidate lists are small
> in practice); replacing them is a mechanical future item if a profile
> ever indicts them.
>
> **Acceptance evidence.**  Discrimination on the pre-change binary
> (both new gates fail there, per project rule):
> `tools/fgraph_verify.py` pins_bad=5, real_bad=7, WRONG=112 at 60
> fuzzed; post-change pins_bad=0 real_bad=0 WRONG=0 at 200.
> `tools/orbit_detect_verify.py` WRONG=16/40 (positives UNKNOWN or
> timed out; pre binary also lacks `array_bool_and`); post-change
> WRONG=0/100.  `tools/procstates_orbit_verify.py` WRONG=0/120 (rerun
> after the engine predicate change).  Full `test.sh` rc=0 incl.
> `fzn_output_check` WRONG=0/2000, MiniZinc differential OK=33,
> 77-instance bench 0 semantic diffs (2 search-node telemetry changes
> recorded: bool_and_sat 1->2, subcircuit_demo 13->6).  ASan/UBSan/LSan
> sweep: the three new gates at 60/40/60 plus output-check 500, plus
> the stock 65536-state rewrite and the lattice template direct -
> rc=0, zero sanitizer reports.  Auto-detection headline: the stock
> procstates encoding (n=65536, H=64) is recovered and PROVEN optimal
> (44, objective=objectiveBound) in 1.76-1.89 s / 103 MB, vs Gecode
> 600 s UNKNOWN, Chuffed OOM at load, CP-SAT OOM mid-search
> (docs/PROCSTATES.md SS5 baselines, same box).

> **2026-08-16 (9) - branch `arena/cp-engine-correctness`: Phase 6.7 of
> the ambitious roadmap, FlatZinc output-layer round-trip fuzzing.  The
> new checker caught ONE fabricated-SAT class in the CP engine, one
> heap-buffer-overflow in the same engine, and one pre-existing parser
> out-of-bounds stack read (SIGSEGV) - all fixed and locked with
> discriminating gates.  It also produced one false-alarm class of its
> own, documented and corrected below.**
>
> **The tool (new, `tools/fzn_output_check.py`, hard gate in test.sh).**
> Structured-model generator + independent oracle that re-parses every
> emitted byte of `fznsolve` across satisfy / minimize / maximize / `-a`
> and checks: the marker protocol exactly (`----------` per block,
> `==========` rules, UNSAT/UNKNOWN placement), every printed assignment
> against *every* constraint and the declared domains, alias/array-view
> consistency (including constant alias slots), the `%mzn-stat:`
> objective echo against the printed point, `-a` enumeration
> (no duplicates, projection set EQUAL to the brute-forced one for
> satisfy, strictly improving incumbents ending AT the brute optimum for
> optimize), and UNSAT-fabrication detection (a printed UNSATISFIABLE on
> a brute-force-satisfiable model is WRONG).  Float models are checked
> with a *scaled LP feasibility tolerance* (see the false alarm below);
> UNSAT/optimum claims on them are cross-checked against HiGHS (scipy)
> on the benign dyadic data.  Pinned regressions run first and gate the
> corpus.
>
> **Bug 1 (fabrication, CP engine): `cp_set_vals` had REPLACE
> semantics.** Propagator candidate sets were written as the var's new
> domain verbatim, so a propagator could WIDEN a declared domain.  Pin
> of record: `var 5..5: x0; var -4..-3: x1; constraint int_abs(x1,x0);`
> printed **`x1 = -5` -- a value OUTSIDE its declared domain -- as
> SATISFIABLE** (the abs propagator with x0 fixed at 5 wrote
> {+5,-5} over [-4,-3]; leaf printing trusts CP domains); the bare
> sibling `x0 in 0..1, x1 in -4..-3` printed `x0=0, x1=0` SAT on a truly
> UNSAT model.  Fix: restrict semantics inside `cp_set_vals` (sort/dedupe
> candidates, intersect with the current domain; empty intersection
> returns the infeasibility signal).  All callsites audited: propagation
> only ever proposes subdomains, so intersecting cannot change any
> legitimate outcome; the one init callsite intersects against the full
> int64 range and behaves exactly as before.
>
> **Bug 2 (heap-buffer-overflow, exposed by fix 1).** Every one of the
> 12 propagator callsites checked `cp_set_vals(...) < 0` (capacity
> failure) but ignored `==1` (empty domain) - harmless under replace
> semantics because callers pre-guarded empty candidate lists, but
> restrict semantics made "candidates ∩ domain = ∅" a *new* reachable
> way to empty a domain.  A `var {3-valued}` var emptied this way left
> `nvals=0` with propagation continuing, and the next propagator's
> `cp_dmax` read `vals[v][-1]` (ASan: heap-buffer-overflow,
> cp_prop_minmax, on a fuzzed 3-var int model).  Fixed by threading the
> `==1` infeasibility return through all 12 callsites.  Honest
> accounting: the ignored return is the latent flaw; my own fix 1 made
> it reachable - the ASan run on the *post-fix* binary caught it
> pre-commit, exactly what the gate is for.
>
> **Bug 3 (pre-existing, parser): `var {>256 ints}: x` out-of-bounds
> stack read.** fzn.c counted set-domain members without limit but
> stored into a fixed `long vals[256]`, then copied the *counted* number
> of entries - past member 256 the copy read beyond the stack buffer
> into garbage domains (pin: `var {1..70000-as-set}: x` + `x = 54321`
> SIGSEGV'd - verified on the pre-change binary too).  Fixed by
> counting first and allocating exactly.  A related latent fault found
> en route: the CP init path for set literals larger than the CP
> materialization cap wrote an intentionally EMPTY domain and solved on
> it (same underflow); it now *declines* to the exact MIP/SOS1 bridge,
> like every other CP capacity limit.  Both pins added to the tool
> (65536-member boundary materialized in CP; 66000-member declined to
> MIP; both instant).
>
> **False alarm of the tool itself (documented, fixed).**  The first
> float-family revision checked float constraints with exact Fraction
> arithmetic on the printed decimal tokens - unsound: a printed float is
> a %.*g decimal round-trip of a *binary* double, and LP vertices of
> dyadic-input models are rationals with non-dyadic denominators (x =
> -4/3 prints as -1.3333333333333333; its exact Fraction times 3/2 is
> NOT -2, while in IEEE double arithmetic it evaluates to exactly -2.0).
> 4/600 models flagged "WRONG" with residuals ~1e-16 while the same
> expressions evaluated to exactly 0 residual in float64.  The check now
> uses the standard scaled LP feasibility tolerance (1e-6 · scale) -
> ~10 orders above round-trip noise, far below any macroscopic lie on
> these O(1)-scale models - and reports the max scaled residual over the
> run as a drift indicator (observed ≤ 1e-16 across 19.5k models).
>
> **Acceptance evidence.**  Discrimination (pre-change binary, tool
> exit 1): 4/5 pins fail (the two abs fabrications incl. x1=-5 printed;
> both big-set pins crash) + corpus `WRONG=34` at N=2000 (all "outside
> declared domain" int_abs-family escapes).  Post-change: pins pass;
> `N=10000 seed 20260815`, `N=4000 seed 777`, `N=4000 seed 4242` all
> WRONG=0 (float max scaled residual ≤ 1e-16); ASan/UBSan(+LSan) build
> sweep N=1500 WRONG=0 (zero sanitizer events - a nonzero exit is WRONG
> by construction).  Full `test.sh` battery green incl. MiniZinc
> differential and 77-instance bench with 0 semantic diffs; verdicts on
> the int fuzz corpus bit-identical to pre-fix (the fixes changed
> memory safety and answer *honesty*, never a legitimate answer).
>
> **Known limitation surfaced, not fixed (performance, not honesty):**
> a set domain DECLINED to the MIP bridge (members > 65536) whose
> UNSAT-ness needs reasoning (e.g. forcing x to a non-member) grinds in
> branch-and-bound (66000-member SOS1 pin: > 120 s, no wrong output).
> Candidate fix for a later round: presolve set-domain/equality
> intersections before the SOS1 encoding.

> **2026-08-15 (8) - branch `arena/cp-engine-correctness`: Phase 6.5 of
> the ambitious roadmap, the QP numerical audit.  One fabrication-class
> hole found and closed; no others.**
>
> **The hole (found this round).** The QP convexity gate screened only
> the 1x1 and 2x2 principal minors of Q -- so a symmetric matrix n >= 3
> whose negativity lives in a larger minor PASSED as "convex": diag 1
> with off-diagonals -0.9 has every 2x2 minor 0.19 > 0 and an eigenvalue
> of -0.8.  Starting from the stationary origin (c = 0 gadget) the
> active-set returned immediately with the origin as "optimum": on the
> unconstrained gadget the QP is unbounded below (pin pinned in
> tools/qp_psd_verify.py as pin_unconstr), on the box-bounded companion
> the true optimum (-1.2 at (1,1,1)) was reported as 0.  A fabricated
> optimum is the verifier-free direction: nobody re-checks a claimed
> solution.  On the extended family (random Q with all 2x2 minors safely
> positive and a macroscopic negative eigenvalue) the pre-change binary
> fabricated **103 status-0 verdicts out of 122** (25 of 60 box variants
> with objectives wrong vs the true vertex optima, e.g. -4.436 claimed
> vs -14.288 true).
>
> **The fix.** Full symmetrized complete-pivoting elimination scan:
> pivot the largest remaining diagonal each step (a pivot < -tol is a
> negative diagonal of a matrix congruent to Q -- indefinite by
> Sylvester's law; if the largest remaining diagonal is within tol of
> zero the leftover must be entirely ~tol-small, any larger off-diagonal
> is an indefinite principal 2x2).  Completing the scan IS the PSD
> certificate.  tol = 1e-9 (1 + max|Q|), matching the engine's regulari-
> zation tolerances; semidefiniteness of doubles is decidable only to a
> relative frontier (below it the regularized KKT path takes over --
> documented tolerance semantics, not a wrong-verdict hole).  Complete
> pivoting is load-bearing: the first draft's natural-order scan fired
> its "zero pivot, nonzero column" witness on scale-mixed GENUINELY PSD
> blocks like [6e-12 3e-5; 3e-5 1e3] (determinant > 0) and over-blocked
> 14/40 valid models -- the discriminating tool's scale_mix class caught
> it pre-commit, and pivoting the large direction first eliminates the
> coupling at its own scale.  Cost O(n^3/3) once, the same order as one
> active-set KKT factorization.
>
> **Regression lock (calibration rule).** New `tools/qp_psd_verify.py`,
> hard-gated in test.sh: pins (both gadgets now STATUS 4 =
> QP_NON_CONVEX), 120-case indef_gate family (post-change 120/120
> refused, 0 fabrications), near_psd family (singular PSD minus a
> perturbation BELOW the gate tolerance: accepted per tolerance
> semantics; every status-0 answer must pass the qp_diff KKT oracle --
> 6/6 pass), asym family (beyond-tolerance asymmetry 20/20 refused),
> scale_mix family (genuine PSD across 1e-14..1e14 diagonal scalings:
> 39 solves + 1 honest KKT-fail, ZERO over-blocking refusals).  The tool
> is discriminating: on the pre-change binary it counts
> fabricated_status0=103 (obj_mismatch=25) and exits 0 only on the
> pre-detection path.  qp_diff (the pre-existing battery gate) reports
> the IDENTICAL status distribution pre/post on valid families
> (WRONG=0) -- the gate is verdict-neutral on genuinely convex data.
>
> **What was NOT found.** With the closed gate in place, the extended
> families plus qp_diff's pd/singular/zero/diag0 sweep show no wrong
> answers: status-0 answers carry valid certificates, negative statuses
> stay honest (limit-bounded=5 capability gap on singular families is
> unchanged and documented in qp_diff).
>
> **Validation.** ASan/UBSan(+LSan) qpsolve build: zero warnings,
> qp_psd_verify ALL OK, pins refuse cleanly; full test.sh exit 0
> (incl. the new hard gate; oom sweep unchanged at 5930/6391/0 -- the
> gate's n^2 workspace lands per-solve and is injector-covered); mip_diff
> WRONG=0 x5 seeds; MiniZinc differential OK=33 FAIL=0, suite 77/77 with
> 0 semantic diffs vs committed results.
>
> **2026-08-15 (7) - branch `arena/cp-engine-correctness`: Phase 6.3 of
> the ambitious roadmap, the error-protocol redesign (closes "Not done"
> item 2 - the LAST open item; the audit list is now fully dispositioned).**
>
> **The redesign.** The retired protocol kept one process-global triple
> (`jmp_buf psolve_env`, `psolve_active`, `psolve_code`) plus a
> process-global stop callback (`psolve_stop_fn`): a second thread
> installing a handler was *refused* (`psolve_try()` returned 1 -
> demonstrated live on the pre-change err.c with an old-API reproducer),
> and a failure raised on that thread would have longjmp'd into another
> thread's stack.  The replacement keeps the same try/catch shape but with
> caller-owned storage and zero shared state: each thread arms its own
> `PSolveErrFrame` (jmp_buf + chain link), chained through a single
> thread-local top pointer; `psolve_fail` records the code in TLS, pops
> the innermost frame, and jumps to it.  The failure code is read via
> `psolve_err_code()` rather than a frame field on purpose: a TLS read in
> the recovery path is not subject to the C11 7.13.2.1 indeterminacy rule
> for locals modified between setjmp and longjmp (scalars that do cross
> that boundary in tests are volatile; the pattern is documented in
> err.h).  The stop callback is per-thread (`psolve_stop_set`).  No frame
> armed = clean exit(code), as before; popping a non-innermost frame is a
> checked protocol violation (aborts loudly, like double-free).
>
> **Why this is the close and not a shuffle:** the objective claim is
> machine-checked, not argued - `nm -g --defined-only` over all library
> objects showed exactly four exported mutable data symbols pre-change
> (all in err.o, the ones above) and shows **zero** post-change (the only
> remaining statics are `_Thread_local`: the error chain, the arena
> stack, the fx scratch).  test.sh gates this so no mutable global can
> silently return.  With that, *concurrent solves from multiple threads
> on independent problem objects are supported* - README's threading note
> was rewritten to say so, and the claim is tested rather than asserted
> (see below).
>
> **Regression lock (project calibration rule).**  New
> `tools/err_proto_test.c` - 12 single-thread checks: basic unwind with
> code, real malloc failure, realloc NULL-ing *p, calloc overflow guard,
> NESTED frames (the old protocol's refusal, now composing: inner catches
> first failure, still-armed outer catches the second), re-push inside a
> recovery handler, no-frame forked child exiting with the failure code,
> and the checked pop-violation abort.  New `tools/err_mt_test.c` - 8
> threads x 300 rounds of real LP build/solve/destroy with forced
> arena-based allocation failures (deterministic, libc- and
> sanitizer-interception-independent) and per-thread stop-callback
> isolation; clean under ThreadSanitizer (3/3 runs, no reports) - and it
> first caught a real flaw of its own draft (malloc(SIZE_MAX) is
> intercepted as allocation-size-too-big under ASan/TSan, which is why
> the forced failure is arena-based).  **Discrimination:** both tests
> fail to compile against the pre-change `err.h` (`unknown type name
> 'PSolveErrFrame'` - the API is absent), and the old-API reproducer
> shows the second-thread refusal.  All six consumers migrated (lpsolve,
> mipsolve, qpsolve, fznsolve, arena_test, qp_stop_test).
>
> **Validation:** full `test.sh` exit 0 with the oom sweep **unchanged at
> 5930 injection points over 6391 allocations, failures=0** - CLI
> failure behavior is byte-identical at every injection point - the new
> section passing in-battery (proto 12/12, MT plain + TSan, nm gate);
> lp_scale_verify `checked=250 fabricated=0 rescued=60 promoted=48
> healthy=120 ALL OK` and farkas_verify `checked=156 (farkas-fired=23)
> ALL OK` on both the normal and ASan/UBSan(+LSan) builds; mip_diff
> WRONG=0 at seeds 12345/111/222/333/555; MiniZinc differential OK=33
> FAIL=0 and suite 77/77 with 0 semantic diffs vs the committed results
> JSON (timings only); zero compiler warnings with -Wall -Wextra (a draft
> -Wclobbered hit on the MT test's loop scalars was resolved with
> volatile, the documented pattern).
>
> **2026-08-15 (6) - branch `arena/cp-engine-correctness`: Phase 6.1 of
> the ambitious roadmap, exact-or-UNKNOWN promotion for extreme
> scale-mixed *non-integral* LPs (closes "Not done" item 4 - the last
> open wrong-verdict gap).**
>
> **The fabrication.**  On a row `Σ s_j·a·x_j + x_f = b` with the `x_j`
> fixed near 1e25, `a` ~1e-13 (non-integral, so the exact engine
> correctly declines the data), and `x_f ∈ [0,1]`, the double phase-1
> needs `x_f = b − Σ s_j·a·x_j` slightly above 1 - the products feeding
> its artificial sum are ~1e12 and round by ~1.2e-4, four orders of
> magnitude larger than the engine's absolute 1e-6 artificial-sum
> tolerance - and phase-1 *certifies* a Farkas-style ray and reports
> bare INFEASIBLE on a model that is exactly feasible (the planted
> `x_f*` needs no representability: it is a variable value, not data;
> exact truth is decidable in Fractions over the parsed doubles).  The
> MIP and FlatZinc bridges then kept the shaky verdict: pinned repro
> `/tmp/sm_bare_0.fzn` printed `=====UNSATISFIABLE=====` on a feasible
> model pre-change, the dangerous, verifier-free direction.
>
> **The fix, two parts, applied at every place an INFEASIBLE verdict
> escapes the LP core** (LP CLI in main.c, MIP bridge `solve_relaxation`
> in mip.c, FZ pure-LP branch in fzn.c):
>
> 1. **Rescue.**  A bare-INFEASIBLE verdict first gets to prove itself:
>    the phase-1 dual ray is re-verified against the ORIGINAL rows and
>    the variable box by the new exported `solver_farkas_boxcert`
>    (directed FE_DOWNWARD/UPWARD sweeps for the component bounds `z`
>    and the corner-minimum `L = min_box yᵀAx`, `R = yᵀb` upward, margin
>    `tol·(1+|R|)`, poisoned on NaN/inf/≥1e29 sentinels).  Truly
>    infeasible models keep their verdict, now certificate-backed.
> 2. **Promotion.**  If the certificate fails and the instance is
>    exposure-shaky - `solver_row_exposure` computes
>    `E = max_i Σ_j |a_ij|·min(max(|lo_j|,|hi_j|),1e29)` and the gate is
>    `E·DBL_EPSILON ≥ 5e-7` (half the phase-1 tolerance) - the verdict
>    is downgraded instead of printed/pruned: lpsolve prints
>    NUMERICAL_FAILURE, the MIP relaxation returns SOLVE_NUMERICAL (the
>    node is not pruned on an unproven verdict), the FZ branch reports
>    UNKNOWN.  Well-scaled data never reaches the gate (it only fires at
>    `status==INFEASIBLE && farkas_ok`), and the feasible/optimal side
>    is untouched: OPTIMAL verdicts were already protected by the
>    phase-2 solution certificate, so INFEASIBLE was the only
>    unverified direction.  The exact engine's documented refusal of
>    non-integral data is unchanged - this closes the *wrong-verdict*
>    hole, verdict-neutrally for everything else.
>
> **Regression lock (project calibration rule).**  New
> `tools/lp_scale_verify.py`, wired into test.sh as a hard gate
> (60 instances + seed 20260815): three instance classes whose exact
> truth is known by construction (Fractions over the parsed doubles) -
> `feas_shaky` (must never print INFEASIBLE post-change), `inf_shaky`
> (INFEASIBLE only if certificate-backed, else honest NUMERICAL), and
> 120 healthy small LPs held to scipy/HiGHS verdict parity - plus the
> same `feas_shaky` class through the MIP CLI with fixed integer
> columns.  On the pre-change binary the tool reproduces **6 fabricated
> INFEASIBLE verdicts** (5 LP-class, 1 MIP-class: `fs_19/20/51/57/58`,
> `fm_4`) and exits non-zero / exits 0 only on the pre-detection path.
> Post-change:
> `checked=250 fabricated_INFEASIBLE=0 rescued=60 promoted_to_honest=48 healthy_checked=120 ALL OK`
> - every truly-infeasible shaky instance kept its verdict
> (certificate-backed), 48 shaky-feasible instances became honest
> NUMERICAL, zero healthy flips.  FZ pinned pair:
> `/tmp/sm_bare_0.fzn` UNSATISFIABLE→`=====UNKNOWN=====`,
> `/tmp/sm_inf_0.fzn` stays UNSATISFIABLE.
>
> **Sanitizers and full battery.**  ASan/UBSan(+LSan) builds run
> lp_scale_verify and farkas_verify with identical counts and zero
> reports; the pinned instances are clean through all three CLIs
> (lpsolve/mipsolve/fznsolve).  Full `test.sh` exit 0:
> lp_form_verify OK=302 WRONG=0, mip_diff WRONG=0 at seeds
> 12345/111/222/333/555, farkas_verify checked=156 (farkas-fired=23)
> ALL OK, MiniZinc suite 77/77 with 0 semantic diffs vs the committed
> results JSON (no healthy float model flipped to UNKNOWN - the
> frontier `E·eps ≥ 5e-7` stays clear of the ±1e9-box synthetic
> family), oom_test unchanged at 5930 injection points over 6391
> allocations with failures=0 (the gate's scratch buffers live on cold
> paths the injector instances do not reach).
>
> **2026-08-15 (5) - branch `arena/cp-engine-correctness`: independent
> verification of the `main` merge, then Phase 6.2 of the ambitious roadmap:
> the Farkas fast path for infeasibility verdicts in the MIP engine
> (closes "Not done" item 3).**
>
> **Merge verification (the other agent's `add7cdc`/`f098876`/`c34db8c`
> merges of this branch and `arena/phase4-ports` into `main`).**  All three
> merges are textually faithful: tree diffs against the respective branch
> tips are empty except exactly the files each merged side adds, so no
> hand-resolved-conflict drift.  Empirically re-verified on `main` in a
> throwaway worktree: full `./test.sh` exit 0 (incl. oom_test 5882
> injection points, fz_leak_test, the new arena/tlimit/qp-stop entries),
> lp_form_verify OK=302 WRONG=0, mip_diff WRONG=0 (two seeds).  The `48712f5`
> zero-malloc arena port (b7f0561) was read for the previously documented
> rejection reasons: the new design is thread-local with nesting
> save/restore and ownership-checked frees (pointer-in-buffer test before
> treating a pointer as arena-owned), which addresses the
> ownership/alignment/thread-safety grounds the earlier global-arena design
> was rejected on; arena_test verifies zero libc heap calls under
> `-Wl,--wrap=free` and clean undersized-arena failure, and fz-arena results
> are bit-identical to the libc path per its test.  Verdict: main is
> healthy; the harvest dispositions in ROADMAP_AMBITIOUS.md Appendix B were
> updated to reflect that this port is now DONE (by the other agent).
>
> **Farkas fast path (mip.c + solver.c).**  A relaxation the double solver
> declares INFEASIBLE previously always paid for the exact-rational fx
> re-solve (2026-08-15(3) instrumentation: 121 of 122 tsp5 exact re-solves
> were duality-level infeasibility).  Now the solver marks its certified
> Phase-I-infeasible state (`farkas_ok`), `solver_farkas_duals` extracts
> y = B^{-T} c_B as a HINT, and `mip_farkas_certified` independently
> re-verifies the full Farkas separation min_box(y'A)x > y'b with directed
> rounding against the ORIGINAL rows and node box (sign-clamped components,
> corner-minimum over z intervals, engine margin MIP_TOL*(1+|R|)).  The hint
> is never trusted: a garbage or stale ray can only fail the check and fall
> through to the exact path, so the change is verdict-neutral by
> construction.  Rounding-mode regions are allocation-free; btrans's sparse
> path allocates and is called outside them.
>
> Measured effect (verdicts, node counts, and returned solutions identical
> everywhere): tsp_5 0.455s -> 0.218s (**2.1x**), 96 Farkas certificates,
> **exactResolves 0**; open_shop_3x3 / jobshop_3x3 flat (their relaxations
> were already cheap); solutions byte-identical pre/post on tsp_5.
>
> Regression lock (project calibration rule): new `tools/farkas_verify.py`
> (wired into `test.sh` as a hard gate) pins difference-constraint cycles
> the root FBBT cannot single-row certify (asserts INFEASIBLE at nodes==1
> with farkas_certs>=1 and fx_solves==0), margin-discipline families
> (exactly-infeasible sub-tolerance cycles prove INFEASIBLE matching the
> exact-arbitration reference; exactly-FEASIBLE tight neighbours must never
> see a certificate), and a 150-instance random family with planted deep
> infeasibility checked against tolerance-aware brute force.  On the
> pre-change binary all verdict checks pass but the tool FAILS ("no fast
> path"), as required for a performance fix to count as discriminating.
> ASan/UBSan build runs the tool clean (66 checks); mip_diff WRONG=0 at
> seeds 12345/111/222/333/555; full test.sh exit 0 (oom_test now 5930
> injection points over 6391 allocations - the new scratch allocations are
> injector-covered); MiniZinc suite 77/77 PASSED with 0 semantic diffs vs
> the committed results JSON (timings only).

> **2026-08-15 (4) — branch `arena/phase4-ports`: final merge pass.  Every
> remaining remote branch is now dispositioned; `main` is the complete
> tree.**  Remote survey at pass start: `arena/cp-engine-correctness` (16
> commits, superset of `feat/finite-domain-cp-engine`,
> `arena/fzn-set-const-ops`, `feat/flatzinc-complete` — all verified
> contained, and of latest `main` by construction) plus three leftover
> `arena/phase4-interactive-hardening` commits; `arena/audit-hardening` and
> `arena/continue-hardening` unchanged since their documented dispositions
> (rejections stand; sound parts had been ported earlier).  Merged
> `arena/cp-engine-correctness` as `add7cdc` after independent
> re-verification of the merge tree (full `test.sh` exit 0; GLPK sweep
> 119/119, difftest 0 mismatches; ASan/UBSan fuzz clean).  Then ported the
> two un-dispositioned phase4 commits onto the new `main`:
>
> 1. `a42be76` ms-precision time limits + QP cooperative stop — cherry-picked
>    cleanly; stop semantics reviewed honest (QP_STOPPED with feasible
>    incumbent, or NULL x when stopped during Phase-I — never a fabricated
>    OPTIMAL).  qp_stop_test + tlimit_test + `-t` on all drivers pass.
> 2. `48712f5` thread-local zero-malloc arena — merged only after re-doing
>    its allocation-routing invariant on the diverged tree: the CP/engine
>    merge had reintroduced ~290 raw libc call sites (fz_cp.inc's 65 raw
>    `free()`s of psolve_malloc'd memory were the headline class), which under
>    an active arena is a dangling-`free` heap corruption.  Port re-applies
>    the routing rule to fzn.c/fz_cp.inc/mip.c/solver.c/parser.c; the
>    grep-enforced post-invariant is *no raw libc allocation call in src/
>    outside err.c and main.c*.  New `tools/arena_fzn_test.c` extends the
>    phase4 arena test (which predates the CP engine) to the FlatZinc/CP/MIP
>    surface: 14 reference models + a direct mip_solve, asserting
>    --wrap-counted ZERO libc heap calls arena-active, bit-identical
>    verdicts/objectives/solutions arena-vs-libc, and the
>    read→solve→free-in-scope + reset/reuse embedding patterns — 91/91
>    wrapped, ASan/UBSan/LSan clean.  (The one initial FAIL it ever printed
>    was the test's own knapsack expectation, arithmetic-checked against a
>    correct solver answer of 34.)  Full branch `test.sh` green incl. the
>    5882-probe OOM-injection battery.
>
> Remaining environment-gated item (unchanged): the MiniZinc differential
> suite needs a `minizinc` binary, not packaged for Debian — the 77/77
> real-MiniZinc benchmark was validated on the CP branch's own runs.
> Designs for the process-global `setjmp` OOM protocol in multi-threaded
> hosts remain open (the new arena is thread-local and does not worsen it).

> **2026-08-15 (3) — branch `arena/cp-engine-correctness`: fabricated-UNSAT
> fix in the root FBBT (wrong-answer class), a directed-rounding interval
> certificate replacing the tolerance prune, and per-node dead-box
> certificates in the B&B relaxation driver.**  Remote survey: all branches
> unchanged since the external-audit round; nothing to port.
>
> **Finding (wrong-answer, UNSAT direction).**  `fbbt_tighten`'s root
> infeasibility prune compared a *round-to-nearest* activity estimate
> against `rhs + 1e-6·(1+|rhs|)`.  RN accumulation is not a bound: with
> catastrophic cancellation the products round by up to ~half a ulp of the
> *partials* (~1e−4-scale at 1e12-magnitude partials), which dwarfs the
> tolerance, and an overestimated "minimum" then prunes a FEASIBLE model.
> Reachable only on data with cancellation at ≥1e10-magnitude partials,
> which is why the existing `fbbt_verify` family (tiny coefficients × large
> bounds, but small *products*) never crossed the margin.  The prune's
> caller reports proven INFEASIBLE without running any LP and without the
> exact-rational cross-check that guards infeasibility verdicts inside the
> tree — a fabricated final verdict.  Same mechanism, second entrance: the
> *tightening* step's "one nextafter outward bias" provably under-covers the
> rest-sum RN error once bounds exceed ~1e15 (a single ulp of
> underestimate exceeds the absolute 1e-9 slack), collapsing a feasible
> box into the same fabricated return.  Both entrances closed.
>
> **Regression of record** (integral counterexample, found by an exact
> hunt: Python int/Fraction reference vs IEEE-RN simulation of the C
> accumulation order): maximize x0 with x0≡36, x1≡7 and
> `8.658741690308737e17·x0 − 4.4530671550159217e18·x1 ≤ 2561`.  Exact
> activity over the stored doubles is exactly 2560 ≤ 2561 — feasible,
> optimum 36 — while the RN activity is 4096 > 2561 + margin.
> Pre-fix binary: `status: INFEASIBLE`.  Post-fix: `OPTIMAL 36`
> (the certificate stays silent; the exact cross-check then arbitrates the
> numerically undecidable double LP).  Pinned plus a 40-instance randomized
> cancellation family (both relations, exact Fraction references on the
> stored doubles, filtered to fire the old margin) in `fbbt_verify.py`:
> **pre-fix binary fails 29/41 of the battery; post-fix passes all**.
>
> **Fix architecture — `mip_box_conflict` (src/mip.c).**  One O(nnz)
> directed-rounding sweep computes a proven lower bound of every row's
> minimum box activity (FE_DOWNWARD) and a proven upper bound of its
> maximum (FE_UPWARD) over the *exact double data*.  No accumulation
> tolerance is needed or sound: unbounded sides poison the accumulator with
> NaN (row then cannot certify in that direction), overflow only widens
> toward the non-certifying side, and `isfinite` filters the survivors.
> Fire conditions carry the engine's tolerance semantics: prune only when
> the rigorous bound crosses `rhs ± MIP_TOL·(1+|rhs|)`.
>
> **Mid-round catch, recorded per the calibration doctrine.**  The first
> version compared exact-directed bounds to the rhs *with no margin* and
> passed the pinned/family batteries — but `mip_diff 400 12345` flipped
> from WRONG=0 to **WRONG=5**, all "INFEASIBLE != OPTIMAL" on
> tolerance-feasible borderline equality rows (it=236: `2.293·(−3) = −6.879`
> differs from itself by 8.9e−16 on the stored doubles; exact-strict
> pruning is *wrong* in a solver whose every layer — LP `TOL_FEAS`, mip
> `check_solution`, the brute-force references — declares tolerance-based
> semantics).  A prune must never reject anything the engine would accept;
> margins were restored (they are constant ulp-free additions, not
> accumulated terms, so they cannot reintroduce the fabrication).  Post-fix
> status mix at seed 12345 (INFEASIBLE=149/OPTIMAL=251) equals the pre-fix
> binary's exactly; WRONG=0 there and at seeds 111/222/333/555.
>
> **Tightening rewrite.**  The four bound-candidate cases ('<'/'=' vs '>',
> a>0 vs a<0) are now computed by accumulation in the direction of the
> required extremum followed by a subtraction and division each rounded in
> the provably-safe direction (derivation table in the function comment).
> Extremum bookkeeping (rest-min for '<'/'=', rest-max for '>') is
> unchanged from the audit-hardened port; only the rounding discipline is
> new.  Latch: the lattice snaps and comparison hysteresis can now only
> widen the result.
>
> **Optimization half (this round's recorded target).**  `solve_relaxation`
> now runs the same certificate on the intersected node box before starting
> any solve: a provably dead node prunes in O(nnz) without the double LP
> and, crucially, without the exact-rational re-solve that infeasibility
> verdicts previously paid for.  It does not mutate the box, touch the
> warm-start state, or change node accounting, so the tree evolves exactly
> as before — verified empirically on 11 models (tsp5, assignment,
> open_shop_3x3, jobshop/flowshop, knapsacks, bin packings): identical
> verdicts/objectives, lp/fx counters differing by exactly the certified
> prunes.  Honest performance note: on this combinatorial suite few nodes
> are *single-row bound-conflict* certifiable (tsp5: 1 of 299 LPs skipped,
> wall unchanged within noise; knap_lin: 1 of 11) — the instrumented
> histogram shows the remaining ~122 exact re-solves on tsp5 are
> duality-level infeasibility, which an interval row scan cannot see.  The
> cheap sound substeps that remain for that class: extracting the phase-1
> dual ray and interval-checking a Farkas certificate (recorded next
> target, unchanged).
>
> **Verification:** full `test.sh` exit 0 with the extended `fbbt_verify`
> (241 checks incl. 40 fired cancellation instances vs scipy-free exact
> references) and `mip_diff` 400×4 seeds WRONG=0; ASan/UBSan/LSan mipsolve
> build runs the pinned case, the family and mip_diff clean; MiniZinc
> benchmark re-run on the final binary: **77/77 with zero semantic diffs**
> (status/objective/verdict/solutions vs committed results; timings only),
> docs regenerated.
>

> **2026-08-15 (2) — branch `arena/cp-engine-correctness`: external-audit
> round.**  An independent audit of `main`@248eb2f (report supplied by the
> repository owner) reached a "well-hardened" verdict with one Medium and a
> handful of minor findings.  All six actionable findings were reproduced on
> this branch first, fixed, and regression-locked with differential tests
> proven to fail on the pre-fix binaries; the round also surfaced a parser
> hole the audit did **not** find (silently swallowed identifier domains),
> caught because the new leak tool's own calibration was wrong (LSan hides
> block-buffered stdout — see below).
>
> | # | Severity | Finding | Disposition |
> |---|----------|---------|-------------|
> | F-1 | Medium | False `UNBOUNDED` on contradictory bounds (`l > u`) | fixed + gated |
> | F-2 | Low | Duplicate `(row,col)` triplets not merged | fixed (parser + API) |
> | F-3 | Low | `fz_read` leaks the partial model on parse errors | fixed (8 paths) + deeper container leak |
> | F-4 | nit | `ftell() < 0` unchecked in `fz_read` | fixed |
> | F-5 | Low | single-active-solve `setjmp` protocol undocumented | documented (redesign still open — "Not done" #2) |
> | F-6 | nit | `mipsolve --print` only accepted trailing | fixed |
> | F-7 | info | AGPL-3.0 not surfaced in README | documented |
>
> **F-1 — false `UNBOUNDED`, mechanism.**  The parser and the API both accept
> a contradictory box (`l[j] > u[j]`); only generators never emit one.  The
> entering rules skip the stuck variable, so a phase-2 walk could ride a ray
> on a *free* one and report `UNBOUNDED` — a status that asserts a feasible
> ray, i.e. implies feasibility of an empty problem: a wrong answer
> (`maximize 2x₁`, `x₁ ∈ [5,2]`, one free variable reported `UNBOUNDED`).
> Fix, two layers: (a) an up-front empty-box scan in `solver_solve_impl` and,
> for parity, in `solver_warm_solve` (a warm solve may follow
> `solver_set_bounds`, which accepts any pair) reports **certified
> `INFEASIBLE`** — no constraint examination is needed when a variable admits
> no assignment; (b) belt-and-braces, the `r==2` UNBOUNDED return is now
> gated by the same `solver_feasible()` primal certificate that gates
> OPTIMAL, degrading to `SOLVE_NUMERICAL` instead of a false status.
>
> **F-2 — duplicate triplets, mechanism.**  The counting sort in `lp_read`
> passed duplicate `(row,col)` entries through, after which the core treated
> the duplicated row inconsistently (matrix-vector products summed both
> entries while column reads kept one), degrading `x+x≤4, max x` (*OPTIMAL
> 2*) to `NUMERICAL_FAILURE`.  Fix: canonicalize by summing duplicates (GLPK
> semantics) in both ingestion paths — `lp_read` during CSC materialization
> (epoch-marker accumulator, `isfinite` overflow reject on merged sums) and
> `solver_create_internal` for API callers.  Cancellations to exactly 0.0 are
> kept as explicit zeros: inert for products and reads, and dropping them
> would churn allocation bookkeeping for no correctness benefit.
>
> **F-3 — partial-model leaks, plus a deeper one the audit did not name.**
> All 8 `fz_read` error returns now call `fz_model_free(m)` (`fz_new_decl`
> counts its slot immediately, so partial frees are safe).  Additionally
> `expr_free2`'s `if (e->n > 0)` guard skipped `free(e->els)` for an
> empty-but-allocated container, leaking a cap-8 (256 B) elements array on
> any `[`-primary that errored before its first element; the guard is gone
> and `parse_lin`/`parse_array` now share `expr_free2`.  **Why a C harness,
> not the fuzzer:** LSan's root scan treats pointers left in dead stack
> frames as reachable, so a leak inside a caller's just-returned frame is
> *invisible* through the CLI; `tools/fz_leak_test.c` reads each malformed
> input in a `noinline` helper and then clobbers 32 KB of stack, after which
> the leak is genuinely unreachable and LSan reports it.  Proven
> discriminating: pre-fix build exits 1 with
> `5878 bytes leaked in 24 allocations`; fixed build exits 0 clean.
> Wired into `test.sh` as a hard gate under ASan with `detect_leaks=1`.
>
> **Self-found this round (the audit did not report it): identifier domains
> silently swallowed in `fz_read`.**  The new tool's case
> `var foo..bar: q;` was expected-reject — and *parsed*.  An identifier in
> type position was consumed and ignored, degrading the declaration to an
> unbounded variable over the ±1e9 bridge sentinel box (`FZ_BIG_BOUND`);
> interior optima over that box do not even trip the sentinel-hit UNKNOWN
> guard, so this was a silent wrong-answer path (e.g. `var opt int: x` or the
> legal-looking `var D: x` with `D` a declared set parameter — unsupported
> here).  Why it went unnoticed earlier: LSan at exit terminates via
> `_exit()` *before* libc flushes block-buffered stdout when piped, so the
> pre-fix log contained the leak report but no accept/reject lines, and the
> tool's calibration had never actually been validated against either build.
> Fix: an unknown identifier in type position is now a hard parse error.
> MiniZinc's compiler grounds every domain to a literal range/set (verified
> against 2.9.4 output for `var D: x` and `var 1..n: y` models), so no
> legitimate input is lost; rejection replaces a fabricated domain.
> Discrimination: the pre-fix build (leak check disabled) prints
> `case 2 should be rejected but parsed` / `case 3 should be rejected but
> parsed`; the fixed build prints `accept/reject OK`.  (Process lesson,
> reinforced for the second time this week: a regression test's calibration
> claim must be re-verified empirically from the actual logs, not assumed.)
>
> **F-4/F-6 — mechanics.**  `fz_read` now rejects `ftell() < 0` before the
> allocation size depends on it; `mipsolve`'s option loop accepts `--print`
> in any position instead of trailing-only.
>
> **F-5/F-7 — documentation.**  README's library section now states the
> single-active-solve rule (process-global `psolve_env` in `src/err.c`;
> concurrent entry points need an external mutex or process isolation; the
> setjmp-protocol redesign stays on the "Not done" list) and surfaces the
> AGPL-3.0 license obligation next to the embedding example.
>
> Verification: `tools/lp_form_verify.py` (new — contra-bounds class must
> answer exactly `INFEASIBLE`, duplicate-triplet LPs checked against a
> scipy/HiGHS reference, wired into `test.sh`) reads **OK=302 WRONG=0** on
> the fixed binary vs **WRONG=175/302** on the pre-fix one; both repro LPs
> (`contra.lp` → `INFEASIBLE`, `dup.lp` → `OPTIMAL 2`) and the ASan/UBSan/
> LSan-built `lpsolve`/`fznsolve` pass the same batteries.  Full `test.sh`
> exit 0 with the two new sections (`lp_form_verify`, `fz_leak_test`);
> `cp_opt_verify` 500/0 and the fzn semantics matrix under the ASan binary;
> MiniZinc benchmark suite re-run on the final binary: **77/77 PASS with
> zero semantic diffs** (status/objective/verdict/solutions) against the
> previously committed results — timings only.  Next optimization targets
> (unchanged, recorded): sound Farkas-certificate checks to replace the
> per-node exact-rational infeasibility re-solves in the MIP bridge, and CP
> support for the scheduling globals still declined to MIP.

> **2026-08-15 — branch `arena/cp-engine-correctness`: optimization strength
> round — CP incumbent-bound propagation, warm-started B&B relaxations, and a
> fabricated-optimality fix in the public `solver_warm_solve` API.**
> Remote survey (via API; git smart-HTTP is blocked in this sandbox this
> round): all 15 remote branches unchanged since 2026-08-13, nothing to port.
>
> **Feature 1 — incumbent-bound propagation in the CP B&B
> (`cp_opt_bb_tighten`, `src/fz_cp.inc`).**  Once the CP optimizer holds an
> incumbent, only strictly-better assignments matter, so the objective row
> `Σ cᵢvᵢ ≤ B−1` (min) / `≥ B+1` (max) is propagated onto every objective
> variable at every node: each term is bounded against the 128-bit-exact
> side-sum of its classmates (`Aⱼ`), with exact floor/ceil division
> (`cp_fdiv`/`cp_cdiv`) on possibly-negative coefficients and per-term
> wild-magnitude (±2⁶⁰) declines — a term with an unbounded classmate is
> simply never tightened.  Soundness, including when the objective repeats
> a variable: per-term side sums only ever *relax* the classmates' joint
> extremum, so an excluded value can never belong to a strictly-better
> assignment; the incumbent is already recorded.  Cuts tighten domains and
> are then re-propagated through the constraint records.  Measured: the
> 5-permutation weighted-all_different probe goes 135 → 52 explored nodes,
> a 7-permutation one 2161 → 577 (−73 %), same proven optima; the whole
> `cp_opt_verify` battery (300 models × both solver paths × 5 seeds, then
> more) agrees with brute force, ASan/LSan clean.  (One false alarm along
> the way, worth recording for the process file: an apparent "optimum
> 100 ≠ brute-force 110" discrepancy was a *bad hand repro* — the model
> declared `obj ∈ 0..100`, my brute force ignored the declared domain.
> All brute-force references must apply every declared domain, objective
> variable included.)
>
> **Feature 2 — warm-started relaxations across the MIP branch-and-bound
> (`MipWarm`, `src/mip.c`).**  Successive node relaxations differ only in
> variable bounds — precisely the supported incremental case — so one
> persistent `Solver` now serves the whole tree: the first node solves
> cold (phase 1 included), later nodes go through `solver_set_bounds` +
> `solver_warm_solve`, with the box intersection hoisted into the caller so
> the exact-rational cross-check on numerical/infeasible verdicts keeps its
> box.  Total simplex iterations over a full tree, instrumented: tsp_5
> 20 695 → **63** (328×), a 26-item knapsack 1 140 → **18**.  *Wall time on
> the combinatorial-MIP benchmark is unchanged*: its per-node histogram
> (200 optimal + 153 infeasible-declared over 255 nodes) shows the cost is
> the exact-rational feasibility cross-check the bridge runs on every
> "double-infeasible" verdict, not iteration count — recorded as the
> measured next optimization target (a sound Farkas-certificate check
> would replace those full exact solves; not done this round).
> **Measured-and-reverted**: running FBBT on every node box (instead of
> root-only) regressed tsp_5 255 → 303 nodes / +25 % wall — tighter boxes
> shift LP vertices and thereby the most-fractional branching picks — so
> `mip.c` keeps root-only FBBT with a comment carrying the numbers.
>
> **Bug fix (public API, honest-status direction) — `solver_warm_solve`
> fabricated optimality on exhausted iteration/stop budgets.**  The warm
> path treated only `r==2 || !primal-feasible` as failure and fell through
> to the OPTIMAL return for **every other** `solve_phase` outcome — so a
> warm solve halted by the iteration limit (`r==-1`), a cooperative stop
> (`SOLVE_STOPPED`), or a factorization failure (`SOLVE_NUMERICAL`) with a
> still-feasible basis reported *optimal*.  With warm starts now driving
> B&B nodes, that would have converted a per-node LP limit into a bogus
> bound capable of pruning valid subtrees.  Fixed to mirror
> `solver_solve_impl`'s status discipline exactly (limit → 3, stop → 4,
> numerical → 5, plus the missing final-basis-validity check before any
> optimal verdict).  Regression: `tools/incr_test.c` case "warm-limit"
> (cap exhausted before a definitively-suboptimal warm re-entry) prints
> `status=0` on the pre-fix source and `status=3` after — discrimination
> verified against both builds.
>
> Verification: full `test.sh` exit 0 on the final tree (200/200 random
> incremental warm-vs-fresh, mip_diff 3×300 + default 400 cases 0 wrong,
> all fz differentials, fuzz, 6 000+-point OOM injection), ASan+UBSan+LSan
> sweep over tsp_5/knapsack/opt models clean, MiniZinc suite re-run on the
> final binary: **77/77**, benchmark docs regenerated 2026-08-15.

> **2026-08-14 — branch `arena/cp-engine-correctness`: finite-domain
> branch-and-bound *optimization* in the CP engine, plus a real wrong-answer
> bug found by the new cross-path differential, a leak found by the
> leak-enabled fuzzer, and the first full-MiniZinc validation (77/77).**
> Remote survey: `main`/`mzfnsh` moved only to `248eb2f` (docs-only
> literature survey) and are already ancestors here; `fzn-table-constraint`
> likewise already merged.  Nothing to port this round.
>
> **Feature — CP optimization (`fz_cp_try` + `cp_solve_rec`).** `solve
> minimize/maximize` on pure finite-domain models now engages the CP engine
> as a sound branch-and-bound instead of always falling back to the MIP
> bridge: an exact activity bound (128-bit, `cp_opt_activity`, computed from
> live interval/set domains, declined to "no prune" whenever a coefficient
> or domain magnitude exceeds the provable threshold) prunes subtrees that
> cannot beat the incumbent; leaves *verify* assignments against all
> constraint records before scoring, then snapshot the best verified
> assignment.  Engagement gates keep the honest surface small: the whole
> objective (constant and every coefficient) must be exactly integral
> (`rint` round-trip, magnitude < CP_BIG), `-a` with optimize declines to
> the MIP incumbent catalog, the node cap and any uncertifiable state
> fall back to the MIP bridge rather than printing an untruth, and
> objective variables are forced `referenced` so no leaf is accepted with
> the objective undetermined.  One deliberate performance gate: an
> optimization model whose records are *all* plain linear rows declines to
> MIP — measured on `tsp_5` (`-G linear` flattening, 63 linear rows): CP
> 1166 ms vs MIP 8 ms, same proven optimum 34; bounds-fixpoint CP gains
> nothing the LP relaxation does not already provide there, while
> combinatorial models (alldifferent/element/reif/minmax/...) are where CP
> wins.  Benchmark suite regenerated with the final binary: 77/77 pass.
>
> **Bug 1 (wrong answers, fabricated optimality) — `int_min` disjunction
> row lost its `m` term in the `e412777` merge resolution.**  While
> hand-retyping the conflicted min/max block, the min branch's second row
> `m - b <= 0` became `-b <= 0`.  Bridge semantics silently changed to
> *forcing the min's second operand non-negative*: wrong optima whenever
> that operand may be negative, and fabricated UNSAT for `int_min(x,-5,m)`.
> Repro (prints `obj = -1` + `==========` — a fabricated proven optimality
> — on the merged binary; truth is -2):
> `var -1..4: x0; var -4..1: x1; var -60..60: obj;
> constraint int_min(-1, x0, x1);
> constraint int_lin_eq([1,1,-1],[x0,x1,obj],0); solve minimize obj;`
> The satisfy tests never saw it: `int_min` is CP-motivating, so satisfy
> models never touch the broken bridge row — only optimization (which fell
> to MIP before the CP gate existed) exposed it.  Diagnosed by diffing the
> merge output against *both* parents (byte-identical elsewhere; single
> dropped `lin_term`), confirmed by a stored-row dump (mm term missing in
> the bridge, correct in the handler args).  Meta-lesson now enforced in
> process: conflict resolutions are diffed region-by-region against both
> parents before committing.  Regression lock: four negative-domain cases
> in `test_cp_minmax_constant_operands` — all FAIL on the `18eb475` binary
> (fabricated-UNSAT signature reproduced), all PASS here.  The earlier
> positive-domain tests structurally could not catch this (`b >= 0` is
> implied by all-positive domains); the generator now mixes signs and
> operand positions.
>
> **Bug 2 (leak) — `fz_cp_try` opt-buffer cleanup on the CP-decline path.**
> The optimization eligibility block allocates `optcoef`/`bestx` *before*
> the float-variable scan that declines the model; optimize-models carrying
> a float variable then leaked 24 bytes per solve.  Found by LeakSanitizer
> once the fuzzer was made able to see it: `tools/fuzz_fzn.py` previously
> could not — its well-formed corpus contained no float variables (the
> leaking path was unreachable), it never set `detect_leaks=1`, and its
> stderr filter did not match LeakSanitizer output.  All three blind spots
> fixed (float vars in the corpus, `ASAN_OPTIONS=detect_leaks=1` +
> `UBSAN_OPTIONS=halt_on_error=1` pinned for fuzz runs, `-fno-sanitize-
> recover=all` in the fuzz build, LSan/SUMMARY stderr checks, plus a
> deterministic float-pinned optimize probe run on every fuzz batch), and
> the probe empirically aborts on a binary rebuilt with the free removed.
>
> **Stat honesty — CP node counter.**  `%%mzn-stat: nodes` from the CP
> path reported *path depth*, not explored nodes: `cp_solve_rec` recurses
> on per-branch `cp_copy` instances whose constructors duplicate the
> parent's counter, so every subtree's increments died with its copy.
> The counter is now a caller-owned `long*` threaded through the recursion
> (e.g. the small alldifferent-maximize probe reports 482 real nodes
> instead of ~5).  Same `==========`, same optima — only the statistic is
> now true.
>
> **Verification summary.**  `tools/cp_opt_verify.py` (new, wired into
> `test.sh`): 250 random FD optimization models/seed × *both* paths per
> model (dispatcher, which routes combinatorial ones to CP, plus a
> float-pinned variant that forces the MIP bridge) × seeds {1, 42, 7,
> 2024, 99, 314, 555, 77} — 0 wrong vs brute force: optima exact, statuses
> honest (no UNSAT on feasible, no infeasible unproven), witnesses
> in-domain and constraint-valid, `==========` only with proven bounds.
> It prints **22 wrong / 500** against the `18eb475` binary (all the
> int_min class) — discrimination proven.  Full `test.sh` exit 0 on the
> final tree (incl. 6113-point OOM injection, 120-input fuzz with the new
> coverage, GLPK differential n/a — glpsol unavailable on this host).
> ASan+UBSan+LSan sweep over `cp_opt_verify` (300 model-runs), the
> semantics suite, the fuzzer, and the compiled benchmark models (tsp_5
> obj 34, open_shop_3x3 obj 6, golomb_ruler_4 obj 6 — all reference-
> matching): clean.  **First real-MiniZinc run of the whole pipeline:**
> MiniZinc 2.9.4 IDE bundle (gecode/chuffed/cp-sat referees) registered
> with `share/minizinc/solvers/psolve.msc`; `tools/mzn_bench.py` gained a
> `coin-bc`→`cp-sat` linear-referee fallback (coin-bc no longer ships in
> the bundle) and an availability probe; the 77-instance suite passed
> 77/77 and `tools/mzn_diff.py` reports OK=33 FAIL=0.  `docs/
> MINIZINC_BENCHMARK.md` and `tools/benchmark_results.json` regenerated
> from the final binary as part of `test.sh`.

> **2026-08-13 — branch `arena/cp-engine-correctness`: merged
> `feat/finite-domain-cp-engine` tip (`49e3680`) and audited the new CP
> reif/clause/minmax/variable-element/all-solutions work; also fixed a
> latent unit-lattice-step family in `fzn.c` shared by both branches.**
> Remote survey: the CP branch gained `8c1d245` (MIP-side int_min/max
> constant operands — *not ported*: this branch's `lin_materialize` handles
> the strict superset, incl. arbitrary affine operands; conflict resolved
> keeping it), `0e38578` (CP `-a` enumeration + CP reif/bool_clause/minmax/
> element), `49e3680` (CP variable-array element); phase4 gained `3573e3a`
> (root FBBT, reviewed separately).  The merged tree was then put through
> the standard reproduce-then-fix audit, which found four real defect
> classes — every one reproduced on the pre-fix binary before patching:
>
> 1. **`cp_prop_minmax` heap underflow + wrong UNSAT** (from `0e38578`):
>    the const-operand case called `cp_dmin(cp, -1)` on constant terms
>    (`v==-1` convention) and intersected `m` with per-operand bounds that
>    are unsound for min *and* max.  Repro: `var 1..10: x,m;
>    constraint int_min(5,x,m); solve satisfy` printed
>    `=====UNSATISFIABLE=====` (plain build) and dies with ASan
>    heap-buffer-overflow `cp_dmin fz_cp.inc:43 <- cp_prop_minmax`.
>    Fixed by a full rewrite: const-safe operand bounds, exact m-fixed
>    rules (each operand bounded by m, the unique candidate forced to m),
>    sound whole-domain bounds when m is free.
> 2. **Unchecked `llround` in CP parse** (`cp_make_lin` — pre-existing —
>    plus the two new reif parse sites): fractional coefficients were
>    silently rounded, changing semantics *and* feeding the verifier the
>    rounded constraint so wrong answers self-certified.  Repro:
>    `int_lin_eq([0.6],[x],1)` on `x in 0..2` printed `x = 1` (truth:
>    UNSAT); `int_le_reif(x,0.6,r)` with `x=1` printed `r = true` (truth:
>    false).  Fixed with a `cp_lin_is_exact` gate; inexact constraints
>    decline to the MIP bridge, which now handles them exactly (item 4).
> 3. **Inverted gecode-element offset guard** (pre-existing): accepted
>    variable offsets by *dropping the variable part* (crafted
>    satisfiable models printed UNSATISFIABLE) while declining plain
>    constant offsets — the only well-formed case.  Guard corrected;
>    constant-offset gecode elements now engage CP (e.g.
>    `gecode_int_element(i,1,[5,6,9],v)`), variable offsets honestly
>    decline to UNKNOWN.
> 4. **Unit-lattice-step assumption in the integer relation encoders**
>    (pre-existing on *both* branches): `add_int_reif`/`add_int_imp`/
>    `add_int_ne`/`add_int_relation_constant` and the plain
>    `int_lt/int_gt/int_lin_lt/int_lin_gt` handlers encoded "d>0" as
>    "d>=1" and "d<0" as "d<=-1".  Exact on the integer lattice, but
>    wrong by up to one lattice unit when `d` lives on `g*Z+f` with
>    fractional part `f>0` (reachable from any fractional constant in an
>    int predicate).  Repro: `int_lin_lt([1],[x],0.6)` on `x in 0..2`
>    printed UNSATISFIABLE (truth: x=0); `int_le_reif(x,0.6,r)` maximize
>    x returned objective 0 (truth: 1).  Fixed with a shared
>    `int_lattice_steps()` computing the exact neighbors
>    `P = f>0?f:1`, `N = f>0?f-1:-1` (sound for every gcd `g`, since
>    `f in (0,1)` is the fundamental representative); eq/ne on fractional
>    lattices collapse to constants (pin r / vacuous rows); irregular
>    (non-integral-coefficient) lattices now decline to UNKNOWN instead
>    of rounding.  Every emitted row for well-formed integer-lattice
>    models is byte-identical to before, so this cannot regress real
>    MiniZinc output.
>
> Empirically *verified* claims from the upstream commits (no changes
> needed): CP `-a` enumeration is distinct-and-complete on bounded
> integer models (9/9 reif grid + 7/7 clause tuples, completion marker
> honored), and the variable-entry element propagator (`49e3680`)
> matched brute force on 120 randomized instances including UNSAT
> verdicts.  Regression lock: five new groups in
> `tools/fzn_semantics_test.py` (`test_cp_minmax_constant_operands`,
> `test_fractional_lattice_relations`, `test_cp_gecode_offset`,
> `test_cp_variable_element_differential`, `test_cp_allsolutions_reif_clause`);
> groups 1–3 FAIL on `origin/feat/finite-domain-cp-engine` and on the
> pre-merge HEAD (discrimination proven), all PASS here.  Full `test.sh`
> exit 0 (6316-point OOM injection clean), ASan/UBSan clean on the
> adversarial corpus, `fuzz_fzn` 300 inputs clean, and a 300-instance
> generative fuzz aimed at the new propagators clean.

> **2026-08-11 (3) — branch `arena/cp-engine-correctness`: merged the CP
> engine with the correctness layer and audited the union.**  Two new remote
> branches appeared: `feat/finite-domain-cp-engine` (Régin all_different CP
> engine, `fz_cp.inc`, N-Queens/Sudoku/divisor products; branched off `main`
> — a *parallel* implementation, not a descendant of `flatzinc-complete`)
> and `arena/phase4-interactive-hardening` (ms time limits + QP stop; a
> thread-local arena redesigned to answer the earlier rejection; left
> unmerged pending its own review).  The CP branch alone still contained
> every constant/affine-argument lie class (`bool_clause` UNSAT lies,
> `subcircuit` infeasibility, `set_in_reif` r=false lies, min/max/abs
> UNKNOWNs), so this branch merges `arena/fzn-set-const-ops` into it
> (conflicts were cosmetic: identical most-fractional MIP branching on both
> sides, description strings, regenerated docs/data).  Auditing the merged
> `fz_cp.inc` found real defects the CP author missed: heap-buffer-overflow
> in `cp_prop_setin` on a constant LHS (crash on `set_in(-1, -1..3)`),
> same -1-index crash in `cp_prop_elem` for an out-of-range constant index,
> `INT64_MAX+1` signed-overflow sentinel in `cp_prop_pow`, negative-shift UB
> in dead code (function removed), `strtoll` bare-range parser allocating
> `(size_t)(hi-lo+1)` on `5..1` (gigabyte OOM), reversed ranges silently
> swapped (semantics change), and uncapped set materialization (DoS) — all
> fixed; the engine now builds with zero `-Wall -Wextra` warnings.
> `divmod_verify` caught 26/300 wrong answers on the naive merge (all the
> set-in-constant crash class); after the fixes: 0 wrong across seeds,
> full `test.sh` exit 0, ASan/UBSan + fzn fuzz clean, N-Queens 8 still 3 ms.

> **2026-08-11 (2) — constant/affine-argument audit, phase 2:** a 70-probe
> sweep of every FlatZinc handler with par (constant) and affine arguments
> (the branch-review-driven hunt after the `set_in_reif` find) exposed four
> further wrong-answer classes, all fixed and regression-locked:
> `bool_clause`/`bool_clause_reif` silently skipped par literals
> (`bool_clause([true],[])` claimed UNSAT; `bool_clause([], [false])` too),
> inverted negated-literal signs in the reified big-M row, and divided by
> zero (NaN row) on all-par clauses; `subcircuit` dropped par successor
> values (constant-overwrite) AND its MTZ subtour elimination had no anchor
> node, so every real circuit — even a 2-cycle with variables — was
> infeasible (rewritten with a single-anchor MTZ: n=3 now enumerates the
> exact 6 successor mappings); `array_bool_and`/`array_bool_or` skipped par
> literals leaving r unconstrained; `int_min`/`int_max`/`int_abs` rejected
> constants (UNKNOWN) and silently dropped affine constant terms (e.g.
> `int_min(x+1, 5, m)`).  New `lin_materialize()` helper pins constants and
> affine forms as exact alias vars wherever handlers index bounds directly;
> `int_negate` alias added; `among` folds par elements and the empty set.
> New `test_constant_arguments()` in `fzn_semantics_test.py` (fails 10+
> assertions against the pre-fix binary); full `test.sh` green; ASan/UBSan
> corpus sweep clean.

> **2026-08-11 — branch review + set-membership constant fold:** all remote
> branches were re-surveyed from `feat/flatzinc-complete`.  Finding: the
> `arena/continue-hardening` "floor division" change to `int_div`/`int_mod`
> must **not** be ported — the MiniZinc Handbook ("Basic Modelling",
> §2.1.2, and the language spec's arithmetic-operations section) defines
> `a mod b` with the sign of the **dividend** and `a div b` by truncation
> toward zero, i.e. exactly C's `/` and `%`, which the current code
> implements; `divmod_verify.py` pins this.  A genuinely unported bug class
> was found instead: `set_in_reif`/`set_in` with a constant or affine LHS.
> `int_eq_reif(5,5,r)` worked, but `set_in_reif(7,{3,7,9},r)` bound `r=false`
> and non-reified `set_in(265,1..280)` was UNKNOWN — the reified encodings
> overwrote (`=`) instead of accumulating (`-=`) the difference form's
> constant term after `lin_into`.  Fixed, folded, and regression-locked
> (truth tables in `fzn_semantics_test.py`, edges + randomized constant-LHS
> instances in `divmod_verify.py`; full `test.sh` green, ASan/UBSan corpus
> sweep clean).  Branch: `arena/fzn-set-const-ops`.

> **2026-08-08 follow-up:** the remaining remote branches were re-audited from
> `arena/unmerged-audit-and-correctness`. Safe changes were ported, several
> wrong-answer cases were repaired, and the unsound branch-and-clip commit was
> rejected. See [`docs/BRANCH_AUDIT.md`](docs/BRANCH_AUDIT.md) for branch-by-
> branch disposition, counterexamples, and regression coverage.

This is a working audit document. It records what was reviewed, what was
changed on this branch, and the prioritized list of the most important things to
do next. It is intended to be updated as work progresses.

> **Update — integrated `arena/exactness-and-status-audit`.** This branch has
> been merged with the parallel correctness-hardening effort on
> `arena/exactness-and-status-audit`, which found and fixed six further classes
> of "solver lied" / crash bugs (false LP `UNBOUNDED`/`INFEASIBLE` statuses,
> silently wrong exact-rational answers from `int64` overflow, PGS `SIGFPE` on a
> zero diagonal, un-checked allocations proven by OOM-injection testing, MIP
> labelling suboptimal points `OPTIMAL`, and QP returning infeasible/non-optimal
> points as solved). See `docs/AUDIT.md` (from that branch) for the full
> write-up. The combined branch now has both the new FlatZinc handlers
> (`cumulative`, `array_int_maximum`/`array_int_minimum`) *and* the full
> correctness/OOM hardening. Conflict resolution in `src/solver.c`: kept the
> exactness branch's `build_initial_basis` + Phase-I restart logic alongside
> this branch's dense-LU retry wrapper; routed my `solver_reset_to_initial`'s
> scratch allocation through `psolve_calloc` so the OOM-injection suite passes
> (7,542 injection points, 0 failures).

## Baseline state (start of this pass)

- Clean build with the default flags (`-O3 -march=native`, hardening on,
  `-ffast-math` off). No warnings with `-Wall -Wextra`.
- Full `./test.sh` passes. Two differential suites are environment-gated:
  GLPK (`glpsol`) and MiniZinc (`minizinc`) are not installed here, so those
  are SKIPPED, not run.
- The double revised-simplex LP solver (`src/solver.c`), exact-rational
  fixed-point LP solver (`src/fx.c`), QP, MIP, PGS/PGS-fixed physics kernels,
  and the FlatZinc/MiniZinc bridge (`src/fzn.c`, `fznsolve`) are all present
  and mutually cross-checked (LP vs scipy, QP vs scipy, MIP vs brute force,
  fx vs double, table vs brute force).

## Changes on this branch

### 1. `cumulative` constraint handler (`src/fzn.c`)
New exact handler for the FlatZinc `cumulative(s[], d[], r[], b)` scheduling
constraint (also accepts `fzn_cumulative` / `gecode_cumulative`). It handles
the common, exact case where durations `d`, usages `r`, and the limit `b` are
fixed parameters and each start `s_i` is a bounded integer variable:

- For every task `i` and every integer time `t` in the scheduling horizon
  (derived from the start-variable boxes), it introduces binaries
  `a2=[s_i<=t]`, `a1=[s_i+d_i>t]`, and `active = a1 AND a2`, then constrains
  `sum_i r_i*active(i,t) <= b` at every `t`.
- The reifications reuse the existing exact `add_int_reif` (integer lattice
  +/-1, no invented epsilon). The horizon comes from real variable bounds, not
  a synthetic sentinel.
- Variable durations/usages/limits are **not** silently relaxed — they return
  UNHANDLED so the bridge reports `=====UNKNOWN=====` rather than a wrong
  answer. An over-large model is capped and returns UNKNOWN.
- Verified correct against an independent brute-force enumerator
  (`tools/cumulative_verify.py`): **0 mismatches** (SAT/UNSAT) across hundreds
  of random small instances. New examples `examples/fzn/cumulative_sat.fzn`
  and `cumulative_unsat.fzn` are wired into `test.sh`.

### 2. Sparse→dense retry in the double LP solver (`src/solver.c`)
`refactorize` already fell back to dense LU when `splu_factor` *failed*.
Auditing revealed a subtler failure: on some moderately-sparse big-M bases the
sparse LU factorizes "successfully" but is numerically unstable, so the solve
converges to an infeasible point and the solution certificate correctly returns
`SOLVE_NUMERICAL`.

Fix: `solver_solve` now detects a `SOLVE_NUMERICAL` result on the sparse path,
re-initializes the solver to its starting basis (extracted into
`solver_reset_to_initial`, re-runnable and preserving the objective and
iteration limit), and re-solves once with the robust dense LU. This converts a
would-be `NUMERICAL_FAILURE` into a certified answer and never changes the
result of a solve that already converged. Full `test.sh` still passes.

## Key findings (most important things to do)

### A. The double revised-simplex is numerically fragile on big-M MIP relaxations  — *top finding*
The single biggest issue found. The fz/MIP bridge encodes combinatorial
constraints (table, circuit, all_different, set_in, and now cumulative) as
big-M + binary models, and the resulting LP relaxations frequently fail the
double solver's own certificate: `solver_feasible()` returns false at the end
even though the LP is **provably feasible** (verified with `scipy`/HiGHS and the
exact `fxsolve`).

Evidence (cumulative relaxations): full-rank matrices, no duplicate rows, scipy
= OPTIMAL, `fxsolve` = OPTIMAL, but `lpsolve` = `NUMERICAL_FAILURE`. It is not
an objective problem (fails for zero, minimize, and perturbed objectives) and
not a sparse-only problem (fails with dense LU too on some instances).

Impact: real combinatorial FlatZinc models frequently return
`=====UNKNOWN=====` instead of a solution. This is *honest* (never a wrong
answer) and consistent with the documented "weak relaxations on combinatorial
feasibility models" limitation, but it sharply limits usefulness. The `-a`
(all solutions) CLI feature is also gated on this path being reliable.

Recommended fix (highest value): route MIP relaxations through the exact
rational `fxsolve` (`src/fx.c`) — either always for these integer LPs or as a
fallback when the double solve returns `SOLVE_NUMERICAL`. `fxsolve` provably
handles the instances the double solver cannot. This would robustify the whole
big-M/MIP path (cumulative, table/circuit at scale, `-a` enumeration).

### B. Phase I degeneracy on infeasible LPs (known, re-confirmed)
Some genuinely infeasible degenerate LPs iterate to the Phase-I iteration limit
and report `ITERATION_LIMIT` (honest) rather than `INFEASIBLE`. Not a wrong
answer, but a completeness gap. (Roadmap notes this; no change here.)

### C. Minor cleanup done / noted
- Removed a dead duplicate `-t` branch in `tools/fznsolve.c` arg parsing.
- `-Wshadow` warnings in `src/fzn.c` are benign re-declarations; left as-is.

### DONE — exact-solver fallback for MIP relaxations (was finding A)
Implemented. `src/mip.c` now cross-checks a relaxation with the fixed-point
exact-rational simplex (`src/fx.c`) whenever the double revised-simplex returns
`SOLVE_NUMERICAL` **or** a false `INFEASIBLE` — both of which it does on the
ill-conditioned big-M bases of combinatorial MIPs. `fx_from_double` /
`FX_INF_SENT` are exported; `mip_build_fxlp()` converts the double MIP + per-node
bounds into an exact FxLP. Only exactly integral `double` values are accepted;
duplicate CSC cells are summed with checked rational arithmetic. When the exact
solve succeeds its verdict wins; otherwise the honest double verdict is kept.

Impact: `cumulative_verify.py 200 777` went from **OK=124 UNKNOWN=76** on
`main` to **OK=200 UNKNOWN=0 MISMATCH=0**. The false-INFEASIBLE→UNSAT bug is
fixed, with a deterministic regression in `examples/fzn/cumulative_exact.fzn`.

### DONE — CLI `-a` (all solutions) and FlatZinc constraint expansion
Implemented standard `-a` / `--all-solutions` for `fznsolve`, `fz_solve`, and `mip_solve`:
- In satisfaction problems (`solve satisfy`), `-a` traverses the branch-and-bound
  tree and prints distinct **visible output tuples**, de-duplicating alternative
  auxiliary-selector assignments. Continuous output spaces return `UNKNOWN`
  rather than falsely printing a finite completion marker.
- In optimization problems (`solve minimize` / `solve maximize`), `-a` reports all intermediate
  strictly-improving incumbents, each followed by `----------`, and terminates with `==========`
  once optimality is proved.
- In default single-solution mode, optimization problems correctly print the proved optimum
  followed by `----------` and `==========` per the FlatZinc standard.

Expanded native FlatZinc constraint handlers:
- `array_var_int_element`, `array_bool_element`, `array_var_bool_element`,
  `array_float_element`, `array_var_float_element` (exact SOS1 and bounded indicator encodings).
- `array_float_maximum`, `array_float_minimum` (exact selector formulation for float arrays).
- `set_in_reif`, `int_in`, `int_in_reif` (exact reified integer set and range membership).
- `int_div`, `int_mod` (exact linear quotient-remainder formulation for constant divisors).
- `int_pow` (linear exponentiation for integer constants and bounded lattice domains).
- `bool_times` / `int_times` (linear boolean conjunction).
- `count_leq`, `count_geq`, `count_lt`, `count_gt`, `count_ne`, `count_neq`, `among`, `fzn_among`
  (exact reified count and among constraints).
- `table_bool`, `fzn_table_bool`, `gecode_table_bool` (extensional boolean table constraints).

The port was hardened for negative division/modulo, variable count values,
proven element big-M bounds, exact-range exponentiation, and output-level
all-solutions de-duplication. See `docs/BRANCH_AUDIT.md`. All new handlers and
options have dedicated regressions in `tools/fzn_semantics_test.py`.

### DONE — public C API invalid-input audit

Public LP, MIP, QP, exact LP, PGS, FlatZinc, LU, and sparse-LU entry points now
check null/structurally invalid inputs before dereferencing. Invalid models have
dedicated statuses rather than being mislabeled infeasible. `tools/api_test.c`
is wired into `test.sh`; details and differences from remote commit `2253a93`
are recorded in `docs/BRANCH_AUDIT.md`.

### DONE — batch PGS entry points

`pgs_batch_solve` and `pgsf_batch_solve` provide checked sequential traversal of
independent contact systems with aggregate statistics. The global arena half of
remote commit `6ebf11d` was rejected for ownership/alignment/thread-safety bugs;
see `docs/BRANCH_AUDIT.md`.

### DONE — FlatZinc set-literal + status honesty (branch `arena/fzn-set-status-ports`)

Follow-up merge of the still-unmerged parts of `arena/continue-hardening`,
after verifying each of its claims empirically against the MiniZinc
specification (see `docs/BRANCH_AUDIT.md` for the full review):

- **Set-literal parsing no longer truncates silently** (`fz_parse_int_set`):
  `set_in(y,{1,...,280})` with `y=265` had printed `UNSATISFIABLE`; a
  singleton set had overwritten the declared domain (`var 0..5` + `{8}` had
  printed `x = 8`); `among` double-counted duplicate set members.  All three
  fixed and covered by the new brute-force differential
  `tools/divmod_verify.py` (0 wrong answers; wired into `test.sh`).
- **UNSAT is only certified inside the synthetic box**: an infeasible
  verdict on a model containing an undeclared (`var int:`/`var float:`)
  variable is downgraded to UNKNOWN.
- **GLPK differential re-run and green** (sweep 119/119, four-way difftest
  0 mismatches) — glpsol was obtained for this environment.
- That branch's `int_div`/`int_mod` floor-semantics rewrite was **rejected**:
  MiniZinc defines `mod` with the dividend's sign (truncation, C semantics),
  which `main` already implements.  The verifier it ships now encodes the
  spec-correct reference semantics explicitly (with citations), so a future
  contributor cannot make the same mistake either direction.

## Not done (recommended next steps, in priority order)
1. ~~Re-run the MiniZinc differential suite~~ — **done 2026-08-14** with the
   MiniZinc 2.9.4 IDE bundle (OK=33 FAIL=0, benchmark 77/77; the bundle's
   referees are gecode/chuffed/cp-sat — coin-bc no longer ships, so
   `tools/mzn_bench.py` falls back to cp-sat for the linear reference).
2. ~~Redesign the global `setjmp` allocation-error protocol~~ —
   **done 2026-08-15(7)** (this round): caller-owned `PSolveErrFrame`
   storage chained through thread-local state replaces the process-global
   `psolve_env/psolve_active/psolve_code` triple (and `psolve_stop_fn`
   became a per-thread callback); the library now exports ZERO non-TLS
   mutable data symbols (nm-gated in test.sh).  Nested scopes — refused
   by the old protocol — compose; `tools/err_proto_test.c` (12 checks)
   and the 8-thread TSan-clean `tools/err_mt_test.c` pin the semantics.
   See the (7) addendum above.
3. ~~Farkas-certificate check for exact re-solves in the MIP bridge~~ —
   **done 2026-08-15(5)** (this round): `solver_farkas_duals` extracts the
   phase-1 dual ray as a hint and `mip_farkas_certified` interval-checks the
   full yᵀA separation with directed rounding (uncertain components take the
   safer bound extreme, engine margin kept), replacing the full
   exact-rational re-solve per infeasible-verdict node.  tsp_5: 96
   certificates, exactResolves 0, 0.455s → 0.218s with identical
   verdicts/tree/solutions; discriminating test `tools/farkas_verify.py` in
   `test.sh`.  See the (5) addendum above.
4. ~~Honesty gap on extreme scale-mixed *non-integral* LPs~~ —
   **done 2026-08-15(6)** (this round): a bare-INFEASIBLE verdict from
   the double phase-1 is now re-verified by the exported
   `solver_farkas_boxcert` (directed-rounding check of the dual ray
   against original rows + box); if the certificate fails and
   `solver_row_exposure`·eps ≥ 5e-7 the verdict is promoted to honest
   NUMERICAL_FAILURE / SOLVE_NUMERICAL / UNKNOWN in lpsolve, the MIP
   bridge, and the FZ pure-LP branch respectively.  Truly infeasible
   shaky models keep INFEASIBLE, certificate-backed (60/60 rescued);
   the pre-change fabrications (6 reproduced by the discriminating
   tool) are gone.  Hard gate `tools/lp_scale_verify.py` in `test.sh`.
   See the (6) addendum above.  `lpsolve` remains the double-precision
   engine — for certifiable answers on such data `fxsolve` with
   integral scaling remains the right tool.

