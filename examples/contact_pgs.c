/* Example: a single 2D contact resolved with the PGS boxed-QP solver.
 *
 * Two bodies approach along the normal n=(0,1).  We solve for normal + friction
 * impulses with box constraints (impulse must be nonnegative in the normal
 * direction; friction bounded by mu * normal).
 *
 * This mirrors the tiny dense system Box2D-style engines solve every substep;
 * psolve's pgs_solve does it in well under a microsecond.
 */
#include "pgs.h"
#include <stdio.h>
#include <math.h>

int main(void)
{
    /* Contact Jacobian: one normal + two friction axes -> 3 scalar impulses.
       A = J M^-1 J^T (effective inverse mass in contact space) is symmetric
       PSD.  Here M^-1 is diagonal with body inverse masses/inertia. */
    int n = 3;
    double J[9] = {
        /* n_x, f1_x, f2_x */  1.0,  1.0, 0.0,
        /* n_y, f1_y, f2_y */  0.0,  0.0, 1.0,
        /* torque */           0.0,  0.0, 1.0,
    };
    double Minv[9] = {1.0,0,0, 0,1.0,0, 0,0,1.0};   /* inverse mass/inertia */
    double A[9] = {0};
    for (int i=0;i<n;i++) for (int j=0;j<n;j++){
        double s=0; for(int k=0;k<n;k++) s+= J[k*n+i]*Minv[k*n+k]*J[k*n+j];
        A[i*n+j]=s;
    }
    /* velocity bias: impulse = -A^-1 v_rel; use b = -v_rel */
    double b[3] = { -2.0, 0.5, -0.3 };   /* approaching velocity -> resolve */
    double lo[3] = { 0.0, -1.0, -1.0 };  /* normal impulse >= 0 (no pull)   */
    double hi[3] = { PGS_INF, 1.0, 1.0 };/* friction capped */
    double x[3] = { 0.0, 0.0, 0.0 };     /* warm start (previous substep)   */

    PGSOptions opt = { n, 200, 1.0, 1e-10 };
    PGSResult res;
    pgs_solve(&opt, A, b, lo, hi, x, &res);

    printf("contact impulses: normal=%.4f  friction=(%.4f, %.4f)\n", x[0], x[1], x[2]);
    printf("solver: status=%d iters=%d objective=%.4f\n", res.status, res.iters, res.obj);
    return 0;
}
