#include "qp.h"
#include "err.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Regression test for QP cooperative stop (Phase 4 hardening).
 *
 * Verifies that:
 *   1. A normal QP still solves to status 0 with no stop callback installed.
 *   2. When psolve_stop() is asked to abort, qp_solve returns QP_STOPPED -- and
 *      never a fabricated OPTIMAL.
 *   3. A stop during the Phase-I feasibility search returns QP_STOPPED with an
 *      empty solution (x == NULL), since no feasible incumbent exists.
 *
 * usage: qp_stop_test
 * exits 0 on success, 1 on failure. */

static int g_stop = 0;
static int stop_now(void) { return g_stop; }

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } \
    else { printf("ok: %s\n", msg); } \
} while (0)

/* A small, valid convex QP: min 0.5 x0^2  s.t.  -x0 <= -1  (x0 >= 1).
 * Optimum x0 = 1, objective 0.5. */
static void build_qp(QP *qp)
{
    memset(qp, 0, sizeof *qp);   /* extensible struct: zero before assigning */
    static double Q[1] = {1.0};
    static double c[1] = {0.0};
    static double A[1] = {-1.0};
    static double b[1] = {-1.0};
    qp->n = 1; qp->m = 1;
    qp->Q = Q; qp->c = c; qp->A = A; qp->b = b; qp->x0 = NULL;
}

/* A QP whose start point x=0 violates the only row (a stop during Phase-I
 * feasibility leaves no certified feasible point). */
static void build_qp_infeas_start(QP *qp)
{
    memset(qp, 0, sizeof *qp);   /* extensible struct: zero before assigning */
    static double Q[1] = {1.0};
    static double c[1] = {0.0};
    static double A[1] = {-1.0};
    static double b[1] = {-1.0};
    qp->n = 1; qp->m = 1;
    qp->Q = Q; qp->c = c; qp->A = A; qp->b = b; qp->x0 = NULL;
}

int main(void)
{
    /* --- 1. baseline correctness preserved (no stop) --- */
    QP qp; build_qp(&qp);
    QPResult r; memset(&r, 0, sizeof(r));
    psolve_stop_set(NULL);
    qp_solve(&qp, &r);
    CHECK(r.status == 0, "baseline QP solves to OPTIMAL (status 0)");
    CHECK(fabs(r.obj - 0.5) < 1e-9, "baseline objective 0.5");
    CHECK(r.x && fabs(r.x[0] - 1.0) < 1e-9, "baseline x0 == 1");
    qp_result_free(&r);

    /* --- 2. cooperative stop during the active-set phase --- */
    build_qp(&qp);
    memset(&r, 0, sizeof(r));
    g_stop = 1;
    psolve_stop_set(stop_now);
    qp_solve(&qp, &r);
    CHECK(r.status == QP_STOPPED, "cooperative stop -> QP_STOPPED (not OPTIMAL)");
    CHECK(r.status != 0, "stop never reports a fabricated OPTIMAL");
    /* best incumbent: still feasible, but not certified optimal */
    if (r.x) {
        CHECK(fabs(r.x[0] - 1.0) < 1e-6, "stopped QP returns feasible best incumbent");
    }
    qp_result_free(&r);
    g_stop = 0;

    /* --- 3. stop during the Phase-I feasibility search --- */
    build_qp_infeas_start(&qp);
    memset(&r, 0, sizeof(r));
    g_stop = 1;
    psolve_stop_set(stop_now);
    qp_solve(&qp, &r);
    CHECK(r.status == QP_STOPPED, "stop during Phase-I -> QP_STOPPED");
    CHECK(r.x == NULL, "stop during Phase-I leaves x == NULL (no feasible incumbent)");
    qp_result_free(&r);
    g_stop = 0;
    psolve_stop_set(NULL);

    /* --- 4. stop disabled again -> solves --- */
    build_qp(&qp);
    memset(&r, 0, sizeof(r));
    qp_solve(&qp, &r);
    CHECK(r.status == 0, "solver fully functional after stop tests");
    qp_result_free(&r);

    if (failures) { fprintf(stderr, "%d FAILURES\n", failures); return 1; }
    printf("all qp_stop_test checks passed\n");
    return 0;
}
