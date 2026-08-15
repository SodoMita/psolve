#ifndef PSOLVE_ERR_H
#define PSOLVE_ERR_H

#include <setjmp.h>
#include <stddef.h>

/* Error handling protocol.
 *
 * The solver allocates heavily with a checked-alloc helper.  Instead of
 * calling exit(1) on out-of-memory (which callers cannot recover from), the
 * helper signals a failure through a setjmp/longjmp "try/catch" that a caller
 * arms with a caller-owned PSolveErrFrame (see below).  The CLIs install one
 * and report a clean error; a library user can install their own, per
 * thread -- there is no process-global handler state.
 */

#define PSOLVE_OK        0
#define PSOLVE_ERR_OOM   1   /* (allocation failed) */
#define PSOLVE_ERR_SOLVE 2   /* (internal solver error) */

/* Caller-owned error frames: no process-global state (2026-08-15(7)
 * redesign, roadmap 6.3).  Each thread keeps its own chain of frames; a
 * frame's storage is owned by the activating function's stack, so a
 * recovering, multi-threaded library host owns cleanup without any
 * cross-thread or cross-scope interference.  Contrast with the retired
 * protocol, in which one process-global jmp_buf/active-flag/code triple
 * meant a second thread installing a handler (or raising a failure)
 * hijacked or corrupted the first thread's recovery.
 *
 * Usage (the setjmp target is the caller's own frame):
 *
 *     PSolveErrFrame fr;
 *     psolve_frame_push(&fr);
 *     if (setjmp(fr.env) != 0) {
 *         int code = psolve_err_code();
 *         ... recover (the frame is already popped: do NOT pop again) ...
 *     } else {
 *         ... work that may call psolve_fail() ...
 *         psolve_frame_pop(&fr);
 *     }
 *
 * Contract:
 *  - setjmp(fr.env) must come immediately after the push, before any
 *    fallible call (a failure in between would unwind into an un-armed
 *    jmp_buf).
 *  - psolve_fail() pops the innermost frame and jumps; the recovered
 *    handler must NOT pop it again, and a fresh push inside the handler
 *    is fine.
 *  - Pop order is checked: popping a frame that is not innermost is a
 *    protocol violation and aborts.
 *  - With no frame installed anywhere in the calling thread, a failure
 *    prints a message and exit()s (CLI-style behavior).
 */
typedef struct PSolveErrFrame {
    jmp_buf env;                        /* setjmp target */
    struct PSolveErrFrame *prev;        /* next outer frame (this thread) */
} PSolveErrFrame;

/* Push fr as the calling thread's innermost error frame (initializes
 * code to PSOLVE_OK and links it above the previous frame). */
void psolve_frame_push(PSolveErrFrame *fr);

/* Pop fr, which must be the calling thread's innermost frame. */
void psolve_frame_pop(PSolveErrFrame *fr);

/* Called by allocation helpers / solver internals on failure: records the
 * code for psolve_err_code(), pops the calling thread's innermost frame
 * and longjmps there.  exit()s if no frame is installed. */
void psolve_fail(int code);

/* The code of the failure that most recently unwound THIS thread
 * (PSOLVE_OK before any failure).  Kept in thread-local storage and read
 * through this function on purpose: unlike a field of the frame struct, a
 * read in the recovery path is not subject to the C11 7.13.2.1
 * indeterminacy rule for locals modified between setjmp and longjmp. */
int psolve_err_code(void);

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
 * wall-clock time limits.  The callback slot is per-thread (thread-local),
 * so two hosts driving solves on two threads never share stop state; when
 * no callback is installed for the calling thread, psolve_stop() is a no-op
 * returning 0.  The callback itself only READS a flag (e.g. a
 * sig_atomic_t set from a signal handler), keeping signal handling
 * async-signal-safe. */
void psolve_stop_set(int (*fn)(void));
int psolve_stop(void);

#endif
