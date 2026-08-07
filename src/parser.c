#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>

void lp_free(LP *lp);   /* forward decl so the error path can free cleanly */

/* Reasonable dimension limits to prevent resource-exhaustion from malformed
 * input.  These are far above any legitimate LP size but small enough that a
 * hostile file cannot cause unbounded allocation. */
#define MAX_DIM   1000000
#define MAX_NNZ   100000000L

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

/* Parse a double from a bound token, treating "inf"/"-inf"/"+inf" specially.
 * Returns 0 on success, -1 if the value overflows to inf or is NaN. */
static int parse_bound(const char *s, double *out)
{
    if (strcmp(s, "inf") == 0 || strcmp(s, "+inf") == 0) { *out = LP_INF; return 0; }
    if (strcmp(s, "-inf") == 0) { *out = -LP_INF; return 0; }
    errno = 0;
    char *end = NULL;
    double v = strtod(s, &end);
    if (end == s || *end != '\0' || errno == ERANGE || !isfinite(v)) return -1;
    *out = v;
    return 0;
}

int lp_read(const char *path, LP *lp)
{
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return -1; }
    char sense[32];
    if (fscanf(f, "%31s", sense) != 1) goto err;
    lp->maximize = (strncmp(sense, "max", 3) == 0);

    int n, m;
    if (fscanf(f, "%d %d", &n, &m) != 2) goto err;
    /* F-02: reject non-positive / oversized dimensions before allocating */
    if (n <= 0 || m < 0 || n > MAX_DIM || m > MAX_DIM) {
        fprintf(stderr, "invalid dimensions n=%d m=%d\n", n, m);
        goto err;
    }
    lp->n = n; lp->m = m;

    lp->c = (double*)malloc((size_t)n * sizeof(double));
    lp->b = (double*)malloc((size_t)(m ? m : 1) * sizeof(double));
    lp->l = (double*)malloc((size_t)n * sizeof(double));
    lp->u = (double*)malloc((size_t)n * sizeof(double));
    lp->rel = (char*)malloc((size_t)(m ? m : 1) * sizeof(char));
    if (!lp->c || !lp->b || !lp->l || !lp->u || !lp->rel) goto err;

    for (int j = 0; j < n; j++) if (fscanf(f, "%lf", &lp->c[j]) != 1) goto err;
    for (int i = 0; i < m; i++) if (fscanf(f, "%lf", &lp->b[i]) != 1) goto err;

    /* F-01: relation token.  The relations are concatenated without
       delimiters, each being 1 char ('<','>','=') or 2 chars ('<=','>='),
       so the token can be up to 2*m chars.  Read it into a bounded buffer
       and decode into m single-char relations, disambiguating by m.
       The solver treats '<' as "<=" (slack) and '>' as ">=" (surplus). */
    if (m > 0) {
        size_t rel_len = 2 * (size_t)m;               /* max token length */
        char *relbuf = (char*)malloc(rel_len + 1);
        if (!relbuf) goto err;
        char fmt[32];
        snprintf(fmt, sizeof(fmt), "%%%zus", rel_len);
        if (fscanf(f, fmt, relbuf) != 1) { free(relbuf); goto err; }
        size_t pos = 0;
        for (int i = 0; i < m; i++) {
            char ch = relbuf[pos];
            if (ch == '=') { lp->rel[i] = '='; pos += 1; }
            else if (ch == '<') { lp->rel[i] = '<'; pos += (relbuf[pos+1] == '=') ? 2 : 1; }
            else if (ch == '>') { lp->rel[i] = '>'; pos += (relbuf[pos+1] == '=') ? 2 : 1; }
            else {
                fprintf(stderr, "invalid relation at index %d\n", i);
                free(relbuf); goto err;
            }
            if (pos > rel_len) { free(relbuf); goto err; }
        }
        free(relbuf);
    }

    for (int j = 0; j < n; j++) {
        char lo[64], hi[64];
        if (fscanf(f, "%63s %63s", lo, hi) != 2) goto err;
        double lv, uv;
        if (parse_bound(lo, &lv) != 0 || parse_bound(hi, &uv) != 0) {
            fprintf(stderr, "invalid bound at column %d\n", j);
            goto err;
        }
        lp->l[j] = lv; lp->u[j] = uv;
        if (lp->l[j] <= -LP_INF && lp->u[j] >= LP_INF) {
            fprintf(stderr, "free variables (unbounded both sides) not supported at col %d\n", j);
            goto err;
        }
    }

    long nnz;
    if (fscanf(f, "%ld", &nnz) != 1) goto err;
    /* F-04: reject negative or oversized nnz */
    if (nnz < 0 || nnz > MAX_NNZ) {
        fprintf(stderr, "invalid nnz=%ld\n", nnz);
        goto err;
    }

    /* read triplets into temporary arrays (only when nnz > 0) */
    int *tr = NULL, *tc = NULL;
    double *tv = NULL;
    if (nnz > 0) {
        tr = (int*)malloc((size_t)nnz * sizeof(int));
        tc = (int*)malloc((size_t)nnz * sizeof(int));
        tv = (double*)malloc((size_t)nnz * sizeof(double));
        if (!tr || !tc || !tv) goto err;
    }
    for (long k = 0; k < nnz; k++) {
        int r, c; double v;
        if (fscanf(f, "%d %d %lf", &r, &c, &v) != 3) goto err2;
        /* F-03: reject out-of-range row/column indices */
        if (r < 0 || r >= m || c < 0 || c >= n) {
            fprintf(stderr, "triplet %ld out of range: r=%d c=%d (n=%d m=%d)\n", k, r, c, n, m);
            goto err2;
        }
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
    lp->Acolptr = (int*)calloc((size_t)(n + 1), sizeof(int));
    lp->Arow = (int*)malloc((size_t)(nnz ? nnz : 1) * sizeof(int));
    lp->Aval = (double*)malloc((size_t)(nnz ? nnz : 1) * sizeof(double));
    if (!lp->Acolptr || !lp->Arow || !lp->Aval) goto err2;

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
    /* free any partially-allocated problem members so callers can safely
       lp_free() the (zeroed) LP struct */
    lp_free(lp);
    return -1;
}

void lp_free(LP *lp)
{
    if (!lp) return;
    free(lp->c); free(lp->b); free(lp->l); free(lp->u); free(lp->rel);
    free(lp->Acolptr); free(lp->Arow); free(lp->Aval);
    lp->c = lp->b = lp->l = lp->u = NULL;
    lp->rel = NULL; lp->Acolptr = NULL; lp->Arow = NULL; lp->Aval = NULL;
}
