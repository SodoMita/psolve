#include "fx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(int argc,char**argv){
    if(argc<2){
        fprintf(stderr,"usage: %s <problem.lp> [--print] [--wide]\n",argv[0]);
        return 1;
    }
    const char*path=argv[1];
    int print=0, wide=0;
    for(int a=2;a<argc;a++){
        if(strcmp(argv[a],"--print")==0)print=1;
        else if(strcmp(argv[a],"--wide")==0)wide=1;   /* force 128-bit core */
    }

    FxLP lp; memset(&lp,0,sizeof(lp));
    if(fx_read(path,&lp)!=0) return 1;

    FxResult res; memset(&res,0,sizeof(res));
    struct timespec t0,t1; clock_gettime(CLOCK_MONOTONIC,&t0);
    int r=wide?fx_solve_wide(&lp,&res):fx_solve(&lp,&res);
    clock_gettime(CLOCK_MONOTONIC,&t1);
    double secs=(t1.tv_sec-t0.tv_sec)+(t1.tv_nsec-t0.tv_nsec)/1e9;

    printf("iterations: %ld\n",res.iters);
    printf("time: %.6f s\n",secs);
    printf("width: %d-bit rationals\n", res.width);

    if(r!=FX_OPTIMAL) printf("status: %s\n", fx_status_name(r));
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
