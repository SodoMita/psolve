# psolve — long-term roadmap
**Goal:** a small, dependency-free, real-time solver library for interactive UI,
vector graphics, and 2D physics, that also stays feature-complete enough to
test against FlatZinc/MiniZinc.

**Core approach: fixed-point (integer) math.** psolve is an *integer* solver:
the real-time kernels (UI, VG, physics) run on fixed-point arithmetic rather
than IEEE floats. This is deliberate, for both **reliability** and
**performance**:

- **Determinism / replay safety.** Fixed point is bit-identical across
  platforms, compilers, and FPU configurations. No rounding variance, no
  NaN/Inf edge cases from float division. This is essential for networked and
  demo-replay physics.
- **Predictable cost.** Integer multiply-add and exact division are cheap and
  branch-predictable on all cores, including integer-only / embedded targets.
- **Exact comparisons & clamping.** Boxes, tolerance checks, and tie-breaking
  are exact integer comparisons, so two machines never disagree on which
  constraint binds.

Floating-point variants of each kernel are provided for prototyping and as a
cross-check (the fixed-point solver is validated against them), but the
production path is integer.

The primary target is **interactive latency**: single-digit microsecond solves
for the tiny dense systems that appear per-contact and per-layout in a game or
editor frame, executed deterministically and warm-started across frames. The
LP/QP/MIP solvers already built are the correctness backbone; the physics path
is a different algorithm family tuned for that latency.

---

## Guiding principles
1. **Zero hard dependencies.** The whole library must link with only libc/libm
   (as it does today). No Boost, no Eigen, no shared memory pools from third
   parties. This is the core differentiator the user asked for.
2. **Determinism.** Real-time physics needs reproducible results at a fixed
   timestep, across machines. All solvers should be bit-deterministic given the
   same input and iteration counts.
3. **Warm-start friendly.** Physics re-solves the *same* constraint graph each
   frame with small changes. Incremental/warm-start solving is a first-class
   feature, not an afterthought.
4. **Bounded work.** Every solver must respect iteration/time/node budgets and
   return `UNKNOWN`/partial gracefully — never spin forever (UI/input threads
   cannot block).
5. **Small, auditable, tested.** Keep LOC low; every module gets unit + fuzz +
   differential tests.

---

## Phase 0 — Done
- AVX-512 revised-simplex LP (sparse LU + steepest edge), ~1.4× GLPK on sparse LPs
- Convex QP (active-set), verified vs scipy
- MIP via branch-and-bound, verified vs brute force
- Incremental (warm-start) LP solving
- Hardened parsers + fuzzing + sanitizers (untrusted-input safe)
- OOM/error protocol, sensitivity (duals), iteration limits

### Correctness hardening pass (audit-driven)
- **No false OPTIMAL/INFEASIBLE/UNBOUNDED.** `solver_solve` now issues a
  **solution certificate**: after a claimed optimum, it re-checks the variable
  bounds and constraint rows (and that the final basis factorized).  A diverged
  or numerically broken solve reports `NUMERICAL_FAILURE` instead of a wrong
  answer.  The MIP, QP, and CLIs propagate honest statuses.
- **MIP status correctness**: a node/time-limited search that found an incumbent
  now reports status `5` (feasible, optimality NOT proven) instead of falsely
  `OPTIMAL`.  Stopped searches and numerical failures are distinct statuses.
- **QP status correctness**: the active-set solver no longer unconditionally sets
  `status=0`; KKT stationarity is verified before success, KKT failures and the
  iteration cap are reported as `QP_KKT_FAIL` / `QP_ITERATION_LIMIT`.
- **Phase I iteration limit** is reported as `ITERATION_LIMIT`, not falsely
  `INFEASIBLE`.
- **Singular-basis fix**: `remove_basic_artificials` could swap in a candidate
  that made the basis SINGULAR on rank-deficient constraint sets (e.g. big-M
  reification encodings), which then produced wrong or divergent answers.  Now a
  basic artificial at zero (a redundant row) is left basic but *pinned at 0*, so
  it preserves rank without absorbing infeasibility.
- **Parser**: objective sense is validated (rejects `garbage`); the O(nnz²)
  insertion sort for triplets was replaced with a linear counting sort.
- **Allocation discipline**: added `psolve_realloc` / `psolve_calloc` /
  `psolve_strdup` checked helpers; the LP eta-growth and the FlatZinc
  Builder/LP construction now use them (no unchecked `realloc`).
- **`-ffast-math` is off by default** (`make FAST_MATH=1` to opt in): the default
  correctness build no longer reorders FP ops or changes NaN/Inf behaviour.
- **Differential harness fixed**: the GLPK status classifier now recognizes all
  glpsol phrasings ("LP HAS UNBOUNDED PRIMAL SOLUTION", "PROBLEM HAS NO PRIMAL
  FEASIBLE SOLUTION", ...), so the GLPK differential actually measures agreement.
  Differential results after hardening: 0 objective mismatches, 0 infeasible-as-
  optimal, GLPK canonical sweep 119/119, equality/fixed vs scipy 300/300, QP vs
  scipy 40/40.

### Known Phase I limitation
On some genuinely **infeasible** degenerate LPs the Phase-I simplex can iterate
to the iteration limit without converging to a certified verdict; it then
reports `ITERATION_LIMIT` (honest) rather than `INFEASIBLE`.  This is a
convergence weakness in the phase-I pricing, not a wrong-answer path.

---

## Phase 1 — Real-time physics kernel (PRIORITY for primary use case)
The heart of every 2D physics engine (Box2D-style) is not a general simplex but
a **projected Gauss-Seidel / sequential-impulse** solver on boxed
complementarity constraints. Small (m ≤ ~256), dense, warm-started, iterated a
fixed number of times per substep.  **All of this runs in fixed point.**

> Note: the physics-engine-specific layer (contact-point gathering, integration,
> collision broad/narrow phase, VG path geometry) belongs in *other* projects
> that consume this library.  psolve's job here is the **solver foundation** —
> kernels that are deterministic, zero-malloc, warm-start friendly, and fast.

- [x] `src/pgs.c`: floating-point projected Gauss-Seidel boxed-QP solver
      `min ½xᵀAx + bᵀx  s.t.  lo ≤ x ≤ hi` (reference / cross-check).
- [x] `src/pgs_fixed.c`: **fixed-point (integer)** PGS boxed-QP solver,
      all-integer with 128-bit accumulation, round-half-away division, rational
      SOR, bit-identical determinism.  Validated against the float reference.
- [x] **PGS-vs-LP benchmark** (`make pgs_vs_lp`): shows PGS-fixed is ~60–600×
      faster than the general LP simplex and ~10–30× faster than exact
      active-set QP on identical warm-started physics boxed problems — i.e. PGS
      is the correct foundation for a per-frame hot loop; the LP/QP solvers are
      the correctness backbone for everything else.
- [x] **Zero-malloc**: both PGS kernels already use only caller-provided arrays
      and stack (alloca) — no heap in the solve loop.  (The arena requirement
      is satisfied for the foundation; the host app supplies buffers.)
- [ ] *(moved to other projects)* Contact-block builder, broad/narrow-phase
      collision, fixed-point integration, VG path geometry.
- [ ] *(future)* Fixed-point LP/QP kernels (integer revised-simplex) for UI
      layout / VG, when a host project needs an integer general solver.

### Phase 1 complete (foundation). Next: Phase 3 FlatZinc, Phase 2 VG/UI kernels.

---

## Phase 2 — Vector-graphics / UI geometry
Interactive vector graphics and UI layout generate many tiny, independent
convex problems. Two specialized kernels, **all in fixed point**:
- [ ] **Smallest-enclosing / containment QPs** for glyph and shape fitting
      (fixed-point PGS or integer active-set reuse).
- [ ] **Layout solver**: boxed linear constraints (min/max sizes, spacing,
      alignment) as a tiny fixed-point LP/QP solved in microseconds per frame.
- [ ] **Line-of-sight / ray / swept-collision** helpers: integer linear-algebra
      primitives usable without full LP setup.

---

## Phase 3 — FlatZinc / MiniZinc completeness (feature-completeness testing)
Implement per `docs/psolve_todo_fzn.md` — a FlatZinc reader + redefinitions so
the full MiniZinc suite can be run against psolve. This validates LP/QP/MIP
feature completeness against a huge standard corpus.

### Status (started this session — linear subset working)
- [x] **Lexer**: identifiers, ints, floats, strings, keywords, symbols,
      comments; handles `1..10` ranges (decimal-point fix).
- [x] **Parser**: predicate decls (skipped), `par`/`var` scalar + array decls,
      name→index map, array index ranges, annotations (`::output_var`,
      `::output_array`, `:: domain`), solve item (satisfy/minimize/maximize).
- [x] **Domain forms**: both `var int: x :: 1..10` (annotation) and
      `var 1..10: x` (shorthand) are read into variable bounds.
- [x] **Constraint dispatch**: `int_lin_eq/le` (native), `int_eq/le/lt/ge/gt`,
      `bool_eq/le/lt`, `bool_not`, `int_plus/minus` — linear subset mapped to
      the LP solver.
- [x] **Bridge**: big-M clamping of unbounded `var int`, LP solve, FlatZinc
      output (`x = v;`, `array1d(...)`, status markers) + `%%%mzn-stat`.
- [x] **Par-array declarations** (`array[1..n] of int: C = [...]`) incl. the
      bare-`array` form MiniZinc emits (no `par` keyword).
- [x] **Var-array aliasing**: `array[..] of var int: take = [X0,X1,...]` maps
      `take[e]` to the underlying vars so output arrays print correctly.
- [x] **bool vars** bounded to [0,1]; array output uses `array1d(lo..hi,[..])`.
- [x] More handlers: `bool_and/or/xor/not/clause(+reif)`, `array_bool_and/or`,
      `int_neg` (fixed an `array_bool_and` constraint-sign bug).
- [x] More handlers: `int_abs`/`int_max`/`int_min` (big-M with binary flags),
      `int_times`/`float_times` (constant-operand linearization),
      `set_in` + set-domain decls (SOS1), `bool2int`/`int2float`,
      `all_different` (exact domain/permutation encoding),
      `array_int_element` + `gecode_int_element` (SOS1 element lookup).
- [x] Reified forms: `int_eq_reif`, `int_le_reif`, `int_lt_reif`,
      `int_ge_reif`, `int_gt_reif`, `bool_eq_reif`, `bool_le_reif` (big-M with a
      binary, incl. constant reification values); `int_lin_ne`/`bool_lin_ne`
      (SOS1).  Fixed big-M sign bugs in the != / ne encodings.
- [x] `int_lin_ge`/`int_lin_gt`/`bool_lin_ge`; `fzn_count_eq`/`fzn_among_eq`
      (exact via per-variable `[x_i==v]` binary + a side-selector binary for the
      `x_i!=v` OR; fixed an AND-vs-OR encoding bug).
- [x] `table` (tuple of vars must equal one of a list of rows): exact MIP
      encoding with one binary selector per row (`sum b_j = 1` plus big-M
      `x_i = T[j][i]` implications).  The big-M per variable is taken over the
      actual table column values (`max(maxCol-lo, hi-minCol)`), not the domain
      span, so rows whose values fall outside a variable's domain are safely
      excluded.  An empty table is reported UNSATISFIABLE.  Validated against a
      Python brute-force enumerator (`tools/table_verify.py`, 1500+ random
      instances, 0 wrong answers) and fuzzed under ASan/UBSan.
- [x] More relational variants: `float_lt/le/ge/gt`, `bool_ge/gt`.
- [ ] More handlers: `cumulative`, `circuit`.
- [x] **MIP bridge**: when the model has integer vars, `fz_solve` dispatches to
      the MIP branch-and-bound so answers are integral (objectives match brute
      force, e.g. knapsack=10, prod3=57).
- [x] MIP CLI bridge: `-n` (node limit) is wired through `FZSolution.node_limit`
      into the MIP `node_limit`; `-t` (wall-clock limit) and Ctrl-C trigger a
      **cooperative abort** — `psolve_stop()` is polled in the LP simplex loop
      (every 256 iterations) *and* once per B&B node, so a long run inside a
      single relaxation can be interrupted, not just between nodes.  Status 4
      (`stopped`) is returned and printed as `=====UNKNOWN=====` with stats.
- [x] `solve satisfy` fast path: `stop_at_feasible` returns the first
      integer-feasible solution instead of proving optimality; an LP-rounding
      feasibility heuristic produces incumbents at near-lattice nodes.
- [x] CLI flags: `-n` (node limit), `-t` (time limit via alarm), `-s` (stats),
      `-v` (verbose), SIGINT/SIGALRM handlers; `fznsolve` prints objective
      always and `objectiveBound`/`nodes` in stats.
- [ ] CLI: `-a` (all solutions), `-f` (free search).
- [x] **MiniZinc differential** (`tools/mzn_diff.py`): compiles real `.mzn`
      models with the MiniZinc compiler and compares fznsolve vs Gecode
      (objectives + feasibility).  Models in `examples/mzn/`.
- [x] Fuzz the FlatZinc parser (ASan/UBSan), incl. new handlers + MIP path.
      Added a dedicated `tools/fuzz_fzn.py` (well-formed + malformed `.fzn`
      generators) and wired it into `test.sh`; several real leaks in the lexer
      (token text) and the expression parser (unary-minus/paren/array-element
      error paths) found and fixed.
- [ ] More MiniZinc-suite coverage; HiGHS round-trip.

### Known Phase 3 limitation
Pure LP-based B&B has weak relaxations on *combinatorial* feasibility models
that combine gapped `set` domains, `all_different`, and nonlinear (`!=`)
constraints (big-M encodings).  Such models solve correctly when they complete
(never a wrong answer — `UNKNOWN` is returned), but B&B may not reach a
solution quickly.  Real-time CSPs are better served by a propagation-based
engine in the consuming project; psolve's LP/QP/MIP backbone targets
linear/quadratic and well-structured MIP models.

### Phase 3 first-cut deliverable: `fznsolve <x.fzn>` solves the linear subset,
with tests in `examples/fzn/` and `test.sh` (incl. the MiniZinc differential).

### Fixed: LP Phase I degeneracy
The revised-simplex Phase I previously mis-declared INFEASIBLE on degenerate
problems combining an equality constraint with a variable fixed to a value.
Root cause: only *basic* artificials were given the Phase I objective cost,
so the reduced costs were wrong and Phase I stalled with a basic artificial at
a positive value even though a feasible point existed.  **Fix:** give **all**
artificials (basic and non-basic) a Phase I cost of -1.  Validated with a new
differential test (`tools/lp_eq_diff.py`) on 1,800 random equality/fixed-
variable problems vs scipy (0 failures) and the FZ->MIP verifier now passes
100% (was ~20% failing).  This also fixed the equality-constrained MIP path.

### Fixed: LP relation-token ambiguity
The `.lp` relation token previously allowed a redundant two-char `<=`/`>=`
form that was ambiguous with a `<` or `>` immediately followed by `=` (e.g.
`<==` could be `<` `=` `=` or `<=` `=`).  Since `<` already means "<=" (slack)
and `>` means ">=" (surplus), the format now uses **single relation chars
only**, removing all ambiguity.  Examples and generators updated; parser fuzzed
(2,000 malformed inputs) clean under ASan/UBSan.

---

## Phase 4 — Hardening for interactive use
- [ ] `--time-limit` with signal/alarm handler and graceful `UNKNOWN` + best
      incumbent (no blocking on UI/input threads).
- [ ] **Fixed-point end-to-end determinism** (bit-identical across platforms
      for UI/VG/physics; the PGS fixed kernel already guarantees this, extend
      to the rest of the pipeline: integration, collision, rendering).
- [ ] Memory: optional preallocated arena so per-frame solves do zero `malloc`
      (critical for 60 fps with no GC pauses).
- [ ] Public C API audit: every entry point documented, bounds-checked, and
      returning status codes (no `exit` in library code).

---

## Phase 5 — Scale & integration
- [ ] Batch solve API: solve N independent tiny problems with one call
      (SIMD-friendly, amortized setup) — matches "many contacts per frame".
- [ ] SIMD over independent contact clusters (AVX-512 gather/scatter).
- [ ] Python binding (ctypes) + a tiny demo harness (visualize a 2D physics
      scene solved by psolve each frame).
- [ ] Microbenchmark suite with a CI budget guard (no silent regressions).

---

## Non-goals (keep the library small)
- No general nonlinear/IP/global-optimization engines beyond the linearized
  set above.
- No GUI, no asset pipeline, no threading framework (leave threading to the
  host app; provide re-entrant, no-global-state solvers).
- No `exit()`/`abort()` in library code paths reachable from host input.

---

## Definition of done for each phase
- Unit tests (analytic + randomized), fuzz (ASan/UBSan), differential vs a
  reference (GLPK/scipy/HiGHS/brute force) as appropriate.
- Microbenchmark showing the per-solve latency target for that kernel.
- Documented C API in `README.md` and `docs/DESIGN.md`.
