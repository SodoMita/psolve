#include "qp.h"
#include "err.h"
#include "tlimit.h"
#include "cert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <signal.h>

/* Q is a dense n*n matrix, so the practical limit on n is far smaller than for
   the sparse LP solver.  8192 keeps n*n*8 = 512MB max for Q, which is a
   generous ceiling for a dense QP while preventing absurd allocations. */
#define MAX_QPDIM 8192
#define MAX_QPM    1000000

/* Cooperative abort: a SIGINT/SIGALRM handler only flips this flag (single,
   async-signal-safe assignment); the QP core polls psolve_stop() and winds down
   to QP_STOPPED instead of returning a partial result as OPTIMAL. */
static volatile sig_atomic_t g_stop = 0;
static void on_stop_signal(int signo) { (void)signo; g_stop = 1; }
static int stop_requested(void) { return (int)g_stop; }

int main(int argc, char **argv)
{
    const char * volatile path = NULL;
    volatile int print = 0;
    volatile long time_ms = 0;
    volatile int arg_idx = 1;
    while (arg_idx < argc && argv[arg_idx][0] == '-' && strcmp(argv[arg_idx], "--print") != 0) {
        if (strcmp(argv[arg_idx], "-t") == 0 || strcmp(argv[arg_idx], "--time-limit") == 0) {
            if (arg_idx + 1 >= argc) { fprintf(stderr, "missing time limit\n"); return 1; }
            char *end; time_ms = strtol(argv[++arg_idx], &end, 10);
            if (*end != '\0' || time_ms <= 0) { fprintf(stderr, "invalid time limit\n"); return 1; }
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[arg_idx]); return 1;
        }
        arg_idx++;
    }
    if (arg_idx >= argc) {
        fprintf(stderr, "usage: %s [-t ms|--time-limit ms] <qp> [--print]\n", argv[0]);
        return 1;
    }
    path = argv[arg_idx++];
    for (int a = arg_idx; a < argc; a++) if (strcmp(argv[a], "--print") == 0) print = 1;

    /* Install the solver error handler: without it an out-of-memory inside the
       QP core reached psolve_fail() with no handler and abort()ed the process
       (SIGABRT) instead of reporting a clean failure. */
    PSolveErrFrame ef;
    psolve_frame_push(&ef);
    if (setjmp(ef.env) != 0) {
        fprintf(stderr, "qpsolve: %s\n",
                psolve_err_code() == PSOLVE_ERR_OOM ? "out of memory" : "internal error");
        return 2;   /* the frame is already popped by psolve_fail() */
    }

    signal(SIGINT, on_stop_signal);
    signal(SIGALRM, on_stop_signal);
    psolve_stop_set(stop_requested);
    if (tlimit_arm(time_ms) != 0) { fprintf(stderr, "cannot arm time limit\n"); psolve_frame_pop(&ef); return 1; }

    FILE *f = fopen((const char*)path, "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", (const char*)path); tlimit_disarm(); psolve_stop_set(NULL); psolve_frame_pop(&ef); return 1; }
    int n, m;
    if (fscanf(f, "%d %d", &n, &m) != 2) { fclose(f); tlimit_disarm(); psolve_stop_set(NULL); psolve_frame_pop(&ef); return 1; }
    /* validate dimensions before allocating (mirrors parser hardening) */
    if (n <= 0 || m < 0 || n > MAX_QPDIM || m > MAX_QPM) { fclose(f); tlimit_disarm(); psolve_stop_set(NULL); psolve_frame_pop(&ef); return 1; }
    /* guard the n*n dense allocation against size_t overflow / OOM */
    if ((size_t)n > (size_t)-1 / (size_t)n / sizeof(double)) { fclose(f); tlimit_disarm(); psolve_stop_set(NULL); psolve_frame_pop(&ef); return 1; }
    double *c = (double*)malloc((size_t)(n ? n : 1) * sizeof(double));
    double *Q = (double*)malloc((size_t)(n ? n : 1) * (size_t)(n ? n : 1) * sizeof(double));
    double *A = (double*)malloc((size_t)(m ? m : 1) * (size_t)(n ? n : 1) * sizeof(double));
    double *b = (double*)malloc((size_t)(m ? m : 1) * sizeof(double));
    if (!c || !Q || !A || !b) { fclose(f); free(c); free(Q); free(A); free(b); tlimit_disarm(); psolve_stop_set(NULL); psolve_frame_pop(&ef); return 1; }
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

    QP qp; memset(&qp, 0, sizeof qp);   /* the struct is extensible: zero it, do
                                           not assign members one at a time */
    qp.n = n; qp.m = m; qp.Q = Q; qp.c = c; qp.A = A; qp.b = b; qp.x0 = NULL;
    /* A/B hook for the Phase-I ordering policy (QP.phase1_order), so the
     * differential harness and any reviewer can compare both orders over the same
     * models: what may differ is how many models get an answer and how fast,
     * never what an answered model's verdict is. */
    { const char *ab = getenv("PSOLVE_QP_PHASE1_LP_FIRST");
      if (ab && ab[0] == '1') qp.phase1_order = QP_PHASE1_LP_FIRST; }
    QPResult r; memset(&r, 0, sizeof(r));
    qp_solve(&qp, &r);
    /* Machine-readable lines keep the historical contract the differential
       harness (qp_diff.py / qp_gen.py) parses: status 0 -> "SOLUTION x..." +
       "OBJ" + "ITERS"; any other status -> "STATUS <n>".  "--print" adds
       per-variable x[..] lines on top. */
    if (r.status == 0) {
        /* roadmap 6.4: print an optimum only through the unified evidence
           entry point - the checker re-verifies primal rows, stationarity
           residual, multiplier sign/complementarity and the claimed
           objective against the original model */
        PsvCert cl; memset(&cl, 0, sizeof(cl));
        cl.kind = PSVK_QP_OPTIMAL;
        cl.n = n; cl.m = m; cl.Q = Q; cl.A = A; cl.bq = b; cl.cq = c;
        cl.x = r.x; cl.mu = r.mult; cl.obj = r.obj;
        cl.gt_row = 1e-7; cl.dt_dj = 1e-7; cl.dt_obj = 1e-6;
        if (psv_cert_check(&cl) != PSV_OK) {
            fprintf(stderr, "psv: qp_optimal certificate not confirmed\n");
            printf("STATUS %d\n", QP_KKT_FAIL);
            qp_result_free(&r);
            free(c); free(Q); free(A); free(b);
            tlimit_disarm(); psolve_stop_set(NULL); psolve_frame_pop(&ef);
            return 0;
        }
        printf("SOLUTION");
        for (int i = 0; i < n; i++) printf(" %.17g", r.x[i]);
        printf("\nOBJ %.17g\nITERS %d\n", r.obj, r.iterations);
        if (print)
            for (int i = 0; i < n; i++) printf("x[%d] = %.17g\n", i, r.x[i]);
    } else if (r.status == 1) {
        /* UNBOUNDED claims pass the re-verified ray through the checker */
        PsvCert cl; memset(&cl, 0, sizeof(cl));
        cl.kind = PSVK_QP_UNBOUNDED;
        cl.n = n; cl.m = m; cl.Q = Q; cl.A = A; cl.bq = b; cl.cq = c;
        cl.x = r.x; cl.ray = r.ray;
        cl.gt_row = 1e-7; cl.dt_dj = 1e-9;
        if (psv_cert_check(&cl) == PSV_OK) {
            printf("STATUS %d\n", r.status);
        } else {
            fprintf(stderr, "psv: qp_unbounded certificate not confirmed\n");
            printf("STATUS %d\n", QP_KKT_FAIL);
        }
    } else if (r.status == QP_STOPPED) {
        printf("STATUS %d\n", r.status);
        /* best incumbent (feasible) without claiming optimality */
        if (r.x && r.n > 0) {
            printf("OBJ %.17g\nITERS %d\n", r.obj, r.iterations);
            if (print)
                for (int i = 0; i < n; i++) printf("x[%d] = %.17g\n", i, r.x[i]);
        } else {
            printf("no feasible incumbent\n");
        }
    } else {
        printf("STATUS %d\n", r.status);
        if (r.status == -1 && r.infeasible_proven) {
            /* roadmap 6.4, applied to the QP's infeasibility claim: the proof is
             * only worth printing if the unified checker confirms the certificate
             * over THIS model's A and b.  The status line keeps its historical
             * meaning (-1 = "no feasible start", which is honest either way); what
             * is gated here is the stronger PROVEN claim, so a bug in the producer's
             * own verifier shows up as PROVEN 0 plus a warning instead of a
             * confidently empty model. */
            PsvCert cl; memset(&cl, 0, sizeof(cl));
            cl.kind = PSVK_QP_INFEASIBLE;
            cl.n = n; cl.m = m; cl.A = A; cl.bq = b; cl.ray = r.farkas;
            cl.gt_row = 1e-7; cl.dt_gap = 1e-9;
            if (psv_cert_check(&cl) == PSV_OK) {
                printf("PROVEN 1\n");
                if (print)
                    for (int i = 0; i < m; i++) printf("farkas[%d] = %.17g\n", i, r.farkas[i]);
            } else {
                fprintf(stderr, "psv: qp_infeasible certificate not confirmed\n");
                printf("PROVEN 0\n");
            }
        }
    }
    qp_result_free(&r);
    free(c); free(Q); free(A); free(b);
    tlimit_disarm();
    psolve_stop_set(NULL);
    psolve_frame_pop(&ef);
    return 0;

bad:
    fclose(f); free(c); free(Q); free(A); free(b);
    fprintf(stderr, "QP parse error\n");
    tlimit_disarm();
    psolve_stop_set(NULL);
    psolve_frame_pop(&ef);
    return 1;
}
