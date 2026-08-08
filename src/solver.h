#ifndef LP_SOLVER_H
#define LP_SOLVER_H

#include "splu.h"

#define LP_BASIC  0
#define LP_NBL    1   /* nonbasic at lower bound */
#define LP_NBU    2   /* nonbasic at upper bound */
#define LP_REMOVED 3

#define LP_INF 1e30
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
    int negate_obj;        /* original problem was a minimization */
    /* steepest-edge (Goldfarb-Reid) pricing weights */
    double *w;             /* w[j] ~ ||d_j||^2 for nonbasic j */
    double *vw;            /* scratch: v = B^{-T} d */
    double *piw;           /* scratch: pi_p = B^{-T} e_p */
    double *duals;         /* dual (shadow-price) vector, B^{-T} c_B, M */
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

/* Reduced costs for all original variables (c_j - (dual . A_j)); rc must be
 * an n_orig-vector.  For a free x=x+ - x-, this is the reduced cost of the
 * positive/original direction (the negative component has the opposite one). */
void solver_reduced_costs(const Solver *s, double *rc);

#endif
