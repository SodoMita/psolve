/* qp_sparse_test.c -- P1.2 acceptance: sparse A (CSC) plus Q given as
 * diagonal + rank-1 terms produces the same verdict/objective as the same QP
 * in the dense interface.
 */
#include "qp.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

static int fails = 0;
static void check(int cond, const char *fmt, ...)
{
    if (cond) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "FAIL: "); vfprintf(stderr, fmt, ap); fprintf(stderr, "\n");
    va_end(ap);
    fails++;
}

int main(void)
{
    /* min x^2 + y^2 - 2x - 3y, x<=1, y<=1, x>=0, y>=0.
     * Dense Q = 2I (so 1/2 x'Qx = x^2+y^2), c=(-2,-3).
     */
    double qd[2] = {2,2};                 /* Q = diag(2,2) */
    double c[2] = {-2,-3};
    /* CSC for the two inequality rows (rows 0/1 for x, rows 2/3 for y):
     *  row0:  x <= 1      -> [1,0]
     *  row1: -x <= 0      -> [-1,0]
     *  row2:  y <= 1      -> [0,1]
     *  row3: -y <= 0      -> [0,-1]
     */
    int m = 4;
    int colptr[3] = {0,2,4};
    int rowi[4] = {0,1,2,3};
    double avail[4] = {1.0,-1.0,1.0,-1.0};
    double b[4] = {1.0,0.0,1.0,0.0};

    QPSparse s; memset(&s,0,sizeof s);
    s.n=2; s.m=m; s.nq=0; s.c=c; s.b=b;
    s.q_diag=qd; s.q_w=NULL; s.q_rk_colptr=NULL; s.q_rk_rowi=NULL; s.q_rk_val=NULL;
    s.Acolptr=colptr; s.Arow=rowi; s.Aval=avail;

    QPResult r; memset(&r,0,sizeof r);
    qp_solve_sparse(&s,&r);
    fprintf(stderr, "sparse status=%d x=%g,%g obj=%g it=%d\n", r.status,
            r.x ? r.x[0] : 0.0, r.x ? r.x[1] : 0.0, r.obj, r.iterations);
    check(r.status == 0, "sparse QP did not solve (status %d)", r.status);
    if (r.status == 0) {
        check(fabs(r.x[0]-1.0)<1e-7 && fabs(r.x[1]-1.0)<1e-7,
              "sparse answer not (1,1): (%g,%g)", r.x[0], r.x[1]);
        check(fabs(r.obj + 3.0)<1e-6, "sparse objective wrong: %g (expect -3)", r.obj);
    }
    qp_result_free(&r);

    /* Same Q via diagonal + one rank-1 term: Q = diag(1,1) + vv', v=(1,1) is
     * [[2,1],[1,2]]; keep the box so the optimum is still unique. */
    int qc[2] = {0,2}; int qri2[2] = {0,1}; double qv2[2] = {1,1};
    double qd2[2] = {1,1}; double qw2[1] = {1.0};
    double c2[2] = {0,0};
    s.q_diag=qd2; s.nq=1; s.q_w=qw2; s.q_rk_colptr=qc; s.q_rk_rowi=qri2; s.q_rk_val=qv2;
    s.c=c2; s.n=2; s.m=4;
    QPResult r2; memset(&r2,0,sizeof r2);
    qp_solve_sparse(&s,&r2);
    /* min 1/2 x'[[2,1],[1,2]]x over box => stationary interior at (0,0) */
    fprintf(stderr, "rank1 status=%d x=%g,%g obj=%g it=%d\n", r2.status,
            r2.x ? r2.x[0] : 0.0, r2.x ? r2.x[1] : 0.0, r2.obj, r2.iterations);
    check(r2.status == 0, "rank-1 QP did not solve (status %d)", r2.status);
    if (r2.status == 0) {
        check(fabs(r2.x[0])<1e-7 && fabs(r2.x[1])<1e-7 && fabs(r2.obj)<1e-6,
              "rank-1 QP answer wrong: (%g,%g) obj=%g", r2.x[0], r2.x[1], r2.obj);
    }
    qp_result_free(&r2);

    printf("qp_sparse_test: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
