/* -*- c -*-  Exact-rational simplex core, instantiated once per integer width.
 *
 * This file is #included by fx.c with the following macros defined:
 *
 *   FXNUM        signed integer type holding a numerator/denominator
 *   FXUNUM       the matching unsigned type (for gcd magnitudes)
 *   FX_HAS_WIDE  1 if FXWIDE (a type twice as wide) exists, else 0
 *   FXWIDE       the wide type (only when FX_HAS_WIDE)
 *   FXP(x)       token-pasting prefix, e.g. fx64_##x
 *
 * Every arithmetic operation is *checked*: when a result (after full gcd
 * reduction) no longer fits in FXNUM, the core sets its overflow flag and the
 * solve is abandoned with FX_OVERFLOW.  It never returns a silently wrapped —
 * i.e. wrong — "exact" answer.  fx_solve() retries an overflowing 64-bit solve
 * in the 128-bit instantiation before giving up.
 */

typedef struct { FXNUM num, den; } FXP(R);

/* Overflow/failure flag for this instantiation.  Thread-local so two threads
 * can solve independently (the library is otherwise re-entrant). */
static _Thread_local int FXP(ovf);

static FXNUM FXP(gcd)(FXNUM a, FXNUM b)
{
    FXUNUM u = a < 0 ? (FXUNUM)(-(a + 1)) + 1u : (FXUNUM)a;
    FXUNUM v = b < 0 ? (FXUNUM)(-(b + 1)) + 1u : (FXUNUM)b;
    while (v) { FXUNUM t = u % v; u = v; v = t; }
    return (FXNUM)u;
}

/* Build a reduced fraction from values that are already known to fit. */
static inline FXP(R) FXP(mk)(FXNUM n, FXNUM d)
{
    FXP(R) r;
    if (n == 0) { r.num = 0; r.den = 1; return r; }
    if (d == 0) { FXP(ovf) = 1; r.num = 0; r.den = 1; return r; }
    if (d < 0) { n = -n; d = -d; }
    FXNUM g = FXP(gcd)(n, d);
    r.num = n / g; r.den = d / g;
    return r;
}

/* Same, but the caller guarantees gcd(n,d)==1 (cross-cancelled path). */
static inline FXP(R) FXP(mk_reduced)(FXNUM n, FXNUM d)
{
    FXP(R) r;
    if (n == 0) { r.num = 0; r.den = 1; return r; }
    if (d == 0) { FXP(ovf) = 1; r.num = 0; r.den = 1; return r; }
    if (d < 0) { n = -n; d = -d; }
    r.num = n; r.den = d;
    return r;
}

static inline FXP(R) FXP(zero)(void) { FXP(R) r; r.num = 0; r.den = 1; return r; }

static inline FXP(R) FXP(from_ll)(long long v)
{
    FXP(R) r; r.num = (FXNUM)v; r.den = 1; return r;
}

#if FX_HAS_WIDE
/* Reduce a wide fraction and narrow it back to FXNUM, flagging overflow when
 * even the fully reduced value does not fit. */
static FXP(R) FXP(fix_wide)(FXWIDE n, FXWIDE d)
{
    FXP(R) r;
    if (n == 0) { r.num = 0; r.den = 1; return r; }
    if (d == 0) { FXP(ovf) = 1; r.num = 0; r.den = 1; return r; }
    if (d < 0) { n = -n; d = -d; }
    FXWIDE a = n < 0 ? -n : n, b = d;
    while (b) { FXWIDE t = a % b; a = b; b = t; }
    n /= a; d /= a;
    const FXWIDE lim = (FXWIDE)1 << (8 * (FXWIDE)sizeof(FXNUM) - 2);  /* 2^62 for int64 */
    if (n > lim * 2 - 1 || n < -(lim * 2) || d > lim * 2 - 1) {
        FXP(ovf) = 1; r.num = 0; r.den = 1; return r;
    }
    r.num = (FXNUM)n; r.den = (FXNUM)d;
    return r;
}
#endif

static inline FXP(R) FXP(neg)(FXP(R) a) { a.num = -a.num; return a; }

static FXP(R) FXP(add_slow)(FXP(R) a, FXP(R) b)
{
#if FX_HAS_WIDE
    return FXP(fix_wide)((FXWIDE)a.num * b.den + (FXWIDE)b.num * a.den,
                         (FXWIDE)a.den * b.den);
#else
    /* No wider type: cancel the common part of the denominators first
     * (a.den = g*ad, b.den = g*bd  =>  lcm = g*ad*bd). */
    FXNUM g = FXP(gcd)(a.den, b.den);
    FXNUM ad = a.den / g, bd = b.den / g;
    FXNUM t1, t2, n, d;
    if (__builtin_mul_overflow(a.num, bd, &t1) ||
        __builtin_mul_overflow(b.num, ad, &t2) ||
        __builtin_add_overflow(t1, t2, &n) ||
        __builtin_mul_overflow(ad, b.den, &d)) {
        FXP(ovf) = 1; return FXP(zero)();
    }
    return FXP(mk)(n, d);
#endif
}

static inline FXP(R) FXP(add)(FXP(R) a, FXP(R) b)
{
    FXNUM t1, t2, n, d;
    if (!__builtin_mul_overflow(a.num, b.den, &t1) &&
        !__builtin_mul_overflow(b.num, a.den, &t2) &&
        !__builtin_add_overflow(t1, t2, &n) &&
        !__builtin_mul_overflow(a.den, b.den, &d))
        return FXP(mk)(n, d);
    return FXP(add_slow)(a, b);
}

static inline FXP(R) FXP(sub)(FXP(R) a, FXP(R) b) { return FXP(add)(a, FXP(neg)(b)); }

static FXP(R) FXP(mul_slow)(FXP(R) a, FXP(R) b)
{
#if FX_HAS_WIDE
    return FXP(fix_wide)((FXWIDE)a.num * b.num, (FXWIDE)a.den * b.den);
#else
    /* cross-cancel before multiplying */
    FXNUM g1 = FXP(gcd)(a.num, b.den), g2 = FXP(gcd)(b.num, a.den);
    if (g1 == 0) g1 = 1;
    if (g2 == 0) g2 = 1;
    FXNUM an = a.num / g1, bd = b.den / g1, bn = b.num / g2, ad = a.den / g2;
    FXNUM n, d;
    if (__builtin_mul_overflow(an, bn, &n) || __builtin_mul_overflow(ad, bd, &d)) {
        FXP(ovf) = 1; return FXP(zero)();
    }
    return FXP(mk_reduced)(n, d);
#endif
}

static inline FXP(R) FXP(mul)(FXP(R) a, FXP(R) b)
{
    FXNUM n, d;
    if (!__builtin_mul_overflow(a.num, b.num, &n) &&
        !__builtin_mul_overflow(a.den, b.den, &d))
        return FXP(mk)(n, d);
    return FXP(mul_slow)(a, b);
}

static inline FXP(R) FXP(div)(FXP(R) a, FXP(R) b)
{
    if (b.num == 0) { FXP(ovf) = 1; return FXP(zero)(); }   /* never silently 0/0 */
    FXP(R) inv; inv.num = b.den; inv.den = b.num;
    if (inv.den < 0) { inv.num = -inv.num; inv.den = -inv.den; }
    return FXP(mul)(a, inv);
}

static inline int FXP(cmp)(FXP(R) a, FXP(R) b)
{
    FXNUM l, r;
    if (!__builtin_mul_overflow(a.num, b.den, &l) &&
        !__builtin_mul_overflow(b.num, a.den, &r))
        return l < r ? -1 : (l > r ? 1 : 0);
#if FX_HAS_WIDE
    {
        FXWIDE L = (FXWIDE)a.num * b.den, Rr = (FXWIDE)b.num * a.den;
        return L < Rr ? -1 : (L > Rr ? 1 : 0);
    }
#else
    {
        /* compare a-b exactly; if that overflows the difference cannot be
         * represented and the solve is abandoned rather than guessed. */
        FXP(R) d = FXP(sub)(a, b);
        if (FXP(ovf)) return 0;
        return d.num < 0 ? -1 : (d.num > 0 ? 1 : 0);
    }
#endif
}

static inline int FXP(is_zero)(FXP(R) a) { return a.num == 0; }

/* Allocate an array initialized to exactly 0/1 so the hot arithmetic never
 * sees a den==0 entry. */
static FXP(R) *FXP(zalloc)(size_t n)
{
    if (n == 0) n = 1;
    size_t bytes;
    if (__builtin_mul_overflow(n, sizeof(FXP(R)), &bytes)) return NULL;
    FXP(R) *a = (FXP(R)*)malloc(bytes);
    if (a) for (size_t i = 0; i < n; i++) { a[i].num = 0; a[i].den = 1; }
    return a;
}

/* ------------------------------------------------------------------ */
/* Tableau                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    int M, K, V;
    FXP(R) *T;
    int *basis, *activeVar, *activeRow, *isart;
} FXP(Tab);

static inline FXP(R) FXP(tab)(const FXP(Tab) *t, int r, int c)
{
    return t->T[(size_t)r * (t->K + 1) + c];
}
static inline void FXP(tabset)(FXP(Tab) *t, int r, int c, FXP(R) v)
{
    t->T[(size_t)r * (t->K + 1) + c] = v;
}

static void FXP(pivot)(FXP(Tab) *t, int r, int j)
{
    FXP(R) p = FXP(tab)(t, r, j);
    int K = t->K, M = t->M, S = K + 1;
    FXP(R) *T = t->T;
    FXP(R) *Tr = &T[(size_t)r * S];
    for (int c = 0; c <= K; c++) Tr[c] = FXP(div)(Tr[c], p);
    for (int i = 0; i <= M; i++) {
        if (i == r) continue;
        FXP(R) *Ti = &T[(size_t)i * S];
        FXP(R) f = Ti[j];
        if (f.num == 0) continue;
        for (int c = 0; c <= K; c++) {
            FXP(R) b = Tr[c];
            if (b.num == 0) continue;
            Ti[c] = FXP(sub)(Ti[c], FXP(mul)(f, b));
        }
        if (FXP(ovf)) return;      /* abandon: the tableau is no longer exact */
    }
    t->basis[r] = j;
}

/* Returns 0 optimal, 2 unbounded, 3 iteration limit, 4 arithmetic overflow. */
static int FXP(simplex)(FXP(Tab) *t, long itercap, long *iters)
{
    int M = t->M, K = t->K;
    long niter = 0;
    int use_bland = 0;
    long stale = 0;
    FXP(R) z = FXP(zero)();
    for (;;) {
        int ej = -1;
        if (!use_bland) {
            for (int j = 0; j < K; j++) {
                if (!t->activeVar[j]) continue;
                int basic = 0; for (int r = 0; r < M; r++) if (t->basis[r] == j) { basic = 1; break; }
                if (basic) continue;
                if (FXP(cmp)(FXP(tab)(t, M, j), z) < 0) {
                    if (ej < 0 || FXP(cmp)(FXP(tab)(t, M, j), FXP(tab)(t, M, ej)) < 0) ej = j;
                }
            }
        } else {
            for (int j = 0; j < K; j++) {
                if (!t->activeVar[j]) continue;
                int basic = 0; for (int r = 0; r < M; r++) if (t->basis[r] == j) { basic = 1; break; }
                if (basic) continue;
                if (FXP(cmp)(FXP(tab)(t, M, j), z) < 0) { ej = j; break; }
            }
        }
        *iters = niter;
        if (FXP(ovf)) return 4;
        if (ej < 0) return 0;   /* optimal */
        int lr = -1; FXP(R) best = FXP(zero)();
        for (int r = 0; r < M; r++) {
            if (!t->activeRow[r]) continue;
            FXP(R) a = FXP(tab)(t, r, ej);
            if (FXP(cmp)(a, z) <= 0) continue;
            FXP(R) ratio = FXP(div)(FXP(tab)(t, r, K), a);
            if (lr < 0 || FXP(cmp)(ratio, best) < 0 ||
                (FXP(cmp)(ratio, best) == 0 && t->basis[r] < t->basis[lr])) {
                lr = r; best = ratio;
            }
        }
        if (FXP(ovf)) { *iters = niter; return 4; }
        if (lr < 0) return 2;   /* unbounded */
        FXP(R) zbefore = FXP(tab)(t, M, K);
        FXP(pivot)(t, lr, ej);
        if (FXP(ovf)) { *iters = niter; return 4; }
        if (FXP(cmp)(FXP(tab)(t, M, K), zbefore) == 0) stale++; else stale = 0;
        if (stale > (long)(t->V + M + 2)) use_bland = 1;
        niter++;
        if (niter >= itercap) { *iters = niter; return 3; }
    }
}

/* ------------------------------------------------------------------ */
/* Solve                                                               */
/* ------------------------------------------------------------------ */

/* Convert a public (int64) Fx into this core's representation. */
static inline FXP(R) FXP(of_fx)(Fx v)
{
    FXP(R) r;
    if (v.den == 0) { r.num = 0; r.den = 1; return r; }
    r.num = (FXNUM)v.num; r.den = (FXNUM)v.den;
    if (r.den < 0) { r.num = -r.num; r.den = -r.den; }
    return r;
}

/* Narrow back to the public int64 Fx; 0 on success, -1 if it does not fit. */
static inline int FXP(to_fx)(FXP(R) v, Fx *out)
{
    if (v.num > (FXNUM)0x7fffffffffffffffLL || v.num < -(FXNUM)0x7fffffffffffffffLL ||
        v.den > (FXNUM)0x7fffffffffffffffLL)
        return -1;
    out->num = (long long)v.num; out->den = (long long)v.den;
    return 0;
}

/* The exact two-phase full-tableau simplex.  Returns an FX_* status. */
static int FXP(solve)(const FxLP *lp, FxResult *res)
{
    FXP(ovf) = 0;
    int n = lp->n, m = lp->m;
    int status = FX_OPTIMAL;
    long iters = 0;

    FXP(R) *base = FXP(zalloc)((size_t)(n ? n : 1));
    int *sign = (int*)calloc((size_t)(n ? n : 1), sizeof(int));
    int *yidx = (int*)malloc((size_t)(n ? n : 1) * sizeof(int));
    FXP(R) *A = NULL, *b = NULL, *coef = NULL;
    char *rel = NULL;
    int *slackcol = NULL;
    FXP(Tab) t; memset(&t, 0, sizeof(t));
    if (!base || !sign || !yidx) { status = FX_ALLOC_FAIL; goto done; }

    for (int j = 0; j < n; j++) yidx[j] = -1;
    int V = 0, ubrows = 0;
    for (int j = 0; j < n; j++) {
        FXP(R) lo = FXP(of_fx)(lp->l[j]), hi = FXP(of_fx)(lp->u[j]);
        if (lp->lfinite[j] && lp->ufinite[j] && FXP(cmp)(lo, hi) == 0) {
            base[j] = lo; sign[j] = 1; yidx[j] = -1;      /* fixed variable */
        } else {
            int use_lo = lp->lfinite[j];
            base[j] = use_lo ? lo : hi;
            sign[j] = use_lo ? 1 : -1;
            yidx[j] = V++;
            if (use_lo && lp->ufinite[j]) ubrows++;
        }
    }
    int M = m + ubrows;

    A = FXP(zalloc)((size_t)(M ? (size_t)M * V : 1));
    b = FXP(zalloc)((size_t)(M ? M : 1));
    rel = (char*)malloc((size_t)(M ? M : 1));
    if (!A || !b || !rel) { status = FX_ALLOC_FAIL; goto done; }

    int row = 0;
    FXP(R) zero = FXP(zero)();
    for (int i = 0; i < m; i++) {
        FXP(R) rhs = FXP(of_fx)(lp->b[i]); char r = lp->rel[i];
        for (int j = 0; j < n; j++) {
            FXP(R) a = FXP(of_fx)(lp->A[(size_t)i * n + j]);
            if (FXP(is_zero)(a)) continue;
            if (yidx[j] < 0) { rhs = FXP(sub)(rhs, FXP(mul)(a, base[j])); }
            else {
                A[(size_t)row * V + yidx[j]] = FXP(mul)(a, FXP(from_ll)(sign[j]));
                rhs = FXP(sub)(rhs, FXP(mul)(a, base[j]));
            }
        }
        if (FXP(cmp)(rhs, zero) < 0) {
            for (int j = 0; j < V; j++) A[(size_t)row * V + j] = FXP(neg)(A[(size_t)row * V + j]);
            rhs = FXP(neg)(rhs);
            if (r == '<') r = '>'; else if (r == '>') r = '<';
        }
        b[row] = rhs; rel[row] = r; row++;
    }
    for (int j = 0; j < n; j++) {
        if (yidx[j] < 0) continue;
        if (lp->lfinite[j] && lp->ufinite[j]) {
            A[(size_t)row * V + yidx[j]] = FXP(from_ll)(1);
            b[row] = FXP(sub)(FXP(of_fx)(lp->u[j]), FXP(of_fx)(lp->l[j]));
            rel[row] = '<'; row++;
        }
    }
    if (FXP(ovf)) { status = FX_OVERFLOW; goto done; }

    int S = 0;
    for (int r = 0; r < M; r++) if (rel[r] != '=') S++;
    slackcol = (int*)calloc((size_t)(M ? M : 1), sizeof(int));
    if (!slackcol) { status = FX_ALLOC_FAIL; goto done; }
    { int cur = V; for (int r = 0; r < M; r++) { if (rel[r] != '=') slackcol[r] = cur++; else slackcol[r] = -1; } }

    int sense_mult = lp->maximize ? 1 : -1;
    FXP(R) C0 = FXP(zero)();
    for (int j = 0; j < n; j++) C0 = FXP(add)(C0, FXP(mul)(FXP(of_fx)(lp->c[j]), base[j]));
    coef = FXP(zalloc)((size_t)(V ? V : 1));
    if (!coef) { status = FX_ALLOC_FAIL; goto done; }
    for (int j = 0; j < n; j++)
        if (yidx[j] >= 0)
            coef[yidx[j]] = FXP(mul)(FXP(of_fx)(lp->c[j]), FXP(from_ll)((long long)(sense_mult * sign[j])));

    int K = V + S + M;
    t.M = M; t.K = K; t.V = V;
    t.T = FXP(zalloc)((size_t)((size_t)(M + 1) * (size_t)(K + 1)));
    t.basis = (int*)malloc((size_t)(M ? M : 1) * sizeof(int));
    t.activeVar = (int*)malloc((size_t)(K ? K : 1) * sizeof(int));
    t.activeRow = (int*)malloc((size_t)(M ? M : 1) * sizeof(int));
    t.isart = (int*)calloc((size_t)(K ? K : 1), sizeof(int));
    if (!t.T || !t.basis || !t.activeVar || !t.activeRow || !t.isart) { status = FX_ALLOC_FAIL; goto done; }
    for (int j = 0; j < K; j++) t.activeVar[j] = 1;
    for (int r = 0; r < M; r++) t.activeRow[r] = 1;

    for (int r = 0; r < M; r++) {
        for (int j = 0; j < V; j++) FXP(tabset)(&t, r, j, A[(size_t)r * V + j]);
        if (slackcol[r] >= 0) FXP(tabset)(&t, r, slackcol[r], FXP(from_ll)(rel[r] == '<' ? 1 : -1));
        int av = V + S + r;
        FXP(tabset)(&t, r, av, FXP(from_ll)(1)); t.isart[av] = 1;
        FXP(tabset)(&t, r, K, b[r]);
        t.basis[r] = av;
    }

    /* Phase I: maximize -sum(artificials) */
    for (int j = 0; j < K; j++) {
        if (t.isart[j]) { FXP(tabset)(&t, M, j, FXP(zero)()); continue; }
        FXP(R) s = FXP(zero)();
        for (int r = 0; r < M; r++) s = FXP(add)(s, FXP(tab)(&t, r, j));
        FXP(tabset)(&t, M, j, FXP(neg)(s));
    }
    FXP(R) z0 = FXP(zero)();
    for (int r = 0; r < M; r++) z0 = FXP(sub)(z0, FXP(tab)(&t, r, K));
    FXP(tabset)(&t, M, K, z0);
    if (FXP(ovf)) { status = FX_OVERFLOW; goto done; }

    long capI = 10000 + 200L * (M + V + S);
    long sub = 0;
    status = FXP(simplex)(&t, capI, &sub); iters += sub;
    if (status == 4) { status = FX_OVERFLOW; goto done; }
    if (status == 3) { status = FX_ITER_LIMIT; goto done; }
    if (status == 2) { status = FX_ITER_LIMIT; goto done; }  /* cannot happen: Phase I is bounded */
    if (FXP(cmp)(FXP(tab)(&t, M, K), FXP(zero)()) != 0) { status = FX_INFEASIBLE; goto done; }

    /* pivot basic artificials out (or drop the redundant row) */
    for (int a = 0; a < M; a++) {
        int av = V + S + a;
        int r = -1; for (int rr = 0; rr < M; rr++) if (t.basis[rr] == av) { r = rr; break; }
        if (r < 0) continue;
        int ej = -1;
        for (int j = 0; j < V + S; j++) if (t.activeVar[j] && !FXP(is_zero)(FXP(tab)(&t, r, j))) { ej = j; break; }
        if (ej >= 0) { FXP(pivot)(&t, r, ej); }
        else { t.activeRow[r] = 0; t.activeVar[av] = 0; }
    }
    for (int a = 0; a < M; a++) t.activeVar[V + S + a] = 0;
    if (FXP(ovf)) { status = FX_OVERFLOW; goto done; }

    /* Phase II: maximize coef over y */
    for (int j = 0; j < K; j++) {
        if (!t.activeVar[j]) { FXP(tabset)(&t, M, j, FXP(zero)()); continue; }
        FXP(R) rc = FXP(zero)();
        for (int r = 0; r < M; r++) {
            if (!t.activeRow[r]) continue;
            int bv = t.basis[r];
            FXP(R) cB = (bv < V) ? coef[bv] : FXP(zero)();
            rc = FXP(sub)(rc, FXP(mul)(cB, FXP(tab)(&t, r, j)));
        }
        if (j < V) rc = FXP(add)(rc, coef[j]);
        FXP(tabset)(&t, M, j, FXP(neg)(rc));
    }
    FXP(R) z02 = FXP(zero)();
    for (int r = 0; r < M; r++) {
        if (!t.activeRow[r]) continue;
        int bv = t.basis[r];
        FXP(R) cB = (bv < V) ? coef[bv] : FXP(zero)();
        z02 = FXP(add)(z02, FXP(mul)(cB, FXP(tab)(&t, r, K)));
    }
    FXP(tabset)(&t, M, K, z02);
    if (FXP(ovf)) { status = FX_OVERFLOW; goto done; }

    long capII = 10000 + 200L * (V + M + S);
    long sub2 = 0;
    status = FXP(simplex)(&t, capII, &sub2); iters += sub2;
    if (status == 4) { status = FX_OVERFLOW; goto done; }
    if (status == 3) { status = FX_ITER_LIMIT; goto done; }
    if (status == 2) { status = FX_UNBOUNDED; goto done; }

    /* optimal: report the exact objective and variable values */
    {
        FXP(R) maxval = FXP(tab)(&t, M, K);
        FXP(R) objv = (sense_mult == 1) ? FXP(add)(C0, maxval) : FXP(sub)(C0, maxval);
        if (FXP(ovf)) { status = FX_OVERFLOW; goto done; }
        Fx obj;
        if (FXP(to_fx)(objv, &obj) != 0) { status = FX_OVERFLOW; goto done; }
        res->x = (Fx*)calloc((size_t)(n ? n : 1), sizeof(Fx));
        if (!res->x) { status = FX_ALLOC_FAIL; goto done; }
        for (int j = 0; j < n; j++) {
            FXP(R) xv;
            if (yidx[j] < 0) xv = base[j];
            else {
                int k = yidx[j];
                FXP(R) yv = FXP(zero)();
                for (int r = 0; r < M; r++) if (t.basis[r] == k) { yv = FXP(tab)(&t, r, K); break; }
                xv = FXP(add)(base[j], FXP(mul)(FXP(from_ll)(sign[j]), yv));
            }
            if (FXP(ovf) || FXP(to_fx)(xv, &res->x[j]) != 0) {
                free(res->x); res->x = NULL;
                status = FX_OVERFLOW; goto done;
            }
        }
        res->obj = obj;
        status = FX_OPTIMAL;
    }

done:
    res->iters = iters;
    if (FXP(ovf) && status != FX_ALLOC_FAIL) status = FX_OVERFLOW;
    free(base); free(sign); free(yidx); free(A); free(b); free(rel);
    free(slackcol); free(coef);
    free(t.T); free(t.basis); free(t.activeVar); free(t.activeRow); free(t.isart);
    res->status = status;
    return status;
}
