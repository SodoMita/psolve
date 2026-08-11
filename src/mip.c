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
        if (lj > uj) { psolve_free(lo); psolve_free(hi); return -1; }   /* infeasible node */
        lo[j] = lj; hi[j] = uj;
    }
    lp.l = lo; lp.u = hi;

    Solver *s = solver_create(&lp);
    if (!s) { psolve_free(lo); psolve_free(hi); return -1; }
    if (mip->lp_iter_limit > 0) s->iteration_limit = mip->lp_iter_limit;
    int r = solver_solve(s);
    int status = r;
    if (r == 0) {
        double *xo = (double*)psolve_malloc((size_t)n * sizeof(double));
        solver_optimum(s, xo, obj);
        for (int j = 0; j < n; j++) x[j] = xo[j];
        psolve_free(xo);
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
    psolve_free(lo); psolve_free(hi);
    return status;
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

        /* check integrality; find a fractional integer variable */
        int frac = -1; double fracval = 0.0;
        int allint = 1;
        for (int j = 0; j < n; j++) {
            if (!mip->isint[j]) continue;
            if (x[j] < lcur[j] - MIP_TOL || x[j] > ucur[j] + MIP_TOL) { allint = 0; frac = j; fracval = x[j]; break; }
            double xj = x[j];
            if (fabs(xj - floor(xj + 0.5)) > MIP_TOL) {
                allint = 0; frac = j; fracval = xj; break;
            }
        }

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

    psolve_free(x); psolve_free(lcur); psolve_free(ucur); psolve_free(bestx); psolve_free(xc);
}

void mip_result_free(MIPResult *res)
{
    if (!res) return;
    psolve_free(res->x);
    psolve_free(res->isint_sol);
    memset(res, 0, sizeof(*res));
}
