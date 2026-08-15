#include "mip.h"
#include "fx.h"
#include "err.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <fenv.h>
#include <float.h>

#define MIP_TOL 1e-6

/* Build an exact-rational FxLP from the double MIP data plus per-node bounds.
 * Used to retry a relaxation with the exact solver when the double revised
 * simplex diverges (returns SOLVE_NUMERICAL) on a big-M relaxation.
 *
 * Only integral coefficients/bounds are representable exactly (see
 * fx_from_double); a model with float data returns -1 so the caller keeps the
 * double solver's honest failure rather than producing a wrong answer.  On
 * success the caller owns *flp (free with fx_free). */
static int mip_build_fxlp(const MIP *mip, const double *lo, const double *hi, FxLP *flp){
    int n = mip->n, m = mip->m;
    size_t cells;
    memset(flp, 0, sizeof(*flp));
    if(n <= 0 || m < 0 || (m > 0 && (size_t)n > (size_t)-1 / (size_t)m))
        return -1;
    cells = m ? (size_t)m * (size_t)n : 1;
    if(cells > (size_t)-1 / sizeof(Fx)) return -1;
    flp->n = n; flp->m = m; flp->maximize = mip->maximize;
    flp->c = (Fx*)psolve_calloc((size_t)n, sizeof(Fx));
    flp->b = (Fx*)psolve_calloc((size_t)(m ? m : 1), sizeof(Fx));
    flp->l = (Fx*)psolve_calloc((size_t)n, sizeof(Fx));
    flp->u = (Fx*)psolve_calloc((size_t)n, sizeof(Fx));
    flp->lfinite = (int*)psolve_calloc((size_t)n, sizeof(int));
    flp->ufinite = (int*)psolve_calloc((size_t)n, sizeof(int));
    flp->rel = (char*)psolve_malloc((size_t)(m ? m : 1));
    flp->A = (Fx*)psolve_calloc(cells, sizeof(Fx));
    if(!flp->c || !flp->b || !flp->l || !flp->u ||
       !flp->lfinite || !flp->ufinite || !flp->rel || !flp->A){ fx_free(flp); return -1; }
    for(int j = 0; j < n; j++) if(fx_from_double(mip->c[j], &flp->c[j]) != 0) goto fail;
    for(int i = 0; i < m; i++) if(fx_from_double(mip->b[i], &flp->b[i]) != 0) goto fail;
    for(int i = 0; i < m; i++) flp->rel[i] = mip->rel[i];
    for(int j = 0; j < n; j++){
        if(lo[j] <= -1e29){ flp->lfinite[j] = 0; flp->l[j].num = -FX_INF_SENT; }
        else { if(fx_from_double(lo[j], &flp->l[j]) != 0) goto fail; flp->lfinite[j] = 1; }
        if(hi[j] >= 1e29){ flp->ufinite[j] = 0; flp->u[j].num = FX_INF_SENT; }
        else { if(fx_from_double(hi[j], &flp->u[j]) != 0) goto fail; flp->ufinite[j] = 1; }
        if(!flp->lfinite[j] && !flp->ufinite[j]) goto fail;   /* free var unsupported */
    }
    for(int j = 0; j < n; j++){
        if(mip->Acolptr[j] < 0 || mip->Acolptr[j+1] < mip->Acolptr[j]) goto fail;
        for(int k = mip->Acolptr[j]; k < mip->Acolptr[j+1]; k++){
            int r = mip->Arow[k];
            Fx term;
            if(r < 0 || r >= m) goto fail;
            if(fx_from_double(mip->Aval[k], &term) != 0 ||
               fx_add_checked(flp->A[(size_t)r * n + j], term,
                              &flp->A[(size_t)r * n + j]) != 0) goto fail;
        }
    }
    return 0;
fail:
    fx_free(flp);
    return -1;
}


/* LP-rounding feasibility heuristic: round the relaxation solution's integer
   variables to integer values inside the current node bounds and test the
   resulting point against every constraint.  This cheaply produces a feasible
   integer incumbent at nodes where the LP optimum is near a lattice point,
   which is essential for `solve satisfy` problems (there the first feasible
   integer point is an answer).  Returns 1 and fills xc on success. */
static int try_rounding_heuristic(const MIP *mip, const double *x,
                                  const double *lcur, const double *ucur,
                                  double *xc)
{
    int n = mip->n, m = mip->m;
    for (int j = 0; j < n; j++) {
        double v = x[j];
        if (mip->isint[j]) {
            double r = floor(v + 0.5);
            /* Clamping into the node box must land on a LATTICE point.  The
               old code clamped to the raw bound, so a fractional bound (say
               u = 1.875) produced xc[j] = 1.875 for a variable declared
               integer -- and that fractional point was then accepted as an
               integer incumbent and reported as the optimum. */
            if (r < lcur[j]) r = ceil(lcur[j] - MIP_TOL);
            if (r > ucur[j]) r = floor(ucur[j] + MIP_TOL);
            if (r < lcur[j] - MIP_TOL || r > ucur[j] + MIP_TOL) return 0;
            if (fabs(r - floor(r + 0.5)) > MIP_TOL) return 0;
            v = floor(r + 0.5);
        }
        xc[j] = v;
    }
    for (int j = 0; j < n; j++)
        if (xc[j] < lcur[j] - MIP_TOL || xc[j] > ucur[j] + MIP_TOL) return 0;
    double *rs = (double*)psolve_malloc((size_t)(m ? m : 1) * sizeof(double));
    for (int i = 0; i < m; i++) rs[i] = 0.0;
    for (int j = 0; j < n; j++) {
        for (int k = mip->Acolptr[j]; k < mip->Acolptr[j + 1]; k++)
            rs[mip->Arow[k]] += mip->Aval[k] * xc[j];
    }
    int ok = 1;
    for (int i = 0; i < m && ok; i++) {
        char r = mip->rel[i]; double b = mip->b[i];
        if (r == '=')      { if (fabs(rs[i] - b) > MIP_TOL) ok = 0; }
        else if (r == '<') { if (rs[i] > b + MIP_TOL) ok = 0; }
        else if (r == '>') { if (rs[i] < b - MIP_TOL) ok = 0; }
    }
    psolve_free(rs);
    return ok;
}

typedef struct Node {
    double *lo, *hi;      /* tightened bounds for this node (n) */
    double bound;         /* relaxation objective value (for ordering) */
    int feasible;
    struct Node *next;    /* linked list (DFS stack, best-bound sorted) */
} Node;

/* priority by best bound: we keep a simple sorted-insert list ordered by
   bound (descending for maximize, ascending for minimize) so the best-bound
   node is popped first. */
static void push_node(Node **list, Node *node, int maximize)
{
    Node *cur = *list, *prev = NULL;
    int before;
    while (cur) {
        before = maximize ? (node->bound > cur->bound + 1e-9)
                          : (node->bound < cur->bound - 1e-9);
        if (before) break;
        prev = cur; cur = cur->next;
    }
    node->next = cur;
    if (prev) prev->next = node; else *list = node;
}

static Node *pop_node(Node **list)
{
    Node *n = *list; if (n) *list = n->next;
    return n;
}

/* ------------------------------------------------------------------ */
/* Rigorous interval row-conflict certificate, O(nnz).
 *
 * For every row i the minimum and maximum possible activity over the box
 * [lo,hi] is accumulated in DIRECTED rounding: the minimum side under
 * FE_DOWNWARD, the maximum side under FE_UPWARD, so the computed mn[i] is
 * a proven LOWER bound of the exact row minimum over the double data and
 * mx[i] a proven UPPER bound of the exact row maximum (every stored double
 * is an exact dyadic rational; each product and each partial sum rounds
 * toward the accumulated side, an FMA contraction only rounds once in the
 * same direction, and reassociation/vectorization cannot break the bound:
 * the exact total is association-independent while every computed partial
 * is on the correct side of the corresponding exact partial).
 *
 *   '<'/'=' conflict: mn[i] > b[i]   (even the best case violates the row)
 *   '>'/'=' conflict: mx[i] < b[i]
 *
 * There are NO tolerances: a return of 1 is a proof that no point of the
 * box satisfies all rows.  Unbounded sides (sentinel magnitude ~LP_INF,
 * gated at 1e29 like the rest of this file) poison the affected
 * accumulator with NaN so the row cannot certify in that direction; NaN
 * propagates through every later partial sum and the final isfinite()
 * filter then excludes it (along with any arithmetic overflow), so "cannot
 * say" is always the answer a degenerate row produces.  The rounding mode
 * is saved and restored around the two sweeps, and nothing in between can
 * allocate or longjmp -- these sweeps run inside error-trapping library
 * code (psolve_env), so no call that can fail is allowed there.
 *
 * This replaces the round-to-nearest, tolerance-padded prune that used to
 * guard FBBT root infeasibility: with catastrophic cancellation the RN
 * error exceeds the tolerance (products near 1e12 round by up to ~6e-5
 * while the margin was 1e-6), and "MN overestimate > rhs + tol" fires on a
 * FEASIBLE model -- a fabricated-UNSAT verdict reached before any solve
 * and therefore before the exact-rational cross-check that guards the same
 * status inside the branch-and-bound loop.  Regression of record:
 * "maximize x2, x2 == 1, 9.999853740683515e-14*x0 - ...e-14*x1 + 0.75*x2
 *  <= 0.7504304904478102, x0/x1 fixed near 1e25": the RN activity
 * 0.75048828125 exceeds rhs + 1.75e-6 while the exact activity
 * 0.7504294904478... is feasible; mipsolve printed INFEASIBLE.
 *
 * Returns 1 if a conflict is PROVEN, else 0 ("cannot say").  mn/mx are
 * caller-owned m-sized scratch so per-node callers avoid malloc churn. */
static int mip_box_conflict(const MIP *mip, const double *lo, const double *hi,
                            double *mn, double *mx)
{
    const double BIG = 1e29;
    int n = mip->n, m = mip->m;
    if (m > 0) for (int i = 0; i < m; i++) mn[i] = 0.0;
    int rm = fegetround();
    fesetround(FE_DOWNWARD);          /* min side: every op rounds down */
    for (int j = 0; j < n; j++) {
        double lj = lo[j], hj = hi[j];
        int lo_inf = (lj <= -BIG), hi_inf = (hj >= BIG);
        for (int k = mip->Acolptr[j]; k < mip->Acolptr[j + 1]; k++) {
            int i = mip->Arow[k];
            double a = mip->Aval[k];
            if (a > 0.0) {
                if (lo_inf) { mn[i] = NAN; } else mn[i] += a * lj;
            } else {
                if (hi_inf) { mn[i] = NAN; } else mn[i] += a * hj;
            }
        }
    }
    if (m > 0) for (int i = 0; i < m; i++) mx[i] = 0.0;
    fesetround(FE_UPWARD);            /* max side: every op rounds up */
    for (int j = 0; j < n; j++) {
        double lj = lo[j], hj = hi[j];
        int lo_inf = (lj <= -BIG), hi_inf = (hj >= BIG);
        for (int k = mip->Acolptr[j]; k < mip->Acolptr[j + 1]; k++) {
            int i = mip->Arow[k];
            double a = mip->Aval[k];
            if (a > 0.0) {
                if (hi_inf) { mx[i] = NAN; } else mx[i] += a * hj;
            } else {
                if (lo_inf) { mx[i] = NAN; } else mx[i] += a * lj;
            }
        }
    }
    fesetround(rm);
    for (int i = 0; i < m; i++) {
        char rel = mip->rel[i];
        double rhs = mip->b[i];
        /* Semantics note (learned the hard way, mip_diff seed 12345 it=236):
           the engine's row feasibility is TOLERANCE-based (check_solution:
           MIP_TOL absolute; the LP core: TOL_FEAS=1e-9), so a certificate
           that prunes on exact arithmetic alone CHANGES the declared
           semantics -- a borderline row like 2.293*(-3) = -6.879, exact
           activity 8.9e-16 inside/outside the rhs, is feasible to every
           layer of this solver and to the independent brute-force
           reference.  A prune may only fire when not even a tolerance-slack
           assignment survives, hence the margin.  It cannot reintroduce the
           cancellation fabrication: mn/mx are rigorous directed-rounding
           bounds (never an RN overestimate), so firing requires the TRUE
           extremum to exceed rhs by the full margin. */
        double mar = MIP_TOL * (1.0 + fabs(rhs));
        if ((rel == '<' || rel == '=') && isfinite(mn[i]) && mn[i] > rhs + mar) return 1;
        if ((rel == '>' || rel == '=') && isfinite(mx[i]) && mx[i] < rhs - mar) return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Farkas infeasibility certificate over the ORIGINAL rows and node box,
 * checked with directed rounding in O(m + nnz).  Input `ys` is the raw
 * Phase-I dual vector the double solver left behind on an INFEASIBLE
 * verdict (solver_farkas_duals, scaled-row space); `mlt` is the solver's
 * per-row sign (+1/-1 by rhs sign).  y/zl/zh are caller scratch (m/n/n).
 *
 * The solver's vector is a HINT, never trusted on its own: the complete
 * Farkas conditions are re-verified here against the original MIP data, so
 * even a garbage or stale ray can only fail this check -- at which point the
 * caller pays for the exact-rational re-solve exactly as before.  It can
 * never fabricate a prune.
 *
 * Proof shape.  Scale back to original rows: y_i = mlt_i * ys_i.  For every
 * row-feasible x we need the componentwise implication y_i a_i x <= y_i b_i,
 * which holds by the row's own constraint iff y_i >= 0 on '<' rows and
 * y_i <= 0 on '>' rows ('=' rows: any sign, the constraint is an identity).
 * Components violating their sign are CLAMPED to 0: the row then contributes
 * the trivial 0 <= 0, which weakens the ray but keeps it sound for any
 * feasible point.  With z := y^T A it follows that z.x <= y^T b for every
 * (row-feasible, box-feasible) x, so
 *
 *     min_{box} z.x  >  y^T b  (+ engine margin)   ==>   box has no feasible point.
 *
 * min z.x over the rectangular box with z_j in the directed-rounding
 * interval [zl_j, zh_j] is bounded below by the corner minimum
 * min{zl*l, zl*u, zh*l, zh*u} (all products rounded DOWNWARD), accumulated
 * downward; y^T b is accumulated UPWARD.  An infinite bound touching any
 * corner, a NaN/inf anywhere, or a non-finite partial makes the whole check
 * fail ("cannot say") -- mirroring mip_box_conflict's poison semantics.
 *
 * The margin mirrors the engine semantics called out in mip_box_conflict:
 * row feasibility in this engine is tolerance-based (check_solution accepts
 * MIP_TOL slack), so a node that is only epsilon-infeasible still hosts
 * lattice points the engine itself would accept as incumbents; pruning it
 * would change the OPTIMUM the engine reports.  mar = MIP_TOL*(1+|R|) only
 * lets certificates fire when no tolerance-slack assignment survives. */
static int mip_farkas_certified(const MIP *mip, const double *lo, const double *hi,
                                const double *ys, const int *mlt,
                                double *y, double *zl, double *zh)
{
    if (mip->m <= 0 || mip->n <= 0) return 0;
    /* shared directed-rounding checker lives in solver.c so the LP CLI and
       the FlatZinc bridge can gate their own infeasibility verdicts with the
       same proof shape (the long proof comment stays there); the MIP margin
       is the engine's MIP_TOL. */
    return solver_farkas_boxcert(mip->n, mip->m, mip->Acolptr, mip->Arow,
                                 mip->Aval, mip->rel, mip->b, lo, hi,
                                 ys, mlt, MIP_TOL, y, zl, zh);
}

/* Persistent simplex state reused across the whole branch-and-bound tree.
 * Successive node relaxations differ ONLY in the variable bounds, which is
 * exactly the supported incremental case (solver_set_bounds +
 * solver_warm_solve): the warm solve re-factorizes the previous basis and
 * re-enters phase 2, and transparently falls back to a full cold rebuild
 * (solver_refresh) whenever the basis cannot be kept feasible -- it never
 * returns an unverified optimum.  The first node solves cold (phase 1
 * included), establishing the basis every later node warms from. */
typedef struct {
    Solver *s;         /* NULL until the first (cold) solve creates it */
    int     started;
    double *blo;       /* intersected per-node box scratch (n each) */
    double *bhi;
    double *cmin;      /* row-activity scratch for mip_box_conflict (m each) */
    double *cmax;
    double *fy;        /* raw Phase-I dual ray from the solver (m) */
    double *fyc;       /* sign-clamped ray (m) */
    double *fzlo;      /* columnwise z = y^T A bounds (n each) */
    double *fzhi;
    long    farkas_certs;  /* nodes certified infeasible by the Farkas path */
    long    fx_solves;     /* relaxations handed to the exact fx solver */
} MipWarm;

/* build an LP from the MIP with per-node tightened bounds, and solve it;
 * warm-starts from the previous node's basis whenever possible */
static int solve_relaxation(const MIP *mip, const Node *node, MipWarm *ws,
                            double *x, double *obj, double *lcur, double *ucur)
{
    int n = mip->n;
    double *lo = ws->blo;
    double *hi = ws->bhi;
    for (int j = 0; j < n; j++) {
        double lj = node->lo[j] > mip->l[j] ? node->lo[j] : mip->l[j];
        double uj = node->hi[j] < mip->u[j] ? node->hi[j] : mip->u[j];
        if (lj > uj) return -1;   /* infeasible node */
        lo[j] = lj; hi[j] = uj;
    }
    /* Sound certificate before any solve: if one row's best-case activity
       over the node box already violates its rhs, the relaxation is
       infeasible and the node prunes -- without running the double LP and,
       crucially, without the exact-rational cross-check that such nodes
       previously paid for their returned-INFEASIBLE verdict (the histogram
       showed those exact re-solves dominating wall time on combinatorial
       models: 153 of 255 nodes on tsp_5).  The certificate doesn't mutate
       the box, touch the warm-start state, or change node accounting, so
       the search evolves exactly as before; only the dead nodes' solve
       cost disappears.  Corner noted for the record: a certified node whose
       double LP would previously have hit its iteration limit (honest
       status 3 search stop) now prunes for free and the search continues --
       fewer honest limits, never a different verdict. */
    if (mip->m > 0 &&
        mip_box_conflict(mip, lo, hi, ws->cmin, ws->cmax)) return -1;

    Solver *s;
    int r;
    if (!ws->started) {
        LP lp;
        memset(&lp, 0, sizeof(lp));
        lp.n = n; lp.m = mip->m; lp.maximize = mip->maximize;
        lp.c  = (double*)mip->c;
        lp.Acolptr = (int*)mip->Acolptr;
        lp.Arow = (int*)mip->Arow;
        lp.Aval = (double*)mip->Aval;
        lp.rel = (char*)mip->rel;
        lp.b  = (double*)mip->b;
        lp.l  = lo;
        lp.u  = hi;
        s = solver_create(&lp);
        if (!s) return -1;
        if (mip->lp_iter_limit > 0) s->iteration_limit = mip->lp_iter_limit;
        r = solver_solve(s);
        ws->s = s;
        ws->started = 1;
    } else {
        s = ws->s;
        solver_set_bounds(s, lo, hi);
        r = solver_warm_solve(s);
    }
    int status = r;
    if (r == 0) {
        double *xo = (double*)psolve_malloc((size_t)n * sizeof(double));
        solver_optimum(s, xo, obj);
        for (int j = 0; j < n; j++) x[j] = xo[j];
        psolve_free(xo);
    } else if (r == SOLVE_NUMERICAL || r == 1) {
        int need_exact = 1;
        if (r == 1 && mip->m > 0 && s->farkas_ok) {
            /* Farkas fast path: pull the Phase-I dual ray as a hint and prove
               (or fail to prove) infeasibility against the ORIGINAL rows and
               node box with directed rounding.  A certified node prunes for
               O(nnz) instead of paying the exact-rational re-solve; any hint
               the re-verification rejects (sign-incoherent, numerically
               poisoned, margin-borderline) falls through to the exact path
               unchanged, so this cannot alter a verdict.  (2026-08-15(3)
               instrumentation: 121 of 122 tsp5 exact re-solves were
               duality-level infeasibility -- precisely this case.) */
            if (solver_farkas_duals(s, ws->fy) == 0 &&
                mip_farkas_certified(mip, lo, hi, ws->fy, s->mlt,
                                     ws->fyc, ws->fzlo, ws->fzhi)) {
                status = 1;      /* certified infeasible: skip the re-solve */
                need_exact = 0;
                ws->farkas_certs++;
            }
        }
        /* The double revised-simplex either diverged (SOLVE_NUMERICAL, its
           solution certificate failed) or declared the relaxation INFEASIBLE.
           On the ill-conditioned big-M bases of combinatorial MIPs
           (table/circuit/cumulative) both are possible even when the
           relaxation is feasible.  Cross-check the SAME relaxation exactly
           with the fixed-point rational simplex, which is immune to double
           rounding.  If the data are integral and the exact solve succeeds we
           trust its verdict; otherwise we keep the double solver's honest
           result. */
        if (need_exact) {
        FxLP flp; memset(&flp, 0, sizeof(flp));
        if (mip_build_fxlp(mip, lo, hi, &flp) == 0) {
            ws->fx_solves++;
            FxResult fres; memset(&fres, 0, sizeof(fres));
            int fr = fx_solve(&flp, &fres);
            if (fr == FX_OPTIMAL) {
                status = 0;
                for (int j = 0; j < n; j++) x[j] = fx_todouble(fres.x[j]);
                *obj = fx_todouble(fres.obj);
            } else if (fr == FX_INFEASIBLE) status = 1;
            else if (fr == FX_UNBOUNDED) status = 2;
            else status = r;   /* iter limit / overflow: keep the double verdict */
            fx_result_free(&fres);
        }
        fx_free(&flp);
        if (status == 1 && r == 1) {
            /* Neither the Farkas certificate nor the exact fx engine could
               arbitrate this infeasibility verdict (non-integral data, an
               exact-engine limit, or a declined ray).  On extreme scale-mixed
               boxes the double phase-1's infeasibility proof is numerically
               shaky: its absolute artificial-sum tolerance (1e-6) is small
               against the rounding noise of the products feeding it
               (exposure * eps past half that tolerance).  Pruning the node on
               an uncertified shaky verdict can fabricate UNSAT -- the
               dangerous, verifier-free direction (AUDIT.md not-done #4;
               regression pins in tools/lp_scale_verify.py).  Report the
               honest numerical failure instead; the verdict was trustworthy
               whenever the exposure is comfortably below the frontier. */
            double E = solver_row_exposure(n, mip->m, mip->Acolptr, mip->Arow,
                                           mip->Aval, lo, hi);
            if (E * DBL_EPSILON >= 5e-7) status = SOLVE_NUMERICAL;
        }
        }
    }
    for (int j = 0; j < n; j++) { lcur[j] = lo[j]; ucur[j] = hi[j]; }
    return status;   /* solver and box scratch live on in the MipWarm context */
}

/* ------------------------------------------------------------------ */
/* Sound feasibility-based bound tightening (FBBT / "branch-and-clip").
 *
 * Tightens the box [lo,hi] in place using the row constraints, computing for
 * each row the minimum and maximum possible activity over the current box and
 * deriving per-variable bound updates.  It is a pre-solve that can only ever
 * tighten bounds -- never loosen them -- so the LP/MIP optimum is preserved
 * while the branch-and-bound tree is pruned.
 *
 * The previous attempt at this (remote commit 24985ad) was rejected by the
 * audit because it was UNSOUND.  This version satisfies each rejected
 * property:
 *
 *   1. NO coefficient is dropped.  The old code skipped every |a| < 1e-12,
 *      which changes the model when variable magnitudes are large.  The audit's
 *      counterexample (1e-13*x + y <= 1 with x = -1e13, whose true optimum is
 *      y = 2) is solved correctly here because the 1e-13 coefficient is kept.
 *
 *   2. OUTWARD-ROUNDED activity bounds and candidates, via DIRECTED
 *      ROUNDING (FE_DOWNWARD / FE_UPWARD regions): the "others" activity
 *      feeding a '<'/'=' bound is accumulated downward (a proven lower
 *      bound of the exact rest-minimum), the subtraction and division then
 *      round in the direction that provably cannot exclude a feasible value
 *      (up for upper candidates with a>0, down for lower ones...; '>' rows
 *      mirror with an upward rest-maximum).  The nextafter-biased
 *      round-to-nearest predecessor was NOT provably outward: at |partial|
 *      ~1e12 the RN rest-sum error (~1e-4) dwarfs a nextafter step AND the
 *      absolute 1e-9 slack, and at bound magnitudes ~1e15 even one ulp of
 *      underestimate overflows the slack -- which is how a fixed x0 near
 *      1e25 got its box collapsed into a fabricated INFEASIBLE verdict
 *      (2026-08-15(2) round; regression of record in mip_box_conflict and
 *      tools/fbbt_verify.py's cancellation family).
 *
 *   3. Integer variables are snapped to the lattice (floor for upper bounds,
 *      ceil for lower bounds) with the historical 1e-9 hysteresis slacks,
 *      which now only ever widen the interval: the directed candidate
 *      itself is already on the safe side.
 *
 * Infeasibility (return 1) is decided only by mip_box_conflict's rigorous
 * directed-rounding certificate and by genuine box collapse; the previous
 * tolerance-padded round-to-nearest prune ("min_act > rhs + 1e-6*(...)"
 * overflow-margin reasoning) was retired for fabricating UNSAT under
 * catastrophic cancellation, with a pinned .lp regression and a randomized
 * exact-referenced family in tools/fbbt_verify.py.
 *
 * A reduced-cost fixing path is deliberately NOT included: the previous
 * attempt's reduced-cost logic assumed minimization signs while the solver
 * exposes the maximization-form reduced costs, and that convention is not yet
 * documented.  Bound tightening alone is the sound, high-value subset.
 *
 * Returns 1 if the box is provably infeasible (lo[j] > hi[j]), else 0.
 */
static int fbbt_tighten(const MIP *mip, double *lo, double *hi)
{
    int n = mip->n, m = mip->m;
    if (n <= 0 || m <= 0) return 0;
    const double BIG = 1e29;
    int rc = 0;
    /* Captured once: every directed-rounding region below restores the mode
       immediately, so this value is current again after each region. */
    int old_rm = fegetround();
    /* scratch for the conflict certificate (freed on every exit below) */
    double *cmin = (double*)psolve_malloc((size_t)m * sizeof(double));
    double *cmax = (double*)psolve_malloc((size_t)m * sizeof(double));

    /* Root infeasibility is decided EXCLUSIVELY by the rigorous directed-
       rounding certificate.  The round-to-nearest, tolerance-padded prune
       this replaces ("RN min_act > rhs + 1e-6*(1+|rhs|)") fabricated UNSAT
       under catastrophic cancellation: products near 1e12 round by up to
       ~6e-5, far beyond the tolerance, so an overestimated min_act pruned
       FEASIBLE models before any LP ran -- and fbbt's caller reports that
       return as proven INFEASIBLE without any cross-check.  See
       mip_box_conflict for the regression of record. */
    if (mip_box_conflict(mip, lo, hi, cmin, cmax)) { rc = 1; goto done; }

    for (int pass = 0; pass < 4; pass++) {
        int changed = 0;
        for (int i = 0; i < m; i++) {
            char rel = mip->rel[i];
            double rhs = mip->b[i];

            /* Tighten each variable's bound from this row.  The arithmetic
               runs in DIRECTED rounding: the nextafter-biased round-to-
               nearest version this replaces was NOT provably outward at
               large magnitudes -- the rest-sum cancellation error is
               bounded by ~eps*max|partial|, which dwarfs both a single
               nextafter step and the absolute 1e-9 slack once bounds reach
               ~1e15, so the computed bound could sit below the true one and
               the box-collapse check would report a FEASIBLE model
               infeasible (the regression of record in mip_box_conflict goes
               through exactly this path).

               Derivation.  For a '<'/'=' row, x_j is feasible for the row
               iff a*x_j <= rhs - rest_min (others at their minimum), i.e.
               the candidate bound is V = (rhs - rest_min)/a.  With rest_L
               <= exact rest_min accumulated under FE_DOWNWARD, the exact
               expression E = (rhs - rest_L)/a satisfies E >= V when a > 0
               and E <= V when a < 0 -- so rounding both the subtraction and
               the division UP (a > 0) / DOWN (a < 0) yields a candidate
               that provably never excludes a feasible value.  '>' rows
               mirror this: V = (rhs - rest_max)/a with rest_R >= exact
               rest_max accumulated under FE_UPWARD, so a > 0 rounds DOWN
               and a < 0 rounds UP.  The lattice snaps (floor/ceil with the
               1e-9 hysteresis slacks) and the 1e-9 comparison hysteresis
               can only loosen further. */
            for (int j = 0; j < n; j++) {
                double a = 0.0; int found = 0;
                for (int k = mip->Acolptr[j]; k < mip->Acolptr[j + 1]; k++) {
                    if (mip->Arow[k] == i) { a = mip->Aval[k]; found = 1; break; }
                }
                if (!found || a == 0.0) continue;
                /* Only tighten INTEGER-variable bounds.  FBBT's purpose is to
                   prune the integer branch-and-bound tree, and integer bounds
                   get snapped to the lattice, so this is where the value is.
                   Tightening a float variable's bound here would perturb the
                   LP's reported vertex (e.g. a satisfy model returns mx as the
                   tightened bound 4.2500000000000009 instead of 4.25) with no
                   pruning benefit.  The conflict certificate above considers
                   every variable and stays rigorous. */
                if (!mip->isint[j]) continue;

                /* rest at the required extremum, accumulated in the directed
                   mode; an overflowed +-inf product (or a NaN from an +-inf
                   pair) makes every comparison below false: no tighten,
                   always the sound choice */
                int down_side = (rel == '<' || rel == '=');
                double rest = 0.0; int rest_inf = 0;
                fesetround(down_side ? FE_DOWNWARD : FE_UPWARD);
                for (int t = 0; t < n; t++) {
                    if (t == j) continue;
                    for (int k = mip->Acolptr[t]; k < mip->Acolptr[t + 1]; k++) {
                        if (mip->Arow[k] != i) continue;
                        double at = mip->Aval[k];
                        if (down_side) {          /* minimum-activity terms */
                            if (at > 0.0) { if (lo[t] <= -BIG) rest_inf = 1; else rest += at * lo[t]; }
                            else          { if (hi[t] >=  BIG) rest_inf = 1; else rest += at * hi[t]; }
                        } else {                  /* maximum-activity terms */
                            if (at > 0.0) { if (hi[t] >=  BIG) rest_inf = 1; else rest += at * hi[t]; }
                            else          { if (lo[t] <= -BIG) rest_inf = 1; else rest += at * lo[t]; }
                        }
                    }
                }
                if (!rest_inf) {
                    if (down_side) {
                        if (a > 0.0) {              /* upper candidate: round UP */
                            fesetround(FE_UPWARD);
                            double ub = (rhs - rest) / a;
                            if (mip->isint[j]) ub = floor(ub + 1e-9);
                            if (ub < hi[j] - 1e-9) { hi[j] = ub; changed = 1; }
                        } else {                    /* lower candidate: stay DOWN */
                            double lb = (rhs - rest) / a;
                            if (mip->isint[j]) lb = ceil(lb - 1e-9);
                            if (lb > lo[j] + 1e-9) { lo[j] = lb; changed = 1; }
                        }
                    } else {
                        if (a > 0.0) {              /* lower candidate: round DOWN */
                            fesetround(FE_DOWNWARD);
                            double lb = (rhs - rest) / a;
                            if (mip->isint[j]) lb = ceil(lb - 1e-9);
                            if (lb > lo[j] + 1e-9) { lo[j] = lb; changed = 1; }
                        } else {                    /* upper candidate: stay UP */
                            double ub = (rhs - rest) / a;
                            if (mip->isint[j]) ub = floor(ub + 1e-9);
                            if (ub < hi[j] - 1e-9) { hi[j] = ub; changed = 1; }
                        }
                    }
                }
                fesetround(old_rm);
            }
        }
        for (int j = 0; j < n; j++) if (lo[j] > hi[j] + 1e-9) { rc = 1; goto done; }
        /* A pass that tightened the box may expose a row conflict that was
           not provable on entry; re-certify cheaply (O(nnz)). */
        if (changed && mip_box_conflict(mip, lo, hi, cmin, cmax)) { rc = 1; goto done; }
        if (!changed) break;
    }
    for (int j = 0; j < n; j++) if (lo[j] > hi[j]) { rc = 1; goto done; }
done:
    psolve_free(cmin); psolve_free(cmax);
    return rc;
}

void mip_solve(const MIP *mip, MIPResult *res)
{
    if(!res)return;
    memset(res,0,sizeof(*res));res->status=MIP_INVALID;
    if(!mip||mip->n<=0||mip->m<0||mip->n>1000000||mip->m>1000000||
       !mip->c||!mip->Acolptr||!mip->l||!mip->u||!mip->isint||
       (mip->m>0&&(!mip->rel||!mip->b)))return;
    if(mip->Acolptr[0]!=0)return;
    for(int j=0;j<mip->n;j++){
        if(!isfinite(mip->c[j])||!isfinite(mip->l[j])||!isfinite(mip->u[j])||
           mip->Acolptr[j]<0||mip->Acolptr[j+1]<mip->Acolptr[j])return;
    }
    int nnz=mip->Acolptr[mip->n];
    if(nnz>0&&(!mip->Arow||!mip->Aval))return;
    for(int k=0;k<nnz;k++)
        if(mip->Arow[k]<0||mip->Arow[k]>=mip->m||!isfinite(mip->Aval[k]))return;
    for(int i=0;i<mip->m;i++)
        if(!isfinite(mip->b[i])||
           (mip->rel[i]!='<'&&mip->rel[i]!='>'&&mip->rel[i]!='='))return;
    int n = mip->n;
    double gap = mip->mip_gap > 0 ? mip->mip_gap : 1e-4;
    long node_limit = mip->node_limit > 0 ? mip->node_limit : 100000;

    memset(res, 0, sizeof(*res));
    res->x = (double*)psolve_malloc((size_t)n * sizeof(double));
    res->isint_sol = (int*)psolve_calloc((size_t)n, sizeof(int));
    double *x = (double*)psolve_malloc((size_t)n * sizeof(double));
    double *lcur = (double*)psolve_malloc((size_t)n * sizeof(double));
    double *ucur = (double*)psolve_malloc((size_t)n * sizeof(double));
    double *bestx = (double*)psolve_malloc((size_t)n * sizeof(double));
    double *xc = (double*)psolve_malloc((size_t)n * sizeof(double));

    double incumbent = mip->maximize ? -1e30 : 1e30;
    int have_incumbent = 0;
    double best_bound = mip->maximize ? -1e30 : 1e30;
    res->best_bound = mip->maximize ? 1e30 : -1e30;
    Node *stack = NULL;
    double *lo0 = (double*)psolve_malloc((size_t)n * sizeof(double));
    double *hi0 = (double*)psolve_malloc((size_t)n * sizeof(double));
    /* Presolve: an integer variable's bounds can be rounded inward to the
       lattice.  Besides tightening every relaxation, this removes fractional
       bounds, which are a trap for anything that clamps a rounded value into
       the box.  ceil/floor leave the +-LP_INF sentinels unchanged. */
    for (int j = 0; j < n; j++) {
        lo0[j] = mip->l[j]; hi0[j] = mip->u[j];
        if (mip->isint[j]) {
            if (lo0[j] > -LP_INF) lo0[j] = ceil(lo0[j] - MIP_TOL);
            if (hi0[j] <  LP_INF) hi0[j] = floor(hi0[j] + MIP_TOL);
        }
    }
    /* Sound feasibility-based bound tightening (FBBT): propagates the rows to
       clip the root box.  It only ever tightens validly (see fbbt_tighten),
       preserving the optimum while pruning the tree.  If the box is provably
       infeasible we can report it immediately without solving anything.
       FBBT is only run when there is at least one integer variable: its whole
       purpose is to prune the integer branch-and-bound tree.  On a pure-float
       model there is no lattice to prune, so tightening bounds would only
       perturb the LP's vertex (e.g. a float bound tightened to a slightly-loose
       value that the LP then snaps to), with no correctness benefit. */
    int has_int = 0;
    for (int j = 0; j < n; j++) if (mip->isint[j]) { has_int = 1; break; }
    if (has_int && fbbt_tighten(mip, lo0, hi0)) {
        /* The box is provably infeasible (bound tightening pruned every
           integer point).  Report INFEASIBLE immediately -- no solve needed --
           with a proven verdict. */
        res->status = 1;
        res->proven_optimal = 1;
        res->nodes = 0;   /* callers read nodes/best_bound unconditionally */
        psolve_free(lo0); psolve_free(hi0);
        psolve_free(x); psolve_free(lcur); psolve_free(ucur); psolve_free(bestx); psolve_free(xc);
        return;
    }
    Node *root = (Node*)psolve_malloc(sizeof(Node));
    root->lo = lo0; root->hi = hi0; root->bound = 0.0; root->feasible = 0;
    root->next = NULL;
    push_node(&stack, root, mip->maximize);

    /* persistent warm-start context for the per-node relaxations; created
       only after the root-FBBT early-return so every exit below passes the
       shared teardown */
    MipWarm ws; memset(&ws, 0, sizeof(ws));
    ws.blo = (double*)psolve_malloc((size_t)n * sizeof(double));
    ws.bhi = (double*)psolve_malloc((size_t)n * sizeof(double));
    ws.cmin = (double*)psolve_malloc((size_t)(mip->m ? mip->m : 1) * sizeof(double));
    ws.cmax = (double*)psolve_malloc((size_t)(mip->m ? mip->m : 1) * sizeof(double));
    ws.fy = (double*)psolve_malloc((size_t)(mip->m ? mip->m : 1) * sizeof(double));
    ws.fyc = (double*)psolve_malloc((size_t)(mip->m ? mip->m : 1) * sizeof(double));
    ws.fzlo = (double*)psolve_malloc((size_t)n * sizeof(double));
    ws.fzhi = (double*)psolve_malloc((size_t)n * sizeof(double));

    long nodes = 0;
    int status = 1;   /* assume infeasible until a feasible integer found */
    int limit_reached = 0;   /* the search was cut short (node/time/stop/iter) */
    int stopped_early = 0;   /* stop_at_feasible fired: feasible, not optimal */

    while (stack) {
        if (nodes >= node_limit) { status = 3; limit_reached = 1; break; }
        if (psolve_stop()) { status = 4; limit_reached = 1; break; }
        Node *node = pop_node(&stack);
        nodes++;

        /* NOTE (measured, 2026-08-14, then reverted): running fbbt_tighten on
           every popped node box did NOT pay for itself on the benchmark
           suite -- assignment 19->17 nodes, knap_lin flat, but tsp_5 grew
           255->303 nodes and +25% wall (tighter boxes shift the LP vertex
           and thereby the most-fractional branching picks adversarially;
           the per-node fixpoint cost is not recovered).  Root-only FBBT
           stays: it is where the sound tightening wins. */

        double obj;
        int r = solve_relaxation(mip, node, &ws, x, &obj, lcur, ucur);
        if (r == -1) { psolve_free(node->lo); psolve_free(node->hi); psolve_free(node); continue; } /* infeasible */
        if (r == 3) { psolve_free(node->lo); psolve_free(node->hi); psolve_free(node); status = 3; limit_reached = 1; break; } /* lp limit */
        if (r == SOLVE_STOPPED) { psolve_free(node->lo); psolve_free(node->hi); psolve_free(node); status = 4; limit_reached = 1; break; } /* stopped */
        if (r == SOLVE_NUMERICAL) { psolve_free(node->lo); psolve_free(node->hi); psolve_free(node); status = 6; limit_reached = 1; break; } /* numerical */
        if (r == SOLVE_INVALID) { psolve_free(node->lo); psolve_free(node->hi); psolve_free(node); status = MIP_INVALID; limit_reached = 1; break; }
        /* infeasible relaxation */
        if (r == 1) { psolve_free(node->lo); psolve_free(node->hi); psolve_free(node); continue; }
        if (r == 2) {
            /* An unbounded relaxation on the ROOT node (original bounds) means
               the MIP itself is unbounded.  Branching only tightens bounds, so
               an unbounded non-root relaxation cannot happen when the root was
               bounded; if it does (numerical trouble), treat the node as
               infeasible rather than guess. */
            if (nodes == 1) { psolve_free(node->lo); psolve_free(node->hi); psolve_free(node); status = 2; break; }
            psolve_free(node->lo); psolve_free(node->hi); psolve_free(node); continue;
        }

        /* prune by bound */
        /* track best bound over solved (not pruned) nodes */
        if (mip->maximize) { if (obj > best_bound) best_bound = obj; }
        else { if (obj < best_bound) best_bound = obj; }
        if (have_incumbent && !mip->stop_at_feasible) {
            if (mip->maximize && obj <= incumbent + gap * (1.0 + fabs(incumbent))) { psolve_free(node->lo); psolve_free(node->hi); psolve_free(node); continue; }
            if (!mip->maximize && obj >= incumbent - gap * (1.0 + fabs(incumbent))) { psolve_free(node->lo); psolve_free(node->hi); psolve_free(node); continue; }
        }

        /* LP-rounding feasibility heuristic: if the relaxation is already
           integral after rounding, grab it as an incumbent immediately
           (fast path, especially for solve satisfy). */
        if (!mip->all_solutions && try_rounding_heuristic(mip, x, lcur, ucur, xc)) {
            double objr = 0.0;
            for (int j = 0; j < n; j++) objr += mip->c[j] * xc[j];
            if (!have_incumbent ||
                (mip->maximize && objr > incumbent) ||
                (!mip->maximize && objr < incumbent)) {
                have_incumbent = 1;
                incumbent = objr;
                memcpy(bestx, xc, (size_t)n * sizeof(double));
                for (int j = 0; j < n; j++) res->isint_sol[j] = mip->isint[j];
            }
        }
        if (mip->stop_at_feasible && have_incumbent && !mip->all_solutions) {
            psolve_free(node->lo); psolve_free(node->hi); psolve_free(node);
            stopped_early = 1;
            break;
        }

        /* check integrality; pick the most fractional integer variable (fraction
           closest to 0.5) for branching.  On weak relaxations (the flat
           fractional assignments of combinatorial models) branching on the
           variable nearest the midpoint prunes far more effectively than taking
           the first fractional variable.  (Convergent change on both branches.) */
        int frac = -1; double fracval = 0.0;
        int allint = 1;
        double best_dist = 2.0;
        for (int j = 0; j < n; j++) {
            if (!mip->isint[j]) continue;
            int out_of_bounds = (x[j] < lcur[j] - MIP_TOL || x[j] > ucur[j] + MIP_TOL);
            double xj = x[j];
            double f = out_of_bounds ? 0.0 : fabs(xj - floor(xj + 0.5));
            if (out_of_bounds || f > MIP_TOL) {
                allint = 0;
                double dist = out_of_bounds ? 0.0 : fabs(f - 0.5);
                if (dist < best_dist) { best_dist = dist; frac = j; fracval = xj; }
            }
        }
        if (!allint && frac == -1) allint = 1;   /* fallback; should not happen */

        if (allint) {
            /* In all-solutions satisfaction mode, if any integer variable is
               not yet fixed to a single value, branch on it to enumerate all
               lattice points rather than stopping at the first point. */
            if (mip->all_solutions && mip->stop_at_feasible) {
                int unfixed = -1;
                for (int j = 0; j < n; j++) {
                    if (mip->isint[j] && (ucur[j] - lcur[j] > 0.5)) {
                        unfixed = j; break;
                    }
                }
                if (unfixed >= 0) {
                    double v = floor(x[unfixed] + 0.5);
                    if (v < lcur[unfixed]) v = lcur[unfixed];
                    if (v >= ucur[unfixed]) v = ucur[unfixed] - 1.0;

                    Node *c1 = (Node*)psolve_malloc(sizeof(Node));
                    c1->lo = (double*)psolve_malloc((size_t)n * sizeof(double));
                    c1->hi = (double*)psolve_malloc((size_t)n * sizeof(double));
                    for (int j = 0; j < n; j++) { c1->lo[j] = lcur[j]; c1->hi[j] = ucur[j]; }
                    c1->hi[unfixed] = v;
                    c1->bound = obj; c1->feasible = 0; c1->next = NULL;
                    push_node(&stack, c1, mip->maximize);

                    Node *c2 = (Node*)psolve_malloc(sizeof(Node));
                    c2->lo = (double*)psolve_malloc((size_t)n * sizeof(double));
                    c2->hi = (double*)psolve_malloc((size_t)n * sizeof(double));
                    for (int j = 0; j < n; j++) { c2->lo[j] = lcur[j]; c2->hi[j] = ucur[j]; }
                    c2->lo[unfixed] = v + 1.0;
                    c2->bound = obj; c2->feasible = 0; c2->next = NULL;
                    push_node(&stack, c2, mip->maximize);

                    psolve_free(node->lo); psolve_free(node->hi); psolve_free(node);
                    continue;
                }
            }

            /* Integer-feasible.  Snap the integer components to the lattice:
               the LP returns them within MIP_TOL of an integer, and callers
               must never see 2.9999999997 for a variable declared integer. */
            for (int j = 0; j < n; j++)
                if (mip->isint[j]) x[j] = floor(x[j] + 0.5);
            /* ...and report the objective OF THE POINT WE RETURN, so
               res->obj == c . res->x exactly rather than to within the
               integrality tolerance. */
            obj = 0.0;
            for (int j = 0; j < n; j++) obj += mip->c[j] * x[j];
            if (!have_incumbent ||
                (mip->maximize && obj > incumbent) ||
                (!mip->maximize && obj < incumbent)) {
                incumbent = obj;
                memcpy(bestx, x, (size_t)n * sizeof(double));
                have_incumbent = 1;
                for (int j = 0; j < n; j++) res->isint_sol[j] = mip->isint[j];
                if (mip->all_solutions && mip->on_solution)
                    mip->on_solution(x, obj, mip->solution_user_data);
                if (mip->stop_at_feasible && !mip->all_solutions) {
                    /* solve satisfy: the first feasible integer point is an
                       answer; stop branching instead of proving optimality. */
                    psolve_free(node->lo); psolve_free(node->hi); psolve_free(node);
                    stopped_early = 1;
                    break;
                }
            } else if (mip->all_solutions && mip->stop_at_feasible && mip->on_solution) {
                mip->on_solution(x, obj, mip->solution_user_data);
            }
            psolve_free(node->lo); psolve_free(node->hi); psolve_free(node);
            continue;
        }

        /* branch on the fractional variable */
        double fdown = floor(fracval);
        double fup   = ceil(fracval);

        /* child 1: x[frac] <= fdown */
        Node *c1 = (Node*)psolve_malloc(sizeof(Node));
        c1->lo = (double*)psolve_malloc((size_t)n * sizeof(double));
        c1->hi = (double*)psolve_malloc((size_t)n * sizeof(double));
        for (int j = 0; j < n; j++) { c1->lo[j] = lcur[j]; c1->hi[j] = ucur[j]; }
        c1->hi[frac] = fdown;
        c1->bound = obj; c1->feasible = 0; c1->next = NULL;
        push_node(&stack, c1, mip->maximize);

        /* child 2: x[frac] >= fup */
        Node *c2 = (Node*)psolve_malloc(sizeof(Node));
        c2->lo = (double*)psolve_malloc((size_t)n * sizeof(double));
        c2->hi = (double*)psolve_malloc((size_t)n * sizeof(double));
        for (int j = 0; j < n; j++) { c2->lo[j] = lcur[j]; c2->hi[j] = ucur[j]; }
        c2->lo[frac] = fup;
        c2->bound = obj; c2->feasible = 0; c2->next = NULL;
        push_node(&stack, c2, mip->maximize);

        psolve_free(node->lo); psolve_free(node->hi); psolve_free(node);
    }

    res->nodes = nodes;
    res->best_bound = best_bound;
    res->farkas_certs = ws.farkas_certs;
    res->fx_solves = ws.fx_solves;
    /* `obj` is a proven optimum only if nothing cut the search short: no node
       or iteration limit, no cooperative stop, and no stop_at_feasible. */
    res->proven_optimal = (!limit_reached && !stopped_early && have_incumbent);
    if (have_incumbent) {
        res->obj = incumbent;
        memcpy(res->x, bestx, (size_t)n * sizeof(double));
        if (!limit_reached) {
            /* the tree was exhausted and the incumbent is optimal */
            status = 0;
        } else {
            /* search cut short: the incumbent is FEASIBLE but NOT proven
               optimal.  Never report OPTIMAL for a truncated search. */
            status = 5;
        }
    }
    res->status = status;

    /* free remaining nodes */
    Node *n2 = stack;
    while (n2) { Node *t = n2; n2 = n2->next; psolve_free(t->lo); psolve_free(t->hi); psolve_free(t); }

    if (ws.s) solver_destroy(ws.s);
    psolve_free(ws.blo); psolve_free(ws.bhi);
    psolve_free(ws.cmin); psolve_free(ws.cmax);
    psolve_free(ws.fy); psolve_free(ws.fyc);
    psolve_free(ws.fzlo); psolve_free(ws.fzhi);
    psolve_free(x); psolve_free(lcur); psolve_free(ucur); psolve_free(bestx); psolve_free(xc);
}

void mip_result_free(MIPResult *res)
{
    if (!res) return;
    psolve_free(res->x);
    psolve_free(res->isint_sol);
    memset(res, 0, sizeof(*res));
}
