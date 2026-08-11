#include "solver.h"
#include "lu.h"
#include "kernels.h"
#include "err.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <limits.h>

#define TOL_FEAS 1e-9
#define TOL_PIV  1e-12
/* Dual (reduced-cost) tolerance.  A reduced cost at rounding level is *not* an
   improving direction: pivoting on one produces a meaningless search direction
   that can end in a bogus "unbounded" ray or in cycling.  Only a reduced cost
   that clears this tolerance counts as improving. */
#define TOL_DJ   1e-9

static void *xmalloc(size_t n) {
    return psolve_malloc(n);   /* signals PSOLVE_ERR_OOM instead of exit() */
}

/* forward decls */
static void get_col(const Solver *s, int var, double *out);
static void refactorize(Solver *s);
static void recompute_basic(Solver *s);
static void solver_reset_to_initial(Solver *s);

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */
/* Construct the simplex tableau for an LP whose decision variables already
 * each have at least one finite bound.  The public solver_create() below
 * normalizes fully free caller variables before reaching this routine. */
static Solver *solver_create_internal(const LP *lp)
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
    s->N = N; s->n_orig = n; s->n_core = n; s->M = m;
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
    s->duals = (double*)xmalloc(m * sizeof(double));
    for (j = 0; j < N; j++) s->w[j] = 1.0;
    s->slackVar = (int*)xmalloc(m * sizeof(int));
    s->artVar = (int*)xmalloc(m * sizeof(int));
    s->beq = (double*)xmalloc(m * sizeof(double));
    s->borig = (double*)xmalloc(m * sizeof(double));
    s->mlt = (int*)xmalloc(m * sizeof(int));
    s->artSign = (int*)xmalloc(m * sizeof(int));
    s->rel = (char*)xmalloc(m);
    for (i = 0; i < m; i++) { s->beq[i] = fabs(lp->b[i]); s->borig[i] = lp->b[i]; s->mlt[i] = (lp->b[i]>=0)?1:-1; s->artSign[i] = 1; s->rel[i] = lp->rel[i]; }

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
    /* original columns -- canonicalized: duplicate (row,col) entries handed
       in through the API are merged by summing (GLPK semantics), so the
       normalized matrix stays consistent between matvecs and column reads
       (external audit F-2; the text parser does the same merge).  Merged
       coefficients that cancel to exactly zero are kept: an explicit 0.0 is
       inert for both products and reads, and dropping it would churn the
       allocation/size bookkeeping for no correctness benefit. */
    {
        int *mk = (int*)xmalloc((size_t)(m ? m : 1) * sizeof(int));
        double *acc = (double*)xmalloc((size_t)(m ? m : 1) * sizeof(double));
        int *tlist = (int*)xmalloc((size_t)(m ? m : 1) * sizeof(int));
        memset(mk, 0, (size_t)(m ? m : 1) * sizeof(int));
        for (j = 0; j < n; j++) {
            s->colptr[j] = (int)pos;
            int nt = 0;
            for (k = lp->Acolptr[j]; k < lp->Acolptr[j + 1]; k++) {
                int r = lp->Arow[k];
                double a = lp->Aval[k] * mlt[r];     /* apply row scaling */
                if (a == 0.0) continue;
                if (mk[r] != j + 1) { mk[r] = j + 1; acc[r] = a; tlist[nt++] = r; }
                else acc[r] += a;
            }
            for (int t = 0; t < nt; t++) {
                int r = tlist[t];
                s->row[pos] = r;
                s->val[pos] = acc[r];
                pos++;
            }
        }
        psolve_free(mk); psolve_free(acc); psolve_free(tlist);
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

    /* Guard against int truncation of the CSC column counts (the arrays are
       int-typed, so pos must fit in an int).  Also guard the dense basis
       matrix m*m against size_t overflow (only when m > 0, to avoid a
       divide-by-zero). */
    int basis_ok = 1;
    if (m > 0 && (size_t)m > (size_t)-1 / (size_t)m / sizeof(double)) basis_ok = 0;
    if (pos > INT_MAX || !basis_ok) {
        solver_destroy(s);
        psolve_free(mlt);
        return NULL;
    }

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

    /* Initial basis and values.
       At this point every original variable is nonbasic at its starting value
       (lower bound if finite, else upper bound).  These nonbasic values
       contribute to each row's equality:
            sum_j (mlt_i A_ij) x_j + (slack/artificial coeff) * x_v = |b_i|
       so the correct initial basic value for row i is
            x_v = (|b_i| - S_i) / coeff,  S_i = sum_j (mlt_i A_ij) x_j(start).
       We compute S_i and choose, per row, the slack if it yields a feasible
       (nonneg) value, otherwise a sign-correct artificial so the artificial
       value is nonneg and Phase I can drive it to zero. */
    s->negate_obj = maximize ? 0 : 1;
    /* Build the canonical starting basis and reset the phase state (a full
       reset: basis + phase + refactorize).  Also used by the dense-LU retry
       in solver_solve() after a sparse solve diverges. */
    solver_reset_to_initial(s);
    s->iteration_limit = 2000000;
    psolve_free(mlt);
    return s;
}

/* (Re)build the canonical starting basis from the stored problem data:
 * every structural variable nonbasic at a finite bound, and per row either the
 * slack (when that gives a non-negative slack value) or a sign-corrected
 * artificial, so the starting point is *exactly* primal feasible for the
 * Phase I problem.
 *
 * It is used twice: once by solver_create_internal(), and again by the Phase I
 * restart in solver_solve() when a Phase I run ends in a state that cannot be
 * certified (an artificial driven outside its bounds by a refactorization, or
 * a mathematically impossible "unbounded" Phase I).  Restarting from a clean
 * feasible basis with Bland's rule turns those into a real verdict instead of
 * a wrong one.  Only stored solver state is used (s->mlt / s->rel / s->beq),
 * never the caller's LP, so it is safe to call at any time. */
static void build_initial_basis(Solver *s)
{
    int n = s->n_core, N = s->N, M = s->M;

    s->needs_phase1 = 0;

    /* all columns start nonbasic at a bound */
    for (int j = 0; j < N; j++) { s->basispos[j] = -1; s->w[j] = 1.0; }
    for (int j = 0; j < n; j++) {
        double xj = (s->l[j] > -LP_INF) ? s->l[j] : s->u[j];
        s->x[j] = xj;
        s->status[j] = (s->l[j] > -LP_INF) ? LP_NBL : LP_NBU;
    }
    for (int j = n; j < N; j++) { s->x[j] = 0.0; s->status[j] = LP_NBL; }

    /* artificial columns are stored with coefficient +1; the per-row sign is
       (re)applied below so every artificial starts at a non-negative value. */
    for (int i = 0; i < M; i++) {
        int av = s->artVar[i];
        s->artSign[i] = 1;
        for (int k = s->colptr[av]; k < s->colptr[av+1]; k++) s->val[k] = 1.0;
    }

    double *S = (double*)psolve_calloc((size_t)(M > 0 ? M : 1), sizeof(double));

    /* contributions of the nonbasic structural variables at their start value */
    for (int j = 0; j < n; j++) {
        double xj = s->x[j];
        if (xj == 0.0) continue;
        for (int k = s->colptr[j]; k < s->colptr[j+1]; k++)
            S[s->row[k]] += s->val[k] * xj;
    }
    for (int i = 0; i < M; i++) {
        double resid = s->beq[i] - S[i];
        int sv = s->slackVar[i];
        int chosen = -1;
        if (sv >= 0) {
            /* slack coeff = mlt_i * (rel=='<' ? +1 : -1) */
            double scoef = (double)s->mlt[i] * (s->rel[i] == '<' ? 1.0 : -1.0);
            double v = resid / scoef;
            if (v >= -1e-12) {   /* slack value nonneg => feasible start */
                chosen = sv;
                s->x[sv] = (v > 0.0) ? v : 0.0;
            }
        }
        if (chosen < 0) {
            int av = s->artVar[i];
            double v = (resid >= 0.0) ? resid : -resid;
            s->basis[i] = av;
            s->basispos[av] = i;
            s->status[av] = LP_BASIC;
            s->x[av] = v;
            s->needs_phase1 = 1;
            s->artSign[i] = (resid >= 0.0) ? 1 : -1;
            for (int k = s->colptr[av]; k < s->colptr[av+1]; k++)
                s->val[k] = (double)s->artSign[i];
        } else {
            s->basis[i] = chosen;
            s->basispos[chosen] = i;
            s->status[chosen] = LP_BASIC;
        }
    }
    psolve_free(S);

    /* finalize: every non-basic variable must have basispos = -1 */
    for (int j = 0; j < N; j++)
        if (s->status[j] != LP_BASIC) s->basispos[j] = -1;
}

static int lp_var_is_free(double lo, double hi)
{
    return lo <= -LP_INF && hi >= LP_INF;
}

static void free_normalized_lp(LP *lp)
{
    psolve_free(lp->c); psolve_free(lp->l); psolve_free(lp->u);
    psolve_free(lp->Acolptr); psolve_free(lp->Arow); psolve_free(lp->Aval);
    memset(lp, 0, sizeof(*lp));
}

/* Public construction path.  Revised simplex needs every nonbasic variable
 * to sit at a bound, so normalize a caller-visible free variable x into
 * x+ - x-, x+,x- >= 0.  This is an exact linear transformation: the positive
 * column is A_j, the negative column is -A_j, and their costs are c_j/-c_j.
 * The mapping is retained for solution, sensitivity, and incremental APIs. */
Solver *solver_create(const LP *lp)
{
    if (!lp || lp->n <= 0 || lp->m < 0 || lp->n > 1000000 || lp->m > 1000000 ||
        !lp->c || !lp->l || !lp->u || !lp->Acolptr ||
        (lp->m > 0 && (!lp->b || !lp->rel))) return NULL;
    if(lp->Acolptr[0]!=0)return NULL;
    for(int j=0;j<lp->n;j++){
        if(!isfinite(lp->c[j])||!isfinite(lp->l[j])||!isfinite(lp->u[j])||
           lp->Acolptr[j]<0||lp->Acolptr[j+1]<lp->Acolptr[j])return NULL;
    }
    int input_nnz=lp->Acolptr[lp->n];
    if(input_nnz>0&&(!lp->Arow||!lp->Aval))return NULL;
    for(int k=0;k<input_nnz;k++)
        if(lp->Arow[k]<0||lp->Arow[k]>=lp->m||!isfinite(lp->Aval[k]))return NULL;
    for(int i=0;i<lp->m;i++)
        if(!isfinite(lp->b[i])||
           (lp->rel[i]!='<'&&lp->rel[i]!='>'&&lp->rel[i]!='='))return NULL;

    int n = lp->n;
    int *orig_pos = (int*)xmalloc((size_t)n * sizeof(int));
    int *orig_neg = (int*)xmalloc((size_t)n * sizeof(int));
    int nfree = 0;
    for (int j = 0; j < n; j++) if (lp_var_is_free(lp->l[j], lp->u[j])) nfree++;
    /* ncore+1 is stored in an int-sized CSC pointer array. */
    if (n >= INT_MAX || nfree > INT_MAX - n - 1) {
        psolve_free(orig_pos); psolve_free(orig_neg); return NULL;
    }
    int ncore = n + nfree;
    int next = 0;
    for (int j = 0; j < n; j++) {
        orig_pos[j] = next++;
        orig_neg[j] = lp_var_is_free(lp->l[j], lp->u[j]) ? next++ : -1;
    }

    Solver *s = NULL;
    if (nfree == 0) {
        s = solver_create_internal(lp);
    } else {
        LP norm; memset(&norm, 0, sizeof(norm));
        norm.n = ncore; norm.m = lp->m; norm.maximize = lp->maximize;
        norm.b = lp->b; norm.rel = lp->rel; /* copied by the internal builder */
        norm.c = (double*)xmalloc((size_t)ncore * sizeof(double));
        norm.l = (double*)xmalloc((size_t)ncore * sizeof(double));
        norm.u = (double*)xmalloc((size_t)ncore * sizeof(double));

        long nnz = 0;
        for (int j = 0; j < n; j++) {
            long cnt = (long)lp->Acolptr[j+1] - lp->Acolptr[j];
            long mult = orig_neg[j] >= 0 ? 2L : 1L;
            if (cnt < 0 || cnt > (LONG_MAX - nnz) / mult) {
                free_normalized_lp(&norm); psolve_free(orig_pos); psolve_free(orig_neg); return NULL;
            }
            nnz += cnt * mult;
        }
        if (nnz > INT_MAX) {
            free_normalized_lp(&norm); psolve_free(orig_pos); psolve_free(orig_neg); return NULL;
        }
        norm.Acolptr = (int*)xmalloc((size_t)(ncore + 1) * sizeof(int));
        norm.Arow = (int*)xmalloc((size_t)(nnz ? nnz : 1) * sizeof(int));
        norm.Aval = (double*)xmalloc((size_t)(nnz ? nnz : 1) * sizeof(double));

        long pos = 0;
        for (int j = 0; j < n; j++) {
            int p = orig_pos[j], q = orig_neg[j];
            norm.c[p] = lp->c[j];
            norm.l[p] = q >= 0 ? 0.0 : lp->l[j];
            norm.u[p] = q >= 0 ? LP_INF : lp->u[j];
            norm.Acolptr[p] = (int)pos;
            for (int k = lp->Acolptr[j]; k < lp->Acolptr[j+1]; k++) {
                norm.Arow[pos] = lp->Arow[k]; norm.Aval[pos] = lp->Aval[k]; pos++;
            }
            if (q >= 0) {
                norm.c[q] = -lp->c[j]; norm.l[q] = 0.0; norm.u[q] = LP_INF;
                norm.Acolptr[q] = (int)pos;
                for (int k = lp->Acolptr[j]; k < lp->Acolptr[j+1]; k++) {
                    norm.Arow[pos] = lp->Arow[k]; norm.Aval[pos] = -lp->Aval[k]; pos++;
                }
            }
        }
        norm.Acolptr[ncore] = (int)pos;
        s = solver_create_internal(&norm);
        free_normalized_lp(&norm);
    }
    if (!s) { psolve_free(orig_pos); psolve_free(orig_neg); return NULL; }

    s->n_orig = n;
    s->n_core = ncore;
    s->orig_pos = orig_pos;
    s->orig_neg = orig_neg;
    s->orig_c = (double*)xmalloc((size_t)n * sizeof(double));
    s->orig_l = (double*)xmalloc((size_t)n * sizeof(double));
    s->orig_u = (double*)xmalloc((size_t)n * sizeof(double));
    memcpy(s->orig_c, lp->c, (size_t)n * sizeof(double));
    memcpy(s->orig_l, lp->l, (size_t)n * sizeof(double));
    memcpy(s->orig_u, lp->u, (size_t)n * sizeof(double));
    s->rebuild_pending = 0;
    return s;
}

void solver_destroy(Solver *s)
{
    if (!s) return;
    for (int i = 0; i < s->eta_cap; i++) psolve_free(s->eta[i]);
    psolve_free(s->eta); psolve_free(s->eta_piv);
    psolve_free(s->l); psolve_free(s->u); psolve_free(s->cobj); psolve_free(s->c0); psolve_free(s->basis); psolve_free(s->basispos);
    psolve_free(s->orig_pos); psolve_free(s->orig_neg); psolve_free(s->orig_c); psolve_free(s->orig_l); psolve_free(s->orig_u);
    psolve_free(s->status); psolve_free(s->x); psolve_free(s->rc); psolve_free(s->cB);
    psolve_free(s->lu); psolve_free(s->piv); psolve_free(s->y); psolve_free(s->d); psolve_free(s->v); psolve_free(s->xb);
    psolve_free(s->slackVar); psolve_free(s->artVar); psolve_free(s->beq); psolve_free(s->borig); psolve_free(s->mlt); psolve_free(s->artSign); psolve_free(s->rel);
    psolve_free(s->w); psolve_free(s->vw); psolve_free(s->piw); psolve_free(s->duals);
    psolve_free(s->colptr); psolve_free(s->row); psolve_free(s->val);
    splu_free(&s->splu);
    psolve_free(s->bBp); psolve_free(s->bBi); psolve_free(s->bBx);
    psolve_free(s);
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
            if (!s->lu_valid) s->factor_failed = 1;
        }
    } else {
        for (int i = 0; i < M; i++) {
            int var = s->basis[i];
            memset(s->lu + (size_t)i * M, 0, M * sizeof(double));
            for (int k = s->colptr[var]; k < s->colptr[var + 1]; k++)
                s->lu[(size_t)i * M + s->row[k]] = s->val[k];
        }
        s->lu_valid = (lu_factor(s->lu, M, s->piv) == 0);
        if (!s->lu_valid) s->factor_failed = 1;
        s->eta_count = 0;
    }
    /* steepest-edge weights: reset to the reference framework */
    for (int _j = 0; _j < s->N; _j++) s->w[_j] = 1.0;
}

static void push_eta(Solver *s, int pslot, const double *d)
{
    int M = s->M;
    if (s->eta_count >= s->eta_cap) {
        /* growth is bounded by the iteration limit, but guard against an
           overflow of the doubling arithmetic regardless. */
        if (s->eta_cap > 2000000000L / 2) psolve_fail(PSOLVE_ERR_SOLVE);
        int newcap = s->eta_cap * 2;
        psolve_realloc((void**)&s->eta_piv, (size_t)newcap * sizeof(int));
        psolve_realloc((void**)&s->eta, (size_t)newcap * sizeof(double*));
        for (int i = s->eta_cap; i < newcap; i++) s->eta[i] = NULL;
        s->eta_cap = newcap;
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

/* Re-initialize the solver to its starting basis: every original variable
   nonbasic at a bound, and each row served by a slack (if it yields a
   nonnegative value) or a sign-correct artificial.  This is used both at
   construction and to retry a solve with a more robust factorization after the
   sparse LU path diverged.  It preserves the caller-set objective coefficients
   (cobj is set to the Phase-I objective here; solver_solve re-establishes the
   true objective after Phase I, exactly as on a fresh solve) and the
   iteration limit, so a retry behaves like a clean solve. */
static void solver_reset_to_initial(Solver *s)
{
    int n = s->n_core, m = s->M, N = s->N;
    if (m < 0) { s->status_out = SOLVE_NUMERICAL; return; }   /* defensive: never valid */
    int nslack = 0;
    for (int i = 0; i < m; i++) if (s->rel[i] != '=') nslack++;
    int *mlt = s->mlt;
    for (int i = 0; i < m; i++) mlt[i] = (s->borig[i] >= 0) ? 1 : -1;

    /* Phase I objective: all zero except -1 for artificials */
    for (int j = 0; j < N; j++) s->cobj[j] = 0.0;
    for (int i = 0; i < m; i++) s->cobj[s->artVar[i]] = -1.0;

    /* restore canonical artificial column coefficients (+1) before re-selecting
       basic artificials and applying their per-row sign */
    for (int i = 0; i < m; i++) {
        int av = s->artVar[i];
        for (int k = s->colptr[av]; k < s->colptr[av+1]; k++) s->val[k] = 1.0;
    }

    s->needs_phase1 = 0;
    {
        double *S = (double*)psolve_calloc((size_t)(m > 0 ? m : 1), sizeof(double));
        /* contributions from nonbasic originals at their starting value */
        for (int j = 0; j < n; j++) {
            double xj = (s->l[j] > -LP_INF) ? s->l[j] : s->u[j];
            s->x[j] = xj;
            if (xj == 0.0) continue;
            for (int k = s->colptr[j]; k < s->colptr[j+1]; k++)
                S[s->row[k]] += s->val[k] * xj;
        }
        for (int i = 0; i < m; i++) {
            double resid = s->beq[i] - S[i];
            int sv = s->slackVar[i];
            int chosen = -1;
            if (sv >= 0) {
                double scoef = (double)mlt[i] * (s->rel[i] == '<' ? 1.0 : -1.0);
                double v = resid / scoef;
                if (v >= -1e-12) {   /* slack value nonneg => feasible start */
                    chosen = sv;
                    s->x[sv] = (v > 0.0) ? v : 0.0;
                }
            }
            if (chosen < 0) {
                int av = s->artVar[i];
                double v = (resid >= 0.0) ? resid : -resid;
                s->basis[i] = av;
                s->basispos[av] = i;
                s->status[av] = LP_BASIC;
                s->x[av] = v;
                s->needs_phase1 = 1;
                s->artSign[i] = (resid >= 0.0) ? 1 : -1;
                for (int k = s->colptr[av]; k < s->colptr[av+1]; k++)
                    s->val[k] = (double)s->artSign[i];
            } else {
                s->basis[i] = chosen;
                s->basispos[chosen] = i;
                s->status[chosen] = LP_BASIC;
            }
        }
        psolve_free(S);
    }

    for (int j = 0; j < n; j++) {
        s->basispos[j] = -1;
        if (s->l[j] > -LP_INF) s->status[j] = LP_NBL;
        else s->status[j] = LP_NBU;
    }
    for (int j = n; j < n + nslack; j++) {
        if (s->basispos[j] >= 0) continue;   /* slack already basic */
        s->basispos[j] = -1;
        s->status[j] = LP_NBL;
    }
    for (int i = 0; i < m; i++) {
        int av = s->artVar[i];
        if (s->status[av] != LP_BASIC) { s->status[av] = LP_NBL; s->x[av] = 0.0; }
    }

    /* finalize: every non-basic variable must have basispos = -1 */
    for (int j = 0; j < N; j++)
        if (s->status[j] != LP_BASIC) s->basispos[j] = -1;

    s->phase = 1;
    s->lu_valid = 0;
    s->sparse_ok = 0;
    s->bland = 0; s->flat = 0; s->last_obj = -LP_INF;
    s->iters = 0;
    s->status_out = 0;
    s->objval = 0.0;
    s->hyper_tol = 0.0;

    /* initial refactorization (honors s->sparse_disabled / use_sparse) */
    refactorize(s);
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
            int cand = (st == LP_NBL && rc > TOL_DJ) || (st == LP_NBU && rc < -TOL_DJ);
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
            if (rc <= TOL_DJ) continue;
            score = rc / sqrt(s->w[j] < 1e-30 ? 1e-30 : s->w[j]);
        } else { /* NBU */
            if (rc >= -TOL_DJ) continue;
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

    /* A basic variable only blocks the step if the bound it moves toward is
       genuinely finite.  LP_INF (1e30) is the *sentinel* for "no bound": using
       it as a numeric bound made the ratio test return a huge-but-finite theta
       (e.g. 1e30/|v| when |v|>1), so a truly unbounded LP was pushed to
       x = 1e30 and then reported as a bogus OPTIMAL / NUMERICAL_FAILURE
       instead of UNBOUNDED. */
    for (int i = 0; i < M; i++) {
        int bv = s->basis[i];
        double vv = v[i];
        if (vv > TOL_PIV) {
            if (s->u[bv] >= LP_INF) continue;         /* no finite upper bound */
            double r = (s->u[bv] - s->x[bv]) / vv;
            if (r < theta) { theta = r; *blocker = bv; *blockSlot = i; blockIsBasic = 1; }
        } else if (vv < -TOL_PIV) {
            if (s->l[bv] <= -LP_INF) continue;        /* no finite lower bound */
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
            if (vv > TOL_PIV) {
                if (s->u[bv] >= LP_INF) continue;     /* sentinel = no bound */
                r = (s->u[bv] - s->x[bv]) / vv;
            } else if (vv < -TOL_PIV) {
                if (s->l[bv] <= -LP_INF) continue;
                r = (s->x[bv] - s->l[bv]) / (-vv);
            }
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

    /* Degenerate guard: if the ratio test reports a normal pivot (rr==0) but
       no basic variable actually blocks (blockSlot<0), the basis exchange would
       read/write basis[-1].  This can happen in Phase I with an artificial
       stuck at a positive value.  Reinitialize the basic values and continue. */
    if (rr == 0 && blockSlot < 0) {
        refactorize(s);
        recompute_basic(s);
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
        /* A basic artificial at zero is a degenerate column marking a
           (numerically) redundant row.  Removing it by swapping in an
           arbitrary candidate can make the basis SINGULAR (rank-deficient
           constraint sets, e.g. big-M encodings that produce dependent rows).
           Leave such artificials basic so their identity column keeps the basis
           nonsingular, but FIX them at zero (l=u=0): otherwise Phase II would
           let the artificial absorb infeasibility and report a solution that
           violates the row (e.g. -3x0=0 solved as x0=20).  Only replace an
           artificial that is basic at a nonzero value. */
        if (fabs(s->x[bv]) <= 1e-9) {
            s->l[bv] = 0.0; s->u[bv] = 0.0;   /* pin the redundant artificial at 0 */
            continue;
        }
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
    psolve_free(rhs);
}

static int solve_phase(Solver *s)
{
    int r;
    long cap = s->iteration_limit > 0 ? s->iteration_limit : 2000000;
    long poll = 0;
    while ((r = iterate(s)) == 1) {
        if (s->iters > cap) { r = -1; break; }   /* iteration limit / cycling */
        /* cooperative abort (time limit / Ctrl-C): check periodically so a
           long run inside a single relaxation can be interrupted, not just
           between branch-and-bound nodes. */
        if ((++poll & 255) == 0 && psolve_stop()) { r = SOLVE_STOPPED; break; }
    }
    return r;
}

static int solver_solve_impl(Solver *s)
{
    int r = 0;
    /* Empty box => INFEASIBLE, certified by construction: a variable with
       l[j] > u[j] admits no assignment at all, so no constraint examination
       is needed.  Without this up-front verdict a contradictory box (the
       parser and the API both accept l > u; only generators never emit it)
       could surface as UNBOUNDED when the entering rules skip the stuck
       variable and the phase-2 walk rides a ray on a free one -- a status
       that implies feasibility, i.e. a wrong answer (external audit F-1). */
    for (int j = 0; j < s->N; j++) {
        if (s->l[j] > s->u[j]) { s->status_out = 1; return 1; }
    }
    /* Phase I: needed if any artificial is in the current basis.  Detecting it
       from the basis (not a flag) makes this work both for the initial solve
       and for warm starts after incremental changes. */
    int first_art = s->N - s->M;
    int need_p1 = 0;
    for (int i = 0; i < s->M; i++)
        if (s->basis[i] >= first_art) { need_p1 = 1; break; }
    if (need_p1) {
        /* Phase I verdicts are only trustworthy when the final point is a
           genuine point of the Phase I problem.  Two ways that used to fail:
             (1) Phase I reporting "unbounded" — impossible in exact arithmetic
                 (its objective -sum(artificials) is bounded above by 0), so it
                 only ever signals numerical trouble.  It was reported as
                 UNBOUNDED for the *user's* LP: a false status.
             (2) a refactorization/recompute pushing a basic artificial far
                 negative (outside its [0, inf) bound).  The feasibility test
                 `sum(artificials) > tol` then reads a *negative* sum as
                 "feasible", and Phase II ran from an infeasible basis and
                 reported OPTIMAL/UNBOUNDED for an INFEASIBLE LP.
           Both are now detected; the solve restarts once from a clean, exactly
           feasible basis under Bland's rule (finite termination) and only
           reports NUMERICAL_FAILURE if that also fails to certify. */
        int attempt = 0;
        for (;;) {
            s->phase = 1;
            for (int j = 0; j < s->N; j++) s->cobj[j] = 0.0;
            for (int i = 0; i < s->M; i++) {
                int av = s->artVar[i];
                s->cobj[av] = -1.0;      /* all artificials, basic or not */
            }
            r = solve_phase(s);
            if (r == SOLVE_NUMERICAL) { s->status_out = SOLVE_NUMERICAL; return SOLVE_NUMERICAL; }
            if (r == SOLVE_STOPPED) { s->status_out = SOLVE_STOPPED; return SOLVE_STOPPED; }
            if (r == -1) { s->status_out = 3; return 3; }   /* Phase I iteration limit: NOT infeasible */

            /* r == 0 (optimal) is the only certifiable outcome; r == 2 means
               the ratio test lost a blocking bound to rounding. */
            int certified = (r == 0);
            double artsum = 0.0;
            if (certified) {
                double worst = 0.0;
                for (int j = 0; j < s->N; j++) {
                    if (s->status[j] == LP_REMOVED) continue;
                    double viol = 0.0;
                    if (s->x[j] < s->l[j]) viol = s->l[j] - s->x[j];
                    else if (s->x[j] > s->u[j]) viol = s->x[j] - s->u[j];
                    if (viol > worst) worst = viol;
                }
                for (int i = 0; i < s->M; i++) {
                    int av = s->artVar[i];
                    if (s->status[av] == LP_BASIC) artsum += s->x[av];
                }
                if (worst > 1e-6 * (1.0 + fabs(artsum))) certified = 0;
            }
            if (certified) {
                if (artsum > 1e-6) { s->status_out = 1; return 1; }  /* infeasible */
                break;                                               /* feasible */
            }
            if (attempt++ >= 1) { s->status_out = SOLVE_NUMERICAL; return SOLVE_NUMERICAL; }

            build_initial_basis(s);
            s->sparse_disabled = 1; s->use_sparse = 0;   /* prefer dense stability */
            s->lu_valid = 0;
            refactorize(s);
            s->bland = 1; s->flat = 0; s->last_obj = -LP_INF;
        }
        remove_basic_artificials(s);
    }

    cleanup_phase1(s);

    /* switch to Phase II objective: true objective for originals, 0 else */
    memcpy(s->cobj, s->c0, s->N * sizeof(double));
    s->phase = 2;
    s->bland = 0; s->flat = 0; s->last_obj = -LP_INF;
    refactorize(s);

    /* re-derive the basic values so the solve always starts exactly feasible */
    recompute_basic(s);

    r = solve_phase(s);
    if (r == SOLVE_NUMERICAL) { s->status_out = SOLVE_NUMERICAL; return SOLVE_NUMERICAL; }
    if (r == SOLVE_STOPPED) { s->status_out = SOLVE_STOPPED; return SOLVE_STOPPED; }
    if (r == 2) {
        /* UNBOUNDED asserts a feasible ray -- i.e. it implies feasibility.
           Run the same primal certificate that gates OPTIMAL before making
           that claim; a violation means the verdict is numerical noise,
           which is what SOLVE_NUMERICAL is for (external audit F-1,
           belt-and-braces next to the up-front empty-box INFEASIBLE). */
        if (!solver_feasible(s)) { s->status_out = SOLVE_NUMERICAL; return SOLVE_NUMERICAL; }
        s->status_out = 2; return 2;
    }
    if (r == -1) { s->status_out = 3; return 3; }   /* iteration limit hit */

    /* Solution certificate: if the claimed optimum does not actually satisfy
       the variable bounds and constraint rows — or the final basis could not
       be factorized, so the reduced costs / FTRAN were computed with stale
       data — the solve has diverged.  Report NUMERICAL_FAILURE rather than a
       false OPTIMAL.  Transient singular bases that the simplex recovers from
       are fine; a bad FINAL factorization or a diverged solution is not. */
    int basis_valid = (s->use_sparse && s->sparse_ok) || (!s->use_sparse && s->lu_valid);
    if (!basis_valid || !solver_feasible(s)) { s->status_out = SOLVE_NUMERICAL; return SOLVE_NUMERICAL; }

    /* objective value */
    double obj = 0.0;
    for (int i = 0; i < s->M; i++) obj += s->cobj[s->basis[i]] * s->x[s->basis[i]];
    for (int j = 0; j < s->N; j++)
        if (s->status[j] != LP_BASIC) obj += s->cobj[j] * s->x[j];
    s->objval = obj;
    s->status_out = 0;

    /* save dual (shadow-price) values = B^{-T} c_B at the optimum */
    for (int i = 0; i < s->M; i++) s->cB[i] = s->cobj[s->basis[i]];
    memcpy(s->duals, s->cB, (size_t)s->M * sizeof(double));
    {
        double *tmp = (double*)psolve_malloc((size_t)s->M * sizeof(double));
        if (tmp) {
            memcpy(tmp, s->duals, (size_t)s->M * sizeof(double));
            btrans(s, tmp);
            memcpy(s->duals, tmp, (size_t)s->M * sizeof(double));
            psolve_free(tmp);
        }
    }
    return 0;
}

int solver_solve(Solver *s)
{
    if(!s)return SOLVE_INVALID;
    int r = solver_solve_impl(s);
    /* Robustness: on ill-conditioned moderately-sparse big-M bases the sparse
       LU can factorize "successfully" yet diverge, so solver_solve_impl returns
       SOLVE_NUMERICAL (its solution certificate fails).  When that happens on
       the sparse path, retry once from a clean starting basis with the robust
       dense LU — the identical problem, just a more stable factorization.
       Correctness is preserved either way (never a false OPTIMAL); this only
       turns a would-be NUMERICAL failure into a certified answer. */
    if (r == SOLVE_NUMERICAL && s->use_sparse && !s->sparse_disabled) {
        s->sparse_disabled = 1;
        solver_reset_to_initial(s);
        r = solver_solve_impl(s);
    }
    return r;
}

void solver_duals(const Solver *s, double *dual)
{
    if(!s||!dual)return;
    /* duals are computed for the internal maximize form; negate if the user's
       problem was a minimization so the reported shadow prices have the sign
       consistent with the original objective. */
    double sign = s->negate_obj ? -1.0 : 1.0;
    for (int i = 0; i < s->M; i++) dual[i] = sign * s->duals[i];
}

void solver_reduced_costs(const Solver *s, double *rc)
{
    if(!s||!rc)return;
    double ytol = 0.0;
    for (int j = 0; j < s->n_orig; j++) {
        /* The direct/positive component has exactly the user's column A_j
           and objective coefficient c_j, so its reduced cost is the reduced
           cost of the original direction even when x_j = x_j+ - x_j-. */
        int p = s->orig_pos[j];
        if (s->status[p] == LP_REMOVED) { rc[j] = 0.0; continue; }
        rc[j] = s->cobj[p] - k_dsdot_sparse(s->duals,
                              s->row + s->colptr[p], s->val + s->colptr[p],
                              (long)(s->colptr[p+1] - s->colptr[p]), ytol);
    }
}

void solver_optimum(const Solver *s, double *x_orig, double *obj)
{
    if(!s||!x_orig||!obj)return;
    for (int j = 0; j < s->n_orig; j++) {
        int p = s->orig_pos[j], q = s->orig_neg[j];
        x_orig[j] = s->x[p] - (q >= 0 ? s->x[q] : 0.0);
    }
    *obj = s->negate_obj ? -s->objval : s->objval;
}

void solver_set_objective(Solver *s, const double *c, int maximize)
{
    if(!s||!c)return;
    s->negate_obj = maximize ? 0 : 1;
    for (int j = 0; j < s->n_orig; j++) {
        int p = s->orig_pos[j], q = s->orig_neg[j];
        double cj = maximize ? c[j] : -c[j];
        s->orig_c[j] = c[j];
        s->c0[p] = cj;
        if (q >= 0) s->c0[q] = -cj;
    }
    memcpy(s->cobj, s->c0, (size_t)s->N * sizeof(double));
    /* slacks/artificials keep zero objective (already 0 after Phase II) */
}

void solver_set_bounds(Solver *s, const double *l, const double *u)
{
    if(!s||!l||!u)return;
    int topology_changed = s->rebuild_pending;
    for (int j = 0; j < s->n_orig; j++) {
        int was_free = s->orig_neg[j] >= 0;
        int now_free = lp_var_is_free(l[j], u[j]);
        if (was_free != now_free) topology_changed = 1;
        s->orig_l[j] = l[j]; s->orig_u[j] = u[j];
    }
    if (topology_changed) {
        /* Switching a variable between free and bounded changes the number of
           normalized columns.  Rebuild on the next warm solve rather than
           corrupting the live basis with an incompatible mapping. */
        s->rebuild_pending = 1;
        return;
    }
    for (int j = 0; j < s->n_orig; j++) {
        int p = s->orig_pos[j];
        if (s->orig_neg[j] < 0) { s->l[p] = l[j]; s->u[p] = u[j]; }
    }
}

/* Export the caller-visible LP from the normalized tableau.  The positive
 * component of every mapped variable is the original A column, so the original
 * sparse CSC can be recovered without retaining a second matrix copy. */
static void free_exported_lp(LP *lp)
{
    psolve_free(lp->c); psolve_free(lp->l); psolve_free(lp->u); psolve_free(lp->b); psolve_free(lp->rel);
    psolve_free(lp->Acolptr); psolve_free(lp->Arow); psolve_free(lp->Aval);
    memset(lp, 0, sizeof(*lp));
}

static int solver_export_lp(const Solver *s, const double *new_a,
                            double new_rhs, char new_rel, LP *lp)
{
    int n = s->n_orig, m = s->M;
    int add = new_a != NULL;
    int mout = m + add;
    if (n <= 0 || m < 0 || (add && (new_rel != '<' && new_rel != '>' && new_rel != '=')))
        return -1;
    memset(lp, 0, sizeof(*lp));
    lp->n = n; lp->m = mout; lp->maximize = !s->negate_obj;
    lp->c = (double*)xmalloc((size_t)n * sizeof(double));
    lp->l = (double*)xmalloc((size_t)n * sizeof(double));
    lp->u = (double*)xmalloc((size_t)n * sizeof(double));
    lp->b = (double*)xmalloc((size_t)(mout ? mout : 1) * sizeof(double));
    lp->rel = (char*)xmalloc((size_t)(mout ? mout : 1));
    for (int j = 0; j < n; j++) {
        lp->c[j] = s->orig_c[j];
        lp->l[j] = s->orig_l[j]; lp->u[j] = s->orig_u[j];
    }
    for (int i = 0; i < m; i++) { lp->b[i] = s->borig[i]; lp->rel[i] = s->rel[i]; }
    if (add) { lp->b[m] = new_rhs; lp->rel[m] = new_rel; }

    long nnz = 0;
    for (int j = 0; j < n; j++) {
        int p = s->orig_pos[j];
        long cnt = (long)(s->colptr[p+1] - s->colptr[p]);
        if (cnt < 0 || cnt > LONG_MAX - nnz) { free_exported_lp(lp); return -1; }
        nnz += cnt;
        if (add && new_a[j] != 0.0) {
            if (nnz == LONG_MAX) { free_exported_lp(lp); return -1; }
            nnz++;
        }
    }
    if (nnz > INT_MAX) { free_exported_lp(lp); return -1; }
    lp->Acolptr = (int*)xmalloc((size_t)(n + 1) * sizeof(int));
    lp->Arow = (int*)xmalloc((size_t)(nnz ? nnz : 1) * sizeof(int));
    lp->Aval = (double*)xmalloc((size_t)(nnz ? nnz : 1) * sizeof(double));
    long pos = 0;
    for (int j = 0; j < n; j++) {
        int p = s->orig_pos[j];
        lp->Acolptr[j] = (int)pos;
        for (int k = s->colptr[p]; k < s->colptr[p+1]; k++) {
            int row = s->row[k];
            if (row < 0 || row >= m) { free_exported_lp(lp); return -1; }
            double v = s->val[k] / s->mlt[row];  /* undo equality row scaling */
            if (v != 0.0) { lp->Arow[pos] = row; lp->Aval[pos] = v; pos++; }
        }
        if (add && new_a[j] != 0.0) { lp->Arow[pos] = m; lp->Aval[pos] = new_a[j]; pos++; }
    }
    lp->Acolptr[n] = (int)pos;
    return 0;
}

/* Reconstruct the caller-visible LP and do a clean full re-solve.  Used when
 * an incremental bound update changes the free-variable normalization or the
 * current basis is no longer a safe warm start. */
static void solver_refresh(Solver *s)
{
    LP lp;
    if (solver_export_lp(s, NULL, 0.0, 0, &lp) != 0) {
        s->status_out = SOLVE_NUMERICAL;
        return;
    }
    Solver *fresh = solver_create(&lp);
    if (!fresh) {
        free_exported_lp(&lp);
        s->status_out = SOLVE_NUMERICAL;
        return;
    }
    fresh->iteration_limit = s->iteration_limit;
    fresh->reinvert_interval = s->reinvert_interval;
    fresh->hyper_tol = s->hyper_tol;
    solver_solve(fresh);
    Solver hold = *fresh; *fresh = *s; *s = hold;
    solver_destroy(fresh);
    free_exported_lp(&lp);
}

/* Re-solve from the current basis (warm start).  Assumes Phase I has been
 * completed (or was unnecessary) and the basis is a valid starting point.
 * If an incremental change (e.g. a tightened bound) makes the warm start
 * infeasible, falls back to a clean re-solve. */
int solver_warm_solve(Solver *s)
{
    if(!s)return SOLVE_INVALID;
    if (s->rebuild_pending) {
        solver_refresh(s);
        return s->status_out;
    }
    /* empty-box INFEASIBLE check (parity with solver_solve_impl; a warm solve
       may follow solver_set_bounds, which accepts any bound pair) */
    for (int j = 0; j < s->N; j++) {
        if (s->l[j] > s->u[j]) { s->status_out = 1; return 1; }
    }
    s->phase = 2;
    s->bland = 0; s->flat = 0; s->last_obj = -LP_INF;
    refactorize(s);
    recompute_basic(s);     /* restore primal feasibility for the current basis */
    int r = solve_phase(s);
    /* Honest-status parity with solver_solve_impl: an iteration limit, a
       cooperative stop, or a factorization failure must surface as its own
       status, never fall through to the "optimal" return below (the previous
       version mapped a still-feasible r==-1 iteration-limit stop to a
       fabricated OPTIMAL). */
    if (r == SOLVE_NUMERICAL) { s->status_out = SOLVE_NUMERICAL; return SOLVE_NUMERICAL; }
    if (r == SOLVE_STOPPED)   { s->status_out = SOLVE_STOPPED;   return SOLVE_STOPPED; }
    if (r == -1)              { s->status_out = 3;               return 3; }
    int basis_valid = (s->use_sparse && s->sparse_ok) || (!s->use_sparse && s->lu_valid);
    if (r == 2 || !basis_valid || !solver_feasible(s)) { solver_refresh(s); return s->status_out; }
    double obj = 0.0;
    for (int i = 0; i < s->M; i++) obj += s->cobj[s->basis[i]] * s->x[s->basis[i]];
    for (int j = 0; j < s->N; j++)
        if (s->status[j] != LP_BASIC && s->status[j] != LP_REMOVED)
            obj += s->cobj[j] * s->x[j];
    s->objval = obj;
    s->status_out = 0;
    return 0;
}

int solver_add_row(Solver *s, const double *a, double rhs, char rel)
{
    if(!s||!a||!isfinite(rhs))return -1;
    int m = s->M;
    /* F-06: guard against dimension overflow / abuse before allocating. */
    if (s->n_orig <= 0 || m < 0 || m >= 1000000) return -1;
    for(int j=0;j<s->n_orig;j++)if(!isfinite(a[j]))return -1;
    if (rel != '<' && rel != '>' && rel != '=') return -1;

    LP lp;
    if (solver_export_lp(s, a, rhs, rel, &lp) != 0) return -1;
    Solver *fresh = solver_create(&lp);
    if (!fresh) { free_exported_lp(&lp); return -1; }
    fresh->iteration_limit = s->iteration_limit;
    fresh->reinvert_interval = s->reinvert_interval;
    fresh->hyper_tol = s->hyper_tol;
    int r = solver_solve(fresh);
    Solver hold = *fresh; *fresh = *s; *s = hold;
    solver_destroy(fresh);     /* frees the old *s data now living in fresh */
    free_exported_lp(&lp);
    return r;
}

/* Primal-feasibility check of the current solution. */
int solver_feasible(const Solver *s)
{
    if(!s)return 0;
    int N = s->N, M = s->M;
    const double tol = 1e-6 * (1.0 + fabs(s->objval));
    for (int j = 0; j < N; j++) {
        if (s->status[j] == LP_REMOVED) continue;
        if (s->x[j] < s->l[j] - tol || s->x[j] > s->u[j] + tol) return 0;
    }
    /* A_eq x == beq */
    double *res = (double*)psolve_calloc((size_t)M, sizeof(double));
    for (int j = 0; j < N; j++) {
        if (s->status[j] == LP_REMOVED) continue;
        double xj = s->x[j];
        for (int k = s->colptr[j]; k < s->colptr[j+1]; k++)
            res[s->row[k]] += s->val[k] * xj;
    }
    int ok = 1;
    for (int i = 0; i < M; i++)
        if (fabs(res[i] - s->beq[i]) > 1e-5 * (1.0 + fabs(s->beq[i]))) { ok = 0; break; }
    psolve_free(res);
    return ok;
}
