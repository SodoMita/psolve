/* FlatZinc + MIP coverage extension for the re-entrant thread-local arena
 * (Phase 4, tools/arena_test.c covers QP/LP/fx).
 *
 * The arena port routed src/fzn.c, src/fz_cp.inc, src/mip.c, src/solver.c and
 * src/parser.c through psolve_malloc/calloc/realloc/free.  A single missed
 * site means a libc free() on arena-owned memory (heap corruption) or a libc
 * malloc inside a supposedly zero-heap solve; this test makes both loud:
 *
 *   1. For every FlatZinc reference model (CP engine, table, cumulative,
 *      optimization/MIP-bridge), fz_solve under an active arena must give the
 *      SAME verdict and byte-identical objective/solution as without it.
 *   2. While the arena is active the libc heap-call counter (via
 *      --wrap=malloc/calloc/realloc/free) must not move during fz_solve /
 *      mip_solve -- every library allocation must come from the arena.
 *      (__wrap_free forwards to libc free: a leaked arena pointer reaching it
 *      aborts in glibc -- "free(): invalid pointer" -- and ASan flags it in
 *      the sanitizer build, so missed free-routing cannot pass silently.)
 *   3. The full embedding cycle read->solve->free inside one arena scope, then
 *      reset and re-use, runs cleanly (arena-contract patterns).
 *   4. A direct mip_solve (knapsack, exercises FBBT + branch-and-bound + the
 *      exact-rational fallback path) matches arena vs non-arena exactly.
 *
 * usage: arena_fzn_test [fzn_examples_dir]   (default examples/fzn)
 * exits 0 on success, 1 on failure.
 */

#include "fzn.h"
#include "mip.h"
#include "err.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- libc heap call counter via --wrap (same scheme as arena_test.c) ---- */
#ifdef ARENA_TEST_WRAP
extern void *__real_malloc(size_t n);
extern void *__real_calloc(size_t n, size_t sz);
extern void *__real_realloc(void *p, size_t n);
extern void  __real_free(void *p);

static long g_heap_calls = 0;
void *__wrap_malloc(size_t n) { g_heap_calls++; return __real_malloc(n); }
void *__wrap_calloc(size_t n, size_t sz) { g_heap_calls++; return __real_calloc(n, sz); }
void *__wrap_realloc(void *p, size_t n) { g_heap_calls++; return __real_realloc(p, n); }
void  __wrap_free(void *p) { __real_free(p); }  /* arena ptr here -> glibc abort */
#define HEAP_CALLS() (g_heap_calls)
#else
#define HEAP_CALLS() (0L)
#endif

static int g_fail = 0;
static void check(int ok, const char *what, const char *model)
{
    if (ok) printf("ok: %s [%s]\n", what, model);
    else { printf("FAIL: %s [%s]\n", what, model); g_fail = 1; }
}

/* 64 MiB arena: comfortably covers the MIP materializations (SOS1 expansions,
   big-M relaxations) of the reference models. */
static _Alignas(64) char g_buf[64u * 1024u * 1024u];

static void run_fzn(const char *dir, const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.fzn", dir, name);

    /* Reference: read + solve entirely outside any arena. */
    FZModel m;
    memset(&m, 0, sizeof m);
    if (fz_read(path, &m) != 0) { check(0, "fz_read reference", name); return; }
    FZSolution ref;
    memset(&ref, 0, sizeof ref);
    fz_solve(&m, &ref);

    /* Pattern 1: model owned by libc, solve inside the arena. */
    PSolveArena a;
    psolve_arena_init(&a, g_buf, sizeof g_buf);
    psolve_arena_use(&a);
    long h0 = HEAP_CALLS();
    FZSolution s2;
    memset(&s2, 0, sizeof s2);
    fz_solve(&m, &s2);
    check(HEAP_CALLS() == h0, "fz_solve under arena made ZERO libc heap calls", name);
    check(s2.status == ref.status, "verdict identical inside arena", name);
    if (s2.status == 0 && (m.solve_kind == 1 || m.solve_kind == 2)) {
        check(s2.obj == ref.obj, "objective bit-identical inside arena", name);
    }
    if (s2.status == 0 && s2.x && ref.x) {
        int same = 1;
        for (int i = 0; i < ref.nvars; i++)
            if (s2.x[i] != ref.x[i]) { same = 0; break; }
        check(same, "solution vector bit-identical inside arena", name);
    }
    fz_solution_free(&s2);          /* inside the scope: no-op for arena blocks */
    psolve_arena_end();
    psolve_arena_reset(&a);

    /* Pattern 2: the whole embedding cycle (read -> solve -> free) inside one
       arena scope, then reset and re-use the buffer. */
    psolve_arena_use(&a);
    long h1 = HEAP_CALLS();
    FZModel m2;
    memset(&m2, 0, sizeof m2);
    int rd = fz_read(path, &m2);
    check(rd == 0, "fz_read under arena", name);
    if (rd == 0) {
        check(HEAP_CALLS() == h1, "fz_read under arena made ZERO libc heap calls", name);
        FZSolution s3;
        memset(&s3, 0, sizeof s3);
        fz_solve(&m2, &s3);
        check(s3.status == ref.status, "verdict identical (arena-owned model)", name);
        fz_solution_free(&s3);
        fz_model_free(&m2);         /* arena-owned: reclaimed at reset */
    }
    psolve_arena_end();
    psolve_arena_reset(&a);
    if (a.used != 0) { check(0, "arena reset leaves used==0", name); }

    fz_solution_free(&ref);
    fz_model_free(&m);              /* libc-owned model, freed outside arena */
}

static void run_mip_knapsack(void)
{
    /* maximize 10x0+7x1+25x2+24x3 st 2x0+x1+6x2+5x3<=7, xi in {0,1}
       optimum 34: items {0,3} (weight 7); {0,2}=35 would need weight 8 > 7. */
    enum { N = 4 };
    /* CSC by column: x0:(row0,2) x1:(row0,1) x2:(row0,6) x3:(row0,5) */
    int    colptr[N + 1] = { 0, 1, 2, 3, 4 };
    int    rows[4]       = { 0, 0, 0, 0 };
    double vals[4]       = { 2, 1, 6, 5 };
    char   rel[1]        = { '<' };
    double rhs[1]        = { 7 };
    double c[N]          = { 10, 7, 25, 24 };
    double lo[N]         = { 0, 0, 0, 0 };
    double hi[N]         = { 1, 1, 1, 1 };
    unsigned char isint[N] = { 1, 1, 1, 1 };

    MIP mip;
    memset(&mip, 0, sizeof mip);
    mip.n = N; mip.m = 1;
    mip.c = c; mip.Acolptr = colptr; mip.Arow = rows; mip.Aval = vals;
    mip.rel = rel; mip.b = rhs; mip.l = lo; mip.u = hi;
    mip.maximize = 1; mip.isint = isint;

    MIPResult ref;
    memset(&ref, 0, sizeof ref);
    mip_solve(&mip, &ref);
    check(ref.status == 0 && ref.obj == 34.0, "MIP baseline optimum 34", "knapsack");

    PSolveArena a;
    psolve_arena_init(&a, g_buf, sizeof g_buf);
    psolve_arena_use(&a);
    long h0 = HEAP_CALLS();
    MIPResult s2;
    memset(&s2, 0, sizeof s2);
    mip_solve(&mip, &s2);
    check(HEAP_CALLS() == h0, "mip_solve under arena made ZERO libc heap calls", "knapsack");
    check(s2.status == ref.status && s2.obj == ref.obj, "MIP result identical inside arena", "knapsack");
    if (s2.x && ref.x) {
        int same = 1;
        for (int i = 0; i < N; i++) if (s2.x[i] != ref.x[i]) { same = 0; break; }
        check(same, "MIP solution bit-identical inside arena", "knapsack");
    }
    mip_result_free(&s2);           /* inside scope */
    psolve_arena_end();
    psolve_arena_reset(&a);
    mip_result_free(&ref);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "examples/fzn";

    /* CP engine, table handler, cumulative globals, optimization bridge --
       the port's conversion surface.  Mix of satisfy and optimize, CP and MIP
       paths, SAT and UNSAT reference outcomes. */
    static const char *models[] = {
        "satisfy_lin",     /* LP bridge satisfy            */
        "mip_max",         /* MIP maximize                 */
        "makespan",        /* bigger MIP scheduling        */
        "table_sat",       /* table encoding (SAT)         */
        "table_unsat",     /* table encoding (UNSAT)       */
        "table_opt",       /* table + optimization         */
        "cumulative_sat",  /* global + big-M (SAT)         */
        "cumulative_unsat",/* global + big-M (UNSAT)       */
        "cumulative_exact",/* exact-rational fallback path */
        "circuit",         /* CP: circuit/all_different    */
        "max_lin",         /* CP B&B optimization          */
        "min_lin",         /* CP B&B minimization          */
        "bool_logic",      /* bool reification handlers    */
        "float_lin",       /* float rows through the bridge*/
    };
    for (size_t i = 0; i < sizeof models / sizeof models[0]; i++)
        run_fzn(dir, models[i]);

    run_mip_knapsack();

    if (g_fail) { printf("arena_fzn_test: FAILURES\n"); return 1; }
    printf("all arena_fzn_test checks passed\n");
    return 0;
}
