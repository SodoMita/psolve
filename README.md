# AVX512-Simplex — a vectorized revised simplex LP solver in C

A from-scratch linear-programming solver built around the **revised simplex
method**, tuned for modern x86 hardware (AVX-512 FMA), cache-friendly sparse
data layout, and hyper-sparsity-aware pricing.

It reads a simple text LP format, solves it, and prints the optimum.  It also
ships a GLPK-based differential tester and a benchmark harness so results can
be cross-checked and timed against a reference solver.

## Building

```sh
make            # produces ./lpsolve (requires GCC, x86-64 with AVX-512)
make clean
```

The Makefile uses `-march=native -mavx512f -mfma`. On a CPU without AVX-512 the
code falls back to AVX2/SSE2/scalar automatically (see `src/kernels.c`).

## Usage

```sh
./lpsolve <problem.lp> [--print]
```

`--print` also dumps the optimal variable values.

The solver handles: **maximize or minimize**, `<`, `>`, and `=` constraints,
variables with arbitrary finite/infinite bounds (lower, upper, or boxed), and
correctly reports `OPTIMAL`, `INFEASIBLE`, or `UNBOUNDED`.

### LP file format

```
maximize|minimize
<n> <m>
<c_1 ... c_n>            # objective coefficients
<b_1 ... b_m>            # right-hand sides
<rel[0..m-1]>            # one token, chars '<' '>' '='  (e.g. <<=<<)
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
                sparse/dense basis dispatch + fallback
src/parser.c    LP file reader
src/main.c      CLI
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
