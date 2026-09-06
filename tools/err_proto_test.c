/* Error-protocol test suite (roadmap 6.3): the PSolveErrFrame /
 * psolve_fail protocol after retiring the process-global
 * setjmp/active/code triple (AUDIT.md "Not done" #2).
 *
 * Single-thread coverage:
 *   1. basic fail -> recover, correct code via psolve_err_code();
 *   2. real allocation failure (malloc(SIZE_MAX)) recovers with OOM;
 *   3. psolve_realloc failure NULLs *p (no leak of the old block) and
 *      recovers with OOM;
 *   4. psolve_calloc size-overflow guard recovers with OOM;
 *   5. NESTED frames: the inner frame catches the first failure, the
 *      outer frame is still installed and catches the second (the
 *      retired global protocol refused nesting outright -- psolve_try()
 *      returned "already installed");
 *   6. re-pushing a fresh frame inside a recovery handler works;
 *   7. psolve_err_code() defaults to PSOLVE_OK before any failure;
 *   8. forked child WITHOUT a frame exits with the failure code (the
 *      documented no-handler behavior), and its message is printed;
 *   9. forked child popping a non-innermost frame aborts (checked
 *      protocol violation).
 *
 * Discrimination (calibration rule): this tool cannot build against the
 * pre-change err.h (PSolveErrFrame/psolve_frame_push/psolve_err_code do
 * not exist there) -- compile failure on the old tree is the
 * pre-change FAIL, and the runtime checks pin the new semantics.
 */

#include "err.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>

static int failures;

#define CHECK(cond, msg) do { \
    if (cond) printf("ok: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); failures++; } \
} while (0)

/* Real malloc failure that glibc reports deterministically. */
static int try_bad_malloc(void)
{
    PSolveErrFrame ef;
    psolve_frame_push(&ef);
    if (setjmp(ef.env) != 0) {
        return psolve_err_code();       /* frame already popped */
    }
    void *p = psolve_malloc((size_t)-1);
    psolve_frame_pop(&ef);
    free(p);                            /* unreachable if NULL */
    return -1;                          /* no failure happened */
}

int main(void)
{
    /* 7. default code */
    CHECK(psolve_err_code() == PSOLVE_OK, "err_code defaults to PSOLVE_OK");

    /* 1. basic fail -> recover, correct code */
    {
        PSolveErrFrame ef;
        psolve_frame_push(&ef);
        volatile int recovered = 0, code = -1;
        if (setjmp(ef.env) != 0) {
            recovered = 1;
            code = psolve_err_code();
        } else {
            psolve_fail(PSOLVE_ERR_SOLVE);
            psolve_frame_pop(&ef);      /* unreachable */
        }
        CHECK(recovered && code == PSOLVE_ERR_SOLVE,
              "psolve_fail(SOLVE) unwinds to innermost frame with that code");
    }

    /* 2. real allocation failure */
    CHECK(try_bad_malloc() == PSOLVE_ERR_OOM,
          "psolve_malloc(SIZE_MAX) unwinds with PSOLVE_ERR_OOM");

    /* 3. realloc failure NULLs *p, frees the old block, code OOM */
    {
        void *p = psolve_malloc(64);
        CHECK(p != NULL, "baseline psolve_malloc(64) succeeds");
        PSolveErrFrame ef;
        psolve_frame_push(&ef);
        volatile int recovered = 0, code = -1;
        if (setjmp(ef.env) != 0) {
            recovered = 1;
            code = psolve_err_code();
        } else {
            void *q = psolve_realloc(&p, (size_t)-1);
            (void)q;
            psolve_frame_pop(&ef);      /* unreachable */
        }
        CHECK(recovered && code == PSOLVE_ERR_OOM && p == NULL,
              "psolve_realloc(SIZE_MAX) unwinds with OOM and *p == NULL");
        /* p is NULL after the failure path: nothing to free. */
    }

    /* 4. calloc overflow guard */
    {
        PSolveErrFrame ef;
        psolve_frame_push(&ef);
        volatile int recovered = 0, code = -1;
        if (setjmp(ef.env) != 0) {
            recovered = 1;
            code = psolve_err_code();
        } else {
            void *p = psolve_calloc((size_t)-1 / 2 + 1, 2);
            free(p);
            psolve_frame_pop(&ef);      /* unreachable */
        }
        CHECK(recovered && code == PSOLVE_ERR_OOM,
              "psolve_calloc(n*sz overflow) unwinds with PSOLVE_ERR_OOM");
    }

    /* 5. NESTED frames: inner catches first, outer catches second.  The
       retired global protocol refused a second handler (psolve_try()
       returned 1), so this is the semantic novelty of the redesign.
       Scalars written between an outer setjmp and the longjmp that lands
       past it are declared volatile (C11 7.13.2.1: only then is their
       value defined after the unwind). */
    {
        PSolveErrFrame outer, inner;
        volatile int inner_hit = 0, outer_hit = 0;
        volatile int icode = -1, ocode = -1;

        psolve_frame_push(&outer);
        if (setjmp(outer.env) != 0) {
            outer_hit = 1;
            ocode = psolve_err_code();
            goto outer_done;            /* outer already popped */
        }

        psolve_frame_push(&inner);
        if (setjmp(inner.env) != 0) {
            /* inner caught the first failure; inner is already popped, and
               OUTER must still be armed: a second failure lands there. */
            inner_hit = 1;
            icode = psolve_err_code();
            psolve_fail(PSOLVE_ERR_OOM);
            /* unreachable: the outer frame catches this one */
            printf("FAIL: psolve_fail with armed outer frame returned\n");
            failures++;
        }
        /* straight-line flow with inner armed: this failure is INNER's. */
        psolve_fail(PSOLVE_ERR_SOLVE);
        psolve_frame_pop(&inner);       /* unreachable: fail never returns */

outer_done:
        CHECK(inner_hit && icode == PSOLVE_ERR_SOLVE,
              "nested frames: inner caught the first failure with its code");
        CHECK(outer_hit && ocode == PSOLVE_ERR_OOM,
              "nested frames: outer still armed, caught the second failure");
    }

    /* 6. push inside a recovery handler */
    {
        PSolveErrFrame ef1, ef2;
        volatile int first = 0, second = 0, code2 = -1;
        psolve_frame_push(&ef1);
        if (setjmp(ef1.env) != 0) {
            first = 1;
            /* recovery pushes its own frame and fails into it */
            psolve_frame_push(&ef2);
            if (setjmp(ef2.env) != 0) {
                second = 1;
                code2 = psolve_err_code();
            } else {
                psolve_fail(PSOLVE_ERR_OOM);
                psolve_frame_pop(&ef2);  /* unreachable */
            }
        } else {
            psolve_fail(PSOLVE_ERR_SOLVE);
            psolve_frame_pop(&ef1);      /* unreachable */
        }
        CHECK(first && second && code2 == PSOLVE_ERR_OOM,
              "recovery handler may push a fresh frame and fail into it");
    }

    /* 8. no frame -> exit(code), with message (child process) */
    {
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {
            /* no frame armed anywhere on this thread */
            psolve_malloc((size_t)-1);   /* fn prints + exit(PSOLVE_ERR_OOM) */
            _exit(0);                    /* FAIL if we get here */
        }
        int st = 0;
        waitpid(pid, &st, 0);
        CHECK(WIFEXITED(st) && WEXITSTATUS(st) == PSOLVE_ERR_OOM,
              "failure without a frame exits with the failure code");
    }

    /* 9. pop of a non-innermost frame aborts (child process) */
    {
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {
            PSolveErrFrame a, b;
            psolve_frame_push(&a);
            psolve_frame_push(&b);
            psolve_frame_pop(&a);        /* protocol violation: b is inner */
            _exit(0);                    /* FAIL if we get here */
        }
        int st = 0;
        waitpid(pid, &st, 0);
        CHECK(WIFSIGNALED(st) && WTERMSIG(st) == SIGABRT,
              "popping a non-innermost frame aborts (checked violation)");
        /* also: correct pop order is accepted in-process */
        PSolveErrFrame a, b;
        psolve_frame_push(&a);
        psolve_frame_push(&b);
        psolve_frame_pop(&b);
        psolve_frame_pop(&a);
        CHECK(1, "LIFO pop order is accepted");
    }

    if (failures) { fprintf(stderr, "err_proto_test: %d FAILURES\n", failures); return 1; }
    printf("err_proto_test: all checks passed\n");
    return 0;
}
