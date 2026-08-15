#include "solver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Build a small canonical LP directly. */
static void build_lp(LP *lp){
    int n=4, m=3;
    memset(lp,0,sizeof(LP));
    lp->n=n; lp->m=m; lp->maximize=1;
    double cv[4]={3,4,2,1};
    double bv[3]={10,8,12};
    char relv[3]={'<','<','<'};
    lp->c=(double*)malloc(n*sizeof(double));
    lp->b=(double*)malloc(m*sizeof(double));
    lp->rel=(char*)malloc(m);
    lp->l=(double*)malloc(n*sizeof(double));
    lp->u=(double*)malloc(n*sizeof(double));
    for(int j=0;j<n;j++){lp->c[j]=cv[j];lp->l[j]=0;lp->u[j]=LP_INF;}
    for(int i=0;i<m;i++){lp->b[i]=bv[i];lp->rel[i]=relv[i];}
    /* CSC: rows: r0: x0+2x1; r1: x1+x2+x3; r2: 2x0+3x2+x3+x1 */
    int nz=9;
    lp->Acolptr=(int*)calloc(n+1,sizeof(int));
    lp->Arow=(int*)malloc(nz*sizeof(int));
    lp->Aval=(double*)malloc(nz*sizeof(double));
    /* col0: (r0,1),(r2,2)
       col1: (r0,2),(r1,1),(r2,1)
       col2: (r1,1),(r2,3)
       col3: (r1,1),(r2,1) */
    int k=0;
    int col[9]={0,0,  1,1,1,  2,2,  3,3};
    int row[9]={0,2,  0,1,2,  1,2,  1,2};
    double val[9]={1,2,  2,1,1,  1,3,  1,1};
    for(int t=0;t<nz;t++) lp->Acolptr[col[t]+1]++;
    for(int j=0;j<n;j++) lp->Acolptr[j+1]+=lp->Acolptr[j];
    int *f=(int*)malloc(n*sizeof(int)); for(int j=0;j<n;j++)f[j]=lp->Acolptr[j];
    for(int t=0;t<nz;t++){lp->Arow[f[col[t]]]=row[t];lp->Aval[f[col[t]]]=val[t];f[col[t]]++;}
    free(f);
}

static void free_lp(LP*lp){free(lp->c);free(lp->b);free(lp->rel);free(lp->l);free(lp->u);free(lp->Acolptr);free(lp->Arow);free(lp->Aval);}

int main(void){
    int fails=0;
    /* baseline LP */
    LP lp; build_lp(&lp);
    Solver *s1=solver_create(&lp);
    Solver *s2=solver_create(&lp);
    solver_solve(s1);
    solver_solve(s2);
    double x1[4],o1,x2[4],o2;
    solver_optimum(s1,x1,&o1);
    printf("baseline: obj=%.6g\n",o1);

    /* 1. objective change: new c = [1,5,3,2], warm solve s1 vs fresh s2 */
    double nc[4]={1,5,3,2};
    solver_set_objective(s1,nc,1);
    solver_warm_solve(s1);
    lp.maximize=1; for(int j=0;j<4;j++)lp.c[j]=nc[j];
    /* fresh: create new solver from modified lp */
    solver_destroy(s2); s2=solver_create(&lp); solver_solve(s2);
    solver_optimum(s1,x1,&o1); solver_optimum(s2,x2,&o2);
    printf("obj-change: warm obj=%.6g fresh obj=%.6g  %s\n",o1,o2,(fabs(o1-o2)<1e-6)?"PASS":"FAIL");
    if(fabs(o1-o2)>=1e-6) fails++;

    /* 2. bound change: tighten x0 <= 2 */
    double l2[4]={0,0,0,0},u2[4]={2,LP_INF,LP_INF,LP_INF};
    solver_set_bounds(s1,l2,u2);
    solver_warm_solve(s1);
    solver_destroy(s2); s2=solver_create(&lp); 
    { for(int j=0;j<4;j++){lp.l[j]=l2[j];lp.u[j]=u2[j];} }
    solver_solve(s2);
    solver_optimum(s1,x1,&o1); solver_optimum(s2,x2,&o2);
    printf("bound-change: warm obj=%.6g fresh obj=%.6g  %s\n",o1,o2,(fabs(o1-o2)<1e-6)?"PASS":"FAIL");
    if(fabs(o1-o2)>=1e-6) fails++;

    /* 3. add row: x0 + x1 + x2 + x3 <= 5  (likely violated -> reinit/Phase I) */
    double arow[4]={1,1,1,1};
    solver_add_row(s1,arow,5.0,'<');
    solver_solve(s1);
    /* add same row to lp first, THEN create fresh s2 from it */
    {
        int m2=lp.m+1, n2=lp.n;
        int nz2=lp.Acolptr[n2]+4;
        int *np=(int*)calloc(n2+1,sizeof(int));
        int *nr=(int*)malloc(nz2*sizeof(int));
        double *nv=(double*)malloc(nz2*sizeof(double));
        double Ad[3][4]; for(int i=0;i<3;i++)for(int j=0;j<4;j++)Ad[i][j]=0;
        for(int j=0;j<4;j++)for(int k=lp.Acolptr[j];k<lp.Acolptr[j+1];k++)Ad[lp.Arow[k]][j]=lp.Aval[k];
        double Ad2[4][4]; for(int j=0;j<4;j++){Ad2[0][j]=Ad[0][j];Ad2[1][j]=Ad[1][j];Ad2[2][j]=Ad[2][j];Ad2[3][j]=arow[j];}
        np[0]=0;
        for(int j=0;j<4;j++){ int cnt=0; for(int i=0;i<4;i++) if(Ad2[i][j]!=0) cnt++; np[j+1]=np[j]+cnt; }
        int *ff=(int*)malloc(4*sizeof(int));for(int j=0;j<4;j++)ff[j]=np[j];
        for(int j=0;j<4;j++)for(int i=0;i<4;i++)if(Ad2[i][j]!=0){nr[ff[j]]=i;nv[ff[j]]=Ad2[i][j];ff[j]++;}
        free(ff);
        free(lp.Acolptr);free(lp.Arow);free(lp.Aval);
        lp.Acolptr=np;lp.Arow=nr;lp.Aval=nv;
        lp.m=4; lp.b=(double*)realloc(lp.b,4*sizeof(double)); lp.b[3]=5.0;
        lp.rel=(char*)realloc(lp.rel,4); lp.rel[3]='<';
    }
    solver_destroy(s2); s2=solver_create(&lp);
    solver_solve(s2);
    solver_optimum(s1,x1,&o1); solver_optimum(s2,x2,&o2);
    printf("add-row: warm obj=%.6g fresh obj=%.6g  %s\n",o1,o2,(fabs(o1-o2)<1e-6)?"PASS":"FAIL");
    if(fabs(o1-o2)>=1e-6) fails++;

    /* 4. warm-start honesty: a warm solve that still needs pivots when the
       iteration cap is already exhausted must report 3 (iteration limit),
       never 0 (OPTIMAL of an unproven vertex).  Pre-fix solver_warm_solve
       ignored r==-1 and fell through to the optimal return whenever the
       halted basis happened to stay primal-feasible -- a fabricated optimum
       that B&B warm starts would have used as a bound. */
    {
        LP lp2; build_lp(&lp2);
        Solver *w = solver_create(&lp2);
        solver_solve(w);                    /* cold solve: consumes iterations */
        double nc2[4]={1,5,3,2};
        solver_set_objective(w,nc2,1);      /* different objective: warm needs pivots */
        w->iteration_limit = 1;             /* cap below the already-spent count */
        int rw = solver_warm_solve(w);
        printf("warm-limit: status=%d  %s\n", rw, rw==3?"PASS":"FAIL");
        if(rw!=3) fails++;
        /* a fresh cold solve of the same problem may hit its own limit too;
           the warm-verdict contract is only about never printing 0 here */
        solver_destroy(w);
        free_lp(&lp2);
    }

    solver_destroy(s1); solver_destroy(s2); free_lp(&lp);
    printf(fails==0?"ALL INCREMENTAL TESTS PASSED\n":"%d FAILURES\n",fails);
    return fails==0?0:1;
}
