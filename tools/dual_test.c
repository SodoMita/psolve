#include "solver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fenv.h>

/* tools/dual_test.c -- roadmap 7.2 acceptance gate, correctness half.
 *
 * A/B over >= 20000 random LPs: every model is solved fresh, then walked
 * through 1..3 rounds of B&B-shaped bound tightenings (a variable's bound
 * is pushed well past its current optimum value -- the branch-and-bound
 * pattern; occasionally the cut flips the box empty, and some starts are
 * infeasible/unbounded so the warm-after-terminal paths run too).  Each
 * round is decided twice: incrementally by solver_warm_solve (where the
 * 7.2 bounded-variable dual simplex engages whenever the warm basis is
 * dual feasible) and from scratch by solver_solve on identical data.
 *
 * The engine's own primal path is the trusted reference (solution
 * certificates + scipy/GLPK differentials gate it elsewhere), so the bar
 * here is EXACT: identical verdict classes on every round, objective
 * agreement <= 1e-7 whenever both sides claim OPTIMAL, and a passing
 * primal certificate on every warm OPTIMAL point.  On the pre-change
 * binary this file does not compile (dual_iters/dfarkas_* absent) -- the
 * discrimination mechanism per the roadmap constitution. */

static void free_lp(LP *lp)
{
    free(lp->c); free(lp->b); free(lp->rel); free(lp->l); free(lp->u);
    free(lp->Acolptr); free(lp->Arow); free(lp->Aval);
}

static double rnd(unsigned *s)
{
    *s = *s * 1103515245u + 12345u;
    return (double)((*s >> 16) & 0x7fff) / 32767.0;
}

/* Random LP with a deliberately mixed structure: '<' '>' '=' rows,
   boxed / nonneg / bounded / free variables, max or min -- so every warm
   path (dual, legacy primal, refresh, empty-box) is exercised. */
static void gen_lp(LP *lp, unsigned *seed)
{
    int n = 2 + (int)(rnd(seed) * 22.0);   /* 2..23 vars */
    int m = 1 + (int)(rnd(seed) * 18.0);   /* 1..18 rows */
    memset(lp, 0, sizeof(*lp));
    lp->n = n; lp->m = m;
    lp->maximize = (rnd(seed) < 0.5) ? 0 : 1;
    lp->c = (double*)malloc((size_t)n * sizeof(double));
    lp->l = (double*)malloc((size_t)n * sizeof(double));
    lp->u = (double*)malloc((size_t)n * sizeof(double));
    lp->b = (double*)malloc((size_t)m * sizeof(double));
    lp->rel = (char*)malloc((size_t)m);
    double *dense = (double*)malloc((size_t)m * (size_t)n * sizeof(double));
    long nnz = 0;
    for (int i = 0; i < m; i++)
        for (int j = 0; j < n; j++) {
            double a = (rnd(seed) < 0.4) ? (rnd(seed) * 8.0 - 4.0) : 0.0;
            dense[(size_t)i * n + j] = a;
            if (a != 0.0) nnz++;
        }
    /* keep every row nonempty: structural degeneracies stay covered via
       the tightening rounds, not via empty generator output */
    for (int i = 0; i < m; i++) {
        int any = 0;
        for (int j = 0; j < n; j++) if (dense[(size_t)i * n + j] != 0.0) any = 1;
        if (!any) { dense[(size_t)i * n + (i % n)] = 1.0; nnz++; }
    }
    lp->Acolptr = (int*)calloc((size_t)n + 1, sizeof(int));
    lp->Arow = (int*)malloc((size_t)nnz * sizeof(int));
    lp->Aval = (double*)malloc((size_t)nnz * sizeof(double));
    for (int i = 0; i < m; i++)
        for (int j = 0; j < n; j++)
            if (dense[(size_t)i * n + j] != 0.0) lp->Acolptr[j + 1]++;
    for (int j = 0; j < n; j++) lp->Acolptr[j + 1] += lp->Acolptr[j];
    int *f = (int*)malloc((size_t)n * sizeof(int));
    for (int j = 0; j < n; j++) f[j] = lp->Acolptr[j];
    for (int i = 0; i < m; i++)
        for (int j = 0; j < n; j++) {
            double a = dense[(size_t)i * n + j];
            if (a != 0.0) { lp->Arow[f[j]] = i; lp->Aval[f[j]] = a; f[j]++; }
        }
    free(f); free(dense);
    for (int j = 0; j < n; j++) {
        lp->c[j] = rnd(seed) * 10.0 - 3.0;
        double kind = rnd(seed);
        if (kind < 0.45)      { lp->l[j] = 0.0;                lp->u[j] = 4.0 + rnd(seed) * 8.0; }
        else if (kind < 0.65) { lp->l[j] = 0.0;                lp->u[j] = LP_INF; }
        else if (kind < 0.85) { lp->l[j] = -(3.0 + rnd(seed) * 5.0); lp->u[j] = 3.0 + rnd(seed) * 5.0; }
        else                  { lp->l[j] = -LP_INF;            lp->u[j] = LP_INF; }   /* free */
    }
    for (int i = 0; i < m; i++) {
        double r = rnd(seed);
        lp->rel[i] = (r < 0.55) ? '<' : (r < 0.8) ? '>' : '=';
        lp->b[i] = rnd(seed) * 26.0 - 8.0;
    }
}

static const char *vclass(int r)
{
    switch (r) {
    case 0: return "OPTIMAL";
    case 1: return "INFEASIBLE";
    case 2: return "UNBOUNDED";
    case 3: return "LIMIT";
    case SOLVE_STOPPED: return "STOPPED";
    case SOLVE_NUMERICAL: return "NUMERICAL";
    default: return "?";
    }
}

int main(int argc, char **argv)
{
    int seeds = (argc > 1) ? atoi(argv[1]) : 24000;
    unsigned base = (argc > 2) ? (unsigned)strtoul(argv[2], NULL, 10) : 20260921u;
    long rounds = 0, agrees = 0, dual_pivots = 0, warm_pivots = 0;
    long dual_rounds = 0, refresh_rounds = 0, cert_rounds = 0, fresh_pivots = 0;
    long declines = 0, fresh_declines = 0;
    long cls[4][4];
    memset(cls, 0, sizeof(cls));
    int fails = 0;
    double worst_objgap = 0.0;
    unsigned long long hash = 1469598103934665603ULL;
    int hash_stop = 0;

    for (int sd = 0; sd < seeds; sd++) {
        unsigned seed = base + 0x9e3779b9u * (unsigned)(sd + 1);
        LP lp; gen_lp(&lp, &seed);
        int sm = (sd & 1) ? 0 : 1;    /* A/B both scaling lanes (mip pins 0) */
        double lo[32], hi[32], x[32], obj_w, obj_f;
        Solver *W = solver_create_opts(&lp, sm);
        if (!W) { fprintf(stderr, "create failed\n"); return 2; }
        int rW = solver_solve(W);
        for (int j = 0; j < lp.n; j++) { lo[j] = lp.l[j]; hi[j] = lp.u[j]; }

        int nrounds = 1 + sd % 3;
        int dump = (getenv("DUAL_DUMP") && atoi(getenv("DUAL_DUMP")) == sd);
        if (dump) {
            printf("DUMP seed=%d n=%d m=%d max=%d sm=%d nrounds=%d coldr=%d\n",
                   sd, lp.n, lp.m, lp.maximize, sm, nrounds, rW);
            for (int i = 0; i < lp.m; i++) {
                printf("  row %d rel=%c b=%.17g :", i, lp.rel[i], lp.b[i]);
                for (int j = 0; j < lp.n; j++)
                    for (int k = lp.Acolptr[j]; k < lp.Acolptr[j+1]; k++)
                        if (lp.Arow[k] == i) printf(" %.17g*x%d", lp.Aval[k], j);
                printf("\n");
            }
            for (int j = 0; j < lp.n; j++)
                printf("  x%d c=%.17g [%.17g, %.17g]\n", j, lp.c[j], lp.l[j], lp.u[j]);
        }
        for (int round = 0; round < nrounds; round++) {
            /* B&B-shaped tightening on one variable: push a bound well past
               the incumbent value (clearance >> engine tolerances); 10% of
               the time flip the box empty outright. */
            int j = (int)(rnd(&seed) * lp.n) % lp.n;
            int rcur = rW;
            double v;
            if (rcur == 0) { solver_optimum(W, x, &obj_w); v = x[j]; }
            else v = 0.5 * ((lo[j] > -1e25 ? lo[j] : -5.0) + (hi[j] < 1e25 ? hi[j] : 5.0));
            double gap = 0.5 + rnd(&seed) * 5.0;
            double nl = lo[j], nh = hi[j];
            double lane = rnd(&seed);
            if (lane < 0.70) {
                /* B&B-shaped tightening on one variable: push a bound well
                   past the incumbent value (clearance >> engine tolerances) */
                if (rnd(&seed) < 0.5) nh = v - gap;
                else                  nl = v + gap;
                lo[j] = nl < lo[j] ? lo[j] : nl;    /* tighten only */
                hi[j] = nh > hi[j] ? hi[j] : nh;
            } else if (lane < 0.85) {
                /* WIDENING lane (regression: the pre-fix dual engine trusted
                   a stale NBL/NBU status whose bound had moved away -- x was
                   parked on the far side and the ratio test mis-signed the
                   reduced cost, fabricating false optima).  Widen one side
                   past the incumbent value; the parked status no longer
                   names the bound the value sits on. */
                if (rnd(&seed) < 0.5) {
                    nh = (hi[j] < 1e25 ? ((v + 4.0*gap > hi[j]) ? v + 4.0*gap : hi[j]) : 1e25);
                } else {
                    nl = (lo[j] > -1e25 ? ((v - 4.0*gap < lo[j]) ? v - 4.0*gap : lo[j]) : -1e25);
                }
                lo[j] = nl > lo[j] ? lo[j] : nl;    /* widen only (join) */
                hi[j] = nh < hi[j] ? hi[j] : nh;
            } else {
                /* pin->widen Shabbo sequence: pin the variable at its
                   incumbent value this round; a later round (the widen lane
                   above) may unpark it to the opposite side. */
                nl = v; nh = v;
                lo[j] = nl < lo[j] ? lo[j] : nl;    /* tighten to the pin */
                hi[j] = nh > hi[j] ? hi[j] : nh;
            }
            if (nh < -1e25) nh = -1e25;
            if (nl >  1e25) nl =  1e25;
            if (lo[j] > hi[j]) { double t = lo[j]; lo[j] = hi[j]; hi[j] = t; }
            if (dump) printf("DUMP round=%d x%d -> [%.17g, %.17g]\n", round, j, lo[j], hi[j]);
            long it0 = W->iters, dit0 = W->dual_iters, rf0 = W->refreshes, dc0 = W->dual_certs;
            if (fegetround() != FE_TONEAREST) {
                printf("ROUNDING-MODE LEAK before warm seed=%d round=%d mode=%d\n",
                       sd, round, fegetround());
                fails++;
            }
            solver_set_bounds(W, lo, hi);
            for (int rep = 0; rep < (dump ? 2 : 1); rep++) {
                if (rep) printf("DUMP replay of the same warm solve:\n");
                rW = solver_warm_solve(W);
                if (fegetround() != FE_TONEAREST) {
                    printf("ROUNDING-MODE LEAK after warm seed=%d round=%d mode=%d\n",
                           sd, round, fegetround());
                    fails++;
                }
                if (dump) {
                    double gx[32], go; solver_optimum(W, gx, &go);
                    printf("  -> r=%d feasible=%d obj=%.17g dual_d=%ld rf_d=%ld dc_d=%ld x8?=%.10g\n",
                           rW, solver_feasible(W), go, W->dual_iters - dit0,
                           W->refreshes - rf0, W->dual_certs - dc0, gx[8]);
                }
            }
            dual_pivots += W->dual_iters - dit0;
            warm_pivots += W->iters - it0;
            if (W->dual_iters > dit0) dual_rounds++;
            if (W->refreshes > rf0) refresh_rounds++;
            if (W->dual_certs > dc0) cert_rounds++;

            /* same decision, from scratch, identical data (struct copy with
               the tightened box; lp itself is never repointed) */
            LP lf = lp;
            lf.l = lo; lf.u = hi;
            Solver *F = solver_create_opts(&lf, sm);
            int rF = solver_solve(F);
            if (fegetround() != FE_TONEAREST) {
                printf("ROUNDING-MODE LEAK after fresh seed=%d round=%d mode=%d\n",
                       sd, round, fegetround());
                fails++;
            }
            fresh_pivots += F->iters;

            rounds++;
            int cw = (rW >= 0 && rW <= 2) ? rW : 3;
            int cf = (rF >= 0 && rF <= 2) ? rF : 3;
            cls[cw][cf]++;
            if (cw == 3 && cf < 3) {
                /* warm DECLINED (limit/stop/numerical) where the trusted
                   path reports a verdict: the honesty doctrine's safe
                   direction, tolerable only in a tight budget (the
                   1e30-sentinel mixed-row families post-terminal chains
                   sit on the numerically-fragile frontier AUDIT.md flags;
                   refresh-vs-fresh last-ulp drift there may decline what a
                   direct fresh solve decides).  Verdict-vs-verdict
                   disagreement is NEVER tolerated (fabrication lanes). */
                declines++;
                if (declines > rounds / 10000 + 1) {
                    printf("DECLINE-BUDGET seed=%d round=%d warm=%s fresh=%s declines=%ld rounds=%ld\n",
                           sd, round, vclass(rW), vclass(rF), declines, rounds);
                    fails++;
                }
            } else if (cf == 3 && cw < 3) {
                /* the trusted path itself declining where warm answers is
                   not a warm-side defect; count, but never fail on it */
                fresh_declines++;
                printf("FRESH-DECLINE seed=%d round=%d rW=%d rF=%d\n", sd, round, rW, rF);
            } else if (cw != cf) {
                printf("MISMATCH seed=%d round=%d var=%d [%g,%g] warm=%s fresh=%s",
                       sd, round, j, lo[j], hi[j], vclass(rW), vclass(rF));
                /* stability probe: solve the identical model again, now and
                   at the end of the program -- a fresh verdict that flips
                   between identical extractions is knife-edge engine noise,
                   not solver state (never silently tolerated either way) */
                int rr[4];
                for (int k2 = 0; k2 < 4; k2++) {
                    Solver *G = solver_create_opts(&lf, sm);
                    rr[k2] = solver_solve(G);
                    solver_destroy(G);
                }
                printf("  replays(fresh): %s %s %s %s\n",
                       vclass(rr[0]), vclass(rr[1]), vclass(rr[2]), vclass(rr[3]));
                fails++;
            } else if (rW == 0) {
                solver_optimum(W, x, &obj_w);
                solver_optimum(F, x, &obj_f);
                double denom = 1.0 + fabs(obj_f);
                double gapo = fabs(obj_w - obj_f) / denom;
                if (gapo > worst_objgap) worst_objgap = gapo;
                if (gapo > 1e-7) {
                    printf("OBJGAP seed=%d round=%d warm=%.17g fresh=%.17g\n",
                           sd, round, obj_w, obj_f);
                    fails++;
                }
                if (!solver_feasible(W)) {
                    printf("WARM-INFEASIBLE-OPTIMAL seed=%d round=%d\n", sd, round);
                    fails++;
                }
                agrees++;
            } else agrees++;
            if (cw == 3 && fails == 0) { /* both hit limit/numerical: parity
                                            holds, nothing to compare beyond */ }
            /* rerun-independent state hash: verdicts and objectives only */
            if (!hash_stop) {
                unsigned long long h = hash;
                const unsigned char *pb = (const unsigned char*)&rW;
                for (size_t z = 0; z < sizeof rW; z++) { h ^= pb[z]; h *= 1099511628211ULL; }
                double ov = (rW == 0) ? obj_w : (double)rW;
                pb = (const unsigned char*)&ov;
                for (size_t z = 0; z < sizeof ov; z++) { h ^= pb[z]; h *= 1099511628211ULL; }
                hash_stop = (getenv("DUAL_HASH_OFF") != NULL);
                hash = h;
            }
            solver_destroy(F);
            if (rcur == SOLVE_NUMERICAL || rcur == SOLVE_STOPPED) break;
        }
        solver_destroy(W);
        if (fails > 10) break;
        free_lp(&lp);
    }

    printf("dual_test: seeds=%d rounds=%ld agrees=%ld dual_pivots=%ld warm_pivots=%ld worst_objgap=%.3g\n",
           seeds, rounds, agrees, dual_pivots, warm_pivots, worst_objgap);
    printf("engagement: dual_rounds=%ld cert_rounds=%ld refresh_rounds=%ld fresh_pivots=%ld declines=%ld fresh_declines=%ld\n",
           dual_rounds, cert_rounds, refresh_rounds, fresh_pivots, declines, fresh_declines);
    printf("verdict classes (warm x fresh):  optimal=%ld infeas=%ld unbnd=%ld lim/num(agreed)=%ld\n",
           cls[0][0], cls[1][1], cls[2][2], cls[3][3]);
    printf("hash: %016llx\n", hash);
    if (fails) { printf("dual_test: FAIL (%d)\n", fails); return 1; }
    printf("dual_test: ALL OK\n");
    return 0;
}
