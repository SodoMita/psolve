#include "lu.h"
#include "kernels.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* Column-major indexing helper. */
#define A(i, j) a[(j) * m + (i)]

/* ------------------------------------------------------------------ */
/* In-place LU with partial pivoting (LAPACK-style dgetrf).            */
/* Vectorized with daxpy over each column segment.                     */
/* ------------------------------------------------------------------ */
int lu_factor(double *restrict a, int m, int *restrict piv)
{
    if (!a || !piv || m <= 0) return -1;
    for (int k = 0; k < m; k++) {
        /* find pivot: max |a[i][k]|, i >= k */
        int imax = k;
        double best = fabs(A(k, k));
        for (int i = k + 1; i < m; i++) {
            double v = fabs(A(i, k));
            if (v > best) { best = v; imax = i; }
        }
        if (best < 1e-300) return -1;   /* singular */
        piv[k] = imax;

        if (imax != k) {
            /* swap rows k and imax for columns k..m-1 (and the factor part) */
            for (int j = 0; j < m; j++) {
                double t = A(k, j); A(k, j) = A(imax, j); A(imax, j) = t;
            }
        }

        const double pivot = A(k, k);
        /* multipliers for rows k+1..m-1, column k */
        for (int i = k + 1; i < m; i++) A(i, k) /= pivot;

        /* update trailing submatrix: A[i][j] -= A[i][k]*A[k][j] */
        const double *colk = a + (size_t)k * m;    /* column k base */
        double *Ak = a + (size_t)k * m;            /* row k (column-major stride m) */
        /* row k stored with stride m in column-major; build explicit row k */
        /* We update column j (j>k): A[i][j] -= A[i][k]*A[k][j]  for i>k.
           A[k][j] is at A(k,j). Column k multipliers at A(i,k) for i>k.  */
        for (int j = k + 1; j < m; j++) {
            double akj = A(k, j);
            if (akj == 0.0) continue;
            double *colj = a + (size_t)j * m;
            k_daxpy(colk + (k + 1), -akj, colj + (k + 1), (long)(m - k - 1));
        }
        (void)Ak;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Solve A x = b with factored a (LU with partial pivot).              */
/*  1) apply row permutation P to b                                    */
/*  2) forward substitution  L y = P b   (unit diagonal)               */
/*  3) back substitution     U x = y                                   */
/* ------------------------------------------------------------------ */
/* Solve A x = b with factored a (LU with row partial pivoting).
 * piv is a sequence of row transpositions performed during factorization
 * (LAPACK convention); apply them to the RHS in forward order, then
 * forward-substitute with L, then back-substitute with U. */
/* ------------------------------------------------------------------ */
void lu_solve(const double *restrict a, const int *restrict piv, int m,
              const double *restrict b, double *restrict x)
{
    if (!a || !piv || !b || !x || m <= 0) return;
    double *y = x;
    for (int i = 0; i < m; i++) y[i] = b[i];
    for (int k = 0; k < m; k++) {
        if (piv[k] != k) { double t = y[k]; y[k] = y[piv[k]]; y[piv[k]] = t; }
    }
    /* forward substitution L y = permuted b.  L_ii = 1 implicit.
       L stored in strictly-lower part: A(i,k) for i>k. */
    for (int k = 0; k < m; k++) {
        const double *colk = a + (size_t)k * m;   /* column k = L multipliers */
        const double yk = y[k];
        if (yk != 0.0) k_daxpy(colk + (k + 1), -yk, y + (k + 1), (long)(m - k - 1));
    }
    /* back substitution U x = y.  U stored in upper triangle incl. diag. */
    for (int k = m - 1; k >= 0; k--) {
        double s = y[k];
        for (int j = k + 1; j < m; j++) {
            double ukj = A(k, j);
            if (ukj != 0.0) s -= ukj * x[j];
        }
        x[k] = s / A(k, k);
    }
}

/* ------------------------------------------------------------------ */
/* Solve A^T x = b.
 *  1) solve U^T z = b    (forward)
 *  2) solve L^T w = z    (backward, L unit diagonal)
 *  3) apply the recorded row transpositions in REVERSE order
 *     (LAPACK convention for the transposed solve). */
/* ------------------------------------------------------------------ */
void lu_solve_t(const double *restrict a, const int *restrict piv, int m,
                const double *restrict b, double *restrict x)
{
    if (!a || !piv || !b || !x || m <= 0) return;
    double *z = x;   /* working buffer */

    /* step 1: U^T z = b.  (U^T)_{i,j}=U(j,i). Forward substitution:
       z_i = (b_i - sum_{k<i} U(k,i) z_k)/U(i,i). */
    for (int i = 0; i < m; i++) {
        double s = b[i];
        for (int k = 0; k < i; k++) {
            double uki = A(k, i);
            if (uki != 0.0) s -= uki * z[k];
        }
        z[i] = s / A(i, i);
    }
    /* step 2: L^T w = z.  (L^T)_{i,j}=L(j,i), unit diag. Back substitution:
       w_i = z_i - sum_{j>i} L(j,i) w_j. */
    for (int i = m - 1; i >= 0; i--) {
        double s = z[i];
        for (int j = i + 1; j < m; j++) {
            double lji = A(j, i);
            if (lji != 0.0) s -= lji * x[j];
        }
        x[i] = s;
    }
    /* step 3: apply transpositions in reverse order */
    for (int k = m - 1; k >= 0; k--) {
        if (piv[k] != k) { double t = x[k]; x[k] = x[piv[k]]; x[piv[k]] = t; }
    }
}
