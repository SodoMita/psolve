/* Presolve + postsolve for the LP engine (roadmap 7.1).
 *
 * Reductions on the caller-visible LP (BEFORE the engine's free-variable
 * split, mlt sign rows and 7.5 equilibration), plus the reverse maps that
 * transfer certificates back to the ORIGINAL model exactly.  The LP CLI
 * pipeline is
 *
 *   read -> lp_presolve -> solver_create_opts(reduced, scale_mode)
 *        -> solve -> lp_presolve_postsolve_* -> psv lane on ORIGINAL data
 *
 * so every printed verdict passes the same 6.4 original-data
 * re-verification as before, and any evidence the maps cannot produce
 * degrades to the CLI's raw-path fallback instead of printing.  Presolve
 * never fabricates: every uncertain situation either SKIPS the reduction
 * (equivalence is then trivially preserved) or declines the whole run
 * (caller walks the untouched original).
 *
 * Reductions (each recorded for replay; fired to a pass-capped fixpoint):
 *   fixed columns (l==u, finite)      fold the constant everywhere
 *   empty rows                        consistent: drop (dual 0);
 *                                     inconsistent: EXACT infeasible with
 *                                     a one-row Farkas ray hint
 *   empty columns (empty at READ)     fix at the cost-sign bound; a cost
 *                                     walking through an open side gives
 *                                     a certified-UNBOUNDED note
 *   singleton rows (1 entry)          fold the implied bound into the
 *                                     box (directed rounding outward),
 *                                     dual replay via pivot-column
 *                                     snapshot; conflicts against an
 *                                     ORIGINAL box side are exact
 *                                     infeasible with a one-row ray,
 *                                     conflicts against another
 *                                     singleton-derived bound get a
 *                                     two-row ray; anything deeper
 *                                     declines the run
 *   redundant rows (directed-rounding activity limits prove the row can
 *                                     never bind): drop (dual 0);
 *                                     activity-conflicts: exact
 *                                     infeasible with a one-row ray
 *   doubleton equality rows (2 ent.)  x_j := (b - a_k x_k)/a_j
 *                                     substitution (fill-capped), primal
 *                                     replay by the same map, dual replay
 *                                     via pivot-column snapshot
 *
 * Soundness invariants (the whole design rests on these; tools check
 * them functionally and the psv lane re-verifies every consequence):
 *  1. every reduction preserves the feasible-set projection and the
 *     objective value of the projected optimum EXACTLY (substitutions are
 *     performed with directed rounding only where a bound crosses the
 *     working model, always enlarging it by at most rounding);
 *  2. dual replay for row-elimination recs processes records in REVERSE
 *     firing order and uses only (a) the firing-time pivot-column
 *     snapshot (entries then-active), (b) the firing-time current cost,
 *     (c) duals of rows eliminated LATER (already replayed) and of rows
 *     alive at the end (engine duals).  Rows eliminated earlier have no
 *     snapshot entries by construction, so the replay is well-founded;
 *  3. presolve-reported INFEASIBLE is only emitted with an explicit
 *     Farkas ray over ORIGINAL rows that the caller passes through the
 *     standard psv_cert_check(PSVK_LP_INFEASIBLE) lane - presolve's own
 *     arithmetic is never the verdict, the directed-rounding certificate
 *     is;
 *  4. anything the maps cannot transfer (reduced-model INFEASIBLE/ ray
 *     replay gaps / psv reject) is a caller-level fallback event, not a
 *     verdict.
 *
 * Memory: reduction records carry pivot-column snapshots bounded by a
 * byte budget; exceeding it (or the record cap) declines the run.
 *
 * Concurrency: a PreSolve is caller-owned state; the module holds no
 * globals.  Allocation errors unwind through psolve_fail like the rest.
 */
#ifndef LP_PRESOLVE_H
#define LP_PRESOLVE_H
#include "solver.h"

typedef struct PreRec PreRec;   /* record layout is module-private */
typedef struct PreSolve PreSolve;

/* statistics for the --prestat lane and AUDIT tables */
typedef struct {
    long rows_removed, cols_removed, nnz_removed;
    long fixed_cols, empty_cols, empty_rows, singleton_rows,
         redundant_rows, doubleton_rows, unbounded_notes;
    int passes;
    int n_red, m_red;      /* reduced dims (valid when rc == 0) */
    long nnz_red;
} PreStats;

/* Run the reduction fixpoint.
 *  rc  0: *red is the reduced LP (free with lp_free), *ps_out holds the
 *         replay state (free with lp_presolve_free); stats filled.
 *  rc  1: EXACT infeasibility found by reduction logic; *ps_out holds a
 *         Farkas ray hint (lp_presolve_farkas_ray, length lp->m,
 *         ORIGINAL-row units) that the caller MUST route through the psv
 *         Farkas lane before printing; *red untouched.
 *  rc -1: presolve declines (budget/consistency); solve the original.
 *         *red untouched, *ps_out NULL, stats hold the reason counts. */
int  lp_presolve(const LP *lp, LP *red, PreSolve **ps_out, PreStats *stats);

/* Replay maps (rc -1 means "map cannot transfer this evidence"; the
 * caller then falls back).  x/dual inputs are engine outputs on the
 * REDUCED model; outputs are ORIGINAL-length arrays provided by the
 * caller. */
int  lp_presolve_postsolve_x(const PreSolve *ps, const double *x_red,
                             double *x_orig);
/* dual replay additionally needs the postsolved primal point: the
 * complementarity guard (slack singleton row -> dual 0) reads it */
int  lp_presolve_postsolve_duals(const PreSolve *ps, const double *y_red,
                                 const double *x_orig, double *y_orig);
int  lp_presolve_postsolve_ray(const PreSolve *ps, const double *d_red,
                               double *d_orig);

/* rc==1 witness: write the ORIGINAL-length Farkas ray (len lp->m) */
int  lp_presolve_farkas_ray(const PreSolve *ps, double *y_orig);

/* certified-UNBOUNDED notes from empty columns: after an OPTIMAL reduced
 * solve, if a note exists the ORIGINAL model is unbounded with the
 * postsolved point and ray walk*e_{var}; returns var index or -1,
 * writes the walk sign (+-1, improvement direction, objective-raw) */
int  lp_presolve_unbounded_note(const PreSolve *ps, double *walk_out);

/* dimension maps for the CLI: reduced row/col -> original indices */
int  lp_presolve_row_orig(const PreSolve *ps, int rred);
int  lp_presolve_col_orig(const PreSolve *ps, int cred);

void lp_presolve_free(PreSolve *ps);

#endif /* LP_PRESOLVE_H */
