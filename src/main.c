#include "parser.h"
#include "solver.h"
#include "err.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

/* The simplex loop polls this flag, so signal handling stays async-signal-safe:
   the handler only performs a single assignment to sig_atomic_t. */
static volatile sig_atomic_t g_stop = 0;
static void on_stop_signal(int signo) { (void)signo; g_stop = 1; }
static int stop_requested(void) { return (int)g_stop; }

static int parse_positive_ms(const char *text, long *out)
{
    char *end;
    long value;
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || value <= 0 || value > INT_MAX)
        return -1;
    *out = value;
    return 0;
}

int main(int argc, char **argv)
{
    /* These values remain valid across the allocation error longjmp below. */
    const char * volatile path = NULL;
    volatile int print = 0;
    volatile long time_ms = 0;
    for (int a = 1; a < argc; a++) {
        if (strcmp(argv[a], "--print") == 0) {
            print = 1;
        } else if (strcmp(argv[a], "-t") == 0 || strcmp(argv[a], "--time-limit") == 0) {
            long parsed_ms;
            if (++a == argc || parse_positive_ms(argv[a], &parsed_ms) != 0) {
                fprintf(stderr, "invalid time limit (expected positive milliseconds)\n");
                return 1;
            }
            time_ms = parsed_ms;
        } else if (argv[a][0] != '-') {
            if (path) {
                fprintf(stderr, "only one problem file may be supplied\n");
                return 1;
            }
            path = argv[a];
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[a]);
            return 1;
        }
    }
    if (!path) {
        fprintf(stderr, "usage: %s [-t ms|--time-limit ms] <problem.lp> [--print]\n", argv[0]);
        return 1;
    }

    /* Install the solver error handler so out-of-memory (and internal solver
       failures) unwind here and are reported cleanly instead of aborting. */
    if (setjmp(psolve_env) != 0) {
        fprintf(stderr, "solver failed: %s\n",
                psolve_code == PSOLVE_ERR_OOM ? "out of memory" : "internal error");
        return 2;
    }
    psolve_try();

    /* SIGINT and the optional wall-clock limit are cooperative: solver_solve()
       notices the flag and returns SOLVE_STOPPED rather than leaving a partial
       result labelled OPTIMAL. alarm() is seconds-granularity, hence round up. */
    signal(SIGINT, on_stop_signal);
    signal(SIGALRM, on_stop_signal);
    psolve_stop_fn = stop_requested;
    if (time_ms > 0) alarm((unsigned)((time_ms + 999) / 1000));

    LP lp;
    memset(&lp, 0, sizeof(LP));
    if (lp_read((const char *)path, &lp) != 0) { psolve_stop_fn = NULL; psolve_end(); return 1; }

    Solver *s = solver_create(&lp);
    if (!s) { lp_free(&lp); psolve_stop_fn = NULL; psolve_end(); return 1; }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int r = solver_solve(s);
    /* The sparse factorization is fast but can, on ill-conditioned bases,
       return a wrong (infeasible) point.  Detect that and retry with the
       robust dense path to guarantee a correct answer.  Never retry after an
       explicit user stop: doing so would violate the requested time budget. */
    if (r == 0 && !solver_feasible(s) && !psolve_stop()) {
        solver_destroy(s);
        s = solver_create(&lp);
        if (!s) { lp_free(&lp); psolve_stop_fn = NULL; psolve_end(); return 1; }
        s->sparse_disabled = 1;  /* force dense from the start */
        s->use_sparse = 0;
        r = solver_solve(s);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double secs = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;

    printf("iterations: %ld\n", s->iters);
    printf("time: %.6f s\n", secs);

    if (r == 1) {
        printf("status: INFEASIBLE\n");
    } else if (r == 2) {
        printf("status: UNBOUNDED\n");
    } else if (r == 3) {
        printf("status: ITERATION_LIMIT\n");
    } else if (r == SOLVE_STOPPED) {
        printf("status: STOPPED\n");
    } else if (r == SOLVE_NUMERICAL) {
        printf("status: NUMERICAL_FAILURE\n");
    } else {
        double *xo = (double*)malloc((size_t)(lp.n > 0 ? lp.n : 1) * sizeof(double));
        double obj;
        if (!xo) { solver_destroy(s); lp_free(&lp); psolve_stop_fn = NULL; psolve_end(); return 1; }
        solver_optimum(s, xo, &obj);
        printf("status: OPTIMAL\n");
        printf("objective: %.15g\n", obj);
        if (print) {
            for (int j = 0; j < lp.n; j++)
                printf("x[%d] = %.10g\n", j, xo[j]);
        }
        free(xo);
    }

    solver_destroy(s);
    lp_free(&lp);
    alarm(0);
    psolve_stop_fn = NULL;
    psolve_end();
    return 0;
}
