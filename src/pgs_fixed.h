#ifndef PSOLVE_PGS_FIXED_H
#define PSOLVE_PGS_FIXED_H

#include <stdint.h>
#include "pgs.h"   /* reuse PGSResult */

/* Fixed-point (integer) projected Gauss-Seidel boxed-QP solver.
 *
 * The real-time UI / vector-graphics / physics kernels should run on fixed
 * point rather than IEEE floats for:
 *   - deterministic, bit-identical results across platforms & compilers
 *     (no FPU rounding variance; important for network / replay determinism)
 *   - predictable, fast arithmetic on integer-only cores
 *   - reliable comparison and clamping
 *
 * Solves the same problem as pgs_solve but with integer inputs:
 *
 *      minimize   1/2 x^T A x + b^T x
 *      subject to   lo_i <= x_i <= hi_i
 *
 * where every value is an integer in a common fixed-point scale S
 * (real_value = int_value / S).  A stores the raw coefficients that multiply
 * x_real, so:
 *
 *      r_i = b_i + sum_{j != i} A_{i,j} x_j        (all integers, 64-bit/128-bit)
 *      x_i_new = (1-omega) x_i + omega * round(-r_i / A_{i,i})
 *      x_i_new = clamp(x_i_new, lo_i, hi_i)
 *
 * The update divides by the integer diagonal A_{i,i}, which automatically
 * preserves the scale S (both b and x are scaled by S, A is not).  The
 * diagonal must be positive (PSD/PD contact matrices).  Accumulation uses
 * 128-bit intermediate sums so even large contact clusters do not overflow.
 *
 * Determinism: division rounds half away from zero and omega is an exact
 * rational w_num/w_den, so every operation is integer-exact and reproducible.
 */

typedef struct {
    int n;               /* number of variables */
    int max_iter;        /* fixed sweep budget */
    int64_t w_num;       /* SOR relaxation omega = w_num/w_den (0<omega<2) */
    int64_t w_den;       /* set w_num=w_den (or w_num=1,w_den=1) for plain GS */
    int64_t tol;         /* convergence tolerance in scaled units (0 = run all) */
} PGSFixedOptions;

/* Solve the fixed-point boxed QP in place on x (warm start allowed).
 * A is n*n row-major-integer (A[i*n + j] = A_{i,j}); b, lo, hi, x are scaled
 * by the same S.  res carries status / iterations / objective (objective is
 * returned as a double for reporting only; all solving is integer). */
void pgsf_solve(const PGSFixedOptions *opt,
                const int64_t *A, const int64_t *b,
                const int64_t *lo, const int64_t *hi,
                int64_t *x, PGSResult *res);

/* Integer A*x matvec for tests / energy checks.  y = A x (128-bit-safe). */
void pgsf_matvec(const int64_t *A, int n, const int64_t *x, int64_t *y);

/* Fixed-point counterpart of pgs_batch_solve; same return contract. */
long pgsf_batch_solve(int count, const PGSFixedOptions *opts,
                      const int64_t *const *A_arr, const int64_t *const *b_arr,
                      const int64_t *const *lo_arr, const int64_t *const *hi_arr,
                      int64_t **x_arr, PGSResult *res_arr);

#endif
