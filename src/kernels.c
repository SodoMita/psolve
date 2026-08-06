#include "kernels.h"
#include <immintrin.h>

/* ------------------------------------------------------------------ */
/* k_daxpy:  y += alpha*x                                              */
/* ------------------------------------------------------------------ */
void k_daxpy(const double *restrict x, double alpha,
             double *restrict y, long n)
{
#if defined(__AVX512F__)
    const __m512d a = _mm512_set1_pd(alpha);
    long i = 0;
    for (; i + 8 <= n; i += 8) {
        __m512d xv = _mm512_loadu_pd(x + i);
        __m512d yv = _mm512_loadu_pd(y + i);
        _mm512_storeu_pd(y + i, _mm512_fmadd_pd(a, xv, yv));
    }
    for (; i < n; i++) y[i] += alpha * x[i];
#elif defined(__AVX2__) && defined(__FMA__)
    const __m256d a = _mm256_set1_pd(alpha);
    long i = 0;
    for (; i + 4 <= n; i += 4) {
        __m256d xv = _mm256_loadu_pd(x + i);
        __m256d yv = _mm256_loadu_pd(y + i);
        _mm256_storeu_pd(y + i, _mm256_fmadd_pd(a, xv, yv));
    }
    for (; i < n; i++) y[i] += alpha * x[i];
#else
    long i;
    for (i = 0; i < n; i++) y[i] += alpha * x[i];
#endif
}

/* ------------------------------------------------------------------ */
/* k_ddot:  x . y                                                      */
/* ------------------------------------------------------------------ */
double k_ddot(const double *restrict x, const double *restrict y, long n)
{
#if defined(__AVX512F__)
    __m512d acc = _mm512_setzero_pd();
    long i = 0;
    for (; i + 8 <= n; i += 8) {
        __m512d xv = _mm512_loadu_pd(x + i);
        __m512d yv = _mm512_loadu_pd(y + i);
        acc = _mm512_fmadd_pd(xv, yv, acc);
    }
    double r = _mm512_reduce_add_pd(acc);
    for (; i < n; i++) r += x[i] * y[i];
    return r;
#elif defined(__AVX2__)
    __m256d acc = _mm256_setzero_pd();
    long i = 0;
    for (; i + 4 <= n; i += 4) {
        __m256d xv = _mm256_loadu_pd(x + i);
        __m256d yv = _mm256_loadu_pd(y + i);
        acc = _mm256_fmadd_pd(xv, yv, acc);
    }
    __m128d lo = _mm256_castpd256_pd128(acc);
    __m128d hi = _mm256_extractf128_pd(acc, 1);
    double r = _mm_cvtsd_f64(_mm_add_pd(lo, hi)) +
               _mm_cvtsd_f64(_mm_shuffle_pd(_mm_add_pd(lo, hi),
                                            _mm_add_pd(lo, hi), 1));
    for (; i < n; i++) r += x[i] * y[i];
    return r;
#else
    double r = 0.0;
    long i;
    for (i = 0; i < n; i++) r += x[i] * y[i];
    return r;
#endif
}

/* ------------------------------------------------------------------ */
/* k_dsdot_sparse: sparse-vector dot with hyper-sparsity skip          */
/* ------------------------------------------------------------------ */
double k_dsdot_sparse(const double *restrict y,
                      const int *restrict col, const double *restrict val,
                      long nnz, double ytol)
{
    double r = 0.0;
    long k;
    for (k = 0; k < nnz; k++) {
        int row = col[k];
        double yv = y[row];
        /* hyper-sparsity: skip multiply when y[row] is (near) zero */
        if (yv >= ytol || yv <= -ytol)
            r += yv * val[k];
    }
    return r;
}
