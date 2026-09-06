#include "qp.h"
#include "lu.h"
#include "solver.h"
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

/* Size of the terms that must cancel in row i at x: 1 + |b_i| + |a_i|^T |x|.
 * Every feasibility/activity tolerance in this file is relative to this, not to
 * an absolute constant: the same code has to answer for a UI layout measured in
 * pixels (1e0..1e3, curv-ps) and for geometry measured in nanometres, and an
 * absolute 1e-8 decides "feasible" by the unit of measure in the latter case and
 * never decides it in the former. */
static double row_scale(const QP *qp, int i, const double *x)
{
    double s = fabs(qp->b[i]);
    const double *Ai = qp->A + (size_t)i * qp->n;
    for (int j = 0; j < qp->n; j++) s += fabs(Ai[j]) * fabs(x[j]);
    return 1.0 + s;
}

/* 1 when row i is violated at x beyond the relative tolerance. */
static int row_violated(const QP *qp, int i, const double *x)
{
    /* 1e-11 * row_scale is ~100x the rounding noise of a_i^T x and, at the
       pixel scale a UI layout uses, lands on the 1e-8 this file historically
       used.  Looser than that and a materially infeasible "warm start" is
       accepted; the active set then stalls on the violated rows (measured: an
       8-chip model at scale 1e3 went OPTIMAL-in-16-iters -> ITER_LIMIT-in-8100
       when 1e-9 was tried), which trades a wrong verdict for a useless one. */
    return row_resid(qp, i, x) > 1e-11 * row_scale(qp, i, x);   /* TOLSHEET TOL-QP-WARMFEAS */
}

int qp_start_feasible(const QP *qp, const double *x)
{
    if (!qp || !x) return 0;
    for (int i = 0; i < qp->m; i++)
        if (row_violated(qp, i, x)) return 0;
    return 1;
}

/* Phase-I hand-over test: "feasible well enough to hand the active set a
 * start".  Looser than qp_start_feasible on purpose -- the simplex answers at
 * its own pivot tolerance (1e-9, src/solver.c TOL_FEAS), which is coarser than
 * the rounding floor a *caller's* cached warm start has to clear, and the
 * active set carries its own primal verification that rejects a bad start with
 * KKT_FAIL rather than a wrong OPTIMAL.  One tolerance for both decisions made
 * the LP route throw away starts it had legitimately found. */
static int phase1_point_ok(const QP *qp, const double *x)
{
    for (int i = 0; i < qp->m; i++)
        if (row_resid(qp, i, x) > 1e-8 * row_scale(qp, i, x)) return 0;   /* TOLSHEET TOL-QP-P1HANDOVER */
    return 1;
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

/* Verify a candidate Farkas certificate of infeasibility for Ax <= b:
 *     lambda >= 0,   A^T lambda ~ 0,   b^T lambda < 0.
 * (Theorem of alternatives: any x with Ax <= b would give
 *  0 <= x^T A^T lambda = lambda^T A x <= lambda^T b < 0, a contradiction.)
 *
 * `dual` is the LP's per-row multiplier vector.  Its per-row sign is an
 * internal convention of the simplex driver -- rows are rescaled so their right
 * side is nonnegative, and prices are negated for a maximisation -- so rather
 * than couple this file to that, every cheap candidate combination is tried and
 * the arithmetic on the CALLER's data decides.  A candidate that does not verify
 * is never reported: losing the proof only downgrades the verdict to "no
 * feasible start found", which is what the "trust but verify" rule at the top of
 * this file is for, and it means a future change of convention in the LP core
 * costs a rescue rather than producing a wrong answer.
 * Tolerances are relative to the magnitudes of the products they compare, so
 * they carry no unit of measure.  Returns 1 with the normalised lambda (max
 * |lambda_i| = 1, so rows with lambda_i > 0 name a conflicting subset) in `out`. */
static int farkas_verify(const QP *qp, const double *dual, double *out)
{
    const int n = qp->n, m = qp->m;
    for (int combo = 0; combo < 4; combo++) {
        int flip  = combo & 1;            /* negate all                     */
        int byrow = (combo >> 1) & 1;     /* negate rows whose b_i is < 0   */
        double lam_max = 0.0, lmin = 1e300;   /* TOLSHEET TOL-QP-FARKASINIT */
        for (int i = 0; i < m; i++) {
            double v = dual[i];
            if (byrow && qp->b[i] < 0.0) v = -v;
            if (flip) v = -v;
            out[i] = v;
            lam_max = fmax(lam_max, fabs(v));
            if (v < lmin) lmin = v;
        }
        if (!(lam_max > 0.0)) continue;
        if (lmin < -1e-12 * lam_max) continue;        /* requires lambda >= 0; TOLSHEET TOL-QP-FARKASSIGN */
        for (int i = 0; i < m; i++) out[i] /= lam_max;      /* scale-free checks below */
        double colmax = 0.0;
        for (int j = 0; j < n; j++) {
            double sv = 0.0, mag = 0.0;
            for (int i = 0; i < m; i++) {
                double a = qp->A[(size_t)i*n + j];
                sv  += a * out[i];
                mag += fabs(a) * out[i];
            }
            colmax = fmax(colmax, fabs(sv) / (1.0 + mag));  /* A^T lambda ~ 0 */
        }
        if (colmax > 1e-7) continue;                     /* TOLSHEET TOL-QP-FARKASCOL */
        double bt = 0.0, bmag = 0.0;
        for (int i = 0; i < m; i++) { bt += out[i]*qp->b[i]; bmag += out[i]*fabs(qp->b[i]); }
        if (bt < -1e-9 * (1.0 + bmag)) return 1;      /* b^T lambda < 0; TOLSHEET TOL-QP-FARKASB */
    }
    return 0;
}

/* Phase-I through the LP core:
 *      minimize    sum_i s_i
 *      subject to  A x - s <= b,  s >= 0,  x free.
 * Feasible iff the optimum is 0 (any x is feasible for the LP itself by taking
 * s large, so the LP never has to answer "infeasible"), and bounded below, so
 * only `OPTIMAL` carries information.  Two answers come out of it:
 *   - a point x with the least total row violation: if it clears
 *     qp_start_feasible(), it is a start for the active set (from x0 the search
 *     happens in the residual space of x0, i.e. for the shift d = x - x0, so a
 *     drag frame's near-feasible point costs a couple of pivots);
 *   - with a positive optimum, the row multipliers give a Farkas certificate
 *     (see farkas_verify) turning "no feasible start" into a proof.
 * Returns 1 (x written, feasible start), 2 (infeasible, proven; lambda in
 * `farkas`), 0 (no verdict: give the caller's fallback a chance), 3 on stop. */
static int lp_phase1_once(const QP *qp, const double *x0, double *x, double *farkas,
                          double boxmul)
{
    int n = qp->n, m = qp->m;
    if (m <= 0) return 0;
    long nnz = m;                         /* one entry per slack column */
    for (int i = 0; i < m; i++)
        for (int j = 0; j < n; j++) if (qp->A[(size_t)i*n+j]) nnz++;
    int nL = n + m;
    int *colptr = (int*)psolve_malloc(sizeof(int) * (size_t)(nL + 1));
    int *rowi   = (int*)psolve_malloc(sizeof(int) * (size_t)nnz);
    double *val = (double*)psolve_malloc(sizeof(double) * (size_t)nnz);
    double *cL  = (double*)psolve_calloc((size_t)nL, sizeof(double));
    double *lL  = (double*)psolve_malloc(sizeof(double) * (size_t)nL);
    double *uL  = (double*)psolve_malloc(sizeof(double) * (size_t)nL);
    double *bL  = (double*)psolve_malloc(sizeof(double) * (size_t)m);
    double *xL  = (double*)psolve_malloc(sizeof(double) * (size_t)nL);
    double *dual= (double*)psolve_malloc(sizeof(double) * (size_t)m);
    char *rel   = (char*)psolve_malloc((size_t)m);
    if (!colptr || !rowi || !val || !cL || !lL || !uL || !bL || !xL || !dual || !rel) {
        psolve_free(colptr); psolve_free(rowi); psolve_free(val); psolve_free(cL);
        psolve_free(lL); psolve_free(uL); psolve_free(bL); psolve_free(xL);
        psolve_free(dual); psolve_free(rel);
        return 0;                          /* allocation failed: no verdict */
    }
    long p = 0;
    for (int j = 0; j < n; j++) {          /* x columns: A^T column j */
        colptr[j] = (int)p;
        for (int i = 0; i < m; i++) {
            double a = qp->A[(size_t)i*n + j];
            if (a) { rowi[p] = i; val[p] = a; p++; }
        }
    }
    for (int i = 0; i < m; i++) {          /* slack column i: -1 in row i */
        colptr[n+i] = (int)p; rowi[p] = i; val[p] = -1.0; p++;
    }
    colptr[nL] = (int)p;
    /* maximize -sum s == minimize sum s */
    for (int i = 0; i < m; i++) cL[n+i] = -1.0;
    /* The QP's variables are free, but handing the simplex ±LP_INF (1e30)
     * bounds on them is what makes this Phase-I LP fail: the x = x+ - x- split
     * of a free column then carries magnitudes ~1e30 whose difference is O(1),
     * and the resulting cancellation breaks solver_feasible()'s certificate
     * (measured on the UI-layout family: 1e30 bounds -> NUMERICAL_FAILURE on
     * 11 of 16 models, a data-derived box -> all 16 solved).  The box is a
     * search bound only, never a claim about the model: a start it produces is
     * verified against the caller's rows (phase1_point_ok), and infeasibility is
     * only ever reported from the verified Farkas certificate, which is stated
     * for A x <= b alone.  So a too-tight box can cost a rescue, not a verdict. */
    double bmax = 0.0;
    for (int i = 0; i < m; i++) bmax = fmax(bmax, fabs(qp->b[i]));
    double box = boxmul * (1.0 + bmax);                  /* TOLSHEET TOL-QP-BOX */
    double *bx = (double*)psolve_malloc(sizeof(double) * (size_t)n);
    for (int j = 0; j < n; j++) {
        double aj = 0.0;
        for (int i = 0; i < m; i++) aj = fmax(aj, fabs(qp->A[(size_t)i*n + j]));
        double u = (aj > 1e-300) ? box / aj : box;   /* TOLSHEET TOL-QP-BOXDIV */
        if (!(u > 1.0)) u = 1.0;
        if (u > 1e100) u = 1e100;                   /* TOLSHEET TOL-QP-BOXCAP */
        if (bx) bx[j] = u;
        lL[j] = -u; uL[j] = u;
    }
    for (int i = 0; i < m; i++) { lL[n+i] = 0.0; uL[n+i] = LP_INF; }     /* s >= 0 */
    /* right-hand side: with a warm start the search runs in the shift space
       d = x - x0, so a drag frame's near-feasible point costs a couple of
       pivots instead of a full feasibility solve */
    for (int i = 0; i < m; i++) {
        double ax0 = 0.0;
        if (x0) {
            const double *Ai = qp->A + (size_t)i*n;
            for (int j = 0; j < n; j++) ax0 += Ai[j] * x0[j];
        }
        bL[i] = qp->b[i] - ax0;
    }
    for (int i = 0; i < m; i++) rel[i] = '<';

    LP lp; memset(&lp, 0, sizeof lp);
    lp.n = nL; lp.m = m; lp.c = cL; lp.Acolptr = colptr; lp.Arow = rowi;
    lp.Aval = val; lp.rel = rel; lp.b = bL; lp.l = lL; lp.u = uL; lp.maximize = 1;
    Solver *s = solver_create(&lp);
    int got = 0;
    if (!s) { psolve_free(colptr); psolve_free(rowi); psolve_free(val); psolve_free(cL);
              psolve_free(lL); psolve_free(uL); psolve_free(bL); psolve_free(xL);
              psolve_free(dual); psolve_free(rel); return 0; }
    int st = solver_solve(s);
    if (st == SOLVE_STOPPED) got = 3;
    if (st == 0) {
        double obj = 0.0;
        solver_optimum(s, xL, &obj);
        for (int j = 0; j < n; j++) x[j] = (x0 ? x0[j] : 0.0) + xL[j];
        if (phase1_point_ok(qp, x)) got = 1;
        else if (farkas && bx) {
            /* The duals certify infeasibility of the BOXED system.  They are a
             * certificate for the caller's Ax <= b only if the box played no
             * part at this vertex (complementary slackness: a variable strictly
             * inside its bounds has zero box multiplier, so A^T y = 0 holds for
             * the unboxed LP too).  Checked with a wide margin, then verified
             * numerically anyway -- a certificate that does not check out is
             * never reported. */
            int box_touch = 0;
            for (int j = 0; j < n; j++)
                if (fabs(xL[j]) > 0.25 * bx[j]) { box_touch = 1; break; }   /* TOLSHEET TOL-QP-BOXIDLE */
            if (!box_touch) {
            solver_duals(s, dual);
            if (farkas_verify(qp, dual, farkas)) {
                got = 2;
            }
            }
        }
    }
    solver_destroy(s);
    psolve_free(colptr); psolve_free(rowi); psolve_free(val); psolve_free(cL);
    psolve_free(lL); psolve_free(uL); psolve_free(bL); psolve_free(xL);
    psolve_free(dual); psolve_free(rel); psolve_free(bx);
    return got;
}

/* A second route to the certificate: solve the Farkas alternative itself.
 *
 * The boxed Phase-I LP above asks the simplex for a point and reads the
 * infeasibility verdict off the *duals* of that problem; when the simplex
 * cannot certify its own answer (SOLVE_NUMERICAL -- typically the badly scaled
 * free-variables-plus-slacks basis) the certificate is lost even though the
 * conflict is obvious.  So ask for the certificate directly:
 *
 *     minimise    b^T lambda
 *     subject to  A^T lambda = 0,   sum_i lambda_i >= 1,   0 <= lambda <= 1
 *
 * which is Farkas' alternative for {x : Ax <= b}.  Its optimum is < 0 exactly
 * when the row system is infeasible, and the argmin IS the certificate.  Every
 * variable is bounded in [0,1] and the only structural entries come from A, so
 * this LP is as well scaled as the model itself -- no box ladder, no free
 * columns, nothing for the 1e30 business to go wrong in.  The candidate is
 * still verified against the caller's data (non-negativity is already forced by
 * the bounds, but A^T lambda ~ 0 and b^T lambda < 0 are checked), so a
 * misbehaving simplex costs the proof rather than creating a false one. */
static int lp_farkas(const QP *qp, double *farkas)
{
    const int n = qp->n, m = qp->m, nL = m, rows = n + 1;
    if (m <= 0 || n <= 0 || !farkas) return 0;
    int nnz = 0;
    for (int i = 0; i < m; i++)
        for (int j = 0; j < n; j++) if (qp->A[(size_t)i*n + j] != 0.0) nnz++;
    nnz += nL;                                     /* the >= row touches each lambda */
    int *colptr = (int*)psolve_malloc(sizeof(int) * (size_t)(nL + 1));
    int *rowi   = (int*)psolve_malloc(sizeof(int) * (size_t)nnz);
    double *val = (double*)psolve_malloc(sizeof(double) * (size_t)nnz);
    double *cL  = (double*)psolve_malloc(sizeof(double) * (size_t)nL);
    double *lL  = (double*)psolve_malloc(sizeof(double) * (size_t)nL);
    double *uL  = (double*)psolve_malloc(sizeof(double) * (size_t)nL);
    double *bL  = (double*)psolve_malloc(sizeof(double) * (size_t)rows);
    double *xL  = (double*)psolve_malloc(sizeof(double) * (size_t)nL);
    char *rel   = (char*)psolve_malloc((size_t)rows);
    if (!colptr || !rowi || !val || !cL || !lL || !uL || !bL || !xL || !rel) {
        psolve_free(colptr); psolve_free(rowi); psolve_free(val); psolve_free(cL);
        psolve_free(lL); psolve_free(uL); psolve_free(bL); psolve_free(xL); psolve_free(rel);
        return 0;
    }
    int pos = 0;
    for (int j = 0; j < nL; j++) {
        colptr[j] = pos;
        for (int k = 0; k < n; k++) {
            double a = qp->A[(size_t)j*n + k];
            if (a != 0.0) { rowi[pos] = k; val[pos] = a; pos++; }
        }
        rowi[pos] = n; val[pos] = 1.0; pos++;        /* sum_i lambda_i >= 1 */
        cL[j] = -qp->b[j];                           /* max -b^T lambda = min b^T lambda */
        lL[j] = 0.0; uL[j] = 1.0;
    }
    colptr[nL] = pos;
    for (int k = 0; k < n; k++) { bL[k] = 0.0; rel[k] = '='; }
    bL[n] = 1.0; rel[n] = '>';
    LP lp; memset(&lp, 0, sizeof lp);
    lp.n = nL; lp.m = rows;
    lp.Acolptr = colptr; lp.Arow = rowi; lp.Aval = val;
    lp.c = cL; lp.l = lL; lp.u = uL; lp.b = bL; lp.rel = rel;
    lp.maximize = 1;
    Solver *s = solver_create(&lp);
    int got = 0;
    if (s) {
        int st = solver_solve(s);
        if (st == 0) {
            double obj = 0.0;
            solver_optimum(s, xL, &obj);             /* obj = -b^T lambda */
            if (obj > 0.0) {                         /* a witness exists */
                double *cand = (double*)psolve_malloc(sizeof(double) * (size_t)m);
                if (cand) {
                    for (int i = 0; i < m; i++) cand[i] = xL[i];
                    if (farkas_verify(qp, cand, farkas)) got = 1;
                    psolve_free(cand);
                }
            }
        }
        solver_destroy(s);
    }
    psolve_free(colptr); psolve_free(rowi); psolve_free(val); psolve_free(cL);
    psolve_free(lL); psolve_free(uL); psolve_free(bL); psolve_free(xL); psolve_free(rel);
    return got;
}

/* lp_phase1_once() is a coin flip on the box width: the box exists to keep the
 * QP's free variables away from the 1e30 bounds that wreck the simplex's final
 * certificate, but a box that is too wide or too narrow leaves the rows badly
 * scaled inside the basis and the LP answers NUMERICAL_FAILURE.  On the
 * UI-layout family (models constructed feasible, or constructed infeasible) a
 * single scale rescued 5 of 16 hard cases; this ladder rescues all 16.  Nothing
 * here can produce a wrong verdict: every start is re-checked against the
 * caller's rows and every proof against the caller's Farkas system, and a box
 * that clips the search only ever yields "no verdict" -- status -1 as before. */
static int lp_phase1(const QP *qp, const double *x0, double *x, double *farkas)
{
    static const double boxmul[4] = { 1e9, 1e6, 1e3, 1e12 };   /* TOLSHEET TOL-QP-BOXLADDER */
    for (int k = 0; k < 4; k++) {
        if (psolve_stop()) return 3;      /* a budget must bound the ladder too */
        int r = lp_phase1_once(qp, x0, x, farkas, boxmul[k]);
        if (r) return r;
    }
    return 0;
}

/* Find a feasible point for A x <= b, or accept a provided one.
 *
 * First the caller's x0 (relative tolerance, so a warm start that drifted by
 * rounding still counts as feasible).  Then the LP core as Phase-I (see
 * lp_phase1) -- O(nnz)-per-pivot, and the only route that can PROVE the row
 * system empty.  Then the dense auxiliary QP as a fallback, kept because it
 * handles what the simplex can not (and it is what this solver did before the
 * LP route existed):
 *   minimize 1/2||x||^2 + 1/2||s||^2 + sum s
 *     s.t.  a_i x - s_i <= b_i,  s_i >= 0.
 * Strictly convex so the active-set converges; the linear term on s drives
 * s -> 0 whenever a feasible x exists.
 * Returns 1 on success (writes x), 0 if no feasible point was certified, and 2
 * if the search was cooperatively stopped (time limit / Ctrl-C). */
/* The dense Phase-I search: one strictly convex auxiliary QP
 *
 *     minimise  sum_i s_i + eps/2*(||x||^2 + ||s||^2)  s.t.  Ax - s <= b, s >= 0
 *
 * in n+m variables and 2m rows, run through the same active set (eps is a ridge,
 * not a term that moves the optimum).  Returns 1 with a start in x, 0 with none,
 * 2 if the search was cooperatively stopped.  Its virtue is the start it
 * produces -- strictly convex objective, so a well-centred interior point rather
 * than a vertex, which is what the main active set converges from; its cost is a
 * dense (n+m)-variable KKT factorisation per iteration, which is why
 * find_feasible gates it by size. */
static int dense_phase1(const QP *qp, const double *base, double *x)
{
    int n = qp->n, m = qp->m;
    int ok = 0;
    int N = n + m;
    double eps = 1e-6;                              /* TOLSHEET TOL-QP-P1RIDGE */
    double *Q1 = (double*)psolve_calloc((size_t)N*N, sizeof(double));
    double *c1 = (double*)psolve_calloc((size_t)N, sizeof(double));
    /* minimise  sum s  +  eps/2*(||x||^2 + ||s||^2).  The linear term on s
       dominates (eps tiny), so s -> 0 whenever a feasible x exists; the tiny
       quadratic keeps the problem strictly convex and bounded. */
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
    memcpy(x, base, (size_t)n * sizeof(double));   /* search from the given point */
    double *z0 = (double*)psolve_calloc((size_t)N, sizeof(double));
    for (int i = 0; i < m; i++) z0[n+i] = (qp->b[i] < 0) ? -qp->b[i] : 0.0;
    q1.x0 = z0;
    QPResult r1; memset(&r1, 0, sizeof(r1));
    active_set(&q1, z0, &r1);
    if (r1.status == QP_STOPPED) {
        qp_result_free(&r1);
        psolve_free(Q1); psolve_free(c1); psolve_free(A1); psolve_free(b1); psolve_free(z0);
        return 2;   /* stopped during Phase-I feasibility search */
    }
    if (r1.status == 0) {
        double ssum = 0.0;
        for (int i = 0; i < m; i++) ssum += r1.x[n+i];
        if (ssum <= 1e-7) {   /* total slack; TOLSHEET TOL-QP-P1SUM */
            /* Trust but verify: check the candidate against the ORIGINAL rows
               instead of inferring feasibility from the slack sum. */
            int feas2 = 1;
            for (int i = 0; i < m; i++) {
                double rr = -qp->b[i];
                for (int j = 0; j < n; j++) rr += qp->A[(size_t)i*n + j] * r1.x[j];
                if (rr > 1e-7 * (1.0 + fabs(qp->b[i]))) { feas2 = 0; break; }   /* TOLSHEET TOL-QP-P1VERIFY */
            }
            if (feas2) { for (int j = 0; j < n; j++) x[j] = r1.x[j]; ok = 1; }
        }
    }

    qp_result_free(&r1);
    psolve_free(Q1); psolve_free(c1); psolve_free(A1); psolve_free(b1); psolve_free(z0);
    return ok;
}

static int find_feasible(const QP *qp, const double *x0, double *x, QPResult *res)
{
    int n = qp->n, m = qp->m;
    const double *base = x0 ? x0 : x;
    memcpy(x, base, (size_t)n * sizeof(double));
    if (qp_start_feasible(qp, x)) return 1;

    int ok = 0;
    double *lam = (double*)psolve_malloc(sizeof(double) * (size_t)(m > 0 ? m : 1));
    double *xl  = n > 0 ? (double*)psolve_malloc(sizeof(double) * (size_t)n) : NULL;
    if (lam) for (int i = 0; i < m; i++) lam[i] = 0.0;

    /* The dense search goes first, but only while it is cheap enough to be a
     * bounded step.  TOLSHEET TOL-QP-P1DENSE
     *
     * Its virtue is the point it hands over: a strictly convex auxiliary
     * objective returns a well-centred interior point, which is what the active
     * set converges from -- handing it an LP vertex instead measurably stalls on
     * degenerate working sets (N=8 at scale 1e3 went OPTIMAL-in-16-iters ->
     * ITER_LIMIT-in-8100).  Its cost is one dense KKT factorisation in n+m
     * variables PER ITERATION, which at large n+m exceeds a frame budget on its
     * own and cannot be interrupted from inside a factorisation, so no
     * cooperative stop can make it responsive.  Measured against a 16 ms budget
     * on the layout family, dense-first overshoots to 48 ms at n+m = 161, 11.4 s
     * at 642, 180 s at 1281; through the LP route the same models stop inside
     * 20 ms.  Above the gate the LP route is therefore the only one attempted:
     * O(nnz) per pivot, so the budget binds within a pivot or two.
     *
     * LP-first is not merely "the same but cheaper": on the 200-/400-model
     * differential sweep against scipy it runs 3-12x faster and still reports
     * only verified verdicts, but it loses the rescue on models the simplex
     * cannot certify -- 158 vs 162 comparable models at n=200, 293 vs 306 at
     * n=400 (each lost model ends as "no feasible start", never as a wrong
     * answer).  Trading verified solves for speed is a policy the *caller* should
     * choose, not one this function should guess.  Inferring it from whether a
     * stop callback happens to be installed is a trap -- a host can install one
     * with no deadline at all (tools/qpsolve.c does exactly that, to be Ctrl-C
     * safe) -- and would then silently change a batch caller's answers.  An
     * explicit knob is the way; see docs/CURV_PS_PLAN.md P0.3. */
    if (n + m <= 600) {   /* TOLSHEET TOL-QP-P1DENSE */
        ok = dense_phase1(qp, base, x);
        if (ok == 2) { psolve_free(xl); psolve_free(lam); return 2; }
    }

    if (!ok) {
        /* Sparse LP on min sum(s): the cheap route, and the only one that can
         * PROVE the row system empty.  A start it hands back is re-verified
         * against the caller's own rows inside lp_phase1. */
        int pr = (lam && xl) ? lp_phase1(qp, x0, xl, lam) : 0;
        if (pr == 1) {
            memcpy(x, xl, (size_t)n * sizeof(double));
            ok = 1;
        } else if (pr == 2) {                    /* verified Farkas certificate */
            res->infeasible_proven = 1;
            res->farkas = lam;  lam = NULL;      /* ownership moves to QPResult */
            psolve_free(xl);  psolve_free(lam);
            return 0;                            /* proven empty: nothing to search for */
        } else if (pr == 3) {                    /* stopped: no verdict, not a give-up */
            psolve_free(xl);  psolve_free(lam);
            return 2;
        }
    }

    /* Still no start: ask the Farkas alternative directly.  It needs neither a
     * box the caller never asked for nor a basis the simplex can certify, so it
     * succeeds on models where the boxed LP gave up.  Failure here is still only
     * "no feasible start" -- never infeasible. */
    if (!ok && lam && !psolve_stop() && lp_farkas(qp, lam)) {
        res->infeasible_proven = 1;
        res->farkas = lam;  lam = NULL;
    }
    psolve_free(xl);  psolve_free(lam);
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
    int fr = find_feasible(qp, qp->x0, x, res);
    if (fr == 0) {                             /* status stays -1: no feasible start */
        psolve_free(x);
        return;                                /* res->infeasible_proven says whether
                                                  that is a proof or a give-up */
    }
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
    if (res->x) {                              /* report how feasible the answer is,
                                                  in the caller's own units */
        double mr = 0.0;
        for (int i = 0; i < qp->m; i++) mr = fmax(mr, row_resid(qp, i, res->x));
        res->max_resid = mr > 0.0 ? mr : 0.0;
    }
}

void qp_result_free(QPResult *res)
{
    if (!res) return;
    psolve_free(res->x); psolve_free(res->mult); psolve_free(res->farkas);
    memset(res, 0, sizeof(*res));
}
