#include "solver.h"
#include "lu.h"
#include "kernels.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#define TOL_FEAS 1e-9
#define TOL_PIV  1e-12

static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) { fprintf(stderr, "out of memory\n"); exit(1); }
    return p;
}

/* forward decls */
static void get_col(const Solver *s, int var, double *out);
static void refactorize(Solver *s);
static void recompute_basic(Solver *s);

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */
Solver *solver_create(const LP *lp)
{
    int i, j, k;
    int n = lp->n, m = lp->m;
    int maximize = lp->maximize;

    /* row signs: mlt[i] in {-1,+1} so that mlt[i]*b[i] >= 0 */
    int *mlt = (int*)xmalloc(m * sizeof(int));

    /* count slacks */
    int nslack = 0;
    for (i = 0; i < m; i++) if (lp->rel[i] != '=') nslack++;

    int N = n + nslack + m;          /* orig + slack + artificial */

    Solver *s = (Solver*)xmalloc(sizeof(Solver));
    memset(s, 0, sizeof(Solver));
    s->N = N; s->n_orig = n; s->M = m;
    s->reinvert_interval = 100;
    s->hyper_tol = 0.0;
    s->use_sparse = 0;      /* decided per-basis in refactorize */
    /* Global decision: sparse LU only pays off on genuinely sparse problems.
       On dense problems its fill-in makes it both slower and less stable than
       dense LU, so disable it up front. */
    {
        double dens = (n > 0 && m > 0) ? (double)lp->Acolptr[n] / (double)((long)n * m) : 0.0;
        s->sparse_disabled = (dens > 0.05) ? 1 : 0;
    }
    s->sparse_ok = 0;
    memset(&s->splu, 0, sizeof(s->splu));
    s->splu.pivot_tol = 1e-13;

    s->l = (double*)xmalloc(N * sizeof(double));
    s->u = (double*)xmalloc(N * sizeof(double));
    s->cobj = (double*)xmalloc(N * sizeof(double));
    s->c0 = (double*)xmalloc(N * sizeof(double));
    s->basis = (int*)xmalloc(m * sizeof(int));
    s->basispos = (int*)xmalloc(N * sizeof(int));
    s->status = (char*)xmalloc(N * sizeof(char));
    memset(s->basispos, -1, N * sizeof(int));
    memset(s->status, LP_REMOVED, N * sizeof(char));
    s->x = (double*)xmalloc(N * sizeof(double));
    s->rc = (double*)xmalloc(N * sizeof(double));
    s->cB = (double*)xmalloc(m * sizeof(double));
    s->lu = (double*)xmalloc((size_t)m * m * sizeof(double));
    s->piv = (int*)xmalloc(m * sizeof(int));
    s->eta_piv = (int*)xmalloc(sizeof(int));
    s->eta = (double**)xmalloc(sizeof(double*));
    s->eta_cap = 1; s->eta_count = 0;
    s->eta[0] = NULL;
    s->y = (double*)xmalloc(m * sizeof(double));
    s->d = (double*)xmalloc(m * sizeof(double));
    s->v = (double*)xmalloc(m * sizeof(double));
    s->xb = (double*)xmalloc(m * sizeof(double));
    s->w = (double*)xmalloc(N * sizeof(double));
    s->vw = (double*)xmalloc(m * sizeof(double));
    s->piw = (double*)xmalloc(m * sizeof(double));
    for (j = 0; j < N; j++) s->w[j] = 1.0;
    s->slackVar = (int*)xmalloc(m * sizeof(int));
    s->artVar = (int*)xmalloc(m * sizeof(int));
    s->beq = (double*)xmalloc(m * sizeof(double));
    for (i = 0; i < m; i++) s->beq[i] = fabs(lp->b[i]);

    /* determine mlt and transformed rhs */
    for (i = 0; i < m; i++) mlt[i] = (lp->b[i] >= 0) ? 1 : -1;

    /* build variable-to-column: we need to know the slack/artificial index per row.
       Assign original cols 0..n-1, slacks n..n+nslack-1, artificials n+nslack..N-1 */
    int slack_idx = n;
    int art_idx = n + nslack;
    for (i = 0; i < m; i++) {
        if (lp->rel[i] != '=') { s->slackVar[i] = slack_idx++; }
        else s->slackVar[i] = -1;
        s->artVar[i] = art_idx++;
    }

    /* count nnz: original entries (only nonzero) + 1 per slack + 1 per artificial */
    long nnz = 0;
    for (k = 0; k < lp->Acolptr[n]; k++) nnz++;   /* all stored original entries */
    nnz += nslack + m;

    s->nnz = nnz;
    s->colptr = (int*)xmalloc((N + 1) * sizeof(int));
    s->row = (int*)xmalloc(nnz * sizeof(int));
    s->val = (double*)xmalloc(nnz * sizeof(double));

    /* Fill column pointers by a first pass, then fill. Simplest: build in order. */
    long pos = 0;
    /* original columns */
    for (j = 0; j < n; j++) {
        s->colptr[j] = (int)pos;
        for (k = lp->Acolptr[j]; k < lp->Acolptr[j + 1]; k++) {
            int r = lp->Arow[k];
            double a = lp->Aval[k] * mlt[r];     /* apply row scaling */
            if (a != 0.0) {
                s->row[pos] = r;
                s->val[pos] = a;
                pos++;
            }
        }
    }
    /* slack columns */
    for (i = 0; i < m; i++) {
        int sv = s->slackVar[i];
        if (sv >= 0) {
            s->colptr[sv] = (int)pos;
            double coeff = (double)mlt[i] * (lp->rel[i] == '<' ? 1.0 : -1.0);
            s->row[pos] = i; s->val[pos] = coeff; pos++;
        }
    }
    /* artificial columns: added to the already-scaled equality with coeff +1
       so that initial artificial value |b_i| gives a feasible start. */
    for (i = 0; i < m; i++) {
        int av = s->artVar[i];
        s->colptr[av] = (int)pos;
        s->row[pos] = i; s->val[pos] = 1.0; pos++;
    }
    s->colptr[N] = (int)pos;
    s->nnz = pos;

    /* scratch for building the sparse basis each reinversion */
    s->bBp = (int*)xmalloc((m + 1) * sizeof(int));
    s->bBi = (int*)xmalloc((size_t)s->nnz * sizeof(int));
    s->bBx = (double*)xmalloc((size_t)s->nnz * sizeof(double));
    s->bcap = s->nnz;

    /* bounds and objective and initial values */
    for (j = 0; j < n; j++) {
        s->l[j] = lp->l[j];
        s->u[j] = lp->u[j];
        s->c0[j] = maximize ? lp->c[j] : -lp->c[j];
        s->x[j] = (lp->l[j] > -LP_INF) ? lp->l[j] : lp->u[j];
    }
    for (j = n; j < N; j++) {
        s->l[j] = 0.0;
        s->u[j] = LP_INF;
        s->x[j] = 0.0;
        s->c0[j] = 0.0;
    }
    /* Phase I objective: all zero except -1 for artificials */
    for (j = 0; j < N; j++) s->cobj[j] = 0.0;
    for (i = 0; i < m; i++) s->cobj[s->artVar[i]] = -1.0;

    /* Initial basis: a '<=' row's slack is feasible at |b|, use it; otherwise
       use the artificial column.  Phase I only runs if an artificial enters. */
    s->needs_phase1 = 0;
    s->negate_obj = maximize ? 0 : 1;
    for (i = 0; i < m; i++) {
        int sv = s->slackVar[i];
        if (sv >= 0 && lp->rel[i] == '<') {
            s->basis[i] = sv;
            s->basispos[sv] = i;
            s->status[sv] = LP_BASIC;
            s->x[sv] = fabs(lp->b[i]);
        } else {
            int av = s->artVar[i];
            s->basis[i] = av;
            s->basispos[av] = i;
            s->status[av] = LP_BASIC;
            s->x[av] = fabs(lp->b[i]);
            s->needs_phase1 = 1;
        }
    }

    for (j = 0; j < n; j++) {
        s->basispos[j] = -1;
        if (s->l[j] > -LP_INF) s->status[j] = LP_NBL;
        else s->status[j] = LP_NBU;
    }
    for (j = n; j < n + nslack; j++) {
        if (s->basispos[j] >= 0) continue;   /* slack already basic */
        s->basispos[j] = -1;
        s->status[j] = LP_NBL;
    }
    for (i = 0; i < m; i++) {
        int av = s->artVar[i];
        if (s->status[av] != LP_BASIC) { s->status[av] = LP_NBL; s->x[av] = 0.0; }
    }

    /* finalize: every non-basic variable must have basispos = -1 */
    for (j = 0; j < N; j++)
        if (s->status[j] != LP_BASIC) s->basispos[j] = -1;

    s->phase = 1;
    s->lu_valid = 0;
    s->bland = 0; s->flat = 0; s->last_obj = -LP_INF;
    s->iters = 0;
    s->status_out = 0;
    s->objval = 0.0;
    s->hyper_tol = 0.0;

    /* initial refactorization */
    refactorize(s);

    free(mlt);
    return s;
}

void solver_destroy(Solver *s)
{
    if (!s) return;
    for (int i = 0; i < s->eta_cap; i++) free(s->eta[i]);
    free(s->eta); free(s->eta_piv);
    free(s->l); free(s->u); free(s->cobj); free(s->c0); free(s->basis); free(s->basispos);
    free(s->status); free(s->x); free(s->rc); free(s->cB);
    free(s->lu); free(s->piv); free(s->y); free(s->d); free(s->v); free(s->xb);
    free(s->slackVar); free(s->artVar); free(s->beq);
    free(s->w); free(s->vw); free(s->piw);
    free(s->colptr); free(s->row); free(s->val);
    splu_free(&s->splu);
    free(s->bBp); free(s->bBi); free(s->bBx);
    free(s);
}

/* ------------------------------------------------------------------ */
/* Basis helpers                                                       */
/* ------------------------------------------------------------------ */
static void get_col(const Solver *s, int var, double *out)
{
    memset(out, 0, s->M * sizeof(double));
    for (int k = s->colptr[var]; k < s->colptr[var + 1]; k++)
        out[s->row[k]] = s->val[k];
}

/* build the current basis into a sparse CSC and factor it */
static void refactorize(Solver *s)
{
    int M = s->M;
    /* Decide sparse vs dense from the current basis' non-zero count.  Sparse
       LU pays off only for genuinely sparse bases; on denser bases the fill-in
       makes it slower than dense, so use dense there.  Once disabled (an
       unstable or rejected factorization) it stays dense. */
    if (!s->sparse_disabled) {
        long bnnz = 0;
        for (int _i = 0; _i < M; _i++)
            bnnz += (long)(s->colptr[s->basis[_i]+1] - s->colptr[s->basis[_i]]);
        s->use_sparse = (M >= 40) && (bnnz <= 8L * M);
    }
    if (s->use_sparse) {
        long pos = 0;
        s->bBp[0] = 0;
        for (int i = 0; i < M; i++) {
            int var = s->basis[i];
            for (int k = s->colptr[var]; k < s->colptr[var + 1]; k++) {
                s->bBi[pos] = s->row[k];
                s->bBx[pos] = s->val[k];
                pos++;
            }
            s->bBp[i + 1] = (int)pos;
        }
        splu_free(&s->splu);
        memset(&s->splu, 0, sizeof(s->splu));
        s->splu.pivot_tol = 1e-13;
        s->sparse_ok = (splu_factor(&s->splu, s->bBp, s->bBi, s->bBx, M) == 0);
        s->eta_count = 0;
        if (!s->sparse_ok) {
            /* the sparse factorization was numerically unstable: switch to the
               robust dense path for the remainder of the solve */
            s->sparse_disabled = 1;
            s->use_sparse = 0;
            for (int i = 0; i < M; i++) {
                int var = s->basis[i];
                memset(s->lu + (size_t)i * M, 0, M * sizeof(double));
                for (int k = s->colptr[var]; k < s->colptr[var + 1]; k++)
                    s->lu[(size_t)i * M + s->row[k]] = s->val[k];
            }
            s->lu_valid = (lu_factor(s->lu, M, s->piv) == 0);
        }
    } else {
        for (int i = 0; i < M; i++) {
            int var = s->basis[i];
            memset(s->lu + (size_t)i * M, 0, M * sizeof(double));
            for (int k = s->colptr[var]; k < s->colptr[var + 1]; k++)
                s->lu[(size_t)i * M + s->row[k]] = s->val[k];
        }
        s->lu_valid = (lu_factor(s->lu, M, s->piv) == 0);
        s->eta_count = 0;
    }
    /* steepest-edge weights: reset to the reference framework */
    for (int _j = 0; _j < s->N; _j++) s->w[_j] = 1.0;
}

static void push_eta(Solver *s, int pslot, const double *d)
{
    int M = s->M;
    if (s->eta_count >= s->eta_cap) {
        s->eta_cap *= 2;
        s->eta_piv = (int*)realloc(s->eta_piv, s->eta_cap * sizeof(int));
        s->eta = (double**)realloc(s->eta, s->eta_cap * sizeof(double*));
        for (int i = s->eta_cap / 2; i < s->eta_cap; i++) s->eta[i] = NULL;
    }
    if (!s->eta[s->eta_count]) s->eta[s->eta_count] = (double*)xmalloc(M * sizeof(double));
    double *e = s->eta[s->eta_count];
    int idx = s->eta_count;
    s->eta_piv[idx] = pslot;
    double dp = d[pslot];
    double inv = 1.0 / dp;
    /* E = I + (f - e_p) e_p^T  with  f_p = 1/d_p,  f_i = -d_i/d_p (i != p).
       This is M^{-1} where M has column p = d; B'^{-1} = E B^{-1}. */
    for (int i = 0; i < M; i++)
        e[i] = (i == pslot) ? inv : (-d[i] * inv);
    s->eta_count++;
}

/* apply eta idx to vector w:  w <- E w.
 * E = I + (f - e_p) e_p^T, so (E w)[i] = w[i] + f[i]*w[p] (i!=p), (E w)[p]=f[p]*w[p]. */
static void apply_eta(const Solver *s, int idx, double *w)
{
    int M = s->M;
    int p = s->eta_piv[idx];
    const double *e = s->eta[idx];
    double wp = w[p];
    k_daxpy(e, wp, w, p);
    k_daxpy(e + p + 1, wp, w + p + 1, (long)(M - p - 1));
    w[p] = e[p] * wp;
}

/* apply transpose of eta idx to vector y:  y <- E^T y.
 * E^T = I + e_p(f - e_p)^T, so (E^T y)[p] = f . y  and all other components
 * are unchanged.  Used by BTRAN. */
static void apply_eta_t(const Solver *s, int idx, double *y)
{
    int M = s->M;
    int p = s->eta_piv[idx];
    const double *e = s->eta[idx];
    double dot = k_ddot(e, y, M);
    y[p] = dot;
}

/* FTRAN:  solve B x = rhs, result in x (in-place).  B^{-1} = E_t...E_1 B0^{-1}. */
static void ftran(Solver *s, double *x)
{
    if (s->use_sparse && s->sparse_ok) {
        splu_solve(&s->splu, x, x);
    } else {
        if (s->lu_valid) lu_solve(s->lu, s->piv, s->M, x, x);
    }
    for (int i = 0; i < s->eta_count; i++) apply_eta(s, i, x);
}

/* BTRAN:  solve B^T y = rhs, result in y (in-place).
 * B^{-T} = B0^{-T} E_1^T ... E_t^T, so etas apply in reverse order. */
static void btrans(Solver *s, double *y)
{
    for (int i = s->eta_count - 1; i >= 0; i--) apply_eta_t(s, i, y);
    if (s->use_sparse && s->sparse_ok) {
        splu_solve_t(&s->splu, y, y);
    } else {
        if (s->lu_valid) lu_solve_t(s->lu, s->piv, s->M, y, y);
    }
}

/* compute pricing vector y and reduced costs for all nonbasic vars */
static void price(Solver *s)
{
    int M = s->M, N = s->N;
    for (int i = 0; i < M; i++) s->cB[i] = s->cobj[s->basis[i]];
    memcpy(s->y, s->cB, M * sizeof(double));
    btrans(s, s->y);
    /* max|y| for hyper-sparsity threshold */
    double ymax = 0.0;
    for (int i = 0; i < M; i++) { double a = fabs(s->y[i]); if (a > ymax) ymax = a; }
    double ytol = s->hyper_tol * (1.0 + ymax);
    for (int j = 0; j < N; j++) {
        if (s->status[j] == LP_BASIC || s->status[j] == LP_REMOVED) continue;
        double rc = s->cobj[j] - k_dsdot_sparse(s->y,
                              s->row + s->colptr[j], s->val + s->colptr[j],
                              (long)(s->colptr[j+1] - s->colptr[j]), ytol);
        s->rc[j] = rc;
    }
}

/* pick entering variable.
 * Default: steepest edge (Goldfarb-Reid), maximizing |reduced cost| /
 * sqrt(weight) among improving variables.  If Bland's rule is active
 * (anti-cycling), pick the lowest index with a strictly improving cost. */
static int pick_entering(const Solver *s)
{
    int N = s->N;
    int q = -1;
    double best = 0.0;
    if (s->bland) {
        for (int j = 0; j < N; j++) {
            char st = s->status[j];
            if (st == LP_BASIC || st == LP_REMOVED) continue;
            if (s->u[j] - s->l[j] <= TOL_FEAS) continue;
            double rc = s->rc[j];
            int cand = (st == LP_NBL && rc > 0.0) || (st == LP_NBU && rc < 0.0);
            if (cand) return j;   /* lowest index */
        }
        return -1;
    }
    for (int j = 0; j < N; j++) {
        char st = s->status[j];
        if (st == LP_BASIC || st == LP_REMOVED) continue;
        if (s->u[j] - s->l[j] <= TOL_FEAS) continue;   /* fixed var */
        double rc = s->rc[j];
        double score;   /* improving reduced cost normalized by sqrt(weight) */
        if (st == LP_NBL) {
            if (rc <= 0.0) continue;
            score = rc / sqrt(s->w[j] < 1e-30 ? 1e-30 : s->w[j]);
        } else { /* NBU */
            if (rc >= 0.0) continue;
            score = (-rc) / sqrt(s->w[j] < 1e-30 ? 1e-30 : s->w[j]);
        }
        if (score > best) { best = score; q = j; }
    }
    return q;
}

/* ratio test (Harris two-pass). Returns:
   0 = normal pivot, 1 = bound flip (q changes status, no basis change),
   2 = unbounded */
static int ratio_test(const Solver *s, int q, int dir, const double *d,
                      double *theta_out, int *blocker, int *blockSlot)
{
    int M = s->M;
    double *v = s->v;
    /* Increasing x_q (dir=+1) changes basic vars by -B^-1 a_q; decreasing
       (dir=-1) by +B^-1 a_q.  d = B^-1 a_q, so v = -dir * d. */
    for (int i = 0; i < M; i++) v[i] = -dir * d[i];

    double theta = LP_INF;
    *blocker = q;
    *blockSlot = -1;
    int blockIsBasic = 0;

    /* q's own other bound */
    double qlim = (dir > 0) ? (s->u[q] - s->l[q]) : (s->u[q] - s->l[q]);
    if (s->u[q] - s->l[q] < LP_INF) { theta = qlim; }

    for (int i = 0; i < M; i++) {
        int bv = s->basis[i];
        double vv = v[i];
        if (vv > TOL_PIV) {
            double r = (s->u[bv] - s->x[bv]) / vv;
            if (r < theta) { theta = r; *blocker = bv; *blockSlot = i; blockIsBasic = 1; }
        } else if (vv < -TOL_PIV) {
            double r = (s->x[bv] - s->l[bv]) / (-vv);
            if (r < theta) { theta = r; *blocker = bv; *blockSlot = i; blockIsBasic = 1; }
        }
    }
    if (theta >= LP_INF - 1.0) return 2;   /* unbounded */

    /* Harris pass 2: among candidates within relax of min, pick largest |v| */
    if (blockIsBasic) {
        double relax = 1e-9 * (1.0 + fabs(theta));
        double bestv = 0.0; int bestslot = -1;
        for (int i = 0; i < M; i++) {
            int bv = s->basis[i];
            double vv = v[i];
            double r;
            if (vv > TOL_PIV) r = (s->u[bv] - s->x[bv]) / vv;
            else if (vv < -TOL_PIV) r = (s->x[bv] - s->l[bv]) / (-vv);
            else continue;
            if (r <= theta + relax && fabs(vv) > bestv) { bestv = fabs(vv); bestslot = i; }
        }
        if (bestslot >= 0) {
            *blockSlot = bestslot;
            *blocker = s->basis[bestslot];
            double vv = v[bestslot];
            *theta_out = (vv > 0) ? (s->u[*blocker] - s->x[*blocker]) / vv
                                  : (s->x[*blocker] - s->l[*blocker]) / (-vv);
            return 0;
        }
    }
    *theta_out = theta;
    if (!blockIsBasic) return 1;   /* q flips to other bound */
    return 0;
}

/* Goldfarb-Reid steepest-edge weight update.  Must be called with the
 * pre-pivot basis (before push_eta).  q enters, slot p leaves. */
static void update_steepest_edge(Solver *s, int q, int p)
{
    int M = s->M, N = s->N;
    double wq = 0.0;
    for (int i = 0; i < M; i++) wq += s->d[i] * s->d[i];
    double aq = s->d[p];
    if (fabs(aq) < 1e-300) aq = (aq < 0) ? -1e-300 : 1e-300;
    /* v = B^{-T} d */
    memcpy(s->vw, s->d, (size_t)M * sizeof(double));
    btrans(s, s->vw);
    /* pi_p = B^{-T} e_p */
    memset(s->piw, 0, (size_t)M * sizeof(double));
    s->piw[p] = 1.0;
    btrans(s, s->piw);
    for (int j = 0; j < N; j++) {
        char st = s->status[j];
        if (st == LP_BASIC || st == LP_REMOVED) continue;
        if (j == q) continue;
        if (s->u[j] - s->l[j] <= TOL_FEAS) continue;
        long nnz = (long)(s->colptr[j+1] - s->colptr[j]);
        double alpha = k_dsdot_sparse(s->piw, s->row + s->colptr[j], s->val + s->colptr[j], nnz, 0.0);
        double ajv   = k_dsdot_sparse(s->vw,   s->row + s->colptr[j], s->val + s->colptr[j], nnz, 0.0);
        double t = alpha / aq;
        double nj = s->w[j] - 2.0 * t * ajv + t * t * wq;
        if (nj < 1e-30) nj = 1e-30;
        if (nj > 1e18) nj = 1e18;
        s->w[j] = nj;
    }
    /* the variable leaving the basis (slot p) becomes nonbasic */
    s->w[s->basis[p]] = 1.0 + wq;
}

/* one simplex iteration. returns 0=optimal,1=continue,2=unbounded */
static int iterate(Solver *s)
{
    int M = s->M;

    price(s);
    int q = pick_entering(s);
    if (q < 0) return 0;


    /* FTRAN */
    get_col(s, q, s->d);
    ftran(s, s->d);

    int dir = (s->status[q] == LP_NBL) ? 1 : -1;

    double theta; int blocker, blockSlot;
    int rr = ratio_test(s, q, dir, s->d, &theta, &blocker, &blockSlot);
    if (rr == 2) {
        /* "Unbounded" is only valid if the factorization is accurate.  The
           sparse path can, on ill-conditioned bases, produce a wrong FTRAN
           direction and falsely report unboundedness.  Fall back to the
           dense factorization once and retry before concluding the LP is
           genuinely unbounded. */
        if (s->use_sparse && s->sparse_ok) {
            s->sparse_disabled = 1;
            s->use_sparse = 0;
            refactorize(s);
            recompute_basic(s);
            return 1;   /* continue iterating with the dense basis */
        }
        return 2;
    }

    /* update basic values and q */
    if (dir > 0) s->x[q] = s->l[q] + theta; else s->x[q] = s->u[q] - theta;
    for (int i = 0; i < M; i++) s->x[s->basis[i]] += s->v[i] * theta;

    /* steepest-edge weight update (needs the pre-pivot basis) */
    if (rr == 0) update_steepest_edge(s, q, blockSlot);

    s->iters++;

    /* anti-cycling: track objective improvement; if it stagnates, switch to
       Bland's rule (guarantees termination of the phase). */
    {
        double obj = 0.0;
        for (int _j = 0; _j < s->N; _j++)
            if (s->status[_j] != LP_REMOVED) obj += s->cobj[_j] * s->x[_j];
        if (obj > s->last_obj + 1e-9 * (1.0 + fabs(s->last_obj))) s->flat = 0;
        else s->flat++;
        s->last_obj = obj;
        if (s->flat > 500) s->bland = 1;
    }

    if (rr == 1) {
        /* q flips to its other bound, no basis change */
        s->status[q] = (dir > 0) ? LP_NBU : LP_NBL;
        return 1;
    }

    /* normal pivot: blocker (slot blockSlot) leaves, q enters */
    int p = blockSlot;
    int bv = s->basis[p];
    /* clamp blocker to bound and set status */
    if (s->v[p] > 0) { s->x[bv] = s->u[bv]; s->status[bv] = LP_NBU; }
    else             { s->x[bv] = s->l[bv]; s->status[bv] = LP_NBL; }

    s->status[q] = LP_BASIC;
    s->basis[p] = q;
    s->basispos[q] = p;
    s->basispos[bv] = -1;

    /* product-form update */
    push_eta(s, p, s->d);

    if (s->eta_count >= s->reinvert_interval) {
        refactorize(s);
        /* reset the primal basic values from the nonbasic values so the
           factorization always starts from an exactly feasible point,
           which prevents numerical drift from producing false results. */
        recompute_basic(s);
    }
    return 1;
}

/* after Phase I: mark nonbasic artificial variables as removed so Phase II
   pricing ignores them. */
static void cleanup_phase1(Solver *s)
{
    for (int i = 0; i < s->M; i++) {
        int av = s->artVar[i];
        if (s->status[av] != LP_BASIC) s->status[av] = LP_REMOVED;
    }
}

/* artificial variable indices are the top M of the variable space:
   [N-M, N).  An artificial left basic at 0 would absorb infeasibility in
   Phase II, so pivot it out by swapping in any nonbasic column that has a
   nonzero in that constraint row. */
static void remove_basic_artificials(Solver *s)
{
    int M = s->M;
    int first_art = s->N - M;
    for (int i = 0; i < M; i++) {
        int bv = s->basis[i];
        if (bv < first_art) continue;              /* not an artificial */
        int r = bv - first_art;                    /* row it serves */
        int cand = -1;
        for (int j = 0; j < s->N; j++) {
            if (j >= first_art) continue;          /* skip artificials */
            if (s->status[j] == LP_BASIC || s->status[j] == LP_REMOVED) continue;
            /* does column j have a nonzero at row r? */
            for (int k = s->colptr[j]; k < s->colptr[j + 1]; k++) {
                if (s->row[k] == r && s->val[k] != 0.0) { cand = j; break; }
            }
            if (cand >= 0) break;
        }
        if (cand >= 0) {
            s->basis[i] = cand;
            s->basispos[cand] = i;
            s->basispos[bv] = -1;
            s->status[cand] = LP_BASIC;
            s->status[bv] = LP_REMOVED;
        }
        /* if no candidate, the row is (numerically) redundant; the artificial
           stays basic at 0 as a degenerate slack. */
    }
}

/* Recompute the primal basic values from the current nonbasic values so the
   point is exactly feasible for the (artificial-free) basis.  Requires that
   the basis has already been refactorized (eta file empty). */
static void recompute_basic(Solver *s)
{
    int M = s->M;
    double *rhs = (double*)xmalloc(M * sizeof(double));
    for (int i = 0; i < M; i++) rhs[i] = s->beq[i];
    for (int j = 0; j < s->N; j++) {
        if (s->status[j] == LP_BASIC || s->status[j] == LP_REMOVED) continue;
        double xj = s->x[j];
        if (xj == 0.0) continue;
        for (int k = s->colptr[j]; k < s->colptr[j + 1]; k++)
            rhs[s->row[k]] -= s->val[k] * xj;
    }
    ftran(s, rhs);
    for (int i = 0; i < M; i++) s->x[s->basis[i]] = rhs[i];
    free(rhs);
}

static int solve_phase(Solver *s)
{
    int r;
    long cap = 2000000;   /* safety cap against degeneracy cycling */
    while ((r = iterate(s)) == 1) {
        if (s->iters > cap) { r = -1; break; }   /* cycling / no convergence */
    }
    return r;
}

int solver_solve(Solver *s)
{
    int r = 0;
    /* Phase I: only needed if artificials are in the initial basis */
    if (s->needs_phase1) {
        s->phase = 1;
        r = solve_phase(s);
        if (r == 2) { s->status_out = 2; return 2; }
        if (r != 0) { s->status_out = 1; return 1; }

        /* check feasibility: any artificial left basic at positive value */
        double sum = 0.0;
        for (int i = 0; i < s->M; i++) {
            int av = s->artVar[i];
            if (s->status[av] == LP_BASIC) sum += s->x[av];
        }
        if (sum > 1e-6) { s->status_out = 1; return 1; }  /* infeasible */
        remove_basic_artificials(s);
    }

    cleanup_phase1(s);

    /* switch to Phase II objective: true objective for originals, 0 else */
    memcpy(s->cobj, s->c0, s->N * sizeof(double));
    s->phase = 2;
    s->bland = 0; s->flat = 0; s->last_obj = -LP_INF;
    refactorize(s);

    /* if Phase I ran, re-derive the basic values so Phase II starts feasible */
    if (s->needs_phase1) recompute_basic(s);

    r = solve_phase(s);
    if (r == 2) { s->status_out = 2; return 2; }

    /* objective value */
    double obj = 0.0;
    for (int i = 0; i < s->M; i++) obj += s->cobj[s->basis[i]] * s->x[s->basis[i]];
    for (int j = 0; j < s->N; j++)
        if (s->status[j] != LP_BASIC) obj += s->cobj[j] * s->x[j];
    s->objval = obj;
    s->status_out = 0;
    return 0;
}

void solver_optimum(const Solver *s, double *x_orig, double *obj)
{
    for (int j = 0; j < s->n_orig; j++) x_orig[j] = s->x[j];
    *obj = s->negate_obj ? -s->objval : s->objval;
}

/* Primal-feasibility check of the current solution. */
int solver_feasible(const Solver *s)
{
    int N = s->N, M = s->M;
    const double tol = 1e-6 * (1.0 + fabs(s->objval));
    for (int j = 0; j < N; j++) {
        if (s->status[j] == LP_REMOVED) continue;
        if (s->x[j] < s->l[j] - tol || s->x[j] > s->u[j] + tol) return 0;
    }
    /* A_eq x == beq */
    double *res = (double*)calloc((size_t)M, sizeof(double));
    for (int j = 0; j < N; j++) {
        if (s->status[j] == LP_REMOVED) continue;
        double xj = s->x[j];
        for (int k = s->colptr[j]; k < s->colptr[j+1]; k++)
            res[s->row[k]] += s->val[k] * xj;
    }
    int ok = 1;
    for (int i = 0; i < M; i++)
        if (fabs(res[i] - s->beq[i]) > 1e-5 * (1.0 + fabs(s->beq[i]))) { ok = 0; break; }
    free(res);
    return ok;
}
