#include "solver.h"
#include "qp.h"
#include "mip.h"
#include "fx.h"
#include "pgs.h"
#include "pgs_fixed.h"
#include "fzn.h"
#include "parser.h"
#include "err.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void test_solver_null_guards(void)
{
    /* Null and degenerate inputs must return cleanly without crashing */
    assert(solver_create(NULL) == NULL);
    solver_destroy(NULL);
    assert(solver_solve(NULL) == -1);
    assert(solver_warm_solve(NULL) == -1);
    assert(solver_feasible(NULL) == 0);
    solver_set_objective(NULL, NULL, 0);
    solver_set_bounds(NULL, NULL, NULL);
    assert(solver_add_row(NULL, NULL, 0.0, '<') == -1);
    solver_duals(NULL, NULL);
    solver_reduced_costs(NULL, NULL);
    solver_optimum(NULL, NULL, NULL);
}

static void test_qp_null_guards(void)
{
    QPResult res;
    qp_solve(NULL, NULL);
    qp_result_free(NULL);

    QP qp; memset(&qp, 0, sizeof(qp));
    qp_solve(&qp, &res);
    assert(res.status == -1);
    qp_result_free(&res);

    qp.n = 2; qp.m = 1;
    qp.Q = NULL; qp.c = NULL;
    qp_solve(&qp, &res);
    assert(res.status == -1);
    qp_result_free(&res);
}

static void test_pgs_null_guards(void)
{
    PGSResult res;
    pgs_solve(NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    pgs_matvec(NULL, 0, NULL, NULL);

    PGSOptions opt = { 0, 10, 1.0, 0.0 };
    pgs_solve(&opt, NULL, NULL, NULL, NULL, NULL, &res);
    assert(res.status == 1);

    PGSFixedOptions fopt = { 0, 10, 1, 1, 0 };
    pgsf_solve(NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    pgsf_matvec(NULL, 0, NULL, NULL);
    pgsf_solve(&fopt, NULL, NULL, NULL, NULL, NULL, &res);
    assert(res.status == 1);
}

static void test_fx_null_guards(void)
{
    FxResult res;
    assert(fx_solve(NULL, NULL) == FX_ALLOC_FAIL);
    assert(fx_solve_wide(NULL, NULL) == FX_ALLOC_FAIL);
    fx_result_free(NULL);
    fx_free(NULL);

    FxLP lp; memset(&lp, 0, sizeof(lp));
    assert(fx_solve(&lp, &res) == FX_INFEASIBLE);
    fx_result_free(&res);
}

static void test_fzn_null_guards(void)
{
    assert(fz_read(NULL, NULL) == -1);
    fz_solve(NULL, NULL);
    fz_print_solution(NULL, NULL);
    fz_solution_free(NULL);
    fz_model_free(NULL);

    FZSolution sol; memset(&sol, 0, sizeof(sol));
    fz_solve(NULL, &sol);
    assert(sol.status == 3);
    fz_solution_free(&sol);
}

int main(void)
{
    test_solver_null_guards();
    test_qp_null_guards();
    test_pgs_null_guards();
    test_fx_null_guards();
    test_fzn_null_guards();
    printf("ALL PUBLIC C API NULL/BOUND GUARDS PASSED\n");
    return 0;
}
