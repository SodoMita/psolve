#ifndef LP_LU_H
#define LP_LU_H

/* Dense LU factorization (row partial pivoting) of an m x m matrix and
 * forward/backward substitution, used to (re-)invert the basis matrix B.
 *
 * The factorization is stored in-place in a single m*m column-major array
 * `a` (upper triangle holds U, strictly-lower triangle holds the multipliers
 * of L with implicit unit diagonal).  `piv` records the row permutation such
 * that P*A = L*U.
 *
 * Between reinversions the basis is not refactored; the product-form-of-the-
 * inverse eta file keeps B^{-1} current, and these solves only run when the
 * eta file is empty (i.e. right after INVERT).
 */

/* In-place LU with partial pivoting. Returns 0 on success, -1 if singular. */
int lu_factor(double *restrict a, int m, int *restrict piv);

/* Solve A x = b  (a already factored).  x may alias b. */
void lu_solve(const double *restrict a, const int *piv, int m,
              const double *b, double *x);

/* Solve A^T x = b  (a already factored).  x may alias b. */
void lu_solve_t(const double *restrict a, const int *piv, int m,
                const double *b, double *x);

#endif
