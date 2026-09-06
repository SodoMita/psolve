/* Unified evidence objects for verdict-printing exits (roadmap 6.4).
 *
 * One claim type per verdict class, one entry point: psv_cert_check().
 * A CLI that is about to print OPTIMAL / INFEASIBLE / UNBOUNDED (or the
 * FlatZinc/MIP/QP equivalents) fills a PsvCert over the ORIGINAL model
 * data plus the engine's evidence payload, and calls psv_cert_check
 * exactly once.  A REJECT or DEFER answer must degrade the printed
 * verdict to the honest not-proven class (UNKNOWN / NUMERICAL_*): the
 * checker is the single choke point that makes "forgot to re-check" a
 * structurally unreachable bug class, and the substrate Phase 16 proof
 * export builds on.
 *
 * What the checker re-verifies, per kind (all arithmetic over the
 * caller's ORIGINAL data; every numeric margin is an explicit claim field
 * whose semantics are documented in docs/DESIGN.md section 8.9):
 *
 *   PSVK_LP_OPTIMAL      primal feasibility (bounds+rows) + bounded-LP
 *                        Lagrangian dual bound from the engine's duals:
 *                        sign-invalid dual components are clipped to 0
 *                        (soundly weakening the bound), reduced costs
 *                        rc = s*c - A^T y must admit a finite dual bound,
 *                        and the gap dual_bound - s*c^T x must close.
 *                        Also: claimed objective == recomputed c^T x.
 *   PSVK_LP_INFEASIBLE   directed-rounding Farkas box separation
 *                        (solver_farkas_boxcert semantics; the ray is a
 *                        hint and gets fully re-verified).
 *   PSVK_LP_UNBOUNDED    primal-feasible point + recession ray: A d
 *                        sign-consistent per row relation, every macro
 *                        component of d has an infinite bound on the side
 *                        it walks toward, and s*c^T d > 0.
 *   PSVK_MIP_POINT       bounds + rows + EXACT integrality of the
 *                        integer-flagged components (engine incumbents
 *                        are stored snapped) + claimed objective ==
 *                        recomputed c^T x.  When `proven_optimal` is
 *                        claimed, also DIRECTIONAL bound coherence: the
 *                        engine's best_bound is a running extremum over
 *                        solved node relaxations (the root LP bound
 *                        dominates forever), so a proven optimum need NOT
 *                        close to it - the residual is the model's root
 *                        integrality gap.  The sound stamp is that the
 *                        bound never sits on the wrong side of the
 *                        objective (max: bound >= obj; min: bound <= obj)
 *                        up to arithmetic grace.
 *   PSVK_QP_OPTIMAL      primal rows + stationarity residual of
 *                        (Q x + c + A^T mu) + multiplier sign/complement
 *                        + claimed objective == recomputed QP objective.
 *                        (Global optimality additionally rests on the
 *                        engine-side PSD gate; the evidence object
 *                        records that the gate ran.)
 *   PSVK_QP_UNBOUNDED    primal-feasible point + ray d with A d <= 0
 *                        (strict, mirroring qp.c's deliberate no-margin
 *                        rule), d^T Q d within the curvature tolerance
 *                        and (Q x + c)^T d < 0.
 *   PSVK_EXHAUSTION      search-completed stamp for UNSAT-by-exhaustion
 *                        verdicts (CP-FlatZinc, MIP tree exhaustion):
 *                        discrete coherence - no node/time/cooperative
 *                        limit may be set on a completed search.  This
 *                        class carries no numeric certificate; it exists
 *                        so a truncated search can NEVER be printed as
 *                        UNSAT through this entry point.
 *
 * Result codes: PSV_OK (claim verified), PSV_REJECT (evidence contradicts
 * the claim), PSV_DEFER (evidence cannot certify: NaN/inf, missing
 * payload, vacuous dual bound).  REJECT and DEFER both mean: do not
 * print that verdict.  The checker NEVER prints and never mutates the
 * model or payload.
 */
#ifndef PSV_CERT_H
#define PSV_CERT_H

/* INFINITY-token parity with the engines: bounds at/above this magnitude
 * encode "no bound" in the LP/MIP data handed to the checker. */
#define PSV_INF 1e29  /* TOLSHEET TOL-CERT-INF */

typedef enum {
    PSVK_LP_OPTIMAL = 1,
    PSVK_LP_INFEASIBLE = 2,
    PSVK_LP_UNBOUNDED = 3,
    PSVK_MIP_POINT = 4,
    PSVK_EXHAUSTION = 5,
    PSVK_QP_OPTIMAL = 6,
    PSVK_QP_UNBOUNDED = 7
} PsvKind;

typedef enum { PSV_OK = 0, PSV_REJECT = 1, PSV_DEFER = 2 } PsvRc;

typedef struct {
    PsvKind kind;

    /* ---- shared LP-form problem view (System-in-C-scolumn) ---- */
    int n;                 /* variables */
    int m;                 /* rows */
    const int *colptr;     /* CSC, n+1 */
    const int *row;        /* CSC row indices */
    const double *val;     /* CSC values */
    const char *rel;       /* '<','>','=' per row */
    const double *b;       /* rhs */
    const double *lo, *hi; /* variable bounds; |.| >= PSV_INF codes infinity */
    const double *c;       /* objective */
    int maximize;          /* 1 max, 0 min (sense s = +1/-1) */

    /* ---- payload: what the engine claims ---- */
    double obj;            /* claimed objective value (point verdicts) */
    const double *x;       /* claimed primal point (n) */
    const double *y;       /* dual vector in ORIGINAL row space AND original
                              objective sense (the shadow-price convention of
                              solver_duals); the checker normalizes to max
                              form internally before sign analysis */
    const double *ray;     /* Farkas or recession ray (m or n per kind) */

    /* MIP_POINT only */
    const unsigned char *isint;  /* n, integer flags (may be NULL) */
    int proven_optimal;          /* 1 = tree exhausted, optimum proven */
    double best_bound;           /* report bound for the coherence stamp */
    double mip_gap;              /* engine gap contract (rel), e.g. 1e-4 */

    /* QP payload (kinds 6/7): dense column-major Q (n*n), row-major A,
       row vector bq, linear term cq; objective 1/2 x^T Q x + cq^T x with
       A x <= bq.  x/ray/minimized through the shared fields. */
    const double *Q, *A, *bq, *cq;
    const double *mu;            /* multipliers for A x <= bq (m) */

    /* EXHAUSTION only: discrete search stamps */
    long nodes;
    long node_limit;             /* <= 0 means no limit configured */
    int stopped;                 /* cooperative stop / time limit hit */

    /* V-class margins (docs/DESIGN.md section 8.9).  Explicit, never
       hidden in a #define: the claim decides how strict the re-check is,
       and the engine passes its own contract values. */
    double gt_box;     /* primal bound grace (LP) */
    double gt_row;     /* primal row grace (LP/MIP/QP) */
    double dt_dj;      /* dual/reduced-cost/grace (LP dual bound, gap) */
    double dt_gap;     /* dual-gap closure grace (LP OPTIMAL) */
    double dt_obj;     /* claimed-objective consistency grace */
} PsvCert;

/* One entry point, total over all kinds.  Never fails, never mutates. */
PsvRc psv_cert_check(const PsvCert *cl);

/* Human-readable class tag for diagnostics (e.g. "lp_optimal"). */
const char *psv_cert_kind_name(PsvKind k);

#endif /* PSV_CERT_H */
