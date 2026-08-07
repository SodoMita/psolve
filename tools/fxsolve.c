#include "fx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(int argc,char**argv){
    if(argc<2){
        fprintf(stderr,"usage: %s <problem.lp> [--print]\n",argv[0]);
        return 1;
    }
    const char*path=argv[1];
    int print=0;
    for(int a=2;a<argc;a++) if(strcmp(argv[a],"--print")==0)print=1;

    FxLP lp; memset(&lp,0,sizeof(lp));
    if(fx_read(path,&lp)!=0) return 1;

    FxResult res; memset(&res,0,sizeof(res));
    struct timespec t0,t1; clock_gettime(CLOCK_MONOTONIC,&t0);
    int r=fx_solve(&lp,&res);
    clock_gettime(CLOCK_MONOTONIC,&t1);
    double secs=(t1.tv_sec-t0.tv_sec)+(t1.tv_nsec-t0.tv_nsec)/1e9;

    printf("iterations: %ld\n",res.iters);
    printf("time: %.6f s\n",secs);

    if(r==1) printf("status: INFEASIBLE\n");
    else if(r==2) printf("status: UNBOUNDED\n");
    else if(r==3) printf("status: ITERATION_LIMIT\n");
    else {
        char buf[80]; fx_fmt(buf,sizeof(buf),res.obj,12);
        printf("status: OPTIMAL\n");
        printf("objective (exact): %lld/%lld\n", (long long)res.obj.num, (long long)res.obj.den);
        printf("objective (dec):   %s\n", buf);
        if(print){
            for(int j=0;j<lp.n;j++){
                char xb[80]; fx_fmt(xb,sizeof(xb),res.x[j],12);
                printf("x[%d] = %s  (exact %lld/%lld)\n", j, xb,
                       (long long)res.x[j].num,(long long)res.x[j].den);
            }
        }
    }
    fx_result_free(&res);
    fx_free(&lp);
    return 0;
}
