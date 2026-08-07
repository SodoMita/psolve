#include "parser.h"
#include "solver.h"
#include "err.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <problem.lp> [--print]\n", argv[0]);
        return 1;
    }
    const char *path = argv[1];
    int print = 0;
    for (int a = 2; a < argc; a++) if (strcmp(argv[a], "--print") == 0) print = 1;

    /* Install the solver error handler so out-of-memory (and internal solver
       failures) unwind here and are reported cleanly instead of aborting. */
    if (setjmp(psolve_env) != 0) {
        fprintf(stderr, "solver failed: %s\n",
                psolve_code == PSOLVE_ERR_OOM ? "out of memory" : "internal error");
        return 2;
    }
    psolve_try();

    LP lp;
    memset(&lp, 0, sizeof(LP));
    if (lp_read(path, &lp) != 0) return 1;

    Solver *s = solver_create(&lp);
    if (!s) { lp_free(&lp); psolve_end(); return 1; }


    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int r = solver_solve(s);
    /* The sparse factorization is fast but can, on ill-conditioned bases,
       return a wrong (infeasible) point.  Detect that and retry with the
       robust dense path to guarantee a correct answer. */
    if (r == 0 && !solver_feasible(s)) {
        solver_destroy(s);
        s = solver_create(&lp);
        if (!s) { lp_free(&lp); psolve_end(); return 1; }
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
    } else {
        double *xo = (double*)malloc((size_t)(lp.n > 0 ? lp.n : 1) * sizeof(double));
        double obj;
        if (!xo) { solver_destroy(s); lp_free(&lp); psolve_end(); return 1; }
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
    psolve_end();
    return 0;
}
