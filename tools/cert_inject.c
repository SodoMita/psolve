/* cert_inject.c - error-injection acceptance for the unified evidence
 * objects (roadmap 6.4).  Drives the real engines (LP via lib solver, QP,
 * MIP) on small random instances, wraps their verdicts in psv claims, and
 * asks psv_cert_check three questions:
 *
 *   LEGIT: does the checker accept the TRUE claim?            (must be 100%
 *           for every family - a legit reject is a false-rejection bug in
 *           the certificate layer and fails this gate)
 *   LARGE: does the checker reject payload corruption at 1e-1..1e-3 scale
 *           (out-of-box pushes, obj drifts, int off-by-one, negated rays,
 *           flipped statuses)?                              (must be 100%
 *           of ADVERSARIAL shots: an independent oracle - closed-form box
 *           extrema in long double, never the checker itself - counts a
 *           corruption only when it provably falsifies a checked surface;
 *           a corrupted payload that still proves the claim accepts
 *           correctly and is skipped, not a miss)
 *   ULP:   directed 1-ulp noise on payload results.  Reported per family;
 *           hard-asserted only where a zero-width surface makes it
 *           structurally decidable (the snapped-integer surface of the
 *           MIP engine) - tolerance-absorbing surfaces are REPORTED
 *           honestly, not asserted (see AUDIT addendum (12)).
 *
 * Everything is deterministic (LCG seeds); prints a measured table and
 * exits nonzero if any REQUIRED class falls short. */

#include "solver.h"
#include "parser.h"
#include "mip.h"
#include "qp.h"
#include "cert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

/* ------------------------------------------------------------------ */
/* tiny deterministic RNG                                              */
static unsigned long long RS;
static double rnd(void) { RS = RS * 6364136223846793005ull + 1442695040888963407ull; return (double)(RS >> 11) * (1.0 / 9007199254740992.0); }
static double rrange(double a, double b) { return a + (b - a) * rnd(); }

/* ------------------------------------------------------------------ */
/* LP construction helpers (dense -> CSC in an LP struct, no files)     */
static void mk_lp(LP *lp, int n, int m, const double *c, const double *A,
                  const double *b, const char *rel, const double *lo,
                  const double *hi, int maximize)
{
    memset(lp, 0, sizeof(*lp));
    lp->n = n; lp->m = m; lp->maximize = maximize;
    lp->c = (double*)malloc(sizeof(double) * n);
    lp->b = (double*)malloc(sizeof(double) * m);
    lp->rel = (char*)malloc(m);
    lp->l = (double*)malloc(sizeof(double) * n);
    lp->u = (double*)malloc(sizeof(double) * n);
    memcpy(lp->c, c, sizeof(double) * n);
    memcpy(lp->b, b, sizeof(double) * m);
    memcpy(lp->rel, rel, m);
    memcpy(lp->l, lo, sizeof(double) * n);
    memcpy(lp->u, hi, sizeof(double) * n);
    lp->Acolptr = (int*)calloc((size_t)n + 1, sizeof(int));
    int nnz = 0;
    for (int j = 0; j < n; j++) for (int i = 0; i < m; i++) if (A[(size_t)i * n + j] != 0.0) nnz++;
    lp->Arow = (int*)malloc(sizeof(int) * nnz);
    lp->Aval = (double*)malloc(sizeof(double) * nnz);
    for (int j = 0, p = 0; j < n; j++) {
        lp->Acolptr[j] = p;
        for (int i = 0; i < m; i++)
            if (A[(size_t)i * n + j] != 0.0) { lp->Arow[p] = i; lp->Aval[p] = A[(size_t)i * n + j]; p++; }
    }
    lp->Acolptr[n] = nnz;
}
static void lp_free_local(LP *lp)
{
    free(lp->c); free(lp->b); free(lp->rel); free(lp->l); free(lp->u);
    free(lp->Acolptr); free(lp->Arow); free(lp->Aval);
}

static void fill_claim(PsvCert *cl, PsvKind k, const LP *lp)
{
    memset(cl, 0, sizeof(*cl));
    cl->kind = k; cl->n = lp->n; cl->m = lp->m;
    cl->colptr = lp->Acolptr; cl->row = lp->Arow; cl->val = lp->Aval;
    cl->rel = lp->rel; cl->b = lp->b; cl->lo = lp->l; cl->hi = lp->u;
    cl->c = lp->c; cl->maximize = lp->maximize;
}

static void lp_margins(PsvCert *cl)
{
    cl->gt_box = 1e-6; cl->gt_row = 1e-5; cl->dt_dj = 1e-9;
    cl->dt_gap = 1e-7; cl->dt_obj = 1e-9;
}

/* ------------------------------------------------------------------ */
/* counters                                                            */
typedef struct { long legit_ok, legit_no; long large_rej, large_tot;
                 long lr[4], lt[4]; long ulp_rej, ulp_tot; } Fam;
static Fam F[8];
static const char *FAMNAME[8] = { "lp_optimal", "lp_infeasible", "lp_unbounded",
                                  "mip_point", "qp_optimal", "qp_unbounded",
                                  "exhaustion", "status_flip" };

static void legit(Fam *f, PsvRc r) { if (r == PSV_OK) f->legit_ok++; else f->legit_no++; }
static void shot_large_mode(Fam *f, const PsvCert *cl, int mode)
{
    f->large_tot++; f->lt[mode]++;
    if (psv_cert_check(cl) != PSV_OK) { f->large_rej++; f->lr[mode]++; }
}
static void shot_large(Fam *f, const PsvCert *cl) { shot_large_mode(f, cl, 3); }
static void shot_ulp(Fam *f, const PsvCert *cl) { f->ulp_tot++; if (psv_cert_check(cl) != PSV_OK) f->ulp_rej++; }

static double nudge_down(double v) { return v == 0.0 ? -DBL_MIN : nextafter(v, -HUGE_VAL); }
static double nudge_up(double v)   { return v == 0.0 ?  DBL_MIN : nextafter(v,  HUGE_VAL); }

/* ------------------------------------------------------------------ */
/* independent adversarialness oracles (NOT the checker under test).   */
/* A LARGE shot may only be COUNTED when the corruption provably falsifies
   a surface the checker verifies - otherwise acceptance is the CORRECT
   answer (a corrupted payload that still proves the claim is not a
   counterexample: e.g. a permutation-invariant Farkas ray stays a valid
   Farkas proof).  These oracles re-derive validity directly from first
   principles (closed-form box extrema, long-double accumulation), so the
   counted rejection rate measures the checker, never test noise.       */

/* Farkas-ray validity over the ORIGINAL model, mirroring
   solver_farkas_boxcert semantics (mlt = identity): clip sign-invalid
   components, valid iff min_box (y^T A) x  >  y^T b + tol*(1+|y^T b|).
   Returns +1 provably valid, -1 provably invalid, 0 borderline (shot
   within decision dust; not counted either way). */
static int farkas_valid_oracle(const LP *lp, const double *y, double tol)
{
    int n = lp->n, m = lp->m;
    long double yc[16];
    for (int i = 0; i < m; i++) {
        long double yi = y[i];
        if (lp->rel[i] == '<' && yi < 0.0L) yi = 0.0L;
        else if (lp->rel[i] == '>' && yi > 0.0L) yi = 0.0L;
        yc[i] = yi;
    }
    long double R = 0.0L;
    for (int i = 0; i < m; i++) R += yc[i] * lp->b[i];
    long double L = 0.0L, scl = 1.0L;
    for (int j = 0; j < n; j++) {
        long double z = 0.0L;
        for (int k = lp->Acolptr[j]; k < lp->Acolptr[j + 1]; k++)
            z += yc[lp->Arow[k]] * lp->Aval[k];
        long double t1 = z * lp->l[j], t2 = z * lp->u[j];
        L += t1 < t2 ? t1 : t2;
        scl += fabsl(z) * (fabsl(lp->l[j]) + fabsl(lp->u[j]));
    }
    if (scl > 1e18L) return 0;              /* oracle dust no longer decidable */
    long double diff = L - R;
    long double mg = tol * (1.0L + fabsl(R));
    if (diff > mg) return 1;
    if (diff < -mg - 1e-9L * scl) return -1;
    return 0;
}

/* MIP off-by-one shot: adversarial iff SOME surface the PSVK_MIP_POINT
   lane checks is violated beyond a STRICT (10x) margin - box, rows,
   objective consistency, or the directional bound stamp.  A lattice
   neighbor that stays feasible with a below-incumbent objective is
   evidence-equivalent to the true claim's surfaces (the point+stamp
   class cannot see tree exhaustion), so it is skipped, not counted. */
static int mip_offbyone_adversarial(const PsvCert *cl, const double *x, double objnew)
{
    double gt_box = 10.0 * cl->gt_box * (1.0 + fabs(objnew));
    double gt_row = 10.0 * cl->gt_row;
    for (int j = 0; j < cl->n; j++) {
        if (cl->lo[j] > -1e29 && x[j] < cl->lo[j] - gt_box) return 1;
        if (cl->hi[j] <  1e29 && x[j] > cl->hi[j] + gt_box) return 1;
        if (cl->isint && cl->isint[j] && x[j] != rint(x[j])) return 1;
    }
    for (int i = 0; i < cl->m; i++) {
        double r = 0.0, act = 0.0;
        for (int j = 0; j < cl->n; j++)
            for (int k = cl->colptr[j]; k < cl->colptr[j + 1]; k++)
                if (cl->row[k] == i) { double t = cl->val[k] * x[j]; r += t; act += fabs(t); }
        double g = gt_row * (1.0 + fabs(cl->b[i]) + act);   /* activity-scaled, mirrors the checker */
        if (cl->rel[i] == '<' && r > cl->b[i] + g) return 1;
        if (cl->rel[i] == '>' && r < cl->b[i] - g) return 1;
        if (cl->rel[i] != '<' && cl->rel[i] != '>' && fabs(r - cl->b[i]) > g) return 1;
    }
    /* bound stamp wrong side (checker rejects only the catastrophic
       direction): s*obj' strictly above s*bb */
    double s = cl->maximize ? 1.0 : -1.0;
    double allow = (cl->mip_gap + cl->dt_gap) * (1.0 + fabs(objnew) + fabs(cl->best_bound));
    if (s * objnew - s * cl->best_bound > 10.0 * allow) return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* family drivers                                                      */

/* LP OPTIMAL: random bounded LP; solve; claim (x,y,obj) */
static void fam_lp_optimal(int N)
{
    Fam *f = &F[0];
    for (int t = 0; t < N; t++) {
        int n = 2 + (int)(rnd() * 4), m = 1 + (int)(rnd() * 4);
        double c[8], A[8 * 8], b[8], lo[8], hi[8]; char rel[8];
        int maxi = (rnd() < 0.5);
        for (int j = 0; j < n; j++) { c[j] = rrange(-5, 5); lo[j] = rrange(-4, -1); hi[j] = rrange(1, 6); }
        /* one interior witness every row's rhs is built around: xw is a
           global feasible point with >=0.5 slack per row (used below to
           fabricate a FEASIBLE but suboptimal optimality claim) */
        double xw[8]; for (int j = 0; j < n; j++) xw[j] = rrange(lo[j], hi[j]);
        for (int i = 0; i < m; i++) {
            rel[i] = "<>"[rnd() < 0.5 ? 0 : 1];
            for (int j = 0; j < n; j++) A[(size_t)i * n + j] = rrange(-3, 3);
            double s = 0; for (int j = 0; j < n; j++) s += A[(size_t)i * n + j] * xw[j];
            b[i] = rel[i] == '<' ? s + rrange(0.5, 3) : s - rrange(0.5, 3);
        }
        LP lp; mk_lp(&lp, n, m, c, A, b, rel, lo, hi, maxi);
        Solver *s = solver_create(&lp);
        int r = solver_solve(s);
        if (r != 0) { solver_destroy(s); lp_free_local(&lp); continue; }
        double x[8], y[8], obj;
        solver_optimum(s, x, &obj);
        solver_duals(s, y);
        for (int i = 0; i < m; i++) y[i] *= (double)s->mlt[i];
        PsvCert cl; fill_claim(&cl, PSVK_LP_OPTIMAL, &lp);
        cl.x = x; cl.y = y; cl.obj = obj; lp_margins(&cl);
        legit(f, psv_cert_check(&cl));
        /* LARGE 1: push a var out of its box */
        { PsvCert c2 = cl; double xc[8]; memcpy(xc, x, sizeof(double) * n);
          int j = (int)(rnd() * n); xc[j] = cl.hi[j] + 1.0 + rnd(); c2.x = xc; shot_large_mode(f, &c2, 0); }
        /* LARGE 2: drift the claimed objective */
        { PsvCert c2 = cl; c2.obj = obj + (rnd() < 0.5 ? 1.0 : -1.0) * (1.0 + fabs(obj)) * 0.01; shot_large_mode(f, &c2, 1); }
        /* LARGE 3: FALSE OPTIMALITY - a feasible but suboptimal point
           claiming optimality (the half-way point toward the interior
           witness, with its own exact objective and the true duals).
           Every local check passes; only the dual bound B ~= s*opt
           exposing gap = B - s*cx' > 0 rejects it.  (The retired
           "garbage duals" shot was unsound as a test: clipped to zero
           they yield B = the box-sup of s*c, which is a VALID certificate
           whenever the optimum saturates that sup - the checker accepted
           those correctly.)  Skip when the witness happens to sit within
           claim-grace of optimal so the shot stays discriminating. */
        { double cxw = 0.0; for (int j = 0; j < n; j++) cxw += c[j] * xw[j];
          double sgn = maxi ? 1.0 : -1.0;
          double sub = sgn * (obj - cxw);          /* >= 0; >> 0 iff xw suboptimal */
          if (sub > 1e-2 * (1.0 + fabs(obj))) {
              PsvCert c2 = cl; double xp[8];
              for (int j = 0; j < n; j++) xp[j] = 0.5 * (x[j] + xw[j]);
              double cxp = 0.0; for (int j = 0; j < n; j++) cxp += c[j] * xp[j];
              c2.x = xp; c2.obj = cxp;              /* consistent, but suboptimal */
              shot_large_mode(f, &c2, 2);
          } }
        /* ULP: directed ulp noise on every payload scalar */
        { PsvCert c2 = cl; double xc[8], yc[8];
          for (int j = 0; j < n; j++) xc[j] = (rnd() < 0.5) ? nudge_down(x[j]) : nudge_up(x[j]);
          for (int i = 0; i < m; i++) yc[i] = (rnd() < 0.5) ? nudge_down(y[i]) : nudge_up(y[i]);
          c2.x = xc; c2.y = yc; shot_ulp(f, &c2); }
        solver_destroy(s); lp_free_local(&lp);
    }
}

/* LP INFEASIBLE: contradictory rows; solve; Farkas claim */
static void fam_lp_infeasible(int N)
{
    Fam *f = &F[1];
    for (int t = 0; t < N; t++) {
        int n = 2 + (int)(rnd() * 4), m = 2 + (int)(rnd() * 3);
        double c[8], A[8 * 8], b[8], lo[8], hi[8]; char rel[8];
        int maxi = (rnd() < 0.5);
        for (int j = 0; j < n; j++) { c[j] = rrange(-5, 5); lo[j] = rrange(-4, -1); hi[j] = rrange(1, 6); }
        for (int i = 0; i < m; i++)
            for (int j = 0; j < n; j++) A[(size_t)i * n + j] = rrange(-3, 3);
        /* contradiction: row 0 forces a^T x >= hi0, row 1 forces the same
           comb a^T x <= lo0 with a gap */
        double gap0 = rrange(1, 3);
        for (int j = 0; j < n; j++) { A[(size_t)0 * n + j] = rrange(0.5, 2); A[(size_t)1 * n + j] = A[(size_t)0 * n + j]; }
        double mn = 0; for (int j = 0; j < n; j++) mn += A[(size_t)0 * n + j] * (A[(size_t)0 * n + j] > 0 ? lo[j] : hi[j]);
        rel[0] = '>'; b[0] = mn + gap0;   /* a x >= min_ax + gap */
        rel[1] = '<'; b[1] = mn - gap0 / 2;
        if (m > 2) { /* filler satisfiable rows */
            for (int i = 2; i < m; i++) { rel[i] = '<'; b[i] = rrange(10, 20); }
        }
        LP lp; mk_lp(&lp, n, m, c, A, b, rel, lo, hi, maxi);
        Solver *s = solver_create(&lp);
        int r = solver_solve(s);
        if (r != 1 || !s->farkas_ok) { solver_destroy(s); lp_free_local(&lp); continue; }
        double y[8];
        if (solver_farkas_duals(s, y) != 0) { solver_destroy(s); lp_free_local(&lp); continue; }
        for (int i = 0; i < m; i++) y[i] *= (double)s->mlt[i];
        PsvCert cl; fill_claim(&cl, PSVK_LP_INFEASIBLE, &lp);
        cl.ray = y; cl.dt_gap = 1e-6;
        legit(f, psv_cert_check(&cl));
        /* LARGE 1: negate the ray */
        { PsvCert c2 = cl; double yn[8]; for (int i = 0; i < m; i++) yn[i] = -y[i]; c2.ray = yn; shot_large_mode(f, &c2, 0); }
        /* LARGE 2: permute the ray - counted only when the independent
           oracle proves the permuted ray is NO LONGER a valid Farkas
           certificate (a rotation-invariant ray stays a valid proof and
           acceptance is then the correct answer; first-observed on the
           symmetric two-row contradiction family: engine rays with equal
           entries are permutation-invariant) */
        { PsvCert c2 = cl; double yp[8]; for (int i = 0; i < m; i++) yp[i] = y[(i + 1) % m];
          c2.ray = yp;
          if (farkas_valid_oracle(&lp, yp, c2.dt_gap) == -1) shot_large_mode(f, &c2, 2); }
        /* ULP: ulp noise on every ray entry */
        { PsvCert c2 = cl; double yu[8]; for (int i = 0; i < m; i++) yu[i] = (rnd() < 0.5) ? nudge_down(y[i]) : nudge_up(y[i]); c2.ray = yu; shot_ulp(f, &c2); }
        solver_destroy(s); lp_free_local(&lp);
    }
}

/* LP UNBOUNDED: free ray + feasible point */
static void fam_lp_unbounded(int N)
{
    Fam *f = &F[2];
    for (int t = 0; t < N; t++) {
        int n = 2 + (int)(rnd() * 3), m = 1 + (int)(rnd() * 2);
        double c[8], A[8 * 8], b[8], lo[8], hi[8]; char rel[8];
        int maxi = (rnd() < 0.5);
        /* unbounded direction: var 0 drives the objective to +inf (max) or
           -inf (min); others bounded */
        for (int j = 0; j < n; j++) { lo[j] = rrange(-4, 0); hi[j] = rrange(1, 5); }
        hi[0] = maxi ? LP_INF : rrange(1, 5);
        lo[0] = maxi ? rrange(-4, 0) : -LP_INF;
        c[0] = 1.0; for (int j = 1; j < n; j++) c[j] = rrange(-2, 2);
        for (int i = 0; i < m; i++) {
            rel[i] = rnd() < 0.5 ? '<' : '>';
            for (int j = 0; j < n; j++) A[(size_t)i * n + j] = rrange(-2, 2);
            A[(size_t)i * n + 0] = 0.0;      /* keep the ray row-clean */
            double xr = 0.0; for (int j = 1; j < n; j++) xr += A[(size_t)i * n + j] * rrange(lo[j], hi[j]);
            b[i] = rel[i] == '<' ? xr + rrange(1, 3) : xr - rrange(1, 3);
        }
        LP lp; mk_lp(&lp, n, m, c, A, b, rel, lo, hi, maxi);
        Solver *s = solver_create(&lp);
        int r = solver_solve(s);
        if (r != 2) { solver_destroy(s); lp_free_local(&lp); continue; }
        double x0[8], d[8], obj = 0.0;
        solver_optimum(s, x0, &obj);
        if (solver_unbounded_ray(s, d) != 0) { solver_destroy(s); lp_free_local(&lp); continue; }
        PsvCert cl; fill_claim(&cl, PSVK_LP_UNBOUNDED, &lp);
        cl.x = x0; cl.ray = d; cl.obj = obj;
        cl.gt_box = 1e-6; cl.gt_row = 1e-5; cl.dt_dj = 1e-9;
        legit(f, psv_cert_check(&cl));
        /* LARGE 1: reversed ray */
        { PsvCert c2 = cl; double dn[8]; for (int j = 0; j < n; j++) dn[j] = -d[j]; c2.ray = dn; shot_large_mode(f, &c2, 0); }
        /* LARGE 2: walk toward a finite bound */
        { PsvCert c2 = cl; double dw[8]; memcpy(dw, d, sizeof(double) * n);
          int j = 1 + (int)(rnd() * (n - 1)); dw[j] = 3.0; c2.ray = dw; shot_large_mode(f, &c2, 2); }
        /* ULP: ulp noise on the ray + point */
        { PsvCert c2 = cl; double du[8], xu[8];
          for (int j = 0; j < n; j++) { du[j] = (rnd() < 0.5) ? nudge_down(d[j]) : nudge_up(d[j]);
                                        xu[j] = (rnd() < 0.5) ? nudge_down(x0[j]) : nudge_up(x0[j]); }
          c2.ray = du; c2.x = xu; shot_ulp(f, &c2); }
        solver_destroy(s); lp_free_local(&lp);
    }
}

/* MIP POINT: integer-feasible optimum; snapped ints are the zero-width
   surface where directed ulp noise IS decidable exactly */
static void fam_mip_point(int N)
{
    Fam *f = &F[3];
    for (int t = 0; t < N; t++) {
        int n = 2 + (int)(rnd() * 3), m = 1 + (int)(rnd() * 3);
        double c[8], A[8 * 8], b[8], lo[8], hi[8]; char rel[8];
        int maxi = (rnd() < 0.5);
        for (int j = 0; j < n; j++) { c[j] = rrange(-6, 6); lo[j] = 0; hi[j] = 3 + (int)(rnd() * 4); }
        for (int i = 0; i < m; i++) {
            rel[i] = rnd() < 0.7 ? '<' : '>';
            for (int j = 0; j < n; j++) A[(size_t)i * n + j] = rrange(0.5, 3) * (rnd() < 0.8 ? 1 : -1);
            double mn = 0, mx = 0;
            for (int j = 0; j < n; j++) {
                double a = A[(size_t)i * n + j];
                mn += a > 0 ? a * lo[j] : a * hi[j];
                mx += a > 0 ? a * hi[j] : a * lo[j];
            }
            b[i] = rel[i] == '<' ? mx - rrange(0.3, 0.7) * (mx - mn) : mn + rrange(0.0, 0.3) * (mx - mn);
        }
        LP lp; mk_lp(&lp, n, m, c, A, b, rel, lo, hi, maxi);
        unsigned char isint[8]; memset(isint, 1, n);
        MIP mip; memset(&mip, 0, sizeof(mip));
        mip.n = n; mip.m = m; mip.c = lp.c; mip.Acolptr = lp.Acolptr; mip.Arow = lp.Arow; mip.Aval = lp.Aval;
        mip.rel = lp.rel; mip.b = lp.b; mip.l = lp.l; mip.u = lp.u; mip.maximize = maxi;
        mip.isint = isint; mip.mip_gap = 1e-4; mip.node_limit = 50000; mip.lp_iter_limit = 500000;
        MIPResult res; mip_solve(&mip, &res);
        if (res.status != 0) { mip_result_free(&res); lp_free_local(&lp); continue; }
        PsvCert cl; fill_claim(&cl, PSVK_MIP_POINT, &lp);
        cl.x = res.x; cl.isint = isint; cl.obj = res.obj;
        cl.proven_optimal = res.proven_optimal; cl.best_bound = res.best_bound; cl.mip_gap = mip.mip_gap;
        cl.gt_box = 1e-6; cl.gt_row = 1e-6; cl.dt_obj = 1e-6; cl.dt_gap = 1e-6;
        legit(f, psv_cert_check(&cl));
        /* LARGE 1: integer off-by-one (corrupts rows/optimality claim) -
           counted only when the independent oracle proves some checked
           surface violated: a lattice neighbor that stays FEASIBLE with a
           directionally-coherent (worse) objective is evidence-equivalent
           for the point+stamp class, and accepting it is correct */
        { PsvCert c2 = cl; double xc[8]; memcpy(xc, res.x, sizeof(double) * n);
          int j = (int)(rnd() * n); xc[j] += (xc[j] + 1 <= cl.hi[j]) ? 1.0 : -1.0;
          c2.obj = 0; for (int k = 0; k < n; k++) c2.obj += c[k] * xc[k];
          if (mip_offbyone_adversarial(&c2, xc, c2.obj)) shot_large_mode(f, &c2, 0); }
        /* LARGE 2: objective drift against the same point */
        { PsvCert c2 = cl; c2.obj = res.obj + 2.0; shot_large_mode(f, &c2, 1); }
        /* ULP: snap-corrupt every integer component by one ulp - the
           zero-width surface must catch every single case */
        { PsvCert c2 = cl; double xu[8];
          for (int j = 0; j < n; j++) xu[j] = (rnd() < 0.5) ? nudge_down(res.x[j]) : nudge_up(res.x[j]);
          c2.x = xu; shot_ulp(f, &c2); }
        mip_result_free(&res); lp_free_local(&lp);
    }
}

/* QP OPTIMAL: PSD Q = R^T R + diag, bounded optimum, verify KKT claim */
static void fam_qp_optimal(int N)
{
    Fam *f = &F[4];
    for (int t = 0; t < N; t++) {
        int n = 2 + (int)(rnd() * 3), m = 2 + (int)(rnd() * 3);
        double Q[8 * 8], A[8 * 8], bq[8], cq[8];
        double Rm[8 * 8];
        for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) Rm[(size_t)i * n + j] = rrange(-1.5, 1.5);
        for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) {
            double s = (i == j ? rrange(0.2, 1.5) : 0.0);
            for (int k = 0; k < n; k++) s += Rm[(size_t)k * n + i] * Rm[(size_t)k * n + j];
            Q[(size_t)j * n + i] = s;   /* column-major */
        }
        for (int j = 0; j < n; j++) cq[j] = rrange(-3, 3);
        /* rows with slack around a chosen interior point (x in [-2,2]) */
        double xi[8]; for (int j = 0; j < n; j++) xi[j] = rrange(-2, 2);
        for (int i = 0; i < m; i++) {
            for (int j = 0; j < n; j++) A[(size_t)i * n + j] = rrange(-2, 2);
            double s = 0; for (int j = 0; j < n; j++) s += A[(size_t)i * n + j] * xi[j];
            bq[i] = s + rrange(1, 4);   /* A x <= b with slack */
        }
        QP qp; qp.n = n; qp.m = m; qp.Q = Q; qp.c = cq; qp.A = A; qp.b = bq; qp.x0 = NULL;
        QPResult r; memset(&r, 0, sizeof(r));
        qp_solve(&qp, &r);
        if (r.status != 0) { qp_result_free(&r); continue; }
        PsvCert cl; memset(&cl, 0, sizeof(cl));
        cl.kind = PSVK_QP_OPTIMAL;
        cl.n = n; cl.m = m; cl.Q = Q; cl.A = A; cl.bq = bq; cl.cq = cq;
        cl.x = r.x; cl.mu = r.mult; cl.obj = r.obj;
        cl.gt_row = 1e-7; cl.dt_dj = 1e-7; cl.dt_obj = 1e-6;
        legit(f, psv_cert_check(&cl));
        /* LARGE 1: offset the point */
        { PsvCert c2 = cl; double xw[8]; memcpy(xw, r.x, sizeof(double) * n);
          int j = (int)(rnd() * n); xw[j] += 0.5; c2.x = xw; shot_large_mode(f, &c2, 0); }
        /* LARGE 2: wrong multipliers */
        { PsvCert c2 = cl; double mw[8]; for (int i = 0; i < m; i++) mw[i] = r.mult[i] + rrange(-2, 2); c2.mu = mw; shot_large_mode(f, &c2, 1); }
        /* ULP: ulp noise on point and multipliers */
        { PsvCert c2 = cl; double xu[8], mu2[8];
          for (int j = 0; j < n; j++) xu[j] = (rnd() < 0.5) ? nudge_down(r.x[j]) : nudge_up(r.x[j]);
          for (int i = 0; i < m; i++) mu2[i] = (rnd() < 0.5) ? nudge_down(r.mult[i]) : nudge_up(r.mult[i]);
          c2.x = xu; c2.mu = mu2; shot_ulp(f, &c2); }
        qp_result_free(&r);
    }
}

/* QP UNBOUNDED: Q = 0 with a free descent direction */
static void fam_qp_unbounded(int N)
{
    Fam *f = &F[5];
    for (int t = 0; t < N; t++) {
        int n = 2 + (int)(rnd() * 3), m = 1 + (int)(rnd() * 3);
        double Q[8 * 8], A[8 * 8], bq[8], cq[8];
        memset(Q, 0, sizeof(Q));
        int dir = (int)(rnd() * n);
        for (int j = 0; j < n; j++) cq[j] = 0.0;
        cq[dir] = -1.0;                 /* descends along +e_dir */
        double xi[8]; for (int j = 0; j < n; j++) xi[j] = rrange(-1, 1);
        for (int i = 0; i < m; i++) {
            for (int j = 0; j < n; j++) A[(size_t)i * n + j] = rrange(-2, 2);
            A[(size_t)i * n + dir] = (rnd() < 0.5) ? A[(size_t)i * n + dir] : 0.0;
            double s = 0; for (int j = 0; j < n; j++) s += A[(size_t)i * n + j] * xi[j];
            bq[i] = s + rrange(1, 4);
            /* keep the descent row-clean so the ray argument is exact */
            A[(size_t)i * n + dir] = 0.0;
        }
        /* feasible start: interior point we built rows around */
        QP qp; qp.n = n; qp.m = m; qp.Q = Q; qp.c = cq; qp.A = A; qp.b = bq;
        qp.x0 = xi;
        QPResult r; memset(&r, 0, sizeof(r));
        qp_solve(&qp, &r);
        if (r.status != 1 || !r.x) { qp_result_free(&r); continue; }
        PsvCert cl; memset(&cl, 0, sizeof(cl));
        cl.kind = PSVK_QP_UNBOUNDED;
        cl.n = n; cl.m = m; cl.Q = Q; cl.A = A; cl.bq = bq; cl.cq = cq;
        cl.x = r.x; cl.ray = r.ray;
        cl.gt_row = 1e-7; cl.dt_dj = 1e-9;
        legit(f, psv_cert_check(&cl));
        /* LARGE 1: reverse the ray */
        { PsvCert c2 = cl; double dn[8]; for (int j = 0; j < n; j++) dn[j] = -r.ray[j]; c2.ray = dn; shot_large_mode(f, &c2, 0); }
        /* LARGE 2: infeasible point claim - offset along a row NORMAL so a
           violation is guaranteed.  (A raw component push was unsound as a
           test: rows are exact in the descent direction, so pushing x_dir
           kept the point feasible and the UNBOUNDED claim genuinely true -
           the checker accepted those correctly.) */
        { int i0 = -1; double amax = 0.0;
          for (int i = 0; i < m; i++) {
              double nrm2 = 0.0;
              for (int j = 0; j < n; j++) nrm2 += A[(size_t)i * n + j] * A[(size_t)i * n + j];
              if (nrm2 > amax) { amax = nrm2; i0 = i; }
          }
          if (i0 >= 0 && amax > 1e-18) {
              double K = 10.0 / amax;   /* row i0 residual rises by K*amax = 10 > any slack (< 4) */
              PsvCert c2 = cl; double xw[8];
              for (int j = 0; j < n; j++) xw[j] = r.x[j] + K * A[(size_t)i0 * n + j];
              c2.x = xw; shot_large_mode(f, &c2, 2);
          } }
        /* ULP: ulp noise on ray and point */
        { PsvCert c2 = cl; double du[8], xu[8];
          for (int j = 0; j < n; j++) { du[j] = (rnd() < 0.5) ? nudge_down(r.ray[j]) : nudge_up(r.ray[j]);
                                        xu[j] = (rnd() < 0.5) ? nudge_down(r.x[j]) : nudge_up(r.x[j]); }
          c2.ray = du; c2.x = xu; shot_ulp(f, &c2); }
        qp_result_free(&r);
    }
}

/* EXHAUSTION stamps: discrete, no engine run */
static void fam_exhaustion(int N)
{
    Fam *f = &F[6];
    for (int t = 0; t < N; t++) {
        PsvCert cl; memset(&cl, 0, sizeof(cl));
        cl.kind = PSVK_EXHAUSTION;
        cl.nodes = 1 + (long)(rnd() * 90000);
        cl.node_limit = (rnd() < 0.7) ? 200000 : 0;
        cl.stopped = 0;
        legit(f, psv_cert_check(&cl));
        { PsvCert c2 = cl; c2.nodes = 200000; c2.node_limit = 200000; shot_large_mode(f, &c2, 0); }
        { PsvCert c2 = cl; c2.stopped = 1; shot_large_mode(f, &c2, 1); }
    }
}

/* STATUS-FLIP lane: OPTIMAL claimed for a truly infeasible system (any x
   must fail primal) and INFEASIBLE claimed with a bogus ray for a truly
   feasible system (Farkas-lemma soundness: no separating ray exists) */
static void fam_status_flip(int N)
{
    Fam *f = &F[7];
    for (int t = 0; t < N; t++) {
        int n = 2 + (int)(rnd() * 4), m = 2 + (int)(rnd() * 3);
        double c[8], A[8 * 8], b[8], lo[8], hi[8]; char rel[8];
        for (int j = 0; j < n; j++) { c[j] = rrange(-5, 5); lo[j] = rrange(-4, -1); hi[j] = rrange(1, 6); }
        for (int i = 0; i < m; i++)
            for (int j = 0; j < n; j++) A[(size_t)i * n + j] = rrange(-3, 3);
        double gap0 = rrange(1, 3);
        for (int j = 0; j < n; j++) { A[(size_t)0 * n + j] = rrange(0.5, 2); A[(size_t)1 * n + j] = A[(size_t)0 * n + j]; }
        double mn = 0; for (int j = 0; j < n; j++) mn += A[(size_t)0 * n + j] * (A[(size_t)0 * n + j] > 0 ? lo[j] : hi[j]);
        rel[0] = '>'; b[0] = mn + gap0; rel[1] = '<'; b[1] = mn - gap0 / 2;
        for (int i = 2; i < m; i++) { rel[i] = '<'; b[i] = rrange(10, 20); }
        LP lpinf; mk_lp(&lpinf, n, m, c, A, b, rel, lo, hi, 1);
        /* flip 1: OPTIMAL claim on an infeasible system, garbage point */
        double xg[8], yg[8];
        for (int j = 0; j < n; j++) xg[j] = rrange(lo[j], hi[j]);
        for (int i = 0; i < m; i++) yg[i] = rrange(-2, 2);
        { PsvCert cl; fill_claim(&cl, PSVK_LP_OPTIMAL, &lpinf);
          cl.x = xg; cl.y = yg; cl.obj = rrange(-10, 10); lp_margins(&cl);
          shot_large(f, &cl); }
        /* flip 2: INFEASIBLE claim with a random ray on a FEASIBLE system
           (make row 1 slack again) */
        b[1] = rrange(10, 20);
        LP lpok; mk_lp(&lpok, n, m, c, A, b, rel, lo, hi, 1);
        for (int i = 0; i < m; i++) yg[i] = rrange(-2, 2);
        { PsvCert cl; fill_claim(&cl, PSVK_LP_INFEASIBLE, &lpok);
          cl.ray = yg; cl.dt_gap = 1e-6; shot_large(f, &cl); }
        lp_free_local(&lpinf); lp_free_local(&lpok);
    }
}

int main(int argc, char **argv)
{
    int N = (argc > 1) ? atoi(argv[1]) : 200;
    unsigned long long seed = (argc > 2) ? strtoull(argv[2], NULL, 10) : 20260818ull;
    RS = seed ? seed : 1;
    fam_lp_optimal(N);
    fam_lp_infeasible(N);
    fam_lp_unbounded(N);
    fam_mip_point(N);
    fam_qp_optimal(N);
    fam_qp_unbounded(N);
    fam_exhaustion(N);
    fam_status_flip(N);
    int fail = 0;
    printf("%-14s %10s %10s %12s %14s %12s %12s\n",
           "family", "legit_ok", "legit_no", "large_rej", "ulp_rej/tot", "(rate)", "");
    for (int k = 0; k < 8; k++) {
        long tot = F[k].legit_ok + F[k].legit_no;
        printf("%-14s %10ld %10ld %9ld/%-3ld %12ld/%-4ld",
               FAMNAME[k], F[k].legit_ok, F[k].legit_no,
               F[k].large_rej, F[k].large_tot, F[k].ulp_rej, F[k].ulp_tot);
        if (k == 7) printf("  (ulp n/a)");
        else if (F[k].ulp_tot) printf("  (%5.1f%%)", 100.0 * F[k].ulp_rej / F[k].ulp_tot);
        printf("\n");
        (void)tot;
        /* LEGIT acceptance must be 100% - any false rejection is a cert bug */
        if (F[k].legit_no > 0) { fail = 1; printf("  !! LEGIT false-rejections in %s\n", FAMNAME[k]); }
        /* LARGE corruption + status flips must be rejected 100% */
        if (F[k].large_tot > 0 && F[k].large_rej != F[k].large_tot) { fail = 1; printf("  !! LARGE misses in %s  per-mode: %ld/%ld %ld/%ld %ld/%ld\n", FAMNAME[k], F[k].lr[0], F[k].lt[0], F[k].lr[1], F[k].lt[1], F[k].lr[2], F[k].lt[2]); }
        /* legit coverage must actually have run */
        if (F[k].legit_ok == 0 && k != 7) { fail = 1; printf("  !! no legit instances produced in %s\n", FAMNAME[k]); }
    }
    /* zero-width surfaced hard-asserted: MIP snapped-integrality ulp */
    if (F[3].ulp_tot > 0 && F[3].ulp_rej != F[3].ulp_tot) {
        fail = 1; printf("  !! MIP snapped-int ulp surface leaked\n");
    }
    if (!fail) printf("cert_inject: LEGIT 100%% accepted, LARGE 100%% rejected, MIP-ulp 100%% rejected (see rates above)\n");
    return fail;
}
