#ifndef PSOLVE_FX_H
#define PSOLVE_FX_H

/* Fixed-point (exact rational) LP solver.
 *
 * The floating-point `solver.c` LP core uses `double` and a revised-simplex
 * with numerical tolerances.  This module is the fixed-point analogue: every
 * number is an exact rational (int64-pair numerator/denominator reduced, with
 * __int128 intermediate products), and the LP is solved by an exact two-phase
 * full-tableau simplex.  For problems whose data are integers (or short
 * decimals), the optimum is therefore *exact* and bit-identical across
 * platforms/compilers — the same determinism guarantee the PGS fixed-point
 * kernel already provides for the physics hot loop, now extended to the LP
 * backbone.
 *
 * Cost: exact rational arithmetic is heavier than double SIMD, so this path
 * targets small, data-friendly problems (UI/layout/VG and integer MiniZinc
 * LPs) where exactness and determinism matter more than raw throughput.  The
 * coefficients must stay small enough that __int128 intermediates do not
 * overflow; very ill-conditioned large instances should keep using solver.c.
 *
 * It reads the same simple `.lp` format as `lpsolve` (see docs) and reports
 * the exact optimum plus the same status vocabulary: OPTIMAL / INFEASIBLE /
 * UNBOUNDED / ITERATION_LIMIT.
 */

typedef struct {
    long long num, den;   /* den>0, reduced (num/den is the exact value) */
} Fx;

typedef struct {
    int n, m;
    int maximize;
    Fx *c;                /* n objective coefficients */
    char *rel;            /* m rows: '<' '>' '=' */
    Fx *b;                /* m rhs */
    Fx *l, *u;            /* n bounds (LP_INF-like sentinels via finite flags) */
    int *lfinite, *ufinite;/* whether the bound is finite */
    Fx *A;                /* dense m*n row-major */
} FxLP;

typedef struct {
    int status;           /* 0 optimal, 1 infeasible, 2 unbounded, 3 iter limit */
    Fx obj;               /* exact objective value */
    Fx *x;                /* n original-variable values (exact) */
    long iters;           /* phase I + phase II pivots */
} FxResult;

/* Read an LP file (same format as lp_read).  Returns 0 on success, -1 on
 * parse error (message to stderr).  The model owns all memory. */
int  fx_read(const char *path, FxLP *lp);

/* Solve with exact rational two-phase simplex.  Fills *res (owner must call
 * fx_result_free).  Returns res->status. */
int  fx_solve(const FxLP *lp, FxResult *res);

void fx_result_free(FxResult *res);
void fx_free(FxLP *lp);

/* Helpers shared with the CLI / benchmark. */
Fx  fx_from_ll(long long v);
double fx_todouble(Fx r);
/* Print the exact value as a decimal with `prec` digits (round-half-even not
 * needed; long division).  Returns the number of chars written. */
int fx_fmt(char *buf, int buflen, Fx r, int prec);

#endif
