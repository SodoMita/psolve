#include "mip.h"
#include "fx.h"
#include "err.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

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
    flp->c = (Fx*)calloc((size_t)n, sizeof(Fx));
    flp->b = (Fx*)calloc((size_t)(m ? m : 1), sizeof(Fx));
    flp->l = (Fx*)calloc((size_t)n, sizeof(Fx));
    flp->u = (Fx*)calloc((size_t)n, sizeof(Fx));
    flp->lfinite = (int*)calloc((size_t)n, sizeof(int));
    flp->ufinite = (int*)calloc((size_t)n, sizeof(int));
    flp->rel = (char*)malloc((size_t)(m ? m : 1));
    flp->A = (Fx*)calloc(cells, sizeof(Fx));
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
    free(rs);
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

/* build an LP from the MIP with per-node tightened bounds, and solve it */
static int solve_relaxation(const MIP *mip, const Node *node,
                            double *x, double *obj, double *lcur, double *ucur)
{
    int n = mip->n;
    LP lp;
    memset(&lp, 0, sizeof(lp));
    lp.n = n; lp.m = mip->m; lp.maximize = mip->maximize;
    lp.c  = (double*)mip->c;
    lp.Acolptr = (int*)mip->Acolptr;
    lp.Arow = (int*)mip->Arow;
    lp.Aval = (double*)mip->Aval;
    lp.rel = (char*)mip->rel;
    lp.b  = (double*)mip->b;
    lp.l  = (double*)mip->l;
    lp.u  = (double*)mip->u;
    /* per-node bounds (allocate copies so node can free them) */
    double *lo = (double*)psolve_malloc((size_t)n * sizeof(double));
    double *hi = (double*)psolve_malloc((size_t)n * sizeof(double));
    for (int j = 0; j < n; j++) {
        double lj = node->lo[j] > mip->l[j] ? node->lo[j] : mip->l[j];
        double uj = node->hi[j] < mip->u[j] ? node->hi[j] : mip->u[j];
        if (lj > uj) { free(lo); free(hi); return -1; }   /* infeasible node */
        lo[j] = lj; hi[j] = uj;
    }
    lp.l = lo; lp.u = hi;

    Solver *s = solver_create(&lp);
    if (!s) { free(lo); free(hi); return -1; }
    if (mip->lp_iter_limit > 0) s->iteration_limit = mip->lp_iter_limit;
    int r = solver_solve(s);
    int status = r;
    if (r == 0) {
        double *xo = (double*)psolve_malloc((size_t)n * sizeof(double));
        solver_optimum(s, xo, obj);
        for (int j = 0; j < n; j++) x[j] = xo[j];
        free(xo);
    } else if (r == SOLVE_NUMERICAL || r == 1) {
        /* The double revised-simplex either diverged (SOLVE_NUMERICAL, its
           solution certificate failed) or declared the relaxation INFEASIBLE.
           On the ill-conditioned big-M bases of combinatorial MIPs
           (table/circuit/cumulative) both are possible even when the
           relaxation is feasible.  Cross-check the SAME relaxation exactly
           with the fixed-point rational simplex, which is immune to double
           rounding.  If the data are integral and the exact solve succeeds we
           trust its verdict; otherwise we keep the double solver's honest
           result. */
        FxLP flp; memset(&flp, 0, sizeof(flp));
        if (mip_build_fxlp(mip, lo, hi, &flp) == 0) {
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
    }
    for (int j = 0; j < n; j++) { lcur[j] = lo[j]; ucur[j] = hi[j]; }
    solver_destroy(s);
    free(lo); free(hi);
    return status;
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
 *   2. OUTWARD-ROUNDED activity bounds.  Every computed bound is rounded
 *      toward the side that can only WIDEN the remaining feasible region:
 *      upper bounds round up (nextafter toward +inf), lower bounds round down,
 *      and the intermediate "others" activity sums are rounded conservatively
 *      (the sum feeding an upper bound is biased small, the sum feeding a
 *      lower bound is biased large).  A tightened bound therefore never cuts
 *      off a value the constraint actually allows.
 *
 *   3. Integer variables are snapped to the lattice (floor for upper bounds,
 *      ceil for lower bounds).  An integer x with x <= UB satisfies
 *      x <= floor(UB), so this is exact and safe.
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

    for (int pass = 0; pass < 4; pass++) {
        int changed = 0;
        for (int i = 0; i < m; i++) {
            char rel = mip->rel[i];
            double rhs = mip->b[i];
            /* Full min/max activity of the row over the current box. */
            double min_act = 0.0, max_act = 0.0;
            int min_inf = 0, max_inf = 0;
            for (int j = 0; j < n; j++) {
                for (int k = mip->Acolptr[j]; k < mip->Acolptr[j + 1]; k++) {
                    if (mip->Arow[k] != i) continue;
                    double a = mip->Aval[k];
                    if (a > 0.0) {
                        if (lo[j] <= -BIG) min_inf = 1; else min_act += a * lo[j];
                        if (hi[j] >=  BIG) max_inf = 1; else max_act += a * hi[j];
                    } else {
                        if (hi[j] >=  BIG) min_inf = 1; else min_act += a * hi[j];
                        if (lo[j] <= -BIG) max_inf = 1; else max_act += a * lo[j];
                    }
                }
            }
            /* Infeasibility pruning: even the best case violates the row. */
            double rhst = MIP_TOL * (1.0 + fabs(rhs));
            if ((rel == '<' || rel == '=') && !min_inf && min_act > rhs + rhst) return 1;
            if ((rel == '>' || rel == '=') && !max_inf && max_act < rhs - rhst) return 1;

            /* Tighten each variable's bound from this row. */
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
                   pruning benefit.  The infeasibility check above still
                   considers every variable and stays sound. */
                if (!mip->isint[j]) continue;

                /* Activity of all OTHER variables, computed conservatively:
                 * other_min is biased small, other_max is biased large. */
                double other_min = 0.0, other_max = 0.0;
                int other_min_inf = 0, other_max_inf = 0;
                for (int t = 0; t < n; t++) {
                    if (t == j) continue;
                    for (int k = mip->Acolptr[t]; k < mip->Acolptr[t + 1]; k++) {
                        if (mip->Arow[k] != i) continue;
                        double at = mip->Aval[k];
                        if (at > 0.0) {
                            if (lo[t] <= -BIG) other_min_inf = 1; else other_min += at * lo[t];
                            if (hi[t] >=  BIG) other_max_inf = 1; else other_max += at * hi[t];
                        } else {
                            if (hi[t] >=  BIG) other_min_inf = 1; else other_min += at * hi[t];
                            if (lo[t] <= -BIG) other_max_inf = 1; else other_max += at * lo[t];
                        }
                    }
                }
                /* A variable x_j is feasible for this row iff there EXISTS an
                 * assignment of the other variables satisfying it.  For a
                 * '<'/'=' row (a*x_j + rest <= rhs) that existence condition is
                 *   a*x_j <= rhs - rest_min
                 * (others at their minimum make the LHS smallest), so BOTH the
                 * upper bound (a>0) and the lower bound (a<0) come from
                 * rest_min.  For a '>' row (a*x_j + rest >= rhs) the condition
                 * is   a*x_j >= rhs - rest_max  (others at their maximum), so
                 * both bounds come from rest_max.  Using the wrong extremum is
                 * exactly the unsoundness the audit flagged, so it matters that
                 * '<'/'=' always uses rest_min and '>' always uses rest_max. */
                /* Outward-bias the "others" activity before it feeds a bound:
                 * a '<'/'=' row divides by (rhs - rest_min), so biasing rest_min
                 * DOWN (more negative) only loosens the bound; a '>' row divides
                 * by (rhs - rest_max), so biasing rest_max UP only loosens it.
                 * A loosened bound never excludes a feasible value. */
                if (rel == '<' || rel == '=') {
                    if (!other_min_inf) {
                        other_min = nextafter(other_min, -HUGE_VAL);
                        /* a*x_j <= rhs - rest_min */
                        double v = (rhs - other_min) / a;
                        if (a > 0.0) {
                            /* x_j <= (rhs-rest_min)/a : round up (outward) */
                            double ub = nextafter(v, HUGE_VAL);
                            if (mip->isint[j]) ub = floor(ub + 1e-9);
                            if (ub < hi[j] - 1e-9) { hi[j] = ub; changed = 1; }
                        } else {
                            /* a<0 flips: x_j >= (rhs-rest_min)/a : round down */
                            double lb = nextafter(v, -HUGE_VAL);
                            if (mip->isint[j]) lb = ceil(lb - 1e-9);
                            if (lb > lo[j] + 1e-9) { lo[j] = lb; changed = 1; }
                        }
                    }
                } else {                                 /* '>' */
                    if (!other_max_inf) {
                        other_max = nextafter(other_max, HUGE_VAL);
                        /* a*x_j >= rhs - rest_max */
                        double v = (rhs - other_max) / a;
                        if (a > 0.0) {
                            /* x_j >= (rhs-rest_max)/a : round down (outward) */
                            double lb = nextafter(v, -HUGE_VAL);
                            if (mip->isint[j]) lb = ceil(lb - 1e-9);
                            if (lb > lo[j] + 1e-9) { lo[j] = lb; changed = 1; }
                        } else {
                            /* a<0 flips: x_j <= (rhs-rest_max)/a : round up */
                            double ub = nextafter(v, HUGE_VAL);
                            if (mip->isint[j]) ub = floor(ub + 1e-9);
                            if (ub < hi[j] - 1e-9) { hi[j] = ub; changed = 1; }
                        }
                    }
                }
            }
        }
        for (int j = 0; j < n; j++) if (lo[j] > hi[j] + 1e-9) return 1;
        if (!changed) break;
    }
    for (int j = 0; j < n; j++) if (lo[j] > hi[j]) return 1;
    return 0;
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
        free(lo0); free(hi0);
        free(x); free(lcur); free(ucur); free(bestx); free(xc);
        return;
    }
    Node *root = (Node*)psolve_malloc(sizeof(Node));
    root->lo = lo0; root->hi = hi0; root->bound = 0.0; root->feasible = 0;
    root->next = NULL;
    push_node(&stack, root, mip->maximize);

    long nodes = 0;
    int status = 1;   /* assume infeasible until a feasible integer found */
    int limit_reached = 0;   /* the search was cut short (node/time/stop/iter) */
    int stopped_early = 0;   /* stop_at_feasible fired: feasible, not optimal */

    while (stack) {
        if (nodes >= node_limit) { status = 3; limit_reached = 1; break; }
        if (psolve_stop()) { status = 4; limit_reached = 1; break; }
        Node *node = pop_node(&stack);
        nodes++;

        double obj;
        int r = solve_relaxation(mip, node, x, &obj, lcur, ucur);
        if (r == -1) { free(node->lo); free(node->hi); free(node); continue; } /* infeasible */
        if (r == 3) { free(node->lo); free(node->hi); free(node); status = 3; limit_reached = 1; break; } /* lp limit */
        if (r == SOLVE_STOPPED) { free(node->lo); free(node->hi); free(node); status = 4; limit_reached = 1; break; } /* stopped */
        if (r == SOLVE_NUMERICAL) { free(node->lo); free(node->hi); free(node); status = 6; limit_reached = 1; break; } /* numerical */
        if (r == SOLVE_INVALID) { free(node->lo); free(node->hi); free(node); status = MIP_INVALID; limit_reached = 1; break; }
        /* infeasible relaxation */
        if (r == 1) { free(node->lo); free(node->hi); free(node); continue; }
        if (r == 2) {
            /* An unbounded relaxation on the ROOT node (original bounds) means
               the MIP itself is unbounded.  Branching only tightens bounds, so
               an unbounded non-root relaxation cannot happen when the root was
               bounded; if it does (numerical trouble), treat the node as
               infeasible rather than guess. */
            if (nodes == 1) { free(node->lo); free(node->hi); free(node); status = 2; break; }
            free(node->lo); free(node->hi); free(node); continue;
        }

        /* prune by bound */
        /* track best bound over solved (not pruned) nodes */
        if (mip->maximize) { if (obj > best_bound) best_bound = obj; }
        else { if (obj < best_bound) best_bound = obj; }
        if (have_incumbent && !mip->stop_at_feasible) {
            if (mip->maximize && obj <= incumbent + gap * (1.0 + fabs(incumbent))) { free(node->lo); free(node->hi); free(node); continue; }
            if (!mip->maximize && obj >= incumbent - gap * (1.0 + fabs(incumbent))) { free(node->lo); free(node->hi); free(node); continue; }
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
            free(node->lo); free(node->hi); free(node);
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

                    free(node->lo); free(node->hi); free(node);
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
                    free(node->lo); free(node->hi); free(node);
                    stopped_early = 1;
                    break;
                }
            } else if (mip->all_solutions && mip->stop_at_feasible && mip->on_solution) {
                mip->on_solution(x, obj, mip->solution_user_data);
            }
            free(node->lo); free(node->hi); free(node);
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

        free(node->lo); free(node->hi); free(node);
    }

    res->nodes = nodes;
    res->best_bound = best_bound;
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
    while (n2) { Node *t = n2; n2 = n2->next; free(t->lo); free(t->hi); free(t); }

    free(x); free(lcur); free(ucur); free(bestx); free(xc);
}

void mip_result_free(MIPResult *res)
{
    if (!res) return;
    free(res->x);
    free(res->isint_sol);
    memset(res, 0, sizeof(*res));
}
