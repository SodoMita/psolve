#include "tlimit.h"
#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

/* Verifies that tlimit_arm() delivers millisecond-precision wall-clock
 * budgets -- the Phase 4 interactive-hardening improvement over alarm().
 *
 * alarm() is whole-second granularity and rounds up: `-t 50` (50 ms) actually
 * fired after a full second.  tlimit_arm() uses ITIMER_REAL, so a 50 ms budget
 * must fire within well under a second.
 *
 * usage: tlimit_test
 * exits 0 if a 50ms budget fires within 500ms (i.e. sub-second precision),
 * 1 otherwise.  The 500ms ceiling is far enough from both 50ms (the real
 * budget) and 1000ms (what alarm() would have granted) to be robust. */

static volatile sig_atomic_t g_stop = 0;
static void on_alarm(int signo) { (void)signo; g_stop = 1; }

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

int main(void)
{
    signal(SIGALRM, on_alarm);
    g_stop = 0;
    if (tlimit_arm(50) != 0) { fprintf(stderr, "tlimit_arm failed\n"); return 1; }

    double t0 = now_ms();
    /* Busy-wait for the SIGALRM to set the flag. */
    while (!g_stop && now_ms() - t0 < 500.0) { /* spin */ }
    double elapsed = now_ms() - t0;
    tlimit_disarm();

    if (!g_stop) {
        fprintf(stderr, "FAIL: 50ms budget did not fire within 500ms "
                        "(elapsed %.1f ms; sub-second precision broken)\n", elapsed);
        return 1;
    }
    if (elapsed > 500.0) {
        fprintf(stderr, "FAIL: timer fired but outside the 500ms window (%.1f ms)\n", elapsed);
        return 1;
    }
    /* It fired sub-second, proving ITIMER_REAL precision.  Report the timing. */
    printf("ok: 50ms budget fired after %.1f ms (< 500ms => sub-second precision)\n", elapsed);
    return 0;
}
