# psolve — audit & hardening notes

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
2. Redesign the global `setjmp` allocation-error protocol so a recovering,
   multi-threaded library host can own cleanup without process-global state.
