/* Regression tests for fully free LP/MIP variables.
 *
 * A free caller variable x is normalized internally to x+ - x-, while the
 * public API must continue to expose one x value and the original dimensions.
 */
#include "solver.h"
#include "mip.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EPS 1e-7

static void lp_free_local(LP *p)
{
    free(p->c); free(p->l); free(p->u); free(p->b); free(p->rel);
    free(p->Acolptr); free(p->Arow); free(p->Aval);
    memset(p, 0, sizeof(*p));
}

static LP one_free_box_lp(void)
{
    /* max x, with -3 <= x <= 4 expressed solely as rows. */
    LP p; memset(&p, 0, sizeof(p));
    p.n = 1; p.m = 2; p.maximize = 1;
    p.c = (double*)malloc(sizeof(double)); p.c[0] = 1.0;
    p.l = (double*)malloc(sizeof(double)); p.u = (double*)malloc(sizeof(double));
    p.l[0] = -LP_INF; p.u[0] = LP_INF;
    p.b = (double*)malloc(2 * sizeof(double)); p.b[0] = 4.0; p.b[1] = -3.0;
    p.rel = (char*)malloc(2); p.rel[0] = '<'; p.rel[1] = '>';
    p.Acolptr = (int*)malloc(2 * sizeof(int)); p.Acolptr[0] = 0; p.Acolptr[1] = 2;
    p.Arow = (int*)malloc(2 * sizeof(int)); p.Arow[0] = 0; p.Arow[1] = 1;
    p.Aval = (double*)malloc(2 * sizeof(double)); p.Aval[0] = 1.0; p.Aval[1] = 1.0;
    return p;
}

static int get1(Solver *s, double *x, double *obj)
{
    solver_optimum(s, x, obj);
    return s->status_out;
}

static int test_lp_and_incremental(void)
{
    int bad = 0;
    LP p = one_free_box_lp();
    Solver *s = solver_create(&p);
    int r = solver_solve(s);
    double x, obj;
    get1(s, &x, &obj);
    if (r != 0 || s->n_orig != 1 || s->n_core != 2 ||
        fabs(x - 4.0) > EPS || fabs(obj - 4.0) > EPS || !solver_feasible(s)) {
        printf("free LP initial FAIL: r=%d n=%d core=%d x=%g obj=%g\n",
               r, s->n_orig, s->n_core, x, obj);
        bad++;
    }

    /* Objective update must change both split-component costs. */
    double negc = -1.0;
    solver_set_objective(s, &negc, 1);
    r = solver_warm_solve(s);
    get1(s, &x, &obj);
    if (r != 0 || fabs(x + 3.0) > EPS || fabs(obj - 3.0) > EPS) {
        printf("free LP objective warm-start FAIL: r=%d x=%g obj=%g\n", r, x, obj);
        bad++;
    }

    /* The native minimization convention must also reconstruct the original
       free value and objective sign. */
    double posc = 1.0;
    solver_set_objective(s, &posc, 0);
    r = solver_warm_solve(s);
    get1(s, &x, &obj);
    if (r != 0 || fabs(x + 3.0) > EPS || fabs(obj + 3.0) > EPS) {
        printf("free LP minimization FAIL: r=%d x=%g obj=%g\n", r, x, obj);
        bad++;
    }

    /* Free -> lower-bounded changes the normalization topology and must trigger
       a clean rebuild on warm solve. */
    double lo = 0.0, hi = LP_INF;
    solver_set_objective(s, &posc, 1);
    solver_set_bounds(s, &lo, &hi);
    r = solver_warm_solve(s);
    get1(s, &x, &obj);
    if (r != 0 || s->n_core != 1 || fabs(x - 4.0) > EPS || fabs(obj - 4.0) > EPS) {
        printf("free->bounded rebuild FAIL: r=%d core=%d x=%g obj=%g\n", r, s->n_core, x, obj);
        bad++;
    }

    /* And bounded -> free must rebuild back to the two-column form. */
    lo = -LP_INF; hi = LP_INF;
    solver_set_bounds(s, &lo, &hi);
    r = solver_warm_solve(s);
    get1(s, &x, &obj);
    if (r != 0 || s->n_core != 2 || fabs(x - 4.0) > EPS || fabs(obj - 4.0) > EPS) {
        printf("bounded->free rebuild FAIL: r=%d core=%d x=%g obj=%g\n", r, s->n_core, x, obj);
        bad++;
    }

    /* Row addition exports the original free-variable column, not an internal
       split component. */
    double a = 1.0;
    r = solver_add_row(s, &a, 2.0, '<');
    get1(s, &x, &obj);
    if (r != 0 || fabs(x - 2.0) > EPS || fabs(obj - 2.0) > EPS) {
        printf("free LP add-row FAIL: r=%d x=%g obj=%g\n", r, x, obj);
        bad++;
    }

    solver_destroy(s);
    lp_free_local(&p);
    return bad;
}

static int test_negative_solution_and_unbounded(void)
{
    int bad = 0;
    /* x is free, y is boxed, x+y=-2.  This requires the negative split
       component of x to carry value 2 at the optimum. */
    LP p; memset(&p, 0, sizeof(p));
    p.n = 2; p.m = 1; p.maximize = 1;
    p.c = (double*)malloc(2 * sizeof(double)); p.c[0] = 1.0; p.c[1] = 0.0;
    p.l = (double*)malloc(2 * sizeof(double)); p.u = (double*)malloc(2 * sizeof(double));
    p.l[0] = -LP_INF; p.u[0] = LP_INF; p.l[1] = 0.0; p.u[1] = 3.0;
    p.b = (double*)malloc(sizeof(double)); p.b[0] = -2.0;
    p.rel = (char*)malloc(1); p.rel[0] = '=';
    p.Acolptr = (int*)malloc(3 * sizeof(int)); p.Acolptr[0] = 0; p.Acolptr[1] = 1; p.Acolptr[2] = 2;
    p.Arow = (int*)malloc(2 * sizeof(int)); p.Arow[0] = 0; p.Arow[1] = 0;
    p.Aval = (double*)malloc(2 * sizeof(double)); p.Aval[0] = 1.0; p.Aval[1] = 1.0;
    Solver *s = solver_create(&p);
    int r = solver_solve(s);
    double x[2], obj;
    solver_optimum(s, x, &obj);
    if (r != 0 || fabs(x[0] + 2.0) > EPS || fabs(x[1]) > EPS || fabs(obj + 2.0) > EPS) {
        printf("negative free solution FAIL: r=%d x=[%g %g] obj=%g\n", r, x[0], x[1], obj);
        bad++;
    }
    solver_destroy(s); lp_free_local(&p);

    /* A zero row leaves a free maximization direction genuinely unbounded. */
    memset(&p, 0, sizeof(p));
    p.n = 1; p.m = 1; p.maximize = 1;
    p.c = (double*)malloc(sizeof(double)); p.c[0] = 1.0;
    p.l = (double*)malloc(sizeof(double)); p.u = (double*)malloc(sizeof(double));
    p.l[0] = -LP_INF; p.u[0] = LP_INF;
    p.b = (double*)malloc(sizeof(double)); p.b[0] = 0.0;
    p.rel = (char*)malloc(1); p.rel[0] = '<';
    p.Acolptr = (int*)malloc(2 * sizeof(int)); p.Acolptr[0] = 0; p.Acolptr[1] = 0;
    p.Arow = (int*)malloc(sizeof(int)); p.Aval = (double*)malloc(sizeof(double));
    s = solver_create(&p);
    r = solver_solve(s);
    if (r != 2) { printf("free unbounded FAIL: r=%d\n", r); bad++; }
    solver_destroy(s); lp_free_local(&p);
    return bad;
}

static int test_mip_free_integer(void)
{
    /* max integer x, 2x <= 3, x >= -2.  LP relaxation is 1.5, so branch and
       bound must branch on the externally reconstructed free variable. */
    double c[1] = {1.0}, b[2] = {3.0, -2.0};
    double l[1] = {-LP_INF}, u[1] = {LP_INF}, aval[2] = {2.0, 1.0};
    int colptr[2] = {0, 2}, row[2] = {0, 1};
    char rel[2] = {'<', '>'};
    unsigned char isint[1] = {1};
    MIP mip; memset(&mip, 0, sizeof(mip));
    mip.n = 1; mip.m = 2; mip.c = c; mip.b = b; mip.l = l; mip.u = u;
    mip.Acolptr = colptr; mip.Arow = row; mip.Aval = aval; mip.rel = rel;
    mip.maximize = 1; mip.isint = isint; mip.node_limit = 1000; mip.lp_iter_limit = 100000;
    MIPResult res; mip_solve(&mip, &res);
    int bad = (res.status != 0 || fabs(res.obj - 1.0) > EPS || fabs(res.x[0] - 1.0) > EPS);
    if (bad) printf("positive free integer MIP FAIL: status=%d obj=%g x=%g nodes=%ld\n",
                    res.status, res.obj, res.x[0], res.nodes);
    mip_result_free(&res);

    /* Symmetric negative-direction branch: min x, 2x >= -3, x <= 2. */
    b[0] = -3.0; b[1] = 2.0; rel[0] = '>'; rel[1] = '<';
    aval[0] = 2.0; aval[1] = 1.0;
    mip.maximize = 0;
    mip_solve(&mip, &res);
    int bad2 = (res.status != 0 || fabs(res.obj + 1.0) > EPS || fabs(res.x[0] + 1.0) > EPS);
    if (bad2) printf("negative free integer MIP FAIL: status=%d obj=%g x=%g nodes=%ld\n",
                     res.status, res.obj, res.x[0], res.nodes);
    mip_result_free(&res);
    return bad + bad2;
}

int main(void)
{
    int bad = test_lp_and_incremental() + test_negative_solution_and_unbounded() + test_mip_free_integer();
    if (bad) { printf("FREE VARIABLE TESTS FAILED: %d\n", bad); return 1; }
    printf("ALL FREE-VARIABLE LP/MIP TESTS PASSED\n");
    return 0;
}
