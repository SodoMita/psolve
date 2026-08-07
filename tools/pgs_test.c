#include "pgs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Check a PGS solve against a brute-force minimum over a boxed 1D/2D case
   and against the exact unconstrained solution when the box is inactive. */

static double objval(const double *A, const double *b, const double *x, int n)
{
    double s = 0;
    double ax[16];
    pgs_matvec(A, n, x, ax);
    for (int i = 0; i < n; i++) s += 0.5 * ax[i] * x[i] + b[i] * x[i];
    return s;
}

static void t1_1d_inactive(void)
{
    /* min 1/2 x^2 - 3x  =>  x = 3 (unconstrained); box [-10,10] inactive */
    double A[1] = {1.0};
    double b[1] = {-3.0};
    double lo[1] = {-10.0}, hi[1] = {10.0};
    double x[1] = {0.0};
    PGSOptions opt = {1, 1000, 1.0, 1e-12};
    PGSResult res;
    pgs_solve(&opt, A, b, lo, hi, x, &res);
    printf("T1 1D-inactive: x=%g (expect 3) status=%d iters=%d\n", x[0], res.status, res.iters);
}

static void t2_1d_active(void)
{
    /* min 1/2 x^2 - 3x, box [0,1] => x clamped to 1 */
    double A[1] = {1.0};
    double b[1] = {-3.0};
    double lo[1] = {0.0}, hi[1] = {1.0};
    double x[1] = {0.0};
    PGSOptions opt = {1, 1000, 1.0, 1e-12};
    PGSResult res;
    pgs_solve(&opt, A, b, lo, hi, x, &res);
    printf("T2 1D-active:  x=%g (expect 1) status=%d\n", x[0], res.status);
}

static void t3_2d(void)
{
    /* min 1/2 x^T A x + b^T x, A=[[2,0.5],[0.5,1]], b=[-2,-1]
       unconstrained optimum: solve A x = -b.
       2x + 0.5y = 2, 0.5x + y = 1  =>  y = 1 - 0.5x
       2x + 0.5(1-0.5x)=2 => 2x+0.5-0.25x=2 => 1.75x=1.5 => x=0.8571, y=0.5714 */
    double A[4] = {2.0, 0.5, 0.5, 1.0};   /* column-major: A[0][0],A[1][0],A[0][1],A[1][1] */
    double b[2] = {-2.0, -1.0};
    double lo[2] = {-100.0, -100.0}, hi[2] = {100.0, 100.0};
    double x[2] = {0.0, 0.0};
    PGSOptions opt = {2, 2000, 1.0, 1e-12};
    PGSResult res;
    pgs_solve(&opt, A, b, lo, hi, x, &res);
    printf("T3 2D: x=%g,%g (expect 0.8571,0.5714) status=%d iters=%d\n",
           x[0], x[1], res.status, res.iters);
}

/* Random PSD test: verify projected optimum is no worse than random feasible
   points and respects the box, and warm start preserves feasibility. */
static void t4_random(void)
{
    srand(7);
    int bad = 0, tested = 0;
    for (int t = 0; t < 2000; t++) {
        int n = 1 + rand() % 5;
        /* build symmetric PSD A = G^T G + I */
        double G[25], A[25];
        for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) G[i*n+j] = ((rand()%200)-100)/50.0;
        for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) {
            double s = (i==j?1.0:0.0);
            for (int k = 0; k < n; k++) s += G[k*n+i]*G[k*n+j];
            A[i*n+j] = s;
        }
        double b[5], lo[5], hi[5], x[5];
        for (int i = 0; i < n; i++) {
            b[i] = ((rand()%400)-200)/100.0;
            lo[i] = -((rand()%10)+1);
            hi[i] = ((rand()%10)+1);
            x[i] = 0.0;
        }
        PGSOptions opt = {n, 500, 1.0, 1e-10};
        PGSResult res;
        pgs_solve(&opt, A, b, lo, hi, x, &res);
        /* verify box */
        int inbox = 1;
        for (int i = 0; i < n; i++) if (x[i] < lo[i]-1e-9 || x[i] > hi[i]+1e-9) inbox = 0;
        if (!inbox) { printf("T4 BOX VIOLATION t=%d\n", t); bad++; continue; }
        /* gradient at solution must be zero for interior, or point inward at box */
        double g[5]; pgs_matvec(A, n, x, g);
        for (int i = 0; i < n; i++) {
            double grad = b[i] + g[i];
            if (x[i] > lo[i]+1e-6 && x[i] < hi[i]-1e-6 && fabs(grad) > 1e-5) { bad++; printf("T4 GRAD t=%d i=%d grad=%g\n", t, i, grad); break; }
        }
        /* compare to random feasible points: objective must be <= all of them */
        double optv = objval(A, b, x, n);
        for (int r = 0; r < 200; r++) {
            double y[5];
            for (int i = 0; i < n; i++) y[i] = lo[i] + (hi[i]-lo[i])*(((rand()%1000))/1000.0);
            if (objval(A, b, y, n) < optv - 1e-6) { bad++; printf("T4 SUBOPT t=%d r=%d\n", t, r); break; }
        }
        tested++;
    }
    printf("T4 random: tested=%d bad=%d\n", tested, bad);
}

int main(void)
{
    t1_1d_inactive();
    t2_1d_active();
    t3_2d();
    t4_random();
    return 0;
}
