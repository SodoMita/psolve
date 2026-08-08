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

static void test_batch_apis(void)
{
    assert(pgs_batch_solve(-1,NULL,NULL,NULL,NULL,NULL,NULL,NULL)==-1);
    assert(pgs_batch_solve(0,NULL,NULL,NULL,NULL,NULL,NULL,NULL)==0);
    assert(pgs_batch_solve(1,NULL,NULL,NULL,NULL,NULL,NULL,NULL)==-1);

    PGSOptions fo[2]={{1,10,1.0,1e-12},{1,10,1.0,1e-12}};
    double A0[1]={2},A1[1]={1},b0[1]={-6},b1[1]={-2};
    double lo0[1]={0},lo1[1]={0},hi0[1]={10},hi1[1]={1},x0[1]={0},x1[1]={0};
    const double *As[2]={A0,A1},*bs[2]={b0,b1},*los[2]={lo0,lo1},*his[2]={hi0,hi1};
    double *xs[2]={x0,x1};PGSResult rr[2];
    assert(pgs_batch_solve(2,fo,As,bs,los,his,xs,rr)>0);
    assert(rr[0].status==0&&rr[1].status==0&&x0[0]==3.0&&x1[0]==1.0);

    assert(pgsf_batch_solve(-1,NULL,NULL,NULL,NULL,NULL,NULL,NULL)==-1);
    assert(pgsf_batch_solve(0,NULL,NULL,NULL,NULL,NULL,NULL,NULL)==0);
    PGSFixedOptions io[2]={{1,10,1,1,1},{1,10,1,1,1}};
    int64_t iA0[1]={2},iA1[1]={1},ib0[1]={-6},ib1[1]={-2};
    int64_t ilo0[1]={0},ilo1[1]={0},ihi0[1]={10},ihi1[1]={1},ix0[1]={0},ix1[1]={0};
    const int64_t *iAs[2]={iA0,iA1},*ibs[2]={ib0,ib1},*ilos[2]={ilo0,ilo1},*ihis[2]={ihi0,ihi1};
    int64_t *ixs[2]={ix0,ix1};
    assert(pgsf_batch_solve(2,io,iAs,ibs,ilos,ihis,ixs,rr)>0);
    assert(rr[0].status==0&&rr[1].status==0&&ix0[0]==3&&ix1[0]==1);
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
    test_batch_apis();
    test_parser_fzn_apis();
    puts("ALL PUBLIC C API INVALID-INPUT GUARDS PASSED");
    return 0;
}
