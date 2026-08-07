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

---

## Phase 1 — Real-time physics kernel (PRIORITY for primary use case)
The heart of every 2D physics engine (Box2D-style) is not a general simplex but
a **projected Gauss-Seidel / sequential-impulse** solver on boxed
complementarity constraints. Small (m ≤ ~256), dense, warm-started, iterated a
fixed number of times per substep.  **All of this runs in fixed point.**

- [x] `src/pgs.c`: floating-point projected Gauss-Seidel boxed-QP solver
      `min ½xᵀAx + bᵀx  s.t.  lo ≤ x ≤ hi` (reference / cross-check).
- [x] `src/pgs_fixed.c`: **fixed-point (integer)** PGS boxed-QP solver
      `min ½xᵀAx + bᵀx  s.t.  lo ≤ x ≤ hi`, all-integer arithmetic with 128-bit
      accumulation, round-half-away division, rational SOR (`w_num/w_den`),
      bit-identical determinism.  Validated against the float reference.
- [ ] Contact block helper: build the LCP/A-matrix from contact points
      (friction + normal) as integer, solve impulses in fixed point.
- [ ] Pre-sized row storage for the banded/diagonal-dominant contact-cluster
      matrices.
- [ ] Fixed-point QP / LP kernels (integer revised-simplex for VG/UI layout).

### Phase 1 progress: float + fixed-point PGS kernels, tests, bench — done this turn.

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
- [ ] FlatZinc lexer + parser (name→index map, ranges, annotations, solve item)
- [ ] Constraint dispatch: int_lin_* (native), element/all_different/count/
      table/circuit/cumulative/regular (rejected or linearized)
- [ ] MIP CLI bridge (integer var list, node/time limits, best-bound stats)
- [ ] Output format + stats (`===UNKNOWN===`, `%%%mzn-stat:*`)
- [ ] Differential validation vs HiGHS; round-trip .mzn→.fzn→solve→verify
- [ ] Fuzz the FlatZinc parser

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
