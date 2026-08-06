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

typedef struct {
    int status;         /* 0 solved, -1 no feasible start given, 1 unbounded */
    int n;
    double *x;          /* solution (n) */
    double *mult;       /* Lagrange multipliers for A x <= b (m) */
    double obj;         /* optimal objective value */
    int iterations;     /* active-set iterations */
} QPResult;

/* Solve the QP.  Fill *res (call qp_result_free when done). */
void qp_solve(const QP *qp, QPResult *res);
void qp_result_free(QPResult *res);

#endif
