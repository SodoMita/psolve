#include "qp.h"
#include "solver.h"
#include "parser.h"
#include "mip.h"
#include "fx.h"
#include "err.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

/* Regression test for the re-entrant, thread-local preallocated arena
 * (Phase 4: zero-malloc per-frame solves).
 *
 * Link with --wrap=malloc --wrap=calloc --wrap=realloc so every libc heap call
 * goes through __real_* and is counted.  While an arena is active the solve
 * paths must route all allocation through psolve_malloc/calloc/realloc, which
 * bump-allocate from the arena -- so the libc heap-call counter must not move.
 *
 * Verifies:
 *   1. QP / LP / exact-fx solves inside an arena give the SAME objective as
 *      the same solves without an arena.
 *   2. While the arena is active, libc malloc/calloc/realloc are NOT called by
 *      the solve path (heap counter unchanged).
 *   3. Arena alloc/free/realloc ownership is correct -- a non-arena pointer
 *      realloc'd under an active arena falls back to libc (no header misread).
 *   4. The arena is re-entrant: nested use()/end() scopes do not interfere,
 *      and the outer solve is still correct after an inner scope closes.
 *   5. Reset reuses the buffer (used returns to 0).
 *
 * usage: arena_test
 * exits 0 on success, 1 on failure.
 */

/* ---- libc heap call counter via --wrap ---- */
/* Define ARENA_TEST_WRAP when linking with --wrap=malloc,calloc,realloc,free
 * (test.sh).  For a plain ASan/UBSan memory-safety build, omit it: the heap
 * count is then a no-op but the correctness/ownership/reset checks still run. */
#ifdef ARENA_TEST_WRAP
extern void *__real_malloc(size_t n);
extern void *__real_calloc(size_t n, size_t sz);
extern void *__real_realloc(void *p, size_t n);
extern void  __real_free(void *p);

static long g_heap_calls = 0;

/* Counting is always on: every libc heap call is tallied.  A solve running
 * under an active arena must leave g_heap_calls unchanged (all its psolve_*
 * allocations bump-allocate from the arena). */
void *__wrap_malloc(size_t n) { g_heap_calls++; return __real_malloc(n); }
void *__wrap_calloc(size_t n, size_t sz) { g_heap_calls++; return __real_calloc(n, sz); }
void *__wrap_realloc(void *p, size_t n) { g_heap_calls++; return __real_realloc(p, n); }
void  __wrap_free(void *p) { __real_free(p); }

#define HEAP_CALLS()   (g_heap_calls)
#else
static long g_heap_calls = 0;
#define HEAP_CALLS()   (g_heap_calls)
#endif

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } \
    else { printf("ok: %s\n", msg); } \
} while (0)

/* ------------------------------------------------------------------ */

static void build_qp(QP *qp)
{
    static double Q[4] = {1,0,0,1};   /* 0.5 (x0^2 + x1^2) */
    static double c[2] = {0,0};
    static double A[2] = {-1,-1};     /* -(x0+x1) <= -1  =>  x0+x1 >= 1 */
    static double b[1] = {-1};
    qp->n = 2; qp->m = 1; qp->Q = Q; qp->c = c; qp->A = A; qp->b = b; qp->x0 = NULL;
}

/* Run a solve with a heap-call window that covers ONLY the solve call, not
 * parsing (file parsing legitimately uses stdio).  Returns 1 if the solve
 * made zero libc heap calls while the arena was active, 0 otherwise. */
static int window_check_zero(const char *name, long before)
{
    long after = HEAP_CALLS();
    if (after != before) {
        printf("FAIL: %s under arena made %ld libc heap calls\n", name, after - before);
        failures++;
        return 0;
    }
    printf("ok: %s under arena made ZERO libc heap calls\n", name);
    return 1;
}

static double solve_qp_under(PSolveArena *arena, int *zero_heap)
{
    QP qp; build_qp(&qp);
    QPResult r; memset(&r, 0, sizeof(r));
    long before = HEAP_CALLS();
    qp_solve(&qp, &r);
    double obj = (r.status == 0) ? r.obj : NAN;
    if (arena) *zero_heap = window_check_zero("QP solve", before);
    qp_result_free(&r);
    return obj;
}

static double solve_lp_under(PSolveArena *arena, int *zero_heap)
{
    LP lp; memset(&lp, 0, sizeof(LP));
    /* parsing is outside the counted window (uses stdio) */
    if (lp_read("examples/prodplan.lp", &lp) != 0) { *zero_heap = 0; return NAN; }
    Solver *s = solver_create(&lp);
    long before = HEAP_CALLS();
    int r = solver_solve(s);
    double obj = 0.0;
    if (r == 0) {
        double xo_stk[64];   /* stack buffer, not a heap call */
        solver_optimum(s, xo_stk, &obj);
    }
    if (arena) *zero_heap = window_check_zero("LP solve", before);
    solver_destroy(s);
    lp_free(&lp);
    return obj;
}

static double solve_fx_under(PSolveArena *arena, int *zero_heap)
{
    FxLP flp; memset(&flp, 0, sizeof(flp));
    FxResult res; memset(&res, 0, sizeof(res));
    /* parsing outside the counted window (stdio) */
    if (fx_read("examples/prodplan.lp", &flp) != 0) { *zero_heap = 0; return NAN; }
    long before = HEAP_CALLS();
    fx_solve(&flp, &res);
    double obj = (res.status == FX_OPTIMAL) ? (double)res.obj.num / (double)res.obj.den : NAN;
    if (arena) *zero_heap = window_check_zero("exact-fx solve", before);
    fx_result_free(&res);
    fx_free(&flp);
    return obj;
}

static int run_in_arena(PSolveArena *arena, double (*solve)(PSolveArena*, int*), double base)
{
    int zh = 0;
    if (arena) psolve_arena_use(arena);
    double obj = solve(arena, &zh);
    if (arena) psolve_arena_end();
    CHECK(base == obj, "objective identical inside arena");
    if (arena) CHECK(zh == 1, "zero-libc-heap-call check ran");
    return obj == base;
}

int main(void)
{
    /* ---- baseline (no arena) ---- */
    double qp_base = solve_qp_under(NULL, &(int){0});
    double lp_base = solve_lp_under(NULL, &(int){0});
    double fx_base = solve_fx_under(NULL, &(int){0});
    CHECK(isfinite(qp_base) && isfinite(lp_base) && isfinite(fx_base), "baseline solves succeed");
    /* Roadmap 7.5: solver_create now Ruiz-equilibrates the working image by
       default, so the double engine's reconstructed objective is the true
       26 only up to funnel-composition rounding (1 ulp observed on
       prodplan: 26.000000000000004) - exactly what the certification
       margins already allow.  The pin's intent (LP baseline reaches the
       known optimum) is kept with a narrow ulp window; arena-vs-libc
       bit-identity is separately asserted unchanged by run_in_arena.  The
       fx oracle below stays EXACT: the rational engine does not round. */
    CHECK(fabs(lp_base - 26.0) <= 8 * DBL_EPSILON * 26.0,
          "LP baseline objective 26 (+- ulp window, 7.5 scaled default)");
    CHECK(fx_base == 26.0, "fx baseline objective 26");

    /* ---- QP / LP / fx inside an arena ---- */
    unsigned char buf[1 << 20];
    PSolveArena arena;
    psolve_arena_init(&arena, buf, sizeof(buf));
    run_in_arena(&arena, solve_qp_under, qp_base);
    CHECK(arena.used > 0, "arena consumed memory during the solve");

    psolve_arena_reset(&arena);
    CHECK(arena.used == 0, "arena reset reuses buffer (used==0)");

    run_in_arena(&arena, solve_lp_under, lp_base);
    psolve_arena_reset(&arena);

    run_in_arena(&arena, solve_fx_under, fx_base);
    psolve_arena_reset(&arena);

    /* ---- re-entrancy: nested scopes ---- */
    psolve_arena_use(&arena);
    {
        unsigned char ibuf[1 << 16];
        PSolveArena inner;
        psolve_arena_init(&inner, ibuf, sizeof(ibuf));
        psolve_arena_use(&inner);
        int zh = 0;
        double o = solve_qp_under(&inner, &zh);
        CHECK(qp_base == o, "nested inner-arena QP still correct");
        CHECK(zh == 1, "nested inner-arena QP zero-heap");
        psolve_arena_end();
    }
    {
        int zh = 0;
        double o3 = solve_qp_under(&arena, &zh);
        CHECK(qp_base == o3, "outer arena still correct after nested scope");
        CHECK(zh == 1, "outer arena QP zero-heap after nested scope");
    }
    psolve_arena_end();

    /* ---- ownership: non-arena pointer realloc'd under an active arena ---- */
    psolve_arena_use(&arena);
    {
        void *ext = malloc(8);            /* libc pointer */
        long before = HEAP_CALLS();
        psolve_realloc(&ext, 32);          /* must fall back to libc, not read ext as arena-owned */
        long after = HEAP_CALLS();
        CHECK(after > before, "non-arena pointer realloc under arena uses libc (no header misread)");
        psolve_free(ext);                  /* falls back to libc free */
    }
    psolve_arena_end();

    /* ---- too-small arena reports OOM cleanly (no crash) ---- */
    {
        unsigned char tiny[64];
        PSolveArena ta; psolve_arena_init(&ta, tiny, sizeof(tiny));
        /* A solve that needs more than 64 bytes should fail via psolve_fail.
           Without a handler installed it exits; here we install one so we can
           assert a clean error path rather than a crash. */
        PSolveErrFrame ef;
        psolve_frame_push(&ef);
        if (setjmp(ef.env) != 0) {
            printf("ok: undersized arena fails cleanly (psolve_fail)\n");
            /* frame already popped by psolve_fail() */
            return (failures) ? 1 : 0;
        }
        psolve_arena_use(&ta);
        solve_qp_under(&ta, &(int){0});
        printf("FAIL: undersized arena did not report OOM\n");
        failures++;
        psolve_frame_pop(&ef);
    }

    if (failures) { fprintf(stderr, "%d FAILURES\n", failures); return 1; }
    printf("all arena_test checks passed\n");
    return 0;
}
