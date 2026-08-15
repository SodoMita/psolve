# Phase 4: interactive-use time limits (millisecond precision + QP stop)

This branch continues **Phase 4 — Hardening for interactive use** in the
roadmap.  The deliverable is the first Phase 4 bullet, done to the project's
honesty bar: *cooperative `--time-limit` with a graceful stop and best
incumbent, at the precision an interactive consumer actually needs.*

## Problem

Two gaps for per-frame / UI-thread use:

1. **`alarm()` granularity.** `lpsolve`, `mipsolve` and `fznsolve` armed their
   `-t` budget with `alarm((ms + 999) / 1000)`, which is whole-second
   granularity and rounds **up**.  `-t 1` (1 ms) actually granted a full
   second; `-t 1500` (1.5 s) granted two.  For a real-time budget of a few
   milliseconds that is a ~1000x overshoot — the exact thing the "no blocking
   on UI/input threads" goal forbids.

2. **No QP time limit at all.** The QP solver is the UI/layout workhorse
   (the PGS physics path is already zero-malloc and per-frame, but general
   convex QP is what layout and contact-set refinement use).  `qpsolve` had no
   `-t`, no SIGINT handling, and `active_set` never polled `psolve_stop()`, so
   a slow QP would run to completion (or to the active-set iteration cap) with
   no way to cut it off.

## Changes

### Shared, precise budget helper — `tools/tlimit.h`

A small header-only helper (`tlimit_arm` / `tlimit_disarm`) that arms an
`ITIMER_REAL` with microsecond resolution.  All four CLI drivers now use it:
`src/main.c` (LP), `tools/mipsolve.c`, `tools/fznsolve.c`, and `tools/qpsolve.c`
(which previously had no budget at all).  The SIGINT/SIGALRM flag stays in each
driver (a single async-signal-safe assignment); the helper only owns the timer.
`tools/tlimit_test.c` verifies that a 50 ms budget fires sub-second — i.e. that
the precision improvement is real and not the old whole-second `alarm()`.

### QP cooperative stop — `src/qp.c` / `src/qp.h`

- New status `QP_STOPPED` (6).
- `active_set` polls `psolve_stop()` once per iteration; on a stop it hands back
  the current — still feasible — iterate as a best incumbent and sets
  `QP_STOPPED`, **without** claiming optimality.
- The Phase-I feasibility search (`find_feasible`) now returns a tri-state
  (feasible / not-feasible / **stopped**) and propagates a stop; `qp_solve`
  then reports `QP_STOPPED` with `res->x == NULL` because no feasible point
  exists to hand back.  This preserves the "never report a wrong answer" rule:
  a possibly-infeasible iterate is never presented as a solution.
- The CLI (`tools/qpsolve.c`) gained `-t`/`--time-limit` and `--print`, SIGINT
  + SIGALRM handling, and reports the stop honestly.  The historical
  machine-readable output (`SOLUTION …` / `OBJ …` / `ITERS …` for solved,
  `STATUS <n>` otherwise) is preserved so the differential harness
  (`qp_diff.py`, `qp_gen.py`) keeps working unchanged.

## Honesty guarantees

The design rule of this repo is *never report a wrong answer*.  Consistent
with it:

- A stopped QP returns `QP_STOPPED`, **never** `0` (OPTIMAL).  The incumbent is
  the current feasible iterate, explicitly un-certified.
- A stop during the Phase-I feasibility search returns `QP_STOPPED` with no
  incumbent (`x == NULL`) — a feasible-looking but un-verified point is not
  fabricated.
- On a stop, `solver_solve` (LP/MIP) and the QP core do not retry the
  sparse-then-dense fallback: retrying would violate the requested budget.

## Regression tests (wired into `test.sh`, section `[5.2/7]`)

- `tools/qp_stop_test.c` — deterministic: baseline QP still solves to OPTIMAL;
  a forced stop returns `QP_STOPPED` (not OPTIMAL) with a feasible incumbent;
  a stop during Phase-I returns `QP_STOPPED` with `x == NULL`; the solver is
  fully usable again once the stop flag is cleared.
- `tools/tlimit_test.c` — a 50 ms `tlimit_arm` fires SIGALRM sub-second,
  proving ITIMER_REAL precision (old `alarm()` would have taken a full second).

Full `./test.sh` (minus the MiniZinc differential, which needs the `minizinc`
compiler) passes with zero wrong answers; the changed drivers are fuzz-clean
and OOM-injection-clean under ASan/UBSan.

## Zero-malloc arena (added later on this branch)

The next Phase 4 bullet — *optional preallocated arena for zero-`malloc`
per-frame solves* — was completed.  A prior remote branch (`arena/audit-hardening`)
had attempted this with a **process-global** active arena, which the branch
audit rejected as unsound: global mutable state violates the project's
"re-entrant, no-global-state" rule, and its `realloc` read a header from a
pointer that might not be arena-owned (a libc pointer in scope → garbage read).

The implementation on this branch (`src/err.c`, `src/err.h`) is sound by
construction:

- **Thread-local**: each thread has its own active-arena stack
  (`_Thread_local`), so concurrent frames on different threads never corrupt
  one another.  `psolve_arena_use(a)` pushes, `psolve_arena_end()` pops;
  nesting is supported.
- **Ownership-checked `free` / `realloc`**: a pointer is only treated as
  arena-owned if it lies inside the active arena's buffer range.  A libc
  pointer realloc'd under an active arena falls back to libc — no header is
  ever read from a non-arena block.
- **Every library allocation now routes through `psolve_*`**: the solver,
  QP, MIP, fx (including `fx_core.inc`), sparse-LU, parser and FlatZinc paths
  were converted from raw `malloc`/`calloc`/`free` to `psolve_malloc` /
  `psolve_calloc` / `psolve_free`.  This is what makes the zero-malloc
  guarantee real: while an arena is active, all of these bump-allocate from
  the arena and their frees are no-ops (reclaimed on `psolve_arena_reset`).
- **Correctness & budget**: an undersized arena reports OOM through the
  existing `psolve_fail` protocol rather than corrupting memory.

`tools/arena_test.c` (wired into `test.sh` as `[5.3/7]`) links with
`--wrap=malloc,calloc,realloc,free` and asserts that the QP, LP and exact-fx
**solve** calls make zero libc heap calls under an active arena, return the
same objective as a non-arena solve, and remain correct under nested scopes.
It also verifies ownership (non-arena pointer realloc → libc), reset reuse, and
a clean failure on an undersized arena.  Full `./test.sh` (minus the MiniZinc
differential, which needs the `minizinc` compiler) passes with zero wrong
answers.

## Remaining Phase 4 items (not in this branch)

- Fixed-point end-to-end determinism for the wider pipeline.
- Public C API audit completion.
