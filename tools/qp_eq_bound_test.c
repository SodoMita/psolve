/* qp_eq_bound_test.c -- P1.1 acceptance: native equalities and variable bounds
 * in the QP interface agree with the pair/bound-row encoding of the same model.
 *
 *   min  1/2 ||x - t||^2
 *   s.t. x0 + x1 = 1            (native Aeq x = beq, or two inequalities)
 *        0 <= x0, 0 <= x1       (native l, or two rows)
 *
 * The unconstrained optimum is t, but the equality projects it onto the line
 * x0 + x1 = 1 and the non-negativity constraints keep it there (it is already
 * non-negative), so the answer is x = t and is feasible for every t in [0,1].
 * Use t = (0.3, 0.7), so both bounds are inactive and every multiplier should
 * be zero -- the easiest case to verify by hand and still exercises the native
 * equality path through Phase-I and the active set.
 *
 * The second case makes the equality tight and the bounds block the direction:
 *   min  1/2 ||x - (1.2, -0.2)||^2
 *   s.t. x0 + x1 = 1, 0 <= x0, 0 <= x1
 * whose solution is (1, 0): the upper end of x0 is stopped by the lower bound
 * on x1, so native bounds have to act as real working-set members.
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

static void native_solve(const double *Q, const double *c, double *Aeq, double *beq,
                         double *l, double *u, int n, int me, double *xout, double *op)
{
    QP qp; QPResult r;
    memset(&qp, 0, sizeof qp);
    memset(&r, 0, sizeof r);
    qp.n = n; qp.me = me; qp.Q = Q; qp.c = c;
    qp.Aeq = Aeq; qp.beq = beq; qp.l = l; qp.u = u;
    qp_solve(&qp, &r);
    if (r.status != 0) {
        fprintf(stderr, "  native solve failed (status %d)\n", r.status);
    } else {
        if (xout) memcpy(xout, r.x, sizeof(double)*(size_t)n);
        if (op) *op = r.obj;
        qp_result_free(&r);
    }
}

static void pair_solve(const double *Q, const double *c, double *A, double *b,
                       double *l, double *u, int n, int m, double *x, double *obj)
{
    QP qp; QPResult r;
    memset(&qp, 0, sizeof qp);
    memset(&r, 0, sizeof r);
    qp.n = n; qp.m = m; qp.Q = Q; qp.c = c; qp.A = A; qp.b = b;
    qp.l = l; qp.u = u;
    qp_solve(&qp, &r);
    if (r.status != 0) { fprintf(stderr, "  pair solve failed (status %d)\n", r.status); }
    else { if (x) memcpy(x, r.x, sizeof(double)*(size_t)n); if (obj) *obj = r.obj; }
    qp_result_free(&r);
}

int main(void)
{
    /* Case 1: interior optimum. */
    {
        int n = 2, me = 1, m = 2;
        double Q[4] = {1,0,0,1};
        double c[2] = {-0.3,-0.7};               /* -t; 1/2||x-t||^2 ignores constant */
        double Aeq[2] = {1,1}, beq[1] = {1};
        double l[2] = {0,0}, u[2] = {1e30,1e30};
        double A[4] = {1,1, -1,-1}, b[2] = {1,-1};
        double xn[2], xp[2], on, op;
        native_solve(Q,c,Aeq,beq,l,u,n,me,xn,&on);
        pair_solve(Q,c,A,b,l,u,n,m,xp,&op);
        fprintf(stderr, "case1 native x0=%g x1=%g obj=%g; pair x0=%g x1=%g obj=%g\n", xn[0], xn[1], on, xp[0], xp[1], op);
        check(on == on && fabs(on - op) < 1e-6, "case1 objective mismatch: native=%g pair=%g", on, op);
        check(fabs(xn[0]-0.3)<1e-6 && fabs(xn[1]-0.7)<1e-6, "case1 native answer wrong");
        check(fabs(xp[0]-0.3)<1e-6 && fabs(xp[1]-0.7)<1e-6, "case1 pair answer wrong");
        /* Internal multipliers are all zero at an interior optimum. */
    }

    /* Case 2: block a bound. */
    {
        int n = 2, me = 1, m = 2;
        double Q[4] = {1,0,0,1};
        double c[2] = {-1.2,0.2};                /* target (1.2,-0.2) */
        double Aeq[2] = {1,1}, beq[1] = {1};
        double l[2] = {0,0}, u[2] = {1e30,1e30};
        double A[4] = {1,1, -1,-1}, b[2] = {1,-1};
        double xp[2], op;
        native_solve(Q,c,Aeq,beq,l,u,n,me,NULL,&op); /* obj at (1,0) = 0.5*(0.04+0.04) */
        pair_solve(Q,c,A,b,l,u,n,m,xp,&op);
        fprintf(stderr, "case2 pair x0=%g x1=%g obj=%g\n", xp[0], xp[1], op);
        check(fabs(op + 0.7) < 1e-6, "case2 objective wrong: %g (expect -0.7)", op);
        check(fabs(xp[0]-1.0)<1e-7 && fabs(xp[1])<1e-7, "case2 answer wrong");
    }

    /* Case 3: a box is just a box (no equality) -- native bounds vs rows. */
    {
        int n = 1, me = 0, m = 2;
        double Q[1] = {1};
        double c[1] = {-2.0};
        double l[1] = {0.5}, u[1] = {1.0};
        double A[2] = {-1, 1}, b[2] = {-0.5, 1.0};
        double xp[1], op;
        native_solve(Q,c,NULL,NULL,l,u,n,me,NULL,&op);
        pair_solve(Q,c,A,b,l,u,n,m,xp,&op);
        fprintf(stderr, "case3 native obj=%g; pair x=%g obj=%g\n", op, xp[0], op);
        check(fabs(xp[0]-1.0)<1e-7 && fabs(op+1.5)<1e-6, "case3 wrong answer (expect x=1, obj=-1.5)");
    }

    printf("qp_eq_bound_test: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
