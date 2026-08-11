#include "err.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

jmp_buf psolve_env;
int     psolve_active = 0;
int     psolve_code   = PSOLVE_OK;

int (*psolve_stop_fn)(void) = NULL;

int psolve_stop(void)
{
    return psolve_stop_fn ? psolve_stop_fn() : 0;
}

int psolve_try(void)
{
    if (psolve_active) return 1;      /* a handler is already installed */
    psolve_active = 1;
    psolve_code   = PSOLVE_OK;
    return 0;
}

void psolve_fail(int code)
{
    psolve_code = code;
    if (psolve_active) {
        longjmp(psolve_env, code);    /* unwinds to the psolve_try() frame */
    }
    /* No handler installed: exit cleanly instead of aborting. */
    fprintf(stderr, "psolve: internal failure (code %d)\n", code);
    exit(code);
}

void psolve_end(void)
{
    psolve_active = 0;
    psolve_code   = PSOLVE_OK;
}

/* ------------------------------------------------------------------ */
/* Thread-local arena state.                                          */
/*                                                                    */
/* Each thread keeps a small stack of active arenas.  psolve_arena_use */
/* pushes, psolve_arena_end pops, so nested "use" scopes and solves   */
/* that themselves allocate (QP's Phase-I -> active_set recursion,    */
/* FlatZinc's nested LP builds) never corrupt one another.            */
/* ------------------------------------------------------------------ */
#define PSOLVE_ARENA_MAX_NEST 32
static _Thread_local PSolveArena *psolve_arena_stack[PSOLVE_ARENA_MAX_NEST];
static _Thread_local int          psolve_arena_depth = 0;

/* 16-byte alignment for every arena block. */
#define PSOLVE_ARENA_ALIGN 16
/* Header stored just before each aligned block: [used_bytes][magic]. */
typedef struct { size_t size; unsigned long magic; } PSolveArenaHeader;
#define PSOLVE_ARENA_MAGIC 0x50736F6C76654155UL   /* "PsolveAU" */

void psolve_arena_init(PSolveArena *a, void *buf, size_t cap)
{
    if (!a) return;
    a->buf = (char*)buf;
    a->cap = cap;
    a->used = 0;
}

void psolve_arena_use(PSolveArena *a)
{
    if (psolve_arena_depth < PSOLVE_ARENA_MAX_NEST)
        psolve_arena_stack[psolve_arena_depth++] = a;
}

void psolve_arena_end(void)
{
    if (psolve_arena_depth > 0) psolve_arena_depth--;
}

void psolve_arena_reset(PSolveArena *a)
{
    if (a) a->used = 0;
}

/* Active arena for this thread, or NULL. */
static PSolveArena *active_arena(void)
{
    return psolve_arena_depth > 0 ? psolve_arena_stack[psolve_arena_depth - 1] : NULL;
}

/* Is p a pointer we handed out from the active arena's buffer? */
static int in_active_arena(const PSolveArena *a, const void *p)
{
    if (!a || !a->buf || !p) return 0;
    const char *c = (const char*)p;
    return c >= a->buf && c < a->buf + a->used;
}

/* Bump-allocate from an arena.  Returns NULL if the arena is too small
 * (does not fail via psolve_fail -- the caller decides how to report). */
static void *arena_alloc(PSolveArena *a, size_t n)
{
    if (!a || !a->buf) return NULL;
    /* Align the start offset up to PSOLVE_ARENA_ALIGN. */
    size_t off = (a->used + (PSOLVE_ARENA_ALIGN - 1)) & ~((size_t)(PSOLVE_ARENA_ALIGN - 1));
    size_t need = sizeof(PSolveArenaHeader) + n;
    if (off > a->cap || need > a->cap - off) return NULL;   /* overflow-safe */
    PSolveArenaHeader *h = (PSolveArenaHeader*)(a->buf + off);
    h->size = n;
    h->magic = PSOLVE_ARENA_MAGIC;
    a->used = off + need;
    return (void*)(h + 1);
}

void *psolve_malloc(size_t n)
{
    PSolveArena *a = active_arena();
    if (a) {
        void *p = arena_alloc(a, n);
        if (!p) { psolve_fail(PSOLVE_ERR_OOM); return NULL; }
        return p;
    }
    void *p = malloc(n);
    if (!p) {
        /* signal OOM; longjmp if a handler is installed, else exit */
        psolve_fail(PSOLVE_ERR_OOM);
        return NULL;   /* unreachable when a handler is installed */
    }
    return p;
}

void *psolve_realloc(void **p, size_t n)
{
    PSolveArena *a = active_arena();
    if (a) {
        /* Ownership-checked arena realloc: if the old pointer is arena-owned,
           bump-allocate a fresh block and copy min(old,new).  Otherwise fall
           back to libc realloc so a pointer from outside the arena scope is
           never misread. */
        if (*p == NULL) {
            void *np = arena_alloc(a, n);
            if (!np) { psolve_fail(PSOLVE_ERR_OOM); return NULL; }
            *p = np;
            return np;
        }
        if (in_active_arena(a, *p)) {
            PSolveArenaHeader *h = ((PSolveArenaHeader*)(*p)) - 1;
            size_t old = h->size;
            void *np = arena_alloc(a, n);
            if (!np) { psolve_fail(PSOLVE_ERR_OOM); return NULL; }
            memcpy(np, *p, old < n ? old : n);
            *p = np;
            return np;
        }
        /* Not arena-owned: libc path. */
    }
    void *np = realloc(*p, n);
    if (!np) {
        free(*p);
        *p = NULL;
        psolve_fail(PSOLVE_ERR_OOM);
        return NULL;   /* unreachable when a handler is installed */
    }
    *p = np;
    return np;
}

void *psolve_calloc(size_t n, size_t sz)
{
    if (sz > 0 && n > (size_t)-1 / sz) { psolve_fail(PSOLVE_ERR_OOM); return NULL; }
    size_t total = n * sz;
    void *p = psolve_malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void psolve_free(void *p)
{
    if (!p) return;
    PSolveArena *a = active_arena();
    /* No-op for arena-owned pointers (bump arena reclaims on reset).  Anything
       not owned by the active arena is freed via libc. */
    if (a && in_active_arena(a, p)) return;
    free(p);
}

char *psolve_strndup(const char *s, size_t n)
{
    size_t l = strnlen(s, n);
    char *p = (char*)psolve_malloc(l + 1);
    if (!p) { psolve_fail(PSOLVE_ERR_OOM); return NULL; }
    memcpy(p, s, l);
    p[l] = 0;
    return p;
}

char *psolve_strdup(const char *s)
{
    size_t l = strlen(s) + 1;
    char *p = (char*)psolve_malloc(l);
    if (!p) psolve_fail(PSOLVE_ERR_OOM);
    memcpy(p, s, l);
    return p;
}
