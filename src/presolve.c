/* Roadmap 7.1: presolve + postsolve (see src/presolve.h for the design
 * contract and the soundness invariants).
 *
 * Implementation notes:
 *  - The working image is per-column vectors (rows[], vals[], len, cap) -
 *    doubleton substitution only ever appends to ONE column (the
 *    survivor), so per-column growth is the simple structure.  A per-pass
 *    rebuild drops dead-row entries and exact-zero coefficients, and
 *    EVERY detection re-collects the row's entries fresh from the image,
 *    so a stale count can only lose a reduction, never fire a wrong one.
 *  - Bound folding rounds OUTWARD (FE_DOWNWARD for lower claims,
 *    FE_UPWARD for upper): the reduced box is a superset of the exact
 *    one, so a postsolved point can violate an original relation by at
 *    most rounding - and the psv primal lane (original data, 1e-5/1e-6
 *    margins) is the end-to-end check.  Conflict TESTS use the SAME
 *    outward-rounded candidate: a candidate lower bound lo_dn <= exact
 *    lo that already exceeds the current (original) upper bound proves
 *    exact_lo > hi in the reals; symmetric for upper claims.  That is
 *    the only direction in which a numeric conflict is declared, and a
 *    declared conflict is always handed to the psv Farkas lane as a ray
 *    hint - presolve's own arithmetic never prints INFEASIBLE.
 *  - Anything the exact rules don't cover (deeper provenance, budget
 *    overflow, sentinel crossing, dual-replay reference cycles) SKIPS
 *    the reduction or ABORTS the run (rc -1: the caller walks the
 *    original model).  Presolve may cost effort; it may never change
 *    who answers.
 */
#include "presolve.h"
#include "err.h"
#include <fenv.h>
#include <math.h>
#include <string.h>

#define PRE_PASS_CAP   16          /* fixpoint passes */
#define PRE_NNZ_GROWTH 3L          /* fill cap: reduced nnz <= 3x original */
#define PRE_SNAP_BYTES (8L<<20)    /* pivot-snapshot pool budget */

/* ------------------------------------------------------------------ */
/* directed-rounded scalar ops (project pattern, as src/mip.c)         */
/* ------------------------------------------------------------------ */
static double rd_add(double a, double b)
{ fesetround(FE_DOWNWARD); double t = a + b; fesetround(FE_TONEAREST); return t; }
static double ru_add(double a, double b)
{ fesetround(FE_UPWARD);  double t = a + b; fesetround(FE_TONEAREST); return t; }
static double rd_mul(double a, double b)
{ fesetround(FE_DOWNWARD); double t = a * b; fesetround(FE_TONEAREST); return t; }
static double ru_mul(double a, double b)
{ fesetround(FE_UPWARD);  double t = a * b; fesetround(FE_TONEAREST); return t; }
static double rd_div(double a, double b)
{ fesetround(FE_DOWNWARD); double t = a / b; fesetround(FE_TONEAREST); return t; }
static double ru_div(double a, double b)
{ fesetround(FE_UPWARD);  double t = a / b; fesetround(FE_TONEAREST); return t; }

/* ------------------------------------------------------------------ */
/* working image                                                       */
/* ------------------------------------------------------------------ */
typedef struct {
    int *rows;      /* row indices (original numbering) */
    double *vals;
    int len, cap;
} PCol;

typedef struct {
    int n, m;
    double *c, *lo, *hi, *b;
    char *rel;
    int *lo_src, *hi_src;         /* per column: -1 original, else orig row */
    char *col_dead, *row_dead;
    char *row_touched;            /* row's rhs/coeffs were mutated by a
                                     substitution: EXACT-ray paths gate on
                                     untouched rows (a working image row
                                     that is no longer the original row
                                     does not carry an e_i Farkas ray on
                                     original data) */
    PCol *col;
    long nnz;
    double objconst;              /* bookkeeping only: never used for a
                                     printed objective (the CLI recomputes
                                     c^T x on ORIGINAL data) */
} PW;

typedef struct UnbNote { int var; double walk; } UnbNote;

struct PreRec {
    int kind;
    int i, j, k;              /* original row/col indices per kind */
    char rel_i;               /* rel of row i at firing (singleton) */
    double a_ij;              /* singleton coef / doubleton pivot coef */
    double bi;                /* rhs of row i at firing */
    double tcoef;             /* doubleton: -a_k/a_j in x_j = bover + tcoef x_k */
    double bover;             /* doubleton: b/a_j */
    double cj;                /* firing-time current cost of pivot col */
    double v;                 /* COL_FIXED/COL_EMPTY value */
    int snap_n, snap_off;     /* pivot-col snapshot into ps->snap_* pools */
};

/* reduction kinds */
enum { PRK_COL_FIXED = 1, PRK_COL_EMPTY, PRK_ROW_EMPTY, PRK_ROW_SINGLETON,
       PRK_ROW_REDUNDANT, PRK_ROW_DOUBLETON };

struct PreSolve {
    int n_orig, m_orig;
    PreRec *recs; int n_recs, cap_recs;
    int *snap_row; double *snap_coef; long snap_len, snap_cap;
    int *row_orig, *col_orig;   /* reduced -> original maps */
    int n_red, m_red;
    double *inf_ray;            /* len m_orig when rc==1, else NULL */
    UnbNote *notes; int n_notes, cap_notes;
};

/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */
static void pw_init(PW *w, const LP *lp)
{
    int n = lp->n, m = lp->m;
    memset(w, 0, sizeof(*w));
    w->n = n; w->m = m;
    w->c  = (double*)psolve_malloc((size_t)n * sizeof(double));
    w->lo = (double*)psolve_malloc((size_t)n * sizeof(double));
    w->hi = (double*)psolve_malloc((size_t)n * sizeof(double));
    w->b  = (double*)psolve_malloc((size_t)(m ? m : 1) * sizeof(double));
    w->rel = (char*)psolve_malloc((size_t)(m ? m : 1));
    w->lo_src = (int*)psolve_malloc((size_t)n * sizeof(int));
    w->hi_src = (int*)psolve_malloc((size_t)n * sizeof(int));
    w->col_dead = (char*)psolve_calloc((size_t)n, 1);
    w->row_dead = (char*)psolve_calloc((size_t)(m ? m : 1), 1);
    w->row_touched = (char*)psolve_calloc((size_t)(m ? m : 1), 1);
    w->col = (PCol*)psolve_calloc((size_t)n, sizeof(PCol));
    memcpy(w->c, lp->c, (size_t)n * sizeof(double));
    memcpy(w->lo, lp->l, (size_t)n * sizeof(double));
    memcpy(w->hi, lp->u, (size_t)n * sizeof(double));
    if (m > 0) { memcpy(w->b, lp->b, (size_t)m * sizeof(double));
                 memcpy(w->rel, lp->rel, (size_t)m); }
    for (int j = 0; j < n; j++) { w->lo_src[j] = -1; w->hi_src[j] = -1; }
    w->nnz = lp->Acolptr[n];
    for (int j = 0; j < n; j++) {
        int len = lp->Acolptr[j + 1] - lp->Acolptr[j];
        PCol *pc = &w->col[j];
        pc->cap = len ? len : 1;
        pc->rows = (int*)psolve_malloc((size_t)pc->cap * sizeof(int));
        pc->vals = (double*)psolve_malloc((size_t)pc->cap * sizeof(double));
        pc->len = len;
        for (int k = 0; k < len; k++) {
            pc->rows[k] = lp->Arow[lp->Acolptr[j] + k];
            pc->vals[k] = lp->Aval[lp->Acolptr[j] + k];
        }
    }
}

static void pw_free(PW *w)
{
    psolve_free(w->c); psolve_free(w->lo); psolve_free(w->hi);
    psolve_free(w->b); psolve_free(w->rel);
    psolve_free(w->lo_src); psolve_free(w->hi_src);
    psolve_free(w->col_dead); psolve_free(w->row_dead);
    psolve_free(w->row_touched);
    if (w->col) {
        for (int j = 0; j < w->n; j++) {
            psolve_free(w->col[j].rows); psolve_free(w->col[j].vals);
        }
        psolve_free(w->col);
    }
    memset(w, 0, sizeof(*w));
}

static PreSolve *ps_new(int n, int m)
{
    PreSolve *ps = (PreSolve*)psolve_calloc(1, sizeof(PreSolve));
    ps->n_orig = n; ps->m_orig = m;
    ps->cap_recs = 64;
    ps->recs = (PreRec*)psolve_malloc((size_t)ps->cap_recs * sizeof(PreRec));
    ps->snap_cap = 1024;
    ps->snap_row = (int*)psolve_malloc((size_t)ps->snap_cap * sizeof(int));
    ps->snap_coef = (double*)psolve_malloc((size_t)ps->snap_cap * sizeof(double));
    return ps;
}

void lp_presolve_free(PreSolve *ps)
{
    if (!ps) return;
    psolve_free(ps->recs);
    psolve_free(ps->snap_row); psolve_free(ps->snap_coef);
    psolve_free(ps->row_orig); psolve_free(ps->col_orig);
    psolve_free(ps->inf_ray);
    psolve_free(ps->notes);
    psolve_free(ps);
}

static int ps_add_rec(PreSolve *ps, const PreRec *r)
{
    if (ps->n_recs >= (1 << 22)) return -1;      /* record cap: decline */
    if (ps->n_recs == ps->cap_recs) {
        ps->cap_recs *= 2;
        ps->recs = (PreRec*)psolve_realloc((void**)&ps->recs,
                                           (size_t)ps->cap_recs * sizeof(PreRec));
    }
    ps->recs[ps->n_recs++] = *r;
    return 0;
}

/* pivot-column snapshot over rows ACTIVE at firing time; declines past
 * the byte budget (returns -1) */
static int ps_snapshot(PreSolve *ps, const PCol *pc, const char *row_dead,
                       int *off_out, int *n_out)
{
    int cnt = 0;
    for (int k = 0; k < pc->len; k++)
        if (pc->vals[k] != 0.0 && !row_dead[pc->rows[k]]) cnt++;
    if ((ps->snap_len + cnt) * (long)(sizeof(int) + sizeof(double)) >
        PRE_SNAP_BYTES) return -1;
    if (ps->snap_len + cnt > ps->snap_cap) {
        while (ps->snap_len + cnt > ps->snap_cap) ps->snap_cap *= 2;
        ps->snap_row = (int*)psolve_realloc((void**)&ps->snap_row,
                                            (size_t)ps->snap_cap * sizeof(int));
        ps->snap_coef = (double*)psolve_realloc((void**)&ps->snap_coef,
                                                (size_t)ps->snap_cap * sizeof(double));
    }
    *off_out = (int)ps->snap_len;
    *n_out = cnt;
    for (int k = 0; k < pc->len; k++)
        if (pc->vals[k] != 0.0 && !row_dead[pc->rows[k]]) {
            ps->snap_row[ps->snap_len] = pc->rows[k];
            ps->snap_coef[ps->snap_len] = pc->vals[k];
            ps->snap_len++;
        }
    return 0;
}

static void ps_add_note(PreSolve *ps, int var, double walk)
{
    if (ps->n_notes == ps->cap_notes) {
        ps->cap_notes = ps->cap_notes ? 2 * ps->cap_notes : 16;
        ps->notes = (UnbNote*)psolve_realloc((void**)&ps->notes,
                                             (size_t)ps->cap_notes * sizeof(UnbNote));
    }
    ps->notes[ps->n_notes].var = var;
    ps->notes[ps->n_notes].walk = walk;
    ps->n_notes++;
}

/* infinity-token predicates (TOL-LP-INF semantics, like solver.c) */
static int side_open_lo(double lo) { return lo <= -LP_INF; }
static int side_open_hi(double hi) { return hi >=  LP_INF; }

/* ------------------------------------------------------------------ */
/* per-pass rebuild: drop dead rows/cols + exact zeros, count row nnz  */
/* ------------------------------------------------------------------ */
static void pw_rebuild(PW *w, int *rownnz)
{
    for (int i = 0; i < w->m; i++) rownnz[i] = 0;
    long nnz = 0;
    for (int j = 0; j < w->n; j++) {
        PCol *pc = &w->col[j];
        if (w->col_dead[j]) { pc->len = 0; continue; }
        int out = 0;
        for (int k = 0; k < pc->len; k++) {
            if (w->row_dead[pc->rows[k]]) continue;
            if (pc->vals[k] == 0.0) continue;
            pc->rows[out] = pc->rows[k]; pc->vals[out] = pc->vals[k]; out++;
            rownnz[pc->rows[k]]++; nnz++;
        }
        pc->len = out;
    }
    w->nnz = nnz;
}

/* collect row i's entries fresh from the image; returns count */
static int row_collect(const PW *w, int i, int js[2], double as[2])
{
    int got = 0;
    for (int j = 0; j < w->n; j++) {
        const PCol *pc = &w->col[j];
        if (w->col_dead[j]) continue;
        for (int k = 0; k < pc->len; k++)
            if (pc->rows[k] == i) {
                if (got < 2) { js[got] = j; as[got] = pc->vals[k]; }
                got++;
            }
    }
    return got;
}

/* ------------------------------------------------------------------ */
/* row activity limits with directed rounding; side[k]=1 when that     */
/* limit is infinite (an involved var is open on the needed side)      */
/* ------------------------------------------------------------------ */
static void row_activity(const PW *w, int row, double lim[2], int side[2])
{
    double mn = 0.0, mx = 0.0;
    int open_mn = 0, open_mx = 0;
    for (int j = 0; j < w->n; j++) {
        const PCol *pc = &w->col[j];
        if (w->col_dead[j]) continue;
        for (int k = 0; k < pc->len; k++) {
            if (pc->rows[k] != row) continue;
            double a = pc->vals[k];
            if (a > 0) {
                if (side_open_lo(w->lo[j])) open_mn = 1;
                else mn = rd_add(mn, rd_mul(a, w->lo[j]));
                if (side_open_hi(w->hi[j])) open_mx = 1;
                else mx = ru_add(mx, ru_mul(a, w->hi[j]));
            } else {
                if (side_open_hi(w->hi[j])) open_mn = 1;
                else mn = rd_add(mn, rd_mul(a, w->hi[j]));
                if (side_open_lo(w->lo[j])) open_mx = 1;
                else mx = ru_add(mx, ru_mul(a, w->lo[j]));
            }
        }
    }
    lim[0] = mn; lim[1] = mx;
    side[0] = open_mn; side[1] = open_mx;
}

/* EXACT infeasibility reports (ray hints over ORIGINAL rows; the psv
 * lane re-verifies before anything prints) */
static void report_infeasible_row(PreSolve *ps, int m, int i, double s)
{
    ps->inf_ray = (double*)psolve_calloc((size_t)(m ? m : 1), sizeof(double));
    ps->inf_ray[i] = s;
}
static void report_infeasible_pair(PreSolve *ps, int m, int i, double s,
                                   int i2, double s2)
{
    ps->inf_ray = (double*)psolve_calloc((size_t)(m ? m : 1), sizeof(double));
    ps->inf_ray[i] = s; ps->inf_ray[i2] = s2;
}

/* find the singleton record that fired row src (for pair rays) */
static const PreRec *find_singleton_rec(const PreSolve *ps, int src)
{
    for (int t = 0; t < ps->n_recs; t++)
        if (ps->recs[t].kind == PRK_ROW_SINGLETON && ps->recs[t].i == src)
            return &ps->recs[t];
    return NULL;
}

/* ------------------------------------------------------------------ */
/* bound fold with provenance and exact-conflict rules.                */
/* vlo/vhi are outward-rounded implied candidates (vlo <= exact lower,  */
/* vhi >= exact upper), used for BOTH the fold and the (sound)         */
/* conflict test.  Returns 1 folded, 0 unchanged, -1 exact INFEASIBLE  */
/* (lower-claim conflict), -4 (upper-claim conflict), -3/-5 try a      */
/* singleton-pair ray, -2 ABORT.                                       */
/* ------------------------------------------------------------------ */
static int fold_bounds(PW *w, int j, double vlo, double vhi,
                       int src_row, int src_kind, int j_side_orig_both)
{
    /* implied lower bound */
    if (vlo > w->lo[j]) {
        if (vlo > w->hi[j]) {
            if (w->hi_src[j] < 0 && src_kind == PRK_ROW_SINGLETON) return -1;
            if (w->hi_src[j] < 0 && src_kind == PRK_ROW_DOUBLETON &&
                j_side_orig_both) return -1;
            if (src_kind == PRK_ROW_SINGLETON && w->hi_src[j] >= 0) return -3;
            return -2;
        }
        if (vlo > -LP_INF && vlo < LP_INF) {   /* sentinel-crossing: skip */
            w->lo[j] = vlo; w->lo_src[j] = src_row;
        }
        return 1;
    }
    if (vhi < w->hi[j]) {
        if (vhi < w->lo[j]) {
            if (w->lo_src[j] < 0 && src_kind == PRK_ROW_SINGLETON) return -4;
            if (w->lo_src[j] < 0 && src_kind == PRK_ROW_DOUBLETON &&
                j_side_orig_both) return -4;
            if (src_kind == PRK_ROW_SINGLETON && w->lo_src[j] >= 0) return -5;
            return -2;
        }
        if (vhi > -LP_INF && vhi < LP_INF) {
            w->hi[j] = vhi; w->hi_src[j] = src_row;
        }
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* the reduction engine                                                */
/* ------------------------------------------------------------------ */
int lp_presolve(const LP *lp, LP *red, PreSolve **ps_out, PreStats *st)
{
    int n, m;
    long nnz0;
    PW w; PreSolve *ps;
    int aborted = 0;             /* 1 decline, 2 exact-infeasible */
    PreStats stats;
    if (st) memset(&stats, 0, sizeof(stats));
    if (!lp || lp->n <= 0 || lp->m < 0 || !red || !ps_out) return -1;
    n = lp->n; m = lp->m;
    nnz0 = lp->Acolptr ? lp->Acolptr[n] : 0;
    *ps_out = NULL;

    pw_init(&w, lp);
    ps = ps_new(n, m);
    int *rownnz = (int*)psolve_malloc((size_t)(m ? m : 1) * sizeof(int));

    /* ---- read-time pass: empty columns (notes only valid here) ---- */
    pw_rebuild(&w, rownnz);
    for (int j = 0; j < n && !aborted; j++) {
        PCol *pc = &w.col[j];
        if (pc->len != 0 || w.col_dead[j]) continue;
        double cj = w.c[j];
        double walk = lp->maximize ? cj : -cj;   /* improvement direction */
        double v; int give_note = 0;
        if (walk > 0) {
            if (side_open_hi(w.hi[j])) {
                v = side_open_lo(w.lo[j]) ? 0.0 : w.lo[j]; give_note = 1;
            } else v = w.hi[j];
        } else if (walk < 0) {
            if (side_open_lo(w.lo[j])) {
                v = side_open_hi(w.hi[j]) ? 0.0 : w.hi[j]; give_note = 1;
            } else v = w.lo[j];
        } else {
            v = side_open_lo(w.lo[j]) ? (side_open_hi(w.hi[j]) ? 0.0 : w.hi[j])
                                      : w.lo[j];
        }
        PreRec r; memset(&r, 0, sizeof(r));
        r.kind = PRK_COL_EMPTY; r.j = j; r.v = v; r.cj = cj;
        if (ps_add_rec(ps, &r) != 0) { aborted = 1; break; }
        w.objconst = ru_add(w.objconst, ru_mul(cj, v));
        w.col_dead[j] = 1;
        stats.cols_removed++; stats.empty_cols++;
        if (give_note) {
            ps_add_note(ps, j, walk > 0 ? 1.0 : -1.0);
            stats.unbounded_notes++;
        }
    }

    /* ---- fixpoint passes ---- */
    for (int pass = 0; pass < PRE_PASS_CAP && !aborted; pass++) {
        int fired = 0;
        pw_rebuild(&w, rownnz);
        stats.passes = pass + 1;
        if (w.nnz > PRE_NNZ_GROWTH * (nnz0 ? nnz0 : 1)) { aborted = 1; break; }

        /* empty rows + singleton rows (bounds only mutate, image stays) */
        for (int i = 0; i < m && !aborted; i++) {
            if (w.row_dead[i]) continue;
            int js[2]; double as[2];
            int got = row_collect(&w, i, js, as);
            if (got == 0) {
                char R = w.rel[i]; double bi = w.b[i];
                int incon = (R == '<' && bi < 0.0) || (R == '>' && bi > 0.0) ||
                            (R == '=' && bi != 0.0);
                if (incon) {           /* 0 R b false: one-row ray, but
                       only when the row is still the ORIGINAL one - a
                       substitution-cancelled row's e_i ray does not
                       separate on original data (the fallback decides) */
                    if (w.row_touched[i]) { aborted = 1; break; }
                    double s = (R == '<') ? 1.0 : (R == '>') ? -1.0
                                   : (bi > 0 ? -1.0 : 1.0);
                    report_infeasible_row(ps, m, i, s);
                    aborted = 2; break;
                }
                PreRec r; memset(&r, 0, sizeof(r));
                r.kind = PRK_ROW_EMPTY; r.i = i;
                if (ps_add_rec(ps, &r) != 0) { aborted = 1; break; }
                w.row_dead[i] = 1; fired = 1;
                stats.rows_removed++; stats.empty_rows++;
                continue;
            }
            if (got == 1) {
                int j = js[0]; double a = as[0];
                char R = w.rel[i]; double bi = w.b[i];
                int want_lo = 0, want_hi = 0;
                if (R == '=') { want_lo = want_hi = 1; }
                else if (R == '<') { if (a > 0) want_hi = 1; else want_lo = 1; }
                else               { if (a > 0) want_lo = 1; else want_hi = 1; }
                double v_dn = rd_div(bi, a), v_up = ru_div(bi, a);
                int fb = fold_bounds(&w, j,
                                     want_lo ? v_dn : w.lo[j],
                                     want_hi ? v_up : w.hi[j],
                                     i, PRK_ROW_SINGLETON, 0);
                if (fb == -1 || fb == -4) {
                    /* singleton-implied bound vs ORIGINAL box side: the
                       row itself separates (sign derived in the design
                       notes; re-verified by the psv lane downstream) -
                       gated on the row still being the original one */
                    if (w.row_touched[i]) { aborted = 1; break; }
                    double s = (fb == -1) ? -1.0 : 1.0;
                    if (a < 0) s = -s;
                    report_infeasible_row(ps, m, i, s);
                    aborted = 2; break;
                }
                if (fb == -3 || fb == -5) {
                    /* conflict vs another singleton-derived side: pair
                       ray from the two row records; coefficient signs
                       admitted by both rels and separating yb */
                    int i2 = (fb == -3) ? w.hi_src[j] : w.lo_src[j];
                    const PreRec *r2 = find_singleton_rec(ps, i2);
                    if (!r2 || r2->j != j ||
                        w.row_touched[i] || w.row_touched[i2]) {
                        aborted = 1; break;
                    }
                    double a2 = r2->a_ij;
                    char rr2 = w.rel[i2];
                    double alpha = 0.0, beta = 0.0, yb = 0.0;
                    int ok = 0;
                    for (int sgn = 1; sgn >= -1 && !ok; sgn -= 2) {
                        alpha = sgn / a; beta = -sgn / a2;
                        int sok = 1;
                        if (R == '<' && alpha < 0) sok = 0;
                        if (R == '>' && alpha > 0) sok = 0;
                        if (rr2 == '<' && beta < 0) sok = 0;
                        if (rr2 == '>' && beta > 0) sok = 0;
                        yb = alpha * bi + beta * w.b[i2];
                        if (sok && yb < 0.0) ok = 1;
                    }
                    if (!ok) { aborted = 1; break; }
                    report_infeasible_pair(ps, m, i, alpha, i2, beta);
                    aborted = 2; break;
                }
                if (fb == -2) { aborted = 1; break; }
                PreRec r; memset(&r, 0, sizeof(r));
                r.kind = PRK_ROW_SINGLETON; r.i = i; r.j = j; r.a_ij = a;
                r.rel_i = R; r.bi = bi; r.cj = w.c[j];
                if (ps_snapshot(ps, &w.col[j], w.row_dead,
                                &r.snap_off, &r.snap_n) != 0 ||
                    ps_add_rec(ps, &r) != 0) { aborted = 1; break; }
                w.row_dead[i] = 1; fired = 1;
                stats.rows_removed++; stats.singleton_rows++;
                continue;
            }
        }

        /* redundant rows via directed-rounding activity limits */
        if (!aborted)
        for (int i = 0; i < m && !aborted; i++) {
            if (w.row_dead[i]) continue;
            int js[2]; double as[2];
            if (row_collect(&w, i, js, as) <= 1) continue;
            double lim[2]; int side[2];
            row_activity(&w, i, lim, side);
            char R = w.rel[i]; double bi = w.b[i];
            if (R == '<') {
                if (!side[0] && lim[0] > bi) {
                    if (w.row_touched[i]) { aborted = 1; break; }
                    report_infeasible_row(ps, m, i, 1.0);
                    aborted = 2; break;
                }
                if (!side[1] && lim[1] <= bi) {
                    PreRec r; memset(&r, 0, sizeof(r));
                    r.kind = PRK_ROW_REDUNDANT; r.i = i;
                    if (ps_add_rec(ps, &r) != 0) { aborted = 1; break; }
                    w.row_dead[i] = 1; fired = 1;
                    stats.rows_removed++; stats.redundant_rows++;
                }
            } else if (R == '>') {
                if (!side[1] && lim[1] < bi) {
                    if (w.row_touched[i]) { aborted = 1; break; }
                    report_infeasible_row(ps, m, i, -1.0);
                    aborted = 2; break;
                }
                if (!side[0] && lim[0] >= bi) {
                    PreRec r; memset(&r, 0, sizeof(r));
                    r.kind = PRK_ROW_REDUNDANT; r.i = i;
                    if (ps_add_rec(ps, &r) != 0) { aborted = 1; break; }
                    w.row_dead[i] = 1; fired = 1;
                    stats.rows_removed++; stats.redundant_rows++;
                }
            }
            /* '=' redundancy/conflict: skip - the engine chain decides */
        }

        /* fixed columns (l == u after folds) */
        if (!aborted)
        for (int j = 0; j < n && !aborted; j++) {
            if (w.col_dead[j]) continue;
            if (w.lo[j] != w.hi[j]) continue;
            if (side_open_lo(w.lo[j]) || side_open_hi(w.hi[j])) continue;
            double v = w.lo[j];
            PreRec r; memset(&r, 0, sizeof(r));
            r.kind = PRK_COL_FIXED; r.j = j; r.v = v; r.cj = w.c[j];
            if (ps_add_rec(ps, &r) != 0) { aborted = 1; break; }
            w.objconst = ru_add(w.objconst, ru_mul(w.c[j], v));
            PCol *pc = &w.col[j];
            for (int k = 0; k < pc->len; k++) {
                w.b[pc->rows[k]] -= pc->vals[k] * v;   /* data-level fold */
                w.row_touched[pc->rows[k]] = 1;
            }
            w.col_dead[j] = 1;
            fired = 1;
            stats.cols_removed++; stats.fixed_cols++;
        }

        /* doubleton equality rows LAST (they open new opportunities) */
        if (!aborted)
        for (int i = 0; i < m && !aborted; i++) {
            if (w.row_dead[i]) continue;
            if (w.rel[i] != '=') continue;
            int js[2]; double as[2];
            if (row_collect(&w, i, js, as) != 2) continue;   /* exact count */
            int piv = 0;
            double A0 = fabs(as[0]), A1 = fabs(as[1]);
            if (A1 > A0 || (A1 == A0 && w.col[js[1]].len < w.col[js[0]].len))
                piv = 1;
            int j = js[piv], kk = js[1 - piv];
            double a = as[piv], ak = as[1 - piv];
            double abso = fabs(ak / a);
            if (!(abso <= 1.0) || !isfinite(abso)) continue;  /* stability */
            double bover = w.b[i] / a;
            double tcoef = -ak / a;
            double bi = w.b[i];
            /* implied interval on x_k = (bi - a x_j)/ak, outward; only
               folded/tested when x_j's sides are both finite... the
               conflict gate additionally requires them ORIGINAL (the
               one-row ray uses the original box) */
            double tlo = w.lo[kk], thi = w.hi[kk];
            int j_both_orig = (w.lo_src[j] < 0 && w.hi_src[j] < 0);
            if (!side_open_lo(w.lo[j]) && !side_open_hi(w.hi[j])) {
                double ld = rd_mul(-a, w.lo[j]), lu = ru_mul(-a, w.lo[j]);
                double hd = rd_mul(-a, w.hi[j]), hu = ru_mul(-a, w.hi[j]);
                double t1d = ld < hd ? ld : hd, t1u = lu > hu ? lu : hu;
                double t2d = rd_add(bi, t1d), t2u = ru_add(bi, t1u);
                if (ak > 0) { tlo = rd_div(t2d, ak); thi = ru_div(t2u, ak); }
                else        { tlo = rd_div(t2u, ak); thi = ru_div(t2d, ak); }
                int fb = fold_bounds(&w, kk, tlo, thi, i,
                                     PRK_ROW_DOUBLETON, j_both_orig);
                if (fb == -1 || fb == -4) {
                    if (w.row_touched[i]) { aborted = 1; break; }
                    double lim[2]; int side[2];
                    row_activity(&w, i, lim, side);
                    if (!side[0] && lim[0] > bi)
                        report_infeasible_row(ps, m, i, 1.0);
                    else if (!side[1] && lim[1] < bi)
                        report_infeasible_row(ps, m, i, -1.0);
                    else { aborted = 1; break; }
                    aborted = 2; break;
                }
                if (fb == -2) { aborted = 1; break; }
            }
            /* snapshot pivot column BEFORE mutation, then substitute
               x_j = bover + tcoef*x_k */
            PreRec r; memset(&r, 0, sizeof(r));
            r.kind = PRK_ROW_DOUBLETON; r.i = i; r.j = j; r.k = kk;
            r.a_ij = a; r.tcoef = tcoef; r.bover = bover; r.cj = w.c[j];
            if (ps_snapshot(ps, &w.col[j], w.row_dead,
                            &r.snap_off, &r.snap_n) != 0 ||
                ps_add_rec(ps, &r) != 0) { aborted = 1; break; }
            PCol *pk = &w.col[kk];
            if (w.nnz + w.col[j].len > PRE_NNZ_GROWTH * (nnz0 ? nnz0 : 1)) {
                aborted = 1; break;
            }
            w.objconst = ru_add(w.objconst, ru_mul(w.c[j], bover));
            w.c[kk] += w.c[j] * tcoef;
            w.c[j] = 0.0;
            for (int e = 0; e < w.col[j].len; e++) {
                int rr = w.col[j].rows[e];
                double arj = w.col[j].vals[e];
                if (rr == i || w.row_dead[rr]) continue;
                w.b[rr] -= arj * bover;
                w.row_touched[rr] = 1;
                int pos = -1;
                for (int q = 0; q < pk->len; q++)
                    if (pk->rows[q] == rr) { pos = q; break; }
                if (pos < 0) {
                    if (pk->len == pk->cap) {
                        pk->cap *= 2;
                        pk->rows = (int*)psolve_realloc((void**)&pk->rows,
                                                        (size_t)pk->cap * sizeof(int));
                        pk->vals = (double*)psolve_realloc((void**)&pk->vals,
                                                           (size_t)pk->cap * sizeof(double));
                    }
                    pos = pk->len++;
                    pk->rows[pos] = rr; pk->vals[pos] = 0.0;
                    w.nnz++;
                }
                pk->vals[pos] += arj * tcoef;
            }
            w.row_dead[i] = 1; w.col_dead[j] = 1;
            fired = 1;
            stats.rows_removed++; stats.doubleton_rows++;
            stats.cols_removed++;
        }
        if (!fired || aborted) break;
    }

    psolve_free(rownnz);

    if (aborted == 2) {                 /* exact infeasible with ray hint */
        *ps_out = ps;
        if (st) *st = stats;
        pw_free(&w);
        return 1;
    }
    if (aborted || ps->n_recs == 0) {
        /* declined (budget/touch/unsupported), or genuinely nothing to
           reduce: no records exist, so there is nothing to postsolve -
           hand the caller an explicit "declined" and let the ORIGINAL
           data path solve the model. */
        lp_presolve_free(ps);
        pw_free(&w);
        if (st) *st = stats;
        return -1;
    }

    /* ---- compact to the reduced LP + maps ---- */
    {
        int nr = 0, mr = 0;
        int *rmap = (int*)psolve_malloc((size_t)n * sizeof(int));
        int *imap = (int*)psolve_malloc((size_t)(m ? m : 1) * sizeof(int));
        for (int j = 0; j < n; j++) rmap[j] = w.col_dead[j] ? -1 : nr++;
        for (int i = 0; i < m; i++) imap[i] = w.row_dead[i] ? -1 : mr++;
        /* nr == 0 (everything eliminated by reductions) is NOT declined:
           there is no engine run for an empty model, so the caller owns
           a direct-certificate branch - the postsolve maps produce x
           (and duals) with no engine input and the psv lane decides. */
        ps->n_red = nr; ps->m_red = mr;
        ps->row_orig = (int*)psolve_malloc((size_t)(mr ? mr : 1) * sizeof(int));
        ps->col_orig = (int*)psolve_malloc((size_t)nr * sizeof(int));
        for (int j = 0; j < n; j++) if (rmap[j] >= 0) ps->col_orig[rmap[j]] = j;
        for (int i = 0; i < m; i++) if (imap[i] >= 0) ps->row_orig[imap[i]] = i;

        long nnzr = 0;
        for (int j = 0; j < n; j++) if (!w.col_dead[j]) nnzr += w.col[j].len;
        red->n = nr; red->m = mr; red->maximize = lp->maximize;
        red->c = (double*)psolve_malloc((size_t)(nr ? nr : 1) * sizeof(double));
        red->l = (double*)psolve_malloc((size_t)(nr ? nr : 1) * sizeof(double));
        red->u = (double*)psolve_malloc((size_t)(nr ? nr : 1) * sizeof(double));
        red->b = (double*)psolve_malloc((size_t)(mr ? mr : 1) * sizeof(double));
        red->rel = (char*)psolve_malloc((size_t)(mr ? mr : 1));
        red->Acolptr = (int*)psolve_malloc((size_t)(nr + 1) * sizeof(int));
        red->Arow = (int*)psolve_malloc((size_t)(nnzr ? nnzr : 1) * sizeof(int));
        red->Aval = (double*)psolve_malloc((size_t)(nnzr ? nnzr : 1) * sizeof(double));
        long pos = 0; int cix = 0;
        for (int j = 0; j < n; j++) {
            if (w.col_dead[j]) continue;
            red->Acolptr[cix] = (int)pos;
            red->c[cix] = w.c[j]; red->l[cix] = w.lo[j]; red->u[cix] = w.hi[j];
            for (int e = 0; e < w.col[j].len; e++) {
                red->Arow[pos] = imap[w.col[j].rows[e]];
                red->Aval[pos] = w.col[j].vals[e];
                pos++;
            }
            cix++;
        }
        red->Acolptr[nr] = (int)pos;
        for (int i = 0; i < m; i++)
            if (!w.row_dead[i]) { red->b[imap[i]] = w.b[i];
                                  red->rel[imap[i]] = w.rel[i]; }
        stats.n_red = nr; stats.m_red = mr; stats.nnz_red = nnzr;
        stats.nnz_removed = nnz0 - nnzr;
        psolve_free(rmap); psolve_free(imap);
    }
    if (st) *st = stats;
    pw_free(&w);
    *ps_out = ps;
    return 0;
}

/* ------------------------------------------------------------------ */
/* replay maps                                                         */
/* ------------------------------------------------------------------ */
int lp_presolve_postsolve_x(const PreSolve *ps, const double *x_red,
                            double *x_orig)
{
    if (!ps || !x_orig) return -1;
    for (int j = 0; j < ps->n_orig; j++) x_orig[j] = 0.0;
    for (int jj = 0; jj < ps->n_red; jj++)
        x_orig[ps->col_orig[jj]] = x_red ? x_red[jj] : 0.0;
    for (int t = ps->n_recs - 1; t >= 0; t--) {
        const PreRec *r = &ps->recs[t];
        switch (r->kind) {
        case PRK_COL_FIXED:
        case PRK_COL_EMPTY:
            x_orig[r->j] = r->v; break;
        case PRK_ROW_DOUBLETON:
            x_orig[r->j] = r->bover + r->tcoef * x_orig[r->k]; break;
        default: break;             /* row drops: columns untouched */
        }
    }
    return 0;
}

int lp_presolve_postsolve_ray(const PreSolve *ps, const double *d_red,
                              double *d_orig)
{
    if (!ps || !d_orig) return -1;
    for (int j = 0; j < ps->n_orig; j++) d_orig[j] = 0.0;
    for (int jj = 0; jj < ps->n_red; jj++)
        d_orig[ps->col_orig[jj]] = d_red ? d_red[jj] : 0.0;
    for (int t = ps->n_recs - 1; t >= 0; t--) {
        const PreRec *r = &ps->recs[t];
        switch (r->kind) {
        case PRK_COL_FIXED:
        case PRK_COL_EMPTY:
            d_orig[r->j] = 0.0; break;
        case PRK_ROW_DOUBLETON:
            d_orig[r->j] = r->tcoef * d_orig[r->k]; break;
        default: break;
        }
    }
    return 0;
}

/* Dual replay: rows alive at the end take the engine duals; each
 * row-elimination record recovers its dual, in REVERSE firing order,
 * from the invariant rc_j == 0 on the pivot column:
 *   y_i = ( c_j(firing) - sum_{r != i in snapshot} y_r * a_rj ) / a_ij .
 * The snapshot covers rows ACTIVE at firing; a reference to a row whose
 * dual is not yet replayed (an elimination-order cycle) makes the map
 * untransferable: return -1 and the caller falls back, NEVER guesses.
 *
 * Complementarity guard (singleton rows): the rc formula alone pins the
 * pivot column's reduced cost to zero but says nothing about WHICH
 * eliminated row carries the nonzero dual when several pile on one
 * column - and the psv dual bound needs the complementary assignment
 * (a slack row must sit at y = 0 or the bound inflates and the claim
 * justifiably rejects).  An inequality singleton whose implied side is
 * not tight at the postsolved point therefore takes y_i = 0 instead of
 * the formula value.  Equality rows are always tight. */
int lp_presolve_postsolve_duals(const PreSolve *ps, const double *y_red,
                                const double *x_orig, double *y_orig)
{
    if (!ps || !y_orig || !x_orig) return -1;
    char *known = (char*)psolve_calloc((size_t)(ps->m_orig ? ps->m_orig : 1), 1);
    for (int i = 0; i < ps->m_orig; i++) y_orig[i] = 0.0;
    for (int ii = 0; ii < ps->m_red; ii++) {
        y_orig[ps->row_orig[ii]] = y_red ? y_red[ii] : 0.0;
        known[ps->row_orig[ii]] = 1;
    }
    for (int t = ps->n_recs - 1; t >= 0; t--) {
        const PreRec *r = &ps->recs[t];
        if (r->kind == PRK_ROW_EMPTY || r->kind == PRK_ROW_REDUNDANT) {
            y_orig[r->i] = 0.0; known[r->i] = 1;
            continue;
        }
        if (r->kind != PRK_ROW_SINGLETON && r->kind != PRK_ROW_DOUBLETON)
            continue;
        if (r->kind == PRK_ROW_SINGLETON && r->rel_i != '=') {
            double ax = r->a_ij * x_orig[r->j];
            double tol = 1e-9 * (1.0 + fabs(r->bi) + fabs(ax));  /* TOLSHEET TOL-LP-PRETIGHT */
            int tight = (r->rel_i == '<') ? (ax >= r->bi - tol)
                                          : (ax <= r->bi + tol);
            if (!tight) { y_orig[r->i] = 0.0; known[r->i] = 1; continue; }
        }
        double num = r->cj;
        for (int q = 0; q < r->snap_n; q++) {
            int rr = ps->snap_row[r->snap_off + q];
            if (rr == r->i) continue;
            if (!known[rr]) { psolve_free(known); return -1; }
            num -= y_orig[rr] * ps->snap_coef[r->snap_off + q];
        }
        y_orig[r->i] = num / r->a_ij;
        known[r->i] = 1;
    }
    psolve_free(known);
    return 0;
}

int lp_presolve_farkas_ray(const PreSolve *ps, double *y_orig)
{
    if (!ps || !ps->inf_ray || !y_orig) return -1;
    memcpy(y_orig, ps->inf_ray, (size_t)ps->m_orig * sizeof(double));
    return 0;
}

int lp_presolve_unbounded_note(const PreSolve *ps, double *walk_out)
{
    if (!ps || ps->n_notes == 0) return -1;
    if (walk_out) *walk_out = ps->notes[0].walk;
    return ps->notes[0].var;
}

int lp_presolve_row_orig(const PreSolve *ps, int rred)
{
    if (!ps || rred < 0 || rred >= ps->m_red) return -1;
    return ps->row_orig[rred];
}

int lp_presolve_col_orig(const PreSolve *ps, int cred)
{
    if (!ps || cred < 0 || cred >= ps->n_red) return -1;
    return ps->col_orig[cred];
}
