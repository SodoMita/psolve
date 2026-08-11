#include "parser.h"
#include "err.h"
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

int lp_read(const char *path, LP *out)
{
    if(!path||!out)return -1;
    /* Parse into local zeroed storage so an early error never frees garbage
       from an uninitialized caller output and never partially mutates it. */
    LP storage;memset(&storage,0,sizeof(storage));LP *lp=&storage;
    /* Keep every cleanup-owned pointer in function scope and initialize it
       before any path can jump to `err`.  The previous layout declared these
       halfway through the function, so early parse errors jumped past their
       initializers and then passed indeterminate pointers to psolve_free(). */
    int *tr = NULL, *tc = NULL;
    double *tv = NULL;
    int *colcount = NULL;
    int *out_r = NULL;
    double *out_v = NULL;

    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return -1; }
    char sense[32];
    if (fscanf(f, "%31s", sense) != 1) goto err;
    /* Objective sense must be exactly max/maximize or min/minimize.  Reject
       malformed input instead of silently defaulting to minimize. */
    if (strcmp(sense, "max") == 0 || strcmp(sense, "maximize") == 0)
        lp->maximize = 1;
    else if (strcmp(sense, "min") == 0 || strcmp(sense, "minimize") == 0)
        lp->maximize = 0;
    else {
        fprintf(stderr, "invalid objective sense: '%s' (expected max/min)\n", sense);
        goto err;
    }

    int n, m;
    if (fscanf(f, "%d %d", &n, &m) != 2) goto err;
    /* F-02: reject non-positive / oversized dimensions before allocating */
    if (n <= 0 || m < 0 || n > MAX_DIM || m > MAX_DIM) {
        fprintf(stderr, "invalid dimensions n=%d m=%d\n", n, m);
        goto err;
    }
    /* item 4: explicit size_t overflow guards (defense in depth — the caps
       above already keep these far below SIZE_MAX, but be explicit). */
    if ((size_t)n > (size_t)-1 / sizeof(double) ||
        (size_t)(m ? m : 1) > (size_t)-1 / sizeof(double) ||
        (size_t)(m ? m : 1) > (size_t)-1 / sizeof(char)) {
        fprintf(stderr, "dimensions too large\n");
        goto err;
    }
    lp->n = n; lp->m = m;

    lp->c = (double*)psolve_malloc((size_t)n * sizeof(double));
    lp->b = (double*)psolve_malloc((size_t)(m ? m : 1) * sizeof(double));
    lp->l = (double*)psolve_malloc((size_t)n * sizeof(double));
    lp->u = (double*)psolve_malloc((size_t)n * sizeof(double));
    lp->rel = (char*)psolve_malloc((size_t)(m ? m : 1) * sizeof(char));
    if (!lp->c || !lp->b || !lp->l || !lp->u || !lp->rel) goto err;

    for (int j = 0; j < n; j++) {
        if (fscanf(f, "%lf", &lp->c[j]) != 1 || !isfinite(lp->c[j])) {
            fprintf(stderr, "invalid objective coefficient at column %d\n", j);
            goto err;
        }
    }
    for (int i = 0; i < m; i++) {
        if (fscanf(f, "%lf", &lp->b[i]) != 1 || !isfinite(lp->b[i])) {
            fprintf(stderr, "invalid rhs at row %d\n", i);
            goto err;
        }
    }

    /* F-01: relation token.  Each relation is a single char ('<','>','=');
       the token is the m chars concatenated (e.g. "<<=<<").  The solver
       treats '<' as "<=" (slack) and '>' as ">=" (surplus), so the two-char
       '<= '/'>= ' spellings are redundant and are NOT supported (they would
       be ambiguous with '<' then '=').  Read into a bounded buffer of
       exactly m chars and validate. */
    if (m > 0) {
        size_t rel_len = (size_t)m;               /* exactly m chars */
        char *relbuf = (char*)psolve_malloc(rel_len + 1);
        if (!relbuf) goto err;
        int got = 0;
        size_t pos = 0;
        int c;
        /* skip leading whitespace */
        do { c = fgetc(f); } while (c != EOF && (c==' '||c=='\t'||c=='\n'||c=='\r'));
        for (; c != EOF && c!=' ' && c!='\t' && c!='\n' && c!='\r'; c = fgetc(f)) {
            if (pos < rel_len) { relbuf[pos++] = (char)c; got = 1; }
            else { psolve_free(relbuf); goto err; }       /* token longer than m */
        }
        relbuf[pos] = '\0';
        if (!got || pos != rel_len) { psolve_free(relbuf); goto err; }
        for (int i = 0; i < m; i++) {
            char ch = relbuf[i];
            if (ch != '<' && ch != '>' && ch != '=') {
                fprintf(stderr, "invalid relation at index %d\n", i);
                psolve_free(relbuf); goto err;
            }
            lp->rel[i] = ch;
        }
        psolve_free(relbuf);
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
        /* A fully free variable is normalized by solver_create() as x+ - x-
           with non-negative components, so it is a supported LP input. */
    }

    long nnz;
    if (fscanf(f, "%ld", &nnz) != 1) goto err;
    /* F-04: reject negative or oversized nnz */
    if (nnz < 0 || nnz > MAX_NNZ) {
        fprintf(stderr, "invalid nnz=%ld\n", nnz);
        goto err;
    }

    /* read triplets into temporary arrays (only when nnz > 0) */
    if (nnz > 0) {
        tr = (int*)psolve_malloc((size_t)nnz * sizeof(int));
        tc = (int*)psolve_malloc((size_t)nnz * sizeof(int));
        tv = (double*)psolve_malloc((size_t)nnz * sizeof(double));
        if (!tr || !tc || !tv) goto err;
    }
    for (long k = 0; k < nnz; k++) {
        int r, c; double v;
        if (fscanf(f, "%d %d %lf", &r, &c, &v) != 3) goto err2;
        if (!isfinite(v)) {
            fprintf(stderr, "invalid matrix coefficient at triplet %ld\n", k);
            goto err2;
        }
        /* F-03: reject out-of-range row/column indices */
        if (r < 0 || r >= m || c < 0 || c >= n) {
            fprintf(stderr, "triplet %ld out of range: r=%d c=%d (n=%d m=%d)\n", k, r, c, n, m);
            goto err2;
        }
        tr[k] = r; tc[k] = c; tv[k] = v;
    }
    /* Counting sort by column: O(nnz + n), linear in input size (the previous
       insertion sort was O(nnz^2) on adversarial triplet orderings). */
    colcount = (int*)psolve_calloc((size_t)(n + 1), sizeof(int));
    out_r = (int*)psolve_malloc((size_t)(nnz ? nnz : 1) * sizeof(int));
    out_v = (double*)psolve_malloc((size_t)(nnz ? nnz : 1) * sizeof(double));
    if (!colcount || !out_r || !out_v) { psolve_free(colcount); psolve_free(out_r); psolve_free(out_v); colcount = NULL; out_r = NULL; out_v = NULL; goto err2; }
    for (long k = 0; k < nnz; k++) colcount[tc[k] + 1]++;
    for (int j = 0; j < n; j++) colcount[j + 1] += colcount[j];
    for (long k = 0; k < nnz; k++) {
        int pos = colcount[tc[k]]++;
        out_r[pos] = tr[k];
        out_v[pos] = tv[k];
    }
    psolve_free(tr); psolve_free(tc); psolve_free(tv);
    tr = NULL; tc = NULL; tv = NULL;

    lp->Acolptr = (int*)psolve_malloc((size_t)(n + 1) * sizeof(int));
    lp->Arow = (int*)psolve_malloc((size_t)(nnz ? nnz : 1) * sizeof(int));
    lp->Aval = (double*)psolve_malloc((size_t)(nnz ? nnz : 1) * sizeof(double));
    if (!lp->Acolptr || !lp->Arow || !lp->Aval) goto err2;
    lp->Acolptr[0] = 0;
    for (int j = 0; j < n; j++) lp->Acolptr[j + 1] = colcount[j];
    for (long k = 0; k < nnz; k++) { lp->Arow[k] = out_r[k]; lp->Aval[k] = out_v[k]; }
    psolve_free(colcount); psolve_free(out_r); psolve_free(out_v);
    fclose(f);
    *out=storage;
    return 0;

err2:
    goto err;
err:
    fclose(f);
    fprintf(stderr, "LP parse error\n");
    /* Every temporary is initialized at function entry and owned until this
       single cleanup point, so early errors cannot free garbage or double-free
       an allocation already released by an intermediate error path. */
    psolve_free(tr); psolve_free(tc); psolve_free(tv);
    psolve_free(colcount); psolve_free(out_r); psolve_free(out_v);
    lp_free(lp);
    return -1;
}

void lp_free(LP *lp)
{
    if (!lp) return;
    psolve_free(lp->c); psolve_free(lp->b); psolve_free(lp->l); psolve_free(lp->u); psolve_free(lp->rel);
    psolve_free(lp->Acolptr); psolve_free(lp->Arow); psolve_free(lp->Aval);
    lp->c = lp->b = lp->l = lp->u = NULL;
    lp->rel = NULL; lp->Acolptr = NULL; lp->Arow = NULL; lp->Aval = NULL;
}
