#include "fzn.h"
#include "err.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int s){ (void)s; g_stop = 1; }
static int stop_requested(void){ return (int)g_stop; }

typedef struct { const char*path; long node_limit; int show_stats; int verbose; long time_ms; int all_solutions; } Options;
static Options parse_args(int argc, char**argv){
    Options o = { NULL, 200000, 0, 0, 0, 0 };
    for (int a = 1; a < argc; a++) {
        if (strcmp(argv[a], "-s") == 0) o.show_stats = 1;
        else if (strcmp(argv[a], "-v") == 0) o.verbose = 1;
        else if (strcmp(argv[a], "-a") == 0 || strcmp(argv[a], "--all-solutions") == 0) o.all_solutions = 1;
        else if (strcmp(argv[a], "-f") == 0 || strcmp(argv[a], "--free-search") == 0) ;
        else if (strcmp(argv[a], "-p") == 0 && a+1 < argc) a++;
        else if (strcmp(argv[a], "-n") == 0 && a+1 < argc) o.node_limit = atol(argv[++a]);
        else if (strcmp(argv[a], "-t") == 0 && a+1 < argc) o.time_ms = atol(argv[++a]);
        else if (argv[a][0] != '-') o.path = argv[a];
    }
    return o;
}

int main(int argc,char**argv)
{
    Options opt = parse_args(argc, argv);
    if (!opt.path) { fprintf(stderr,"usage: %s [options] <problem.fzn>\n",argv[0]);
        fprintf(stderr,"  -n N   node limit   -s stats   -v verbose\n"); return 1; }

    /* Resolve the path into a volatile-qualified copy before setjmp so the
       compiler cannot hold the pointer in a callee-saved register that
       longjmp clobbers.  Also avoids -Wclobbered. */
    volatile char pathbuf[4096];
    /* safe bounded copy — opt.path lives for the process lifetime in argv */
    size_t plen = strlen(opt.path);
    if (plen >= sizeof(pathbuf)) { fprintf(stderr,"fznsolve: path too long\n"); return 1; }
    memcpy((void*)pathbuf, opt.path, plen+1);

    int rc = 0;
    FZSolution sol; memset(&sol, 0, sizeof(sol));
    FZModel m; memset(&m, 0, sizeof(m));
    double secs = 0.0;

    if (setjmp(psolve_env) != 0) {
        fprintf(stderr,"fznsolve: %s\n",
                psolve_code==PSOLVE_ERR_OOM?"out of memory":"internal error");
        rc = 2; goto done;
    }
    psolve_try();
    signal(SIGINT, on_sigint);
    signal(SIGALRM, on_sigint);            /* alarm() fires this for -t limit */
    psolve_stop_fn = stop_requested;       /* cooperative abort polled by solver */
    if (opt.time_ms > 0) alarm((unsigned)((opt.time_ms+999)/1000));
    sol.node_limit = opt.node_limit;       /* wire -n into the MIP node limit */
    sol.all_solutions = opt.all_solutions; /* -a / --all-solutions */

    {
        struct timespec t0,t1; clock_gettime(CLOCK_MONOTONIC,&t0);
        if (fz_read((const char*)pathbuf, &m) != 0) { psolve_end(); rc = 1; goto done; }
        fz_solve(&m, &sol);
        clock_gettime(CLOCK_MONOTONIC,&t1);
        secs = (t1.tv_sec-t0.tv_sec)+(t1.tv_nsec-t0.tv_nsec)/1e9;
    }
    psolve_end();

    fz_print_solution(&m, &sol);
    if (m.solve_kind != 0 && sol.status == 0 && !opt.all_solutions)
        printf("%%%%mzn-stat: objective=%.15g\n", sol.obj);
    if (opt.show_stats) {
        printf("%%%%mzn-stat: intVariables=%d\n", sol.nvars);
        printf("%%%%mzn-stat: nodes=%ld\n", sol.nodes);
        if (m.solve_kind != 0 && sol.status == 0)
            printf("%%%%mzn-stat: objectiveBound=%.15g\n", sol.best_bound);
        printf("%%%%mzn-stat: solveTime=%.3f\n", secs);
        printf("%%%%mzn-stat-end\n");
    }
    if (opt.verbose && sol.status != 0)
        fprintf(stderr, "fznsolve: status=%d nodes=%ld\n", sol.status, sol.nodes);
done:
    psolve_stop_fn = NULL;
    fz_solution_free(&sol);
    fz_model_free(&m);
    return rc;
}
