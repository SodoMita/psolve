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

void *psolve_malloc(size_t n)
{
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
    void *p = calloc(n, sz);
    if (!p) psolve_fail(PSOLVE_ERR_OOM);
    return p;
}

char *psolve_strdup(const char *s)
{
    size_t l = strlen(s) + 1;
    char *p = (char*)malloc(l);
    if (!p) psolve_fail(PSOLVE_ERR_OOM);
    memcpy(p, s, l);
    return p;
}
