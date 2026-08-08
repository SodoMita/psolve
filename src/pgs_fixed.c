#include "pgs_fixed.h"
#include <stdint.h>
#include <limits.h>
#include <stddef.h>

/* Saturating narrowing.  A 128-bit accumulator that exceeds int64 used to be
 * truncated (implementation-defined wrap-around), silently turning a huge
 * residual into a small one of the opposite sign; saturating keeps the sign
 * and the "very large" magnitude, so the projection onto the box still does
 * the right thing and the kernel stays deterministic. */
static inline int64_t sat64(__int128 v)
{
    if (v > (__int128)INT64_MAX) return INT64_MAX;
    if (v < (__int128)INT64_MIN) return INT64_MIN;
    return (int64_t)v;
}

/* Round half away from zero: q = n / d, saturated to int64.  The common case
 * (a numerator that fits in 64 bits) uses one hardware division; only a truly
 * 128-bit numerator pays for the software 128-bit divide.  The half-test is
 * written `2|r| >= d` as `|r| >= d - |r|` so it cannot overflow.
 * d <= 0 is not a valid divisor: the caller must check. */
static inline int64_t div_round_sat(__int128 n, int64_t d)
{
    if (n <= (__int128)INT64_MAX && n >= (__int128)INT64_MIN) {
        int64_t n64 = (int64_t)n;
        int64_t q = n64 / d, r = n64 % d;
        int64_t ar = r < 0 ? -r : r;
        if (ar >= d - ar) q += (n64 >= 0) ? 1 : -1;
        return q;
    }
    __int128 q = n / d;
    __int128 r = n % d;
    __int128 ar = r < 0 ? -r : r;
    if (ar >= (__int128)d - ar) q += (n >= 0) ? 1 : -1;
    return sat64(q);
}

void pgsf_matvec(const int64_t *A, int n, const int64_t *x, int64_t *y)
{
    if(!A||!x||!y||n<=0||n>46340)return;
    for (int i = 0; i < n; i++) {
        __int128 s = 0;
        for (int j = 0; j < n; j++) s += (__int128)A[i*n + j] * x[j];
        y[i] = sat64(s);
    }
}

void pgsf_solve(const PGSFixedOptions *opt,
                const int64_t *A, const int64_t *b,
                const int64_t *lo, const int64_t *hi,
                int64_t *x, PGSResult *res)
{
    if(!res)return;
    res->iters=0;res->flops=0;res->obj=0.0;res->status=PGS_INVALID;
    if(!opt||!A||!b||!lo||!hi||!x||opt->n<=0||opt->n>46340)return;
    int n = opt->n;
    for(int i=0;i<n;i++)if(lo[i]>hi[i])return;
    int64_t w_num = opt->w_num, w_den = opt->w_den;
    if (w_den <= 0) { w_num = 1; w_den = 1; }
    if (w_num <= 0 || (__int128)w_num >= 2 * (__int128)w_den) { w_num = w_den; } /* default GS */
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

    /* The diagonal is read straight from A inside the sweep: an alloca sized
       by the caller's n is a stack-overflow waiting to happen in a library
       whose n comes from host data, and the kernel promises bounded work with
       no allocation of any kind. */

    for (int it = 0; it < max_iter; it++) {
        int64_t maxdelta = 0;
        for (int i = 0; i < n; i++) {
            /* r_i = b_i + sum_{j != i} A_{i,j} x_j
                  = b_i + (A x)_i - A_{i,i} x_i */
            int64_t d = A[i*n + i];
            __int128 ax = 0;
            for (int j = 0; j < n; j++) { ax += (__int128)A[i*n + j] * x[j]; res->flops++; }
            __int128 r = (__int128)b[i] + (ax - (__int128)d * x[i]);

            /* Gauss-Seidel step, integer-exact:  x = round(-r / diag).
               A non-positive diagonal (an inactive/degenerate contact row, or
               a caller passing a non-PD matrix) has no defined step: it used
               to divide by zero and kill the process with SIGFPE.  Mirror the
               float reference, which treats 1/d as 0 in that case. */
            int64_t step = (d > 0) ? div_round_sat(-r, d) : 0;

            /* SOR on integers:  x = ((w_den - w_num)*x + w_num*step) / w_den */
            __int128 num = (__int128)(w_den - w_num) * x[i] + (__int128)w_num * step;
            int64_t xnew = div_round_sat(num, w_den);

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

long pgsf_batch_solve(int count,const PGSFixedOptions *opts,
                      const int64_t *const *A_arr,const int64_t *const *b_arr,
                      const int64_t *const *lo_arr,const int64_t *const *hi_arr,
                      int64_t **x_arr,PGSResult *res_arr)
{
    if(count<0)return -1;
    if(count==0)return 0;
    if(!opts||!A_arr||!b_arr||!lo_arr||!hi_arr||!x_arr||!res_arr)return -1;
    long total=0;
    for(int k=0;k<count;k++){
        pgsf_solve(&opts[k],A_arr[k],b_arr[k],lo_arr[k],hi_arr[k],x_arr[k],&res_arr[k]);
        if(res_arr[k].flops>LONG_MAX-total)total=LONG_MAX;else total+=res_arr[k].flops;
    }
    return total;
}
