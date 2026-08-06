#include "qp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv){
    if (argc < 2) { fprintf(stderr, "usage: %s <qp>\n", argv[0]); return 1; }
    FILE *f = fopen(argv[1], "r");
    if (!f) return 1;
    int n, m;
    if (fscanf(f, "%d %d", &n, &m) != 2) return 1;
    double *c = (double*)malloc(n*sizeof(double));
    double *Q = (double*)malloc((size_t)n*n*sizeof(double));
    double *A = (double*)malloc((size_t)m*n*sizeof(double));
    double *b = (double*)malloc(m*sizeof(double));
    for (int j=0;j<n;j++) fscanf(f,"%lf",&c[j]);
    for (int j=0;j<n;j++) for (int i=0;i<n;i++) fscanf(f,"%lf",&Q[(size_t)j*n+i]); /* col-major: Q[row i, col j] */
    for (int i=0;i<m;i++) for (int j=0;j<n;j++) fscanf(f,"%lf",&A[(size_t)i*n+j]);
    for (int i=0;i<m;i++) fscanf(f,"%lf",&b[i]);
    fclose(f);
    QP qp; qp.n=n; qp.m=m; qp.Q=Q; qp.c=c; qp.A=A; qp.b=b; qp.x0=NULL;
    QPResult r; qp_solve(&qp,&r);
    if (r.status==0) {
        printf("SOLUTION");
        for (int i=0;i<n;i++) printf(" %.17g", r.x[i]);
        printf("\nOBJ %.17g\nITERS %d\n", r.obj, r.iterations);
    } else {
        printf("STATUS %d\n", r.status);
    }
    qp_result_free(&r);
    free(c); free(Q); free(A); free(b);
    return 0;
}
