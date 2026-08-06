#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* LP file format (one token per whitespace-delimited token):
 *   line 1 : maximize | minimize
 *   line 2 : n m
 *   line 3 : n objective values
 *   line 4 : m rhs values
 *   line 5 : m relation chars in one token  ('<','>','=')
 *   lines  : n bounds, each "lo hi" (use 'inf' for unbounded)
 *   line   : nnz
 *   lines  : row col val  (nnz triplets, 0-indexed)
 */
int lp_read(const char *path, LP *lp)
{
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return -1; }

    char sense[32];
    if (fscanf(f, "%31s", sense) != 1) { fclose(f); return -1; }
    lp->maximize = (strncmp(sense, "max", 3) == 0);

    int n, m;
    if (fscanf(f, "%d %d", &n, &m) != 2) { fclose(f); return -1; }
    lp->n = n; lp->m = m;
    lp->c = (double*)malloc(n * sizeof(double));
    lp->b = (double*)malloc(m * sizeof(double));
    lp->l = (double*)malloc(n * sizeof(double));
    lp->u = (double*)malloc(n * sizeof(double));
    lp->rel = (char*)malloc(m * sizeof(char));
    for (int j = 0; j < n; j++) if (fscanf(f, "%lf", &lp->c[j]) != 1) goto err;
    for (int i = 0; i < m; i++) if (fscanf(f, "%lf", &lp->b[i]) != 1) goto err;
    {
        char *rels = (char*)malloc((m + 1) * sizeof(char));
        if (fscanf(f, "%s", rels) != 1) { free(rels); goto err; }
        for (int i = 0; i < m; i++) lp->rel[i] = rels[i];
        free(rels);
    }
    for (int j = 0; j < n; j++) {
        char lo[64], hi[64];
        if (fscanf(f, "%63s %63s", lo, hi) != 2) goto err;
        lp->l[j] = (strcmp(lo, "inf") == 0 || strcmp(lo, "-inf") == 0) ? -LP_INF : atof(lo);
        lp->u[j] = (strcmp(hi, "inf") == 0 || strcmp(hi, "+inf") == 0) ?  LP_INF : atof(hi);
        if (lp->l[j] <= -LP_INF && lp->u[j] >= LP_INF) {
            fprintf(stderr, "free variables (unbounded both sides) not supported at col %d\n", j);
            fclose(f); return -1;
        }
    }

    long nnz;
    if (fscanf(f, "%ld", &nnz) != 1) goto err;
    /* read triplets, then sort by column so the CSC layout is contiguous */
    int *tr = (int*)malloc(nnz * sizeof(int));
    int *tc = (int*)malloc(nnz * sizeof(int));
    double *tv = (double*)malloc(nnz * sizeof(double));
    for (long k = 0; k < nnz; k++) {
        int r, c; double v;
        if (fscanf(f, "%d %d %lf", &r, &c, &v) != 3) goto err2;
        tr[k] = r; tc[k] = c; tv[k] = v;
    }
    /* insertion sort by column (triplets usually near-sorted) */
    for (long i = 1; i < nnz; i++) {
        int cr = tr[i], cc = tc[i]; double cv = tv[i];
        long j = i - 1;
        while (j >= 0 && tc[j] > cc) {
            tr[j + 1] = tr[j]; tc[j + 1] = tc[j]; tv[j + 1] = tv[j]; j--;
        }
        tr[j + 1] = cr; tc[j + 1] = cc; tv[j + 1] = cv;
    }
    lp->Acolptr = (int*)calloc((n + 1), sizeof(int));
    lp->Arow = (int*)malloc(nnz * sizeof(int));
    lp->Aval = (double*)malloc(nnz * sizeof(double));
    for (long k = 0; k < nnz; k++) {
        lp->Arow[k] = tr[k];
        lp->Aval[k] = tv[k];
        lp->Acolptr[tc[k] + 1]++;
    }
    for (int j = 0; j < n; j++) lp->Acolptr[j + 1] += lp->Acolptr[j];
    free(tr); free(tc); free(tv);
    fclose(f);
    return 0;
err2:
    free(tr); free(tc); free(tv);
err:
    fclose(f);
    fprintf(stderr, "LP parse error\n");
    return -1;
}

void lp_free(LP *lp)
{
    free(lp->c); free(lp->b); free(lp->l); free(lp->u); free(lp->rel);
    free(lp->Acolptr); free(lp->Arow); free(lp->Aval);
}
