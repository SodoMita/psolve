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
    /* No handler installed: nothing safe to do but abort. */
    fprintf(stderr, "psolve: internal failure (code %d)\n", code);
    abort();
}

void psolve_end(void)
{
    psolve_active = 0;
    psolve_code   = PSOLVE_OK;
}

PSolveArena *psolve_active_arena = NULL;

void psolve_arena_init(PSolveArena *a, void *buf, size_t cap)
{
    if (!a) return;
    a->buf = (char*)buf;
    a->cap = cap;
    a->used = 0;
}

void *psolve_arena_alloc(PSolveArena *a, size_t n)
{
    if (!a || !a->buf) return NULL;
    size_t aligned = (a->used + 15) & ~((size_t)15);
    if (aligned + 16 + n > a->cap || aligned + 16 + n < aligned) {
        psolve_fail(PSOLVE_ERR_OOM);
        return NULL;
    }
    size_t *header = (size_t*)(a->buf + aligned);
    header[0] = n;
    header[1] = 0;
    void *p = (void*)(header + 2);
    a->used = aligned + 16 + n;
    return p;
}

void *psolve_arena_calloc(PSolveArena *a, size_t n, size_t sz)
{
    if (sz > 0 && n > (size_t)-1 / sz) {
        psolve_fail(PSOLVE_ERR_OOM);
        return NULL;
    }
    size_t total = n * sz;
    void *p = psolve_arena_alloc(a, total);
    if (p) memset(p, 0, total);
    return p;
}

void psolve_arena_reset(PSolveArena *a)
{
    if (a) a->used = 0;
}

void psolve_arena_use(PSolveArena *a)
{
    psolve_active_arena = a;
}

void psolve_free(void *p)
{
    if (psolve_active_arena || !p) return;
    free(p);
}

void *psolve_malloc(size_t n)
{
    if (psolve_active_arena) {
        return psolve_arena_alloc(psolve_active_arena, n);
    }
    void *p = malloc(n);
    if (!p) {
        /* signal OOM; longjmp if a handler is installed, else abort */
        psolve_fail(PSOLVE_ERR_OOM);
        return NULL;   /* unreachable when a handler is installed */
    }
    return p;
}

void *psolve_realloc(void **p, size_t n)
{
    if (psolve_active_arena) {
        void *np = psolve_arena_alloc(psolve_active_arena, n);
        if (!np) return NULL;
        if (*p) {
            size_t *header = ((size_t*)(*p)) - 2;
            size_t old_sz = header[0];
            size_t to_copy = (old_sz < n) ? old_sz : n;
            memcpy(np, *p, to_copy);
        }
        *p = np;
        return np;
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
    if (psolve_active_arena) {
        return psolve_arena_calloc(psolve_active_arena, n, sz);
    }
    void *p = calloc(n, sz);
    if (!p) psolve_fail(PSOLVE_ERR_OOM);
    return p;
}

char *psolve_strndup(const char *s, size_t n)
{
    size_t l = strnlen(s, n);
    char *p = (char*)psolve_malloc(l + 1);
    if (!p) return NULL;
    memcpy(p, s, l);
    p[l] = 0;
    return p;
}

char *psolve_strdup(const char *s)
{
    size_t l = strlen(s) + 1;
    char *p = (char*)psolve_malloc(l);
    if (!p) return NULL;
    memcpy(p, s, l);
    return p;
}
