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

/* ------------------------------------------------------- P1.1 native equalities/bounds
 * A `QP` can carry three kinds of constraint beside A x <= b:
 *   - me equalities Aeq x = beq (permanent working-set members),
 *   - per-variable lower bounds l <= x,
 *   - per-variable upper bounds x <= u.
 * Bounds are represented as the corresponding inequality rows only when they
 * enter the KKT working set; they cost no rows in the caller's `m` and no rows
 * in the marshalled A/b.  LP_INF (src/solver.h) is used as the infinite sentinel,
 * so the semantics are the same as the LP core's own bound handling.
 */

/* A working-set member: an original inequality, an equality, a lower-bound row
 * (-x_j <= -l_j) or an upper-bound row (x_j <= u_j). */
typedef enum { QP_C_INEQ = 0, QP_C_EQ = 1, QP_C_LOWER = 2, QP_C_UPPER = 3 } QP_ConsKind;
typedef struct { QP_ConsKind kind; int idx; } QP_Cons;

static double qp_lb(const QP *qp, int j)
{
    double v = qp->l ? qp->l[j] : -LP_INF;
    return (!isfinite(v) || v < -LP_INF/2) ? -LP_INF : v;
}
static double qp_ub(const QP *qp, int j)
{
    double v = qp->u ? qp->u[j] : LP_INF;
    return (!isfinite(v) || v > LP_INF/2) ? LP_INF : v;
}
static int qp_bound_is_finite(double v) { return v > -LP_INF/2 && v < LP_INF/2; }

static double eq_resid(const QP *qp, int i, const double *x)
{
    const double *Ai = qp->Aeq + (size_t)i * qp->n;
    double s = -qp->beq[i];
    for (int j = 0; j < qp->n; j++) s += Ai[j] * x[j];
    return s;
}
static double eq_scale(const QP *qp, int i, const double *x)
{
    double s = fabs(qp->beq[i]);
    const double *Ai = qp->Aeq + (size_t)i * qp->n;
    for (int j = 0; j < qp->n; j++) s += fabs(Ai[j]) * fabs(x[j]);
    return 1.0 + s;
}

/* Residual of a working-set member in the `a^T x - b` convention used by the
 * active set: <= 0 is feasible (equal rows are required to be ~0). */
static double cons_resid(const QP *qp, const QP_Cons *c, const double *x)
{
    switch (c->kind) {
    case QP_C_INEQ:  return row_resid(qp, c->idx, x);
    case QP_C_EQ:    return eq_resid(qp, c->idx, x);
    case QP_C_LOWER: return -x[c->idx] + qp_lb(qp, c->idx);
    case QP_C_UPPER: return x[c->idx] - qp_ub(qp, c->idx);
    }
    return 0.0;
}
static double cons_scale(const QP *qp, const QP_Cons *c, const double *x)
{
    switch (c->kind) {
    case QP_C_INEQ:  return row_scale(qp, c->idx, x);
    case QP_C_EQ:    return eq_scale(qp, c->idx, x);
    case QP_C_LOWER: return 1.0 + fabs(qp_lb(qp, c->idx)) + fabs(x[c->idx]);
    case QP_C_UPPER: return 1.0 + fabs(qp_ub(qp, c->idx)) + fabs(x[c->idx]);
    }
    return 1.0;
}
/* Write the working-set row into out[n]. */
static void cons_row(const QP *qp, const QP_Cons *c, double *out)
{
    int j;
    switch (c->kind) {
    case QP_C_INEQ: {
        const double *Ai = qp->A + (size_t)c->idx * qp->n;
        for (j = 0; j < qp->n; j++) out[j] = Ai[j];
        break;
    }
    case QP_C_EQ: {
        const double *Ai = qp->Aeq + (size_t)c->idx * qp->n;
        for (j = 0; j < qp->n; j++) out[j] = Ai[j];
        break;
    }
    case QP_C_LOWER:
        for (j = 0; j < qp->n; j++) out[j] = 0.0;
        out[c->idx] = -1.0;
        break;
    case QP_C_UPPER:
        for (j = 0; j < qp->n; j++) out[j] = 0.0;
        out[c->idx] = 1.0;
        break;
    }
}

/* 1 when a constraint row is violated at x beyond the warm-start tolerance. */
static int cons_violated(const QP *qp, const QP_Cons *c, const double *x)
{
    double r = cons_resid(qp, c, x);
    if (c->kind == QP_C_EQ) return fabs(r) > 1e-11 * cons_scale(qp, c, x);
    return r > 1e-11 * cons_scale(qp, c, x);   /* TOLSHEET TOL-QP-WARMFEAS */
}

int qp_start_feasible(const QP *qp, const double *x)
{
    if (!qp || !x) return 0;
    int i, j;
    for (i = 0; i < qp->m; i++) {
        QP_Cons c; c.kind = QP_C_INEQ; c.idx = i;
        if (cons_violated(qp, &c, x)) return 0;
    }
    for (i = 0; i < qp->me; i++) {
        QP_Cons c; c.kind = QP_C_EQ; c.idx = i;
        if (cons_violated(qp, &c, x)) return 0;
    }
    for (j = 0; j < qp->n; j++) {
        if (qp->l && qp_bound_is_finite(qp_lb(qp, j)) && x[j] < qp_lb(qp, j) - 1e-11 * (1.0 + fabs(qp_lb(qp, j)) + fabs(x[j]))) return 0;
        if (qp->u && qp_bound_is_finite(qp_ub(qp, j)) && x[j] > qp_ub(qp, j) + 1e-11 * (1.0 + fabs(qp_ub(qp, j)) + fabs(x[j]))) return 0;
    }
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
    int i, j;
    for (i = 0; i < qp->m; i++)
        if (row_resid(qp, i, x) > 1e-8 * row_scale(qp, i, x)) return 0;   /* TOLSHEET TOL-QP-FEASROW */
    for (i = 0; i < qp->me; i++)
        if (fabs(eq_resid(qp, i, x)) > 1e-8 * eq_scale(qp, i, x)) return 0;
    for (j = 0; j < qp->n; j++) {
        if (qp->l && qp_bound_is_finite(qp_lb(qp, j)) && x[j] < qp_lb(qp, j) - 1e-8 * (1.0 + fabs(qp_lb(qp, j)) + fabs(x[j]))) return 0;
        if (qp->u && qp_bound_is_finite(qp_ub(qp, j)) && x[j] > qp_ub(qp, j) + 1e-8 * (1.0 + fabs(qp_ub(qp, j)) + fabs(x[j]))) return 0;
    }
    return 1;
}

/* Build & factor KKT, solve [Q A^T; A 0][p;mu]=[-g;0]. Returns 0 ok, -1 singular. */
static int solve_kkt(const QP *qp, const QP_Cons *C, int k,
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
    double *row = (double*)xmalloc((size_t)n * sizeof(double));
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++) K[j*N + i] = qp->Q[j*n + i];
    for (int c = 0; c < k; c++) {
        cons_row(qp, &C[c], row);
        for (int i = 0; i < n; i++) {
            K[(n+c)*N + i] = row[i];
            K[i*N + (n+c)] = row[i];
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
            if (resid <= 1e-8 * (1.0 + rhsnorm + xnorm)) good = 1;  /* TOLSHEET TOL-QP-INITRES */
        }
        if (!good && attempt < 3) {
            /* regularize the Q block with increasing strength.  A singular PSD Q
               makes the KKT matrix singular; larger regularization lets the
               active-set take a null-space-aware step rather than failing. */
            memcpy(K, K0, (size_t)N * N * sizeof(double));
            double reg = (attempt == 0) ? 1e-8 : ((attempt == 1) ? 1e-6 : 1e-4);  /* TOLSHEET TOL-QP-REG */
            for (int i = 0; i < n; i++) K[i*N + i] += reg;
            memcpy(K0, K, (size_t)N * N * sizeof(double));
            memcpy(rhs, rhs0, (size_t)N * sizeof(double));
            r = lu_factor(K, N, piv);
        }
    }
    if (!good) { psolve_free(blk); psolve_free(piv); psolve_free(row); return -1; }
    for (int i = 0; i < n; i++) p[i] = rhs[i];
    for (int c = 0; c < k; c++) mu[c] = rhs[n+c];

    psolve_free(blk); psolve_free(piv); psolve_free(row);
    return 0;
}

/* Build an orthonormal basis of the working-set rows (for rank management).
 * orth is n*k, column j = orthonormal basis vector for C[j]. */
static void build_orth(const QP *qp, const QP_Cons *C, int k, double *orth)
{
    int n = qp->n;
    double tol = 1e-9;  /* TOLSHEET TOL-QP-NRMDIV */
    for (int c = 0; c < k; c++) {
        double *v = orth + (size_t)c * n;
        cons_row(qp, &C[c], v);
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

/* Add c to the working set if it is rank-independent of the members already in
 * C[0..k).  Returns 1 on success (C[k] written, orth rebuilt), 0 on a dependent
 * row (which the active set is allowed to omit: it is implied by the others). */
static int add_cons(QP_Cons *C, int *k, double *orth, double *tmp,
                    const QP *qp, QP_Cons c, double tolrank)
{
    cons_row(qp, &c, tmp);
    for (int d = 0; d < *k; d++) {
        const double *u = orth + (size_t)d * qp->n;
        double dp = 0; for (int i = 0; i < qp->n; i++) dp += u[i]*tmp[i];
        for (int i = 0; i < qp->n; i++) tmp[i] -= dp * u[i];
    }
    double nrm = 0; for (int i = 0; i < qp->n; i++) nrm += tmp[i]*tmp[i];
    nrm = sqrt(nrm);
    if (nrm <= tolrank) return 0;
    C[*k] = c; (*k)++;
    build_orth(qp, C, *k, orth);
    return 1;
}

/* Active-set core.  Requires xstart feasible.  Writes solution to res->x.
 * The working set C is kept row-independent (rank-managed) so the KKT system
 * is never singular; linearly dependent active constraints are skipped.
 * P1.1: equality rows are inserted first and are never dropped; variable bounds
 * enter the working set only when they are active, so they cost no rows in m. */
static int cons_in_set(const QP_Cons *C, int k, QP_Cons c)
{
    for (int i = 0; i < k; i++)
        if (C[i].kind == c.kind && C[i].idx == c.idx) return 1;
    return 0;
}

static void active_set(const QP *qp, const double *xstart, QPResult *res)
{
    int n = qp->n, m = qp->m, me = qp->me;
    int maxc = m + me + 2 * n;
    res->n = n;
    res->x = (double*)xmalloc((size_t)n * sizeof(double));
    res->mult = (double*)xmalloc((size_t)m * sizeof(double));
    res->ray = (double*)xmalloc((size_t)n * sizeof(double));
    if (me > 0) res->mult_eq = (double*)xmalloc((size_t)me * sizeof(double));
    if (qp->l)  res->mult_l  = (double*)xmalloc((size_t)n * sizeof(double));
    if (qp->u)  res->mult_u  = (double*)xmalloc((size_t)n * sizeof(double));
    for (int i = 0; i < n; i++) res->ray[i] = 0.0;
    for (int i = 0; i < m; i++) res->mult[i] = 0.0;
    if (me > 0) for (int i = 0; i < me; i++) res->mult_eq[i] = 0.0;
    if (qp->l)  for (int i = 0; i < n; i++) res->mult_l[i] = 0.0;
    if (qp->u)  for (int i = 0; i < n; i++) res->mult_u[i] = 0.0;
    double *x = res->x;
    memcpy(x, xstart, (size_t)n * sizeof(double));

    QP_Cons *C = (QP_Cons*)xmalloc((size_t)maxc * sizeof(QP_Cons));
    double *orth = (double*)xmalloc((size_t)n * n * sizeof(double));
    double *tmp = (double*)xmalloc((size_t)n * sizeof(double));
    double tolrank = 1e-9;  /* TOLSHEET TOL-QP-RANK */
    int k = 0;
    build_orth(qp, C, 0, orth);

    /* Equality rows are permanent: add every rank-independent one up-front. */
    for (int i = 0; i < me; i++) {
        QP_Cons c; c.kind = QP_C_EQ; c.idx = i;
        add_cons(C, &k, orth, tmp, qp, c, tolrank);
    }
    int neq0 = k;                       /* C[0..neq0) are never dropped */
    for (int i = 0; i < m; i++)
        if (row_resid(qp, i, x) > -1e-7) {  /* TOLSHEET TOL-QP-ACTIVE */
            QP_Cons c; c.kind = QP_C_INEQ; c.idx = i;
            add_cons(C, &k, orth, tmp, qp, c, tolrank);
        }
    for (int j = 0; j < n; j++) {
        QP_Cons c;
        if (qp->l && qp_bound_is_finite(qp_lb(qp, j))) {
            c.kind = QP_C_LOWER; c.idx = j;
            if (cons_resid(qp, &c, x) > -1e-7) add_cons(C, &k, orth, tmp, qp, c, tolrank);
        }
        if (qp->u && qp_bound_is_finite(qp_ub(qp, j))) {
            c.kind = QP_C_UPPER; c.idx = j;
            if (cons_resid(qp, &c, x) > -1e-7) add_cons(C, &k, orth, tmp, qp, c, tolrank);
        }
    }

    double *g = (double*)xmalloc((size_t)n * sizeof(double));
    double *p = (double*)xmalloc((size_t)n * sizeof(double));
    double *mu = (double*)xmalloc((size_t)maxc * sizeof(double));
    double *xnew = (double*)xmalloc((size_t)n * sizeof(double));
    int maxit = 4000 + 100 * (n + m + me);
    int it = 0;
    int status = -1;   /* default: not solved */

    for (it = 0; it < maxit; it++) {
        /* Cooperative abort (time limit / Ctrl-C): polled every iteration so a
           per-frame QP can be cut off at the requested budget.  On stop we hand
           back the current -- still feasible -- iterate as a best incumbent but
           do NOT claim optimality. */
        if (psolve_stop()) { status = QP_STOPPED; break; }
        eval_grad(qp, x, g);
        if (solve_kkt(qp, C, k, g, p, mu) != 0) {
            /* Drop a non-equality member rather than the permanent equalities. */
            if (k > neq0) { k--; build_orth(qp, C, k, orth); continue; }
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
            if (!(xmax < 1e14)) { status = QP_ITERATION_LIMIT; break; }  /* TOLSHEET TOL-QP-DIVERGE */
        }
        if (pnorm < 1e-9 * scale) {  /* TOLSHEET TOL-QP-MU */
            int drop = -1; double minmu = 0.0;
            for (int c = neq0; c < k; c++)
                if (mu[c] < minmu - 1e-9 * scale) { minmu = mu[c]; drop = c; }  /* TOLSHEET TOL-QP-MU */
            if (drop < 0) {
                /* candidate optimum: verify the KKT stationarity residual
                   before certifying success, so a bad KKT solve cannot be
                   reported as optimal. */
                double *crow = (double*)xmalloc((size_t)n * sizeof(double));
                double kkt = 0.0, gmax = 0.0, tmax = 0.0;
                for (int j = 0; j < n; j++) {
                    double rj = g[j];
                    gmax = fmax(gmax, fabs(g[j]));
                    for (int c = 0; c < k; c++) {
                        cons_row(qp, &C[c], crow);
                        double t = crow[j] * mu[c];
                        rj += t;
                        tmax = fmax(tmax, fabs(t));
                    }
                    kkt = fmax(kkt, fabs(rj));
                }
                psolve_free(crow);
                /* Scale the stationarity tolerance by the size of the terms
                   being cancelled, NOT by the objective value.  The old
                   1e-6*(1+|obj|) grew with a diverging iterate: on an
                   unbounded QP the objective ran to 1e36, the tolerance with
                   it, and a meaningless point passed as optimal. */
                double tol = 1e-7 * (1.0 + gmax + tmax);  /* TOLSHEET TOL-QP-KKT */
                if (kkt > tol) { status = QP_KKT_FAIL; break; }
                /* Complementary slackness: a multiplier may only be attached
                   to a constraint that is actually active at x. */
                int comp_ok = 1;
                for (int c = 0; c < k; c++) {
                    double rr = cons_resid(qp, &C[c], x);
                    if (fabs(rr) > 1e-7 * cons_scale(qp, &C[c], x)) { comp_ok = 0; break; }  /* TOLSHEET TOL-QP-COMP */
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
                    if (rr > 1e-7 * (1.0 + fabs(qp->b[i]))) { infeas = 1; break; }  /* TOLSHEET TOL-QP-PRIMAL */
                }
                for (int i = 0; i < me && !infeas; i++)
                    if (fabs(eq_resid(qp, i, x)) > 1e-7 * eq_scale(qp, i, x)) infeas = 1;
                for (int j = 0; j < n && !infeas; j++) {
                    if (qp->l && qp_bound_is_finite(qp_lb(qp, j)) &&
                        x[j] < qp_lb(qp, j) - 1e-7 * (1.0 + fabs(qp_lb(qp, j)) + fabs(x[j]))) infeas = 1;
                    if (qp->u && qp_bound_is_finite(qp_ub(qp, j)) &&
                        x[j] > qp_ub(qp, j) + 1e-7 * (1.0 + fabs(qp_ub(qp, j)) + fabs(x[j]))) infeas = 1;
                }
                if (infeas) { status = QP_KKT_FAIL; break; }
                for (int c = 0; c < k; c++) {
                    if (C[c].kind == QP_C_INEQ) res->mult[C[c].idx] = mu[c];
                    else if (C[c].kind == QP_C_EQ) res->mult_eq[C[c].idx] = mu[c];
                    else if (C[c].kind == QP_C_LOWER) res->mult_l[C[c].idx] = mu[c];
                    else if (C[c].kind == QP_C_UPPER) res->mult_u[C[c].idx] = mu[c];
                }
                status = 0; break;
            }
            C[drop] = C[k-1]; k--;
            build_orth(qp, C, k, orth);
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
            if (gp < -1e-9 * scale && pinf > 0.0) {  /* TOLSHEET TOL-QP-UNBDIR */
                for (int i = 0; i < n; i++) {
                    double qi = 0.0;
                    for (int j = 0; j < n; j++) qi += qp->Q[(size_t)j*n + i] * p[j];
                    pQp += qi * p[i];
                }
                double qnorm = 0.0;
                for (int i = 0; i < n*n; i++) qnorm = fmax(qnorm, fabs(qp->Q[i]));
                if (pQp <= 1e-12 * (1.0 + qnorm) * pinf * pinf) {  /* TOLSHEET TOL-QP-CURV */
                    int ray = 1;
                    for (int i = 0; i < m && ray; i++) {
                        const double *Ai = qp->A + (size_t)i * n;
                        double ap = 0.0;
                        for (int j = 0; j < n; j++) ap += Ai[j] * p[j];
                        if (ap > 0.0) ray = 0;
                    }
                    for (int i = 0; i < me && ray; i++) {
                        double ap = 0.0;
                        const double *Ai = qp->Aeq + (size_t)i * n;
                        for (int j = 0; j < n; j++) ap += Ai[j] * p[j];
                        if (fabs(ap) > 1e-9 * (1.0 + pinf)) ray = 0;
                    }
                    for (int j = 0; j < n && ray; j++) {
                        if (qp->l && qp_bound_is_finite(qp_lb(qp, j)) && p[j] < 0.0) ray = 0;
                        if (qp->u && qp_bound_is_finite(qp_ub(qp, j)) && p[j] > 0.0) ray = 0;
                    }
                    if (ray) {
                        status = 1;
                        /* keep the direction as evidence: the CLI certificate
                           layer re-verifies it against the original data */
                        memcpy(res->ray, p, (size_t)n * sizeof(double));
                        break;
                    }   /* certified unbounded */
                }
            }
        }

        double alpha = 1.0; QP_Cons block; block.kind = QP_C_INEQ; block.idx = -1;
        int has_block = 0;
        for (int i = 0; i < m; i++) {
            QP_Cons c; c.kind = QP_C_INEQ; c.idx = i;
            if (cons_in_set(C, k, c)) continue;
            const double *Ai = qp->A + (size_t)i * n;
            double ap = 0.0;
            for (int j = 0; j < n; j++) ap += Ai[j] * p[j];
            if (ap > 1e-12) {  /* TOLSHEET TOL-QP-STEP */
                double r = -row_resid(qp, i, x) / ap;
                /* A constraint that is already (numerically) violated gives a
                   negative ratio.  Taking that step would move the iterate
                   BACKWARDS along p -- uphill, and deeper out of the feasible
                   region.  Clamp to a zero-length blocking step, which is the
                   standard degenerate-step handling: the constraint enters the
                   working set and the next KKT solve moves along it. */
                if (r < 0.0) r = 0.0;
                if (r < alpha - 1e-10) { alpha = r; block = c; has_block = 1; }  /* TOLSHEET TOL-QP-STEP */
            }
        }
        for (int j = 0; j < n; j++) {
            if (qp->l && qp_bound_is_finite(qp_lb(qp, j)) && p[j] < -1e-12) {
                QP_Cons c; c.kind = QP_C_LOWER; c.idx = j;
                if (cons_in_set(C, k, c)) continue;
                double r = (qp_lb(qp, j) - x[j]) / p[j];
                if (r < 0.0) r = 0.0;
                if (r < alpha - 1e-10) { alpha = r; block = c; has_block = 1; }
            }
            if (qp->u && qp_bound_is_finite(qp_ub(qp, j)) && p[j] > 1e-12) {
                QP_Cons c; c.kind = QP_C_UPPER; c.idx = j;
                if (cons_in_set(C, k, c)) continue;
                double r = (qp_ub(qp, j) - x[j]) / p[j];
                if (r < 0.0) r = 0.0;
                if (r < alpha - 1e-10) { alpha = r; block = c; has_block = 1; }
            }
        }
        for (int i = 0; i < n; i++) xnew[i] = x[i] + alpha * p[i];
        memcpy(x, xnew, (size_t)n * sizeof(double));
        if (alpha < 1.0 - 1e-10 && has_block)
            add_cons(C, &k, orth, tmp, qp, block, tolrank);
    }
    if (status == -1) status = QP_ITERATION_LIMIT;   /* loop exhausted without KKT cert */
    res->iterations = it;
    res->obj = eval_obj(qp, x);
    res->status = status;
    psolve_free(C); psolve_free(orth); psolve_free(tmp); psolve_free(g); psolve_free(p); psolve_free(mu); psolve_free(xnew);
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
    double eps = 1e-6;                              /* TOLSHEET TOL-QP-PERTURB */
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
    memcpy(x, base, (size_t)n * sizeof(double));   /* search from the given point */
    double *z0 = (double*)psolve_calloc((size_t)N, sizeof(double));
    for (int i = 0; i < m; i++) z0[n+i] = (qp->b[i] < 0) ? -qp->b[i] : 0.0;
    QP q1; memset(&q1, 0, sizeof(q1));
    q1.n = N; q1.m = m1; q1.Q = Q1; q1.c = c1; q1.A = A1; q1.b = b1;
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
        if (ssum <= 1e-7) {   /* total slack; TOLSHEET TOL-QP-PH1SUM */
            /* Trust but verify: check the candidate against the ORIGINAL rows
               instead of inferring feasibility from the slack sum. */
            int feas2 = 1;
            for (int i = 0; i < m; i++) {
                double rr = -qp->b[i];
                for (int j = 0; j < n; j++) rr += qp->A[(size_t)i*n + j] * r1.x[j];
                if (rr > 1e-7 * (1.0 + fabs(qp->b[i]))) { feas2 = 0; break; }   /* TOLSHEET TOL-QP-PRIMAL */
            }
            if (feas2) { for (int j = 0; j < n; j++) x[j] = r1.x[j]; ok = 1; }
        }
    }

    qp_result_free(&r1);
    psolve_free(Q1); psolve_free(c1); psolve_free(A1); psolve_free(b1); psolve_free(z0);
    return ok;
}

/* The sparse LP route, run and interpreted.  Returns 1 with a start written to
 * x, 2 with the caller's certificate moved into res (lam is then NULL), 3 if
 * cooperatively stopped, 0 if it had nothing to say. */
static int lp_route(const QP *qp, const double *x0, double *x, double *xl,
                    double **lamp, QPResult *res)
{
    int n = qp->n;
    int pr = (xl && *lamp) ? lp_phase1(qp, x0, xl, *lamp) : 0;
    if (pr == 1) { memcpy(x, xl, (size_t)n * sizeof(double)); return 1; }
    if (pr == 2) {
        res->infeasible_proven = 1;
        res->farkas = *lamp;  *lamp = NULL;      /* ownership moves to QPResult */
        return 0;                                /* proven empty: nothing to search for */
    }
    if (pr == 3) return 3;                       /* stopped: no verdict, not a give-up */
    return 0;
}

static int find_feasible(const QP *qp, const double *x0, double *x, QPResult *res)
{
    int n = qp->n, m = qp->m;
    const double *base = x0 ? x0 : x;
    memcpy(x, base, (size_t)n * sizeof(double));
    if (qp_start_feasible(qp, x)) return 1;

    int ok = 0;
    int lp_first = qp->phase1_order == QP_PHASE1_LP_FIRST;
    double *lam = (double*)psolve_malloc(sizeof(double) * (size_t)(m > 0 ? m : 1));
    double *xl  = n > 0 ? (double*)psolve_malloc(sizeof(double) * (size_t)n) : NULL;
    if (lam) for (int i = 0; i < m; i++) lam[i] = 0.0;

    /* Which route runs first is the caller's decision, and `phase1_lp_first` is
     * how they make it.  The two routes are wrong in opposite directions:
     *
     * The dense auxiliary QP hands over a well-centred interior point, which is
     * what the active set converges from -- handing it an LP vertex instead
     * measurably stalls on degenerate working sets (N=8 at scale 1e3 went
     * OPTIMAL-in-16-iters -> ITER_LIMIT-in-8100).  It costs one dense KKT
     * factorisation in n+m variables PER ITERATION, which at large n+m exceeds a
     * frame budget on its own and cannot be interrupted from inside, so no
     * cooperative stop can make it responsive: measured against a 16 ms budget on
     * the layout family, dense-first overshoots to 48 ms at n+m = 161, 11.4 s at
     * 641, 180 s at 1281, while the LP route stops inside 20 ms.
     *
     * The LP route is O(nnz) per pivot and is the only one that can PROVE the
     * row system empty.  Run first it is 3-12x cheaper on this family (N=8
     * 1.8 -> 0.5 ms, N=64 4045 -> 332 ms) and it turns N=64 from STOPPED into
     * OPTIMAL inside a 1500 ms solve -- but on models the simplex declines to
     * certify it has nothing else to fall back on, and the dense search does: on
     * the scipy differential sweep, LP-first checks 158 of 200 models at n=200
     * and 293 at n=400, against 164 and 312 dense-first, with every difference
     * ending as "no feasible start" and never as a wrong answer.
     *
     * So neither order may be chosen *for* a caller, and neither may be inferred
     * from whether a stop callback happens to be installed (tools/qpsolve.c
     * installs one with no deadline at all, to be Ctrl-C-safe).  Default is
     * dense-first: verified solves over speed, which is what a batch caller wants
     * and what a caller who has not asked for anything should keep getting.
     * TOLSHEET TOL-QP-P1DENSE below is the one exception the QP makes on its own,
     * because past that size dense-first is not a slower route to the same answer
     * but an answer that cannot be interrupted. */
    if (lp_first) {
        ok = lp_route(qp, x0, x, xl, &lam, res);
        if (ok == 2) { psolve_free(xl); psolve_free(lam); return 2; }
        if (ok == 0 && res->infeasible_proven) { psolve_free(xl); psolve_free(lam); return 0; }
    }

    if (!ok && n + m <= 600) {                          /* TOLSHEET TOL-QP-P1DENSE */
        ok = dense_phase1(qp, base, x);
        if (ok == 2) { psolve_free(xl); psolve_free(lam); return 2; }
    }

    if (!ok && !lp_first) {
        ok = lp_route(qp, x0, x, xl, &lam, res);
        if (ok == 2) { psolve_free(xl); psolve_free(lam); return 2; }
        if (ok == 0 && res->infeasible_proven) { psolve_free(xl); psolve_free(lam); return 0; }
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
        qp->me < 0 || qp->me > 1000000 ||
        !qp->Q || !qp->c || (qp->m > 0 && (!qp->A || !qp->b)) ||
        (qp->me > 0 && (!qp->Aeq || !qp->beq))) return;
    int n = qp->n;
    if ((size_t)n > (size_t)-1 / (size_t)n ||
        (qp->m > 0 && (size_t)n > (size_t)-1 / (size_t)qp->m) ||
        (qp->me > 0 && (size_t)n > (size_t)-1 / (size_t)qp->me)) return;
    for (int j = 0; j < n; j++)
        if (!isfinite(qp->c[j]) || (qp->x0 && !isfinite(qp->x0[j]))) return;
    for (size_t k = 0; k < (size_t)n * n; k++) if (!isfinite(qp->Q[k])) return;
    for (int i = 0; i < qp->m; i++) {
        if (!isfinite(qp->b[i])) return;
        for (int j = 0; j < n; j++) if (!isfinite(qp->A[(size_t)i*n+j])) return;
    }
    for (int i = 0; i < qp->me; i++) {
        if (!isfinite(qp->beq[i])) return;
        for (int j = 0; j < n; j++) if (!isfinite(qp->Aeq[(size_t)i*n+j])) return;
    }
    if (qp->l)
        for (int j = 0; j < n; j++)
            if (!isfinite(qp->l[j]) && qp->l[j] != -INFINITY && qp->l[j] != INFINITY) return;
    if (qp->u)
        for (int j = 0; j < n; j++)
            if (!isfinite(qp->u[j]) && qp->u[j] != -INFINITY && qp->u[j] != INFINITY) return;
    res->status = -1;
    /* Convexity gate.  Symmetry first (the factorization assumes it), then a
       FULL symmetric ~Cholesky scan: the old 1x1/2x2 principal-minor screen
       passed indefinite matrices n>=3 whose negativity only shows in a
       larger minor (e.g. diag 1, off-diagonal -0.9: every 2x2 minor is
       0.19 > 0 yet an eigenvalue is -0.8).  Such a Q made the active-set
       report the stationary origin as an "optimum" on a problem unbounded
       below -- a fabricated answer in the verifier-free direction
       (AUDIT "Not done" 6.5; gadget pinned in tools/qp_psd_verify.py).

       The scan is exact in exact arithmetic: complete it with all pivots
       >= -tol  <=>  Q is PSD (within tolerance).  Short witnesses:
         - a negative pivot beyond tol means a negative leading principal
           submatrix step: the matrix is indefinite;
         - a zero-ish pivot with a NONZERO residual column means a 2x2
           block [0 a; a b] with a*a < 0 in the Schur complement:
           indefinite;
         - otherwise the eliminated remainder stays PSD, so finishing
           certifies PSD.
       tol is scaled (entries of Q and its Schur complements share units):
       semidefinitedness of doubles can only ever be decided to a relative
       frontier; past it is the documented tolerance semantics (roadmap 6.8
       will publish the sheets).  Cost is O(n^3/3) once, the same order as
       one active-set KKT factorization. */
    {
        double qscale = 1.0;
        for (size_t k = 0; k < (size_t)n * n; k++)
            qscale = fmax(qscale, fabs(qp->Q[k]));
        double symtol = 1e-8 * (1.0 + qscale);  /* TOLSHEET TOL-QP-SYM */
        for (int i = 0; i < n; i++)
            for (int j = i + 1; j < n; j++) {
                double diff = fabs(qp->Q[i*n+j] - qp->Q[j*n+i]);
                if (diff > symtol) {
                    res->status = QP_NON_CONVEX;
                    return;
                }
            }
        /* Symmetrize into a workspace (get the same answer for +/-1-ulp
           asymmetric input), then a complete-pivoting symmetric
           elimination scan: at each step move the largest remaining
           diagonal to the pivot position and eliminate it.  Soundness of
           the verdicts (exact arithmetic):
             - a pivot < -tol is a negative diagonal of a matrix CONGRUENT
               to the input (elimination and symmetric permutation are
               congruences), so by Sylvester's law the input has a
               negative eigenvalue: indefinite;
             - if the largest remaining diagonal is within tol of zero,
               the leftover matrix is PSD iff every off-diagonal is also
               within tol (a 2x2 [d a; a d'] with |d|,|d'| <= tol and
               |a| above it has determinant ~ -a^2 < 0, a principal
               indefinite 2x2);
             - otherwise the scan finishes with pivots >= -tol, and the
               accumulated factorization IS a PSD certificate.
           Complete pivoting matters: without it, a tiny leading diagonal
           forces the "zero pivot with nonzero column" case to fire on
           scale-mixed but genuinely PSD blocks like
           [6e-12 3e-5; 3e-5 1e3] (det > 0), over-blocking valid models;
           pivoting the 1e3 first eliminates the coupling at its own
           scale and the small direction is judged against its own
           magnitude. */
        double *S = (double*)psolve_malloc((size_t)n * n * sizeof(double));
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++)
                S[i*n+j] = 0.5 * (qp->Q[i*n+j] + qp->Q[j*n+i]);
        }
        double ptol = 1e-9 * (1.0 + qscale);  /* TOLSHEET TOL-QP-PSD */
        int psd = 1;
        int k = 0;
        for (k = 0; k < n; k++) {
            /* largest remaining diagonal */
            int p = k;
            double dmax = S[k*n+k];
            for (int i = k + 1; i < n; i++)
                if (S[i*n+i] > dmax) { dmax = S[i*n+i]; p = i; }
            if (dmax < -ptol) { psd = 0; break; }   /* neg. diagonal: indefinite */
            if (dmax <= ptol) {
                /* tail: all remaining diagonals within tol of 0 -> the
                   leftover is PSD iff it is entirely ~0 (Cauchy-Schwarz);
                   any larger off-diagonal is an indefinite principal 2x2. */
                for (int i = k; i < n && psd; i++)
                    for (int j = i + 1; j < n; j++)
                        if (fabs(S[i*n+j]) > ptol) { psd = 0; break; }
                break;
            }
            /* symmetric permutation k <-> p (full square) */
            if (p != k) {
                for (int j = 0; j < n; j++) {
                    double t = S[k*n+j]; S[k*n+j] = S[p*n+j]; S[p*n+j] = t;
                }
                for (int i = 0; i < n; i++) {
                    double t = S[i*n+k]; S[i*n+k] = S[i*n+p]; S[i*n+p] = t;
                }
            }
            double d = S[k*n+k];
            for (int i = k + 1; i < n; i++) {
                double si = S[i*n+k];
                if (si == 0.0) continue;
                for (int j = k + 1; j < n; j++)
                    S[i*n+j] -= si * S[j*n+k] / d;
            }
        }
        psolve_free(S);
        if (!psd) {
            res->status = QP_NON_CONVEX;
            return;
        }
    }
    /* Phase-I search and the main active set.  With native equalities or
     * variable bounds (P1.1) the feasibility search runs on an expanded row
     * system (equalities as two inequalities, bounds as one row each) that
     * describes the SAME feasible set; the main active set then enforces the
     * equalities permanently and bounds at zero row cost.  The expanded Farkas
     * certificate is discarded (it is over the expansion, whose rows are a
     * different object than the caller's rows), so an equalities/bounds model
     * never reports a proof that the certificate layer could not re-verify. */
    int has_bounds = 0;
    if (qp->l || qp->u) {
        for (int j = 0; j < n; j++) {
            if (qp->l && qp_bound_is_finite(qp_lb(qp, j))) { has_bounds = 1; break; }
            if (qp->u && qp_bound_is_finite(qp_ub(qp, j))) { has_bounds = 1; break; }
        }
    }
    double *x = (double*)xmalloc((size_t)n * sizeof(double));
    memset(x, 0, (size_t)n * sizeof(double));
    int fr;
    if (qp->me > 0 || has_bounds) {
        int maxr = qp->m + 2 * qp->me + 2 * n;
        double *Af = (double*)psolve_calloc((size_t)maxr * n, sizeof(double));
        double *bf = (double*)psolve_calloc((size_t)maxr, sizeof(double));
        int r = 0;
        for (int i = 0; i < qp->m; i++) {
            for (int j = 0; j < n; j++) Af[(size_t)r*n + j] = qp->A[(size_t)i*n + j];
            bf[r++] = qp->b[i];
        }
        for (int i = 0; i < qp->me; i++) {
            for (int j = 0; j < n; j++) {
                double a = qp->Aeq[(size_t)i*n + j];
                Af[(size_t)r*n + j] = a; Af[(size_t)(r+1)*n + j] = -a;
            }
            bf[r++] = qp->beq[i];
            bf[r++] = -qp->beq[i];
        }
        for (int j = 0; j < n; j++) {
            if (qp->l && qp_bound_is_finite(qp_lb(qp, j))) {
                Af[(size_t)r*n + j] = -1.0; bf[r++] = -qp_lb(qp, j);
            }
            if (qp->u && qp_bound_is_finite(qp_ub(qp, j))) {
                Af[(size_t)r*n + j] = 1.0; bf[r++] = qp_ub(qp, j);
            }
        }
        QP fp; memset(&fp, 0, sizeof(fp));
        fp.n = n; fp.m = r; fp.Q = qp->Q; fp.c = qp->c;
        fp.A = Af; fp.b = bf; fp.x0 = qp->x0; fp.phase1_order = qp->phase1_order;
        QPResult rf; memset(&rf, 0, sizeof(rf));
        fr = find_feasible(&fp, qp->x0, x, &rf);
        if (fr == 0) {
            res->status = -1;
            res->infeasible_proven = rf.infeasible_proven;
            qp_result_free(&rf);
            psolve_free(Af); psolve_free(bf); psolve_free(x);
            return;
        }
        if (fr == 2) {
            res->status = QP_STOPPED;
            res->n = n; res->x = NULL; res->obj = 0.0; res->iterations = 0;
            qp_result_free(&rf);
            psolve_free(Af); psolve_free(bf); psolve_free(x);
            return;
        }
        qp_result_free(&rf);
        psolve_free(Af); psolve_free(bf);
        active_set(qp, x, res);
    } else {
        fr = find_feasible(qp, qp->x0, x, res);
        if (fr == 0) {                         /* status stays -1: no feasible start */
            psolve_free(x);
            return;                            /* res->infeasible_proven says whether
                                                  that is a proof or a give-up */
        }
        if (fr == 2) {
            /* Cooperatively stopped while searching for a feasible point, so there
               is no feasible incumbent to hand back.  Report QP_STOPPED with an
               empty solution rather than a possibly-infeasible iterate. */
            res->status = QP_STOPPED;
            res->n = n; res->x = NULL; res->obj = 0.0; res->iterations = 0;
            psolve_free(x);
            return;
        }
        active_set(qp, x, res);
    }
    psolve_free(x);
    if (res->x) {                              /* report how feasible the answer is,
                                                  in the caller's own units */
        double mr = 0.0;
        for (int i = 0; i < qp->m; i++) mr = fmax(mr, row_resid(qp, i, res->x));
        for (int i = 0; i < qp->me; i++) mr = fmax(mr, fabs(eq_resid(qp, i, res->x)));
        for (int j = 0; j < n; j++) {
            if (qp->l && qp_bound_is_finite(qp_lb(qp, j)))
                mr = fmax(mr, qp_lb(qp, j) - res->x[j]);
            if (qp->u && qp_bound_is_finite(qp_ub(qp, j)))
                mr = fmax(mr, res->x[j] - qp_ub(qp, j));
        }
        res->max_resid = mr > 0.0 ? mr : 0.0;
    }
}

void qp_solve_sparse(const QPSparse *s, QPResult *res)
{
    if (!res) return;
    memset(res, 0, sizeof(*res));
    res->status = QP_INVALID;
    if (!s || s->n <= 0 || s->m < 0 || s->n > 8192 || s->m > 1000000 ||
        s->me < 0 || s->me > 1000000 || !s->c ||
        (s->m > 0 && (!s->Acolptr || !s->Arow || !s->Aval || !s->b)) ||
        (s->me > 0 && (!s->Aeq || !s->beq)) ||
        (s->nq < 0) ||
        (s->nq > 0 && (!s->q_w || !s->q_rk_colptr || !s->q_rk_rowi || !s->q_rk_val))) {
        return;
    }
    int n = s->n, m = s->m, me = s->me;
    if ((size_t)n > (size_t)-1 / (size_t)n) return;
    if (m > 0 && (size_t)m > (size_t)-1 / (size_t)n) return;
    if (me > 0 && (size_t)me > (size_t)-1 / (size_t)n) return;
    if (s->m > 0) {
        for (int j = 0; j < n; j++) {
            if (s->Acolptr[j] < 0 || s->Acolptr[j+1] < s->Acolptr[j]) return;
        }
    }
    long long rk_total = -1;
    if (s->nq > 0) {
        for (int k = 0; k < s->nq; k++) {
            long long lo = s->q_rk_colptr[k], hi = s->q_rk_colptr[k+1];
            if (lo < 0 || hi < lo || (size_t)hi > (size_t)-1 / sizeof(double)) return;
        }
        rk_total = s->q_rk_colptr[s->nq];
        if (rk_total < 0) return;
        (void)rk_total;
    }
    size_t qn = (size_t)n * n;
    double *Q = (double*)psolve_calloc(qn, sizeof(double));
    double *A = m > 0 ? (double*)psolve_calloc((size_t)m * n, sizeof(double)) : NULL;
    double *Aeq = me > 0 ? (double*)psolve_calloc((size_t)me * n, sizeof(double)) : NULL;
    if (!Q || (m > 0 && !A) || (me > 0 && !Aeq)) {
        psolve_free(Q); psolve_free(A); psolve_free(Aeq);
        return;
    }
    if (s->q_diag)
        for (int j = 0; j < n; j++) Q[(size_t)j*n + j] = s->q_diag[j];
    for (int k = 0; k < s->nq; k++) {
        int lo = s->q_rk_colptr[k];
        int hi = s->q_rk_colptr[k+1];
        double w = s->q_w[k];
        for (int a = lo; a < hi; a++) {
            int i = s->q_rk_rowi[a];
            double vi = s->q_rk_val[a];
            if (i < 0 || i >= n) { psolve_free(Q); psolve_free(A); psolve_free(Aeq); return; }
            for (int b = a; b < hi; b++) {          /* each unordered pair once */
                int j = s->q_rk_rowi[b];
                double vj = s->q_rk_val[b];
                if (j < 0 || j >= n) { psolve_free(Q); psolve_free(A); psolve_free(Aeq); return; }
                if (i == j) Q[(size_t)i*n + i] += w * vi * vj;
                else {
                    Q[(size_t)i*n + j] += w * vi * vj;
                    Q[(size_t)j*n + i] += w * vi * vj;
                }
            }
        }
    }
    if (m > 0)
        for (int j = 0; j < n; j++)
            for (int p = s->Acolptr[j]; p < s->Acolptr[j+1]; p++) {
                int i = s->Arow[p];
                if (i < 0 || i >= m) { psolve_free(Q); psolve_free(A); psolve_free(Aeq); return; }
                A[(size_t)i*n + j] = s->Aval[p];
            }
    if (me > 0)
        for (int i = 0; i < me; i++)
            for (int j = 0; j < n; j++) Aeq[(size_t)i*n + j] = s->Aeq[(size_t)i*n + j];

    QP qp; memset(&qp, 0, sizeof(qp));
    qp.n = n; qp.m = m; qp.me = me; qp.Q = Q; qp.c = s->c;
    qp.A = A; qp.b = s->b; qp.Aeq = Aeq; qp.beq = s->beq;
    qp.l = s->l; qp.u = s->u; qp.x0 = s->x0;
    qp.phase1_order = (s->phase1_order == QP_PHASE1_LP_FIRST) ? QP_PHASE1_LP_FIRST : QP_PHASE1_DENSE_FIRST;
    qp_solve(&qp, res);
    psolve_free(Q); psolve_free(A); psolve_free(Aeq);
}

void qp_result_free(QPResult *res)
{
    if (!res) return;
    psolve_free(res->x); psolve_free(res->mult); psolve_free(res->ray); psolve_free(res->farkas);
    psolve_free(res->mult_eq); psolve_free(res->mult_l); psolve_free(res->mult_u);
    memset(res, 0, sizeof(*res));
}
