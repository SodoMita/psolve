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
#include <math.h>

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

    /* Coupled 2D members: verifies the batch stride/index routing actually
       carries full n*n matrices per system (a 1D case cannot trip that),
       plus a box-clamped member (ported from the historical
       arena_batch_test.c on the Aug arena lineage). */
    {
        PGSOptions go[3]={{2,20,1.0,1e-9},{2,20,1.0,1e-9},{2,20,1.0,1e-9}};
        double G0[4]={2.0,0.5,0.5,2.0},   G1[4]={3.0,0.0,0.0,4.0},   G2[4]={1.0,0.0,0.0,1.0};
        double gb0[2]={-3.0,-2.0},        gb1[2]={-6.0,-8.0},         gb2[2]={-1.0,-1.0};
        double gl0[2]={0.0,0.0},          gl1[2]={0.0,0.0},           gl2[2]={0.0,0.0};
        double gh0[2]={10.0,10.0},        gh1[2]={5.0,5.0},           gh2[2]={0.5,0.5};
        double gx0[2]={0.0,0.0},          gx1[2]={0.0,0.0},           gx2[2]={0.0,0.0};
        const double *GAs[3]={G0,G1,G2},*Gbs[3]={gb0,gb1,gb2},*Glos[3]={gl0,gl1,gl2},*Ghis[3]={gh0,gh1,gh2};
        double *Gxs[3]={gx0,gx1,gx2};
        PGSResult gr[3];
        assert(pgs_batch_solve(3,go,GAs,Gbs,Glos,Ghis,Gxs,gr)>0);
        assert(gr[0].status==0&&gr[1].status==0&&gr[2].status==0);
        assert(fabs(gx0[0]-(4.0/3.0))<1e-5 && fabs(gx0[1]-(2.0/3.0))<1e-5);
        assert(fabs(gx1[0]-2.0)<1e-5 && fabs(gx1[1]-2.0)<1e-5);
        assert(fabs(gx2[0]-0.5)<1e-5 && fabs(gx2[1]-0.5)<1e-5);
    }

    assert(pgsf_batch_solve(-1,NULL,NULL,NULL,NULL,NULL,NULL,NULL)==-1);
    assert(pgsf_batch_solve(0,NULL,NULL,NULL,NULL,NULL,NULL,NULL)==0);
    PGSFixedOptions io[2]={{1,10,1,1,1},{1,10,1,1,1}};
    int64_t iA0[1]={2},iA1[1]={1},ib0[1]={-6},ib1[1]={-2};
    int64_t ilo0[1]={0},ilo1[1]={0},ihi0[1]={10},ihi1[1]={1},ix0[1]={0},ix1[1]={0};
    const int64_t *iAs[2]={iA0,iA1},*ibs[2]={ib0,ib1},*ilos[2]={ilo0,ilo1},*ihis[2]={ihi0,ihi1};
    int64_t *ixs[2]={ix0,ix1};
    assert(pgsf_batch_solve(2,io,iAs,ibs,ilos,ihis,ixs,rr)>0);
    assert(rr[0].status==0&&rr[1].status==0&&ix0[0]==3&&ix1[0]==1);

    /* Fixed-point 2x2 member with an upper-clamped box: integer batch
       routing and clamp interaction, scale 256 (same provenance as the
       coupled-floating case above). */
    {
        PGSFixedOptions to[2]={{2,20,1,1,0},{2,20,1,1,0}};
        int64_t T0[4]={2,0,0,2},          T1[4]={1,0,0,1};
        int64_t tb0[2]={-768,-512},       tb1[2]={-256,-256};
        int64_t tl0[2]={0,0},             tl1[2]={0,0};
        int64_t th0[2]={2560,2560},       th1[2]={128,128};
        int64_t tx0[2]={0,0},             tx1[2]={0,0};
        const int64_t *TAs[2]={T0,T1},*Tbs[2]={tb0,tb1},*Tlos[2]={tl0,tl1},*This[2]={th0,th1};
        int64_t *Txs[2]={tx0,tx1};
        PGSResult tr[2];
        assert(pgsf_batch_solve(2,to,TAs,Tbs,Tlos,This,Txs,tr)>0);
        assert(tx0[0]==384 && tx0[1]==256);
        assert(tx1[0]==128 && tx1[1]==128);
    }
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
