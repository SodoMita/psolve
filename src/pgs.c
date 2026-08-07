#include "pgs.h"
#include <math.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Projected Gauss-Seidel with optional SOR.                           */
/*                                                                     */
/* Standard update for variable i at sweep k:                          */
/*   r_i = b_i + sum_{j != i} A_{i,j} x_j                              */
/*   x_i_new = clamp( (1-omega) x_i + omega * (-r_i / A_{i,i}),        */
/*                    lo_i, hi_i )                                     */
/*                                                                     */
/* The diagonal is assumed positive (PSD/PD contact matrices).  For a   */
/* symmetric PSD A this is a convergent fixed-point iteration (like     */
/* Box2D's solver).  It is deterministic and runs a fixed number of     */
/* sweeps regardless of input.                                          */
/* ------------------------------------------------------------------ */
void pgs_solve(const PGSOptions *opt,
               const double *A, const double *b,
               const double *lo, const double *hi,
               double *x, PGSResult *res)
{
    int n = opt->n;
    double omega = opt->omega;
    if (omega <= 0.0 || omega > 2.0) omega = 1.0;
    int max_iter = opt->max_iter;
    if (max_iter <= 0) max_iter = 1;
    double tol = opt->tol;

    res->iters = 0;
    res->flops = 0;
    res->status = 1;   /* assume not converged */

    /* warm start: clamp any initial guess to the box */
    for (int i = 0; i < n; i++) {
        if (x[i] < lo[i]) x[i] = lo[i];
        else if (x[i] > hi[i]) x[i] = hi[i];
    }

    /* diagonal reciprocals (precomputed) */
    double *invdiag = (double*)__builtin_alloca((size_t)n * sizeof(double));
    for (int i = 0; i < n; i++) {
        double d = A[i*n + i];
        invdiag[i] = (d > 1e-14) ? (1.0 / d) : 0.0;
    }

    /* Gauss-Seidel sweeps: always use current (updated) x for all but the
       pivot variable to maximize progress */
    for (int it = 0; it < max_iter; it++) {
        double maxdelta = 0.0;
        for (int i = 0; i < n; i++) {
            /* r_i = b_i + sum_{j != i} A_{i,j} x_j
                   = b_i + (A x)_i - A_{i,i} x_i */
            double ax = 0.0;
            for (int j = 0; j < n; j++) { ax += A[i*n + j] * x[j]; res->flops++; }
            double r = b[i] + (ax - A[i*n + i] * x[i]);

            double xnew = (1.0 - omega) * x[i] + omega * (-r * invdiag[i]);
            /* project onto box */
            if (xnew < lo[i]) xnew = lo[i];
            else if (xnew > hi[i]) xnew = hi[i];

            double delta = fabs(xnew - x[i]);
            if (delta > maxdelta) maxdelta = delta;
            x[i] = xnew;
        }
        res->iters = it + 1;
        if (tol > 0.0 && maxdelta <= tol) { res->status = 0; break; }
    }

    /* objective: 1/2 x^T A x + b^T x */
    double obj = 0.0;
    for (int i = 0; i < n; i++) {
        double ax = 0.0;
        for (int j = 0; j < n; j++) ax += A[i*n + j] * x[j];
        obj += 0.5 * ax * x[i] + b[i] * x[i];
    }
    res->obj = obj;
    if (res->status == 1 && tol > 0.0) {
        /* still not converged after all iters */
    }
}

void pgs_matvec(const double *A, int n, const double *x, double *y)
{
    for (int i = 0; i < n; i++) {
        double s = 0.0;
        for (int j = 0; j < n; j++) s += A[i*n + j] * x[j];
        y[i] = s;
    }
}
