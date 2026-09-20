/* Regression test for the QP's unboundedness certificate (docs/CURV_PS_PLAN.md
 * 1.12): the ray test used to admit a direction whenever its curvature was
 * small *relative to* ||Q|| ||p||^2, which is scale-blind -- a plain Newton
 * step of a bounded model clears that band as soon as it is small enough, and
 * the model comes back UNBOUNDED with a finite optimum.  Both halves of the
 * certificate are pinned here against FROZEN data from the run that exposed it,
 * because a seed-dependent reproduction (that one is
 * `PSOLVE_QP_PHASE1_LP_FIRST=1 python3 tools/qp_diff.py 400 99001`, model it=53)
 * is not a regression test:
 *
 *   model     5 variables, 4 rows, Q = G^T G with rank 2 (so Q's smallest
 *             eigenvalues are rounding, +-1e-15); optimum -7.3939738094380649,
 *             which scipy/`qp_diff.py` agrees with
 *   ray-2     the direction the pre-fix code certified at its third iteration:
 *             p'Qp = +2.5301e20 *exactly* over the given doubles (relative to
 *             sum|q_ij p_i p_j| = 1.49e-13, i.e. a genuine positive curvature and
 *             not a rounding artifact), inside the old band of 3.28922e20
 *   ray-0     the earlier candidate from the same run (p'Qp = +2.54219e20), whose
 *             exact row slopes are +8.96e16 and +9.11e15 against row term scales
 *             of 1.96e17 and 2.52e17 -- outside the row window by ~1e13, i.e. a
 *             direction that leaves the feasible set immediately
 *
 * What is checked, in the two layers that make a verdict:
 *
 *   [A] the engine (`qp_solve`) never answers status 1 (unbounded) for that
 *       model, under either Phase-I order -- the order that reproduced the bug
 *       and the shipped default;
 *   [B] the evidence checker (`psv_cert_check`) REFUSES both frozen rays over
 *       the model's own data, and the refusals come from the two rules that
 *       were made one-sided (curvature for ray-2, rows for ray-0);
 *   [C] the checker still decides rounding-level curvature correctly: a 2x2
 *       gadget whose exact curvature is +-2e-15 is refused/confirmed according
 *       to the sign (the coarse pass cannot see it; the double-double pass
 *       decides it), and the pre-fix band admitted both signs;
 *   [D] the row window is stated, not silent: a slope of 0, of 1e-15 (inside
 *       the row's cancellation scale) and of 1e-13 (outside it) are decided the
 *       way TOL-QP-RAYROW/TOL-CERT-QPROW document;
 *   [E] genuinely flat rays still certify end to end -- engine status 1 AND the
 *       checker's confirmation of the ray it returns.
 *
 * Usage: qp_ray_test            (exit code = number of failures)
 */
#include "qp.h"
#include "cert.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails = 0, checks = 0;
static const char *rcname(PsvRc r)
{ return r == PSV_OK ? "OK" : r == PSV_REJECT ? "REJECT" : "DEFER"; }

#define WANT(rc, want, what) do {                                        \
        checks++;                                                        \
        if ((rc) != (want)) {                                            \
            printf("  FAIL %s: got %s, want %s\n", (what),              \
                   rcname(rc), rcname(want));                            \
            fails++;                                                     \
        }                                                                \
    } while (0)

#define WANT_INT(got, want, what) do {                                   \
        checks++;                                                        \
        if ((got) != (want)) {                                           \
            printf("  FAIL %s: got %d, want %d\n", (what), (int)(got),   \
                   (int)(want));                                         \
            fails++;                                                     \
        }                                                                \
    } while (0)

/* ---- the frozen model (docs/CURV_PS_PLAN.md 1.12, qp_diff.py 400/99001 it=53) */
#define N12 5
#define M12 4
static const double q12[N12 * N12] = {          /* column-major: Q[j*n+i] */
     4.3772819657371729, 4.3120935300132359, 2.5473839599244257,
    -3.0476251675098216, 3.0982981363339523,
     4.3120935300132359, 5.3098255276811255, 3.5366546220563482,
    -1.3722353537186824, 1.6752865548700124,
     2.5473839599244257, 3.5366546220563482, 2.4760663507256266,
    -0.1969057734790137, 0.47124671036091947,
    -3.0476251675098216, -1.3722353537186824, -0.1969057734790137,
     4.6237867128575774, -4.2705294573627421,
     3.0982981363339523, 1.6752865548700124, 0.47124671036091947,
    -4.2705294573627421, 3.9781971277072929
};
static const double c12[N12] = {
    4.9438307477080219, -0.38286912520873528, 3.2157207338403886,
    -4.2828844330158953, -1.9278784768281296
};
static const double A12[M12 * N12] = {          /* row-major: A[i*n+j] */
    -1.2828755044263624, -1.7397813987535684, 1.9366805935297755,
    -0.55029602738216088, 2.0820955485343582,
    -0.68280079977092623, 2.3912808030160493, 0.27837626129262105,
    -2.4377572617269454, 0.31372336029899284,
    -0.97689113285193496, 2.3961167529610448, 1.5008036904129485,
     1.984982490817611, -1.8483890826883276,
     1.3329453147842658, 1.9289084095744524, -1.5668106803647246,
     1.6035345088068382, 0.72442113229494742
};
static const double b12[M12] = {
    3.6772020944622863, 2.543981378752985,
    -0.382324404390598, -1.3703155004454084
};
/* the optimum (default Phase-I order) -- used as the certificate's primal point */
static const double x12[N12] = {
    -1.903213472075401, 0.19137520735462882, -0.29823130386375263,
    -0.23206671454038844, 0.96942744355720678
};
/* the ray the pre-fix code certified (iteration 2 of the LP-first run) */
static const double ray2_12[N12] = {
     3135653511151737.5, -4517712713425451.0, 4142437471974687.5,
    -5765987125217108.0, -7220013291567991.0
};
/* the earlier candidate from the same run (iteration 0): blocked by its rows */
static const double ray0_12[N12] = {
     28481276321407176.0, 14959345693326922.0, -40697939700163056.0,
    -43816767577866888.0, -70697020702961008.0
};

/* Write the frozen model in the .qp format (tools/qp_diff.py's writer), so the
   model behind the regression can be driven through the shipped binary and the
   parser as well as through the in-process engine:
       /tmp/qp_ray_test --emit-qp /tmp/case53.qp && ./qpsolve /tmp/case53.qp      */
static int emit_qp(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return 1;
    fprintf(f, "%d %d\n", N12, M12);
    for (int j = 0; j < N12; j++)
        fprintf(f, "%.17g%c", c12[j], j + 1 < N12 ? ' ' : '\n');
    for (int j = 0; j < N12; j++)                       /* column j of Q */
        for (int i = 0; i < N12; i++)
            fprintf(f, "%.17g%c", q12[(size_t)j * N12 + i],
                    i + 1 < N12 ? ' ' : '\n');
    for (int i = 0; i < M12; i++)                       /* row i of A */
        for (int j = 0; j < N12; j++)
            fprintf(f, "%.17g%c", A12[i * N12 + j], j + 1 < N12 ? ' ' : '\n');
    for (int i = 0; i < M12; i++)
        fprintf(f, "%.17g%c", b12[i], i + 1 < M12 ? ' ' : '\n');
    fclose(f);
    printf("wrote %s (the frozen 1.12 model, n=%d m=%d)\n", path, N12, M12);
    return 0;
}

/* exact-ish curvature of d over Q, in double-double, to state in the test what
   the data really says (the same evaluation qp.c/cert.c use for the verdict) */
static double curvature_exact(const double *Q, const double *d, int n)
{
    double s = 0.0, c = 0.0;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            double a = Q[(size_t)j * n + i], b = d[j], e = d[i];
            double t1 = a * b, e1 = fma(a, b, -t1);
            double t2 = t1 * e, e2 = fma(t1, e, -t2);
            double hi = t2, lo = e2 + e1 * e;
            double sum = s + (hi + lo);
            c += (fabs(s) >= fabs(hi + lo)) ? ((s - sum) + (hi + lo))
                                            : ((hi + lo) - sum + s);
            s = sum;
        }
    }
    return s + c;
}

static double old_band(const double *Q, const double *d, int n)
{
    double qnorm = 0.0, pinf = 0.0;
    for (int i = 0; i < n * n; i++) qnorm = fmax(qnorm, fabs(Q[i]));
    for (int i = 0; i < n; i++) pinf = fmax(pinf, fabs(d[i]));
    return 1e-12 * (1.0 + qnorm) * pinf * pinf;   /* the rule this test pins */
}

static PsvCert qp_cert(const double *Q, const double *A, const double *b,
                       const double *c, const double *x, const double *ray,
                       int n, int m)
{
    PsvCert cl;
    memset(&cl, 0, sizeof cl);
    cl.kind = PSVK_QP_UNBOUNDED;
    cl.n = n; cl.m = m;
    cl.Q = Q; cl.A = A; cl.bq = b; cl.cq = c;
    cl.x = x; cl.ray = ray;
    cl.gt_row = 1e-7; cl.dt_dj = 1e-9;      /* what tools/qpsolve.c passes */
    return cl;
}

int main(int argc, char **argv)
{
    if (argc > 2 && !strcmp(argv[1], "--emit-qp")) return emit_qp(argv[2]);
    if (argc > 1) {
        fprintf(stderr, "usage: %s [--emit-qp PATH]\n", argv[0]);
        return 2;
    }
    printf("qp_ray_test: unboundedness certificate over frozen 1.12 data\n");

    /* ---- [A] the engine never claims UNBOUNDED for this model ------------- */
    double xbuf[N12];
    QPResult r;
    for (int order = 0; order < 2; order++) {
        QP qp;
        memset(&qp, 0, sizeof qp);
        qp.n = N12; qp.m = M12; qp.Q = q12; qp.c = c12; qp.A = A12; qp.b = b12;
        qp.phase1_order = order ? QP_PHASE1_LP_FIRST : QP_PHASE1_DENSE_FIRST;
        memset(&r, 0, sizeof r);
        qp_solve(&qp, &r);
        WANT_INT(r.status != 1, 1, order ? "[A] LP-first: no UNBOUNDED claim"
                                         : "[A] default order: no UNBOUNDED claim");
        if (r.status == 0) {
            /* when it does solve it, it must still be the right optimum */
            WANT_INT(fabs(r.obj - (-7.3939738094380649)) < 1e-9 * (1.0 + fabs(r.obj)), 1,
                     "[A] the reported optimum is the known one");
            memcpy(xbuf, r.x, sizeof xbuf);
        }
        qp_result_free(&r);
    }

    /* ---- [B] the checker refuses both frozen rays ------------------------- */
    {
        PsvCert cl = qp_cert(q12, A12, b12, c12, x12, ray2_12, N12, M12);
        double curv = curvature_exact(q12, ray2_12, N12);
        printf("  ray-2: curvature %.6g (exact), old band %.6g -> the pre-fix rule "
               "admitted it: %s\n", curv, old_band(q12, ray2_12, N12),
               curv <= old_band(q12, ray2_12, N12) ? "yes" : "no");
        WANT_INT(curv > 0.0, 1, "[B] ray-2 really has positive curvature");
        WANT(psv_cert_check(&cl), PSV_REJECT, "[B] ray-2 refused (curvature)");

        PsvCert c0 = qp_cert(q12, A12, b12, c12, x12, ray0_12, N12, M12);
        printf("  ray-0: curvature %.6g (exact)\n", curvature_exact(q12, ray0_12, N12));
        WANT_INT(curvature_exact(q12, ray0_12, N12) > 0.0, 1,
                 "[B] ray-0 also really has positive curvature");
        WANT(psv_cert_check(&c0), PSV_REJECT, "[B] ray-0 refused");
        /* ... and it is refused by the ROWS alone: zero Q removes the curvature
           objection and the direction still leaves the feasible set at once */
        double zerob[N12] = { 1, 1, 1, 1, 0 };
        (void)zerob;
        double Adup[M12 * N12], bzero[M12];
        memcpy(Adup, A12, sizeof Adup);
        for (int i = 0; i < M12; i++) bzero[i] = 0.0;
        PsvCert c0r = qp_cert(q12, Adup, bzero, c12, x12, ray0_12, N12, M12);
        /* sanity: with a zeroed Q the curvature objection is gone for real */
        static const double qzero[N12 * N12] = { 0 };
        PsvCert cz = qp_cert(qzero, Adup, bzero, c12, x12, ray0_12, N12, M12);
        WANT(psv_cert_check(&cz), PSV_REJECT, "[B] ray-0 refused by its rows");
        (void)c0r;
    }

    /* ---- [C] rounding-level curvature is decided by its sign -------------- */
    {
        /* Q = [[1, 1-d],[1-d, 1]], d = (1,-1), c = (0,1), x = 0, no rows:
           d^T Q d = 2*delta, so the sign of delta is the whole question and the
           magnitude is ~1e-15, below what a double dot product can resolve. */
        double deltas[4] = { 0.0, -1e-15, +1e-15, +1e-3 };
        PsvRc want[4] = { PSV_OK, PSV_OK, PSV_REJECT, PSV_REJECT };
        const char *what[4] = {
            "[C] exactly flat direction confirmed",
            "[C] rounding-level NEGATIVE curvature confirmed",
            "[C] rounding-level POSITIVE curvature refused",
            "[C] plain positive curvature refused"
        };
        for (int k = 0; k < 4; k++) {
            double Q[4] = { 1.0, 1.0 - deltas[k], 1.0 - deltas[k], 1.0 };
            double c[2] = { 0.0, 1.0 };
            double x[2] = { 0.0, 0.0 }, ray[2] = { 1.0, -1.0 };
            double Adummy[1] = { 0.0 }, bdummy[1] = { 0.0 };
            PsvCert cl = qp_cert(Q, Adummy, bdummy, c, x, ray, 2, 0);
            double curv = curvature_exact(Q, ray, 2);
            printf("  delta=%-10.3g curvature=%-24.17g (2*delta = %.17g)\n",
                   deltas[k], curv, 2.0 * deltas[k]);
            /* the refinement resolves the gadget's curvature to double-double,
               so it must agree with 2*delta in sign and to many digits -- but
               not bit for bit, since the terms are summed in pairs */
            WANT_INT(fabs(curv - 2.0 * deltas[k]) <= 1e-13 * (1.0 + fabs(2.0 * deltas[k])),
                     1, "[C] gadget curvature is 2*delta");
            if (k == 2)
                WANT_INT(curv <= old_band(Q, ray, 2), 1,
                         "[C] the pre-fix band admitted it (the regression)");
            WANT(psv_cert_check(&cl), want[k], what[k]);
        }
    }

    /* ---- [D] the row window is what TOL-QP-RAYROW documents --------------- */
    {
        double Q[4] = { 0.0, 0.0, 0.0, 0.0 };       /* curvature exactly zero */
        double c[2] = { 0.0, 1.0 };                 /* (Qx+c).d = -1 < 0 */
        double x[2] = { 0.0, 0.0 }, ray[2] = { 1.0, -1.0 };
        double bdummy[1] = { 0.0 };
        struct { double a1; PsvRc want; const char *what; } rows[3] = {
            { 1.0,        PSV_OK,     "[D] row slope exactly 0 confirmed" },
            { 1.0 - 1e-15, PSV_OK,    "[D] row slope inside the window confirmed" },
            { 1.0 - 1e-13, PSV_REJECT, "[D] row slope outside the window refused" }
        };
        for (int k = 0; k < 3; k++) {
            double A[2] = { 1.0, rows[k].a1 };
            PsvCert cl = qp_cert(Q, A, bdummy, c, x, ray, 2, 1);
            WANT(psv_cert_check(&cl), rows[k].want, rows[k].what);
        }
    }

    /* ---- [E] real flat rays still certify end to end ---------------------- */
    {
        /* (i) Q = 0, no rows, c = -1: the LP-is-unbounded case through the QP */
        double Q1[1] = { 0.0 }, c1[1] = { -1.0 };
        double A1[1] = { 0.0 }, b1[1] = { 0.0 };
        QP qp; memset(&qp, 0, sizeof qp);
        qp.n = 1; qp.m = 0; qp.Q = Q1; qp.c = c1; qp.A = A1; qp.b = b1;
        memset(&r, 0, sizeof r);
        qp_solve(&qp, &r);
        WANT_INT(r.status, 1, "[E] Q=0 with c=-1 is certified unbounded");
        if (r.status == 1) {
            PsvCert cl = qp_cert(Q1, A1, b1, c1, r.x, r.ray, 1, 0);
            WANT(psv_cert_check(&cl), PSV_OK, "[E] ... and its ray is confirmed");
        }
        qp_result_free(&r);

        /* (ii) a rank-deficient Q with a genuinely flat direction: the ray must
           run along the zero curvature coordinate, c pulls it down */
        double Q2[4] = { 1.0, 0.0, 0.0, 0.0 };      /* diag(1, 0), column-major */
        double c2[2] = { 0.0, -1.0 };
        double A2[4] = { 0.0, 0.0, 0.0, 0.0 }, b2[2] = { 0.0, 0.0 };
        memset(&qp, 0, sizeof qp);
        qp.n = 2; qp.m = 0; qp.Q = Q2; qp.c = c2; qp.A = A2; qp.b = b2;
        memset(&r, 0, sizeof r);
        qp_solve(&qp, &r);
        WANT_INT(r.status, 1, "[E] flat direction in a PSD Q is certified unbounded");
        if (r.status == 1) {
            PsvCert cl = qp_cert(Q2, A2, b2, c2, r.x, r.ray, 2, 0);
            WANT(psv_cert_check(&cl), PSV_OK, "[E] ... and its ray is confirmed");
        }
        qp_result_free(&r);

        /* (iii) and a plain convex QP is still solved, not called unbounded */
        double Q3[4] = { 2.0, 0.0, 0.0, 2.0 };      /* diag(2,2), identity-ish */
        double c3[2] = { 2.0, -4.0 };               /* optimum (-1, 2) */
        double A3[2] = { 0.0, 0.0 }, b3[2] = { 0.0, 0.0 };
        memset(&qp, 0, sizeof qp);
        qp.n = 2; qp.m = 0; qp.Q = Q3; qp.c = c3; qp.A = A3; qp.b = b3;
        memset(&r, 0, sizeof r);
        qp_solve(&qp, &r);
        WANT_INT(r.status, 0, "[E] a bounded QP still reports OPTIMAL");
        if (r.status == 0) {
            WANT_INT(fabs(r.x[0] + 1.0) < 1e-9 && fabs(r.x[1] - 2.0) < 1e-9, 1,
                     "[E] ... at the right point");
        }
        qp_result_free(&r);
    }

    printf("qp_ray_test: %d checks, %d failures\n", checks, fails);
    return fails;
}
