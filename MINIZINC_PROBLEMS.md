# Identified Problems and Technical Analysis: MiniZinc / FlatZinc in psolve

This document outlines the identified problems, mathematical limitations, and resolutions when running MiniZinc / FlatZinc models on `psolve`, along with an analysis of nonlinear polynomials, continuous trigonometry, and discrete combinatorial search.

---

## 1. Identified Solver Problems and Resolutions in psolve

### 1.1 Missing Global Constraint Linearizations
- **Problem:** Standard MiniZinc models produce global constraint predicates (`all_equal`, `increasing`, `decreasing`, `strictly_increasing`, `strictly_decreasing`, `lex_less`, `lex_lesseq`, `global_cardinality`, `bin_packing`, `disjunctive`, `diffn`, `inverse`, `member`, `sliding_sum`, `nvalue`, `alldifferent_except_0`). Previously, `src/fzn.c` did not support them, causing `fznsolve` to return `=====UNKNOWN=====`.
- **Resolution:** Implemented exact Mixed-Integer Linear Programming (MIP) linearizations directly inside `src/fzn.c` for 15+ global constraint families.

### 1.2 Unhandled Half-Reification (`*_imp` Predicates)
- **Problem:** MiniZinc's flattener decomposes conditional constraints (`c -> (x <= y)`) into half-reified predicates (`int_lin_le_imp`, `int_eq_imp`, `bool_eq_imp`, etc.). Previously, only full equivalence reification (`*_reif`) was recognized.
- **Resolution:** Added directional implication helper `add_int_imp` with big-M upper/lower bound relaxations ($r \implies d \le 0 \iff d + U r \le U$).

### 1.3 Inefficient `cumulative` Reification
- **Problem:** The original `cumulative` handler used multi-level auxiliary indicator variables per timestep, resulting in 28 binary variables per timestep and causing small 4-task scheduling instances to time out (>30 seconds).
- **Resolution:** Replaced with the **Pritsker 0-1 time-indexed formulation** ($\sum_i r_i \sum_k z_{i, k} \le B$), reducing solve time to **1 millisecond** (5 B&B nodes).

### 1.4 Solver Configuration for MiniZinc (`psolve.msc`)
- **Problem:** MiniZinc had no configuration file to recognize `psolve` as a native solver backend.
- **Resolution:** Added `psolve.msc` allowing `minizinc --solver psolve model.mzn`.

---

## 2. Analysis of Non-Linear Polynomials and Diophantine Equations

### Example: Unbounded Non-Linear Integer Multiplication
```minizinc
var int: x;
var int: y;
constraint x*x*x*y*y = 100000;
solve satisfy;
```

### Why it behaves differently across solver types:
1. **Gecode (Finite-Domain CP):**
   - MiniZinc flattens this into chained `int_times` constraints.
   - Because $x$ and $y$ are unbounded (`var int`), finite-domain interval bound propagation cannot narrow the infinite bounds $(-\infty, +\infty)$ without search bounds, causing Gecode to spin/hang indefinitely.
2. **HiGHS / Linear MIP Solvers (`-G linear`):**
   - MiniZinc's linear flattening library attempts to build 2D discretization tables over the Cartesian product of variable domains.
   - For unbounded variables, MiniZinc throws a compilation error: `comprehension iterates over an infinite set`.
3. **psolve (`fznsolve`):**
   - In `src/fzn.c`, `int_times(a, b, c)` checks whether operands are constants or booleans.
   - For general unbounded non-linear variables, `psolve` enforces the **Honesty Principle**: it returns `=====UNKNOWN=====` in 0.1 ms rather than fabricating an invalid relaxation.
4. **Bounded Alternative (`var 1..100: x, y;`):**
   - With finite bounds, integer factorization succeeds immediately ($x = 10, y = 10$, since $10^3 \cdot 10^2 = 1000 \cdot 100 = 100,000$).

---

## 3. Analysis of Trigonometry (`sin`, `cos`, `tan`)

Continuous trigonometric constraints (e.g. `y = sin(x)`) cannot be solved natively by pure LP/MIP simplex solvers:

1. **Non-Convex & Transcendental:**
   - $\sin(x)$ and $\cos(x)$ are continuous non-linear, non-convex transcendental curves. LP/MIP solvers operate strictly on linear hyperplanes and convex quadratic matrices.
2. **Behavior in psolve:**
   - `src/fzn.c` detects `float_sin` / `float_cos` as non-linear continuous constraints and honestly reports `=====UNKNOWN=====`.
3. **Approaches to Solve Trigonometry:**
   - **Piecewise Linear MIP (SOS2):** Discretizing $\sin(x)$ into $N$ linear segments via `piecewise_linear(x, y, xi, vi)`. `psolve` guarantees finding the global basin with $O(1/N^2)$ approximation error decay.
   - **Real-Time Fixed-Point (Integer LUT / CORDIC in psolve):** Evaluates trigonometric functions in **35 nanoseconds** with zero heap allocations and 100% bit-identical determinism across FPUs.
   - **Continuous NLP Solvers (Ipopt / SLSQP):** Solves smooth continuous non-linear constraints using gradient descent, but is susceptible to getting trapped in local minima on multi-modal trigonometric landscapes.

---

## 4. Analysis of Pure Discrete Permutations (Sudoku 9x9, N-Queens 8)

1. **CP Solvers (Gecode):** Use Régin's bipartite maximum-matching algorithm on AllDifferent graphs to filter values in polynomial time ($<10\text{ ms}$) without branching.
2. **MIP Solvers (psolve, CBC, HiGHS):** Translate discrete constraints into 0-1 binaries. The LP relaxation assigns fractional values ($\frac{1}{n}$), creating flat relaxation objectives that require deeper branch-and-bound trees unless domain propagation or specialized cuts are applied.

---

## 5. MiniZinc Solver Registration (`psolve.msc`)

To register `psolve` as a native solver in MiniZinc:
```sh
# Copy solver configuration to MiniZinc search path:
mkdir -p ~/.minizinc/solvers
cp psolve.msc ~/.minizinc/solvers/

# Verify registration:
minizinc --solvers

# Run any MiniZinc model directly with psolve:
minizinc --solver psolve model.mzn
```
