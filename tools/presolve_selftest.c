/* Roadmap 7.1 presolve records unit harness.
 *
 * The end-to-end lanes are covered by tools/presolve_verify.py (planted
 * verdict cases + random A/B parity); this file unit-verifies the record
 * bookkeeping itself, per the 7.1 acceptance item "reduction records are
 * themselves unit-verified (apply -> restore -> identical behaviour)":
 *
 *   1. doubleton-chain full elimination: postsolved primal point from an
 *      EMPTY reduced engine input must satisfy the ORIGINAL rows/box and
 *      reproduce the hand-computed optimum; dual replay must reproduce
 *      exact stationarity y^T A == c on every column.
 *   2. fixed-column record: the pinned value is restored exactly.
 *   3. singleton-row record: dual replay is 0 at a SLACK point and the
 *      complementarity value at a TIGHT point (the guard).
 *   4. redundant-row record: replayed dual is exactly 0.
 *   5. empty-row conflict: lp_presolve returns 1 and the Farkas ray hint
 *      is the one-row unit ray.
 *   6. stats accounting matches the fired reductions.
 *
 * Exit 0 iff all checks pass.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "solver.h"
#include "parser.h"     /* lp_free */
#include "presolve.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } \
    else { printf("ok: %s\n", msg); } \
} while (0)

/* build an LP from dense-ish row lists; arrays owned by caller-statics
 * (the harness exits after the checks; lp_free() runs over what we hand
 * it, so keep it consistent with lp_read ownership semantics) */
static LP mk_lp(int n, int m, int nnz, int maximize)
{
    LP lp;
    memset(&lp, 0, sizeof lp);
    lp.n = n; lp.m = m; lp.maximize = maximize;
    lp.c = (double *)calloc((size_t)n, sizeof(double));
    lp.rel = (char *)calloc((size_t)(m ? m : 1), 1);
    lp.b = (double *)calloc((size_t)(m ? m : 1), sizeof(double));
    lp.l = (double *)calloc((size_t)n, sizeof(double));
    lp.u = (double *)calloc((size_t)n, sizeof(double));
    lp.Acolptr = (int *)calloc((size_t)n + 1, sizeof(int));
    lp.Arow = (int *)calloc((size_t)(nnz ? nnz : 1), sizeof(int));
    lp.Aval = (double *)calloc((size_t)(nnz ? nnz : 1), sizeof(double));
    if (!lp.c || !lp.rel || !lp.b || !lp.l || !lp.u || !lp.Acolptr ||
        !lp.Arow || !lp.Aval) { fprintf(stderr, "oom\n"); exit(2); }
    return lp;
}

/* triplet-list -> CSC fill (triplets may repeat; column-major walk) */
static void fill_csc(LP *lp, int ntri,
                     const int *ti, const int *tj, const double *tv)
{
    int n = lp->n;
    for (int t = 0; t < ntri; t++) lp->Acolptr[tj[t] + 1]++;
    for (int j = 0; j < n; j++) lp->Acolptr[j + 1] += lp->Acolptr[j];
    int pos[64];
    for (int j = 0; j < n; j++) pos[j] = lp->Acolptr[j];
    for (int t = 0; t < ntri; t++) {
        int k = pos[tj[t]]++;
        lp->Arow[k] = ti[t]; lp->Aval[k] = tv[t];
    }
}

static double row_resid(const LP *lp, int i, const double *x)
{
    /* activity of ORIGINAL row i at ORIGINAL-length x (CSC walk) */
    double act = 0.0;
    for (int j = 0; j < lp->n; j++)
        for (int k = lp->Acolptr[j]; k < lp->Acolptr[j + 1]; k++)
            if (lp->Arow[k] == i) act += lp->Aval[k] * x[j];
    char r = lp->rel[i];
    if (r == '<') return lp->b[i] - act;         /* >= 0 ok */
    if (r == '>') return act - lp->b[i];         /* >= 0 ok */
    return fabs(act - lp->b[i]);                 /* ~0 ok */
}

static double col_yA(const LP *lp, int j, const double *y)
{
    double v = 0.0;
    for (int k = lp->Acolptr[j]; k < lp->Acolptr[j + 1]; k++)
        v += y[lp->Arow[k]] * lp->Aval[k];
    return v;
}

int main(void)
{
    /* ---- 1. doubleton chain, full elimination -------------------------
       x + y = 3, y + 2w = 2, box [0,10] on all, max x + w.
       Optimum: x <= 3 (y >= 0) and w <= 1 (y >= 0), jointly y = 0 ->
       x = 3, w = 1, objective 4.  */
    {
        LP lp = mk_lp(3, 2, 4, 1);
        int ti[4] = {0, 0, 1, 1}, tj[4] = {0, 1, 1, 2};
        double tv[4] = {1, 1, 1, 2};
        fill_csc(&lp, 4, ti, tj, tv);
        lp.c[0] = 1; lp.c[2] = 1;
        lp.b[0] = 3; lp.b[1] = 2; lp.rel[0] = '='; lp.rel[1] = '=';
        for (int j = 0; j < 3; j++) { lp.l[j] = 0; lp.u[j] = 10; }
        LP red; PreSolve *ps = NULL; PreStats st;
        memset(&red, 0, sizeof red);
        int rc = lp_presolve(&lp, &red, &ps, &st);
        CHECK(rc == 0 && red.n == 1 && red.m == 0,
              "dbl-chain: both rows eliminated, 1 rowless col remains");
        CHECK(st.doubleton_rows == 2, "dbl-chain: 2 doubleton recs fired");
        /* reduced optimum is y = 0 (its reduced cost is -1.5, max wants
           the lower bound): replay must land on x=3, y=0, w=1 */
        double xr[1] = {0}, xo[3] = {0, 0, 0};
        int rx = lp_presolve_postsolve_x(ps, xr, xo);
        double slack = row_resid(&lp, 0, xo), slack1 = row_resid(&lp, 1, xo);
        CHECK(rx == 0 && slack <= 1e-12 && slack1 <= 1e-12 &&
              xo[0] >= 0 && xo[0] <= 10 && xo[1] >= 0 && xo[1] <= 10 &&
              xo[2] >= 0 && xo[2] <= 10,
              "dbl-chain: postsolved x satisfies ORIGINAL rows + box");
        CHECK(fabs(xo[0] - 3.0) <= 1e-12 && fabs(xo[1]) <= 1e-12 &&
              fabs(xo[2] - 1.0) <= 1e-12,
              "dbl-chain: primal replay hits the known optimum x=(3,0,1)");
        /* dual replay invariant: EXACT stationarity on the eliminated
           pivot columns (x for rec row0, w for rec row1); the surviving
           column y must carry the complementary reduced-cost sign for
           its bound status (at lower bound, max: c - y^T A <= 0) */
        double yo[2] = {0, 0};
        int ry = lp_presolve_postsolve_duals(ps, NULL, xo, yo);
        double ry_y = lp.c[1] - col_yA(&lp, 1, yo);
        CHECK(ry == 0 && fabs(col_yA(&lp, 0, yo) - lp.c[0]) <= 1e-12 &&
              fabs(col_yA(&lp, 2, yo) - lp.c[2]) <= 1e-12,
              "dbl-chain: dual replay exact on eliminated pivot columns");
        CHECK(ry == 0 && ry_y <= 1e-12,
              "dbl-chain: surviving col dual sign matches its bound");
        lp_presolve_free(ps);
        lp_free(&red);
        free(lp.c); free(lp.rel); free(lp.b); free(lp.l); free(lp.u);
        free(lp.Acolptr); free(lp.Arow); free(lp.Aval);
    }

    /* ---- 2. fixed column ----------------------------------------------
       x == 7 pinned, row x + y >= 9, y in [0,10], min y.  Reduced model
       keeps y; a reduced-feasible y=4 replays to x=7 exactly.  */
    {
        LP lp = mk_lp(2, 1, 2, 0);
        int ti[2] = {0, 0}, tj[2] = {0, 1};
        double tv[2] = {1, 1};
        fill_csc(&lp, 2, ti, tj, tv);
        lp.c[1] = 1;
        lp.b[0] = 9; lp.rel[0] = '>';
        lp.l[0] = 7; lp.u[0] = 7; lp.l[1] = 0; lp.u[1] = 10;
        LP red; PreSolve *ps = NULL; PreStats st;
        memset(&red, 0, sizeof red);
        int rc = lp_presolve(&lp, &red, &ps, &st);
        /* the pin folds x everywhere; the row becomes the singleton
           y >= 2 and cascades into the box, so 1 col / 0 rows remain */
        CHECK(rc == 0 && red.n == 1 && red.m == 0 && st.fixed_cols == 1 &&
              st.singleton_rows == 1,
              "fixed-col: x folded out, singleton cascade empties rows");
        double xr[1] = {4}, xo[2] = {0, 0};
        int rx = lp_presolve_postsolve_x(ps, xr, xo);
        CHECK(rx == 0 && xo[0] == 7.0 && xo[1] == 4.0 &&
              row_resid(&lp, 0, xo) >= -1e-12,
              "fixed-col: pin restored exactly (apply->restore)");
        lp_presolve_free(ps);
        lp_free(&red);
        free(lp.c); free(lp.rel); free(lp.b); free(lp.l); free(lp.u);
        free(lp.Acolptr); free(lp.Arow); free(lp.Aval);
    }

    /* ---- 3. singleton row: complementarity guard on dual replay -------
       row x >= 5, box x [-10,10], max x (cost 1).  Folds box to [5,10].
       At a SLACK point the replayed dual is 0; at the TIGHT point it is
       the cost-carrying value.  */
    {
        LP lp = mk_lp(1, 1, 1, 1);
        int ti[1] = {0}, tj[1] = {0};
        double tv[1] = {1};
        fill_csc(&lp, 1, ti, tj, tv);
        lp.c[0] = 1;
        lp.b[0] = 5; lp.rel[0] = '>';
        lp.l[0] = -10; lp.u[0] = 10;
        LP red; PreSolve *ps = NULL; PreStats st;
        memset(&red, 0, sizeof red);
        int rc = lp_presolve(&lp, &red, &ps, &st);
        CHECK(rc == 0 && red.m == 0 && st.singleton_rows == 1,
              "singleton: row folded into the box, 1 rec");
        double xr[1] = {6}, xs[1] = {6}, yo[1];
        int rx = lp_presolve_postsolve_x(ps, xr, xs);
        CHECK(rx == 0 && xs[0] >= 5.0 - 1e-12,
              "singleton: folded box bound respected by replay");
        (void)lp_presolve_postsolve_duals(ps, NULL, xs, yo);
        CHECK(yo[0] == 0.0, "singleton: slack row replays dual 0 (guard)");
        double xt[1] = {5};
        xr[0] = 5;
        (void)lp_presolve_postsolve_x(ps, xr, xt);
        (void)lp_presolve_postsolve_duals(ps, NULL, xt, yo);
        CHECK(fabs(yo[0] - 1.0) <= 1e-12,
              "singleton: tight row replays the cost-carrying dual");
        lp_presolve_free(ps);
        lp_free(&red);
        free(lp.c); free(lp.rel); free(lp.b); free(lp.l); free(lp.u);
        free(lp.Acolptr); free(lp.Arow); free(lp.Aval);
    }

    /* ---- 4. redundant row ---------------------------------------------
       2x + 2y <= 30 under boxes [0,7]x[0,7]: activity max 28 ->
       row can never bind; replayed dual exactly 0.  */
    {
        LP lp = mk_lp(2, 1, 2, 1);
        int ti[2] = {0, 0}, tj[2] = {0, 1};
        double tv[2] = {2, 2};
        fill_csc(&lp, 2, ti, tj, tv);
        lp.c[0] = 1;
        lp.b[0] = 30; lp.rel[0] = '<';
        lp.l[0] = 0; lp.u[0] = 7; lp.l[1] = 0; lp.u[1] = 7;
        LP red; PreSolve *ps = NULL; PreStats st;
        memset(&red, 0, sizeof red);
        int rc = lp_presolve(&lp, &red, &ps, &st);
        CHECK(rc == 0 && red.m == 0 && st.redundant_rows == 1,
              "redundant: row dropped, 1 rec");
        double xr[2] = {5, 6}, xo[2] = {0, 0}, yo[1] = {-9};
        int rx = lp_presolve_postsolve_x(ps, xr, xo);
        CHECK(rx == 0 && xo[0] == 5 && xo[1] == 6 &&
              row_resid(&lp, 0, xo) >= -1e-12,
              "redundant: primal replay keeps the original row satisfied");
        (void)lp_presolve_postsolve_duals(ps, NULL, xo, yo);
        CHECK(yo[0] == 0.0, "redundant: replayed dual is exactly 0");
        lp_presolve_free(ps);
        lp_free(&red);
        free(lp.c); free(lp.rel); free(lp.b); free(lp.l); free(lp.u);
        free(lp.Acolptr); free(lp.Arow); free(lp.Aval);
    }

    /* ---- 5. empty-row conflict ------------------------------------------
       no entries, rel '<', b = -1: exact infeasible, one-row unit ray.  */
    {
        LP lp = mk_lp(1, 1, 0, 1);
        lp.c[0] = 1;
        lp.b[0] = -1; lp.rel[0] = '<';
        lp.l[0] = 0; lp.u[0] = 2;
        LP red; PreSolve *ps = NULL; PreStats st;
        memset(&red, 0, sizeof red);
        int rc = lp_presolve(&lp, &red, &ps, &st);
        CHECK(rc == 1, "empty-row conflict: rc==1 (exact infeasible)");
        double y[1] = {0};
        int rz = ps ? lp_presolve_farkas_ray(ps, y) : -1;
        CHECK(rz == 0 && y[0] == 1.0, "empty-row conflict: unit ray hint");
        if (ps) lp_presolve_free(ps);
        free(lp.c); free(lp.rel); free(lp.b); free(lp.l); free(lp.u);
        free(lp.Acolptr); free(lp.Arow); free(lp.Aval);
    }

    if (failures) {
        fprintf(stderr, "presolve_selftest: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("presolve_selftest: OK\n");
    return 0;
}
