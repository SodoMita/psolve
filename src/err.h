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

/* Preallocated memory Arena.
 * Allows interactive and real-time callers (UI / physics loops) to supply a
 * fixed memory buffer so solves run with zero libc malloc calls and no GC pauses. */
typedef struct {
    char *buf;
    size_t cap;
    size_t used;
} PSolveArena;

extern PSolveArena *psolve_active_arena;

void psolve_arena_init(PSolveArena *a, void *buf, size_t cap);
void *psolve_arena_alloc(PSolveArena *a, size_t n);
void *psolve_arena_calloc(PSolveArena *a, size_t n, size_t sz);
void psolve_arena_reset(PSolveArena *a);
void psolve_arena_use(PSolveArena *a);

/* Checked free: no-op when an arena is active, frees with libc free otherwise. */
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
