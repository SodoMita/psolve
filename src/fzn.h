#ifndef PSOLVE_FZN_H
#define PSOLVE_FZN_H

/* FlatZinc reader + solver bridge (Phase 3, in progress).
 *
 * Parses a FlatZinc (.fzn) file produced by the MiniZinc compiler and solves
 * the subset of constraints it can handle natively (linear int/bool/float
 * constraints, int_lin_*, bool_*, set_in, all_different, ...) with the LP
 * solver.  Nonlinear/unhandled constraints are reported so the driver can
 * return =====UNKNOWN===== rather than silently giving a wrong answer.
 *
 * This file defines the parsed model structures and the read/solve API.  The
 * current implementation covers:
 *   - full lexer (identifiers, ints, floats, strings, keywords, symbols)
 *   - parser for predicate/par/var declarations, arrays, annotations, solve
 *   - name->index map, array index ranges, domain (l..u) annotations
 *   - linear + simple nonlinear constraint dispatch table
 *   - solve satisfy / minimize / maximize, FlatZinc output + stats
 */

/* ------------------------------------------------------------------ */
/* Parsed model                                                        */
/* ------------------------------------------------------------------ */

typedef enum {
    FZ_K_NONE = 0,
    FZ_K_INT, FZ_K_FLOAT, FZ_K_BOOL
} FZKind;

/* one declared name: either a parameter (fixed value / par array) or a
 * decision variable (var / var array) */
typedef struct {
    char *name;        /* declared name */
    FZKind kind;       /* int/float/bool */
    int   is_var;      /* 1 = decision variable, 0 = parameter */
    int   is_array;    /* 1 = array decl */
    int   n;           /* number of elements (array) or 1 (scalar) */
    int   index_lo;    /* FlatZinc lower array index (1 for scalars/default arrays) */
    int   base_idx;    /* first solver index for vars; array element i is base_idx+i */
    int   is_output;   /* ::output_var / ::output_array */
    /* parameter value(s) */
    double *par;       /* fixed values for parameters (NULL if var) */
    int   *par_int;
    /* domain bounds for vars (from :: l..u annotation); -inf/+inf if unset */
    double *lo, *hi;
    int   has_lo, has_hi;   /* any element has a bound */
    long  *setvals; int nset;  /* exact set domain (if gapped), for SOS1 */
    int is_alias;      /* var-array that aliases already-declared vars */
} FZDecl;

typedef struct {
    int  npred;
    /* not stored in detail; predicate decls are skipped (builtins known) */
} FZPred;

/* a linear term list used both for constraint rows and the objective */
typedef struct {
    int   n;
    int   *idx;        /* variable indices */
    double *coef;      /* coefficients */
    double constant;   /* constant term */
} FZLin;

/* one constraint: predicate name + raw argument tokens are re-parsed by the
   handler; we store a serialized arg list for dispatch. */
typedef struct FZConstr {
    char *pred;               /* predicate name, e.g. "int_lin_le" */
    char **args;              /* raw argument strings (for simple cases) */
    int   nargs;
    int   handled;            /* set by handler */
    struct FZConstr *next;
} FZConstr;

typedef struct {
    FZDecl *decls;
    int  ndecl, cap_decl;
    FZConstr *constr;
    int  nconstr;
    int  nvars;        /* total decision variables (scalar + array elements) */
    /* solve item */
    int  solve_kind;   /* 0 satisfy, 1 minimize, 2 maximize */
    FZLin objective;   /* for minimize/maximize */
    int  solve_expr_is_var; /* if objective is just a var */
    char *file;
} FZModel;

/* Read a .fzn file into a model.  Returns 0 on success, -1 on parse error.
 * The model owns all memory (free with fz_model_free). */
int  fz_read(const char *path, FZModel *m);

/* Solve the model with the LP solver; fills an FZSolution. */
typedef struct {
    int  status;         /* 0 solved, 1 unsat, 2 unknown (unhandled/unbounded),
                            3 parse error, 4 node/time limit */
    int  nvars;
    double *x;           /* value per solver variable index */
    double obj;
    long  iters;
    long  nodes;
    double best_bound;
    long  node_limit;
} FZSolution;

/* Build + solve the LP from the model.  Unhandled constraints -> UNKNOWN. */
void fz_solve(const FZModel *m, FZSolution *sol);
void fz_solution_free(FZSolution *sol);

void fz_model_free(FZModel *m);

/* output a solution in FlatZinc format (declarations + values) */
void fz_print_solution(const FZModel *m, const FZSolution *sol);

#endif
