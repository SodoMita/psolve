#include "err.h"
#include <stdlib.h>
#include <stdio.h>

jmp_buf psolve_env;
int     psolve_active = 0;
int     psolve_code   = PSOLVE_OK;

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
