#include "solver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fenv.h>
#include <time.h>

/* tools/dual_bench.c -- roadmap 7.2 acceptance gate, performance half.
 *
 * A deterministic branch-and-bound RELAXATION walk over random dense
 * mixed-relation 0-1 MIPs (a planted interior-feasible point keeps every
 * relaxation feasible and fractional: a genuine branching workload where
 * warm re-solves do real simplex work).  The root is solved cold, then
 * every node re-solves by solver_set_bounds + solver_warm_solve from the
 * persistent simplex state -- exactly the MipWarm pattern in src/mip.c --
 * branching depth-first on the most fractional variable, with a
 * bench-side rounding heuristic so bound pruning bites early.  Uses only
 * the stable public API, so the SAME source compiles against the pre-7.2
 * tree (primal warm start) and the post-7.2 tree (dual simplex engaged
 * where eligible).  The tree checksum certifies both engines walked the
 * IDENTICAL search (same nodes, same prunes, same leaf incumbents); the
 * iteration totals then compare "dual re-solve" to "primal warm start" --
 * note the pre-7.2 engine hides refresh-transplanted work, so for a fair
 * baseline build the pre tree with -DPREBURN against a tree that keeps
 * solver's pre-transplant burn counter (see docs/DUAL7_NOTES.md).
 *
 *   dual_bench [models=40] [base_seed=77031] [node_budget=3000]
 *
 * Diagnostics are env-gated and off by default:
 *   BENCH_FRESHCHECK=1  at every optimal node also solve a FRESH solver on
 *                       the node's box and print MISMATCH when warm and
 *                       fresh objectives disagree beyond 1e-9-relative.
 *   BENCH_GENDEBUG=1    per-model root stats, BENCH_TRACE=1 node walk,
 *   BENCH_CHAIN=1       infeasible-node certificate counter stream,
 *   BENCH_DUMP=kd (+BENCH_DUMP2=node) full engine column dump at a node,
 *   BENCH_STOP=node     iteration/counter deltas at that node. */

static double rnd(unsigned *s){ *s=*s*1103515245u+12345u; return (double)((*s>>16)&0x7fff)/32767.0; }

typedef struct { LP lp; int n, m; } Bench;

static void gen_bench(Bench *B, unsigned *seed, int n, int m)
{
    /* dense mixed-relation 0-1 MIP with a PLANTED fractional feasible point
       x0 in the box interior: every row is feasible by construction
       ('<' b=A.x0+slack, '>' b=A.x0-slack, '=' b=A.x0 exactly), and the
       random objective makes the relaxation optimum fractional with high
       probability -- a genuine branching workload. */
    memset(B, 0, sizeof *B);
    LP *lp = &B->lp;
    lp->n = n; lp->m = m; lp->maximize = (rnd(seed) < 0.5);
    lp->c = (double*)malloc((size_t)n * sizeof(double));
    lp->l = (double*)malloc((size_t)n * sizeof(double));
    lp->u = (double*)malloc((size_t)n * sizeof(double));
    lp->b = (double*)malloc((size_t)m * sizeof(double));
    lp->rel = (char*)malloc((size_t)m);
    double *A = (double*)malloc((size_t)m * (size_t)n * sizeof(double));
    double x0[64];
    for (int j = 0; j < n; j++) {
        lp->c[j] = 4.0 * rnd(seed) - 1.5;
        lp->l[j] = 0.0; lp->u[j] = 1.0;
        x0[j] = 0.25 + 0.5 * rnd(seed);
    }
    long nnz = 0;
    for (int i = 0; i < m; i++) {
        double act = 0.0;
        for (int j = 0; j < n; j++) {
            double a = (rnd(seed) < 0.7) ? (6.0 * rnd(seed) - 3.0) : 0.0;
            A[(size_t)i * n + j] = a;
            act += a * x0[j];
            if (a != 0.0) nnz++;
        }
        double r = rnd(seed);
        lp->rel[i] = (r < 0.5) ? '<' : (r < 0.8) ? '>' : '=';
        double slack0 = 0.5 + 3.0 * rnd(seed);
        lp->b[i] = (lp->rel[i] == '<') ? act + slack0
                 : (lp->rel[i] == '>') ? act - slack0 : act;
    }
    lp->Acolptr = (int*)calloc((size_t)n + 1, sizeof(int));
    lp->Arow = (int*)malloc((size_t)(nnz ? nnz : 1) * sizeof(int));
    lp->Aval = (double*)malloc((size_t)(nnz ? nnz : 1) * sizeof(double));
    for (int i = 0; i < m; i++)
        for (int j = 0; j < n; j++)
            if (A[(size_t)i * n + j] != 0.0) lp->Acolptr[j + 1]++;
    for (int j = 0; j < n; j++) lp->Acolptr[j + 1] += lp->Acolptr[j];
    int *f = (int*)malloc((size_t)n * sizeof(int));
    for (int j = 0; j < n; j++) f[j] = lp->Acolptr[j];
    for (int i = 0; i < m; i++)
        for (int j = 0; j < n; j++) {
            double a = A[(size_t)i * n + j];
            if (a != 0.0) { lp->Arow[f[j]] = i; lp->Aval[f[j]] = a; f[j]++; }
        }
    free(f); free(A);
    B->n = n; B->m = m;
}

static void free_bench(Bench *B)
{
    free(B->lp.c); free(B->lp.b); free(B->lp.rel); free(B->lp.l); free(B->lp.u);
    free(B->lp.Acolptr); free(B->lp.Arow); free(B->lp.Aval);
}

typedef struct {
    Solver *s;
    long node_budget;
    long nodes, pruned_infeas, pruned_bound, leaves;
    double incumbent;
    unsigned long long obj_hash;
    long iters_total;
    int abort_r;
} Tree;

typedef struct { double lo[64], hi[64]; } Frame;

#ifdef PREBURN
extern long solver_burned_iters(void);
#endif

int main(int argc, char **argv)
{
    int models = (argc > 1) ? atoi(argv[1]) : 40;
    unsigned base = (argc > 2) ? (unsigned)strtoul(argv[2], NULL, 10) : 77031u;
    long budget = (argc > 3) ? atol(argv[3]) : 3000;
    long tot_nodes = 0, tot_iters = 0, tot_infeas = 0, tot_bound = 0, tot_leaves = 0;
    long tot_refresh = 0;
    unsigned long long tree_hash = 1469598103934665603ULL;
    long skips = 0;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    long draws = 0;
    for (int kd = 0; kd < models; kd++) {
        unsigned seed = base + 0x9e3779b9u * (unsigned)(++draws);
        int n = 10 + (int)(rnd(&seed) * 6.0);   /* 10..15 integer vars */
        int m = n - 2 + (int)(rnd(&seed) * 4.0);
        Bench B; gen_bench(&B, &seed, n, m);
        Tree T; memset(&T, 0, sizeof T);
        T.s = solver_create_opts(&B.lp, 0);     /* raw path: the MIP pin */
        T.node_budget = budget;
        T.incumbent = B.lp.maximize ? -1e100 : 1e100;

        long it0 = T.s->iters;
        int rr = solver_solve(T.s);
        T.iters_total += T.s->iters - it0;
        T.nodes++;
        if (getenv("BENCH_GENDEBUG")) {
            double xg[64], og = 0.0;
            int nfrac = 0;
            if (rr == 0) {
                solver_optimum(T.s, xg, &og);
                for (int j = 0; j < n; j++) {
                    double f = xg[j] - floor(xg[j]);
                    if (f > 1e-6 && f < 1.0 - 1e-6) nfrac++;
                }
            }
            printf("GEN kd=%d n=%d m=%d max=%d r=%d obj=%.6g nfrac=%d skips=%ld\n",
                   kd, n, m, B.lp.maximize, rr, og, nfrac, skips);
        }
        if (rr != 0) {  /* infeasible/unusable root: skip (draw-capped) */
            skips++;
            solver_destroy(T.s); free_bench(&B);
            if (kd - skips + skips < models + skips && skips <= 2000) { kd--; continue; }
            printf("dual_bench: draw cap exceeded\n"); return 2;
        }
        double x[64], obj;
        solver_optimum(T.s, x, &obj);
        T.obj_hash ^= (unsigned long long)llrint(obj * 1073741824.0);
        T.obj_hash *= 1099511628211ULL;
        int frac = -1; double fv = 0.0, bscore = -1.0;
        for (int j = 0; j < n; j++) {
            double f = x[j] - floor(x[j]);
            if (f > 1e-9 && f < 1.0 - 1e-9) {
                double sc = f < 1.0 - f ? f : 1.0 - f;   /* most fractional */
                if (sc > bscore) { bscore = sc; frac = j; fv = x[j]; }
            }
        }
        if (frac < 0) {  /* integral root: skip model (draw-capped) */
            skips++;
            solver_destroy(T.s); free_bench(&B);
            if (skips <= 2000) { kd--; continue; }
            printf("dual_bench: draw cap exceeded\n"); return 2;
        }
        /* bench-side rounding heuristic: round the LP optimum, accept only
           if every row is satisfied to 1e-9 -- an honest MIP incumbent so
           bound pruning has something to bite on from the start (no solver
           internals involved) */
        {
            double xr[64];
            for (int j = 0; j < n; j++) xr[j] = floor(x[j] + 0.5);
            int ok = 1;
            for (int i = 0; i < m && ok; i++) {
                double act = 0.0;
                for (int j = 0; j < n; j++)
                    for (int pp = B.lp.Acolptr[j]; pp < B.lp.Acolptr[j+1]; pp++)
                        if (B.lp.Arow[pp] == i) { act += B.lp.Aval[pp] * xr[j]; break; }
                double bi = B.lp.b[i], d = act - bi;
                if (B.lp.rel[i] == '<' && d > 1e-9 * (1 + fabs(bi))) ok = 0;
                if (B.lp.rel[i] == '>' && d < -1e-9 * (1 + fabs(bi))) ok = 0;
                if (B.lp.rel[i] == '=' && fabs(d) > 1e-9 * (1 + fabs(bi))) ok = 0;
            }
            if (ok) {
                double o = 0.0;
                for (int j = 0; j < n; j++) o += B.lp.c[j] * xr[j];
                T.incumbent = o;
            }
        }
        if (getenv("BENCH_TRACE")) printf("ROOT obj=%.15g inc0=%.15g frac=x%d@%g\n",
                                          obj, T.incumbent, frac, fv);
        /* DFS walk: every node re-solves via set_bounds + warm_solve from
           the persistent state -- exactly the MipWarm pattern */
        {
            Frame stack[128]; int sp = 0;
            Frame dn, up;
            for (int j = 0; j < n; j++) { dn.lo[j] = up.lo[j] = B.lp.l[j];
                                          dn.hi[j] = up.hi[j] = B.lp.u[j]; }
            dn.hi[frac] = floor(fv);
            up.lo[frac] = floor(fv) + 1.0;
            stack[sp++] = up; stack[sp++] = dn;
            while (sp > 0 && T.nodes < T.node_budget && !T.abort_r) {
                Frame fr2 = stack[--sp];
                int fenv0 = fegetround();
                long lb = T.s->iters;
#ifndef NODUALCOUNTERS
                long ld = T.s->dual_iters, lr = T.s->refreshes, ldc = T.s->dual_certs;
#else
                long ld = 0, lr = 0, ldc = 0;
#endif
                solver_set_bounds(T.s, fr2.lo, fr2.hi);
                int r2 = solver_warm_solve(T.s);
                if (getenv("BENCH_DUMP") && kd == atoi(getenv("BENCH_DUMP"))
                        && T.nodes + 1 == atol(getenv("BENCH_DUMP2") ? getenv("BENCH_DUMP2") : "96")) {
                    fprintf(stderr, "DUMP kd=%d node=%ld r2=%d\n", kd, T.nodes, r2);
                    fprintf(stderr, "  N=%d M=%d TOL_DJ absent-in-struct\n", T.s->N, T.s->M);
                    int nsl = 0; (void)nsl;
                    fprintf(stderr, "  cols (l,u) x st rc:\n");
                    for (int j = 0; j < T.s->N; j++)
                        fprintf(stderr, "  col %2d st=%d l=% .6g u=% .6g x=% .6g rc=% .6g c0=% .6g cobj=% .6g\n",
                                j, (int)T.s->status[j], T.s->l[j], T.s->u[j], T.s->x[j],
                                T.s->rc[j], T.s->c0[j], T.s->cobj[j]);
                    fprintf(stderr, "  frame boxes (original space):\n");
                    for (int j = 0; j < n; j++)
                        fprintf(stderr, "  var %2d fr=[%g,%g] orig=[%g,%g] cscale=%g\n",
                                j, fr2.lo[j], fr2.hi[j], B.lp.l[j], B.lp.u[j], T.s->cscale[j]);
                }
#ifndef NODUALCOUNTERS
                if (getenv("BENCH_STOP") && T.nodes + 1 == atol(getenv("BENCH_STOP"))) {
                    fprintf(stderr, "STOP kd=%d node=%ld: r2=%d di=%ld dd=%ld dr=%ld ddc=%ld\n",
                            kd, T.nodes + 1, r2, T.s->iters - lb, T.s->dual_iters - ld,
                            T.s->refreshes - lr, T.s->dual_certs - ldc);
                }
#endif
                /* refresh-transplant-safe delta: after a transplant the
                   counter resets, so charge the transplanted solve at its
                   (fresh-solve) iteration count */
                T.iters_total += (T.s->iters >= lb) ? (T.s->iters - lb)
                                                    : T.s->iters;
                T.nodes++;
                if (fegetround() != fenv0)
                    fprintf(stderr, "FENV-DIRTY after warm node=%ld kd=%d: mode %d -> %d (r2=%d)\n",
                            T.nodes, kd, fenv0, fegetround(), r2);
                if (r2 == 1) {
                    T.pruned_infeas++;
#ifndef NODUALCOUNTERS
                    if (getenv("BENCH_CHAIN")) printf("CHAIN node=%ld kd=%d r=1 vcerts=%ld\n",
                                                      T.nodes, kd, T.s->dual_certs);
#endif
                    continue;
                }
                if (r2 != 0) { T.abort_r = r2; break; }
                solver_optimum(T.s, x, &obj);
                T.obj_hash ^= (unsigned long long)llrint(obj * 1073741824.0);
                T.obj_hash *= 1099511628211ULL;
                if (getenv("BENCH_TRACE")) printf("TRACE node=%ld sp=%d r=0 obj=%.15g inc=%.15g\n",
                                                  T.nodes, sp, obj, T.incumbent);
                if (getenv("BENCH_FRESHCHECK")) {   /* adjudicator mode */
                    LP t = B.lp;
                    double tlo[64], thi[64];
                    memcpy(tlo, B.lp.l, sizeof(double)*n);
                    memcpy(thi, B.lp.u, sizeof(double)*n);
                    for (int j = 0; j < n; j++) if (fr2.lo[j] > tlo[j]) tlo[j] = fr2.lo[j];
                    for (int j = 0; j < n; j++) if (fr2.hi[j] < thi[j]) thi[j] = fr2.hi[j];
                    t.l = tlo; t.u = thi;
                    Solver *F = solver_create_opts(&t, 0);
                    int rf = solver_solve(F);
                    double of = 1e300;
                    if (rf == 0) { double xf[64]; solver_optimum(F, xf, &of); }
                    int mm = (rf == 0 && fabs(of - obj) > 1e-9 * (1.0 + fabs(obj)));
                    printf("FRESH node=%ld kd=%d rf=%d obj=%.15g warm=%.15g %s\n",
                           T.nodes, kd, rf, of, obj, mm ? "MISMATCH" : "ok");
                    if (mm) {
                        /* whose fault? check the warm point against rows+box */
                        double wx[64]; double wo2;
                        solver_optimum(T.s, wx, &wo2);
                        double mxrow = 0.0; int ri = -1;
                        for (int i = 0; i < m; i++) {
                            double act = 0.0;
                            for (int j = 0; j < n; j++)
                                for (int pp = B.lp.Acolptr[j]; pp < B.lp.Acolptr[j+1]; pp++)
                                    if (B.lp.Arow[pp] == i) { act += B.lp.Aval[pp]*wx[j]; break; }
                            double bi = B.lp.b[i], dd = 0.0;
                            if (B.lp.rel[i] == '<') dd = act - bi;
                            else if (B.lp.rel[i] == '>') dd = bi - act;
                            else dd = fabs(act - bi);
                            if (dd > mxrow) { mxrow = dd; ri = i; }
                        }
                        double mxb = 0.0; int bi2 = -1;
                        for (int j = 0; j < n; j++) {
                            double dd = fr2.lo[j] - wx[j];
                            if (wx[j] - fr2.hi[j] > dd) dd = wx[j] - fr2.hi[j];
                            if (dd > mxb) { mxb = dd; bi2 = j; }
                        }
                        double cx = 0.0, cxf = 0.0;
                        for (int j = 0; j < n; j++) cx += B.lp.c[j] * wx[j];
                        { double xf2[64]; solver_optimum(F, xf2, &cxf);
                          double cc = 0.0;
                          for (int j = 0; j < n; j++) cc += B.lp.c[j] * xf2[j];
                          printf("MM-detail: maxrow[%d]=%.4g maxbox[%d]=%.4g trueCx_warm=%.15g trueCx_fresh=%.15g\n",
                                 ri, mxrow, bi2, mxb, cx, cc); }
                    }
                    solver_destroy(F);
                }
                if (B.lp.maximize ? (obj <= T.incumbent + 1e-6)
                                  : (obj >= T.incumbent - 1e-6)) { T.pruned_bound++; continue; }
                int f2 = -1; double fv2 = 0.0, bs2 = -1.0;
                for (int j = 0; j < n; j++) {
                    double f = x[j] - floor(x[j]);
                    if (f > 1e-9 && f < 1.0 - 1e-9) {
                        double sc = f < 1.0 - f ? f : 1.0 - f;
                        if (sc > bs2) { bs2 = sc; f2 = j; fv2 = x[j]; }
                    }
                }
                if (f2 < 0) {
                    if (B.lp.maximize ? (obj > T.incumbent) : (obj < T.incumbent))
                        T.incumbent = obj;
                    T.leaves++;
                    if (getenv("BENCH_TRACE")) printf("TRACE node=%ld sp=%d LEAF obj=%.15g\n", T.nodes, sp, obj);
                    continue;
                }
                if (sp + 2 > 128) { T.abort_r = 99; break; }
                Frame d2 = fr2, u2 = fr2;
                d2.hi[f2] = floor(fv2);
                u2.lo[f2] = floor(fv2) + 1.0;
                stack[sp++] = u2;
                stack[sp++] = d2;
            }
        }
        if (T.abort_r) { printf("ABORT model=%d r=%d\n", kd, T.abort_r); return 2; }
        tot_nodes += T.nodes; tot_iters += T.iters_total;
        tot_infeas += T.pruned_infeas; tot_bound += T.pruned_bound; tot_leaves += T.leaves;
        tree_hash ^= T.obj_hash; tree_hash *= 1099511628211ULL;
        solver_destroy(T.s);
        free_bench(&B);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double wall = (double)(t1.tv_sec - t0.tv_sec) + 1e-9 * (double)(t1.tv_nsec - t0.tv_nsec);
    (void)tot_refresh;
#ifdef PREBURN
    tot_iters += solver_burned_iters();
    printf("pre_burned_iters=%ld\n", solver_burned_iters());
#endif
    printf("dual_bench: models=%d nodes=%ld infeas=%ld bound=%ld leaves=%ld lp_iters=%ld wall=%.3fs tree_hash=%016llx\n",
           models, tot_nodes, tot_infeas, tot_bound, tot_leaves, tot_iters, wall, tree_hash);
    printf("draws skipped: %ld\n", skips);
    return 0;
}
