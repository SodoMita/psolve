/* Example: a 2D contact resolved with the FIXED-POINT PGS solver.
 *
 * Same setup as contact_pgs.c but in Q16.16 integer arithmetic (scale S = 65536)
 * so the result is bit-identical on any platform/compiler.
 */
#include "pgs_fixed.h"
#include <stdio.h>

int main(void)
{
    const int64_t S = 65536;   /* Q16.16 */

    /* integer contact matrix A = J M^-1 J^T (symmetric PSD), raw coefficients */
    int64_t A[9] = { 2, 0, 0,  0, 2, 0,  0, 0, 2 };   /* diagonal for this example */

    /* velocity bias, scaled:  b_int = b_real * S */
    int64_t b[3] = { -2*S,  S/2,  -3*S/10 };   /* approaching velocity -> resolve */

    int64_t lo[3] = { 0, -S, -S };        /* normal impulse >= 0 (no pull) */
    int64_t hi[3] = { PGS_INF, S, S };    /* friction capped at +1 */

    int64_t x[3] = { 0, 0, 0 };           /* warm start (previous substep) */

    PGSFixedOptions opt = { 3, 200, 1, 1, 1 };
    PGSResult res;
    pgsf_solve(&opt, A, b, lo, hi, x, &res);

    printf("fixed-point contact impulses (Q16.16):\n");
    printf("  normal=%.4f  friction=(%.4f, %.4f)\n",
           (double)x[0]/S, (double)x[1]/S, (double)x[2]/S);
    printf("  raw integers: %ld %ld %ld\n", (long)x[0], (long)x[1], (long)x[2]);
    printf("  solver: status=%d iters=%d\n", res.status, res.iters);
    return 0;
}
