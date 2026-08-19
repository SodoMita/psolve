#include "parser.h"
#include "solver.h"
#include "err.h"
#include "tlimit.h"
#include "cert.h"
#include "presolve.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <float.h>

/* The simplex loop polls this flag, so signal handling stays async-signal-safe:
   the handler only performs a single assignment to sig_atomic_t. */
static volatile sig_atomic_t g_stop = 0;
static void on_stop_signal(int signo) { (void)signo; g_stop = 1; }
static int stop_requested(void) { return (int)g_stop; }

static int parse_positive_ms(const char *text, long *out)
{
    char *end;
    long value;
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || value <= 0 || value > INT_MAX)
        return -1;
    *out = value;
    return 0;
}

/* Fill the shared original-model view of a certificate claim from an LP
   file model.  Payload and margins are set per verdict lane by the caller. */
static void psv_fill_lp(PsvCert *cl, PsvKind kind, const LP *lp)
{
    memset(cl, 0, sizeof(*cl));
    cl->kind = kind;
    cl->n = lp->n; cl->m = lp->m;
    cl->colptr = lp->Acolptr; cl->row = lp->Arow; cl->val = lp->Aval;
    cl->rel = lp->rel; cl->b = lp->b; cl->lo = lp->l; cl->hi = lp->u;
    cl->c = lp->c; cl->maximize = lp->maximize;
}

int main(int argc, char **argv)
{
    /* These values remain valid across the allocation error longjmp below. */
    const char * volatile path = NULL;
    volatile int print = 0;
    volatile long time_ms = 0;
    volatile int scale_mode = 1, scalestat = 0;
    volatile int presolve_mode = 1, prestat = 0;
    for (int a = 1; a < argc; a++) {
        if (strcmp(argv[a], "--print") == 0) {
            print = 1;
        } else if (strcmp(argv[a], "--noscale") == 0) {
            scale_mode = 0;   /* roadmap 7.5 A/B escape hatch: raw data path */
        } else if (strcmp(argv[a], "--scalestat") == 0) {
            scalestat = 1;    /* report the pre/post equilibration spread */
        } else if (strcmp(argv[a], "--nopresolve") == 0) {
            presolve_mode = 0;   /* roadmap 7.1 A/B escape hatch */
        } else if (strcmp(argv[a], "--prestat") == 0) {
            prestat = 1;      /* report the reduction stats */
        } else if (strcmp(argv[a], "-t") == 0 || strcmp(argv[a], "--time-limit") == 0) {
            long parsed_ms;
            if (++a == argc || parse_positive_ms(argv[a], &parsed_ms) != 0) {
                fprintf(stderr, "invalid time limit (expected positive milliseconds)\n");
                return 1;
            }
            time_ms = parsed_ms;
        } else if (argv[a][0] != '-') {
            if (path) {
                fprintf(stderr, "only one problem file may be supplied\n");
                return 1;
            }
            path = argv[a];
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[a]);
            return 1;
        }
    }
    if (!path) {
        fprintf(stderr, "usage: %s [-t ms|--time-limit ms] [--noscale] [--scalestat] [--nopresolve] [--prestat] <problem.lp> [--print]\n", argv[0]);
        return 1;
    }

    /* Install this thread's error frame in caller-owned storage so
       out-of-memory (and internal solver failures) unwind here and are
       reported cleanly instead of aborting. */
    PSolveErrFrame ef;
    psolve_frame_push(&ef);
    if (setjmp(ef.env) != 0) {
        fprintf(stderr, "solver failed: %s\n",
                psolve_err_code() == PSOLVE_ERR_OOM ? "out of memory" : "internal error");
        return 2;   /* the frame is already popped by psolve_fail() */
    }

    /* SIGINT and the optional wall-clock limit are cooperative: solver_solve()
       notices the flag and returns SOLVE_STOPPED rather than leaving a partial
       result labelled OPTIMAL.  tlimit_arm() uses ITIMER_REAL (microsecond
       resolution) so the -t budget is honored at the requested millisecond
       precision -- alarm(), the old approach, rounds up to whole seconds. */
    signal(SIGINT, on_stop_signal);
    signal(SIGALRM, on_stop_signal);
    psolve_stop_set(stop_requested);
    if (tlimit_arm(time_ms) != 0) { fprintf(stderr, "cannot arm time limit\n"); psolve_frame_pop(&ef); return 1; }

    LP lp;
    memset(&lp, 0, sizeof(LP));
    if (lp_read((const char *)path, &lp) != 0) { psolve_stop_set(NULL); psolve_frame_pop(&ef); return 1; }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    /* Attempt ladder (roadmap 7.1 + 7.5, evidence-preserving, never
       silent): presolve (7.1) and equilibration (7.5) are conditioning,
       never verdict inputs.  Every printed verdict must be certified
       against the ORIGINAL data by the psv lanes below: a presolve
       infeasibility ray hint, a postsolved point/duals/ray, the engine's
       own certificates -- anything that fails certification degrades the
       attempt and the NEXT ladder config re-solves closer to the raw
       data path:
         (presolve_on, scale_on) -> (presolve_off, scale_on) -> (off, off)
       Flags pin the starting point; a presolve that declines already
       covers the presolve-off config at its scale.  Retries never run
       after an explicit stop (the requested budget is binding).  The
       printed answer is therefore always the best CERTIFIED one
       available: presolve/scaling can only add answers, never take one. */
    int cfgs[3][2], ncfg = 0;
    cfgs[ncfg][0] = presolve_mode; cfgs[ncfg][1] = scale_mode; ncfg++;
    if (presolve_mode) { cfgs[ncfg][0] = 0; cfgs[ncfg][1] = scale_mode; ncfg++; }
    if (presolve_mode || scale_mode) { cfgs[ncfg][0] = 0; cfgs[ncfg][1] = 0; ncfg++; }
    int printed = 0;
    char cfg_done[9] = {0};
    for (int attempt = 0; attempt < ncfg && !printed; attempt++) {
        int cpre = cfgs[attempt][0], csc = cfgs[attempt][1];
        if (cfg_done[cpre * 3 + csc]) continue;
        cfg_done[cpre * 3 + csc] = 1;
        int degraded = 0;
        int can_retry = 0;
        for (int q = attempt + 1; q < ncfg; q++)
            if (!cfg_done[cfgs[q][0] * 3 + cfgs[q][1]]) can_retry = 1;
        if (psolve_stop()) can_retry = 0;

        if (attempt > 0) {
            if (cpre == 0 && csc != 0)
                fprintf(stderr, "psv: previous attempt's evidence not certified, re-solving without presolve\n");
            else
                fprintf(stderr, "psv: previous attempt's evidence not certified, re-solving on raw data path (prescale off)\n");
        }

        LP red; memset(&red, 0, sizeof(red));
        PreSolve *ps = NULL;
        const LP *model = &lp;
        if (cpre) {
            PreStats st; memset(&st, 0, sizeof(st));
            int prc = lp_presolve(&lp, &red, &ps, &st);
            if (prestat) {
                if (prc == 1)
                    fprintf(stderr, "prestat: rc=1 (exact infeasible) rows=%d cols=%d passes=%d\n",
                            lp.m, lp.n, st.passes);
                else if (prc != 0)
                    fprintf(stderr, "prestat: rc=-1 (declined) rows=%d cols=%d\n", lp.m, lp.n);
                else
                    fprintf(stderr, "prestat: rows %d->%d cols %d->%d nnz %ld->%ld "
                            "passes=%d fixed=%ld empty_col=%ld empty_row=%ld "
                            "singleton=%ld redundant=%ld doubleton=%ld notes=%ld\n",
                            lp.m, st.m_red, lp.n, st.n_red,
                            (long)(lp.Acolptr[lp.n]), st.nnz_red, st.passes,
                            st.fixed_cols, st.empty_cols, st.empty_rows,
                            st.singleton_rows, st.redundant_rows,
                            st.doubleton_rows, st.unbounded_notes);
            }
            if (prc == 1) {
                /* presolve proved infeasibility exactly; its ray is only
                   a HINT: verify in the standard Farkas lane against
                   ORIGINAL data before anything prints */
                double *yr = (double*)malloc((size_t)(lp.m ? lp.m : 1) * sizeof(double));
                int okc = 0;
                if (yr && lp_presolve_farkas_ray(ps, yr) == 0) {
                    PsvCert cl; psv_fill_lp(&cl, PSVK_LP_INFEASIBLE, &lp);
                    cl.ray = yr; cl.dt_gap = 1e-6;  /* TOLSHEET TOL-LP-FARKAS */
                    okc = (psv_cert_check(&cl) == PSV_OK);
                }
                free(yr);
                clock_gettime(CLOCK_MONOTONIC, &t1);
                double secs = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;  /* TOLSHEET TOL-SYS-NSEC */
                if (okc) {
                    printf("iterations: 0\n");
                    printf("time: %.6f s\n", secs);
                    printf("status: INFEASIBLE\n");
                    printed = 1;
                } else {
                    fprintf(stderr, "psv: presolve infeasibility ray not confirmed\n");
                    /* degraded: following attempts (raw data) decide */
                }
                lp_presolve_free(ps);
                continue;
            }
            if (prc == 0 && ps) model = &red;
            else cfg_done[0 * 3 + csc] = 1;   /* declined: this attempt
                                                 covers the presolve-off
                                                 config at this scale */
        }

        if (model->n == 0) {
            /* presolve eliminated EVERY variable: no engine to run.  The
               verdict is whatever the postsolve maps can certify against
               ORIGINAL data through the standard psv lanes: the maps
               produce the point (and the OPTIMAL claim's duals) outright.
               Anything unverifiable degrades to the rawer attempts. */
            double *xo = (double*)calloc((size_t)lp.n, sizeof(double));
            double *yp = (double*)malloc((size_t)(lp.m > 0 ? lp.m : 1) * sizeof(double));
            int ok = 0;
            clock_gettime(CLOCK_MONOTONIC, &t1);
            double secs = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;  /* TOLSHEET TOL-SYS-NSEC */
            printf("iterations: 0\n");
            printf("time: %.6f s\n", secs);
            if (xo && yp &&
                lp_presolve_postsolve_x(ps, NULL, xo) == 0 &&
                lp_presolve_postsolve_duals(ps, NULL, xo, yp) == 0) {
                double objc = 0.0;
                for (int j = 0; j < lp.n; j++) objc += lp.c[j] * xo[j];
                double walk = 0.0;
                int note = lp_presolve_unbounded_note(ps, &walk);
                if (note >= 0) {
                    double *d = (double*)calloc((size_t)lp.n, sizeof(double));
                    if (d) {
                        PsvCert cl; psv_fill_lp(&cl, PSVK_LP_UNBOUNDED, &lp);
                        d[note] = walk;
                        cl.x = xo; cl.ray = d; cl.obj = objc;
                        cl.gt_box = 1e-6; cl.gt_row = 1e-5; cl.dt_dj = 1e-9; /* TOLSHEET TOL-CERT-DEFBOX */ /* TOLSHEET TOL-CERT-DEFROW */ /* TOLSHEET TOL-CERT-DEFDJ */
                        ok = (psv_cert_check(&cl) == PSV_OK);
                        if (ok) printf("status: UNBOUNDED\n");
                    }
                    free(d);
                } else {
                    PsvCert cl; psv_fill_lp(&cl, PSVK_LP_OPTIMAL, &lp);
                    cl.x = xo; cl.y = yp; cl.obj = objc;
                    cl.gt_box = 1e-6; cl.gt_row = 1e-5; /* TOLSHEET TOL-CERT-DEFBOX */ /* TOLSHEET TOL-CERT-DEFROW */
                    cl.dt_dj = 1e-9; cl.dt_gap = 1e-7; cl.dt_obj = 1e-9; /* TOLSHEET TOL-CERT-DEFDJ */ /* TOLSHEET TOL-CERT-DEFGAP */ /* TOLSHEET TOL-CERT-DEFOBJ */
                    ok = (psv_cert_check(&cl) == PSV_OK);
                    if (ok) {
                        printf("status: OPTIMAL\n");
                        printf("objective: %.15g\n", objc);
                        if (print) {
                            for (int j = 0; j < lp.n; j++)
                                printf("x[%d] = %.17g\n", j, xo[j]);
                        }
                    }
                }
            }
            if (ok) {
                printed = 1;
            } else {
                fprintf(stderr, "psv: fully-presolved claim not certified\n");
                if (!can_retry) printf("status: NUMERICAL_FAILURE\n");
            }
            free(xo); free(yp);
            lp_presolve_free(ps);
            continue;
        }

        Solver *s = solver_create_opts(model, csc);
        if (!s) { if (ps) { lp_presolve_free(ps); lp_free(&red); }
                  lp_free(&lp); psolve_stop_set(NULL); psolve_frame_pop(&ef); return 1; }
        int r = solver_solve(s);
        /* The sparse factorization is fast but can, on ill-conditioned bases,
           return a wrong (infeasible) point.  Detect that and retry with the
           robust dense path to guarantee a correct answer.  Never retry after
           an explicit user stop: doing so would violate the requested time
           budget. */
        if (r == 0 && !solver_feasible(s) && !psolve_stop()) {
            solver_destroy(s);
            s = solver_create_opts(model, csc);
            if (!s) { if (ps) { lp_presolve_free(ps); lp_free(&red); }
                      lp_free(&lp); psolve_stop_set(NULL); psolve_frame_pop(&ef); return 1; }
            s->sparse_disabled = 1;  /* force dense from the start */
            s->use_sparse = 0;
            r = solver_solve(s);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double secs = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;  /* TOLSHEET TOL-SYS-NSEC */
        if (scalestat)
            fprintf(stderr, "scalestat: mode=%d ratio_pre=%.6g ratio_post=%.6g iters=%ld\n",
                    s->scale_mode, s->stat_ratio_pre, s->stat_ratio_post, s->iters);

        if (r == 1 && ps) {
            /* v1 keeps no Farkas-ray back-propagation through the
               reduction stack (and the reduced empty-box shape below
               cannot arise from the fold rules anyway): every presolved-
               model infeasibility defers to the rawer attempts - which
               decide with the full 6.1/6.4 certificate chain */
            fprintf(stderr, "psv: presolved model infeasible; certificate stays on raw attempts\n");
            degraded = 1;
        } else if (r == 1 && s->farkas_ok) {
                /* The phase-1 infeasibility verdict needs an independent proof
                   before it may be printed bare: extract the dual ray and
                   re-verify the full Farkas separation against the original
                   rows and box with directed rounding.  If that fails AND the
                   model is extreme scale-mixed, downgrade to the honest
                   numerical-failure class (6.1/6.4 chains; see the full
                   comment history in git). */
                int n = lp.n, m = lp.m;
                double *fr_y  = (double*)malloc((size_t)(m ? m : 1) * sizeof(double));
                double *fr_yc = (double*)malloc((size_t)(m ? m : 1) * sizeof(double));
                double *fr_zl = (double*)malloc((size_t)n * sizeof(double));
                double *fr_zh = (double*)malloc((size_t)n * sizeof(double));
                if (fr_y && fr_yc && fr_zl && fr_zh) {
                    int certified = 0;
                    if (solver_farkas_duals(s, fr_y) == 0) {
                        PsvCert cl; psv_fill_lp(&cl, PSVK_LP_INFEASIBLE, &lp);
                        for (int i = 0; i < m; i++) fr_y[i] *= (double)s->mlt[i];
                        cl.ray = fr_y; cl.dt_gap = 1e-6;  /* TOLSHEET TOL-LP-FARKAS */
                        if (psv_cert_check(&cl) == PSV_OK) certified = 1;
                    }
                    if (!certified) {
                        double E = solver_row_exposure(n, m, lp.Acolptr, lp.Arow,
                                                       lp.Aval, lp.l, lp.u);
                        if (E * DBL_EPSILON >= 5e-7) {  /* TOLSHEET TOL-LP-SHAKY */
                            if (can_retry) degraded = 1;
                            else r = SOLVE_NUMERICAL;
                        }
                    }
                }
                free(fr_y); free(fr_yc); free(fr_zl); free(fr_zh);
        }
        if (degraded) {
            solver_destroy(s);
            if (ps) { lp_presolve_free(ps); lp_free(&red); }
            continue;
        }

        printf("iterations: %ld\n", s->iters);
        printf("time: %.6f s\n", secs);

        if (r == 1) {
            printf("status: INFEASIBLE\n");
        } else if (r == 2) {
            /* UNBOUNDED prints only with a re-verified certificate: a primal
               feasible point and a recession ray that the psv layer checks
               against the original rows, box sides and objective direction */
            int mo_n = model->n;
            double *xu = (double*)malloc((size_t)(mo_n > 0 ? mo_n : 1) * sizeof(double));
            double *ur = (double*)malloc((size_t)(mo_n > 0 ? mo_n : 1) * sizeof(double));
            double obju = 0.0;
            int ok = 0;
            if (xu && ur) {
                solver_optimum(s, xu, &obju);
                if (solver_unbounded_ray(s, ur) == 0) {
                    double *xc = xu, *dc = ur;
                    double objc = obju;
                    double *xp = NULL, *dp = NULL;
                    if (ps) {   /* transfer the evidence to ORIGINAL units */
                        xp = (double*)malloc((size_t)lp.n * sizeof(double));
                        dp = (double*)malloc((size_t)lp.n * sizeof(double));
                        if (xp && dp &&
                            lp_presolve_postsolve_x(ps, xu, xp) == 0 &&
                            lp_presolve_postsolve_ray(ps, ur, dp) == 0) {
                            xc = xp; dc = dp;
                            objc = 0.0;
                            for (int j = 0; j < lp.n; j++) objc += lp.c[j] * xc[j];
                        } else {
                            fprintf(stderr, "psv: presolve ray transfer unsupported\n");
                            degraded = 1;
                        }
                        free(xu); free(ur); xu = xp; ur = dp;
                    }
                    if (!degraded) {
                        PsvCert cl; psv_fill_lp(&cl, PSVK_LP_UNBOUNDED, &lp);
                        cl.x = xc; cl.ray = dc; cl.obj = objc;
                        cl.gt_box = 1e-6; cl.gt_row = 1e-5; cl.dt_dj = 1e-9; /* TOLSHEET TOL-CERT-DEFBOX */ /* TOLSHEET TOL-CERT-DEFROW */ /* TOLSHEET TOL-CERT-DEFDJ */
                        ok = (psv_cert_check(&cl) == PSV_OK);
                    }
                }
            }
            /* a verdict the certificate layer cannot confirm must not print */
            if (ok) printf("status: UNBOUNDED\n");
            else if (!degraded) {
                fprintf(stderr, "psv: lp_unbounded certificate not confirmed\n");
                if (can_retry) degraded = 1;
                else printf("status: NUMERICAL_FAILURE\n");
            }
            free(xu); free(ur);
        } else if (r == 3) {
            printf("status: ITERATION_LIMIT\n");
        } else if (r == SOLVE_STOPPED) {
            printf("status: STOPPED\n");
        } else if (r == SOLVE_NUMERICAL) {
            /* engine-surfaced numerics: the rawer attempt may still certify */
            if (can_retry) degraded = 1;
            else printf("status: NUMERICAL_FAILURE\n");
        } else if (r == SOLVE_INVALID) {
            printf("status: INVALID_MODEL\n");
        } else {
            int mo_n = model->n, mo_m = model->m;
            double *xo = (double*)malloc((size_t)(mo_n > 0 ? mo_n : 1) * sizeof(double));
            double obj;
            if (!xo) { solver_destroy(s); if (ps) { lp_presolve_free(ps); lp_free(&red); }
                       lp_free(&lp); psolve_stop_set(NULL); psolve_frame_pop(&ef); return 1; }
            solver_optimum(s, xo, &obj);
            /* unified evidence object (roadmap 6.4): OPTIMAL prints only when
               the certificate layer re-verifies both primal feasibility and a
               closed bounded-LP Lagrangian dual bound from the engine duals.
               Under presolve the primal point and duals are transferred to
               ORIGINAL units first (7.1 postsolve maps); any map that cannot
               transfer degrades to the next, rawer attempt below. */
            {
                double *ylp = (double*)malloc((size_t)(mo_m > 0 ? mo_m : 1) * sizeof(double));
                int ok = 0;
                double *xc = xo, *yc = ylp;
                double objc = obj;
                double *xp = NULL, *yp = NULL;
                if (ylp) {
                    solver_duals(s, ylp);
                    for (int i = 0; i < mo_m; i++) ylp[i] *= (double)s->mlt[i];
                    if (ps) {
                        xp = (double*)malloc((size_t)lp.n * sizeof(double));
                        yp = (double*)malloc((size_t)(lp.m > 0 ? lp.m : 1) * sizeof(double));
                        if (xp && yp &&
                            lp_presolve_postsolve_x(ps, xo, xp) == 0 &&
                            lp_presolve_postsolve_duals(ps, ylp, xp, yp) == 0) {
                            xc = xp; yc = yp;
                            objc = 0.0;
                            for (int j = 0; j < lp.n; j++) objc += lp.c[j] * xc[j];
                        } else {
                            fprintf(stderr, "psv: presolve evidence transfer unsupported\n");
                            degraded = 1;
                        }
                    }
                    if (!degraded) {
                        double walk = 0.0;
                        int note = ps ? lp_presolve_unbounded_note(ps, &walk) : -1;
                        if (note >= 0) {
                            /* an open-direction eliminated empty column
                               makes the ORIGINAL model unbounded: point +
                               unit ray, certified through the same lane */
                            double *d = (double*)calloc((size_t)lp.n, sizeof(double));
                            if (d) {
                                PsvCert cl; psv_fill_lp(&cl, PSVK_LP_UNBOUNDED, &lp);
                                d[note] = walk;
                                cl.x = xc; cl.ray = d; cl.obj = objc;
                                cl.gt_box = 1e-6; cl.gt_row = 1e-5; cl.dt_dj = 1e-9; /* TOLSHEET TOL-CERT-DEFBOX */ /* TOLSHEET TOL-CERT-DEFROW */ /* TOLSHEET TOL-CERT-DEFDJ */
                                ok = (psv_cert_check(&cl) == PSV_OK);
                                if (ok) printf("status: UNBOUNDED\n");
                                else fprintf(stderr, "psv: presolve unbounded note not confirmed\n");
                            }
                            free(d);
                        } else {
                            PsvCert cl; psv_fill_lp(&cl, PSVK_LP_OPTIMAL, &lp);
                            cl.x = xc; cl.y = yc; cl.obj = objc;
                            cl.gt_box = 1e-6; cl.gt_row = 1e-5; /* TOLSHEET TOL-CERT-DEFBOX */ /* TOLSHEET TOL-CERT-DEFROW */
                            cl.dt_dj = 1e-9; cl.dt_gap = 1e-7; cl.dt_obj = 1e-9; /* TOLSHEET TOL-CERT-DEFDJ */ /* TOLSHEET TOL-CERT-DEFGAP */ /* TOLSHEET TOL-CERT-DEFOBJ */
                            ok = (psv_cert_check(&cl) == PSV_OK);
                            if (ok) {
                                printf("status: OPTIMAL\n");
                                printf("objective: %.15g\n", objc);
                                if (print) {
                                    for (int j = 0; j < lp.n; j++)
                                        printf("x[%d] = %.17g\n", j, xc[j]);
                                }
                            } else {
                                /* maybe the model is actually unbounded and
                                   the "optimal" reduced answer is a box
                                   stop: never print, degrade honestly */
                                fprintf(stderr, "psv: lp_optimal certificate not confirmed\n");
                            }
                        }
                    }
                }
                if (!ok && !degraded) {
                    /* ylp alloc failure, failed transfer, or a rejected
                       certificate: the next, rawer attempt decides; the
                       last attempt prints the honest class */
                    if (can_retry) degraded = 1;
                    else printf("status: NUMERICAL_FAILURE\n");
                }
                free(ylp);
                free(xp); free(yp); free(xo);
            }
        }

        if (!degraded) printed = 1;
        solver_destroy(s);
        if (ps) { lp_presolve_free(ps); lp_free(&red); }
    }   /* attempt ladder */

    lp_free(&lp);
    tlimit_disarm();
    psolve_stop_set(NULL);
    psolve_frame_pop(&ef);
    return 0;
}
