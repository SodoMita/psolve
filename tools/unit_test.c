/* Randomized unit test for the dense LU factor/solve + transpose solve. */
#include "lu.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

int main(void){
    srand(1);
    int nfail = 0;
    for (int t = 0; t < 2000; t++) {
        int m = 2 + rand() % 8;
        double *a  = calloc((size_t)m*m, sizeof(double));
        double *orig = calloc((size_t)m*m, sizeof(double));
        for (int i=0;i<m;i++) for (int j=0;j<m;j++){
            double v = ((rand()%2000)-1000)/100.0;
            a[j*m+i] = v; orig[j*m+i] = v;   /* column-major */
        }
        int *piv = malloc(m*sizeof(int));
        if (lu_factor(a,m,piv)!=0){ free(a);free(orig);free(piv); continue; }
        double *b=malloc(m*sizeof(double)), *x=malloc(m*sizeof(double)), *bb=malloc(m*sizeof(double));
        for (int i=0;i<m;i++){ b[i]=((rand()%2000)-1000)/100.0; bb[i]=b[i]; }
        lu_solve(a,piv,m,b,x);
        double err=0;
        for (int i=0;i<m;i++){ double r=0; for(int j=0;j<m;j++) r+=orig[j*m+i]*x[j]; err+=(r-bb[i])*(r-bb[i]); }
        if (err>1e-8){ printf("SOLVE FAIL t=%d err=%g\n",t,err); nfail++; }
        double *v2=malloc(m*sizeof(double)), *u=malloc(m*sizeof(double)), *vv=malloc(m*sizeof(double));
        for (int i=0;i<m;i++){ v2[i]=((rand()%2000)-1000)/100.0; vv[i]=v2[i]; }
        lu_solve_t(a,piv,m,v2,u);
        double terr=0;
        for (int i=0;i<m;i++){ double r=0; for(int j=0;j<m;j++) r+=orig[i*m+j]*u[j]; terr+=(r-vv[i])*(r-vv[i]); }
        if (terr>1e-8){ printf("TRANSPOSE FAIL t=%d err=%g\n",t,terr); nfail++; }
        free(a);free(orig);free(piv);free(b);free(x);free(bb);free(v2);free(u);free(vv);
    }
    printf(nfail==0 ? "ALL 2000 LU SOLVES + TRANSPOSE PASSED\n" : "%d FAILURES\n", nfail);
    return nfail==0?0:1;
}
