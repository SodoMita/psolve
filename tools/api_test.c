#include "solver.h"
#include "qp.h"
#include "mip.h"
#include "fx.h"
#include "pgs.h"
#include "pgs_fixed.h"
#include "fzn.h"
#include "parser.h"
#include "lu.h"
#include "splu.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_linear_apis(void)
{
    assert(solver_create(NULL)==NULL);
    assert(solver_solve(NULL)==SOLVE_INVALID);
    assert(solver_warm_solve(NULL)==SOLVE_INVALID);
    assert(solver_feasible(NULL)==0);
    assert(solver_add_row(NULL,NULL,0.0,'<')==-1);
    solver_destroy(NULL);
    solver_set_objective(NULL,NULL,0);
    solver_set_bounds(NULL,NULL,NULL);
    solver_duals(NULL,NULL);
    solver_reduced_costs(NULL,NULL);
    solver_optimum(NULL,NULL,NULL);

    /* Invalid CSC must be rejected before any row/value dereference. */
    double z=0.0,l=0.0,u=1.0;
    int badptr[2]={1,0};
    LP lp={1,0,&z,badptr,NULL,NULL,NULL,NULL,&l,&u,1};
    assert(solver_create(&lp)==NULL);

    MIPResult mr;
    mip_solve(NULL,NULL);
    mip_solve(NULL,&mr);
    assert(mr.status==MIP_INVALID&&mr.x==NULL);
    mip_result_free(NULL);
    mip_result_free(&mr);

    /* The empty 0x0 factorization remains a valid identity operation. */
    assert(lu_factor(NULL,0,NULL)==0);
    lu_solve(NULL,NULL,0,NULL,NULL);
    lu_solve_t(NULL,NULL,0,NULL,NULL);
    SPLU sp;memset(&sp,0,sizeof(sp));
    assert(splu_factor(&sp,NULL,NULL,NULL,0)==0);
    assert(splu_factor(NULL,NULL,NULL,NULL,0)==-1);
    splu_solve(NULL,NULL,NULL);
    splu_solve_t(NULL,NULL,NULL);
    splu_free(NULL);
}

static void test_qp_fx_apis(void)
{
    QPResult qr;
    qp_solve(NULL,NULL);
    qp_solve(NULL,&qr);
    assert(qr.status==QP_INVALID);
    qp_result_free(NULL);
    qp_result_free(&qr);

    FxResult fr;
    assert(fx_solve(NULL,NULL)==FX_INVALID);
    assert(fx_solve_wide(NULL,NULL)==FX_INVALID);
    assert(fx_solve(NULL,&fr)==FX_INVALID&&fr.status==FX_INVALID);
    fx_result_free(NULL);
    fx_result_free(&fr);
    fx_free(NULL);
}

static void test_physics_apis(void)
{
    PGSResult r;
    pgs_solve(NULL,NULL,NULL,NULL,NULL,NULL,NULL);
    pgs_solve(NULL,NULL,NULL,NULL,NULL,NULL,&r);
    assert(r.status==PGS_INVALID&&r.iters==0);
    pgs_matvec(NULL,0,NULL,NULL);

    pgsf_solve(NULL,NULL,NULL,NULL,NULL,NULL,NULL);
    pgsf_solve(NULL,NULL,NULL,NULL,NULL,NULL,&r);
    assert(r.status==PGS_INVALID&&r.iters==0);
    pgsf_matvec(NULL,0,NULL,NULL);
}

static void test_parser_fzn_apis(void)
{
    LP lp;
    assert(lp_read(NULL,NULL)==-1);
    assert(lp_read(NULL,&lp)==-1);
    lp_free(NULL);

    assert(fz_read(NULL,NULL)==-1);
    fz_solve(NULL,NULL);
    fz_print_solution(NULL,NULL);
    fz_solution_free(NULL);
    fz_model_free(NULL);
    FZSolution sol;memset(&sol,0,sizeof(sol));
    fz_solve(NULL,&sol);
    assert(sol.status==3);
    fz_solution_free(&sol);
}

int main(void)
{
    test_linear_apis();
    test_qp_fx_apis();
    test_physics_apis();
    test_parser_fzn_apis();
    puts("ALL PUBLIC C API INVALID-INPUT GUARDS PASSED");
    return 0;
}
