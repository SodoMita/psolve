#ifndef PSOLVE_ERR_H
#define PSOLVE_ERR_H

#include <setjmp.h>
#include <stddef.h>

/* Error handling protocol.
 *
 * The solver allocates heavily with a checked-alloc helper.  Instead of
 * calling exit(1) on out-of-memory (which callers cannot recover from), the
 * helper signals a failure through a setjmp/longjmp "try/catch" that a caller
 * can set up with psolve_try().  The CLI installs this and reports a clean
 * error; a library user can install their own.
 *
 * failure codes:
 *   PSOLVE_OK        = 0
 *   PSOLVE_ERR_OOM   = 1   (allocation failed)
 *   PSOLVE_ERR_SOLVE = 2   (internal solver error)
 */

#define PSOLVE_OK        0
#define PSOLVE_ERR_OOM   1
#define PSOLVE_ERR_SOLVE 2

extern jmp_buf psolve_env;     /* active error-jump target (empty if none) */
extern int     psolve_active;  /* nonzero when psolve_try() is installed */
extern int     psolve_code;    /* last failure code */

/* Install the error handler; returns 0 if installed, 1 if already installed.
 * On a failure the code jumps back here and psolve_code is set. */
int psolve_try(void);

/* Called by allocation helpers / solver internals on failure; longjmps back
 * to the psolve_try() point.  No-op (falls through) if no handler is set. */
void psolve_fail(int code);

/* Reset the handler (must be called by the owner of psolve_try). */
void psolve_end(void);

/* Checked allocation.  On failure, signals PSOLVE_ERR_OOM and, if no handler
 * is installed, aborts.  Returns the allocation on success. */
void *psolve_malloc(size_t n);

/* Checked realloc.  On failure frees *p (if non-NULL), signals PSOLVE_ERR_OOM
 * (longjmp if a handler is installed, else abort), and returns NULL.  Pass
 * the address of the pointer; *p is set to NULL on failure so callers never
 * leak the old block.  Returns the new allocation on success. */
void *psolve_realloc(void **p, size_t n);

/* Checked calloc: zeroed allocation, signals OOM on failure (see psolve_malloc). */
void *psolve_calloc(size_t n, size_t sz);

/* Checked strdup: signals OOM on failure (see psolve_malloc). */
char *psolve_strdup(const char *s);

/* Checked strndup: copies at most n bytes and NUL-terminates; signals OOM on
 * failure (see psolve_malloc).  Note that plain strndup() allocates inside
 * libc, so its failure cannot be caught by the psolve_* protocol -- always use
 * this one. */
char *psolve_strndup(const char *s, size_t n);

/* Preallocated memory arena for zero-malloc per-frame solves (Phase 4).
 *
 * Interactive / real-time callers (UI, physics loops) can supply a fixed
 * buffer and scoped "use" it so a solve runs without any libc malloc in the
 * hot loop -- no GC pauses, bounded, deterministic cost.  The arena is
 *
 *   - re-entrant and thread-local: each thread has its own active arena, and
 *     `psolve_arena_use` / `psolve_arena_end` nest (save/restore the previous
 *     active arena), so it never introduces cross-thread or nested-solve
 *     corruption.  This is the key difference from the earlier rejected design,
 *     which used a process-global active arena.
 *
 *   - ownership-checked: `psolve_free` and `psolve_realloc` verify a pointer is
 *     inside the active arena's buffer before treating it as arena-owned; any
 *     other pointer falls back to libc.  This removes the earlier design's
 *     unsound header read from pointers that might not be arena-owned.
 *
 * Bump allocation: blocks are 16-byte aligned, each preceded by a small header
 * recording its size (so realloc can copy).  Frees are no-ops within the arena
 * scope; memory is reclaimed on `psolve_arena_reset`.  The PGS physics kernels
 * were already zero-malloc (caller buffers + alloca); this extends that
 * guarantee to the general LP/QP/MIP solve paths.
 *
 * Contract: arm the arena, solve one or more problems, then reset before the
 * next frame.  All psolve_* allocations made while the arena is active for the
 * current thread come from the arena.
 */
typedef struct {
    char *buf;        /* caller-provided buffer */
    size_t cap;       /* buffer size in bytes */
    size_t used;      /* bytes consumed so far */
} PSolveArena;

/* Initialize an arena over a caller buffer.  cap may be 0 (disabled). */
void psolve_arena_init(PSolveArena *a, void *buf, size_t cap);
/* Push `a` as this thread's active arena (saving the previous one); call
 * psolve_arena_end() to restore.  Nesting is supported. */
void psolve_arena_use(PSolveArena *a);
/* Pop back to the previously active arena for this thread (or none). */
void psolve_arena_end(void);
/* Reclaim all arena memory (used = 0) for reuse.  Only valid between solves. */
void psolve_arena_reset(PSolveArena *a);

/* Checked free: no-op for arena-owned pointers while the active arena covers
 * them, libc free otherwise.  All library frees go through this so an active
 * arena never sees a libc free() on arena memory. */
void psolve_free(void *p);

/* Cooperative abort.  Solvers call psolve_stop() periodically (e.g. once per
 * branch-and-bound node) and, if it returns nonzero, wind down and report a
 * limit status instead of running to completion.  The driver installs a
 * callback (e.g. one that reads a SIGINT/SIGALRM flag) to support Ctrl-C and
 * wall-clock time limits.  When no callback is installed, psolve_stop() is a
 * no-op returning 0. */
extern int (*psolve_stop_fn)(void);
int psolve_stop(void);

#endif
