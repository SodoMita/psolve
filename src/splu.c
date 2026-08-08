#include "splu.h"
#include "err.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Sparse right-looking LU with partial row pivoting and a column      */
/* ordering by increasing column degree (cheap Markowitz approximation).*/
/*                                                                     */
/*   P * B * Q = L * U                                                 */
/*     P : row permutation,  factor row i  ->  original row rperm[i]   */
/*     Q : column perm,      factor col j  ->  original col qinv[j]    */
/*                                                                     */
/* The working matrix is kept in ORIGINAL row space (no physical row   */
/* swaps), so stored L multipliers stay valid; the pivot permutation is*/
/* recorded in rperm/rinv and applied only during the solves.          */
/* ------------------------------------------------------------------ */

typedef struct {
    int m, n, cap;
    int *row_of, *col_of, *row_next, *col_next;
    double *val_of;
    int *row_head, *col_head;
    char *elim_row;
} Work;

static void work_init(Work *w, int m, int cap)
{
    w->m = m; w->n = 0; w->cap = cap;
    w->row_of  = (int*)psolve_malloc((size_t)cap * sizeof(int));
    w->col_of  = (int*)psolve_malloc((size_t)cap * sizeof(int));
    w->row_next= (int*)psolve_malloc((size_t)cap * sizeof(int));
    w->col_next= (int*)psolve_malloc((size_t)cap * sizeof(int));
    w->val_of  = (double*)psolve_malloc((size_t)cap * sizeof(double));
    w->row_head= (int*)psolve_malloc((size_t)m * sizeof(int));
    w->col_head= (int*)psolve_malloc((size_t)m * sizeof(int));
    w->elim_row= (char*)psolve_calloc((size_t)m, 1);
    for (int i = 0; i < m; i++) { w->row_head[i] = -1; w->col_head[i] = -1; }
}

static void work_grow(Work *w)
{
    int ncap = w->cap * 2 + 16;
    w->row_of   = (int*)psolve_realloc((void**)&w->row_of,   (size_t)ncap * sizeof(int));
    w->col_of   = (int*)psolve_realloc((void**)&w->col_of,   (size_t)ncap * sizeof(int));
    w->row_next = (int*)psolve_realloc((void**)&w->row_next, (size_t)ncap * sizeof(int));
    w->col_next = (int*)psolve_realloc((void**)&w->col_next, (size_t)ncap * sizeof(int));
    w->val_of   = (double*)psolve_realloc((void**)&w->val_of,(size_t)ncap * sizeof(double));
    w->cap = ncap;
}

static void work_insert(Work *w, int r, int c, double v)
{
    if (w->n >= w->cap) work_grow(w);
    int e = w->n++;
    w->row_of[e] = r; w->col_of[e] = c; w->val_of[e] = v;
    w->row_next[e] = w->row_head[r]; w->row_head[r] = e;
    w->col_next[e] = w->col_head[c]; w->col_head[c] = e;
}

static int work_find(const Work *w, int r, int c)
{
    for (int e = w->row_head[r]; e != -1; e = w->row_next[e])
        if (w->col_of[e] == c) return e;
    return -1;
}

static void work_update(Work *w, int r, int c, double delta, double tol)
{
    int e = work_find(w, r, c);
    if (e != -1) {
        double v = w->val_of[e] + delta;
        if (fabs(v) <= tol) {
            int *p = &w->row_head[r];
            while (*p != -1 && *p != e) p = &w->row_next[*p];
            if (*p == e) *p = w->row_next[e];
            int *q = &w->col_head[c];
            while (*q != -1 && *q != e) q = &w->col_next[*q];
            if (*q == e) *q = w->col_next[e];
        } else {
            w->val_of[e] = v;
        }
    } else {
        if (fabs(delta) > tol) work_insert(w, r, c, delta);
    }
}

static void work_free(Work *w)
{
    free(w->row_of); free(w->col_of); free(w->row_next); free(w->col_next);
    free(w->val_of); free(w->row_head); free(w->col_head); free(w->elim_row);
}

int splu_factor(SPLU *s, const int *Bp, const int *Bi, const double *Bx, int m)
{
    if(!s||m<0)return -1;
    if(m==0)return 0;
    if(!Bp||!Bi||!Bx)return -1;
    double tol = s->pivot_tol > 0 ? s->pivot_tol : 1e-14;

    /* column order by increasing column degree */
    int *colorder = (int*)psolve_malloc((size_t)m * sizeof(int));
    int *colpos   = (int*)psolve_malloc((size_t)m * sizeof(int));
    for (int c = 0; c < m; c++) colorder[c] = c;
    for (int i = 1; i < m; i++) {
        int key = colorder[i];
        long nnzkey = Bp[key+1] - Bp[key];
        int j = i - 1;
        while (j >= 0 && (long)(Bp[colorder[j]+1] - Bp[colorder[j]]) > nnzkey) {
            colorder[j+1] = colorder[j]; j--;
        }
        colorder[j+1] = key;
    }
    for (int k = 0; k < m; k++) colpos[colorder[k]] = k;

    int cap = 64;
    for (int c = 0; c < m; c++) cap += (int)(Bp[c+1] - Bp[c]);
    Work w; work_init(&w, m, cap);
    double maxA = 0.0;
    for (int c = 0; c < m; c++)
        for (int k = Bp[c]; k < Bp[c+1]; k++) {
            double v = Bx[k];
            if (fabs(v) > maxA) maxA = fabs(v);
            if (fabs(v) > tol) work_insert(&w, Bi[k], c, v);
        }
    if (maxA <= 0) maxA = 1.0;
    /* instability detection: fall back to the (robust) dense path if the
       relative pivot is too small or the multipliers grow too large. */
    double pivot_rel = 1e-9 * maxA;   /* relative pivot threshold */
    double growth_limit = 1e10;       /* max allowable multiplier magnitude */

    s->m = m;
    s->piv  = (int*)psolve_malloc((size_t)m * sizeof(int));   /* rperm */
    s->qinv = (int*)psolve_malloc((size_t)m * sizeof(int));
    s->udiag= (double*)psolve_malloc((size_t)m * sizeof(double));
    int *Lp  = (int*)psolve_malloc((size_t)(m+1) * sizeof(int));
    int *Urp = (int*)psolve_malloc((size_t)(m+1) * sizeof(int));
    int lcap = cap, ucap = cap, liN = 0, urN = 0;
    int *Li  = (int*)psolve_malloc((size_t)lcap * sizeof(int));
    double *Lx  = (double*)psolve_malloc((size_t)lcap * sizeof(double));
    int *Urj  = (int*)psolve_malloc((size_t)ucap * sizeof(int));
    double *Urx = (double*)psolve_malloc((size_t)ucap * sizeof(double));
#define GROW_L() do { if (liN >= lcap) { int nc=lcap*2+16; Li=(int*)psolve_realloc((void**)&Li,(size_t)nc*sizeof(int)); Lx=(double*)psolve_realloc((void**)&Lx,(size_t)nc*sizeof(double)); lcap=nc; } } while (0)
#define GROW_U() do { if (urN >= ucap) { int nc=ucap*2+16; Urj=(int*)psolve_realloc((void**)&Urj,(size_t)nc*sizeof(int)); Urx=(double*)psolve_realloc((void**)&Urx,(size_t)nc*sizeof(double)); ucap=nc; } } while (0)

    int ok = 0;
    for (int k = 0; k < m && ok == 0; k++) {
        int c = colorder[k];
        /* choose pivot: max |A[pr,c]| among uneliminated original rows */
        int pe = -1; double piv = 0.0;
        for (int e = w.col_head[c]; e != -1; e = w.col_next[e]) {
            int r = w.row_of[e];
            if (!w.elim_row[r] && fabs(w.val_of[e]) > fabs(piv)) { piv = w.val_of[e]; pe = e; }
        }
        if (pe == -1 || fabs(piv) <= tol || fabs(piv) <= pivot_rel) { ok = -1; break; }
        int pr = w.row_of[pe];          /* original row */
        s->piv[k] = pr;
        s->qinv[k] = c;
        s->udiag[k] = piv;
        w.elim_row[pr] = 1;

        /* U row k: pivot row (orig pr) entries in uneliminated columns */
        Urp[k] = urN;
        for (int e = w.row_head[pr]; e != -1; e = w.row_next[e]) {
            int pos = colpos[w.col_of[e]];
            if (pos >= k) {
                GROW_U();
                Urj[urN] = pos; Urx[urN] = w.val_of[e]; urN++;
            }
        }

        /* L column k: multipliers for other uneliminated rows in column c */
        Lp[k] = liN;
        for (int e = w.col_head[c]; e != -1; e = w.col_next[e]) {
            int i = w.row_of[e];
            if (i != pr && !w.elim_row[i]) {
                double lv = w.val_of[e] / piv;
                if (fabs(lv) > growth_limit) { ok = -1; break; }
                GROW_L();
                Li[liN] = i; Lx[liN] = lv; liN++;
            }
        }
        if (ok == -1) break;

        /* eliminate column c */
        for (int e = w.col_head[c]; e != -1; e = w.col_next[e]) {
            int i = w.row_of[e];
            if (w.elim_row[i]) continue;
            double f = w.val_of[e] / piv;
            for (int e2 = w.row_head[pr]; e2 != -1; e2 = w.row_next[e2]) {
                int c2 = w.col_of[e2];
                if (colpos[c2] <= k) continue;
                work_update(&w, i, c2, -f * w.val_of[e2], tol);
            }
        }
    }
#undef GROW_L
#undef GROW_U

    Lp[m] = liN; Urp[m] = urN;

    if (ok == 0) {
        /* build U column storage from U rows */
        int *colcnt = (int*)psolve_calloc((size_t)m, sizeof(int));
        for (int k = 0; k < m; k++)
            for (int t = Urp[k]; t < Urp[k+1]; t++) colcnt[Urj[t]]++;
        int *Ucp = (int*)psolve_malloc((size_t)(m+1) * sizeof(int));
        Ucp[0] = 0;
        for (int j = 0; j < m; j++) Ucp[j+1] = Ucp[j] + colcnt[j];
        int *Uci = (int*)psolve_malloc((size_t)(urN+1) * sizeof(int));
        double *Ucx = (double*)psolve_malloc((size_t)(urN+1) * sizeof(double));
        int *fill = (int*)psolve_malloc((size_t)(m+1) * sizeof(int));
        memcpy(fill, Ucp, (size_t)(m+1) * sizeof(int));
        for (int k = 0; k < m; k++)
            for (int t = Urp[k]; t < Urp[k+1]; t++) {
                int j = Urj[t];
                Uci[fill[j]] = k; Ucx[fill[j]] = Urx[t]; fill[j]++;
            }
        free(fill); free(colcnt);
        s->Lp = Lp; s->Li = Li; s->Lx = Lx;
        s->Urp = Urp; s->Urj = Urj; s->Urx = Urx;
        s->Ucp = Ucp; s->Uci = Uci; s->Ucx = Ucx;
    } else {
        free(Lp); free(Li); free(Lx);
        free(Urp); free(Urj); free(Urx);
        free(s->piv); free(s->qinv); free(s->udiag);
        s->piv = s->qinv = NULL; s->udiag = NULL;
    }

    free(colorder); free(colpos);
    work_free(&w);
    return ok;
}

void splu_free(SPLU *s)
{
    if (!s) return;
    free(s->piv); free(s->qinv); free(s->udiag);
    free(s->Lp); free(s->Li); free(s->Lx);
    free(s->Urp); free(s->Urj); free(s->Urx);
    free(s->Ucp); free(s->Uci); free(s->Ucx);
    memset(s, 0, sizeof(*s));
}

/* ------------------------------------------------------------------ */
/* Hyper-sparse triangular solves.  rinv maps original row -> factor   */
/* row (built on the fly in a caller scratch is not stored, so we      */
/* recompute it here from piv).                                        */
/* ------------------------------------------------------------------ */

/* Build inverse of piv: invpiv[orig_row] = factor row.  Caller provides
   a scratch buffer of size m. */
static void build_rinv(const int *piv, int m, int *rinv)
{
    for (int i = 0; i < m; i++) rinv[piv[i]] = i;
}

void splu_solve(const SPLU *s, const double *b, double *x)
{
    if(!s||!b||!x||s->m<=0)return;
    int m = s->m;
    const double tol = 1e-14;
    int *rinv = (int*)psolve_malloc((size_t)m * sizeof(int));
    build_rinv(s->piv, m, rinv);
    /* y = P b : y[i] = b[rperm[i]] */
    for (int i = 0; i < m; i++) x[i] = b[s->piv[i]];
    /* L z = y (unit lower). L column k holds orig-row multipliers. */
    for (int k = 0; k < m; k++) {
        double zk = x[k];
        if (zk > tol || zk < -tol)
            for (int t = s->Lp[k]; t < s->Lp[k+1]; t++)
                x[rinv[s->Li[t]]] -= s->Lx[t] * zk;
    }
    /* U w = z (upper), rows of U in factor space */
    for (int k = m - 1; k >= 0; k--) {
        double acc = x[k];
        for (int t = s->Urp[k]; t < s->Urp[k+1]; t++) {
            int j = s->Urj[t];
            if (j != k) acc -= s->Urx[t] * x[j];
        }
        x[k] = acc / s->udiag[k];
    }
    /* x = Q w : x[qinv[j]] = w[j] */
    {
        double *w = (double*)psolve_malloc((size_t)m * sizeof(double));
        memcpy(w, x, (size_t)m * sizeof(double));
        for (int j = 0; j < m; j++) x[s->qinv[j]] = w[j];
        free(w);
    }
    free(rinv);
}

void splu_solve_t(const SPLU *s, const double *b, double *x)
{
    if(!s||!b||!x||s->m<=0)return;
    int m = s->m;
    int *rinv = (int*)psolve_malloc((size_t)m * sizeof(int));
    build_rinv(s->piv, m, rinv);
    /* a = Q^T c : a[j] = c[qinv[j]] */
    for (int j = 0; j < m; j++) x[j] = b[s->qinv[j]];
    /* U^T w = a (forward, U^T lower).  U column i holds (factor row k< i, value). */
    for (int i = 0; i < m; i++) {
        double acc = x[i];
        for (int t = s->Ucp[i]; t < s->Ucp[i+1]; t++) {
            int k = s->Uci[t];
            if (k != i) acc -= s->Ucx[t] * x[k];
        }
        x[i] = acc / s->udiag[i];
    }
    /* L^T v = w (back, L^T upper unit).  L column k holds orig-row multipliers. */
    for (int k = m - 1; k >= 0; k--) {
        double acc = x[k];
        for (int t = s->Lp[k]; t < s->Lp[k+1]; t++)
            acc -= s->Lx[t] * x[rinv[s->Li[t]]];
        x[k] = acc;
    }
    /* y = P^T v : y[piv[i]] = v[i] */
    {
        double *v = (double*)psolve_malloc((size_t)m * sizeof(double));
        memcpy(v, x, (size_t)m * sizeof(double));
        for (int i = 0; i < m; i++) x[s->piv[i]] = v[i];
        free(v);
    }
    free(rinv);
}
