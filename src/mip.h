#ifndef PSOLVE_MIP_H
#define PSOLVE_MIP_H

#include "solver.h"

/* Mixed-integer programming via branch-and-bound.
 *
 * Solves an LP with a subset of variables required to be integer:
 *
 *   optimize  c^T x
 *   s.t.      A x (rel) b,  l <= x <= u
 *   x[j] integer for j in integer set
 *
 * The LP relaxation is solved with the revised-simplex solver; branch-and-bound
 * then enforces integrality by splitting on fractional integer variables and
 * pruning nodes whose relaxation bound cannot beat the incumbent.
 *
 * rel in {'<','>','='} per row.  The LP is specified through the same LP struct
 * the simplex solver uses, plus an array marking integer variables.
 */

typedef struct {
    int n;                 /* number of variables */
    int m;                 /* number of constraints */
    const double *c;       /* objective (n) */
    const int *Acolptr;    /* CSC column pointers (n+1) */
    const int *Arow;       /* CSC rows (nnz) */
    const double *Aval;    /* CSC values (nnz) */
    const char *rel;       /* row relations (m), '<' '>' '=' */
    const double *b;       /* rhs (m) */
    const double *l, *u;   /* bounds (n), use LP_INF for unbounded */
    int maximize;
    const unsigned char *isint;  /* 1 if variable j must be integer */
    double mip_gap;        /* relative optimality gap to stop at (e.g. 1e-4) */
    long   node_limit;     /* max branch-and-bound nodes */
    long   lp_iter_limit;  /* simplex iteration limit per relaxation */
} MIP;

typedef struct {
    int status;            /* 0 optimal, 1 infeasible, 2 unbounded, 3 node limit */
    double obj;            /* optimal objective value */
    double *x;             /* optimal integer solution (n) */
    int *isint_sol;        /* reported integrality status per var (0/1) */
    long nodes;            /* nodes explored */
    long lp_iters;         /* total simplex iterations across all nodes */
} MIPResult;

void mip_solve(const MIP *mip, MIPResult *res);
void mip_result_free(MIPResult *res);

#endif
