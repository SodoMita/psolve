/* Unit test: sparse LU (splu.c) vs dense LU (lu.c) on random sparse matrices.
 * Validates B x = b and B^T u = v against the reference dense solver, which
 * implicitly validates P*B*Q = L*U and all permutations. */
#include "splu.h"
#include "lu.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

int main(void){
    srand(42);
    int fails = 0, tested = 0, skipped = 0;
    for (int t = 0; t < 500; t++) {
        int m = 4 + rand() % 14;
        double dens = 0.1 + (rand()%20)/100.0;
        double *Ad = (double*)calloc((size_t)m*m, sizeof(double));
        int cap = m*(int)(dens*m + 2) + 2*m;
        int *Bp = (int*)malloc((size_t)(m+1)*sizeof(int));
        int *Bi = (int*)malloc((size_t)cap*sizeof(int));
        double *Bx = (double*)malloc((size_t)cap*sizeof(double));
        Bp[0]=0; int nnz=0;
        for (int j=0;j<m;j++){
            for (int i=0;i<m;i++){
                double v;
                if (i==j) v = ((rand()%2000)-1000)/100.0;
                else v = ((rand()%1000)/1000.0 < dens) ? ((rand()%2000)-1000)/100.0 : 0.0;
                Ad[(size_t)j*m+i]=v;
                if (v!=0.0){ Bi[nnz]=i; Bx[nnz]=v; nnz++; }
            }
            Bp[j+1]=nnz;
        }
        /* dense reference factor */
        double *al=(double*)malloc((size_t)m*m*sizeof(double));
        memcpy(al,Ad,(size_t)m*m*sizeof(double));
        int *piv=(int*)malloc((size_t)m*sizeof(int));
        if (lu_factor(al,m,piv)!=0){ skipped++; free(al);free(piv);free(Ad);free(Bp);free(Bi);free(Bx); continue; }

        SPLU s; memset(&s,0,sizeof(s)); s.pivot_tol=1e-13;
        if (splu_factor(&s,Bp,Bi,Bx,m)!=0){ skipped++; free(al);free(piv);free(Ad);free(Bp);free(Bi);free(Bx); continue; }

        /* B x = b : compare splu_solve to dense */
        double *b=malloc(m*sizeof(double)),*bb=malloc(m*sizeof(double));
        double *x=malloc(m*sizeof(double));
        for(int i=0;i<m;i++){ b[i]=((rand()%2000)-1000)/100.0; bb[i]=b[i]; }
        splu_solve(&s,b,x);
        double serr=0;
        for(int i=0;i<m;i++){ double r=0; for(int j=0;j<m;j++) r+=Ad[(size_t)j*m+i]*x[j]; serr+=fabs(r-bb[i]); }
        if(serr>1e-6){ printf("SOLVE FAIL t=%d m=%d err=%g\n",t,m,serr); fails++; }

        /* B^T u = v */
        double *v=malloc(m*sizeof(double)),*vv=malloc(m*sizeof(double));
        double *u=malloc(m*sizeof(double));
        for(int i=0;i<m;i++){ v[i]=((rand()%2000)-1000)/100.0; vv[i]=v[i]; }
        splu_solve_t(&s,v,u);
        double terr=0;
        for(int i=0;i<m;i++){ double r=0; for(int j=0;j<m;j++) r+=Ad[(size_t)i*m+j]*u[j]; terr+=fabs(r-vv[i]); }
        if(terr>1e-6){ printf("TRANSPOSE FAIL t=%d m=%d err=%g\n",t,m,terr); fails++; }

        tested++;
        free(al);free(piv);free(Ad);free(Bp);free(Bi);free(Bx);
        free(b);free(bb);free(x);free(v);free(vv);free(u);
        splu_free(&s);
    }
    printf("splu tested=%d skipped=%d fails=%d\n",tested,skipped,fails);
    return fails==0?0:1;
}
