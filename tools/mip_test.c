#include "mip.h"
#include "solver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Test 1: knapsack-style
   max 3x0 + 4x1 + 2x2 s.t. x0+2x1 <= 4, 3x0+2x2 <= 5, x_i integer, >=0
   Solve by enumeration to verify. */
static void t1(void){
    int n=3,m=2;
    double c[3]={3,4,2};
    double b[2]={4,5};
    char rel[2]={'<','<'};
    double l[3]={0,0,0}, u[3]={LP_INF,LP_INF,LP_INF};
    /* CSC */
    int Acolptr[4]={0,2,3,4};
    int Arow[4]={0,1, 0, 1};
    double Aval[4]={1,3, 2, 2};
    unsigned char isint[3]={1,1,1};
    MIP mip; memset(&mip,0,sizeof(mip)); mip.n=n;mip.m=m;mip.c=c;mip.Acolptr=Acolptr;mip.Arow=Arow;mip.Aval=Aval;
    mip.rel=rel;mip.b=b;mip.l=l;mip.u=u;mip.maximize=1;mip.isint=isint;mip.mip_gap=0;mip.node_limit=100000;mip.lp_iter_limit=2000000;
    MIPResult r; mip_solve(&mip,&r);
    printf("T1: status=%d obj=%g x=[%g %g %g] (expect x=[0 2 2] obj=12)\n",r.status,r.obj,r.x[0],r.x[1],r.x[2]);
    mip_result_free(&r);
}

/* Test 2: mixed integer
   max x0 + x1 s.t. x0 + x1 <= 4, x0 integer, x1 real, x0>=0, 0<=x1<=3
   Opt: x0=3, x1=1? no bound x1<=3 and x0+x1<=4, x0 int -> x0=1,x1=3 obj=4 or x0=4,x1=0 obj=4.
   Actually max x0+x1 with x0 int>=0, 0<=x1<=3, x0+x1<=4.
   Try x1=3 -> x0<=1 -> obj 4. x1=0 -> x0<=4 -> obj 4. So obj=4. */
static void t2(void){
    int n=2,m=1;
    double c[2]={1,1};
    double b[1]={4};
    char rel[1]={'<'};
    double l[2]={0,0}, u[2]={LP_INF,3};
    int Acolptr[3]={0,1,2};
    int Arow[2]={0,0};
    double Aval[2]={1,1};
    unsigned char isint[2]={1,0};
    MIP mip; memset(&mip,0,sizeof(mip)); mip.n=n;mip.m=m;mip.c=c;mip.Acolptr=Acolptr;mip.Arow=Arow;mip.Aval=Aval;
    mip.rel=rel;mip.b=b;mip.l=l;mip.u=u;mip.maximize=1;mip.isint=isint;mip.mip_gap=0;mip.node_limit=100000;mip.lp_iter_limit=2000000;
    MIPResult r; mip_solve(&mip,&r);
    printf("T2: status=%d obj=%g x=[%g %g] (expect obj=4)\n",r.status,r.obj,r.x[0],r.x[1]);
    mip_result_free(&r);
}

int main(void){
    t1(); t2();
    return 0;
}
