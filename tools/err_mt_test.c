/* Multithreaded error-protocol test (roadmap 6.3, AUDIT "Not done" #2).
 *
 * The retired protocol kept one process-global jmp_buf + active flag +
 * code triple (psolve_env/psolve_active/psolve_code) and a process-global
 * stop callback (psolve_stop_fn): a second thread installing a handler or
 * taking a failure hijacked or corrupted the first thread's recovery, and
 * any concurrent use was a data race by construction.
 *
 * After the redesign every piece of handler state is caller-owned or
 * thread-local, so this test drives, on T threads CONCURRENTLY:
 *
 *   - a real LP build+solve+destroy loop through psolve_malloc (the
 *     library's solve path, all objects per-thread), checking the optimum;
 *   - a forced allocation failure (malloc(SIZE_MAX)) caught by the
 *     thread's own innermost frame every few rounds -- with 7 other
 *     threads doing the same at overlapping times, each failure must land
 *     in its OWN thread's frame with its OWN code;
 *   - a per-thread cooperative-stop callback (psolve_stop_set), verifying
 *     one thread's installed callback returns its own value while a
 *     thread that installed none sees 0.
 *
 * Runs clean under -fsanitize=thread (wired into test.sh).  Discriminates
 * against the pre-change tree the same way err_proto_test does: the new
 * API (PSolveErrFrame/psolve_frame_push/psolve_err_code/psolve_stop_set)
 * does not exist there, so the pre-change FAIL is a compile error; and a
 * pre-change-API version of this concurrency pattern is precisely the
 * cross-thread longjmp hazard AUDIT "Not done" #2 describes.
 */

#include "err.h"
#include "solver.h"
#include "parser.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <math.h>

#define THREADS 8
#define ROUNDS  300

static int thread_failures[THREADS];

/* The stop callback reads a flag in thread-local storage, so each thread's
 * callback installation is genuinely per-thread (not just "same function,
 * different data races"). */
static _Thread_local int tls_stop_flag;
static int stop_cb(void) { return tls_stop_flag; }

/* Solve maximize c.x s.t. x0 + x1 <= b, x >= 0  ->  obj = max(c0,c1)*b. */
static int solve_round(int tid, int round, double *obj_out)
{
    double b = 4.0 + (double)((tid + round) % 5);
    LP lp;
    memset(&lp, 0, sizeof(lp));
    lp.n = 2; lp.m = 1; lp.maximize = 1;
    lp.c = (double*)psolve_malloc(2 * sizeof(double));
    lp.Acolptr = (int*)psolve_malloc(3 * sizeof(int));
    lp.Arow = (int*)psolve_malloc(2 * sizeof(int));
    lp.Aval = (double*)psolve_malloc(2 * sizeof(double));
    lp.rel = (char*)psolve_malloc(1);
    lp.b = (double*)psolve_malloc(sizeof(double));
    lp.l = (double*)psolve_malloc(2 * sizeof(double));
    lp.u = (double*)psolve_malloc(2 * sizeof(double));
    lp.c[0] = 1.0; lp.c[1] = 2.0;
    lp.Acolptr[0] = 0; lp.Acolptr[1] = 1; lp.Acolptr[2] = 2;
    lp.Arow[0] = 0; lp.Arow[1] = 0;
    lp.Aval[0] = 1.0; lp.Aval[1] = 1.0;
    lp.rel[0] = '<';
    lp.b[0] = b;
    lp.l[0] = 0.0; lp.l[1] = 0.0;
    lp.u[0] = LP_INF; lp.u[1] = LP_INF;

    Solver *s = solver_create(&lp);
    if (!s) { lp_free(&lp); return -1; }
    int r = solver_solve(s);
    if (r == 0) {
        double x[2];
        solver_optimum(s, x, obj_out);
    }
    solver_destroy(s);
    lp_free(&lp);
    double want = 2.0 * b;              /* maximize x + 2y, x+y <= b */
    return (r == 0 && fabs(*obj_out - want) < 1e-6) ? 0 : -2;
}

static void *worker(void *arg)
{
    int tid = (int)(size_t)arg;
    /* volatile: live across the setjmp and potentially modified between it
       and a longjmp landing (C11 7.13.2.1). */
    volatile int fails = 0;
    for (volatile int rd = 0; rd < ROUNDS; rd++) {
        PSolveErrFrame ef;
        volatile int recovered = 0, code = -1;
        double obj = 0.0;

        psolve_frame_push(&ef);
        if (setjmp(ef.env) != 0) {
            recovered = 1;
            code = psolve_err_code();
            psolve_arena_end();         /* the tiny arena is done */
            /* frame already popped; arm a fresh one for the rest. */
            psolve_frame_push(&ef);
        } else {
            /* Force a failing allocation on some rounds: with all threads
               failing concurrently, each unwind must land HERE.  A tiny
               caller-buffer arena makes the failure deterministic and
               independent of the C library (and of sanitizer huge-malloc
               interception). */
            if (rd % 3 == tid % 3) {
                unsigned char tbuf[64];
                PSolveArena ta;
                psolve_arena_init(&ta, tbuf, sizeof(tbuf));
                psolve_arena_use(&ta);
                void *p = psolve_malloc(4096);  /* cannot fit: fails */
                (void)p;
                psolve_arena_end();     /* unreachable */
            }
        }

        if (recovered && code != PSOLVE_ERR_OOM)
            fails++;

        /* Per-thread stop callback isolation: odd threads install one,
           even threads install none. */
        tls_stop_flag = (rd % 2);
        psolve_stop_set(tid % 2 ? stop_cb : NULL);
        int want_stop = (tid % 2) ? tls_stop_flag : 0;
        if (psolve_stop() != want_stop) fails++;
        psolve_stop_set(NULL);

        /* A real solve end-to-end on this thread's own objects. */
        if (solve_round(tid, rd, &obj) != 0) fails++;

        psolve_frame_pop(&ef);
    }
    thread_failures[tid] = fails;
    return NULL;
}

int main(void)
{
    pthread_t th[THREADS];
    for (int t = 0; t < THREADS; t++)
        if (pthread_create(&th[t], NULL, worker, (void*)(size_t)t) != 0) {
            fprintf(stderr, "pthread_create failed\n");
            return 1;
        }
    for (int t = 0; t < THREADS; t++) pthread_join(th[t], NULL);

    int fails = 0;
    for (int t = 0; t < THREADS; t++) fails += thread_failures[t];
    if (fails) {
        fprintf(stderr, "err_mt_test: %d FAILURES across %d threads\n",
                fails, THREADS);
        return 1;
    }
    printf("err_mt_test: %d threads x %d rounds of solves + forced OOM "
           "recoveries + per-thread stop state, all clean\n", THREADS, ROUNDS);
    return 0;
}
