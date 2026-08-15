#include "qp.h"
#include "lu.h"
#include "err.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

static double *xmalloc(size_t n){ return psolve_malloc(n); }

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
    /* One scratch block for K, its pristine copy, and the two right-hand
       sides: this runs once per active-set iteration, and the solver is meant
       for per-frame use, so keep it to a single allocation. */
    size_t nd = 2 * (size_t)N * N + 2 * (size_t)N;
    double *blk = (double*)psolve_calloc(nd, sizeof(double));  /* zeroed: the bottom-right block must be 0 */
    double *K    = blk;
    double *K0   = blk + (size_t)N * N;
    double *rhs  = blk + 2 * (size_t)N * N;
    double *rhs0 = rhs + N;
    int *piv = (int*)xmalloc((size_t)N * sizeof(int));
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

    /* Keep a pristine copy: LU overwrites K, and the residual check below
       needs the original matrix. */
    memcpy(K0, K, (size_t)N * N * sizeof(double));
    memcpy(rhs0, rhs, (size_t)N * sizeof(double));
    double rhsnorm = 0.0;
    for (int i = 0; i < N; i++) rhsnorm = fmax(rhsnorm, fabs(rhs0[i]));

    /* A singular Q (only PSD is promised, not PD) makes the KKT matrix
       singular.  lu_factor() does not always *fail* on it -- it can come back
       with a tiny pivot and a wildly inaccurate solve, which used to be taken
       at face value.  The step then left the working-set constraints, and the
       iterate drifted out of the feasible region while still being reported as
       solved.  So: solve, measure the residual, and if it is not small,
       regularize the Q block and try again. */
    int r = lu_factor(K, N, piv);
    int good = 0;
    for (int attempt = 0; attempt < 4 && !good; attempt++) {
        if (r == 0) {
            lu_solve(K, piv, N, rhs, rhs);
            double resid = 0.0;
            for (int i = 0; i < N; i++) {
                double sum = -rhs0[i];
                for (int j = 0; j < N; j++) sum += K0[j*N + i] * rhs[j];
                resid = fmax(resid, fabs(sum));
            }
            double xnorm = 0.0;
            for (int i = 0; i < N; i++) xnorm = fmax(xnorm, fabs(rhs[i]));
            if (resid <= 1e-8 * (1.0 + rhsnorm + xnorm)) good = 1;
        }
        if (!good && attempt < 3) {
            /* regularize the Q block with increasing strength.  A singular PSD Q
               makes the KKT matrix singular; larger regularization lets the
               active-set take a null-space-aware step rather than failing. */
            memcpy(K, K0, (size_t)N * N * sizeof(double));
            double reg = (attempt == 0) ? 1e-8 : ((attempt == 1) ? 1e-6 : 1e-4);
            for (int i = 0; i < n; i++) K[i*N + i] += reg;
            memcpy(K0, K, (size_t)N * N * sizeof(double));
            memcpy(rhs, rhs0, (size_t)N * sizeof(double));
            r = lu_factor(K, N, piv);
        }
    }
    if (!good) { psolve_free(blk); psolve_free(piv); return -1; }
    for (int i = 0; i < n; i++) p[i] = rhs[i];
    for (int c = 0; c < k; c++) mu[c] = rhs[n+c];

    psolve_free(blk); psolve_free(piv);
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
    int status = -1;   /* default: not solved */

    for (it = 0; it < maxit; it++) {
        /* Cooperative abort (time limit / Ctrl-C): polled every iteration so a
           per-frame QP can be cut off at the requested budget.  On stop we hand
           back the current -- still feasible -- iterate as a best incumbent but
           do NOT claim optimality. */
        if (psolve_stop()) { status = QP_STOPPED; break; }
        eval_grad(qp, x, g);
        if (solve_kkt(qp, W, k, g, p, mu) != 0) {
            if (k > 0) { k--; build_orth(qp, W, k, orth); continue; }
            status = QP_KKT_FAIL; break;
        }
        double pnorm = 0.0;
        for (int i = 0; i < n; i++) pnorm += p[i]*p[i];
        pnorm = sqrt(pnorm);
        double scale = 1.0 + fabs(eval_obj(qp, x));
        /* Divergence guard: the iterate has run away (typically an unbounded
           ray we could not certify).  Stop instead of letting the objective --
           and every objective-relative tolerance with it -- blow up. */
        {
            double xmax = 0.0;
            for (int i = 0; i < n; i++) xmax = fmax(xmax, fabs(x[i]));
            if (!(xmax < 1e14)) { status = QP_ITERATION_LIMIT; break; }
        }
        if (pnorm < 1e-9 * scale) {
            int drop = -1; double minmu = 0.0;
            for (int c = 0; c < k; c++)
                if (mu[c] < minmu - 1e-9 * scale) { minmu = mu[c]; drop = c; }
            if (drop < 0) {
                /* candidate optimum: verify the KKT stationarity residual
                   before certifying success, so a bad KKT solve cannot be
                   reported as optimal. */
                double kkt = 0.0, gmax = 0.0, tmax = 0.0;
                for (int j = 0; j < n; j++) {
                    double rj = g[j];
                    gmax = fmax(gmax, fabs(g[j]));
                    for (int c = 0; c < k; c++) {
                        double t = qp->A[(size_t)W[c]*n + j] * mu[c];
                        rj += t;
                        tmax = fmax(tmax, fabs(t));
                    }
                    kkt = fmax(kkt, fabs(rj));
                }
                /* Scale the stationarity tolerance by the size of the terms
                   being cancelled, NOT by the objective value.  The old
                   1e-6*(1+|obj|) grew with a diverging iterate: on an
                   unbounded QP the objective ran to 1e36, the tolerance with
                   it, and a meaningless point passed as optimal. */
                double tol = 1e-7 * (1.0 + gmax + tmax);
                if (kkt > tol) { status = QP_KKT_FAIL; break; }
                /* Complementary slackness: a multiplier may only be attached
                   to a constraint that is actually active at x. */
                int comp_ok = 1;
                for (int c = 0; c < k; c++) {
                    double rr = row_resid(qp, W[c], x);
                    if (fabs(rr) > 1e-7 * (1.0 + fabs(qp->b[W[c]]))) { comp_ok = 0; break; }
                }
                if (!comp_ok) { status = QP_KKT_FAIL; break; }
                /* Stationarity alone is not a solution: the point must also be
                   PRIMAL FEASIBLE.  Rounding drift in the KKT steps (worst on
                   a singular Q, or on duplicated/parallel rows where only one
                   of the pair is rank-independent enough to enter the working
                   set) can leave a constraint violated.  Report a failure
                   rather than a stationary point outside the feasible set. */
                int infeas = 0;
                for (int i = 0; i < m; i++) {
                    double rr = row_resid(qp, i, x);
                    if (rr > 1e-7 * (1.0 + fabs(qp->b[i]))) { infeas = 1; break; }
                }
                if (infeas) { status = QP_KKT_FAIL; break; }
                for (int c = 0; c < k; c++) res->mult[W[c]] = mu[c];
                status = 0; break;
            }
            W[drop] = W[k-1]; k--;
            build_orth(qp, W, k, orth);
            continue;
        }
        /* Unboundedness certificate.  For a convex QP the objective is
           unbounded below exactly when some direction d satisfies
           Q d = 0, A d <= 0 and g.d < 0.  Without this test the loop simply
           walks along such a ray until the iteration cap and reports
           ITERATION_LIMIT -- honest, but uninformative and ~4000 wasted
           iterations (measurably: a 2-variable Q=0 case did 12917 mallocs).
           Tolerances are deliberately strict: if the ray cannot be certified
           we fall through to the old behaviour rather than risk a wrong
           UNBOUNDED. */
        {
            double gp = 0.0, pQp = 0.0, pinf = 0.0;
            for (int j = 0; j < n; j++) {
                gp += g[j] * p[j];
                pinf = fmax(pinf, fabs(p[j]));
            }
            if (gp < -1e-9 * scale && pinf > 0.0) {
                for (int i = 0; i < n; i++) {
                    double qi = 0.0;
                    for (int j = 0; j < n; j++) qi += qp->Q[(size_t)j*n + i] * p[j];
                    pQp += qi * p[i];
                }
                double qnorm = 0.0;
                for (int i = 0; i < n*n; i++) qnorm = fmax(qnorm, fabs(qp->Q[i]));
                if (pQp <= 1e-12 * (1.0 + qnorm) * pinf * pinf) {
                    int ray = 1;
                    for (int i = 0; i < m && ray; i++) {
                        const double *Ai = qp->A + (size_t)i * n;
                        double ap = 0.0, anorm = 0.0;
                        for (int j = 0; j < n; j++) {
                            ap += Ai[j] * p[j];
                            anorm = fmax(anorm, fabs(Ai[j]));
                        }
                        /* No tolerance here on purpose: a row with even a
                           rounding-level positive slope does eventually block
                           the ray, so claiming UNBOUNDED would be a wrong
                           answer.  Failing the test just falls back to the
                           iteration limit, which is never wrong. */
                        (void)anorm;
                        if (ap > 0.0) ray = 0;
                    }
                    if (ray) { status = 1; break; }   /* certified unbounded */
                }
            }
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
                /* A constraint that is already (numerically) violated gives a
                   negative ratio.  Taking that step would move the iterate
                   BACKWARDS along p -- uphill, and deeper out of the feasible
                   region.  Clamp to a zero-length blocking step, which is the
                   standard degenerate-step handling: the constraint enters the
                   working set and the next KKT solve moves along it. */
                if (r < 0.0) r = 0.0;
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
    if (status == -1) status = QP_ITERATION_LIMIT;   /* loop exhausted without KKT cert */
    res->iterations = it;
    res->obj = eval_obj(qp, x);
    res->status = status;
    psolve_free(W); psolve_free(orth); psolve_free(tmp); psolve_free(g); psolve_free(p); psolve_free(mu); psolve_free(xnew);
}

/* Find a feasible point for A x <= b, or accept a provided one.
 * Phase-I QP:  minimize 1/2||x||^2 + 1/2||s||^2 + sum s
 *   s.t.  a_i x - s_i <= b_i,  s_i >= 0.
 * Strictly convex so the active-set converges; the linear term on s drives
 * s -> 0 whenever a feasible x exists.
 * Returns 1 on success (writes x), 0 if no feasible point was certified, and 2
 * if the search was cooperatively stopped (time limit / Ctrl-C). */
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
    double *Q1 = (double*)psolve_calloc((size_t)N*N, sizeof(double));
    double *c1 = (double*)psolve_calloc((size_t)N, sizeof(double));
    /* minimize  sum s  +  eps/2*(||x||^2 + ||s||^2).  The linear term on s
       dominates (eps tiny), so s -> 0 whenever a feasible x exists; the
       tiny quadratic keeps the problem strictly convex and bounded. */
    for (int j = 0; j < N; j++) Q1[j*N + j] = eps;
    for (int i = 0; i < m; i++) c1[n+i] = 1.0;
    int m1 = 2 * m;
    double *A1 = (double*)psolve_calloc((size_t)m1*N, sizeof(double));
    double *b1 = (double*)psolve_calloc((size_t)m1, sizeof(double));
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < n; j++) A1[i*N + j] = qp->A[i*n + j];
        A1[i*N + (n+i)] = -1.0;
        b1[i] = qp->b[i];
        A1[(m+i)*N + (n+i)] = -1.0;
        b1[m+i] = 0.0;
    }
    QP q1; q1.n = N; q1.m = m1; q1.Q = Q1; q1.c = c1; q1.A = A1; q1.b = b1;
    double *z0 = (double*)psolve_calloc((size_t)N, sizeof(double));
    for (int i = 0; i < m; i++) z0[n+i] = (qp->b[i] < 0) ? -qp->b[i] : 0.0;
    q1.x0 = z0;
    QPResult r1; memset(&r1, 0, sizeof(r1));
    active_set(&q1, z0, &r1);
    int ok = 0;
    if (r1.status == QP_STOPPED) {
        qp_result_free(&r1);
        psolve_free(Q1); psolve_free(c1); psolve_free(A1); psolve_free(b1); psolve_free(z0);
        return 2;   /* stopped during Phase-I feasibility search */
    }
    if (r1.status == 0) {
        double ssum = 0.0;
        for (int i = 0; i < m; i++) ssum += r1.x[n+i];
        if (ssum <= 1e-7) {
            /* Trust but verify: check the candidate against the ORIGINAL rows
               instead of inferring feasibility from the slack sum. */
            int feas2 = 1;
            for (int i = 0; i < m; i++) {
                double rr = -qp->b[i];
                for (int j = 0; j < n; j++) rr += qp->A[(size_t)i*n + j] * r1.x[j];
                if (rr > 1e-7 * (1.0 + fabs(qp->b[i]))) { feas2 = 0; break; }
            }
            if (feas2) { for (int j = 0; j < n; j++) x[j] = r1.x[j]; ok = 1; }
        }
    }

    qp_result_free(&r1);
    psolve_free(Q1); psolve_free(c1); psolve_free(A1); psolve_free(b1); psolve_free(z0);
    return ok;
}

void qp_solve(const QP *qp, QPResult *res)
{
    if (!res) return;
    memset(res, 0, sizeof(*res));
    res->status = QP_INVALID;
    if (!qp || qp->n <= 0 || qp->m < 0 || qp->n > 8192 || qp->m > 1000000 ||
        !qp->Q || !qp->c || (qp->m > 0 && (!qp->A || !qp->b))) return;
    int n = qp->n;
    if ((size_t)n > (size_t)-1 / (size_t)n ||
        (qp->m > 0 && (size_t)n > (size_t)-1 / (size_t)qp->m)) return;
    for (int j = 0; j < n; j++)
        if (!isfinite(qp->c[j]) || (qp->x0 && !isfinite(qp->x0[j]))) return;
    for (size_t k = 0; k < (size_t)n * n; k++) if (!isfinite(qp->Q[k])) return;
    for (int i = 0; i < qp->m; i++) {
        if (!isfinite(qp->b[i])) return;
        for (int j = 0; j < n; j++) if (!isfinite(qp->A[(size_t)i*n+j])) return;
    }
    res->status = -1;
    /* Check Q for symmetry and 1x1/2x2 principal-minor positive semi-definiteness.
       Reject indefinite or non-symmetric Q immediately instead of iterating. */
    for (int i = 0; i < n; i++) {
        if (qp->Q[i*n+i] < -1e-9) {
            res->status = QP_NON_CONVEX;
            return;
        }
        for (int j = 0; j < n; j++) {
            double diff = fabs(qp->Q[i*n+j] - qp->Q[j*n+i]);
            double scale = fmax(1.0, fmax(fabs(qp->Q[i*n+j]), fabs(qp->Q[j*n+i])));
            if (diff > 1e-8 * scale) {
                res->status = QP_NON_CONVEX;
                return;
            }
        }
        for (int j = i + 1; j < n; j++) {
            double qii = fmax(0.0, qp->Q[i*n+i]);
            double qjj = fmax(0.0, qp->Q[j*n+j]);
            double qij = qp->Q[i*n+j];
            if (qii * qjj - qij * qij < -1e-8 * fmax(1.0, qii * qjj)) {
                res->status = QP_NON_CONVEX;
                return;
            }
        }
    }
    double *x = (double*)xmalloc((size_t)qp->n * sizeof(double));
    memset(x, 0, (size_t)qp->n * sizeof(double));
    int fr = find_feasible(qp, qp->x0, x);
    if (fr == 0) { psolve_free(x); return; }   /* status stays -1: no feasible start */
    if (fr == 2) {
        /* Cooperatively stopped while searching for a feasible point, so there
           is no feasible incumbent to hand back.  Report QP_STOPPED with an
           empty solution rather than a possibly-infeasible iterate. */
        res->status = QP_STOPPED;
        res->n = qp->n;
        res->x = NULL;
        res->obj = 0.0;
        res->iterations = 0;
        psolve_free(x);
        return;
    }
    active_set(qp, x, res);
    psolve_free(x);
}

void qp_result_free(QPResult *res)
{
    if (!res) return;
    psolve_free(res->x); psolve_free(res->mult);
    memset(res, 0, sizeof(*res));
}
