#ifndef PSOLVE_TLIMIT_H
#define PSOLVE_TLIMIT_H

/* Cooperative millisecond-precision wall-clock time limit for the CLI drivers.
 *
 * The solvers already poll psolve_stop() (see src/err.h) inside their hot
 * loops and, when it returns nonzero, wind down to an honest "stopped / best
 * incumbent" status instead of labelling a partial result OPTIMAL.  A driver
 * only needs to arrange for psolve_stop() to start returning nonzero when the
 * budget is spent:
 *
 *   1. a SIGINT handler that sets the shared stop flag (Ctrl-C), and
 *   2. a SIGALRM handler that sets the same flag when a wall-clock budget runs
 *      out (the ITIMER_REAL timer below fires SIGALRM).
 *
 * The historical implementation armed the budget with alarm(), which is
 * whole-second granularity and rounds UP: `-t 1` (1 ms) actually granted up to
 * a full second, and `-t 1500` (1.5 s) granted 2 s.  For interactive use --
 * per-frame solver budgets of a few milliseconds -- that is a ~1000x overshoot,
 * so these helpers arm an ITIMER_REAL with microsecond resolution instead.
 *
 * The signal handler that flips the stop flag is async-signal-safe (a single
 * assignment to a volatile sig_atomic_t), so the flag itself stays in the
 * driver; these helpers only own the timer.
 *
 *   tlimit_arm(time_ms)   arm the budget timer (<=0 disarms); returns 0 ok, -1 on error
 *   tlimit_disarm()       clear the timer (also safe to call when unarmed)
 */

#include <sys/time.h>
#include <stddef.h>

static inline void tlimit_disarm(void)
{
    struct itimerval it;
    it.it_value.tv_sec = 0;
    it.it_value.tv_usec = 0;
    it.it_interval.tv_sec = 0;
    it.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &it, NULL);
}

static inline int tlimit_arm(long time_ms)
{
    struct itimerval it;
    if (time_ms <= 0) {
        it.it_value.tv_sec = 0;
        it.it_value.tv_usec = 0;
        it.it_interval.tv_sec = 0;
        it.it_interval.tv_usec = 0;
        return setitimer(ITIMER_REAL, &it, NULL);
    }
    it.it_value.tv_sec = time_ms / 1000;
    it.it_value.tv_usec = (time_ms % 1000) * 1000;
    it.it_interval.tv_sec = 0;
    it.it_interval.tv_usec = 0;
    return setitimer(ITIMER_REAL, &it, NULL);
}

#endif /* PSOLVE_TLIMIT_H */
