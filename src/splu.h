#ifndef LP_SPLU_H
#define LP_SPLU_H

/* Sparse LU factorization of a sparse square matrix with:
 *   - column ordering by increasing column degree (fill-reducing heuristic,
 *     an inexpensive approximation of Markowitz),
 *   - partial row pivoting for numerical stability,
 *   - factors stored sparsely for cache-friendly, hyper-sparse triangular
 *     solves.
 *
 * Factorization:  P * B * Q = L * U
 *   P = row permutation:  factor row i  ->  original row  s->piv[i]
 *   Q = column perm:      factor col j  ->  original col  s->qinv[j]
 *   L = unit lower triangular (stored by columns)
 *   U = upper triangular    (stored by rows and by columns)
 *
 * The basis matrix B is supplied in CSC form (Bp, Bi, Bx, m columns).
 */

typedef struct {
    int m;
    int *piv;          /* factor row -> original row */
    int *qinv;         /* factor col -> original col */
    /* L unit lower triangular, stored by columns */
    int *Lp, *Li;  double *Lx;
    /* U upper triangular, stored by rows */
    int *Urp, *Urj;  double *Urx;
    /* U upper triangular, stored by columns (for transposed solves) */
    int *Ucp, *Uci;  double *Ucx;
    double *udiag;   /* diagonal of U, udiag[k] */
    double pivot_tol;
} SPLU;

/* Factor the m x m sparse matrix in CSC.  Returns 0 on success, -1 if a
 * pivot below pivot_tol is encountered (near-singular). */
int splu_factor(SPLU *s, const int *Bp, const int *Bi, const double *Bx, int m);

/* Solve B x = b.  x may alias b. */
void splu_solve(const SPLU *s, const double *b, double *x);

/* Solve B^T x = b.  x may alias b. */
void splu_solve_t(const SPLU *s, const double *b, double *x);

void splu_free(SPLU *s);

#endif
