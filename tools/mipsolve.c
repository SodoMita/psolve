#include "mip.h"
#include "parser.h"
#include "err.h"
#include "tlimit.h"
#include "solver.h"
#include "cert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <float.h>

static volatile sig_atomic_t g_stop = 0;
static void on_stop_signal(int signo) { (void)signo; g_stop = 1; }
static int stop_requested(void) { return (int)g_stop; }

/* Shared original-model view for certificate claims. */
static void psv_fill_lp(PsvCert *cl, PsvKind kind, const LP *lp)
{
    memset(cl, 0, sizeof(*cl));
    cl->kind = kind;
    cl->n = lp->n; cl->m = lp->m;
    cl->colptr = lp->Acolptr; cl->row = lp->Arow; cl->val = lp->Aval;
    cl->rel = lp->rel; cl->b = lp->b; cl->lo = lp->l; cl->hi = lp->u;
    cl->c = lp->c; cl->maximize = lp->maximize;
}

/* Re-solve the pure LP relaxation of the model (integrality dropped) to
   arbitrate a MIP-level verdict with an independent LP certificate:
   relaxation-infeasible proves MIP-infeasible, relaxation-unbounded
   (with a verified ray) proves MIP-unbounded.  Returns the solver's raw
   status, and - on those two verdicts - leaves a psv-verified answer in
   *cert_ok. */
static int lp_relax_verdict(const LP *lp, int *cert_ok, PsvKind want)
{
    *cert_ok = 0;
    Solver *s = solver_create(lp);
    if (!s) return -1;
    int r = solver_solve(s);
    if (r == 1 && want == PSVK_LP_INFEASIBLE) {
        double *y = (double*)malloc((size_t)(lp->m ? lp->m : 1) * sizeof(double));
        if (s->farkas_ok && y && solver_farkas_duals(s, y) == 0) {
            PsvCert cl; psv_fill_lp(&cl, PSVK_LP_INFEASIBLE, lp);
            for (int i = 0; i < lp->m; i++) y[i] *= (double)s->mlt[i];
            cl.ray = y; cl.dt_gap = 1e-6;
            *cert_ok = (psv_cert_check(&cl) == PSV_OK);
        }
        free(y);
    } else if (r == 2 && want == PSVK_LP_UNBOUNDED) {
        double *x = (double*)malloc((size_t)(lp->n ? lp->n : 1) * sizeof(double));
        double *d = (double*)malloc((size_t)(lp->n ? lp->n : 1) * sizeof(double));
        double obj = 0.0;
        if (x && d) {
            solver_optimum(s, x, &obj);
            if (solver_unbounded_ray(s, d) == 0) {
                PsvCert cl; psv_fill_lp(&cl, PSVK_LP_UNBOUNDED, lp);
                cl.x = x; cl.ray = d; cl.obj = obj;
                cl.gt_box = 1e-6; cl.gt_row = 1e-5; cl.dt_dj = 1e-9;
                *cert_ok = (psv_cert_check(&cl) == PSV_OK);
            }
        }
        free(x); free(d);
    }
    solver_destroy(s);
    return r;
}

int main(int argc, char **argv)
{
    const char * volatile path = NULL;
    volatile int print = 0;
    volatile long time_ms = 0;
    volatile int arg_idx = 1;
    while (arg_idx < argc && argv[arg_idx][0] == '-') {
        if (strcmp(argv[arg_idx], "-t") == 0 || strcmp(argv[arg_idx], "--time-limit") == 0) {
            if (arg_idx + 1 >= argc) { fprintf(stderr, "missing time limit\n"); return 1; }
            time_ms = atol(argv[++arg_idx]);
            if (time_ms <= 0) { fprintf(stderr, "invalid time limit\n"); return 1; }
        } else if (strcmp(argv[arg_idx], "--print") == 0) {
            print = 1;   /* accepted in any position (was: trailing-only) */
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[arg_idx]); return 1;
        }
        arg_idx++;
    }
    if (arg_idx + 1 >= argc) {
        fprintf(stderr, "usage: %s [-t ms|--time-limit ms] <problem.lp> <nint> <j0 j1 ...> [--print]\n", argv[0]);
        return 1;
    }
    path = argv[arg_idx++];
    volatile int nint = atoi(argv[arg_idx++]);
    for (int a = arg_idx; a < argc; a++) if (strcmp(argv[a], "--print") == 0) print = 1;

    PSolveErrFrame ef;
    psolve_frame_push(&ef);
    if (setjmp(ef.env) != 0) {
        fprintf(stderr, "mipsolve: %s\n",
                psolve_err_code() == PSOLVE_ERR_OOM ? "out of memory" : "internal error");
        return 2;   /* the frame is already popped by psolve_fail() */
    }

    signal(SIGINT, on_stop_signal);
    signal(SIGALRM, on_stop_signal);
    psolve_stop_set(stop_requested);
    /* ITIMER_REAL gives millisecond precision (alarm() rounded up to seconds). */
    if (tlimit_arm(time_ms) != 0) { fprintf(stderr, "cannot arm time limit\n"); psolve_stop_set(NULL); psolve_frame_pop(&ef); return 1; }

    LP lp;
    memset(&lp, 0, sizeof(LP));
    if (lp_read(path, &lp) != 0) { psolve_stop_set(NULL); psolve_frame_pop(&ef); return 1; }

    unsigned char *isint = (unsigned char*)psolve_calloc((size_t)lp.n, 1);
    int ni = 0;
    for (int a = arg_idx; a < arg_idx + nint && a < argc; a++) {
        if (strcmp(argv[a], "--print") == 0) continue;
        int j = atoi(argv[a]);
        if (j >= 0 && j < lp.n) { isint[j] = 1; ni++; }
    }
    if (ni == 0) { fprintf(stderr, "no valid integer variables specified\n"); psolve_frame_pop(&ef); return 1; }

    /* Zero the whole struct first: MIP has optional fields (stop_at_feasible,
       ...) that this driver does not set.  Leaving them uninitialised is
       undefined behaviour -- and it bit: a garbage stop_at_feasible made
       branch-and-bound return the first integer-feasible point it stumbled
       on, which was then reported as OPTIMAL. */
    MIP mip; memset(&mip, 0, sizeof(mip));
    mip.n = lp.n; mip.m = lp.m;
    mip.c = lp.c; mip.Acolptr = lp.Acolptr; mip.Arow = lp.Arow; mip.Aval = lp.Aval;
    mip.rel = lp.rel; mip.b = lp.b; mip.l = lp.l; mip.u = lp.u;
    mip.maximize = lp.maximize;
    mip.isint = isint;
    mip.mip_gap = 1e-4;
    mip.node_limit = 200000;
    mip.lp_iter_limit = 2000000;

    MIPResult res;
    mip_solve(&mip, &res);

    if (res.status == 0 && !res.proven_optimal) {
        /* Feasible, but the tree was not exhausted (e.g. stop_at_feasible).
           Never print OPTIMAL for a point whose optimality was not proven. */
        printf("status: FEASIBLE\n");
        printf("objective: %.15g\n", res.obj);
        printf("nodes: %ld\n", res.nodes);
        if (print)
            for (int j = 0; j < lp.n; j++)
                printf("x[%d] = %.10g\n", j, res.x[j]);
    } else if (res.status == 0) {
        /* roadmap 6.4: OPTIMAL prints only through the unified evidence
           entry point - the checker re-verifies the incumbent (bounds,
           rows, EXACT integrality of snapped ints, objective) and the
           bound-coherence stamp of the proven-optimality claim */
        PsvCert cl; psv_fill_lp(&cl, PSVK_MIP_POINT, &lp);
        cl.x = res.x; cl.isint = isint; cl.obj = res.obj;
        cl.proven_optimal = res.proven_optimal; cl.best_bound = res.best_bound;
        cl.mip_gap = mip.mip_gap;
        cl.gt_box = 1e-6; cl.gt_row = 1e-6; cl.dt_obj = 1e-6; cl.dt_gap = 1e-6;
        if (psv_cert_check(&cl) != PSV_OK) {
            fprintf(stderr, "psv: mip_point certificate not confirmed\n");
            printf("status: NUMERICAL_FAILURE\n");
            goto done_mip;
        }
        printf("status: OPTIMAL\n");
        printf("objective: %.15g\n", res.obj);
        printf("nodes: %ld\n", res.nodes);
        if (print)
            for (int j = 0; j < lp.n; j++)
                printf("x[%d] = %.10g\n", j, res.x[j]);
    } else if (res.status == 1) {
        /* Two evidence lanes (roadmap 6.4): the LP relaxation itself is
           infeasible (then a re-verified Farkas ray certifies it), or the
           branch-and-bound tree exhausted - a discrete search stamp. */
        int relax_cert = 0;
        int rstat = lp_relax_verdict(&lp, &relax_cert, PSVK_LP_INFEASIBLE);
        int ok = 0;
        if (rstat == 1) {
            /* the relaxation infeasibility is only trustworthy through the
               same lanes policy the LP CLI uses: a provable ray, or the
               Phase-I-certified verdict below the shaky frontier */
            if (relax_cert) ok = 1;
            else {
                double E = solver_row_exposure(lp.n, lp.m, lp.Acolptr, lp.Arow,
                                               lp.Aval, lp.l, lp.u);
                if (E * DBL_EPSILON < 5e-7) ok = 1;
            }
        }
        if (!ok) {
            /* Tree-exhaustion stamp: the engine's status 1 means every node
               verdict already passed the engine-internal lanes (Phase-I /
               fx / directed box conflict) and the search was NOT truncated;
               the CLI layer re-verifies exactly the no-truncation stamp.
               This lane is also the only decidable one when infeasibility
               lives below the 1e-6 ray margin - e.g. the pinned 5e-7
               margin cycle, exactly infeasible on the integer lattice,
               whose relaxation ray the box certificate legitimately
               refuses at dt_gap=1e-6 and whose fresh re-solve may even
               surface SOLVE_NUMERICAL (the engine proves that model by
               lattice exhaustion, nodes=1). */
            PsvCert cl; memset(&cl, 0, sizeof(cl));
            cl.kind = PSVK_EXHAUSTION;
            cl.nodes = res.nodes; cl.node_limit = mip.node_limit; cl.stopped = 0;
            ok = (psv_cert_check(&cl) == PSV_OK);
        }
        if (!ok) {
            fprintf(stderr, "psv: mip infeasibility certificate not confirmed\n");
            printf("status: NUMERICAL_FAILURE\n");
            goto done_mip;
        }
        printf("status: INFEASIBLE\n");
        printf("nodes: %ld\n", res.nodes);
    } else if (res.status == 2) {
        /* MIP-unbounded implies relaxation-unbounded; print it only with a
           re-verified relaxation recession certificate (a relaxation that
           turns out bounded/optimal contradicts the claim -> honest class) */
        int relax_cert = 0;
        int rstat = lp_relax_verdict(&lp, &relax_cert, PSVK_LP_UNBOUNDED);
        if (rstat == 2 && relax_cert) {
            printf("status: UNBOUNDED\n");
        } else {
            fprintf(stderr, "psv: mip unbounded certificate not confirmed (relaxation status %d)\n", rstat);
            printf("status: NUMERICAL_FAILURE\n");
        }
    } else if (res.status == 3) {
        printf("status: NODE_LIMIT\n");
        if (res.obj == res.obj) printf("best objective: %.15g\n", res.obj);
    } else if (res.status == 4) {
        printf("status: STOPPED\n");
        if (res.obj == res.obj) printf("best objective: %.15g\n", res.obj);
    } else if (res.status == 6) {
        printf("status: NUMERICAL_FAILURE\n");
    } else if (res.status == MIP_INVALID) {
        printf("status: INVALID_MODEL\n");
    } else {   /* status 5: feasible incumbent, optimality not proven */
        if (res.obj == res.obj && res.x) {
            PsvCert cl; psv_fill_lp(&cl, PSVK_MIP_POINT, &lp);
            cl.x = res.x; cl.isint = isint; cl.obj = res.obj;
            cl.gt_box = 1e-6; cl.gt_row = 1e-6; cl.dt_obj = 1e-6;
            if (psv_cert_check(&cl) != PSV_OK) {
                fprintf(stderr, "psv: mip incumbent certificate not confirmed\n");
                printf("status: NUMERICAL_FAILURE\n");
                goto done_mip;
            }
        }
        printf("status: FEASIBLE_LIMIT\n");
        if (res.obj == res.obj) {
            printf("objective: %.15g\n", res.obj);
            printf("best_bound: %.15g\n", res.best_bound);
        }
        if (print)
            for (int j = 0; j < lp.n; j++)
                printf("x[%d] = %.10g\n", j, res.x[j]);
    }

done_mip:
    if (print) {
        printf("farkas_certs: %ld\n", res.farkas_certs);
        printf("fx_solves: %ld\n", res.fx_solves);
    }

    mip_result_free(&res);
    free(isint);
    lp_free(&lp);
    tlimit_disarm();
    psolve_stop_set(NULL);
    psolve_frame_pop(&ef);
    return 0;
}
