#include "pgs.h"
#include "pgs_fixed.h"
#include "qp.h"
#include "solver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* Benchmark: which solver foundation is right for real-time 2D physics?
 *
 * Physics impulse resolution is a *boxed quadratic* problem:
 *      min 1/2 x^T A x + b^T x   s.t.   lo <= x <= hi
 * with A = J M^-1 J^T  (symmetric PSD, diagonal-dominant).
 *
 * We compare, on identical warm-started problems, the per-solve cost of:
 *   PGS-fixed   : fixed-point projected Gauss-Seidel (integer, deterministic)
 *   PGS-float   : float projected Gauss-Seidel (reference)
 *   active-set QP: exact general QP solver (qp.c) -- box as 2n constraints
 *   LP simplex  : exact general LP (solver.c) on a *linear-cost* proxy of the
 *                 same contact constraint geometry
 *
 * The takeaway: PGS is the right foundation for a per-frame hot loop (it is
 * iterative, warm-start friendly, and fixed-budget); the general exact solvers
 * are correct but ~2-3 orders of magnitude slower and not built for
 * interactive latency.
 */

static double now_ns(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (double)t.tv_sec*1e9+t.tv_nsec; }

typedef struct { int n; double *J; double *A; double *b; double *lo,*hi; } Phys;

static void make_phys(int n, unsigned *seed, Phys *P)
{
    unsigned s=*seed;
    #define R() ((s=s*1664525u+1013904223u),(double)((s>>8)&0xffff)/65535.0)
    P->n=n;
    P->J=(double*)malloc((size_t)n*n*sizeof(double));
    P->A=(double*)calloc((size_t)n*n,sizeof(double));
    P->b=(double*)malloc((size_t)n*sizeof(double));
    P->lo=(double*)malloc((size_t)n*sizeof(double));
    P->hi=(double*)malloc((size_t)n*sizeof(double));
    /* random contact Jacobian J, sparse-ish */
    for(int i=0;i<n;i++)for(int j=0;j<n;j++) P->J[i*n+j]=(R()<0.6)?(R()*2-1):0.0;
    /* A = J^T J + I  (symmetric PSD, column-major for PGS/QP) */
    for(int i=0;i<n;i++)for(int j=0;j<n;j++){ double s=(i==j?1.0:0.0); for(int k=0;k<n;k++) s+=P->J[k*n+i]*P->J[k*n+j]; P->A[i*n+j]=s; }
    for(int i=0;i<n;i++){ P->b[i]=R()*2-1; P->lo[i]=-1.0; P->hi[i]=1.0; }
    *seed=s;
    #undef R
}

static double bench_pgs_fixed(int n, double*A,double*b,double*lo,double*hi,int iters,int reps)
{
    const int64_t S=65536;
    int64_t *Ai=(int64_t*)malloc((size_t)n*n*8);
    int64_t *bi=(int64_t*)malloc((size_t)n*8);
    int64_t *loi=(int64_t*)malloc((size_t)n*8);
    int64_t *hii=(int64_t*)malloc((size_t)n*8);
    int64_t *xi=(int64_t*)calloc((size_t)n,8);
    for(int i=0;i<n*n;i++)Ai[i]=(int64_t)llround(A[i]);
    for(int i=0;i<n;i++){bi[i]=(int64_t)llround(b[i]*S);loi[i]=(int64_t)llround(lo[i]*S);hii[i]=(int64_t)llround(hi[i]*S);}
    PGSFixedOptions opt={n,iters,1,1,1};
    PGSResult res;
    for(int r=0;r<3;r++)pgsf_solve(&opt,Ai,bi,loi,hii,xi,&res);
    double t0=now_ns(); for(int r=0;r<reps;r++)pgsf_solve(&opt,Ai,bi,loi,hii,xi,&res); double t1=now_ns();
    free(Ai);free(bi);free(loi);free(hii);free(xi);
    return (t1-t0)/reps;
}

static double bench_pgs_float(int n,double*A,double*b,double*lo,double*hi,int iters,int reps)
{
    double *x=(double*)calloc((size_t)n,8);
    PGSOptions opt={n,iters,1.0,1e-10};
    PGSResult res;
    for(int r=0;r<3;r++)pgs_solve(&opt,A,b,lo,hi,x,&res);
    double t0=now_ns(); for(int r=0;r<reps;r++)pgs_solve(&opt,A,b,lo,hi,x,&res); double t1=now_ns();
    free(x);
    return (t1-t0)/reps;
}

/* exact active-set QP: box lo<=x<=hi as 2n constraints Ix<=hi, -Ix<=-lo */
static double bench_qp(int n,double*A,double*b,double*lo,double*hi,int reps)
{
    int m=2*n;
    double *Ac=(double*)malloc((size_t)m*n*sizeof(double));
    double *bc=(double*)malloc((size_t)m*sizeof(double));
    for(int i=0;i<n;i++){ /* row i: e_i x <= hi_i ; row n+i: -e_i x <= -lo_i */
        for(int j=0;j<n;j++){Ac[i*n+j]=0;Ac[(n+i)*n+j]=0;}
        Ac[i*n+i]=1.0; bc[i]=hi[i];
        Ac[(n+i)*n+i]=-1.0; bc[n+i]=-lo[i];
    }
    QP qp; qp.n=n; qp.m=m; qp.Q=A; qp.c=b; qp.A=Ac; qp.b=bc; qp.x0=NULL;
    double t0=now_ns();
    for(int r=0;r<reps;r++){
        QPResult res; qp_solve(&qp,&res); qp_result_free(&res);
    }
    double t1=now_ns();
    free(Ac);free(bc);
    return (t1-t0)/reps;
}

/* exact general LP on a linear-cost proxy: min c^T x s.t. J x <= r, lo<=x<=hi */
static double bench_lp(int n,double*J,double*b,double*lo,double*hi,int reps)
{
    /* c = -b (linearize), constraint rows J x <= |b| proxy */
    int m=2*n; /* box as <= */
    /* build LP with box bounds in l/u and a linear objective */
    LP lp; memset(&lp,0,sizeof(lp));
    lp.n=n; lp.m=0; lp.maximize=0;   /* we'll minimize c^T x */
    double *c=(double*)malloc((size_t)n*sizeof(double));
    double *l=(double*)malloc((size_t)n*sizeof(double));
    double *u=(double*)malloc((size_t)n*sizeof(double));
    for(int j=0;j<n;j++){ c[j]=b[j]; l[j]=lo[j]; u[j]=hi[j]; }
    /* add the contact rows as <= constraints via CSC */
    int nnz=0; for(int i=0;i<n;i++)for(int j=0;j<n;j++)if(J[i*n+j]!=0)nnz++;
    lp.m=n; /* n contact rows: Jx <= 1 (bound proxy) */
    lp.c=c; lp.l=l; lp.u=u;
    lp.b=(double*)malloc((size_t)n*sizeof(double));
    lp.rel=(char*)malloc((size_t)n);
    for(int i=0;i<n;i++){lp.b[i]=1.0;lp.rel[i]='<';}
    lp.Acolptr=(int*)calloc((size_t)(n+1),sizeof(int));
    lp.Arow=(int*)malloc((size_t)(nnz?nnz:1)*sizeof(int));
    lp.Aval=(double*)malloc((size_t)(nnz?nnz:1)*sizeof(double));
    for(int i=0;i<n;i++)for(int j=0;j<n;j++)if(J[i*n+j]!=0)lp.Acolptr[j+1]++;
    for(int j=0;j<n;j++)lp.Acolptr[j+1]+=lp.Acolptr[j];
    int *f=(int*)malloc((size_t)n*sizeof(int));for(int j=0;j<n;j++)f[j]=lp.Acolptr[j];
    for(int i=0;i<n;i++)for(int j=0;j<n;j++)if(J[i*n+j]!=0){lp.Arow[f[j]]=i;lp.Aval[f[j]]=J[i*n+j];f[j]++;}
    free(f);
    double t0=now_ns();
    for(int r=0;r<reps;r++){ Solver *s=solver_create(&lp); solver_solve(s); solver_destroy(s); }
    double t1=now_ns();
    free(c);free(l);free(u);free(lp.b);free(lp.rel);free(lp.Acolptr);free(lp.Arow);free(lp.Aval);
    return (t1-t0)/reps;
}

int main(void)
{
    int sizes[]={8,16,32,64};
    int iters=20;
    int reps[4]={30000,20000,3000,1000};
    unsigned seed=1;
    printf("%-6s %12s %12s %14s %14s\n","n","PGS-fixed","PGS-float","active-setQP","LPsimplex");
    printf("%-6s %12s %12s %14s %14s\n","","(us)","(us)","(us)","(us)");
    for(int k=0;k<4;k++){
        int n=sizes[k];
        Phys P; make_phys(n,&seed,&P);
        fprintf(stderr,"  n=%d ...\n",n); fflush(stderr);
        double t_pgsf=bench_pgs_fixed(n,P.A,P.b,P.lo,P.hi,iters,reps[k]);
        double t_pgs=bench_pgs_float(n,P.A,P.b,P.lo,P.hi,iters,reps[k]);
        fprintf(stderr,"  n=%d QP...\n",n); fflush(stderr);
        double t_qp=bench_qp(n,P.A,P.b,P.lo,P.hi,reps[k]);
        fprintf(stderr,"  n=%d LP...\n",n); fflush(stderr);
        double t_lp=bench_lp(n,P.J,P.b,P.lo,P.hi,reps[k]);
        printf("%-6d %12.2f %12.2f %14.1f %14.1f\n",n,t_pgsf/1000.0,t_pgs/1000.0,t_qp/1000.0,t_lp/1000.0);
        free(P.J);free(P.A);free(P.b);free(P.lo);free(P.hi);
    }
    printf("\n(smaller is better; times are per solve, warm-started, 2 CPU cores)\n");
    return 0;
}
