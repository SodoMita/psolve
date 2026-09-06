/* Unified evidence-object checker (roadmap 6.4; see cert.h for the
   contract and docs/DESIGN.md section 8.9 for every tolerance below).
   All checks run over the caller's ORIGINAL data; nothing here trusts
   engine internals. */

#include "cert.h"
#include "solver.h"          /* solver_farkas_boxcert (directed rounding) */
#include <math.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>

/* Conservative substitutes when a claim leaves a margin field at 0. */
#define PSV_DEF_GT_BOX 1e-6   /* TOLSHEET TOL-CERT-DEFBOX */
#define PSV_DEF_GT_ROW 1e-5   /* TOLSHEET TOL-CERT-DEFROW */
#define PSV_DEF_DT_DJ  1e-9   /* TOLSHEET TOL-CERT-DEFDJ */
#define PSV_DEF_DT_GAP 1e-7   /* TOLSHEET TOL-CERT-DEFGAP */
#define PSV_DEF_DT_OBJ 1e-9   /* TOLSHEET TOL-CERT-DEFOBJ */

/* macro-component threshold for rays: |d_j| beyond this share of the
   infinity norm must have an infinite bound side to walk into */
#define PSV_RAY_MACRO( dmax ) (1e-9 * (1.0 + (dmax)))  /* TOLSHEET TOL-CERT-RAYMACRO */

/* engine incumbents are stored snapped, so integrality is checkable
   exactly; kept as a named helper for the one place this contract lives */
static int x_neq_rint(double v) { return v != rint(v); }

const char *psv_cert_kind_name(PsvKind k)
{
    switch (k) {
    case PSVK_LP_OPTIMAL:    return "lp_optimal";
    case PSVK_LP_INFEASIBLE: return "lp_infeasible";
    case PSVK_LP_UNBOUNDED:  return "lp_unbounded";
    case PSVK_MIP_POINT:     return "mip_point";
    case PSVK_EXHAUSTION:    return "exhaustion";
    case PSVK_QP_OPTIMAL:    return "qp_optimal";
    case PSVK_QP_UNBOUNDED:  return "qp_unbounded";
    case PSVK_QP_INFEASIBLE: return "qp_infeasible";
    default:                 return "?";
    }
}

/* row residual a_i^T x over CSC; also returns the activity scale
   sum_j |a_ij * x_j| - the rounding dust of the double dot product is
   ~n*eps times THAT, not times |b_i|.  Sizing the grace by the activity
   is what keeps the check decidable on catastrophic-cancellation data
   (a0*x0 + a1*x1 with |a~1e18| nearly cancelling: an RN residual is off
   by thousands while |b| is 3e3 - see the fbbt_verify cancellation
   family; a tighter |b|-only grace false-rejects TRUE points there). */
static double row_res(const PsvCert *cl, int i, const double *x, double *act)
{
    double r = 0.0, ax = 0.0;
    for (int j = 0; j < cl->n; j++)
        for (int k = cl->colptr[j]; k < cl->colptr[j + 1]; k++)
            if (cl->row[k] == i) {
                double t = cl->val[k] * x[j];
                r += t; ax += fabs(t);
            }
    if (act) *act = ax;
    return r;
}

/* primal point vs box + rows; returns 1 if within the grace margins */
static int lp_primal_ok(const PsvCert *cl, const double *x,
                        double gt_box, double gt_row)
{
    for (int j = 0; j < cl->n; j++) {
        if (!isfinite(x[j])) return 0;
        if (cl->lo[j] > -PSV_INF && x[j] < cl->lo[j] - gt_box) return 0;
        if (cl->hi[j] <  PSV_INF && x[j] > cl->hi[j] + gt_box) return 0;
    }
    for (int i = 0; i < cl->m; i++) {
        double act = 0.0;
        double r = row_res(cl, i, x, &act);
        double g = gt_row * (1.0 + fabs(cl->b[i]) + act);
        switch (cl->rel[i]) {
        case '<': if (r > cl->b[i] + g) return 0; break;
        case '>': if (r < cl->b[i] - g) return 0; break;
        default:  if (fabs(r - cl->b[i]) > g) return 0; break;
        }
    }
    return 1;
}

static double lp_cTx(const PsvCert *cl, const double *x, double *act)
{
    double s = 0.0, ax = 0.0;
    for (int j = 0; j < cl->n; j++) {
        double t = cl->c[j] * x[j];
        s += t; ax += fabs(t);
    }
    if (act) *act = ax;
    return s;
}

/* A^T y for the ORIGINAL model (y in original row space) */
static void lp_ATy(const PsvCert *cl, const double *y, double *rc, double s)
{
    for (int j = 0; j < cl->n; j++) rc[j] = s * cl->c[j];
    for (int j = 0; j < cl->n; j++)
        for (int k = cl->colptr[j]; k < cl->colptr[j + 1]; k++)
            rc[j] -= cl->val[k] * y[cl->row[k]];
}

static PsvRc check_lp_optimal(const PsvCert *cl)
{
    if (!cl->x || !cl->y || !cl->c) return PSV_DEFER;
    if (!isfinite(cl->obj)) return PSV_DEFER;
    double gt_box = cl->gt_box > 0 ? cl->gt_box : PSV_DEF_GT_BOX;
    double gt_row = cl->gt_row > 0 ? cl->gt_row : PSV_DEF_GT_ROW;
    double dt_dj  = cl->dt_dj  > 0 ? cl->dt_dj  : PSV_DEF_DT_DJ;
    double dt_gap = cl->dt_gap > 0 ? cl->dt_gap : PSV_DEF_DT_GAP;
    double dt_obj = cl->dt_obj > 0 ? cl->dt_obj : PSV_DEF_DT_OBJ;
    gt_box *= (1.0 + fabs(cl->obj));
    double s = cl->maximize ? 1.0 : -1.0;

    if (!lp_primal_ok(cl, cl->x, gt_box, gt_row)) { return PSV_REJECT; }

    double cact = 0.0;
    double cx = lp_cTx(cl, cl->x, &cact);
    if (fabs(s * cl->obj - s * cx) > dt_obj * (1.0 + fabs(cx) + cact)) { return PSV_REJECT; }

    /* clip sign-invalid dual components (soundly weakens the bound) */
    double *yc = (double*)malloc((size_t)(cl->m ? cl->m : 1) * sizeof(double));
    double *rc = (double*)malloc((size_t)(cl->n ? cl->n : 1) * sizeof(double));
    if (!yc || !rc) { free(yc); free(rc); return PSV_DEFER; }
    for (int i = 0; i < cl->m; i++) {
        /* engine duals arrive in the ORIGINAL objective sense (shadow-price
           convention, solver_duals); the Lagrangian-bound machinery works
           in max form, so convert first, THEN clip per max-form rules */
        double yi = s * cl->y[i];
        yc[i] = yi;
        /* any non-finite dual component poisons the certificate */
        if (!isfinite(yi)) { free(yc); free(rc); return PSV_DEFER; }
        if (cl->rel[i] == '<' && yc[i] < 0.0) yc[i] = 0.0;
        else if (cl->rel[i] == '>' && yc[i] > 0.0) yc[i] = 0.0;
    }
    lp_ATy(cl, yc, rc, s);
    /* dual bound:  B = y^T b + sum_j sup_{x_j in [lo_j,hi_j]} rc_j x_j
       (valid for the max of s*c over the box, weak duality: s*c^T x <= B).
       Reduced costs within the dj window win = dt_dj*(1+||rc||inf) are
       arithmetic dust (the engine's own optimality test, TOL-LP-DJ, stops
       at this scale): their corner contribution is charged UPWARD as
       |rc_j|*max(|lo_j|,|hi_j|) so B stays a sound upper estimate; a free
       variable with |rc| beyond the window means the dual handed in cannot
       certify anything finite - honest defer. */
    double rmax = 0.0;
    for (int j = 0; j < cl->n; j++) if (fabs(rc[j]) > rmax) rmax = fabs(rc[j]);
    double win = dt_dj * (1.0 + rmax);
    double ytb = 0.0;
    for (int i = 0; i < cl->m; i++) ytb += yc[i] * cl->b[i];
    double B = ytb;
    for (int j = 0; j < cl->n; j++) {
        if (rc[j] > win) {
            if (!(cl->hi[j] < PSV_INF)) { free(yc); free(rc); return PSV_DEFER; }
            B += cl->hi[j] * rc[j];
        } else if (rc[j] < -win) {
            if (!(cl->lo[j] > -PSV_INF)) { free(yc); free(rc); return PSV_DEFER; }
            B += cl->lo[j] * rc[j];
        } else {
            /* |rc_j| <= win: sound upward corner charge on closed boxes.
               With an open side the dust entry is treated as exactly zero
               (the same dj-class grace the simplex's own optimality test
               stops at, TOL-LP-DJ) - charging an |rc|*1e30 sentinel term
               here would be the 6.8 mistake class, corrupting the bound. */
            int lo_open = !(cl->lo[j] > -PSV_INF), hi_open = !(cl->hi[j] < PSV_INF);
            if (!lo_open && !hi_open) {
                double m = fabs(cl->lo[j]) > fabs(cl->hi[j]) ? fabs(cl->lo[j]) : fabs(cl->hi[j]);
                B += fabs(rc[j]) * m;
            }
        }
        /* both sides infinite and |rc| <= win: dj-toleranced zero */
    }
    free(yc); free(rc);
    if (!isfinite(B)) return PSV_DEFER;
    double gap = B - s * cx;
    double gscale = 1.0 + fabs(B) + fabs(cx) + fabs(ytb);
    /* weak-duality violation beyond arithmetic grace: evidence contradicts */
    if (gap < -dt_gap * gscale) return PSV_REJECT;
    /* certified bound not closed by the point: it is not optimal */
    if (gap > (dt_gap + dt_dj) * gscale) return PSV_REJECT;
    return PSV_OK;
}

static PsvRc check_lp_infeasible(const PsvCert *cl)
{
    if (!cl->ray) return PSV_DEFER;
    double dt_gap = cl->dt_gap > 0 ? cl->dt_gap : PSV_DEF_DT_GAP;
    double *y = (double*)malloc((size_t)(cl->m ? cl->m : 1) * sizeof(double));
    double *zl = (double*)malloc((size_t)(cl->n ? cl->n : 1) * sizeof(double));
    double *zh = (double*)malloc((size_t)(cl->n ? cl->n : 1) * sizeof(double));
    int *one = (int*)malloc((size_t)(cl->m ? cl->m : 1) * sizeof(int));
    if (!y || !zl || !zh || !one) { free(y); free(zl); free(zh); free(one); return PSV_DEFER; }
    for (int i = 0; i < cl->m; i++) one[i] = 1;
    /* any non-finite ray component must poison the check (boxcert refuses
       internally too, but a cheap early-out keeps the contract explicit) */
    for (int i = 0; i < cl->m; i++) if (!isfinite(cl->ray[i])) { free(y); free(zl); free(zh); free(one); return PSV_DEFER; }
    int ok = solver_farkas_boxcert(cl->n, cl->m, cl->colptr, cl->row, cl->val,
                                   cl->rel, cl->b, cl->lo, cl->hi,
                                   cl->ray, one, dt_gap, y, zl, zh);
    free(y); free(zl); free(zh); free(one);
    return ok ? PSV_OK : PSV_REJECT;
}

static PsvRc check_lp_unbounded(const PsvCert *cl)
{
    if (!cl->x || !cl->ray || !cl->c) return PSV_DEFER;
    double gt_box = cl->gt_box > 0 ? cl->gt_box : PSV_DEF_GT_BOX;
    double gt_row = cl->gt_row > 0 ? cl->gt_row : PSV_DEF_GT_ROW;
    double dt_dj  = cl->dt_dj  > 0 ? cl->dt_dj  : PSV_DEF_DT_DJ;
    gt_box *= (1.0 + fabs(cl->obj));
    if (!lp_primal_ok(cl, cl->x, gt_box, gt_row)) return PSV_REJECT;
    double dmax = 0.0;
    for (int j = 0; j < cl->n; j++) {
        if (!isfinite(cl->ray[j])) return PSV_DEFER;
        if (fabs(cl->ray[j]) > dmax) dmax = fabs(cl->ray[j]);
    }
    if (!(dmax > 0.0)) return PSV_DEFER;      /* zero ray: no evidence */
    double macro = PSV_RAY_MACRO(dmax);
    /* every macro component needs an open bound on the side it walks to */
    for (int j = 0; j < cl->n; j++) {
        if (cl->ray[j] > macro && !(cl->hi[j] >= PSV_INF)) return PSV_REJECT;
        if (cl->ray[j] < -macro && !(cl->lo[j] <= -PSV_INF)) return PSV_REJECT;
    }
    /* recession: '=' rows must stay on, '<' rows must not increase,
       '>' rows must not decrease (all at reduced-cost-scale grace) */
    for (int i = 0; i < cl->m; i++) {
        double act = 0.0;
        double ad = row_res(cl, i, cl->ray, &act);
        double g = dt_dj * (1.0 + act);
        switch (cl->rel[i]) {
        case '<': if (ad >  g) return PSV_REJECT; break;
        case '>': if (ad < -g) return PSV_REJECT; break;
        default:  if (fabs(ad) > g) return PSV_REJECT; break;
        }
    }
    double s = cl->maximize ? 1.0 : -1.0;
    double cdact = 0.0;
    double cd = lp_cTx(cl, cl->ray, &cdact);
    if (s * cd <= dt_dj * (1.0 + cdact)) return PSV_REJECT;
    return PSV_OK;
}

static PsvRc check_mip_point(const PsvCert *cl)
{
    if (!cl->x || !cl->c) { return PSV_DEFER; }
    if (!isfinite(cl->obj)) { return PSV_DEFER; }
    double gt_box = cl->gt_box > 0 ? cl->gt_box : PSV_DEF_GT_BOX;
    double gt_row = cl->gt_row > 0 ? cl->gt_row : PSV_DEF_GT_ROW;
    double dt_obj = cl->dt_obj > 0 ? cl->dt_obj : PSV_DEF_DT_OBJ;
    double dt_gap = cl->dt_gap > 0 ? cl->dt_gap : PSV_DEF_DT_GAP;
    gt_box *= (1.0 + fabs(cl->obj));
    if (!lp_primal_ok(cl, cl->x, gt_box, gt_row)) { return PSV_REJECT; }
    if (cl->isint) {
        /* engine incumbents are stored snapped: integrality is a
           zero-width property here and is checked exactly */
        for (int j = 0; j < cl->n; j++)
            if (cl->isint[j] && x_neq_rint(cl->x[j])) { return PSV_REJECT; }
    }
    double cact = 0.0;
    double cx = lp_cTx(cl, cl->x, &cact);
    if (fabs(cl->obj - cx) > dt_obj * (1.0 + fabs(cx) + cact)) { return PSV_REJECT; }
    if (cl->proven_optimal) {
        /* Bound coherence, DIRECTIONAL.  The engine's best_bound is a
           running extremum over every solved node relaxation, so the root
           LP bound dominates it forever: for a proven optimum it does NOT
           close to obj - the leftover distance is the model's root
           integrality gap, which is arbitrary.  What a certificate CAN
           demand is directional validity of the pair (obj, bound): an
           upper bound (max sense) must never sit below the incumbent's
           objective, a lower bound (min sense) never above it.  A
           violation is exactly the catastrophic direction - reporting an
           objective strictly better than the proven bound. */
        double allow = ((cl->mip_gap > 0 ? cl->mip_gap : 0.0) + dt_gap)
                       * (1.0 + fabs(cl->obj) + fabs(cl->best_bound));
        if (!isfinite(cl->best_bound)) { return PSV_REJECT; }
        if ( cl->maximize && cl->best_bound < cl->obj - allow) { return PSV_REJECT; }
        if (!cl->maximize && cl->best_bound > cl->obj + allow) { return PSV_REJECT; }
    }
    return PSV_OK;
}

static PsvRc check_exhaustion(const PsvCert *cl)
{
    if (cl->stopped) return PSV_REJECT;
    if (cl->node_limit > 0 && cl->nodes >= cl->node_limit) return PSV_REJECT;
    if (cl->nodes < 0) return PSV_REJECT;
    return PSV_OK;
}

static double qp_obj(const PsvCert *cl, const double *x, double *act)
{
    double v = 0.0, ax = 0.0;
    for (int j = 0; j < cl->n; j++) {
        double qx = 0.0;
        for (int i = 0; i < cl->n; i++) qx += cl->Q[(size_t)j * cl->n + i] * x[i];
        double tq = 0.5 * qx * x[j], tl = cl->cq[j] * x[j];
        v += tq + tl; ax += fabs(tq) + fabs(tl);
    }
    if (act) *act = ax;
    return v;
}

static void qp_grad(const PsvCert *cl, const double *x, double *g)
{
    for (int i = 0; i < cl->n; i++) {
        double qi = 0.0;
        for (int j = 0; j < cl->n; j++) qi += cl->Q[(size_t)j * cl->n + i] * x[j];
        g[i] = qi + cl->cq[i];
    }
}

/* dense QP row residual + activity scale (same dust form as row_res) */
static double qp_row_res(const PsvCert *cl, int i, const double *x, double *act)
{
    double r = -cl->bq[i], ax = fabs(cl->bq[i]);
    for (int j = 0; j < cl->n; j++) {
        double t = cl->A[(size_t)i * cl->n + j] * x[j];
        r += t; ax += fabs(t);
    }
    if (act) *act = ax;
    return r;
}

static PsvRc check_qp_optimal(const PsvCert *cl)
{
    if (!cl->x || !cl->mu || !cl->Q || !cl->A || !cl->bq || !cl->cq)
        return PSV_DEFER;
    if (!isfinite(cl->obj)) return PSV_DEFER;
    double gt_row = cl->gt_row > 0 ? cl->gt_row : PSV_DEF_DT_GAP; /* 1e-7 class */
    double dt_dj  = cl->dt_dj  > 0 ? cl->dt_dj  : PSV_DEF_DT_GAP;
    double dt_obj = cl->dt_obj > 0 ? cl->dt_obj : PSV_DEF_GT_BOX;
    /* primal rows A x <= bq */
    for (int i = 0; i < cl->m; i++) {
        double act = 0.0;
        double rr = qp_row_res(cl, i, cl->x, &act);
        if (rr > gt_row * (1.0 + act)) return PSV_REJECT;
    }
    /* stationarity: Q x + c + A^T mu = 0 with engine-mirrored scaling */
    double *g = (double*)malloc((size_t)(cl->n ? cl->n : 1) * sizeof(double));
    if (!g) return PSV_DEFER;
    qp_grad(cl, cl->x, g);
    double kkt = 0.0, gmax = 0.0, tmax = 0.0;
    for (int j = 0; j < cl->n; j++) {
        double rj = g[j];
        if (fabs(g[j]) > gmax) gmax = fabs(g[j]);
        for (int i = 0; i < cl->m; i++) {
            double t = cl->A[(size_t)i * cl->n + j] * cl->mu[i];
            rj += t;
            if (fabs(t) > tmax) tmax = fabs(t);
        }
        if (fabs(rj) > kkt) kkt = fabs(rj);
    }
    free(g);
    if (kkt > dt_dj * (1.0 + gmax + tmax)) return PSV_REJECT;
    /* multiplier sign (mu >= 0 for Ax<=b in a minimization) and
       complementary slackness against the ORIGINAL rows */
    double muscale = 0.0;
    for (int i = 0; i < cl->m; i++) if (fabs(cl->mu[i]) > muscale) muscale = fabs(cl->mu[i]);
    for (int i = 0; i < cl->m; i++) {
        if (!isfinite(cl->mu[i])) return PSV_DEFER;
        if (cl->mu[i] < -dt_dj * (1.0 + muscale)) return PSV_REJECT;
        if (cl->mu[i] > dt_dj * (1.0 + muscale)) {
            double act = 0.0;
            double rr = qp_row_res(cl, i, cl->x, &act);
            if (fabs(rr) > gt_row * (1.0 + act)) return PSV_REJECT;
        }
    }
    double oact = 0.0;
    double o = qp_obj(cl, cl->x, &oact);
    if (fabs(cl->obj - o) > dt_obj * (1.0 + fabs(o) + oact)) return PSV_REJECT;
    return PSV_OK;
}

static PsvRc check_qp_unbounded(const PsvCert *cl)
{
    if (!cl->x || !cl->ray || !cl->Q || !cl->A || !cl->bq || !cl->cq)
        return PSV_DEFER;
    double gt_row = cl->gt_row > 0 ? cl->gt_row : PSV_DEF_DT_GAP;
    double dt_dj  = cl->dt_dj  > 0 ? cl->dt_dj  : PSV_DEF_DT_DJ;
    /* the point itself must be primal feasible */
    for (int i = 0; i < cl->m; i++) {
        double act = 0.0;
        double rr = qp_row_res(cl, i, cl->x, &act);
        if (rr > gt_row * (1.0 + act)) { return PSV_REJECT; }
    }
    double pinf = 0.0, qnorm = 0.0;
    for (int j = 0; j < cl->n; j++) {
        if (!isfinite(cl->ray[j])) return PSV_DEFER;
        pinf = fmax(pinf, fabs(cl->ray[j]));
    }
    if (!(pinf > 0.0)) return PSV_DEFER;
    for (int i = 0; i < cl->n * cl->n; i++) qnorm = fmax(qnorm, fabs(cl->Q[i]));
    /* A d <= 0 strictly, mirroring qp.c's deliberate no-margin rule: any
       rounding-level positive slope eventually blocks the ray */
    for (int i = 0; i < cl->m; i++) {
        double ap = 0.0;
        for (int j = 0; j < cl->n; j++) ap += cl->A[(size_t)i * cl->n + j] * cl->ray[j];
        if (ap > 0.0) { return PSV_REJECT; }
    }
    /* curvature d^T Q d ~ 0 */
    double pQp = 0.0;
    for (int i = 0; i < cl->n; i++) {
        double qi = 0.0;
        for (int j = 0; j < cl->n; j++) qi += cl->Q[(size_t)j * cl->n + i] * cl->ray[j];
        pQp += qi * cl->ray[i];
    }
    if (pQp > 1e-12 * (1.0 + qnorm) * pinf * pinf) return PSV_REJECT;  /* TOLSHEET TOL-CERT-QPCURV */
    /* descent: (Q x + c)^T d < 0 */
    double *g = (double*)malloc((size_t)(cl->n ? cl->n : 1) * sizeof(double));
    if (!g) return PSV_DEFER;
    qp_grad(cl, cl->x, g);
    double gp = 0.0, gnorm = 0.0;
    for (int j = 0; j < cl->n; j++) { gp += g[j] * cl->ray[j]; gnorm = fmax(gnorm, fabs(g[j])); }
    free(g);
    if (gp >= -dt_dj * (1.0 + gnorm) * pinf) { return PSV_REJECT; }
    return PSV_OK;
}

/* PSVK_QP_INFEASIBLE: see the contract in cert.h.  Deliberately its own
 * arithmetic rather than a call into qp.c's producer-side verifier: a checker
 * that shares code with what it checks cannot catch that code. */
static PsvRc check_qp_infeasible(const PsvCert *cl)
{
    if (!cl->A || !cl->bq || !cl->ray) return PSV_DEFER;
    if (cl->m <= 0) return PSV_DEFER;          /* an empty row set is never empty by proof */
    double gt_row = cl->gt_row > 0 ? cl->gt_row : PSV_DEF_GT_ROW;
    double dt_gap = cl->dt_gap > 0 ? cl->dt_gap : PSV_DEF_DT_GAP;
    int n = cl->n, m = cl->m;
    double lam_max = 0.0, lmin = 0.0;
    for (int i = 0; i < m; i++) {
        double v = cl->ray[i];
        if (!isfinite(v)) return PSV_DEFER;
        if (fabs(v) > lam_max) lam_max = fabs(v);
        if (i == 0 || v < lmin) lmin = v;
    }
    if (!(lam_max > 0.0)) return PSV_DEFER;                  /* vacuous */
    if (lmin < -gt_row * lam_max) return PSV_REJECT;         /* not a nonnegative combination */
    for (int j = 0; j < n; j++) {
        double sv = 0.0, mag = 0.0;
        for (int i = 0; i < m; i++) {
            double a = cl->A[(size_t)i * n + j];
            if (!isfinite(a)) return PSV_DEFER;
            sv  += a * cl->ray[i];
            mag += fabs(a) * fabs(cl->ray[i]);
        }
        if (!(fabs(sv) <= gt_row * (1.0 + mag))) return PSV_REJECT;   /* A^T ray != 0 */
    }
    double bt = 0.0, bmag = 0.0;
    for (int i = 0; i < m; i++) {
        if (!isfinite(cl->bq[i])) return PSV_DEFER;
        bt   += cl->ray[i] * cl->bq[i];
        bmag += fabs(cl->ray[i]) * fabs(cl->bq[i]);
    }
    return (bt <= -dt_gap * (1.0 + bmag)) ? PSV_OK : PSV_REJECT;
}

PsvRc psv_cert_check(const PsvCert *cl)
{
    if (!cl) return PSV_DEFER;
    if (cl->kind != PSVK_EXHAUSTION) {
        if (cl->n < 0 || cl->m < 0) return PSV_DEFER;
        if (cl->kind != PSVK_QP_OPTIMAL && cl->kind != PSVK_QP_UNBOUNDED &&
            cl->kind != PSVK_QP_INFEASIBLE)   /* the QP kinds carry dense rows, not the LP view */
            if (!cl->colptr || !cl->row || !cl->val || !cl->rel || !cl->b ||
                !cl->lo || !cl->hi) return PSV_DEFER;
    }
    switch (cl->kind) {
    case PSVK_LP_OPTIMAL:    return check_lp_optimal(cl);
    case PSVK_LP_INFEASIBLE: return check_lp_infeasible(cl);
    case PSVK_LP_UNBOUNDED:  return check_lp_unbounded(cl);
    case PSVK_MIP_POINT:     return check_mip_point(cl);
    case PSVK_EXHAUSTION:    return check_exhaustion(cl);
    case PSVK_QP_OPTIMAL:    return check_qp_optimal(cl);
    case PSVK_QP_UNBOUNDED:  return check_qp_unbounded(cl);
    case PSVK_QP_INFEASIBLE: return check_qp_infeasible(cl);
    default:                 return PSV_DEFER;
    }
}
