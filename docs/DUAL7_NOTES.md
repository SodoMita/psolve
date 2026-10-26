# Dual simplex (roadmap 7.2) — implementation + evidence 2026-09-21

Branch `arena/01a0c53b-psolve`.  House discipline throughout:
reproduce → fix → prove; decline degrades to a clean re-solve, never to a
guess.

## Why

`solver_set_bounds` + `solver_warm_solve` is the MIP hot loop (every B&B
node).  A tightened bound keeps the previous basis **dual feasible**
(reduced costs do not depend on bounds) but typically **primal
infeasible** (a basic variable violates its new bound).  The pre-change
warm path ignored that structure: it ran bounded-variable *primal*
simplex from a primal-infeasible point (nonbasic values stale vs the new
box), produced garbage pivots, failed its own `solver_feasible`
certificate, and paid `solver_refresh` — a full from-scratch re-solve —
as the usual landing for feasibility-breaking nodes.

The dual simplex is the structurally correct algorithm here: it starts
exactly from a dual-feasible / primal-infeasible basis and restores
primal feasibility with optimal-basis-shaped pivots.

## What (this session)

1. `dual_phase`/`dual_iterate` in `src/solver.c` — bounded-variable dual
   simplex: largest scaled bound-violation leaves, BTRAN pivot row, dual
   minimum-ratio test with a Harris tie-break (largest `|g|` within a
   relaxation of the minimum ratio), eta-file updates with periodic
   refactorize + exact-recompute of the basic values, degenerate-plateau
   and budget caps that surface as honest statuses.
2. Warm-entry **nonbasic reconciliation**: values are snapped into the
   current box AND the status is re-derived from the value's actual
   parked bound (ties to lower).  The status side turned out to be the 's
   load-bearing half — see "the bug the bench was for" below.
3. **Dual-cert infeasibility**: when no entering column can repair a
   row, the leaving row of B⁻¹ yields a dual Farkas ray; it is re-proven
   against the ORIGINAL data with the existing directed-rounding
   `solver_farkas_boxcert` before verdict 1 is returned.  Every uncertified
   or numerical lane falls back to `solver_refresh` — one clean re-solve,
   no guesses.
4. `solver_dual_farkas()` export + mip.c adoption: the MIP bridge pulls
   the dual-certified ray as a HINT next to the Phase-I ray and
   re-verifies it against node-box data at MIP margins — same hint
   discipline as before: a rejected hint falls through to the exact-rational
   re-solve, so this cannot alter a verdict.
5. Optimality epilogue: the dual exit re-prices once and declines to a
   clean re-solve if any sign-free nonbasic reduced cost violates its
   side (never fired on the 48k-round battery; zero-cost insurance
   against future drift lanes).
6. Counters with transplant semantics: `s->iters`, `s->dual_iters`,
   `s->dual_certs`, `s->refreshes` accumulate across `solver_refresh`'s
   internal transplant swap, so instrumentation reads true totals.
   `mip.c` reports `lp_iters` (all simplex iterations across B&B nodes);
   `mipsolve --print` prints it.

## The bug the bench was for

First full tree A/B: the new engine claimed OPTIMAL on 14 of 61k nodes
with objectives provably above the fresh optimum (bench kd=20 node=96:
warm −0.220381157871147 vs true −3.15429050179687 on identical boxes),
and `solver_feasible` passed the point at 2e-15 row residual.  A trace
minimization showed the exit basis was NOT dual-feasible: a variable had
left the basis during an ancestor node whose box was [1,1] (status NBL at
value 1); a later sibling widened its lower bound back to 0, and the warm
entry repaired only the *value*, leaving the variable parked at u=1 with
an NBL status.  Both the dual-feasibility gate and the dual ratio test
derive their off-bound sign from the status — mis-signed by one variable,
the invariant was silently false from step 0 and nine "legal" dual pivots
converged to a fabricated optimum.

Fix: item 2 above (reconcile status from the value).  Post-fix the same
battery reports 0 such nodes, and the exit epilogue (item 5) makes the
class structurally impossible to claim through the dual path again.

The battery lanes that reproduce the pre-fix lane live in
`tools/dual_test.c` (tighten / widen / pin→widen alternation on the same
variable).  On the OLD engine those lanes light up immediately
(warm=OPTIMAL vs fresh=INFEASIBLE at ~every chain), quantifying what the
pre-7.2 warm path was.

## Acceptance evidence

**Parity half** — `tools/dual_test.c`, 24,000 seeds × 2 walls
(20260921, 424242), 47,993/47,995 warm rounds each against a fresh cold
solve of the identical node box:

    ALL OK both walls
    worst objective gap (agreed optima)      1.4e-11 / 9.9e-12
    dual engaged rounds                      3162 / 3156
    dual-certified infeasible rounds         67   / 78
    refresh fallbacks                        37694/ 37862
    verdict classes (warm == fresh): optimal / infeasible / unbounded /
                                     agreed limit-or-numerical
    declines: 1 warm-side per wall (seed-15148-class knife-edge, proven
      identical on the pre-change engine), 2 fresh-side on wall 1
      (seed-2570 rounds 0/1, proven identical on the pre-change engine)
    state hash bit-stable across reruns: 43d1b2fe3ec8ed37 / 38ba5bb57f8c8641
    ASan+UBSan build: 12,000 seeds ALL OK, no findings

**Performance half** — `tools/dual_bench.c`, 300 dense mixed-relation
0-1 MIP relaxations, node budget 3000, seed 77031, identical B&B walk on
both engines (tree checksum equality certifies same nodes, same prunes,
same leaf incumbents: `4b6285ff0a75ba80`):

    nodes 38,808   infeasible-pruned 18,853   bound-pruned 606   leaves 95
    pre-7.2 primal warm start:   552,930 simplex iterations (1.34s)
      of which 341,108 (62%) were wasted pivots before the refresh
      transplant discarded them (-DPREBURN instrumentation build)
    post-7.2 dual re-solve:      265,573 simplex iterations (1.12s)
    iteration ratio 2.08x  (acceptance: >= 2x)

    FRESHCHECK sweep over the same corpus: 0 warm-vs-fresh objective
    mismatches in 38,808 nodes.

**End-to-end (MIP bridge)** — 200 generated integer LPs through
`mipsolve --print` on both engines: statuses/objectives/nodes/
farkas_certs byte-identical; lp_iters 182,905 → 89,364 (2.05x).
`tools/mip_diff.py 150 555`: 150/150 correct on both engines.

**House gate** — `./test.sh` output identical to the pre-change baseline
log (the only delta is environmental: numpy missing in qp_psd_verify,
present in both runs).  `make` tree: zero warnings. Read back-to-front 2
times before commit per house rule.

## Known honest boundaries (by design)

- The dual path engages only after a Phase-II-optimal-or-clean terminal;
  Phase-I-infeasible/ray terminals with live artificials refresh (cheap,
  correct, and rare on B&B chains: 25.6k/48k rounds were refresh for
  other reasons — node boxes that broke the basis classification, exactly
  as on the old engine).
- Tolerance classes: dual enters only when the warm basis prices dual
  feasible within TOL_DJ; knife-edge boxes may decline to refresh (one
  budgeted warm decline per 10k rounds, always proven pre-existing).
- The dual Farkas hint is advisory at the MIP bridge: the bridge
  re-verifies against original rows at its own margins before pruning.
