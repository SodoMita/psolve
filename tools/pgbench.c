#include "pgs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* Microbenchmark: time many small boxed-QP solves of realistic 2D-physics
   sizes (contact clusters with friction), reporting per-solve latency. */

static double now_ns(void)
{
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e9 + (double)t.tv_nsec;
}

/* Build a symmetric PSD contact-style matrix: A = J^T J + I, boxed vars. */
static void make_system(int n, double *A, double *b, double *lo, double *hi,
                        double *warm, unsigned *seed)
{
    unsigned s = *seed;
    #define R() ((s=s*1664525u+1013904223u), (double)((s>>8)&0xffff)/65535.0)
    /* random constraint matrix J (n x n, sparse-ish) */
    double J[64*64];
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            J[i*n+j] = (R() < 0.6) ? (R()*2-1) : 0.0;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            double ssum = (i==j?1.0:0.0);
            for (int k = 0; k < n; k++) ssum += J[k*n+i]*J[k*n+j];
            A[i*n+j] = ssum;
        }
    for (int i = 0; i < n; i++) {
        b[i] = R()*2-1;
        lo[i] = R()*2-1;
        hi[i] = lo[i] + R()*2 + 0.1;
        warm[i] = lo[i] + R()*(hi[i]-lo[i]);
    }
    *seed = s;
    #undef R
}

static void bench(int n, int iters, int reps)
{
    static double A[64*64], b[64], lo[64], hi[64], x[64];
    PGSOptions opt = {n, iters, 1.0, 1e-10};
    PGSResult res;
    unsigned seed = 1;
    make_system(n, A, b, lo, hi, x, &seed);

    /* warm up */
    for (int r = 0; r < 3; r++) pgs_solve(&opt, A, b, lo, hi, x, &res);

    double t0 = now_ns();
    for (int r = 0; r < reps; r++) {
        /* re-warm from previous result to emulate frame-to-frame reuse */
        pgs_solve(&opt, A, b, lo, hi, x, &res);
    }
    double t1 = now_ns();
    double per = (t1 - t0) / reps;
    printf("  n=%-3d iters=%-4d  %8.1f ns/solve  (%6.2f us)  status=%d\n",
           n, iters, per, per/1000.0, res.status);
}

int main(void)
{
    printf("PGS boxed-QP microbenchmark (worst: 2 CPU cores, this box)\n");
    /* realistic 2D physics cluster sizes */
    bench(4, 10, 200000);
    bench(8, 10, 200000);
    bench(16, 10, 100000);
    bench(32, 20, 50000);
    bench(64, 20, 30000);
    return 0;
}
