#include "pgs_fixed.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* Fixed-point PGS microbenchmark: latency for realistic contact-cluster sizes. */

static double now_ns(void)
{
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec*1e9 + (double)t.tv_nsec;
}

static void make_system(int n, int64_t*A, int64_t*b, int64_t*lo, int64_t*hi,
                        int64_t*warm, unsigned*seed)
{
    unsigned s=*seed;
    #define R() ((s=s*1664525u+1013904223u),(int64_t)((s>>8)&0x3fff)-4096)
    for(int i=0;i<n;i++)for(int j=0;j<n;j++) A[i*n+j]=(i==j)?(n+1):0; /* PSD-ish diag */
    for(int i=0;i<n;i++)for(int j=0;j<n;j++){ int64_t v=R()/128; if(i!=j&&j>i){A[i*n+j]=v;A[j*n+i]=v;} }
    for(int i=0;i<n;i++){ b[i]=R()/256; lo[i]=-(1<<20); hi[i]=(1<<20); warm[i]=R()/256; }
    /* ensure PSD: A = A + I*makes diag large; fine */
    *seed=s;
    #undef R
}

static void bench(int n,int iters,int reps)
{
    static int64_t A[64*64],b[64],lo[64],hi[64],x[64];
    PGSFixedOptions opt={n,iters,1,1,1};
    PGSResult res; unsigned seed=1;
    make_system(n,A,b,lo,hi,x,&seed);
    for(int r=0;r<3;r++) pgsf_solve(&opt,A,b,lo,hi,x,&res);
    double t0=now_ns();
    for(int r=0;r<reps;r++) pgsf_solve(&opt,A,b,lo,hi,x,&res);
    double t1=now_ns();
    printf("  n=%-3d iters=%-4d  %8.1f ns/solve  (%6.2f us)  status=%d\n",
           n,iters,(t1-t0)/reps,(t1-t0)/reps/1000.0,res.status);
}

int main(void)
{
    printf("Fixed-point PGS microbenchmark (2 CPU cores)\n");
    bench(4,10,200000);
    bench(8,10,200000);
    bench(16,10,100000);
    bench(32,20,50000);
    bench(64,20,30000);
    return 0;
}
