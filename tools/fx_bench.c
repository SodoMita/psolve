#include "solver.h"
#include "parser.h"
#include "fx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Benchmark: compare the double-precision revised-simplex LP core (solver.c)
 * against the fixed-point exact-rational LP core (fx.c) on the same .lp files.
 * Reports objective agreement and solve time for each. */

static double now(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec/1e9; }

int main(int argc,char**argv){
    if(argc<2){ fprintf(stderr,"usage: %s <a.lp> [<b.lp> ...]\n",argv[0]); return 1; }
    int reps = 3;
    printf("%-28s %12s %12s %12s %12s %14s %10s\n",
           "file","obj_double","obj_fixed","agree","dbl_us","fx_us","fx/dbl");
    int tot_agree=0, tot=0; double sum_ratio=0; int nratio=0;
    for(int a=1;a<argc;a++){
        const char*path=argv[a];

        LP d; memset(&d,0,sizeof(d));
        if(lp_read(path,&d)!=0){ printf("%-28s (double parse error)\n",path); continue; }
        Solver*s=solver_create(&d);
        solver_solve(s);
        double dbl_obj; double*xo=(double*)malloc((size_t)(d.n?d.n:1)*sizeof(double));
        solver_optimum(s,xo,&dbl_obj);
        int dstatus=s->status_out;

        FxLP fx; memset(&fx,0,sizeof(fx));
        if(fx_read(path,&fx)!=0){ printf("%-28s (fx parse error)\n",path); solver_destroy(s); lp_free(&d); continue; }
        FxResult fr; memset(&fr,0,sizeof(fr));
        int fstatus=fx_solve(&fx,&fr);
        double fx_obj=fx_todouble(fr.obj);

        /* time double */
        double t0=now(); for(int r=0;r<reps;r++){ Solver*s2=solver_create(&d); solver_solve(s2); solver_destroy(s2);} double dt=(now()-t0)/reps*1e6;
        /* time fixed */
        double f0=now(); for(int r=0;r<reps;r++){ FxResult fr2; memset(&fr2,0,sizeof(fr2)); fx_solve(&fx,&fr2); fx_result_free(&fr2);} double ft=(now()-f0)/reps*1e6;

        /* agreement on status */
        int agree = (dstatus==0 && fstatus==0);
        char ag[8]; snprintf(ag,sizeof(ag),"%s", agree?"yes":(dstatus==fstatus?"ok":"NO"));

        char dbu[24],fbu[24]; snprintf(dbu,sizeof(dbu),"%.1f",dt); snprintf(fbu,sizeof(fbu),"%.1f",ft);
        char dbo[24],fbo[24]; snprintf(dbo,sizeof(dbo),"%.10g",dbl_obj); snprintf(fbo,sizeof(fbo),"%.10g",fx_obj);
        printf("%-28s %12s %12s %10s %9s%3s %9s%3s %8.2f\n",
               path, agree?dbo:"-", (fstatus==0)?fbo:"-", ag, dbu,"", fbu,"",
               (ft>0&&dt>0)?ft/dt:0);

        if(agree){ tot_agree++; } tot++;
        if(dt>0&&ft>0){ sum_ratio+=ft/dt; nratio++; }
        fx_result_free(&fr); fx_free(&fx); free(xo);
        solver_destroy(s); lp_free(&d);
    }
    printf("SUMMARY: %d/%d solved-instances agree on objective; mean fx/double solve-time ratio = %.2f\n",
           tot_agree,tot,nratio?sum_ratio/nratio:0);
    return 0;
}
