#ifndef LP_QP_H
#define LP_QP_H

/* Convex quadratic programming via a primal active-set method.
 *
 *   minimize    1/2 x^T Q x + c^T x
 *   subject to        A x  <=  b
 *                     Aeq x == beq   (optional; P1.1)
 *                     l <= x <= u    (optional; P1.1)
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

/* Which Phase-I search runs first; see QP.phase1_order.  Values, not bits, so a
 * caller that never touches the field keeps the historical order whatever it
 * left in that word. */
#define QP_PHASE1_DENSE_FIRST 0
#define QP_PHASE1_LP_FIRST    0x4c5031   /* 'LP1' */

typedef struct {
    int n;              /* number of variables */
    int m;              /* number of inequality constraints A x <= b */
    const double *Q;    /* n*n symmetric PSD, column-major: Q[j*n+i] */
    const double *c;    /* n */
    const double *A;    /* m*n, row-major: A[i*n+j] */
    const double *b;    /* m */
    const double *x0;   /* optional feasible start (NULL => use x=0) */
    int me;             /* number of equality constraints Aeq x = beq */
    const double *Aeq;  /* me*n, row-major: Aeq[i*n+j] (NULL if me == 0) */
    const double *beq;  /* me */
    const double *l;    /* n lower bounds, or NULL (entries kept for the
                         * callers that allocate l but want a partial bound:
                         * a value <= -LP_INF/2 is treated as no lower bound) */
    const double *u;    /* n upper bounds, or NULL (a value >= LP_INF/2 is
                         * treated as no upper bound) */
    int phase1_order;     /* which Phase-I search runs first: 0 or
                           * QP_PHASE1_DENSE_FIRST (the default) tries the dense
                           * auxiliary QP, QP_PHASE1_LP_FIRST tries the sparse LP
                           * and falls back to the dense search.  A magic value
                           * rather than a boolean on purpose: the struct is
                           * extensible, some callers assign field by field
                           * instead of zero-initialising it, and a stale or
                           * uninitialised word must not be able to silently
                           * change which algorithm runs (it would silently change
                           * *which models get solved*, which is worse than a
                           * crash).  Any value other than the magic means the
                           * default.  Measured on the final tree, both orders
                           * answer the same layout models (ui_qp_probe sections
                           * 3-5 are identical to within noise) and LP-first
                           * answers fewer random models: tools/qp_diff.py 400
                           * 99001 checks 311 of its models with this default and
                           * 290 with LP-first.  It also exposed one model where
                           * the flatness band for an UNBOUNDED verdict is too
                           * generous (docs/CURV_PS_PLAN.md 1.12), so prefer the
                           * default unless the caller re-checks verdicts. */
} QP;

/* P1.2 sparse input: the shape a constraint-layout front end actually has.
 *   minimize  1/2 x'(D + sum_k w_k v_k v_k') x + c'x
 *   subject to A x <= b   (A in CSC, m x n)
 *             Aeq x = beq (dense, optional)
 *             l <= x <= u
 *  D is the dense/ridge diagonal, and each rank-1 term is a sparse vector v_k
 *  (q_rk_colptr[k]..q_rk_colptr[k+1] into q_rk_rowi/q_rk_val).  q_solve_sparse
 *  materialises the dense Q/A that the active set consumes, so the bridge never
 *  asks a host to marshal n^2 doubles per frame.  Everything except A/b may be
 *  NULL (nq = 0 means no rank-1 terms; q_diag NULL means zero diagonal). */
typedef struct {
    int n;                 /* number of variables */
    int m;                 /* number of inequalities (0 allowed) */
    const double *c;       /* n */
    const double *b;       /* m (A x <= b) */
    const double *x0;      /* optional feasible start */
    int phase1_order;
    int me;                /* number of equalities */
    const double *Aeq;     /* me*n row-major (NULL if me == 0) */
    const double *beq;     /* me */
    const double *l, *u;   /* bounds, same semantics as QP */
    /* A in CSC, column-major over columns j = 0..n-1 (rows are the constraints). */
    const int *Acolptr;    /* n+1 */
    const int *Arow;       /* nnz */
    const double *Aval;    /* nnz */
    /* Q = q_diag + sum_k q_w[k] * v_k v_k^T */
    const double *q_diag;  /* n, or NULL */
    int nq;                /* number of rank-1 terms */
    const double *q_w;     /* nq, or NULL when nq == 0 */
    const int *q_rk_colptr;/* nq+1, or NULL when nq == 0 */
    const int *q_rk_rowi;  /* total nnz of the rank-1 vectors, or NULL */
    const double *q_rk_val;/* total nnz of the rank-1 vectors, or NULL */
} QPSparse;

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
    double *mult_eq;    /* Lagrange multipliers for Aeq x = beq (me), NULL when
                           the model has no native equalities */
    double *mult_l;     /* lower-bound multipliers (n; 0 when a variable is
                           not at its lower bound), NULL when qp->l == NULL */
    double *mult_u;     /* upper-bound multipliers (n), NULL when qp->u == NULL */
    double *ray;        /* certified recession direction (n), only filled
                           when status == 1 (unbounded); NULL otherwise.
                           Verifiable against the original data: A d <= 0,
                           d^T Q d ~ 0, (Q x + c)^T d < 0 (and the point x
                           above is itself primal feasible) */
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
 * relative feasibility tolerance (1e-11 * (1 + |b_i| + |a_i|^T|x|)), else 0.
 * Same test the QP uses to accept a caller-supplied qp->x0 warm start, so a
 * front end can gate its own "reuse last frame's solution" logic on it. */
/* Contract for callers: the QP struct is extensible (fields are appended), so
 * build it as `QP q; memset(&q, 0, sizeof q);` or with designated initialisers
 * rather than assigning members one by one -- qp_solve reads every field,
 * including ones a particular caller was written before. */
int qp_start_feasible(const QP *qp, const double *x);

/* Solve the QP.  Fill *res (call qp_result_free when done). */
void qp_solve(const QP *qp, QPResult *res);
/* Solve the QP from the sparse P1.2 form.  Same result contract as qp_solve;
 * the dense Q/A buffers are internal and released before returning. */
void qp_solve_sparse(const QPSparse *s, QPResult *res);
/* Release res->x, res->mult and res->farkas, then ZERO the struct (so a reused
 * QPResult cannot double-free).  Read status/obj/max_resid/farkas *before*
 * calling this -- afterwards res->status is 0, which reads as OPTIMAL. */
void qp_result_free(QPResult *res);

#endif
