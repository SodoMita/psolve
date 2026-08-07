# psolve — a fixed-point (integer) LP, QP, MIP & physics solver for real-time use

A from-scratch **linear programming** (revised simplex), **quadratic
programming** (active-set), **mixed-integer programming** (branch-and-bound),
and **real-time 2D-physics kernel** (projected Gauss-Seidel) library — tuned
for modern x86 hardware (AVX-512 FMA), cache-friendly sparse data layout, and
interactive latency.  It has **no third-party dependencies** (only libc/libm),
is small and auditable, and supports **incremental solving** (warm starts).

**The real-time physics kernel is an integer (fixed-point) solver.** The
projected-Gauss-Seidel kernel used for contact/friction-style boxed QPs runs on
**fixed-point arithmetic** (`int64_t` + 128-bit intermediates) for bit-identical
determinism across platforms (replay / network safety) and predictable,
branch-friendly cost.  The **LP/QP/MIP solvers are floating-point** (`double`);
the fixed-point PGS kernel is cross-validated against the float reference.  So:
fixed-point for the real-time physics hot loop, double precision for the exact
LP/QP/MIP backbone.

See [`docs/ROADMAP.md`](docs/ROADMAP.md) for the long-term plan (real-time UI /
vector graphics / 2D physics / FlatZinc completeness).

It reads a simple text LP format, solves it, and prints the optimum.  The QP
solver (`qpsolve`) solves convex QPs (minimize ½xᵀQx + cᵀx s.t. Ax ≤ b).  It
ships a GLPK-based differential tester, a scipy-based QP verifier, a benchmark
harness, and an incremental-solving test.

## Building

```sh
make            # produces ./lpsolve and ./qpsolve
make clean
```

The default `ARCH=-march=native` builds for the CPU it is compiled on: on an
AVX-512 machine the vectorized kernels are used; on older CPUs the compiler
emits the scalar/AVX2 fallbacks (see `src/kernels.c`).  To target a specific
baseline, pass e.g. `make ARCH="-march=x86-64 -mavx2"`.  Because the code uses
compile-time feature detection, **a binary is only guaranteed to run on the
architecture (or newer) that it was built for** — build on the target host, or
with an explicit baseline `ARCH`.

## Usage

```sh
./lpsolve [-t ms|--time-limit ms] <problem.lp> [--print]  # LP (revised simplex, double)
./qpsolve <qp.qp>                      # convex QP (active-set)
./mipsolve <problem.lp> <nint> <j...> [--print]   # MIP (branch-and-bound)
./fznsolve <problem.fzn>               # FlatZinc reader + solver (Phase 3)
./fxsolve <problem.lp> [--print]      # LP (exact rational / fixed-point simplex)
```

`--print` also dumps the optimal variable values. `-t` / `--time-limit` sets a
cooperative wall-clock limit in milliseconds; `Ctrl-C` also stops the double LP
solve cleanly. Both return `STOPPED` rather than presenting a partial solution
as optimal.

The LP solver handles: **maximize or minimize**, `<`, `>`, and `=` constraints,
variables with lower, upper, boxed, or fully free bounds (use `-inf inf`), and
correctly reports `OPTIMAL`, `INFEASIBLE`, `UNBOUNDED`, `NUMERICAL_FAILURE`, or
`ITERATION_LIMIT`. A free variable is normalized internally as the difference
of two non-negative variables; the public LP/MIP APIs, printed solution, warm
starts, and added-row path continue to use the original variable dimension.
See `examples/free_vars.lp` for a runnable model.

The QP solver handles: **minimize** ½xᵀQx + cᵀx subject to Ax ≤ b with Q
symmetric positive semi-definite (convex), reporting the optimum, Lagrange
multipliers, and status (solved / infeasible).

The MIP solver (`mipsolve <lp> <nint> <j0 j1 ...>`) solves mixed-integer
programs by **branch-and-bound** over the revised-simplex LP relaxation: the
listed variables are required to be integer.  It reports the optimal objective,
the number of B&B nodes, and the incumbent solution.

## Fixed-point (exact-rational) LP solver

`fxsolve` is the **fixed-point analogue** of the double `lpsolve`: it reads the
same `.lp` format but represents every number as an exact rational
(numerator/denominator pair, reduced, with `__int128` intermediates) and solves
by an exact two-phase full-tableau simplex.  Because the arithmetic is integer,
the result is **exact and bit-identical** across platforms/compilers — the same
determinism guarantee the fixed-point PGS physics kernel already provides, now
applied to the LP backbone:

```sh
./fxsolve examples/diet.lp
# objective (exact): 33/25      <- the true rational optimum, not a float approx
# objective (dec):   1.320000000000

./lpsolve examples/exact.lp | grep objective    # double:  0.333333333333333
./fxsolve examples/exact.lp | grep exact        # fixed:   objective (exact): 1/3
```

Compared with `lpsolve` (`make fx_bench`):

- **Precision / determinism**: strictly better.  Every reported objective and
  variable value is an exact rational; there is no rounding, no
  `LP_INF`-sentinel clamping, and no `NUMERICAL_FAILURE`.  On unbounded LPs the
  double solver can clamp a variable at its internal 1e30 bound and report a
  bogus huge "OPTIMAL"; `fxsolve` reports `UNBOUNDED` correctly.
- **Performance**: comparable-to-faster for tiny problems (single-digit
  microseconds at n≲10; `make fx_bench` shows `fxsolve` about 0.6× the double
  solver's time on the examples).  The pivot uses **Dantzig's entering rule**
  with an anti-cycling Bland fallback, `static inline` fast-path rational
  arithmetic (arrays `0/1`-initialized so no per-cell cleanup branch), and
  zero-skip in the elimination — a gprof-driven optimization worth ~1.0–1.7×
  on random dense/sparse instances with identical exact results.  It still
  degrades on larger dense problems, because exact rational arithmetic with
  coefficient growth (two gcd reductions per tableau cell) costs more than
  double SIMD.  Use `fxsolve` where exactness/determinism matters on small,
  data-friendly problems (UI/layout, integer MiniZinc LPs); keep `lpsolve` for
  large sparse/continuous instances.

The exact LP core is `src/fx.c` / `src/fx.h`; the CLI is `tools/fxsolve.c` and
the double-vs-fixed benchmark is `tools/fx_bench.c`.  Differential test:
`tools/fx_verify.py` (random feasible + arbitrary LPs vs `lpsolve`, checking
status and objective agreement plus fixed-point determinism).

> Note: exact rational arithmetic assumes the LP data stay small enough that
> `__int128` intermediates do not overflow.  Very ill-conditioned large
> instances should use the double `lpsolve`.

## Real-time physics kernel (projected Gauss-Seidel)

The boxed-QP workhorse used by real-time 2D physics engines for contact /
friction resolution, in **both float (reference)** and **fixed-point (integer,
production)** forms:

```c
/* float reference (src/pgs.c) */
PGSOptions opt = { n, /*iters*/ 20, /*omega*/ 1.0, /*tol*/ 1e-10 };
PGSResult res;
pgs_solve(&opt, A, b, lo, hi, x, &res);

/* fixed-point (src/pgs_fixed.c) — bit-identical on every platform */
const int64_t S = 65536;                    /* Q16.16 scale */
PGSFixedOptions fopt = { n, /*iters*/ 20, /*w_num*/ 1, /*w_den*/ 1, /*tol*/ 1 };
PGSResult fres;
pgsf_solve(&fopt, A, b, lo, hi, x, &fres);  /* all integers, 128-bit accumulators */
```

Both solve `min ½xᵀAx + bᵀx  s.t.  lo ≤ x ≤ hi` for symmetric PSD A by
projected Gauss-Seidel with SOR.  The fixed-point kernel uses integer division
(round-half-away), rational relaxation `ω=w_num/w_den`, and 128-bit
intermediate sums — **deterministic, zero-malloc, bounded-work**.  It is
validated against the float solver on thousands of random PSD systems.

Measured on this box (2 CPU cores):

```
        float PGS          fixed-point PGS
  n=4    0.08 µs             0.08 µs
  n=16   0.35 µs             0.53 µs
  n=32   0.84 µs             1.60 µs
  n=64   2.27 µs             6.53 µs   (scalar __int128; float uses AVX-512 FMA)
```

Build benchmarks with `make pgsbench` (float) and `make pgfbench` (fixed);
see `examples/contact_pgs.c` and `examples/contact_pgs_fixed.c`.

**PGS vs LP/QP** (`make pgs_vs_lp`) quantifies the foundation choice: on
identical warm-started physics boxed-QP problems, PGS-fixed is ~60–600× faster
than the general LP simplex and ~10–30× faster than the exact active-set QP:

```
 n   PGS-fixed  active-setQP  LPsimplex
 8    0.23 us     1.3 us       13.6 us
32    1.93 us    15.0 us      929   us
64    6.83 us    70.4 us     4039   us
```

So PGS is the right foundation for a per-frame physics hot loop; the LP/QP/MIP
solvers are the exact correctness backbone for everything else.

## Sensitivity analysis (LP)

The LP solver exposes dual (shadow-price) and reduced-cost values:

```c
double dual[M];  solver_duals(s, dual);           /* shadow prices per row */
double rc[n];    solver_reduced_costs(s, rc);     /* reduced costs per var */
```

It also respects a per-solver simplex iteration limit (`s->iteration_limit`,
default 2,000,000); hitting it returns status `ITERATION_LIMIT` instead of
running forever on a pathological input.

## Security & untrusted input

`lpsolve` and `qpsolve` parse files supplied on the command line, which may be
attacker-controlled.  The parsers are hardened accordingly:

- **Dimensions, counts, and matrix indices are validated** before any
  allocation or indexing (`n`, `m`, `nnz` non-negative and bounded; every
  `row`/`col` triplet in range).  Malformed input is rejected cleanly instead
  of corrupting memory.
- **The objective sense is validated** (`max`/`minimize` only); malformed
  senses are rejected rather than silently defaulted to minimization.
- **Triplets are sorted in linear time** (counting sort), not O(nnz²), so a
  hostile ordering cannot cause a quadratic-time blowup.
- **Relation tokens are read into a bounded buffer** (no unbounded `%s`) and
  their characters validated.
- **Allocations are checked** (the LP solver and FlatZinc path use the
  `psolve_*` checked allocators), and error paths free partial state so a
  caller can safely `lp_free()`.
- **Dense QP dimensions are capped** (`MAX_QPDIM=8192`) so a hostile `n` cannot
  trigger a multi-gigabyte allocation.
- The **build is hardened**: stack canaries, `_FORTIFY_SOURCE=2`, format
  security warnings, PIE + full RELRO, non-executable stack.  `-ffast-math` is
  **off by default** (`make FAST_MATH=1` to opt in for a non-correctness
  benchmark build).

Run a memory-safety sweep with:

```sh
make asan                                   # AddressSanitizer + UBSan binaries
python3 tools/fuzz_inputs.py --iters 200 --seed 1   # fuzz malformed .lp/.qp
```

## Incremental solving

The C API supports warm-start re-solving after a problem changes:

```c
Solver *s = solver_create(&lp);
solver_solve(s);                      // initial solve
solver_set_objective(s, newc, 1);     // change objective -> re-solve fast
solver_warm_solve(s);
solver_set_bounds(s, newl, newu);     // change bounds -> re-solve fast
solver_warm_solve(s);
solver_add_row(s, arow, rhs, '<');    // add a constraint -> re-solve
solver_solve(s);
```

`set_objective` and `set_bounds` warm-start from the previous basis; `add_row`
does a clean re-solve.  All are verified against from-scratch solves.

### LP file format

```
maximize|minimize
<n> <m>
<c_1 ... c_n>            # objective coefficients
<b_1 ... b_m>            # right-hand sides
<rel[0..m-1]>            # one token of m concatenated relation chars, each '<' '>' '='
                        #   (e.g. <<=<< means <, <, =, <, <).  Use single chars
                        #   only: '<' already means "<=" and '>' means ">=".
<lo_1> <hi_1>            # per variable: bounds; '-inf inf' is fully free
...
<nnz>
<row> <col> <value>      # nnz sparse entries (0-indexed), any order
...
```

See `examples/` for ready-made problems.

## Examples

```sh
$ ./lpsolve examples/prodplan.lp --print
status: OPTIMAL
objective: 26
x[0] = 2
x[1] = 4
x[2] = 0
x[3] = 4
```

## Correctness & testing

```sh
python3 tools/sweep.py        # 100+ random bounded LPs vs GLPK (objective match)
python3 tools/difftest.py 200 0.4   # incl. infeasible/unbounded detection vs GLPK
```

The solver is verified against GLPK (`glpsol`) on hundreds of randomized
instances: it matches the objective on all well-conditioned problems and agrees
on infeasible/unbounded status in the vast majority of cases.

## Design

See [`docs/DESIGN.md`](docs/DESIGN.md) for the algorithm, the hardware
optimizations, how it compares with existing open-source solvers (and the
weaknesses it attacks), and an honest performance discussion.

## Using as a library

`make lib` produces **`libpsolve.a`** (LP + QP + MIP cores; no `main()`), and
`make liblp` / `make libqp` produce LP-only / QP-only archives.  Headers live in
`src/`:

```sh
make lib
cc -I psolve/src -o app app.c psolve/libpsolve.a -lm
```

The LP API (`src/solver.h`) takes a sparse-CSC matrix with per-variable bounds
and `<`/`>`/`=` rows, supports warm starts (`solver_warm_solve`) and
sensitivity analysis (`solver_duals`, `solver_reduced_costs`).  The QP API
(`src/qp.h`) solves convex `min ½xᵀQx + cᵀx s.t. Ax ≤ b` with a Phase-I
feasibility search (variable bounds are expressed as rows).  This is the
integration point used by [SmazkaVG](https://github.com/SodoMita/SmazkaVG).

## FlatZinc (Phase 3, linear subset)

`fznsolve <problem.fzn>` reads a MiniZinc-compiled FlatZinc file and solves the
linear plus selected exact finite-domain global-constraint subset natively with
the LP solver (and the MIP solver when integer variables are present, so answers
are integral):

- **Declarations**: `par`/`var` int/float/bool (including bare scalar
  parameters such as `int: n = 4;`), scalar + arrays with their original index
  ranges, annotations, and domains in both `var 1..10:` shorthand and
  `::`-annotation forms. Decimal shorthand domains infer `var float`; var-array
  aliases retain compiler-propagated literal elements such as `x = [1,3]`.
- **Constraints**: exact integer/bool `*_lin_eq/le/lt/ge/gt` and
  `*_eq/le/lt/ge/gt` relations; `int_eq/le/lt/ge/gt_reif` and their bool
  counterparts; `int_lin_ne`, `int_lin_ge/gt`, `count`/`among`; boolean logic
  (`bool_and/or/xor/not/clause`, `array_bool_and/or`); `int_plus/minus/neg`,
  `int_abs/max/min`, `int_times` (constant operand), `set_in` + set domains,
  `all_different`, `array_int_element`, `bool2int`/`int2float`; exact
  Gecode/standard integer `table` (one binary per allowed row, tight per-column
  big-M, up to 1,024 rows, empty table ⇒ UNSAT) and Hamiltonian `circuit`
  constraints (binary successor matrix + MTZ order rows, up to 64 nodes); plus
  the continuous linear float subset `float_lin_eq/le/ge/lt/gt`,
  `float_eq/le/ge/lt/gt`, `float_plus/minus/neg`, constant-operand
  `float_times`, constant-denominator `float_div`, and `bool_ge/gt` relational
  variants.  See `examples/fzn/float_lin.fzn` for a runnable continuous model,
  `examples/fzn/table.fzn` / `examples/fzn/table_{sat,opt,unsat}.fzn` for exact
  extensional tables, and `examples/fzn/circuit.fzn` for a Hamiltonian
  successor circuit.
- **Solve**: satisfy / minimize / maximize; type-faithful FlatZinc output
  (full-precision float values, declared array indices via `array1d(lo..hi,[..])`,
  status markers), objective always, and `-s` stats
  (`nodes`, `objectiveBound`); `-n` node limit and `-t`/SIGINT time limits are
  enforced via a cooperative abort polled in the LP simplex and B&B loops.
  Usage: `fznsolve [-n N] [-t ms] [-s] [-v] <x.fzn>`.
- **MiniZinc differential**: `tools/mzn_diff.py` compiles real `.mzn` models
  with the MiniZinc compiler and checks fznsolve against Gecode (objectives
  match, e.g. knapsack=10, prod3=57).  See `examples/mzn/`.
- Nonlinear/unhandled constraints return `=====UNKNOWN=====` (never a wrong
  answer).  In particular, strict continuous `float_lt`/`float_gt` predicates
  are deliberately not relaxed to non-strict LP rows: their feasible sets are
  open.  An optimization result that reaches the bridge's synthetic bound for
  an otherwise unbounded objective-bearing variable is likewise `UNKNOWN`, not
  a fake optimum.
  See `docs/ROADMAP.md` for the remaining handler list.

## Layout

```
src/kernels.c   AVX-512/AVX2/scalar dense kernels (daxpy, dot, sparse dot)
src/lu.c        dense LU factorization + forward/back substitution (BTRAN/FTRAN)
src/splu.c      sparse LU factorization (fill-reducing order + partial pivoting)
                with hyper-sparse triangular solves
src/err.c       error-handling protocol (checked allocation, setjmp/longjmp OOM)
src/solver.c    revised-simplex driver, two-phase method, steepest-edge pricing,
                sparse/dense dispatch, incremental (warm-start) solving,
                shadow prices + iteration limit
src/mip.c       mixed-integer programming via branch-and-bound
src/pgs.c       projected Gauss-Seidel boxed-QP (float reference kernel)
src/pgs_fixed.c fixed-point (integer) PGS boxed-QP (production physics kernel)
src/fzn.c       FlatZinc reader + LP solver bridge (Phase 3)
src/fx.c        fixed-point (exact rational) LP solver + fraction-aware reader
src/qp.c        convex QP solver (active-set method + Phase-I feasibility)
src/parser.c    LP file reader
src/main.c      LP CLI
tools/fxsolve.c fixed-point LP CLI
tools/fx_bench.c double-vs-fixed LP benchmark
tools/qpsolve.c QP CLI
tools/mipsolve.c MIP CLI
tools/          generators, differential tester, unit tests, benchmarks, fuzzer
examples/       sample LP files
```

## Large sparse LPs

This solver now ships a sparse LU (`src/splu.c`) that factorizes the basis in
its native sparse form and solves BTRAN/FTRAN with hyper-sparse triangular
solves.  Combined with steepest-edge (Goldfarb–Reid) pricing, it is roughly
**1.3–1.5× faster than GLPK on large sparse LPs** while matching GLPK's
objective.  See `docs/DESIGN.md` for the details, the automatic sparse/dense
dispatch, and the numerical-safety fallbacks that keep the result correct on
ill-conditioned bases.
