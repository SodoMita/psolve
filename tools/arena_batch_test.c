#include "solver.h"
#include "pgs.h"
#include "pgs_fixed.h"
#include "err.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

static void test_arena_zero_malloc(void)
{
    /* Allocate 1MB arena buffer */
    size_t cap = 1024 * 1024;
    char *buf = (char*)malloc(cap);
    assert(buf != NULL);

    PSolveArena arena;
    psolve_arena_init(&arena, buf, cap);

    /* Direct arena allocations */
    void *p1 = psolve_arena_alloc(&arena, 128);
    assert(p1 != NULL);
    assert(((uintptr_t)p1 % 8) == 0);

    void *p2 = psolve_arena_calloc(&arena, 10, sizeof(double));
    assert(p2 != NULL);
    for (int i = 0; i < 10; i++) assert(((double*)p2)[i] == 0.0);

    /* Test arena reset */
    psolve_arena_reset(&arena);
    assert(arena.used == 0);

    /* Test arena-routed LP solve */
    psolve_arena_use(&arena);

    LP lp;
    memset(&lp, 0, sizeof(lp));
    lp.n = 2; lp.m = 1; lp.maximize = 1;
    double c[2] = { 1.0, 2.0 };
    double l[2] = { 0.0, 0.0 };
    double u[2] = { 10.0, 10.0 };
    double b[1] = { 15.0 };
    char rel[1] = { '<' };
    int Acolptr[3] = { 0, 1, 2 };
    int Arow[2] = { 0, 0 };
    double Aval[2] = { 1.0, 1.0 };
    lp.c = c; lp.l = l; lp.u = u; lp.b = b; lp.rel = rel;
    lp.Acolptr = Acolptr; lp.Arow = Arow; lp.Aval = Aval;

    Solver *s = solver_create(&lp);
    assert(s != NULL);
    int status = solver_solve(s);
    assert(status == 0);

    double x[2], obj;
    solver_optimum(s, x, &obj);
    assert(fabs(x[0] - 5.0) < 1e-6);
    assert(fabs(x[1] - 10.0) < 1e-6);
    assert(fabs(obj - 25.0) < 1e-6);

    solver_destroy(s);
    psolve_arena_use(NULL);
    free(buf);
}

static void test_arena_oom_guard(void)
{
    char buf[64];
    PSolveArena arena;
    psolve_arena_init(&arena, buf, sizeof(buf));
    psolve_arena_use(&arena);

    if (setjmp(psolve_env) == 0) {
        psolve_try();
        /* Intentionally request more memory than the small arena */
        psolve_malloc(256);
        assert(0 && "should have signaled OOM");
    } else {
        assert(psolve_code == PSOLVE_ERR_OOM);
    }
    psolve_end();
    psolve_arena_use(NULL);
}

static void test_batch_pgs(void)
{
    /* Batch of 3 independent 2D physics contact systems */
    const int count = 3;
    PGSOptions opts[3] = {
        { 2, 20, 1.0, 1e-9 },
        { 2, 20, 1.0, 1e-9 },
        { 2, 20, 1.0, 1e-9 }
    };
    double A0[4] = { 2.0, 0.5, 0.5, 2.0 };
    double b0[2] = { -3.0, -2.0 };
    double lo0[2] = { 0.0, 0.0 };
    double hi0[2] = { 10.0, 10.0 };
    double x0[2] = { 0.0, 0.0 };

    double A1[4] = { 3.0, 0.0, 0.0, 4.0 };
    double b1[2] = { -6.0, -8.0 };
    double lo1[2] = { 0.0, 0.0 };
    double hi1[2] = { 5.0, 5.0 };
    double x1[2] = { 0.0, 0.0 };

    double A2[4] = { 1.0, 0.0, 0.0, 1.0 };
    double b2[2] = { -1.0, -1.0 };
    double lo2[2] = { 0.0, 0.0 };
    double hi2[2] = { 0.5, 0.5 };
    double x2[2] = { 0.0, 0.0 };

    const double *A_arr[3] = { A0, A1, A2 };
    const double *b_arr[3] = { b0, b1, b2 };
    const double *lo_arr[3] = { lo0, lo1, lo2 };
    const double *hi_arr[3] = { hi0, hi1, hi2 };
    double *x_arr[3] = { x0, x1, x2 };
    PGSResult res_arr[3];

    long flops = pgs_batch_solve(count, opts, A_arr, b_arr, lo_arr, hi_arr, x_arr, res_arr);
    assert(flops > 0);

    for (int k = 0; k < count; k++) {
        assert(res_arr[k].status == 0);
    }
    assert(fabs(x0[0] - (4.0 / 3.0)) < 1e-5 && fabs(x0[1] - (2.0 / 3.0)) < 1e-5);
    assert(fabs(x1[0] - 2.0) < 1e-5 && fabs(x1[1] - 2.0) < 1e-5);
    assert(fabs(x2[0] - 0.5) < 1e-5 && fabs(x2[1] - 0.5) < 1e-5);
}

static void test_batch_pgsf(void)
{
    /* Batch of 2 independent fixed-point physics systems (scale 256) */
    const int count = 2;
    PGSFixedOptions opts[2] = {
        { 2, 20, 1, 1, 0 },
        { 2, 20, 1, 1, 0 }
    };
    int64_t A0[4] = { 2, 0, 0, 2 };
    int64_t b0[2] = { -768, -512 }; /* -3.0 and -2.0 scaled by 256 */
    int64_t lo0[2] = { 0, 0 };
    int64_t hi0[2] = { 2560, 2560 };
    int64_t x0[2] = { 0, 0 };

    int64_t A1[4] = { 1, 0, 0, 1 };
    int64_t b1[2] = { -256, -256 };
    int64_t lo1[2] = { 0, 0 };
    int64_t hi1[2] = { 128, 128 }; /* clamped at 0.5 */
    int64_t x1[2] = { 0, 0 };

    const int64_t *A_arr[2] = { A0, A1 };
    const int64_t *b_arr[2] = { b0, b1 };
    const int64_t *lo_arr[2] = { lo0, lo1 };
    const int64_t *hi_arr[2] = { hi0, hi1 };
    int64_t *x_arr[2] = { x0, x1 };
    PGSResult res_arr[2];

    long flops = pgsf_batch_solve(count, opts, A_arr, b_arr, lo_arr, hi_arr, x_arr, res_arr);
    assert(flops > 0);

    assert(x0[0] == 384 && x0[1] == 256);
    assert(x1[0] == 128 && x1[1] == 128);
}

int main(void)
{
    test_arena_zero_malloc();
    test_arena_oom_guard();
    test_batch_pgs();
    test_batch_pgsf();
    printf("ALL ARENA & BATCH SOLVE TESTS PASSED\n");
    return 0;
}
