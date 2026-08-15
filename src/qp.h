#ifndef LP_QP_H
#define LP_QP_H

/* Convex quadratic programming via a primal active-set method.
 *
 *   minimize    1/2 x^T Q x + c^T x
 *   subject to        A x  <=  b
 *
 * Q must be symmetric positive semi-definite (convex).  The active-set method
 * is the natural quadratic generalization of the simplex method: it maintains
 * a working set of active inequalities, solves the resulting equality-
 * constrained KKT system for a step direction, and moves to the blocking
 * constraint; when no direction improves the objective it checks the KKT
 * multipliers and drops the most negative one.
 *
 * The KKT system is solved with the dense LU factorization used by the LP
 * solver (src/lu.c).  PSD Q is made positive definite by a tiny regularization.
 *
 * Convexity gate (2026-08-15(8)): before iterating, Q is checked for
 * symmetry (scaled tolerance) and then certified positive semi-definite by a
 * complete symmetrized COMPLETE-PIVOTING elimination scan (refuse on a
 * pivot below -(1e-9 * (1 + max|Q_ij|)), or on an off-diagonal tail entry
 * beyond that tolerance once the remaining diagonal is within it).  The scan
 * is a certificate in both directions in exact arithmetic -- completing it
 * proves PSD; a negative pivot or indefinite principal 2x2 tail proves
 * non-convexity (Sylvester's law) -- and it replaced a 1x1/2x2
 * principal-minor screen that admitted n >= 3 indefinite matrices whose
 * negativity only shows in a larger minor (the active-set then printed the
 * stationary origin as an "optimum" on problems unbounded below; see
 * tools/qp_psd_verify.py).  Semidefiniteness of doubles is decidable only
 * to a relative frontier: below it, near-singular data is accepted and
 * handled by the regularized KKT path (documented tolerance semantics).
 */

typedef struct {
    int n;              /* number of variables */
    int m;              /* number of inequality constraints A x <= b */
    const double *Q;    /* n*n symmetric PSD, column-major: Q[j*n+i] */
    const double *c;    /* n */
    const double *A;    /* m*n, row-major: A[i*n+j] */
    const double *b;    /* m */
    const double *x0;   /* optional feasible start (NULL => use x=0) */
} QP;

#define QP_ITERATION_LIMIT 2  /* active-set iteration cap reached (no cert) */
#define QP_KKT_FAIL        3  /* KKT solve / stationarity residual not verified */
#define QP_NON_CONVEX      4  /* Q is not positive semi-definite / symmetric */
#define QP_INVALID         5  /* NULL or structurally invalid model */
#define QP_STOPPED         6  /* cooperative abort (time limit / Ctrl-C) */

typedef struct {
    int status;         /* 0 solved, -1 no feasible start, 1 unbounded,
                           2 QP_ITERATION_LIMIT, 3 QP_KKT_FAIL,
                           4 QP_NON_CONVEX, 5 QP_INVALID,
                           6 QP_STOPPED (time limit / Ctrl-C; best incumbent
                           in x, feasibility NOT certified for a stop during
                           the Phase-I feasibility search -- see x/NULL) */
    int n;
    double *x;          /* solution (n); NULL if a stop during Phase-I left no
                           feasible point to hand back */
    double *mult;       /* Lagrange multipliers for A x <= b (m) */
    double obj;         /* optimal objective value (or best incumbent on stop) */
    int iterations;     /* active-set iterations */
} QPResult;

/* Solve the QP.  Fill *res (call qp_result_free when done). */
void qp_solve(const QP *qp, QPResult *res);
void qp_result_free(QPResult *res);

#endif
