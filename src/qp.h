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
 * Phase-I (finding a feasible start) runs on the LP core: `min sum s` s.t.
 * `A x - s <= b, s >= 0` through the revised simplex (src/solver.c), so the QP
 * needs the LP objects at link time.  That route is both much cheaper than a
 * dense auxiliary QP and *certifiable*: when it proves the row system empty it
 * hands back a Farkas multiplier vector (`QPResult.farkas`) when it can prove
 * the row system empty.  Note what that does and does not mean for status -1:
 * -1 always says "no feasible start was found", and `infeasible_proven` is what
 * upgrades it from a give-up to a proof -- psolve never reports infeasibility
 * without one, because a feasibility search that fails says nothing about
 * whether a point exists.  If the LP declines to certify (numerical trouble in
 * its own basis), the dense-QP feasibility search stays as a fallback, so the
 * QP still solves models the simplex chokes on, and nothing is claimed.
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
    double max_resid;   /* max_i (a_i^T x - b_i) at the returned x, in the
                           CALLER's units (0 when x satisfies Ax <= b).  A host
                           that caches x and re-feeds it as qp->x0 next frame
                           uses this to decide whether the point is still
                           usable; qp_start_feasible() is the same test without
                           solving. */
    int infeasible_proven;  /* 1 => status -1 is a PROOF, not a give-up:
                               {x : Ax <= b} is empty, certified by `farkas`.
                               0 => the feasibility search simply failed to find
                               a start (the system may well be feasible). */
    double *farkas;     /* m-vector lambda, normalised to max |lambda| = 1, with
                           lambda >= 0, A^T lambda ~ 0 and b^T lambda < 0, i.e.
                           a Farkas certificate of infeasibility.  Rows with
                           lambda_i > 0 are the conflicting ones (a usable
                           conflict set for a constraint front end).
                           NULL unless infeasible_proven.  Freed by
                           qp_result_free(). */
} QPResult;

/* Returns 1 when x satisfies every row a_i^T x <= b_i within psolve's
 * relative feasibility tolerance (1e-9 * (1 + |b_i| + |a_i|^T|x|)), else 0.
 * Same test the QP uses to accept a caller-supplied qp->x0 warm start, so a
 * front end can gate its own "reuse last frame's solution" logic on it. */
int qp_start_feasible(const QP *qp, const double *x);

/* Solve the QP.  Fill *res (call qp_result_free when done). */
void qp_solve(const QP *qp, QPResult *res);
/* Release res->x, res->mult and res->farkas, then ZERO the struct (so a reused
 * QPResult cannot double-free).  Read status/obj/max_resid/farkas *before*
 * calling this -- afterwards res->status is 0, which reads as OPTIMAL. */
void qp_result_free(QPResult *res);

#endif
