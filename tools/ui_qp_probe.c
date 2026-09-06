/* ui_qp_probe.c -- the UI-layout QP problem class: verdict robustness + capacity.
 *
 * This is the problem class that a constraint-layout front end (curv-ps'
 * `solve { }` blocks: https://github.com/SodoMita/curv-ps) feeds to the QP
 * core: hundreds of pixel-scaled variables, hard equalities often doubled
 * into inequality pairs, soft constraints either folded into Q or expressed
 * with one non-negative error variable each, and a solution that is re-solved
 * every frame from the previous frame's answer.
 *
 * Three questions, all answerable without a browser:
 *
 *   1. ENCODINGS   the same convex QP expressed three ways must give three
 *                  equal verdicts, and a feasible problem must never come back
 *                  INFEASIBLE -- proven by handing the solver a feasible point
 *                  we wrote down by construction.
 *   2. SCALE       the verdict may not depend on whether the model is written
 *                  in pixels, nanometres or light-years.
 *   3. CAPACITY    cold vs warm (x0) per-frame cost, and where the dense
 *                  active-set cost curve crosses an interactive frame budget.
 *
 * Run:  make ui-probe && ./ui_qp_probe [--strict] [--fast]   (--fast = ~30 s)
 * Exit: 0 = all checks passed; 1 = a check failed (with --strict; without it,
 * failures are reported and the exit status is still 0, so the tool can be
 * wired into the battery before the underlying issues are fixed).
 *
 * Known-failing-by-design today (documented in docs/CURV_PS_PLAN.md): the
 * error-variable encoding reports INFEASIBLE for feasible models at N >= 16,
 * and the verdict flips with the unit scale.  Both trace back to the dense
 * Phase-I feasibility search (src/qp.c find_feasible), not to the model.
 */
#include "qp.h"
#include "err.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdarg.h>

static double g_deadline = 1e18;
static double now(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + 1e-9 * ts.tv_nsec;
}
static int stop_cb(void) { return now() > g_deadline; }

static int g_proven;            /* set by run_qp(): -1 came with a Farkas proof */
static const char *statname(int s)
{
    switch (s) {
    case 0:                  return "OPTIMAL";
    case -1:                 return g_proven ? "INFEASIBLE" : "NO_START";
    case 1:                  return "UNBOUNDED";
    case QP_ITERATION_LIMIT: return "ITER_LIMIT";
    case QP_KKT_FAIL:        return "KKT_FAIL";
    case QP_STOPPED:         return "STOPPED";
    default:                 return "INVALID";
    }
}

/* ------------------------------------------------------------------ model --
 * A 1-D flow of N chips: positions x_i and widths w_i.
 *
 *   hard   x_0 = 0                     (a pair of inequalities)
 *   hard   x_{i+1} = x_i + w_i + gap   (a pair of inequalities)
 *   hard   w_i >= minw                 (one inequality)
 *   hard   x_{N-1} + w_{N-1} <= maxw   (one inequality)
 *   soft   w_i ~= nat                  (weight W)
 *
 * `enc` picks the encoding of the soft part, `dup` doubles every hard
 * equality pair (the shape a UI front end produces when it lowers `a == b`
 * into two inequalities and separately softens the same bound), `S` scales the
 * whole model (units), and `x1out` can hand back a feasible point.
 *
 * The model is feasible FOR EVERY N, S and dup: x_i = i*(nat+gap)*S, w_i =
 * nat*S satisfies all rows strictly (maxw has 10 % slack).
 */
#define ENC_Q     0   /* soft equality folded into Q:  W*(w-nat)^2, no rows      */
#define ENC_ERR   1   /* soft equality via a non-negative error var e: W*e^2     */
#define ENC_PAIR  2   /* soft equality as two hard rows, no objective term       */

typedef struct {
    int n, m;
    double *Q, *c, *A, *b;
    double *x1;                 /* a feasible point, if requested */
} Model;

static void model_free(Model *M)
{
    free(M->Q); free(M->c); free(M->A); free(M->b); free(M->x1);
    memset(M, 0, sizeof *M);
}

static Model model_build(int N, int enc, int dup, double S, int want_x1, int tight)
{
    double gap = 6.0*S, minw = 10.0*S, nat = 40.0*S, W = 4.0;
    double total = (N*(nat + gap) - gap);
    double maxw = 1.10 * total;
    /* tight=1: the container cannot hold the minimum widths at all
       (sum_i minw + (N-1) gap > maxw), so the row system has NO solution.
       Section [5] uses that to require a PROOF, not merely a refusal. */
    if (tight) maxw = 0.5 * N * minw;
    int nv = 2 * N;
    int n = nv + (enc == ENC_ERR ? N : 0);
    int base = 2 + 2*(N-1) + N + 1;                    /* hard rows */
    int m  = base * (dup ? 2 : 1) + (enc == ENC_ERR ? 3*N : 0)
                                 + (enc == ENC_PAIR ? 2*N : 0);
    Model M; memset(&M, 0, sizeof M);
    M.n = n; M.m = m;
    M.Q = calloc((size_t)n*n, sizeof(double));
    M.c = calloc((size_t)n,   sizeof(double));
    M.A = calloc((size_t)m*n, sizeof(double));
    M.b = calloc((size_t)m,   sizeof(double));
    if (want_x1) M.x1 = calloc((size_t)n, sizeof(double));
    double *Ar; int r = 0;

    for (int rep = 0; rep < (dup ? 2 : 1); rep++) {
        Ar = M.A + (size_t)r*n; Ar[0] =  1; M.b[r++] = 0;
        Ar = M.A + (size_t)r*n; Ar[0] = -1; M.b[r++] = 0;
        for (int i = 0; i + 1 < N; i++) {
            Ar = M.A + (size_t)r*n; Ar[i+1] =  1; Ar[i] = -1; Ar[N+i] = -1; M.b[r++] =  gap;
            Ar = M.A + (size_t)r*n; Ar[i+1] = -1; Ar[i] =  1; Ar[N+i] =  1; M.b[r++] = -gap;
        }
    }
    for (int i = 0; i < N; i++) { Ar = M.A + (size_t)r*n; Ar[N+i] = -1; M.b[r++] = -minw; }
    Ar = M.A + (size_t)r*n; Ar[N-1] = 1; Ar[2*N-1] = 1; M.b[r++] = maxw;

    for (int i = 0; i < N; i++) {
        if (enc == ENC_Q) {                      /* W*(w-nat)^2 */
            M.Q[(size_t)(N+i)*n + (N+i)] += 2*W;
            M.c[N+i] += -2*W*nat;
        } else if (enc == ENC_ERR) {
            int e = nv + i;
            Ar = M.A + (size_t)r*n; Ar[N+i] =  1; Ar[e] = -1; M.b[r++] =  nat;
            Ar = M.A + (size_t)r*n; Ar[N+i] = -1; Ar[e] = -1; M.b[r++] = -nat;
            Ar = M.A + (size_t)r*n; Ar[e] = -1;               M.b[r++] =  0;
            M.Q[(size_t)e*n + e] += 2*W;
        } else {                                  /* ENC_PAIR: no penalty at all */
            Ar = M.A + (size_t)r*n; Ar[N+i] =  1; M.b[r++] =  nat;
            Ar = M.A + (size_t)r*n; Ar[N+i] = -1; M.b[r++] = -nat;
        }
        /* the tiny ridge a layout front end adds to keep under-determined
           positions unique (curv-ps does exactly this) */
        M.Q[(size_t)i*n + i]         += 1e-7*S*S;
        M.Q[(size_t)(N+i)*n + (N+i)] += 1e-7*S*S;
    }
    M.m = r;
    (void)tight;
    if (M.x1) {
        double x = 0;
        for (int i = 0; i < N; i++) { M.x1[i] = x; M.x1[N+i] = nat; x += nat + gap; }
        if (enc == ENC_ERR)
            for (int i = 0; i < N; i++) M.x1[nv + i] = 0;   /* error vars at 0 */
    }
    return M;
}

static int solve_full(const Model *M, const double *x0, double *obj, double *tout, int *itout,
                      int *proven, double *lam_out)
{
    if (proven) *proven = 0;
    QP qp; memset(&qp, 0, sizeof qp);
    qp.n = M->n; qp.m = M->m; qp.Q = M->Q; qp.c = M->c; qp.A = M->A; qp.b = M->b; qp.x0 = x0;
    QPResult res; memset(&res, 0, sizeof res);
    double t0 = now();
    qp_solve(&qp, &res);
    if (tout)  *tout  = 1e3*(now() - t0);
    if (obj)   *obj   = res.obj;
    if (itout) *itout = res.iterations;
    int st = res.status;
    if (proven) *proven = res.infeasible_proven;
    if (lam_out && res.farkas) {
        double lmax = 0.0;
        for (int i = 0; i < M->m; i++) lmax = fmax(lmax, fabs(res.farkas[i]));
        for (int i = 0; i < M->m; i++) lam_out[i] = lmax > 0 ? res.farkas[i]/lmax : 0.0;
    } else if (lam_out) {
        for (int i = 0; i < M->m; i++) lam_out[i] = -1.0;
    }
    qp_result_free(&res);
    return st;
}

static int solve(const Model *M, const double *x0, double *obj, double *tout, int *itout)
{
    g_proven = 0;
    QP qp; memset(&qp, 0, sizeof qp);
    qp.n = M->n; qp.m = M->m; qp.Q = M->Q; qp.c = M->c; qp.A = M->A; qp.b = M->b; qp.x0 = x0;
    QPResult res; memset(&res, 0, sizeof res);
    double t0 = now();
    qp_solve(&qp, &res);
    if (tout)  *tout  = 1e3*(now() - t0);
    if (obj)   *obj   = res.obj;
    if (itout) *itout = res.iterations;
    int st = res.status;
    if (st == -1 && res.infeasible_proven && res.farkas) {
        /* verify the certificate independently: lambda >= 0, A^T lambda ~ 0,
           b^T lambda < 0.  A proof that does not verify here is a wrong proof. */
        g_proven = 1;
        double amax = 0.0, bt = 0.0, bmag = 0.0; int neg = 0;
        for (int i = 0; i < M->m; i++) amax = fmax(amax, fabs(res.farkas[i]));
        if (amax > 0.0) {
            double colmax = 0.0;
            for (int i = 0; i < M->m; i++) {
                double l = res.farkas[i] / amax;
                if (l < -1e-9) neg = 1;
                bt += l * M->b[i]; bmag += fabs(l * M->b[i]);
            }
            for (int j = 0; j < M->n; j++) {
                double sv = 0.0, mag = 0.0;
                for (int i = 0; i < M->m; i++) {
                    double a = M->A[(size_t)i*M->n + j], l = res.farkas[i] / amax;
                    sv += a * l; mag += fabs(a) * l;
                }
                colmax = fmax(colmax, fabs(sv) / (1.0 + mag));
            }
            if (neg || colmax > 1e-6 || !(bt < -1e-9 * (1.0 + bmag)))
                printf("  FAIL Farkas certificate does not verify (A^T l=%.3g rel, b^T l=%.3g, neg=%d)\n",
                       colmax, bt, neg);
            else g_proven = 2;      /* verified proof */
        }
    }
    qp_result_free(&res);
    return st;
}

/* --------------------------------------------------------------- sections -- */
static int fails = 0, strict = 0, fast = 0, only = 0, nmax = 0;
static double g_dead_ms = 5000.0;
static void check(int cond, const char *fmt, ...)
{
    if (cond) return;
    va_list ap; va_start(ap, fmt);
    printf("  FAIL "); vprintf(fmt, ap); printf("\n");
    va_end(ap);
    fails++;
}

static void sec_encodings(void)
{
    printf("\n[1] encoding equivalence -- same QP, three encodings, one verdict\n");
    printf("%5s %10s %10s %10s   %14s %14s %14s\n",
           "N", "Q-folded", "error-var", "eq-pairs", "obj(Q)", "obj(err)", "obj(pairs)");
    int hi = nmax ? nmax : (fast ? 12 : 40);
    for (int N = 4; N <= hi; N += 4) {
        int st[3]; double ob[3] = {NAN, NAN, NAN};
        for (int e = 0; e < 3; e++) {
            Model M = model_build(N, e, 0, 1.0, 0, 0);
            st[e] = solve(&M, NULL, &ob[e], NULL, NULL);
            model_free(&M);
        }
            printf("%5d %10s %10s %10s   %14.6f %14.6f %14.6f\n",
               N, statname(st[0]), statname(st[1]), statname(st[2]), ob[0], ob[1], ob[2]);
        for (int e = 0; e < 3; e++) {
            check(st[e] == 0, "N=%d encoding %d: model is feasible by construction but solver said %s",
                  N, e, statname(st[e]));
            check(st[e] != -1 || g_proven != 2,
                  "N=%d encoding %d: claimed INFEASIBLE WITH PROOF for a feasible model -- the certificate verified, so the model build is wrong or the verifier is",
                  N, e);
        }
        /* a hand-written feasible point must undo any 'INFEASIBLE' */
        for (int e = 0; e < 3; e++) if (st[e] != 0) {
            Model M = model_build(N, e, 0, 1.0, 1, 0);
            int st2 = solve(&M, M.x1, NULL, NULL, NULL);
            check(st2 == 0, "  -> N=%d encoding %d still not OPTIMAL from the explicit feasible point (%s)",
                  N, e, statname(st2));
            model_free(&M);
        }
    }
}

static void sec_scale(void)
{
    printf("\n[2] unit-scale invariance -- verdict must not depend on the unit\n");
    double scales[] = { 1e-6, 1e-3, 1.0, 1e3, 1e6 };
    printf("%5s %5s", "N", "dup");
    for (unsigned k = 0; k < sizeof scales/sizeof *scales; k++) printf(" %10s", "scale");
    printf("\n");
    int hi2 = nmax ? nmax : (fast ? 16 : 32);
    for (int N = 8; N <= hi2; N *= 2)
    for (int dup = 0; dup <= (fast ? 0 : 1); dup++)
    for (int e = 0; e < 2; e++) {
        printf("%5d %5d", N, dup);
        for (unsigned k = 0; k < sizeof scales/sizeof *scales; k++) {
            Model M = model_build(N, e, dup, scales[k], 0, 0);
            g_deadline = now() + g_dead_ms/1e3;           /* a stop is a failure too */
            double t; int st = solve(&M, NULL, NULL, &t, NULL);
            printf(" %10s", st == 0 ? "ok" : statname(st));
            check(st == 0, "\n  N=%d dup=%d enc=%d at scale %.0e: %s (was %.0f ms; problem is feasible)",
                  N, dup, e, scales[k], statname(st), t);
            model_free(&M);
        }
        printf("\n");
        g_deadline = 1e18;
    }
}

static void sec_proof(void)
{
    printf("\n[5] infeasible model -> PROVEN, with a certificate that checks out\n");
    printf("%5s %10s %8s %10s %12s %12s\n", "N", "enc", "status", "proven", "rows in IIS", "max A^T lam");
    for (int N = 4; N <= 24; N += 4)
    for (int enc = 0; enc <= 1; enc++) {
        Model M = model_build(N, enc, 0, 1.0, 0, 1);
        double *lam = calloc((size_t)M.m, sizeof(double));
        int prov = 0; double t; int it;
        int st = solve_full(&M, NULL, NULL, &t, &it, &prov, lam);
        double colmax = 0.0; int rowsel = 0;
        for (int j = 0; j < M.n; j++) {
            double sv = 0.0, mag = 0.0;
            for (int i = 0; i < M.m; i++) { double a = M.A[(size_t)i*M.n+j]; sv += a*lam[i]; mag += fabs(a)*lam[i]; }
            colmax = fmax(colmax, fabs(sv)/(1.0+mag));
        }
        for (int i = 0; i < M.m; i++) if (lam[i] > 1e-8) rowsel++;
        char cm[32];
        if (lam[0] > -0.5) snprintf(cm, sizeof cm, "%.3g", colmax); else strcpy(cm, "(none)");
        printf("%5d %10s %8s %10s %12d %12s\n", N, enc ? "error-var" : "Q-folded",
               statname(st), prov ? "YES" : "no", rowsel, cm);
        check(st == -1 && prov == 1, "N=%d enc=%d: an infeasible model must return -1 WITH a proof (got %s, proven=%d)",
              N, enc, statname(st), prov);
        check(lam[0] > -0.5 && colmax <= 1e-6, "N=%d enc=%d: certificate missing or A^T lambda not ~0 (%.3g)", N, enc, colmax);
        /* The certificate must name rows the conflict actually needs: every
           min-width row and the container row.  (An IIS must contain them: drop
           any one min-width row and that item can shrink to satisfy the
           container.  Chain rows legitimately carry weight too -- they are how
           the per-item widths add up to the container total.) */
        if (lam[0] > -0.5) {
            int firstmin = 2 + 2*(N-1);                  /* index of the first  -w_i <= -minw  row */
            int container = firstmin + N;
            check(lam[container] > 1e-8, "N=%d enc=%d: container row must carry weight in the certificate", N, enc);
            for (int i = 0; i < N; i++)
                check(lam[firstmin+i] > 1e-8, "N=%d enc=%d: min-width row %d must carry weight in the certificate", N, enc, i);
            check(rowsel >= 2*N, "N=%d enc=%d: certificate names only %d rows; a conflict here needs at least the %d min-width + container rows",
                  N, enc, rowsel, N + 1);
        }
        free(lam); model_free(&M);
    }
}

static void sec_frames(void)
{
    printf("\n[3] per-frame cost: cold start vs warm start (x0 = last frame's answer)\n");
    printf("%5s %6s %6s %12s %12s %10s %12s\n",
           "N", "vars", "rows", "cold ms/f", "warm ms/f", "iters", "max|dx| px");
    int hi3 = nmax ? nmax : (fast ? 32 : 64);
    for (int N = 8; N <= hi3; N *= 2) {
        int FR = fast ? 4 : 10;
        double cold = 0, warm = 0; int coldit = 0, warmit = 0, md = 0;
        double maxdx = 0;
        for (int f = 0; f < FR; f++) {
            Model M = model_build(N, ENC_Q, 0, 1.0, 0, 0);
            M.b[M.m-1] += 4.0*f;                       /* the container grows each frame */
            double t1; int st1 = solve(&M, NULL, NULL, &t1, &coldit);
            cold += t1;
            /* warm: same model, started from the previous frame's solution */
            Model P = model_build(N, ENC_Q, 0, 1.0, 0, 0);
            double px[512]; memset(px, 0, sizeof px);
            QP qp; memset(&qp, 0, sizeof qp);
            qp.n = P.n; qp.m = P.m; qp.Q = P.Q; qp.c = P.c; qp.A = P.A; qp.b = P.b;
            QPResult r; memset(&r, 0, sizeof r);
            qp_solve(&qp, &r);                         /* cold, to get a start point */
            if (r.x && P.n <= 512) { memcpy(px, r.x, sizeof(double)*(size_t)P.n); md = 1; }
            qp_result_free(&r);
            double t2 = 0;
            if (md) {
                QPResult r2; memset(&r2, 0, sizeof r2);
                qp.x0 = px;
                double tt = now(); qp_solve(&qp, &r2);
                t2 = 1e3*(now()-tt); warmit = r2.iterations;
                if (r2.x) {
                    /* compare warm vs cold answer for THIS frame (up to cold's own error) */
                    QPResult r3; memset(&r3, 0, sizeof r3);
                    qp.x0 = NULL; qp_solve(&qp, &r3);
                    if (r3.x) for (int i = 0; i < P.n; i++)
                        maxdx = fmax(maxdx, fabs(r3.x[i] - r2.x[i]));
                    qp_result_free(&r3);
                }
                qp_result_free(&r2);
            }
            warm += t2;
            if (st1) check(0, "cold solve at N=%d frame %d returned %s", N, f, statname(st1));
            model_free(&M); model_free(&P);
        }
        printf("%5d %6d %6d %12.2f %12.2f %10d %12.3g\n",
               N, 2*N, 2 + 2*(N-1) + N + 1, cold/FR, warm/FR, warmit, maxdx);
    }
}

static void sec_capacity(void)
{
    printf("\n[4] capacity curve (cold, one solve) vs a 16 ms frame budget\n");
    printf("%5s %6s %6s %8s %10s %12s %14s\n",
           "N", "vars", "rows", "iters", "ms", "x of budget", "JS doubles/frame");
    int hi4 = nmax ? nmax : (fast ? 64 : 128);
    for (int N = 4; N <= hi4; N *= 2) {
        Model M = model_build(N, ENC_Q, 0, 1.0, 0, 0);
        double t; int it;
        g_deadline = now() + g_dead_ms/1e3;
        int st = solve(&M, NULL, NULL, &t, &it);
        g_deadline = 1e18;
        double doubles = (double)M.n*M.n + (double)M.m*M.n;   /* dense Q + dense A */
        printf("%5d %6d %6d %8d %10.2f %11.2fx %14.0f  %s\n",
               N, M.n, M.m, it, t, t/16.0, doubles, statname(st));
        model_free(&M);
    }
    printf("  'JS doubles/frame' = elements a dense-marshalling front end rebuilds per\n"
           "  solve (n*n for Q plus m*n for A) before copying them into wasm memory.\n");
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--strict")) strict = 1;
        else if (!strcmp(argv[i], "--fast")) { fast = 1; g_dead_ms = 1500; }
        else if (!strcmp(argv[i], "--only") && i+1 < argc) only = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--nmax") && i+1 < argc) nmax = atoi(argv[++i]);
        else { printf("usage: %s [--strict] [--fast] [--only 1..5] [--nmax N]\n", argv[0]); return 2; }
    }
    psolve_stop_set(stop_cb);
    printf("psolve QP core vs the UI-layout problem class\n");
    if (!only || only == 1) sec_encodings();
    if (!only || only == 2) sec_scale();
    if (!only || only == 3) sec_frames();
    if (!only || only == 5) sec_proof();
    if (!only || only == 4) sec_capacity();
    printf("\n%s\n", fails ? "SOME CHECKS FAILED (see above)" : "all checks passed");
    return (fails && strict) ? 1 : 0;
}
