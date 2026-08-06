#include "qp.h"
#include "lu.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

static double *xmalloc(size_t n){ void*p=malloc(n); if(!p){fprintf(stderr,"OOM\n");exit(1);} return p; }

/* y = Q x + c  (Q column-major) */
static void eval_grad(const QP *qp, const double *x, double *g)
{
    int n = qp->n;
    for (int i = 0; i < n; i++) {
        double s = 0;
        const double *Qi = qp->Q + i;
        for (int j = 0; j < n; j++) s += Qi[j*n] * x[j];
        g[i] = s + qp->c[i];
    }
}

static double eval_obj(const QP *qp, const double *x)
{
    int n = qp->n;
    double s = 0;
    for (int i = 0; i < n; i++) {
        double qi = 0;
        const double *Qi = qp->Q + i;
        for (int j = 0; j < n; j++) qi += Qi[j*n] * x[j];
        s += 0.5 * qi * x[i];
    }
    for (int i = 0; i < n; i++) s += qp->c[i] * x[i];
    return s;
}

static double row_resid(const QP *qp, int i, const double *x)
{
    const double *Ai = qp->A + (size_t)i * qp->n;
    double s = -qp->b[i];
    for (int j = 0; j < qp->n; j++) s += Ai[j] * x[j];
    return s;
}

/* Build & factor KKT, solve [Q A^T; A 0][p;mu]=[-g;0]. Returns 0 ok, -1 singular. */
static int solve_kkt(const QP *qp, const int *W, int k,
                     const double *g, double *p, double *mu)
{
    int n = qp->n;
    int N = n + k;
    double *K = (double*)calloc((size_t)N * N, sizeof(double));   /* zeroed: bottom-right block must be 0 */
    int *piv = (int*)xmalloc((size_t)N * sizeof(int));
    double *rhs = (double*)xmalloc((size_t)N * sizeof(double));
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++) K[j*N + i] = qp->Q[j*n + i];
    for (int c = 0; c < k; c++) {
        const double *Arow = qp->A + (size_t)W[c] * n;
        for (int i = 0; i < n; i++) {
            K[(n+c)*N + i] = Arow[i];
            K[i*N + (n+c)] = Arow[i];
        }
    }
    for (int i = 0; i < n; i++) rhs[i] = -g[i];
    for (int c = 0; c < k; c++) rhs[n+c] = 0.0;

    int r = lu_factor(K, N, piv);
    if (r != 0) {   /* regularize Q block (handles PSD) */
        for (int i = 0; i < n; i++) K[i*N + i] += 1e-8;
        r = lu_factor(K, N, piv);
    }
    if (r != 0) { free(K); free(piv); free(rhs); return -1; }
    lu_solve(K, piv, N, rhs, rhs);
    for (int i = 0; i < n; i++) p[i] = rhs[i];
    for (int c = 0; c < k; c++) mu[c] = rhs[n+c];

    free(K); free(piv); free(rhs);
    return 0;
}

/* Build an orthonormal basis of the working-set rows (for rank management).
 * orth is n*k, column j = orthonormal basis vector for W[j]. */
static void build_orth(const QP *qp, const int *W, int k, double *orth)
{
    int n = qp->n;
    double tol = 1e-9;
    for (int c = 0; c < k; c++) {
        const double *Arow = qp->A + (size_t)W[c] * n;
        double *v = orth + (size_t)c * n;
        for (int i = 0; i < n; i++) v[i] = Arow[i];
        for (int d = 0; d < c; d++) {
            const double *u = orth + (size_t)d * n;
            double dp = 0; for (int i = 0; i < n; i++) dp += u[i]*v[i];
            for (int i = 0; i < n; i++) v[i] -= dp * u[i];
        }
        double nrm = 0; for (int i = 0; i < n; i++) nrm += v[i]*v[i];
        nrm = sqrt(nrm);
        if (nrm > tol) for (int i = 0; i < n; i++) v[i] /= nrm;
    }
}

/* Active-set core.  Requires xstart feasible.  Writes solution to res->x.
 * The working set W is kept row-independent (rank-managed) so the KKT system
 * is never singular; linearly dependent active constraints are skipped. */
static void active_set(const QP *qp, const double *xstart, QPResult *res)
{
    int n = qp->n, m = qp->m;
    res->n = n;
    res->x = (double*)xmalloc((size_t)n * sizeof(double));
    res->mult = (double*)xmalloc((size_t)m * sizeof(double));
    for (int i = 0; i < m; i++) res->mult[i] = 0.0;
    double *x = res->x;
    memcpy(x, xstart, (size_t)n * sizeof(double));

    int *W = (int*)xmalloc((size_t)m * sizeof(int));
    double *orth = (double*)xmalloc((size_t)n * n * sizeof(double));
    double *tmp = (double*)xmalloc((size_t)n * sizeof(double));
    double tolrank = 1e-9;
    int k = 0;
    build_orth(qp, W, 0, orth);
    for (int i = 0; i < m; i++) {
        if (row_resid(qp, i, x) > -1e-7) {
            const double *Arow = qp->A + (size_t)i * n;
            for (int j = 0; j < n; j++) tmp[j] = Arow[j];
            for (int d = 0; d < k; d++) {
                const double *u = orth + (size_t)d * n;
                double dp = 0; for (int j = 0; j < n; j++) dp += u[j]*tmp[j];
                for (int j = 0; j < n; j++) tmp[j] -= dp * u[j];
            }
            double nrm = 0; for (int j = 0; j < n; j++) nrm += tmp[j]*tmp[j];
            if (sqrt(nrm) > tolrank) { W[k] = i; k++; build_orth(qp, W, k, orth); }
        }
    }

    double *g = (double*)xmalloc((size_t)n * sizeof(double));
    double *p = (double*)xmalloc((size_t)n * sizeof(double));
    double *mu = (double*)xmalloc((size_t)m * sizeof(double));
    double *xnew = (double*)xmalloc((size_t)n * sizeof(double));
    int maxit = 4000 + 100 * (n + m);
    int it = 0;

    for (it = 0; it < maxit; it++) {
        eval_grad(qp, x, g);
        if (solve_kkt(qp, W, k, g, p, mu) != 0) {
            if (k > 0) { k--; build_orth(qp, W, k, orth); continue; }
            res->status = -1; break;
        }
        double pnorm = 0.0;
        for (int i = 0; i < n; i++) pnorm += p[i]*p[i];
        pnorm = sqrt(pnorm);
        double scale = 1.0 + fabs(eval_obj(qp, x));
        if (pnorm < 1e-9 * scale) {
            int drop = -1; double minmu = 0.0;
            for (int c = 0; c < k; c++)
                if (mu[c] < minmu - 1e-9 * scale) { minmu = mu[c]; drop = c; }
            if (drop < 0) { for (int c = 0; c < k; c++) res->mult[W[c]] = mu[c]; break; }
            W[drop] = W[k-1]; k--;
            build_orth(qp, W, k, orth);
            continue;
        }
        double alpha = 1.0; int block = -1;
        for (int i = 0; i < m; i++) {
            int inW = 0;
            for (int c = 0; c < k; c++) if (W[c] == i) { inW = 1; break; }
            if (inW) continue;
            const double *Ai = qp->A + (size_t)i * n;
            double ap = 0.0;
            for (int j = 0; j < n; j++) ap += Ai[j] * p[j];
            if (ap > 1e-12) {
                double r = -row_resid(qp, i, x) / ap;
                if (r < alpha - 1e-10) { alpha = r; block = i; }
            }
        }
        for (int i = 0; i < n; i++) xnew[i] = x[i] + alpha * p[i];
        memcpy(x, xnew, (size_t)n * sizeof(double));
        if (alpha < 1.0 - 1e-10 && block >= 0) {
            const double *Arow = qp->A + (size_t)block * n;
            for (int j = 0; j < n; j++) tmp[j] = Arow[j];
            for (int d = 0; d < k; d++) {
                const double *u = orth + (size_t)d * n;
                double dp = 0; for (int j = 0; j < n; j++) dp += u[j]*tmp[j];
                for (int j = 0; j < n; j++) tmp[j] -= dp * u[j];
            }
            double nrm = 0; for (int j = 0; j < n; j++) nrm += tmp[j]*tmp[j];
            if (sqrt(nrm) > tolrank) { W[k] = block; k++; build_orth(qp, W, k, orth); }
        }
    }
    res->iterations = it;
    res->obj = eval_obj(qp, x);
    res->status = 0;
    free(W); free(orth); free(tmp); free(g); free(p); free(mu); free(xnew);
}

/* Find a feasible point for A x <= b, or accept a provided one.
 * Phase-I QP:  minimize 1/2||x||^2 + 1/2||s||^2 + sum s
 *   s.t.  a_i x - s_i <= b_i,  s_i >= 0.
 * Strictly convex so the active-set converges; the linear term on s drives
 * s -> 0 whenever a feasible x exists.  Returns 1 on success (writes x). */
static int find_feasible(const QP *qp, const double *x0, double *x)
{
    int n = qp->n, m = qp->m;
    const double *base = x0 ? x0 : x;
    memcpy(x, base, (size_t)n * sizeof(double));
    int feas = 1;
    for (int i = 0; i < m; i++)
        if (row_resid(qp, i, x) > 1e-8) { feas = 0; break; }
    if (feas) return 1;

    int N = n + m;
    double eps = 1e-6;
    double *Q1 = (double*)calloc((size_t)N*N, sizeof(double));
    double *c1 = (double*)calloc((size_t)N, sizeof(double));
    /* minimize  sum s  +  eps/2*(||x||^2 + ||s||^2).  The linear term on s
       dominates (eps tiny), so s -> 0 whenever a feasible x exists; the
       tiny quadratic keeps the problem strictly convex and bounded. */
    for (int j = 0; j < N; j++) Q1[j*N + j] = eps;
    for (int i = 0; i < m; i++) c1[n+i] = 1.0;
    int m1 = 2 * m;
    double *A1 = (double*)calloc((size_t)m1*N, sizeof(double));
    double *b1 = (double*)calloc((size_t)m1, sizeof(double));
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < n; j++) A1[i*N + j] = qp->A[i*n + j];
        A1[i*N + (n+i)] = -1.0;
        b1[i] = qp->b[i];
        A1[(m+i)*N + (n+i)] = -1.0;
        b1[m+i] = 0.0;
    }
    QP q1; q1.n = N; q1.m = m1; q1.Q = Q1; q1.c = c1; q1.A = A1; q1.b = b1;
    double *z0 = (double*)calloc((size_t)N, sizeof(double));
    for (int i = 0; i < m; i++) z0[n+i] = (qp->b[i] < 0) ? -qp->b[i] : 0.0;
    q1.x0 = z0;
    QPResult r1; memset(&r1, 0, sizeof(r1));
    active_set(&q1, z0, &r1);
    int ok = 0;
    if (r1.status == 0) {
        double ssum = 0.0;
        for (int i = 0; i < m; i++) ssum += r1.x[n+i];
        if (ssum <= 1e-7) { for (int j = 0; j < n; j++) x[j] = r1.x[j]; ok = 1; }
    }

    qp_result_free(&r1);
    free(Q1); free(c1); free(A1); free(b1); free(z0);
    return ok;
}

void qp_solve(const QP *qp, QPResult *res)
{
    memset(res, 0, sizeof(*res));
    res->status = -1;
    double *x = (double*)xmalloc((size_t)qp->n * sizeof(double));
    memset(x, 0, (size_t)qp->n * sizeof(double));
    if (!find_feasible(qp, qp->x0, x)) { free(x); return; }
    active_set(qp, x, res);
    free(x);
}

void qp_result_free(QPResult *res)
{
    if (!res) return;
    free(res->x); free(res->mult);
    memset(res, 0, sizeof(*res));
}
