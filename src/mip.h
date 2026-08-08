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
 *
 * IMPORTANT: zero the MIP struct before filling it in
 *
 *     MIP mip; memset(&mip, 0, sizeof(mip));
 *
 * The struct carries optional fields (stop_at_feasible, limits) whose absence
 * must read as 0.  Leaving them uninitialised is undefined behaviour: a
 * garbage stop_at_feasible aborts branch-and-bound at the first integer point
 * found, which is feasible but almost never optimal.
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
    int    stop_at_feasible; /* 1 = return as soon as any integer-feasible
                                solution is found (for solve satisfy); do not
                                keep branching to prove optimality.  The result
                                is then feasible but NOT proven optimal --
                                res->proven_optimal is 0, and callers that care
                                about the objective must check it. */
    long   node_limit;     /* max branch-and-bound nodes */
    long   lp_iter_limit;  /* simplex iteration limit per relaxation */
    int    all_solutions;  /* 1 = find all solutions / report all incumbents */
    void   (*on_solution)(const double *x, double obj, void *user_data);
    void   *solution_user_data;
} MIP;

#define MIP_INVALID 7

typedef struct {
    int status;            /* 0 optimal, 1 infeasible, 2 unbounded, 3 node limit,
                              4 stopped (cooperative abort: time limit / Ctrl-C),
                              5 feasible but not proven optimal (limit reached with
                                an incumbent; NOT a proof of optimality),
                              6 numerical failure (an LP relaxation could not be
                                factorized/certified), 7 invalid model */
    double obj;            /* optimal objective value */
    double *x;             /* optimal integer solution (n) */
    int *isint_sol;        /* reported integrality status per var (0/1) */
    long nodes;            /* nodes explored */
    long lp_iters;         /* total simplex iterations across all nodes */
    double best_bound;     /* best LP-relaxation bound over open nodes */
    int    proven_optimal; /* 1 only if the branch-and-bound tree was exhausted
                              (or emptied by pruning), so `obj` is a proven
                              optimum.  0 whenever the search was cut short --
                              node/iteration limit, cooperative stop, or
                              stop_at_feasible.  status==0 with
                              proven_optimal==0 means "this point is feasible",
                              never "this point is optimal". */
} MIPResult;

void mip_solve(const MIP *mip, MIPResult *res);
void mip_result_free(MIPResult *res);

#endif
