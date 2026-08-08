#include "qp.h"
#include "err.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Q is a dense n*n matrix, so the practical limit on n is far smaller than for
   the sparse LP solver.  8192 keeps n*n*8 = 512MB max for Q, which is a
   generous ceiling for a dense QP while preventing absurd allocations. */
#define MAX_QPDIM 8192
#define MAX_QPM    1000000

int main(int argc, char **argv){
    if (argc < 2) { fprintf(stderr, "usage: %s <qp>\n", argv[0]); return 1; }
    /* Install the solver error handler: without it an out-of-memory inside the
       QP core reached psolve_fail() with no handler and abort()ed the process
       (SIGABRT) instead of reporting a clean failure. */
    if (setjmp(psolve_env) != 0) {
        fprintf(stderr, "qpsolve: %s\n",
                psolve_code == PSOLVE_ERR_OOM ? "out of memory" : "internal error");
        return 2;
    }
    psolve_try();
    FILE *f = fopen(argv[1], "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    int n, m;
    if (fscanf(f, "%d %d", &n, &m) != 2) { fclose(f); return 1; }
    /* validate dimensions before allocating (mirrors parser hardening) */
    if (n <= 0 || m < 0 || n > MAX_QPDIM || m > MAX_QPM) { fclose(f); return 1; }
    /* guard the n*n dense allocation against size_t overflow / OOM */
    if ((size_t)n > (size_t)-1 / (size_t)n / sizeof(double)) { fclose(f); return 1; }
    double *c = (double*)malloc((size_t)(n ? n : 1) * sizeof(double));
    double *Q = (double*)malloc((size_t)(n ? n : 1) * (size_t)(n ? n : 1) * sizeof(double));
    double *A = (double*)malloc((size_t)(m ? m : 1) * (size_t)(n ? n : 1) * sizeof(double));
    double *b = (double*)malloc((size_t)(m ? m : 1) * sizeof(double));
    if (!c || !Q || !A || !b) { fclose(f); free(c); free(Q); free(A); free(b); return 1; }
    for (int j = 0; j < n; j++)
        if (fscanf(f, "%lf", &c[j]) != 1 || !isfinite(c[j])) goto bad;
    for (int j = 0; j < n; j++) for (int i = 0; i < n; i++)
        if (fscanf(f, "%lf", &Q[(size_t)j*n+i]) != 1 ||
            !isfinite(Q[(size_t)j*n+i])) goto bad;
    for (int i = 0; i < m; i++) for (int j = 0; j < n; j++)
        if (fscanf(f, "%lf", &A[(size_t)i*n+j]) != 1 ||
            !isfinite(A[(size_t)i*n+j])) goto bad;
    for (int i = 0; i < m; i++)
        if (fscanf(f, "%lf", &b[i]) != 1 || !isfinite(b[i])) goto bad;
    fclose(f);

    QP qp; qp.n = n; qp.m = m; qp.Q = Q; qp.c = c; qp.A = A; qp.b = b; qp.x0 = NULL;
    QPResult r; qp_solve(&qp, &r);
    if (r.status == 0) {
        printf("SOLUTION");
        for (int i = 0; i < n; i++) printf(" %.17g", r.x[i]);
        printf("\nOBJ %.17g\nITERS %d\n", r.obj, r.iterations);
    } else {
        printf("STATUS %d\n", r.status);
    }
    qp_result_free(&r);
    free(c); free(Q); free(A); free(b);
    return 0;

bad:
    fclose(f); free(c); free(Q); free(A); free(b);
    fprintf(stderr, "QP parse error\n");
    return 1;
}
