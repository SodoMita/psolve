/* LD_PRELOAD shim that makes the Nth (and every later) allocation fail.
 *
 * Used by tools/oom_test.py to check the claim in README's security section:
 * allocations are checked and out-of-memory is reported through the
 * PSolveErrFrame/psolve_fail() protocol instead of dereferencing NULL.
 *
 *   gcc -shared -fPIC -o /tmp/oomlib.so tools/oomlib.c -ldl
 *   PSOLVE_OOM_AFTER=25 LD_PRELOAD=/tmp/oomlib.so ./fznsolve model.fzn
 *
 * Linux/glibc only; the test skips itself elsewhere.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static void *(*real_malloc)(size_t);
static void *(*real_calloc)(size_t, size_t);
static void *(*real_realloc)(void *, size_t);
static void  (*real_free)(void *);

static long budget = -1;     /* -1 = unlimited */
static long counter;
static int  initializing;

/* dlsym() itself allocates, so serve those few allocations from a static
 * bump buffer that free() recognizes by address range. */
static char bootstrap[65536];
static size_t bootstrap_used;

static int from_bootstrap(void *p)
{
    return (char *)p >= bootstrap && (char *)p < bootstrap + sizeof(bootstrap);
}

static void *bump(size_t n)
{
    n = (n + 15) & ~(size_t)15;
    if (bootstrap_used + n > sizeof(bootstrap)) return NULL;
    void *p = bootstrap + bootstrap_used;
    bootstrap_used += n;
    return p;
}

static void init(void)
{
    if (real_malloc) return;
    initializing = 1;
    real_malloc  = (void *(*)(size_t))dlsym(RTLD_NEXT, "malloc");
    real_calloc  = (void *(*)(size_t, size_t))dlsym(RTLD_NEXT, "calloc");
    real_realloc = (void *(*)(void *, size_t))dlsym(RTLD_NEXT, "realloc");
    real_free    = (void (*)(void *))dlsym(RTLD_NEXT, "free");
    initializing = 0;
    const char *e = getenv("PSOLVE_OOM_AFTER");
    budget = e ? atol(e) : -1;
}

static int should_fail(void)
{
    ++counter;                    /* counted even when unlimited, for the census */
    if (budget < 0) return 0;
    return counter > budget;
}

void *malloc(size_t n)
{
    if (initializing) return bump(n);
    init();
    if (should_fail()) { errno = ENOMEM; return NULL; }
    return real_malloc(n);
}

void *calloc(size_t n, size_t sz)
{
    if (initializing || !real_calloc) {
        void *p = bump(n * sz);
        if (p) memset(p, 0, n * sz);
        return p;
    }
    init();
    if (should_fail()) { errno = ENOMEM; return NULL; }
    return real_calloc(n, sz);
}

void *realloc(void *p, size_t n)
{
    init();
    if (from_bootstrap(p)) {
        void *q = malloc(n);
        if (q) memcpy(q, p, n);
        return q;
    }
    if (should_fail()) { errno = ENOMEM; return NULL; }
    return real_realloc(p, n);
}

void free(void *p)
{
    if (!p || from_bootstrap(p)) return;
    init();
    real_free(p);
}

/* PSOLVE_OOM_TRACE=1 installs a SIGSEGV/SIGBUS handler that prints the crashing
 * stack, which points straight at the unchecked allocation site.  Tracing the
 * *failing allocation* instead is not workable: backtrace_symbols_fd() runs
 * inside the interposed allocator with the budget already exhausted.  The
 * handler disables further injection first, so symbolisation can allocate.
 *
 *   gcc -g -rdynamic ... -o fznsolve
 *   PSOLVE_OOM_TRACE=1 PSOLVE_OOM_AFTER=57 LD_PRELOAD=... ./fznsolve m.fzn
 */
#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

/* PSOLVE_OOM_COUNT=1: report the total number of allocations on exit, so the
 * driver can inject a failure at *every* one of them instead of guessing a
 * bound. */
__attribute__((destructor)) static void oom_report(void)
{
    if (!getenv("PSOLVE_OOM_COUNT")) return;
    char b[64];
    int k = snprintf(b, sizeof b, "PSOLVE_ALLOCS %ld\n", counter);
    ssize_t w = write(2, b, (size_t)k); (void)w;
}

static void oom_segv(int sig)
{
    budget = -1;                 /* stop failing: symbolisation allocates */
    char hdr[80];
    int k = snprintf(hdr, sizeof hdr, "--- crash (sig %d) after %ld allocs ---\n",
                     sig, counter);
    ssize_t w = write(2, hdr, (size_t)k); (void)w;
    void *bt[32];
    int n = backtrace(bt, 32);
    backtrace_symbols_fd(bt, n, 2);
    _exit(128 + sig);
}

__attribute__((constructor)) static void oom_setup(void)
{
    if (!getenv("PSOLVE_OOM_TRACE")) return;
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = oom_segv;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
}
