#ifndef LP_SOLVER_H
#define LP_SOLVER_H

#include "splu.h"

#define LP_BASIC  0
#define LP_NBL    1   /* nonbasic at lower bound */
#define LP_NBU    2   /* nonbasic at upper bound */
#define LP_REMOVED 3

#define LP_INF 1e30

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
    int N;                 /* total variables: orig + slack + artificial */
    int n_orig;            /* original variables */
    int M;                 /* number of rows (equalities) */
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

    /* controls / stats */
    long iters;
    int phase;
    int reinvert_interval;
    double hyper_tol;      /* hyper-sparsity skip threshold for PRICE */
    double objval;
    int status_out;        /* 0 ok, 1 infeasible, 2 unbounded */
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
} Solver;

/* API */
Solver *solver_create(const LP *lp);
void solver_destroy(Solver *s);
/* returns status_out: 0 solved (objval set), 1 infeasible, 2 unbounded */
int solver_solve(Solver *s);
void solver_optimum(const Solver *s, double *x_orig, double *obj);
/* returns 1 if the current solution is primal feasible (bounds + constraints) */
int solver_feasible(const Solver *s);

#endif
