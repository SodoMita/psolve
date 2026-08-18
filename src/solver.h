#ifndef LP_SOLVER_H
#define LP_SOLVER_H

#include "splu.h"

#define LP_BASIC  0
#define LP_NBL    1   /* nonbasic at lower bound */
#define LP_NBU    2   /* nonbasic at upper bound */
#define LP_REMOVED 3

#define LP_INF 1e30  /* TOLSHEET TOL-LP-INF */
#define SOLVE_STOPPED 4   /* cooperative abort requested (time limit / Ctrl-C) */
#define SOLVE_NUMERICAL 5 /* factorization/certificate failure */
#define SOLVE_INVALID 6   /* NULL or structurally invalid public-API input */

typedef struct {
    int n, m;              /* original variables, constraints */
    double *c;             /* objective, len n */
    int *Acolptr;          /* CSC column pointers, len n+1 */
    int *Arow;             /* CSC rows, len nnz */
    double *Aval;          /* CSC values, len nnz */
    char *rel;             /* len m: '<' '>' '=' */
    double *b;             /* len m */
    double *l, *u;         /* bounds, len n */
    int maximize;
} LP;

typedef struct {
    /* equality-form problem */
    int N;                 /* total variables: normalized decision + slack + artificial */
    int n_orig;            /* caller-visible/original variable count */
    int n_core;            /* normalized decision columns before slack/artificial */
    int M;                 /* number of rows (equalities) */

    /* User-variable normalization.  A fully free x_j is represented internally
       as x_j^+ - x_j^- with both components non-negative.  orig_pos[j] is the
       direct/positive column and orig_neg[j] is the negative component or -1
       for a variable that did not need splitting.  The original objective and
       bounds are retained so incremental APIs can rebuild this mapping if a
       bound update changes a variable between free and non-free. */
    int *orig_pos, *orig_neg;       /* len n_orig */
    double *orig_c, *orig_l, *orig_u; /* len n_orig, user convention */
    int rebuild_pending;
    long nnz;
    int *colptr;           /* N+1 */
    int *row;              /* nnz */
    double *val;           /* nnz */
    double *l, *u;         /* bounds, N */
    double *cobj;          /* active objective (maximize form), N */
    double *c0;            /* true objective for original vars (maximize form), N */

    /* basis */
    int *basis;            /* M  slot -> var */
    int *basispos;         /* N  var -> slot or -1 */
    char *status;          /* N */
    double *x;             /* N current values */
    double *rc;            /* N reduced costs */
    double *cB;            /* M objective of basic vars */

    /* LU factors of the basis (valid right after INVERT) */
    double *lu;            /* M*M (dense path) */
    int *piv;              /* M (dense path) */
    SPLU splu;             /* sparse path */
    int use_sparse;        /* use sparse LU for the basis */
    int sparse_disabled;   /* permanently fall back to dense (once unstable) */
    int sparse_ok;         /* sparse factorization of current basis succeeded */
    int lu_valid;
    int factor_failed;     /* a basis factorization failed this solve: the
                              result cannot be certified, so do not report
                              OPTIMAL/INFEASIBLE/UNBOUNDED (see solver_solve) */
    /* scratch for building the sparse basis CSC each reinversion */
    int *bBp; int *bBi; double *bBx; long bcap;

    /* product-form-of-the-inverse eta file */
    int eta_count;
    int eta_cap;
    int *eta_piv;          /* pivot slot per eta */
    double **eta;          /* M-vector each */

    /* scratch */
    double *y;             /* pricing vector (M) */
    double *d;             /* FTRAN result (M) */
    double *v;             /* dir*d (M) */
    double *xb;            /* basic values scratch (M) */

    int *slackVar;         /* per row: slack var or -1 */
    int *artVar;           /* per row: artificial var */
    double *beq;           /* scaled rhs |b| (equality form), M */
    double *borig;         /* original rhs (with sign), M */
    int *mlt;              /* row sign: +1 if b>=0 else -1, M */
    int *artSign;          /* sign of each artificial's coefficient (+1/-1), M */
    char *rel;             /* per row: original relation '<' '>' '=' */

    /* controls / stats */
    long iters;
    int phase;
    int reinvert_interval;
    double hyper_tol;      /* hyper-sparsity skip threshold for PRICE */
    double objval;
    int status_out;        /* 0 ok, 1 infeasible, 2 unbounded, 3 limit hit,
                              4 SOLVE_STOPPED, 5 SOLVE_NUMERICAL,
                              6 SOLVE_INVALID */
    long iteration_limit;  /* max simplex iterations before giving up */
    /* anti-cycling */
    int bland;             /* use Bland's rule (lowest-index) entering */
    long flat;             /* iterations without objective improvement */
    double last_obj;
    int needs_phase1;      /* any artificial in the initial basis */
    int farkas_ok;         /* the last solver_solve(/warm) returned INFEASIBLE
                              from a certified Phase-I optimum (artsum > tol on
                              the final, certificate-clean basis).  The basis,
                              LU/eta factors and the Phase-I objective (cobj)
                              left behind then describe a genuine Phase-I
                              optimum, so B^{-T} c_B is a valid Farkas-ray
                              HINT -- see solver_farkas_duals.  Cleared at the
                              start of every solve. */
    int negate_obj;        /* original problem was a minimization */
    /* steepest-edge (Goldfarb-Reid) pricing weights */
    double *w;             /* w[j] ~ ||d_j||^2 for nonbasic j */
    double *vw;            /* scratch: v = B^{-T} d */
    double *piw;           /* scratch: pi_p = B^{-T} e_p */
    double *duals;         /* dual (shadow-price) vector, B^{-T} c_B, M */
    /* recession-ray evidence left behind by an UNBOUNDED verdict
       (unb_valid==1): ray_int[q]=unb_dir, ray_int[basis[i]]=s->v[i] at
       detection, exactly the same state the honest "is the factorization
       accurate" dense-retry gate just cleared.  Cleared at solve start. */
    int unb_valid, unb_var, unb_dir;
    double *unb_ray;       /* len N */
} Solver;

/* API */
Solver *solver_create(const LP *lp);
void solver_destroy(Solver *s);
/* returns status_out: 0 solved (objval set), 1 infeasible, 2 unbounded */
int solver_solve(Solver *s);
void solver_optimum(const Solver *s, double *x_orig, double *obj);
/* returns 1 if the current solution is primal feasible (bounds + constraints) */
int solver_feasible(const Solver *s);

/* ------------------------------------------------------------------ */
/* Incremental solving (warm starts).                                  */
/* These let you change part of a solved problem and re-solve starting  */
/* from the previous basis, instead of from scratch.                   */
/* ------------------------------------------------------------------ */

/* Replace the objective coefficients (for the original variables). */
void solver_set_objective(Solver *s, const double *c, int maximize);

/* Replace the lower/upper bounds of the original variables. */
void solver_set_bounds(Solver *s, const double *l, const double *u);

/* Add one inequality/equality row:  a^T x (rel) rhs.  rel in {'<','>','='}.
 * Rebuilds the matrix and warm-starts from the previous basis. */
int solver_add_row(Solver *s, const double *a, double rhs, char rel);

/* Re-solve from the current basis (warm start).  Returns status_out. */
int solver_warm_solve(Solver *s);

/* Sensitivity analysis: dual / shadow prices for the equality-form rows.
 * dual must be an M-vector.  The dual of row i gives the marginal change in
 * the objective per unit change in that constraint's right-hand side. */
void solver_duals(const Solver *s, double *dual);

/* Materialize a certified improving recession ray over the ORIGINAL
 * variables for a solve that returned UNBOUNDED, using the basis state the
 * dense-verified detection left behind: D[q] = dir, D[basic_i] = v_i, then
 * unmapped through the x = x+ - x- split like solver_optimum.  Returns 0 on
 * success (ray filled, n_orig doubles) and -1 when no UNBOUNDED state is
 * available.  Like every solver hint the ray is only evidence: the caller
 * re-verifies point feasibility, row recession, open bound sides and the
 * objective direction against ITS original data (lpsolve does this through
 * the psv certificate layer, cert.h). */
int solver_unbounded_ray(const Solver *s, double *ray);

/* Extract y = B^{-T} c_B at the Phase-I-optimal basis left behind by an
 * INFEASIBLE verdict (farkas_ok == 1): one component per equality row, in
 * the scaled-row space of this struct (multiply component i by s->mlt[i] to
 * obtain the dual of the caller's original row i; mlt is +1/-1 by rhs sign).
 *
 * The returned vector is a HINT, never a certificate on its own: the caller
 * MUST re-verify the Farkas conditions (relation-sign consistency and the
 * y^T A x > y^T b separation over the variable box) against its own original
 * data before using it to prune anything.  A wrong or stale hint can only
 * fail such a check, never fabricate one.  Returns 0 on success, -1 if no
 * Phase-I-infeasible state is available.  Fills y[0..M). */
int solver_farkas_duals(Solver *s, double *y);

/* Directed-rounding Farkas box check over the ORIGINAL rows and a given
 * variable box, O(m + nnz).  `ys` is a candidate dual ray in the solver's
 * scaled-row space (see solver_farkas_duals); `mlt` is the +1/-1 per-row
 * sign.  The complete Farkas conditions are re-verified against the
 * supplied data: relation-sign consistency (violating components are
 * clamped to 0, soundly weakening the ray) and the separation
 * min_box (y^T A)x > y^T b + tol*(1+|R|), with both sides computed under
 * directed rounding (proven corner bounds for the min side, upward for the
 * rhs side).  Any NaN/inf, infinite needed bound, or failed margin makes
 * the check fail ("cannot say") -- it can never fabricate a separation.
 * y/zl/zh are caller scratch of sizes m/n/n.  Returns 1 iff infeasibility
 * of the box system is PROVEN. */
int solver_farkas_boxcert(int n, int m, const int *colptr, const int *row,
                          const double *val, const char *rel, const double *b,
                          const double *lo, const double *hi, const double *ys,
                          const int *mlt, double tol,
                          double *y, double *zl, double *zh);

/* Scale-mix exposure of a box system: max over rows of
 * sum_j |a_ij| * min(max(|lo_j|,|hi_j|), 1e29).  The double phase-1 decides
 * infeasibility against an ABSOLUTE artificial-sum tolerance of 1e-6, while
 * the products feeding its residuals round by up to ~exposure*DBL_EPSILON
 * even before accumulation; once exposure*eps approaches that tolerance the
 * verdict ceases to distinguish infeasibility from rounding noise.  Callers
 * use exposure*DBL_EPSILON >= 5e-7 (half tolerance) as the "shaky"
 * frontier: an UNCERTIFIED infeasibility verdict past it must be downgraded
 * to the honest numerical-failure class rather than pruned/printed. */
double solver_row_exposure(int n, int m, const int *colptr, const int *row,
                           const double *val,
                           const double *lo, const double *hi);

/* Reduced costs for all original variables (c_j - (dual . A_j)); rc must be
 * an n_orig-vector.  For a free x=x+ - x-, this is the reduced cost of the
 * positive/original direction (the negative component has the opposite one). */
void solver_reduced_costs(const Solver *s, double *rc);

#endif
