/* Adversarial test for PSVK_QP_INFEASIBLE (roadmap 6.4 choke point, applied to
 * the QP verdict this repo gained in the Phase-I rework).
 *
 * The checker is the thing standing between "the feasibility search gave up" and
 * "the model has no feasible point", so what matters is not that it accepts good
 * certificates -- it must REFUSE the ones that look good.  Cases:
 *
 *   legit      a certificate qp.c produced itself, re-checked independently
 *   sign-flip  one weight negated          -> not a nonnegative combination
 *   zero       the whole vector zeroed      -> vacuous, cannot say (DEFER)
 *   nan        one weight NaN               -> cannot say (DEFER)
 *   wrong-rhs  checked against a different b -> no separation (REJECT)
 *   truncated  one row's coefficient cleared -> A^T lambda != 0 (REJECT)
 *   hand-rolled a certificate for a FEASIBLE system -> REJECT (the whole point:
 *              a model that has a point cannot be certified empty)
 *
 * The last case is the one a producer bug would produce, and it is the reason
 * the CLI verifies rather than trusts.  Usage: qp_cert_test [reps]
 */
#include "qp.h"
#include "cert.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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

/* Two rows that cannot both hold:  x <= 1 and -x <= -2  (i.e. x >= 2). */
#define NQ 1
static void infeasible_model(double *Q, double *c, double *A, double *b, int *m)
{
    Q[0] = 2.0; c[0] = 0.0;
    A[0] = 1.0;  b[0] = 1.0;
    A[1] = -1.0; b[1] = -2.0;
    *m = 2;
}

int main(int argc, char **argv)
{
    int reps = argc > 1 ? atoi(argv[1]) : 1;
    int n = NQ, m = 0, i;
    double Q[1], c[1], A[2 * NQ], b[2], lam[2];
    infeasible_model(Q, c, A, b, &m);

    printf("qp_cert_test: PSVK_QP_INFEASIBLE over a 1-variable, 2-row empty system\n");
    for (int r = 0; r < reps; r++) {
        /* Ask the engine for the certificate, then judge it here -- nothing about
         * the expected lambda is hardcoded except in the hand-rolled case. */
        QP qp; memset(&qp, 0, sizeof qp);
        qp.n = n; qp.m = m; qp.Q = Q; qp.c = c; qp.A = A; qp.b = b;
        QPResult res; memset(&res, 0, sizeof res);
        qp_solve(&qp, &res);
        if (res.status != -1 || !res.infeasible_proven || !res.farkas) {
            printf("  FAIL engine did not prove this model empty (status %d, proven %d)\n",
                   res.status, res.infeasible_proven);
            fails++; checks++;
            qp_result_free(&res);
            continue;
        }
        memcpy(lam, res.farkas, (size_t)m * sizeof(double));
        checks++;
        if (!(lam[0] > 0.0 && lam[1] > 0.0)) {
            printf("  FAIL certificate weights not both positive (%g, %g)\n", lam[0], lam[1]);
            fails++;
        }

        PsvCert cl; memset(&cl, 0, sizeof cl);
        cl.kind = PSVK_QP_INFEASIBLE;
        cl.n = n; cl.m = m; cl.A = A; cl.bq = b; cl.ray = lam;
        cl.gt_row = 1e-7; cl.dt_gap = 1e-9;
        WANT(psv_cert_check(&cl), PSV_OK, "legit certificate accepted");

        { double bad[2] = { -lam[0], lam[1] }; PsvCert c2 = cl; c2.ray = bad;
          WANT(psv_cert_check(&c2), PSV_REJECT, "sign-flipped weight refused"); }

        { double bad[2] = { 0.0, 0.0 }; PsvCert c2 = cl; c2.ray = bad;
          WANT(psv_cert_check(&c2), PSV_DEFER, "zero vector cannot say"); }

        { double bad[2] = { lam[0], nan("") }; PsvCert c2 = cl; c2.ray = bad;
          WANT(psv_cert_check(&c2), PSV_DEFER, "NaN weight cannot say"); }

        { double other[2] = { 1.0, 2.0 }; PsvCert c2 = cl; c2.bq = other;
          WANT(psv_cert_check(&c2), PSV_REJECT, "certificate against another rhs refused"); }

        { double A2[2] = { A[0], 0.0 }; PsvCert c2 = cl; c2.A = A2;
          WANT(psv_cert_check(&c2), PSV_REJECT, "cleared row coefficient refused"); }

        { PsvCert c2 = cl; c2.ray = NULL;
          WANT(psv_cert_check(&c2), PSV_DEFER, "missing payload cannot say"); }
        qp_result_free(&res);
    }

    /* A hand-rolled "proof" against a system that IS feasible: x <= 1, -x <= 1.
     * Any lambda >= 0 with A^T lambda = 0 must be lambda = 0, so b^T lambda < 0
     * is unreachable -- this is the class of bug the choke point exists for. */
    {   double Af[2 * NQ] = { 1.0, -1.0 }, bf[2] = { 1.0, 1.0 };
        double guess[2] = { 1.0, 1.0 };
        PsvCert cl; memset(&cl, 0, sizeof cl);
        cl.kind = PSVK_QP_INFEASIBLE;
        cl.n = n; cl.m = 2; cl.A = Af; cl.bq = bf; cl.ray = guess;
        cl.gt_row = 1e-7; cl.dt_gap = 1e-9;
        WANT(psv_cert_check(&cl), PSV_REJECT, "fabricated proof on a feasible system refused");
    }

    printf("%s: %d checks, %d failures\n", fails ? "qp_cert_test: FAIL" : "qp_cert_test: OK",
           checks, fails);
    (void)i;
    return fails ? 1 : 0;
}
