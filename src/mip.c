#include "mip.h"
#include "err.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#define MIP_TOL 1e-6

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
    }
    for (int j = 0; j < n; j++) { lcur[j] = lo[j]; ucur[j] = hi[j]; }
    solver_destroy(s);
    free(lo); free(hi);
    return status;
}

void mip_solve(const MIP *mip, MIPResult *res)
{
    int n = mip->n;
    double gap = mip->mip_gap > 0 ? mip->mip_gap : 1e-4;
    long node_limit = mip->node_limit > 0 ? mip->node_limit : 100000;

    memset(res, 0, sizeof(*res));
    res->x = (double*)psolve_malloc((size_t)n * sizeof(double));
    res->isint_sol = (int*)calloc((size_t)n, sizeof(int));
    double *x = (double*)psolve_malloc((size_t)n * sizeof(double));
    double *lcur = (double*)psolve_malloc((size_t)n * sizeof(double));
    double *ucur = (double*)psolve_malloc((size_t)n * sizeof(double));
    double *bestx = (double*)psolve_malloc((size_t)n * sizeof(double));

    double incumbent = mip->maximize ? -1e30 : 1e30;
    int have_incumbent = 0;
    Node *stack = NULL;
    double *lo0 = (double*)psolve_malloc((size_t)n * sizeof(double));
    double *hi0 = (double*)psolve_malloc((size_t)n * sizeof(double));
    for (int j = 0; j < n; j++) { lo0[j] = mip->l[j]; hi0[j] = mip->u[j]; }
    Node *root = (Node*)psolve_malloc(sizeof(Node));
    root->lo = lo0; root->hi = hi0; root->bound = 0.0; root->feasible = 0;
    root->next = NULL;
    push_node(&stack, root, mip->maximize);

    long nodes = 0;
    int status = 1;   /* assume infeasible until a feasible integer found */

    while (stack) {
        if (nodes >= node_limit) { status = 3; break; }
        Node *node = pop_node(&stack);
        nodes++;

        double obj;
        int r = solve_relaxation(mip, node, x, &obj, lcur, ucur);
        if (r == -1) { free(node->lo); free(node->hi); free(node); continue; } /* infeasible */
        if (r == 3) { free(node->lo); free(node->hi); free(node); status = 3; break; } /* lp limit */
        /* infeasible or unbounded relaxation */
        if (r == 1) { free(node->lo); free(node->hi); free(node); continue; }
        if (r == 2) {
            /* unbounded relaxation: only a problem if it also happens on the
               whole space (already checked elsewhere); treat as infeasible */
            free(node->lo); free(node->hi); free(node); continue;
        }

        /* prune by bound */
        if (have_incumbent) {
            if (mip->maximize && obj <= incumbent + gap * (1.0 + fabs(incumbent))) { free(node->lo); free(node->hi); free(node); continue; }
            if (!mip->maximize && obj >= incumbent - gap * (1.0 + fabs(incumbent))) { free(node->lo); free(node->hi); free(node); continue; }
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
            /* integer-feasible: update incumbent */
                if (!have_incumbent ||
                (mip->maximize && obj > incumbent) ||
                (!mip->maximize && obj < incumbent)) {
                incumbent = obj;
                memcpy(bestx, x, (size_t)n * sizeof(double));
                have_incumbent = 1;
                for (int j = 0; j < n; j++) res->isint_sol[j] = mip->isint[j];
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
    if (have_incumbent) {
        status = 0;
        res->obj = incumbent;
        memcpy(res->x, bestx, (size_t)n * sizeof(double));
    }
    res->status = status;

    /* free remaining nodes */
    Node *n2 = stack;
    while (n2) { Node *t = n2; n2 = n2->next; free(t->lo); free(t->hi); free(t); }

    free(x); free(lcur); free(ucur); free(bestx);
}

void mip_result_free(MIPResult *res)
{
    if (!res) return;
    free(res->x);
    free(res->isint_sol);
    memset(res, 0, sizeof(*res));
}
