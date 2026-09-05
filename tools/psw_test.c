/* psw_test.c -- host-side test for the psolve_web.c bridge.
 *
 * It is deliberately written the way a front end uses the bridge (flat
 * buffers, one status int, no solver internals), because that is the layer
 * where the mistakes live: a warm start that is silently ignored, an
 * "INFEASIBLE" label attached to a solver give-up, a solve that cannot be
 * bounded by a frame budget.  Compile it against any libc and it runs natively;
 * tools/wasm_build.sh builds the same checks inside the wasm module.
 *
 *   cc -I src tools/psw_test.c tools/psolve_web.c src/{qp,solver,splu,lu,kernels,err}.c -lm
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "solver.h"
#include "psolve_web.h"

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("  FAIL "); \
    printf(__VA_ARGS__); printf("\n  (at %s:%d)\n", __FILE__, __LINE__); } } while (0)

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

/* A row of N boxes in a container: the shape a layout engine emits.
 *   x_0 = 0, x_{i+1} = x_i + w_i + gap, w_i >= minw, x_{N-1} + w_{N-1} <= maxw
 *   minimize  sum_i 4*(w_i - nat)^2 + 1e-7*||x||^2
 * tight=1 shrinks the container so that no assignment of widths fits, i.e. the
 * row system is infeasible by construction. */
typedef struct { int n, m, N; double *Q, *c, *A, *b, *x; } Model;

static void build(Model *M, int N, int tight)
{
    double gap = 6.0, minw = 10.0, nat = 40.0, maxw = 1.10 * (N*(nat+gap) - gap);
    if (tight) maxw = 0.5 * N * minw;
    int n = 2*N, m = 2 + 2*(N-1) + N + 1;
    M->N = N; M->n = n; M->m = m;
    M->Q = calloc((size_t)n*n, sizeof(double));
    M->c = calloc((size_t)n, sizeof(double));
    M->A = calloc((size_t)m*n, sizeof(double));
    M->b = calloc((size_t)m, sizeof(double));
    M->x = calloc((size_t)n, sizeof(double));
    int r = 0; double *Ar;
    Ar = M->A + (size_t)r*n; Ar[0] =  1; M->b[r++] = 0;
    Ar = M->A + (size_t)r*n; Ar[0] = -1; M->b[r++] = 0;
    for (int i = 0; i + 1 < N; i++) {
        Ar = M->A + (size_t)r*n; Ar[i+1] =  1; Ar[i] = -1; Ar[N+i] = -1; M->b[r++] =  gap;
        Ar = M->A + (size_t)r*n; Ar[i+1] = -1; Ar[i] =  1; Ar[N+i] =  1; M->b[r++] = -gap;
    }
    for (int i = 0; i < N; i++) { Ar = M->A + (size_t)r*n; Ar[N+i] = -1; M->b[r++] = -minw; }
    Ar = M->A + (size_t)r*n; Ar[N-1] = 1; Ar[2*N-1] = 1; M->b[r++] = maxw;
    for (int i = 0; i < N; i++) {
        M->Q[(size_t)(N+i)*n + (N+i)] += 8.0;
        M->c[N+i] += -320.0;                          /* 4 * nat * w_i */
        M->Q[(size_t)i*n + i] += 1e-7;                /* tiny pull on positions */
        M->Q[(size_t)(N+i)*n + (N+i)] += 1e-7;
    }
}

static void scaled(const Model *M, double s, Model *O)
{
    int n = M->n, m = M->m;
    build(O, M->N, 0);
    free(O->A); free(O->b); free(O->Q); free(O->c); free(O->x);
    O->A = calloc((size_t)m*n, sizeof(double)); O->b = calloc((size_t)m, sizeof(double));
    O->Q = calloc((size_t)n*n, sizeof(double)); O->c = calloc((size_t)n, sizeof(double));
    O->x = calloc((size_t)n, sizeof(double));
    for (size_t k = 0; k < (size_t)m*n; k++) O->A[k] = M->A[k];       /* rows are unit-free */
    for (int i = 0; i < m; i++) O->b[i] = M->b[i] * s;
    /* variables scale as x' = s*x, so 1/2 x'Q'x' + c'x' reproduces s^2 * obj */
    for (size_t k = 0; k < (size_t)n*n; k++) O->Q[k] = M->Q[k];          /* unchanged */
    for (int j = 0; j < n; j++) O->c[j] = M->c[j] * s;
}

static void free_m(Model *M)
{
    free(M->Q); free(M->c); free(M->A); free(M->b); free(M->x);
    memset(M, 0, sizeof *M);
}

int main(void)
{
    printf("psw bridge test (host-side view of the wasm ABI)\n");

    CHECK(psw_abi() >= 3, "psw_abi()=%d, this test needs ABI >= 3", psw_abi());

    /* [1] a feasible model solves, and the bridge reports a clean residual */
    Model M; build(&M, 8, 0);
    double obj = 0, resid = -1; int it = -1;
    int st = psw_qp_solve2(M.n, M.m, M.Q, M.c, M.A, M.b, NULL, -1.0,
                           M.x, &obj, &it, &resid);
    CHECK(st == 0, "[1] feasible solve returned %s", psw_qp_status_name(st));
    CHECK(fabs(resid) <= 1e-9, "[1] max_resid=%.3g expected ~0", resid);
    CHECK(psw_qp_proven() == 0, "[1] a solved model must not be reported as proved-infeasible");
    double obj_cold = obj;
    int it_cold = it;

    /* [2] warm start: the previous frame's answer must be accepted, not ignored */
    st = psw_qp_solve2(M.n, M.m, M.Q, M.c, M.A, M.b, M.x, -1.0,
                       M.x, &obj, &it, &resid);
    CHECK(st == 0, "[2] warm solve returned %s", psw_qp_status_name(st));
    CHECK(fabs(obj - obj_cold) <= 1e-9 * (1.0 + fabs(obj_cold)),
          "[2] warm objective %.9f differs from cold %.9f", obj, obj_cold);
    CHECK(it <= it_cold, "[2] warm start used %d iterations, cold used %d -- x0 ignored?", it, it_cold);

    /* [3] the same model in a different unit: acceptance of x0 must not depend
     * on the scale of the numbers (millimetres vs pixels vs micrometres) */
    for (int k = 0; k < 2; k++) {
        double s = k ? 1e-3 : 1e3;
        Model O; scaled(&M, s, &O);
        double ox[16]; for (int j = 0; j < O.n; j++) ox[j] = M.x[j] * s;
        st = psw_qp_solve2(O.n, O.m, O.Q, O.c, O.A, O.b, ox, -1.0,
                           O.x, &obj, &it, &resid);
        CHECK(st == 0, "[3] scale 1e%+d: warm solve returned %s", (int)log10(s), psw_qp_status_name(st));
        CHECK(fabs(obj - s*s*obj_cold) <= 1e-6 * (1.0 + fabs(s*s*obj_cold)),
              "[3] scale 1e%+d: objective %.6g, expected %.6g", (int)log10(s), obj, s*s*obj_cold);
        double worst = 0;
        for (int i = 0; i < O.m; i++) {
            double v = -O.b[i];
            for (int j = 0; j < O.n; j++) v += O.A[(size_t)i*O.n + j] * O.x[j];
            if (v > worst) worst = v;
        }
        CHECK(worst <= 1e-6 * (1.0 + fabs(s)), "[3] scale 1e%+d: %.3g of row violation at the 'optimum'",
              (int)log10(s), worst);
        free_m(&O);
    }

    /* [4] psw_qp_solve (the entry point hosts already call) still works, and
     * the helper that gates a cached start agrees with what the solver accepts */
    double x2[64], obj2 = 0; int it2 = 0;
    st = psw_qp_solve(M.n, M.m, M.Q, M.c, M.A, M.b, x2, &obj2, &it2);
    CHECK(st == 0, "[4] psw_qp_solve returned %s", psw_qp_status_name(st));
    CHECK(fabs(obj2 - obj_cold) <= 1e-9 * (1.0 + fabs(obj_cold)), "[4] objectives differ");
    CHECK(psw_qp_start_feasible(M.n, M.m, M.A, M.b, x2) == 1,
          "[4] the returned point must satisfy the rows (psw_qp_start_feasible said no)");
    double bad[64]; memcpy(bad, x2, sizeof bad); bad[M.n - 1] += 1e3;
    CHECK(psw_qp_start_feasible(M.n, M.m, M.A, M.b, bad) == 0,
          "[4] a point violating a row must be rejected as a start");

    /* [5] an infeasible model must come back as a PROOF, with certificate rows,
     * and the bridge must not need the solver's word for it */
    {
        Model T; build(&T, 8, 1);
        double lam[64]; memset(lam, 0, sizeof lam);
        st = psw_qp_solve2(T.n, T.m, T.Q, T.c, T.A, T.b, NULL, -1.0,
                           T.x, &obj, &it, &resid);
        CHECK(st == -1, "[5] infeasible model returned %s, expected NO_FEASIBLE_START",
              psw_qp_status_name(st));
        CHECK(psw_qp_proven() == 1, "[5] infeasible model was not proved -- the host cannot tell a conflict from a give-up");
        CHECK(strcmp(psw_qp_verdict_name(st, 1), "INFEASIBLE_PROVEN") == 0,
              "[5] verdict name for a proved-infeasible status");
        CHECK(strcmp(psw_qp_verdict_name(-1, 0), "NO_FEASIBLE_START") == 0,
              "[5] verdict name for a mere give-up must not say infeasible");
        int cnt = psw_qp_farkas(lam);
        CHECK(cnt == T.m, "[5] psw_qp_farkas wrote %d entries, model has %d rows", cnt, T.m);
        double colmax = 0, bt = 0, bmag = 0, lmin = 1e300;
        for (int i = 0; i < T.m; i++) if (lam[i] < lmin) lmin = lam[i];
        for (int j = 0; j < T.n; j++) {
            double sv = 0, mag = 0;
            for (int i = 0; i < T.m; i++) {
                double a = T.A[(size_t)i*T.n + j];
                sv += a * lam[i]; mag += fabs(a) * fabs(lam[i]);
            }
            if (fabs(sv) / (1.0 + mag) > colmax) colmax = fabs(sv) / (1.0 + mag);
        }
        for (int i = 0; i < T.m; i++) { bt += lam[i]*T.b[i]; bmag += fabs(lam[i]*T.b[i]); }
        CHECK(lmin >= -1e-12, "[5] certificate has a negative multiplier (%.3g)", lmin);
        CHECK(colmax <= 1e-6, "[5] A'lambda is not ~0 (worst relative %.3g)", colmax);
        CHECK(bt < -1e-9 * (1.0 + bmag), "[5] b'lambda = %.6g is not negative", bt);
        int named = 0; for (int i = 0; i < T.m; i++) if (lam[i] > 1e-8) named++;
        CHECK(named >= 2*T.N, "[5] certificate names only %d rows; the conflict here spans at least %d",
              named, 2*T.N);
        free_m(&T);
    }

    /* [6] a frame budget must bound the work AND keep the incumbent, so a host
     * can prefer "close, now" over "exact, too late" */
    {
        Model B; build(&B, 48, 0);
        double t0 = now();
        st = psw_qp_solve2(B.n, B.m, B.Q, B.c, B.A, B.b, NULL, 20.0,
                           B.x, &obj, &it, &resid);
        double el = 1e3 * (now() - t0);
        CHECK(el < 500.0, "[6] a 20 ms budget took %.0f ms -- unusable in a frame", el);
        CHECK(st == 0 || st == 6, "[6] budgeted solve returned %s; expected OPTIMAL or STOPPED",
              psw_qp_status_name(st));
        if (st == 6) {
            /* x_out holds the incumbent; resid says how far from feasible it is */
            CHECK(resid >= 0.0, "[6] negative residual on a stopped solve (%.3g)", resid);
        }
        free_m(&B);
    }

    /* [7] arena: zero-libc-heap solves, same answer */
    {
        size_t need = (size_t)16 << 20;
        char *buf = (char*)malloc(need);
        CHECK(buf != NULL, "[7] could not allocate a %zu byte arena", need);
        psw_arena_set(buf, need);
        double ax[64], obj3 = 0; int it3 = 0;
        st = psw_qp_solve(M.n, M.m, M.Q, M.c, M.A, M.b, ax, &obj3, &it3);
        CHECK(st == 0, "[7] arena solve returned %s", psw_qp_status_name(st));
        CHECK(fabs(obj3 - obj_cold) <= 1e-9 * (1.0 + fabs(obj_cold)),
              "[7] arena objective %.9f vs libc %.9f", obj3, obj_cold);
        CHECK(psw_arena_used() > 0, "[7] arena consumed nothing -- it is not being used");
        CHECK(psw_arena_used() < need, "[7] arena exhausted (%zu of %zu)", psw_arena_used(), need);
        size_t peak = psw_arena_highwater();
        CHECK(peak == psw_arena_used() || psw_arena_used() == 0,
              "[7] high-water %.0f KB disagrees with the arena's own count %.0f KB",
              peak / 1024.0, psw_arena_used() / 1024.0);
        psw_arena_reset();
        CHECK(psw_arena_used() == 0, "[7] arena not released by psw_arena_reset");
        psw_arena_set(NULL, 0);
        /* the bound must also cover the route that runs when the dense search
         * fails -- an infeasible model exercises Phase-I plus the LP core plus
         * the certificate copy, all from the same arena */
        Model T; build(&T, 8, 1);
        size_t need2 = (size_t)16 << 20;
        char *buf2 = (char*)malloc(need2);
        psw_arena_set(buf2, need2);
        double tx[64];
        st = psw_qp_solve2(T.n, T.m, T.Q, T.c, T.A, T.b, NULL, -1.0, tx, &obj, &it, &resid);
        CHECK(st == -1 && psw_qp_proven() == 1,
              "[7] arena-sized proof pass returned %s (proven=%d)", psw_qp_status_name(st), psw_qp_proven());
        CHECK(psw_arena_used() < need2, "[7] Phase-I/LP overran the arena (%zu of %zu)",
              psw_arena_used(), need2);
        CHECK(psw_arena_highwater() > 0, "[7] high-water not recorded on the Phase-I route");
        psw_arena_reset(); psw_arena_set(NULL, 0);
        free(buf); free(buf2); free_m(&T);
    }

    /* [8] the LP side of the bridge, unchanged in shape from what hosts call today */
    {
        /* maximize 3x+2y  s.t. x+y<=4, x<=3, y<=2  ->  obj 11 at (3,1) */
        int n = 2, m = 3;
        int colptr[3] = {0, 2, 4};
        int rowi[4] = {0, 1, 0, 2};
        double vals[4] = {1, 1, 1, 1};
        double c[2] = {3, 2}, b[3] = {4, 3, 2}, l[2] = {0, 0}, u[2] = {LP_INF, LP_INF};
        char rel[3] = {'<', '<', '<'};
        double xl[2], oLP = 0; int itLP = 0;
        st = psw_lp_solve(n, m, c, colptr, rowi, vals, rel, b, l, u, 1, xl, &oLP, &itLP);
        CHECK(st == 0, "[8] LP returned %d", st);
        CHECK(fabs(oLP - 11.0) <= 1e-9, "[8] LP objective %.6f, expected 11", oLP);
        CHECK(fabs(xl[0] - 3.0) <= 1e-9 && fabs(xl[1] - 1.0) <= 1e-9,
              "[8] LP point (%.6f, %.6f), expected (3, 1)", xl[0], xl[1]);
    }

    psw_free_scratch();
    free_m(&M);
    printf("%s: %d checks, %d failures\n", fails ? "FAILED" : "psw_test", checks, fails);
    return fails ? 1 : 0;
}
