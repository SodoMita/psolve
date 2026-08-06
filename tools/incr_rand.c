#include "solver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void free_lp(LP*lp){free(lp->c);free(lp->b);free(lp->rel);free(lp->l);free(lp->u);free(lp->Acolptr);free(lp->Arow);free(lp->Aval);}
static double rnd(unsigned*s){ *s=*s*1103515245u+12345u; return (double)((*s>>16)&0x7fff)/32767.0; }

/* Build a random canonical LP (bounded, feasible, origin feasible). */
static void rand_lp(LP *lp, int n, int m, unsigned *seed){
    unsigned s=*seed;
    #define RND() ((s=s*1103515245u+12345u),(double)((s>>16)&0x7fff)/32767.0)
    memset(lp,0,sizeof(LP));
    lp->n=n; lp->m=m; lp->maximize=1;
    lp->c=malloc(n*sizeof(double)); lp->b=malloc(m*sizeof(double));
    lp->rel=malloc(m); lp->l=malloc(n*sizeof(double)); lp->u=malloc(n*sizeof(double));
    for(int j=0;j<n;j++){ lp->c[j]=RND()*10-2; lp->l[j]=0; lp->u[j]=LP_INF; }
    for(int i=0;i<m;i++){ lp->b[i]=RND()*20+5; lp->rel[i]='<'; }
    /* nnz ~ 0.4, plus a cap row to guarantee boundedness */
    int mm=m+1;
    int nnz=0; double A[24][16];
    for(int i=0;i<m;i++)for(int j=0;j<n;j++){ A[i][j]=(RND()<0.4)?(RND()*4):0.0; if(A[i][j]!=0)nnz++; }
    for(int j=0;j<n;j++){ A[m][j]=1.0; nnz++; }   /* cap: sum x <= C */
    lp->Acolptr=calloc(n+1,sizeof(int)); lp->Arow=malloc((nnz>0?nnz:1)*sizeof(int)); lp->Aval=malloc((nnz>0?nnz:1)*sizeof(double));
    for(int i=0;i<mm;i++)for(int j=0;j<n;j++)if(A[i][j]!=0)lp->Acolptr[j+1]++;
    for(int j=0;j<n;j++)lp->Acolptr[j+1]+=lp->Acolptr[j];
    int *f=malloc(n*sizeof(int)); for(int j=0;j<n;j++)f[j]=lp->Acolptr[j];
    for(int i=0;i<mm;i++)for(int j=0;j<n;j++)if(A[i][j]!=0){lp->Arow[f[j]]=i;lp->Aval[f[j]]=A[i][j];f[j]++;}
    free(f);
    /* cap row rhs */
    lp->b=(double*)realloc(lp->b,mm*sizeof(double)); lp->b[m]=RND()*20+10;
    lp->rel=(char*)realloc(lp->rel,mm); lp->rel[m]='<';
    lp->m=mm; m=mm;
    *seed=s;
    #undef RND
}

static double solve_obj(Solver*s){ double x[24],o; solver_optimum(s,x,&o); return o; }

int main(void){
    int fails=0;
    for(int seed=1;seed<=200;seed++){
        unsigned s=seed;
        int n=3+seed%8, m=3+seed%6;
        LP lp; rand_lp(&lp,n,m,&s);
        Solver *w=solver_create(&lp); solver_solve(w);
        double base=solve_obj(w);

        /* objective change */
        double nc[24]; for(int j=0;j<n;j++){ double r=(seed*17+j*13)%97/97.0; nc[j]=r*10-2; }
        solver_set_objective(w,nc,1);
        solver_warm_solve(w);
        double ow=solve_obj(w);
        /* fresh */
        for(int j=0;j<n;j++)lp.c[j]=nc[j];
        Solver *f=solver_create(&lp); solver_solve(f);
        double of=solve_obj(f);
        if(fabs(ow-of)>1e-5){ printf("seed %d OBJ mismatch warm=%g fresh=%g\n",seed,ow,of); fails++; }
        solver_destroy(f);

        /* bound change */
        double l2[24],u2[24]; for(int j=0;j<n;j++){l2[j]=0;u2[j]=LP_INF;}
        for(int j=0;j<n;j++) if((seed+j)%7==0) u2[j]=rnd(&s)*5+1;
        solver_set_bounds(w,l2,u2);
        solver_warm_solve(w);
        double bw=solve_obj(w);
        for(int j=0;j<n;j++){lp.l[j]=l2[j];lp.u[j]=u2[j];}
        f=solver_create(&lp); solver_solve(f);
        double bf=solve_obj(f);
        if(fabs(bw-bf)>1e-5){ printf("seed %d BOUND mismatch warm=%g fresh=%g\n",seed,bw,bf); fails++; }
        solver_destroy(f);

        /* add row: a random combination <= rhs */
        double arow[24]; double sum=0;
        for(int j=0;j<n;j++){ arow[j]=rnd(&s)*3-1; sum+=fabs(arow[j]); }
        double rhs=rnd(&s)*sum;   /* sometimes small (may be violated), sometimes large */
        if(seed<=3||1){ printf("seed %d n=%d m=%d rhs=%g addrow\n",seed,n,m,rhs); fflush(stdout);} 
        solver_add_row(w,arow,rhs,'<');
        solver_solve(w);
        double aw=solve_obj(w);
        /* fresh with extra row */
        {
            int m2=lp.m+1;
            double *b2=malloc(m2*sizeof(double)); char *r2=malloc(m2);
            for(int i=0;i<lp.m;i++){b2[i]=lp.b[i];r2[i]=lp.rel[i];}
            b2[lp.m]=rhs; r2[lp.m]='<';
            /* dense rebuild */
            double A2[32][24]; for(int i=0;i<m2;i++)for(int j=0;j<n;j++)A2[i][j]=0;
            for(int j=0;j<n;j++)for(int k=lp.Acolptr[j];k<lp.Acolptr[j+1];k++)A2[lp.Arow[k]][j]=lp.Aval[k];
            for(int j=0;j<n;j++)A2[lp.m][j]=arow[j];
            int nnz=0; for(int i=0;i<m2;i++)for(int j=0;j<n;j++)if(A2[i][j]!=0)nnz++;
            int *np=calloc(n+1,sizeof(int)); int *nr=malloc((nnz>0?nnz:1)*sizeof(int)); double *nv=malloc((nnz>0?nnz:1)*sizeof(double));
            for(int i=0;i<m2;i++)for(int j=0;j<n;j++)if(A2[i][j]!=0)np[j+1]++;
            for(int j=0;j<n;j++)np[j+1]+=np[j];
            int *ff=malloc(n*sizeof(int)); for(int j=0;j<n;j++)ff[j]=np[j];
            for(int i=0;i<m2;i++)for(int j=0;j<n;j++)if(A2[i][j]!=0){nr[ff[j]]=i;nv[ff[j]]=A2[i][j];ff[j]++;}
            free(ff);
            free(lp.Acolptr);free(lp.Arow);free(lp.Aval);free(lp.b);free(lp.rel);
            lp.Acolptr=np;lp.Arow=nr;lp.Aval=nv;lp.b=b2;lp.rel=r2;lp.m=m2;
        }
        f=solver_create(&lp); solver_solve(f);
        double af=solve_obj(f);
        if(fabs(aw-af)>1e-5){ printf("seed %d ADDROW mismatch warm=%g fresh=%g\n",seed,aw,af); fails++; }
        solver_destroy(f);

        solver_destroy(w); free_lp(&lp);
    }
    printf(fails==0?"ALL %d RANDOM INCREMENTAL TESTS PASSED\n":"%d FAILURES\n", fails==0?200:fails);
    return fails==0?0:1;
}
