# Advanced Optimization Techniques for LP, QP, MIP, and Real-Time Physics Solvers

**Author:** psolve engineering & research team  
**Target Architectures:** High-Performance Linear, Mixed-Integer, Quadratic, and Fixed-Point Physics Solvers  
**Date:** 2026

---

## 1. Executive Overview

This document presents a research-backed survey of high-impact optimization techniques across modern mathematical programming and real-time physical simulation. It analyzes the mathematical foundations, algorithmic data structures, empirical speedup factors, and implementation trade-offs for:

1. **Linear Programming (LP):** Presolve, Dual Steepest Edge, Crash Bases, and Sparse LU updates.
2. **Mixed-Integer Programming (MIP):** Conflict Graphs, Cutting Planes, Primal Heuristics (Feasibility Pump, RINS), and Reliability Branching.
3. **Real-Time Physics & LCP Solvers:** Warm-starting, Block-Coupled PGS, Subspace Acceleration, and Island Parallelism.
4. **MiniZinc / FlatZinc Compiler Bridges:** Common Subexpression Elimination (CSE), SOS2 Special Ordered Sets, and Bound Consistency.

---

## 2. Linear Programming (LP) & Simplex Optimizations

```
+-----------------------------------------------------------------------------------+
|                              LP SIMPLEX PIPELINE                                  |
|                                                                                   |
|  [ Raw LP/MPS ] ---> [ High-Impact Presolve ] ---> [ Crash Basis (Maros-Mitra) ]  |
|                             |                                  |                  |
|                             v                                  v                  |
|                     [ Dual Postsolve ] <--- [ Dual Steepest-Edge Simplex (DSE) ]  |
|                                                     |                             |
|                                                     v                             |
|                                       [ Forrest-Tomlin Sparse LU Update ]         |
+-----------------------------------------------------------------------------------+
```

### 2.1 Presolve Engine (PaPILO / HiGHS Architecture)

Presolve reduces the constraint matrix dimensions before factorization. In industrial benchmarks, presolve eliminates **30% to 70% of rows and columns**, reducing solve times by **$2\times\text{ to }10\times$**:

1. **Singleton Column / Row Reductions:**
   - **Row Singleton ($a_i x_j = b_i$):** Instantly fixes $x_j = b_i / a_i$ and eliminates row $i$.
   - **Column Singleton with Zero Objective ($c_j = 0, x_j$ appears only in row $i$):** Variable $x_j$ can be treated as a slack/free variable to satisfy row $i$, eliminating both $x_j$ and row $i$.
2. **Doubleton Equality Substitution ($a x_1 + b x_2 = c$):**
   - Expresses $x_2 = (c - a x_1) / b$, substitutes $x_2$ in all other constraints, updates variable bounds, and eliminates row and column.
3. **Constraint Activity Bound Strengthening:**
   - For row $L_i \le \sum a_{ij} x_j \le U_i$, compute minimum and maximum activities:
     $$\underline{\alpha}_i = \sum_{j: a_{ij} > 0} a_{ij} l_j + \sum_{j: a_{ij} < 0} a_{ij} u_j, \quad \overline{\alpha}_i = \sum_{j: a_{ij} > 0} a_{ij} u_j + \sum_{j: a_{ij} < 0} a_{ij} l_j$$
   - If $\overline{\alpha}_i < L_i$ or $\underline{\alpha}_i > U_i$, model is **provably infeasible**.
   - If $\overline{\alpha}_i \le U_i$, the upper bound is **redundant** and can be removed.
   - For variable $k$ with $a_{ik} > 0$, deduce tighter upper bound:
     $$x_k \le l_k + \frac{U_i - \underline{\alpha}_i}{a_{ik}}$$
4. **Dual Postsolve Stack:**
   - Store all linear transformations on a LIFO stack to reconstruct original primal and dual ($y, s$) solutions with exact precision.

---

### 2.2 Dual Steepest-Edge (DSE) Pricing

Traditional Dantzig pricing selects the pivot with the most negative reduced cost $d_j = c_j - y^T A_j$, but requires many small steps. **Dual Steepest Edge (Goldfarb-Reid)** selects the entering variable along the direction of steepest Euclidean descent in dual space:

$$\gamma_i = \|B^{-1} e_i\|^2$$
$$\text{Pivot Row } p = \arg\max_{i} \frac{(b_i - x_i)^2}{\gamma_i}$$

- **Update Recurrence:** Instead of recomputing $\gamma_i$ ($O(m^2)$), update $\gamma$ dynamically using the FTRAN/BTRAN vectors in $O(m)$ work.
- **Performance Impact:** Cuts total simplex pivot count by **$2\times\text{ to }5\times$** compared to Dantzig and **$1.3\times\text{ to }1.8\times$** compared to Devex.

---

### 2.3 Crash Basis Generation (Maros-Mitra / Bixby)

Starting the revised simplex from an all-slack basis ($B = I$) requires hundreds of Phase-I iterations to reach primal/dual feasibility.
- **Maros-Mitra Heuristic:** Inspects structural columns, scoring them by bound tightness (free variables $>$ single-bounded $>$ boxed $>$ fixed).
- Selects an upper-triangular submatrix of structural columns to replace slack columns in $B_0$.
- **Impact:** Reduces Phase-I iterations by **20% to 40%**.

---

### 2.4 Sparse LU Updating (Forrest-Tomlin / Suhl-Suhl)

Refactorizing the basis matrix $B$ from scratch at every pivot takes $O(m^3)$ or $O(m \cdot \text{nnz}(B))$.
- **Forrest-Tomlin Update:** When column $p$ is replaced by $a_q$, the transformed column creates a spike in $U$. Forrest-Tomlin applies row permutations and elimination to restore upper-triangular form in $O(\text{nnz}(\text{spike}))$ time.
- **Refactorization Threshold:** Refactorize from scratch only every $50\text{ to }100$ pivots or when numerical condition number degrades ($\kappa(B) > 10^{10}$).

---

## 3. Mixed-Integer Programming (MIP) & Branch-and-Cut

```
+-----------------------------------------------------------------------------------+
|                              MIP BRANCH-AND-CUT                                   |
|                                                                                   |
|  [ Presolved MIP ] ---> [ Root LP Relaxation ] ---> [ Conflict Graph / Cliques ]  |
|                                   |                              |                |
|                                   v                              v                |
|                    [ Primal Heuristics (RINS/FP) ] <--- [ Cut Generators (GMI) ]  |
|                                   |                                               |
|                                   v                                               |
|                    [ Reliability Branching Tree ]                                 |
+-----------------------------------------------------------------------------------+
```

### 3.1 Conflict Graphs & Clique Merging

A **Conflict Graph $G = (V, E)$** represents mutual exclusivity between binary literals ($x_j$ and $\bar{x}_j = 1 - x_j$):
- An edge $(u, v) \in E$ indicates literal $u$ and literal $v$ cannot both be 1 ($u + v \le 1$).
- **Clique Separation:** Any clique $C \subseteq V$ defines a valid inequality:
  $$\sum_{j \in C} x_j \le 1$$
- **Clique Merging:** Extends set-packing rows by greedily adding conflicting variables detected from other constraints, drastically tightening the root relaxation.

---

### 3.2 Cutting Planes (Valid Inequalities)

1. **Gomory Mixed-Integer (GMI) Cuts:**
   - Derived directly from tableau rows of fractional basic variables $x_i = \bar{b}_i - \sum \bar{a}_{ij} x_j$:
     $$\sum_{j \in J: f_j \le f_0} \frac{f_j}{f_0} x_j + \sum_{j \in J: f_j > f_0} \frac{1 - f_j}{1 - f_0} x_j \ge 1, \quad \text{where } f_0 = \bar{b}_i - \lfloor \bar{b}_i \rfloor$$
2. **Knapsack Cover Cuts with Lifting:**
   - For knapsack constraint $\sum a_j x_j \le b$, find minimal cover $C \subseteq N$ such that $\sum_{j \in C} a_j > b$.
   - Yields valid inequality $\sum_{j \in C} x_j \le |C| - 1$, lifted into non-cover variables.
3. **Impact:** Root-node cutting planes close **$20\%\text{ to }50\%$ of the optimality gap** before branching begins.

---

### 3.3 Primal Feasibility Heuristics

1. **Feasibility Pump (Fischetti, Glover, Lodi):**
   - Alternates between LP projection (rounding $x^*$ to nearest integer $\tilde{x}$) and LP feasibility (minimizing $\|x - \tilde{x}\|_1$).
   - Finds initial feasible integer solutions in **$< 10$ iterations** on $90\%$ of industrial MIPs.
2. **Relaxation Induced Neighborhood Search (RINS):**
   - Fixes all variables where the continuous relaxation $x_{LP}$ agrees with the incumbent $x_{INC}$ ($x_j^{LP} = x_j^{INC}$).
   - Solves a small sub-MIP on the remaining fractional variables with a tight node limit (e.g. 500 nodes).
3. **Proximity Search:**
   - Adds an objective penalty minimizing the $L_1$ distance to the current best solution: $\min c^T x + \theta \sum |x_j - x_j^0|$.

---

### 3.4 Reliability Branching (Pseudo-Costs + Strong Branching)

- **Strong Branching:** Tentatively pivots on candidate fractional variables to evaluate dual bound improvement $\Delta z^-$ and $\Delta z^+$. Highly accurate but computationally expensive.
- **Pseudo-Cost Branching:** Tracks historical objective gains per unit change:
  $$\Psi_j^- = \frac{\sum \Delta z_j^-}{\sum (x_j^* - \lfloor x_j^* \rfloor)}, \quad \Psi_j^+ = \frac{\sum \Delta z_j^+}{\sum (\lceil x_j^* \rceil - x_j^*)}$$
- **Reliability Branching:** Uses strong branching for variable $j$ only until it has been branched on $\eta_{\text{rel}} \approx 8$ times, then switches to fast pseudo-cost estimates.

---

## 4. Real-Time Physics & Fixed-Point LCP Solvers

```
+-----------------------------------------------------------------------------------+
|                        REAL-TIME 2D/3D PHYSICS SOLVER                             |
|                                                                                   |
|  [ Contact Graph ] ---> [ Island Decomposition ] ---> [ Warm-Start Impulse Cache] |
|                                                              |                    |
|                                                              v                    |
|                [ Subspace Acceleration ] <--- [ Fixed-Point Block PGS (Q16.16) ]  |
+-----------------------------------------------------------------------------------+
```

### 4.1 Temporal Coherence & Warm Starting

In game engines and physical simulations, contact manifolds evolve smoothly between frames ($t \to t + \Delta t$).
- **Impulse Caching:** Cache accumulated normal and friction impulses $\lambda_N, \lambda_T$ keyed by contact feature ID.
- Initialize the PGS iterate with previous impulses: $\lambda^{(0)} = \alpha \lambda_{\text{prev}}$ ($\alpha \approx 0.85\text{--}0.95$).
- **Convergence Impact:** Speeds up convergence by **$2\times\text{ to }5\times$**, eliminating stacking jitter in Box2D/Bullet.

---

### 4.2 Block-Coupled PGS (2x2 Normal + Friction Solvers)

Standard 1D Projected Gauss-Seidel decouples normal impulse $\lambda_N$ and tangential friction $\lambda_T$, causing oscillation at friction cones.
- **Coupled Block PGS:** Solves the coupled $2\times 2$ or $3\times 3$ contact block analytically:
  $$\begin{bmatrix} \lambda_N \\ \lambda_T \end{bmatrix}^{k+1} = \text{ProjectCone}\left( \begin{bmatrix} J M^{-1} J^T \end{bmatrix}^{-1} \begin{bmatrix} v_N \\ v_T \end{bmatrix} \right)$$
- Eliminates slip-stick chatter on steep contact angles and heavy stacks.

---

### 4.3 Subspace Acceleration (Dual-Space Conjugate Residuals)

Interleaving Projected Gauss-Seidel with unconstrained subspace conjugate gradient steps (Baraff / Moreau-Jean):
1. Run $k_1$ iterations of PGS to identify active contact bounds ($x_i = lo_i$ or $x_i = hi_i$).
2. Lock active bounds and perform 2–3 iterations of **Conjugate Residual (CR)** on the unconstrained submatrix.
3. Propagates contact waves through long articulated chains in $O(1)$ iterations instead of $O(N)$ PGS sweeps.

---

### 4.4 Contact Island Partitioning & AVX-512 Vectorization

- **Island Decomposition:** Partition the global contact graph into disjoint connected components (islands).
- **SIMD / Multi-Threading:** Process independent islands concurrently using AVX-512 16-bit/32-bit SIMD fixed-point kernels (`pgsf_batch_solve`).

---

## 5. MiniZinc / FlatZinc Compiler & Bridge Optimizations

```
+-----------------------------------------------------------------------------------+
|                        FLATZINC COMPILER OPTIMIZATIONS                            |
|                                                                                   |
|  [ .fzn Model ] ---> [ Common Subexpression Elimination ] ---> [ Bound Pruning ]  |
|                                                                      |            |
|                                                                      v            |
|                     [ Native SOS2 Linearization ] <--- [ Special Ordered Sets ]   |
+-----------------------------------------------------------------------------------+
```

### 5.1 Common Subexpression Elimination (CSE)

MiniZinc decomposition often creates duplicate intermediate variables (e.g. multiple `int_times(x, x, _1)` or identical reifications `int_eq_reif(a, b, r1)` and `int_eq_reif(a, b, r2)`).
- **Hash-Consing Table:** Hash all constraints by `(predicate, sorted_arguments)`.
- If an identical constraint exists, alias $r_2 \equiv r_1$ and discard the redundant row.
- Reduces FlatZinc variable count by **$15\%\text{ to }30\%$** on complex constraint models.

---

### 5.2 Direct SOS1 / SOS2 Detection

Instead of translating `piecewise_linear`, `table`, or `element` into large systems of big-M inequality rows, detect Special Ordered Sets:
- **SOS1:** At most one variable in the set can be non-zero ($\sum \lambda_i = 1$).
- **SOS2:** At most two adjacent variables in the set can be non-zero (exact piecewise linear functions).
- Native SOS2 branching splits the interval $[0, k]$ vs $[k+1, N]$ directly without binary slack variables, cutting B&B node counts by **$5\times\text{ to }20\times$**.

---

### 5.3 Domain Consistency & Pre-Tableau Pruning

Before allocating the simplex tableau in `fz_solve`:
- Run a 2-pass forward/backward bound consistency loop.
- Propagate bounds through linear rows ($x + y = z$) to tighten unbounded $\pm 10^9$ variables to finite boxes, eliminating artificial `UNKNOWN` downgrades.

---

## 6. Implementation Priority & Roadmap

| Optimization Technique | Component | Expected Speedup | Implementation Complexity | Priority |
| :--- | :--- | :---: | :---: | :---: |
| **Pritsker 0-1 Cumulative Formulation** | `src/fzn.c` | **$30,000\times$** on scheduling | Low | Completed |
| **Directional Half-Reification (`*_imp`)** | `src/fzn.c` | Enables 20+ global constraints | Low | Completed |
| **Common Subexpression Elimination (CSE)** | `src/fzn.c` | $1.2\times\text{–}1.5\times$ memory/time | Medium | High (Next) |
| **Basic Presolve (Singletons + Row Bounds)** | `src/solver.c` | $1.5\times\text{–}3.0\times$ on sparse LPs | Medium | High (Next) |
| **Dual Steepest Edge (DSE) Pricing** | `src/solver.c` | $2\times\text{–}4\times$ pivot reduction | Medium | High |
| **Feasibility Pump Primal Heuristic** | `src/mip.c` | $5\times\text{–}10\times$ faster first incumbent | Medium | High |
| **Gomory Mixed-Integer (GMI) Cuts** | `src/mip.c` | $2\times\text{–}5\times$ B&B node reduction | High | Medium |
| **Temporal Warm-Start Caching** | `src/pgs.c`, `pgs_fixed.c`| $2\times\text{–}4\times$ on physics frames | Low | High |
| **Subspace Acceleration for PGS** | `src/pgs_fixed.c` | $3\times\text{–}10\times$ on deep stacks | Medium | Medium |
