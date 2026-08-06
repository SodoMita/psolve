/* kernels.h -- micro-optimized dense linear algebra kernels.
 *
 * These are the arithmetic "hot loops" of the simplex solver: dense LU
 * factorization, triangular solves (BTRAN/FTRAN on the basis), and a
 * hyper-sparse sparse dot product used for PRICE.
 *
 * On x86-64 builds with -march=native we use AVX-512 FMA (or AVX2) via
 * intrinsics.  The vectorized tail is handled explicitly so every kernel
 * is correct for any length, not just multiples of the vector width.
 * There is always a portable scalar fallback behind the same API.
 */
#ifndef LP_KERNELS_H
#define LP_KERNELS_H

#include <stddef.h>

/* daxpy:  y[i] += alpha * x[i]   (i = 0..n-1) */
void k_daxpy(const double *restrict x, double alpha,
             double *restrict y, long n);

/* ddot:  return x . y */
double k_ddot(const double *restrict x, const double *restrict y, long n);

/* dsdot_sparse: sparse-vector dot product used for PRICE.
 *   y is a dense m-vector, col[i] gives row indices of the sparse column,
 *   val[i] the matching values.  Computes sum_i y[col[i]]*val[i].
 *   Skipping rows where |y[row]| is below a tolerance exploits hyper-sparsity
 *   (the reduced-cost vector is often very sparse), which is the single
 *   biggest practical win for hard sparse LP problems.
 */
double k_dsdot_sparse(const double *restrict y,
                      const int *restrict col, const double *restrict val,
                      long nnz, double ytol);

#endif /* LP_KERNELS_H */
