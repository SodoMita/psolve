# AVX512-Simplex — a vectorized LP & QP solver in C

A from-scratch **linear programming** (revised simplex) and **quadratic
programming** (active-set) solver, tuned for modern x86 hardware (AVX-512 FMA),
cache-friendly sparse data layout, and hyper-sparsity-aware pricing. It also
supports **incremental solving** (warm starts) so you can re-solve a perturbed
problem without starting over.

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
./lpsolve <problem.lp> [--print]      # LP (revised simplex)
./qpsolve <qp.qp>                      # convex QP (active-set)
```

`--print` also dumps the optimal variable values.

The LP solver handles: **maximize or minimize**, `<`, `>`, and `=` constraints,
variables with arbitrary finite/infinite bounds (lower, upper, or boxed), and
correctly reports `OPTIMAL`, `INFEASIBLE`, or `UNBOUNDED`.

The QP solver handles: **minimize** ½xᵀQx + cᵀx subject to Ax ≤ b with Q
symmetric positive semi-definite (convex), reporting the optimum, Lagrange
multipliers, and status (solved / infeasible).

## Security & untrusted input

`lpsolve` and `qpsolve` parse files supplied on the command line, which may be
attacker-controlled.  The parsers are hardened accordingly:

- **Dimensions, counts, and matrix indices are validated** before any
  allocation or indexing (`n`, `m`, `nnz` non-negative and bounded; every
  `row`/`col` triplet in range).  Malformed input is rejected cleanly instead
  of corrupting memory.
- **Relation tokens are read into a bounded buffer** (no unbounded `%s`) and
  their characters validated.
- **Allocations are checked**, and error paths free partial state so a caller
  can safely `lp_free()`.
- **Dense QP dimensions are capped** (`MAX_QPDIM=8192`) so a hostile `n` cannot
  trigger a multi-gigabyte allocation.
- The **build is hardened**: stack canaries, `_FORTIFY_SOURCE=2`, format
  security warnings, PIE + full RELRO, non-executable stack.

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
<rel[0..m-1]>            # one token of concatenated relations, each '<' '>' '=' '<='
                        #   or '>='  (e.g. <<=<< means <, <, =, <, <;  <=<=< is <=, <=, <)
<lo_1> <hi_1>            # per variable: bounds, 'inf' for unbounded
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

## Layout

```
src/kernels.c   AVX-512/AVX2/scalar dense kernels (daxpy, dot, sparse dot)
src/lu.c        dense LU factorization + forward/back substitution (BTRAN/FTRAN)
src/splu.c      sparse LU factorization (fill-reducing order + partial pivoting)
                with hyper-sparse triangular solves
src/solver.c    revised-simplex driver, two-phase method, steepest-edge pricing,
                sparse/dense dispatch, incremental (warm-start) solving
src/qp.c        convex QP solver (active-set method + Phase-I feasibility)
src/parser.c    LP file reader
src/main.c      LP CLI
tools/qpsolve.c QP CLI
tools/          generators, differential tester, unit tests, benchmarks
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
