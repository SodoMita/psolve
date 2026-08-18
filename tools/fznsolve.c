#include "fzn.h"
#include "err.h"
#include "tlimit.h"
#include <stdio.h>
#include <stdlib.h>
#include "cert.h"
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
        fprintf(stderr,"  -a/--all-solutions  enumerate all solutions / improving incumbents\n");
        fprintf(stderr,"  -n N   node limit   -t MS   time limit (s)   -s stats   -v verbose\n"); return 1; }

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

    PSolveErrFrame ef;
    psolve_frame_push(&ef);
    if (setjmp(ef.env) != 0) {
        fprintf(stderr,"fznsolve: %s\n",
                psolve_err_code()==PSOLVE_ERR_OOM?"out of memory":"internal error");
        rc = 2; goto done;    /* the frame is already popped by psolve_fail() */
    }
    signal(SIGINT, on_sigint);
    signal(SIGALRM, on_sigint);            /* ITIMER_REAL fires this for -t limit */
    psolve_stop_set(stop_requested);       /* cooperative abort polled by solver */
    /* Millisecond precision (alarm() rounded up to whole seconds). */
    if (tlimit_arm(opt.time_ms) != 0) { psolve_frame_pop(&ef); rc = 1; goto done; }
    sol.node_limit = opt.node_limit;       /* wire -n into the MIP node limit */
    sol.all_solutions = opt.all_solutions; /* -a / --all-solutions */

    {
        struct timespec t0,t1; clock_gettime(CLOCK_MONOTONIC,&t0);
        if (fz_read((const char*)pathbuf, &m) != 0) { psolve_frame_pop(&ef); rc = 1; goto done; }
        fz_solve(&m, &sol);
        /* roadmap 6.4: verdict-printing exits pass the unified evidence
           entry point.  Two FlatZinc lanes are checked here:
             - optimize claims (status 0, solve_kind != 0): the reported
               objective may never cross the reported bound (directional
               coherence; CP path proves exact equality) AND the incumbent
               was verified engine-side (leaf/point re-checks in
               fzn.c/mip.c);
             - UNSATISFIABLE (status 1): exhaustion stamp - no node or
               time limit may have truncated the search status 1 implies.
           A failed lane downgrades the print to UNKNOWN (status 2): the
           certificate layer never prints a verdict it cannot confirm. */
        if (sol.status == 0 && m.solve_kind != 0) {
            /* Bound coherence, DIRECTIONAL (see psv_cert_check's MIP_POINT
               lane): best_bound is the widest valid relaxation bound (the
               root bound dominates it forever), so a proven optimum does
               NOT close to it - the leftover distance is the model's root
               integrality gap/CP slack.  The sound check is that the bound
               never sits on the wrong side of the claimed objective: an
               upper bound (max) below obj, or a lower bound (min) above
               obj, is the catastrophic direction (objective claimed
               strictly better than proven).  The constant folds only into
               sol.obj, so add it back to the bound for a same-units
               comparison; the CP path stamps exact equality and passes. */
            double cb = sol.best_bound + m.objective.constant;
            double scale = 1.0 + (sol.obj < 0 ? -sol.obj : sol.obj)
                         + (cb < 0 ? -cb : cb);
            double allow = (1e-4 + 1e-6) * scale;
            int bad = (m.solve_kind == 2) ? (cb < sol.obj - allow)
                                          : (cb > sol.obj + allow);
            if (bad) {
                fprintf(stderr, "psv: fzn optimize bound-coherence certificate not confirmed (obj %.15g bound %.15g, sense %s)\n",
                        sol.obj, sol.best_bound, m.solve_kind == 2 ? "max" : "min");
                sol.status = 2;
            }
        }
        if (sol.status == 1) {
            PsvCert cl; memset(&cl, 0, sizeof(cl));
            cl.kind = PSVK_EXHAUSTION;
            cl.nodes = sol.nodes;
            cl.node_limit = sol.node_limit;
            cl.stopped = 0;
            if (psv_cert_check(&cl) != PSV_OK) {
                fprintf(stderr, "psv: fzn UNSAT exhaustion stamp not confirmed\n");
                sol.status = 2;
            }
        }
        clock_gettime(CLOCK_MONOTONIC,&t1);
        secs = (t1.tv_sec-t0.tv_sec)+(t1.tv_nsec-t0.tv_nsec)/1e9;
    }
    psolve_frame_pop(&ef);

    fz_print_solution(&m, &sol);
    if (m.solve_kind != 0 && sol.status == 0 && !opt.all_solutions)
        printf("%%%%mzn-stat: objective=%.15g\n", sol.obj);
    if (opt.show_stats) {
        printf("%%%%mzn-stat: intVariables=%d\n", sol.nvars);
        printf("%%%%mzn-stat: nodes=%ld\n", sol.nodes);
        printf("%%%%mzn-stat: farkasCerts=%ld\n", sol.farkas_certs);
        printf("%%%%mzn-stat: exactResolves=%ld\n", sol.fx_solves);
        if (m.solve_kind != 0 && sol.status == 0)
            printf("%%%%mzn-stat: objectiveBound=%.15g\n", sol.best_bound);
        printf("%%%%mzn-stat: solveTime=%.3f\n", secs);
        printf("%%%%mzn-stat-end\n");
    }
    if (opt.verbose && sol.status != 0)
        fprintf(stderr, "fznsolve: status=%d nodes=%ld\n", sol.status, sol.nodes);
done:
    tlimit_disarm();
    psolve_stop_set(NULL);
    fz_solution_free(&sol);
    fz_model_free(&m);
    return rc;
}
