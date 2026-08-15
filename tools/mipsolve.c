#include "mip.h"
#include "parser.h"
#include "err.h"
#include "tlimit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;
static void on_stop_signal(int signo) { (void)signo; g_stop = 1; }
static int stop_requested(void) { return (int)g_stop; }

int main(int argc, char **argv)
{
    const char * volatile path = NULL;
    volatile int print = 0;
    volatile long time_ms = 0;
    volatile int arg_idx = 1;
    while (arg_idx < argc && argv[arg_idx][0] == '-') {
        if (strcmp(argv[arg_idx], "-t") == 0 || strcmp(argv[arg_idx], "--time-limit") == 0) {
            if (arg_idx + 1 >= argc) { fprintf(stderr, "missing time limit\n"); return 1; }
            time_ms = atol(argv[++arg_idx]);
            if (time_ms <= 0) { fprintf(stderr, "invalid time limit\n"); return 1; }
        } else if (strcmp(argv[arg_idx], "--print") == 0) {
            print = 1;   /* accepted in any position (was: trailing-only) */
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[arg_idx]); return 1;
        }
        arg_idx++;
    }
    if (arg_idx + 1 >= argc) {
        fprintf(stderr, "usage: %s [-t ms|--time-limit ms] <problem.lp> <nint> <j0 j1 ...> [--print]\n", argv[0]);
        return 1;
    }
    path = argv[arg_idx++];
    volatile int nint = atoi(argv[arg_idx++]);
    for (int a = arg_idx; a < argc; a++) if (strcmp(argv[a], "--print") == 0) print = 1;

    if (setjmp(psolve_env) != 0) {
        fprintf(stderr, "mipsolve: %s\n",
                psolve_code == PSOLVE_ERR_OOM ? "out of memory" : "internal error");
        return 2;
    }
    psolve_try();

    signal(SIGINT, on_stop_signal);
    signal(SIGALRM, on_stop_signal);
    psolve_stop_fn = stop_requested;
    /* ITIMER_REAL gives millisecond precision (alarm() rounded up to seconds). */
    if (tlimit_arm(time_ms) != 0) { fprintf(stderr, "cannot arm time limit\n"); psolve_stop_fn = NULL; psolve_end(); return 1; }

    LP lp;
    memset(&lp, 0, sizeof(LP));
    if (lp_read(path, &lp) != 0) { psolve_stop_fn = NULL; psolve_end(); return 1; }

    unsigned char *isint = (unsigned char*)psolve_calloc((size_t)lp.n, 1);
    int ni = 0;
    for (int a = arg_idx; a < arg_idx + nint && a < argc; a++) {
        if (strcmp(argv[a], "--print") == 0) continue;
        int j = atoi(argv[a]);
        if (j >= 0 && j < lp.n) { isint[j] = 1; ni++; }
    }
    if (ni == 0) { fprintf(stderr, "no valid integer variables specified\n"); psolve_end(); return 1; }

    /* Zero the whole struct first: MIP has optional fields (stop_at_feasible,
       ...) that this driver does not set.  Leaving them uninitialised is
       undefined behaviour -- and it bit: a garbage stop_at_feasible made
       branch-and-bound return the first integer-feasible point it stumbled
       on, which was then reported as OPTIMAL. */
    MIP mip; memset(&mip, 0, sizeof(mip));
    mip.n = lp.n; mip.m = lp.m;
    mip.c = lp.c; mip.Acolptr = lp.Acolptr; mip.Arow = lp.Arow; mip.Aval = lp.Aval;
    mip.rel = lp.rel; mip.b = lp.b; mip.l = lp.l; mip.u = lp.u;
    mip.maximize = lp.maximize;
    mip.isint = isint;
    mip.mip_gap = 1e-4;
    mip.node_limit = 200000;
    mip.lp_iter_limit = 2000000;

    MIPResult res;
    mip_solve(&mip, &res);

    if (res.status == 0 && !res.proven_optimal) {
        /* Feasible, but the tree was not exhausted (e.g. stop_at_feasible).
           Never print OPTIMAL for a point whose optimality was not proven. */
        printf("status: FEASIBLE\n");
        printf("objective: %.15g\n", res.obj);
        printf("nodes: %ld\n", res.nodes);
        if (print)
            for (int j = 0; j < lp.n; j++)
                printf("x[%d] = %.10g\n", j, res.x[j]);
    } else if (res.status == 0) {
        printf("status: OPTIMAL\n");
        printf("objective: %.15g\n", res.obj);
        printf("nodes: %ld\n", res.nodes);
        if (print)
            for (int j = 0; j < lp.n; j++)
                printf("x[%d] = %.10g\n", j, res.x[j]);
    } else if (res.status == 1) {
        printf("status: INFEASIBLE\n");
        printf("nodes: %ld\n", res.nodes);
    } else if (res.status == 2) {
        printf("status: UNBOUNDED\n");
    } else if (res.status == 3) {
        printf("status: NODE_LIMIT\n");
        if (res.obj == res.obj) printf("best objective: %.15g\n", res.obj);
    } else if (res.status == 4) {
        printf("status: STOPPED\n");
        if (res.obj == res.obj) printf("best objective: %.15g\n", res.obj);
    } else if (res.status == 6) {
        printf("status: NUMERICAL_FAILURE\n");
    } else if (res.status == MIP_INVALID) {
        printf("status: INVALID_MODEL\n");
    } else {   /* status 5: feasible incumbent, optimality not proven */
        printf("status: FEASIBLE_LIMIT\n");
        if (res.obj == res.obj) {
            printf("objective: %.15g\n", res.obj);
            printf("best_bound: %.15g\n", res.best_bound);
        }
        if (print)
            for (int j = 0; j < lp.n; j++)
                printf("x[%d] = %.10g\n", j, res.x[j]);
    }

    if (print) {
        printf("farkas_certs: %ld\n", res.farkas_certs);
        printf("fx_solves: %ld\n", res.fx_solves);
    }

    mip_result_free(&res);
    free(isint);
    lp_free(&lp);
    tlimit_disarm();
    psolve_stop_fn = NULL;
    psolve_end();
    return 0;
}
