#include "pgs_fixed.h"
#include <stdint.h>
#include <limits.h>
#include <stddef.h>

/* round half away from zero: q = n / d with d > 0 */
static inline int64_t div_round(int64_t n, int64_t d)
{
    int64_t q = n / d;
    int64_t r = n % d;
    int64_t ar = r < 0 ? -r : r;
    if (ar * 2 >= d) {
        q += (n >= 0) ? 1 : -1;
    }
    return q;
}

void pgsf_matvec(const int64_t *A, int n, const int64_t *x, int64_t *y)
{
    for (int i = 0; i < n; i++) {
        __int128 s = 0;
        for (int j = 0; j < n; j++) s += (__int128)A[i*n + j] * x[j];
        y[i] = (int64_t)s;
    }
}

void pgsf_solve(const PGSFixedOptions *opt,
                const int64_t *A, const int64_t *b,
                const int64_t *lo, const int64_t *hi,
                int64_t *x, PGSResult *res)
{
    int n = opt->n;
    int64_t w_num = opt->w_num, w_den = opt->w_den;
    if (w_den <= 0) { w_num = 1; w_den = 1; }
    if (w_num <= 0 || w_num >= 2 * w_den) { w_num = w_den; } /* default GS */
    int max_iter = opt->max_iter > 0 ? opt->max_iter : 1;
    int64_t tol = opt->tol;

    res->iters = 0;
    res->flops = 0;
    res->status = 1;   /* not converged by default */

    /* clamp warm start to box */
    for (int i = 0; i < n; i++) {
        if (x[i] < lo[i]) x[i] = lo[i];
        else if (x[i] > hi[i]) x[i] = hi[i];
    }

    /* diagonal reciprocals are divisions; store diag for each row */
    int64_t *diag = (int64_t*)__builtin_alloca((size_t)n * sizeof(int64_t));
    for (int i = 0; i < n; i++) diag[i] = A[i*n + i];

    for (int it = 0; it < max_iter; it++) {
        int64_t maxdelta = 0;
        for (int i = 0; i < n; i++) {
            /* r_i = b_i + sum_{j != i} A_{i,j} x_j
                  = b_i + (A x)_i - A_{i,i} x_i */
            __int128 ax = 0;
            for (int j = 0; j < n; j++) { ax += (__int128)A[i*n + j] * x[j]; res->flops++; }
            __int128 r = (__int128)b[i] + (ax - (__int128)diag[i] * x[i]);

            /* Gauss-Seidel step, integer-exact:  x = round(-r / diag) */
            int64_t step = div_round((int64_t)-r, diag[i]);

            /* SOR on integers:  x = ((w_den - w_num)*x + w_num*step) / w_den */
            __int128 num = (__int128)(w_den - w_num) * x[i] + (__int128)w_num * step;
            int64_t xnew = div_round((int64_t)num, w_den);

            if (xnew < lo[i]) xnew = lo[i];
            else if (xnew > hi[i]) xnew = hi[i];

            int64_t delta = xnew > x[i] ? xnew - x[i] : x[i] - xnew;
            if (delta > maxdelta) maxdelta = delta;
            x[i] = xnew;
        }
        res->iters = it + 1;
        if (tol > 0 && maxdelta <= tol) { res->status = 0; break; }
    }

    /* objective as a double for reporting (solving is integer) */
    double obj = 0.0;
    for (int i = 0; i < n; i++) {
        __int128 ax = 0;
        for (int j = 0; j < n; j++) ax += (__int128)A[i*n + j] * x[j];
        obj += 0.5 * ((double)ax) * (double)x[i] + (double)b[i] * (double)x[i];
    }
    res->obj = obj;
}
