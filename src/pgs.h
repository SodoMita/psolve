#ifndef PSOLVE_PGS_H
#define PSOLVE_PGS_H

/* Projected Gauss-Seidel (PGS) boxed-QP solver.
 *
 * The workhorse for real-time 2D physics (sequential impulses / contact
 * resolution).  Solves
 *
 *      minimize   1/2 x^T A x + b^T x
 *      subject to   lo_i <= x_i <= hi_i        (per-variable box)
 *
 * by iteratively projecting each variable onto its box (Gauss-Seidel with
 * optional over-relaxation).  This converges for symmetric positive
 * semi-definite A (the A matrices that arise from contact constraints are
 * symmetric PSD / P-matrix).
 *
 * Design goals for interactive use:
 *   - small, dense, caller-preallocated (zero malloc in the solve loop)
 *   - deterministic: same input + same iteration budget => same result
 *   - warm-started: pass the previous frame's solution as the initial guess
 *   - bounded work: fixed number of iterations, never spins
 *
 * A is stored column-major n*n (A[j*n + i] = A_{i,j}), symmetric.
 * A and b are read-only; x is read/written (warm start in, result out).
 */

typedef struct {
    int n;                 /* number of variables (size of system) */
    int max_iter;          /* fixed Gauss-Seidel sweep budget */
    double omega;          /* SOR relaxation in (0,2); 1.0 = plain GS */
    double tol;            /* convergence tolerance (0 = run all iters) */
} PGSOptions;

#define PGS_INVALID 2

typedef struct {
    int status;            /* 0 = converged, 1 = iteration limit, 2 = invalid input */
    double obj;            /* final objective value */
    int iters;             /* sweeps performed */
    long flops;            /* approx multiply-adds performed (stats) */
} PGSResult;

/* Solve the boxed QP in place on x (warm start allowed).
 * A is n*n column-major symmetric PSD; lo/hi are the boxes (may be -/+inf
 * via PGS_INF).  Returns result status. */
void pgs_solve(const PGSOptions *opt,
               const double *A, const double *b,
               const double *lo, const double *hi,
               double *x, PGSResult *res);

/* Convenience: A*x evaluation (for tests / energy checks).  y = A x. */
void pgs_matvec(const double *A, int n, const double *x, double *y);

#define PGS_INF 1e30

#endif
