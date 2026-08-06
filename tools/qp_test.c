#include "qp.h"
#include <stdio.h>
#include <math.h>

/* Test 1: min 1/2 x^2 - 2x s.t. x <= 1  =>  min at x=1 (blocked), obj = 0.5-2 = -1.5 */
static void t1(void){
    int n=1,m=1;
    double Q[1]={1.0};
    double c[1]={-2.0};
    double A[1]={1.0};
    double b[1]={1.0};
    QP qp={n,m,Q,c,A,b,NULL};
    QPResult r; qp_solve(&qp,&r);
    printf("T1: status=%d x=%g obj=%g (expect x=1, obj=-1.5)\n",r.status,r.x[0],r.obj);
    qp_result_free(&r);
}

/* Test 2: min 1/2 x^2 - 2x s.t. x<=5 (inactive) => x=2, obj=-2 */
static void t2(void){
    int n=1,m=1;
    double Q[1]={1.0}; double c[1]={-2.0};
    double A[1]={1.0}; double b[1]={5.0};
    QP qp={n,m,Q,c,A,b,NULL};
    QPResult r; qp_solve(&qp,&r);
    printf("T2: x=%g obj=%g (expect x=2, obj=-2)\n",r.x[0],r.obj);
    qp_result_free(&r);
}

/* Test 3: 2D min 1/2(x^2+y^2) s.t. x+y>=2, x>=0,y>=0.
   We use -a <= b form: -x-y<=-2, -x<=0, -y<=0.
   Solution: x=y=1, obj=1. */
static void t3(void){
    int n=2,m=3;
    double Q[4]={1,0,0,1};      /* col-major identity */
    double c[2]={0,0};
    double A[6]={ -1,-1,  -1,0,  0,-1 };  /* row-major */
    double b[3]={ -2, 0, 0 };
    QP qp={n,m,Q,c,A,b,NULL};
    QPResult r; qp_solve(&qp,&r);
    printf("T3: status=%d x=%g,%g obj=%g (expect 1,1 obj=1)\n",r.status,r.x[0],r.x[1],r.obj);
    qp_result_free(&r);
}

/* Test 4: portfolio-like: min 1/2 x'Qx with Q=[[2,1],[1,2]], c=0, s.t. x>=0, x1+x2>=... 
   Use simple: min 1/2(x^2+y^2) + c'x, x>=0, box. Let's do min x^2+y^2 -2x -3y s.t. x>=0,y>=0 -> x=1,y=1.5 obj=-3.25 */
static void t4(void){
    int n=2,m=2;
    double Q[4]={2,0,0,2};   /* col-major: Q=[[2,0],[0,2]] */
    double c[2]={-2,-3};
    double A[4]={ -1,0,  0,-1 };
    double b[2]={0,0};
    QP qp={n,m,Q,c,A,b,NULL};
    QPResult r; qp_solve(&qp,&r);
    printf("T4: x=%g,%g obj=%g (expect 1, 1.5 obj=-3.25)\n",r.x[0],r.x[1],r.obj);
    qp_result_free(&r);
}

int main(void){
    t1(); t2(); t3(); t4();
    return 0;
}
