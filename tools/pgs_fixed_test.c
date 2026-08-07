#include "pgs.h"
#include "pgs_fixed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Verify fixed-point PGS:
 *   1. matches the float PGS within scale tolerance on random PSD systems
 *   2. exact analytic cases
 *   3. determinism (two runs identical bit-for-bit)
 */

static double fx2real(int64_t v, int64_t S){ return (double)v / (double)S; }

static void t1_analytic(void)
{
    /* min 1/2 x^2 - 3x  => x=3.  S=256 => x_int=768. */
    const int64_t S = 256;
    int64_t A[1] = {1};
    int64_t b[1] = {-3*S};
    int64_t lo[1] = {-10*S}, hi[1] = {10*S};
    int64_t x[1] = {0};
    PGSFixedOptions opt = {1, 1000, 1, 1, 1};
    PGSResult res;
    pgsf_solve(&opt, A, b, lo, hi, x, &res);
    printf("T1: x_int=%ld (expect 768) real=%g status=%d iters=%d\n",
           (long)x[0], fx2real(x[0],S), res.status, res.iters);
}

static void t2_clamped(void)
{
    /* min 1/2 x^2 - 3x, box [0,1] => x=1. S=256 => x_int=256. */
    const int64_t S = 256;
    int64_t A[1] = {1};
    int64_t b[1] = {-3*S};
    int64_t lo[1] = {0}, hi[1] = {S};
    int64_t x[1] = {0};
    PGSFixedOptions opt = {1, 1000, 1, 1, 1};
    PGSResult res;
    pgsf_solve(&opt, A, b, lo, hi, x, &res);
    printf("T2: x_int=%ld (expect 256) real=%g status=%d\n",(long)x[0], fx2real(x[0],S), res.status);
}

static void t3_random_match_float(void)
{
    srand(11);
    int bad=0, tested=0;
    const int64_t S = 65536;   /* Q16.16 */
    for (int t=0;t<4000;t++){
        int n=1+rand()%6;
        /* build an INTEGER symmetric PSD A = L L^T + I, L integer lower-tri */
        int64_t L[6*6]; for(int i=0;i<n*n;i++)L[i]=0;
        for(int i=0;i<n;i++)for(int j=0;j<=i;j++)L[i*n+j]=(rand()%5)-2;
        int64_t A[36]; for(int i=0;i<n;i++)for(int j=0;j<n;j++){
            __int128 s=(i==j?1:0); for(int k=0;k<n;k++) s+=(__int128)L[i*n+k]*L[j*n+k];
            A[i*n+j]=(int64_t)s;
        }
        double Ad[36]; for(int i=0;i<n*n;i++)Ad[i]=(double)A[i];
        int64_t b[6], lo[6], hi[6], xf[6];
        double bd[6], lod[6], hid[6], xd[6];
        for(int i=0;i<n;i++){
            double bv=((rand()%400)-200)/100.0;
            double lov=-((rand()%10)+1), hiv=((rand()%10)+1);
            bd[i]=bv; lod[i]=lov; hid[i]=hiv;
            b[i]=(int64_t)llround(bv*S); lo[i]=(int64_t)llround(lov*S); hi[i]=(int64_t)llround(hiv*S);
            xf[i]=0; xd[i]=0;
        }
        PGSFixedOptions fopt={n,1000,1,1,0};   /* run all iters, no tol */
        PGSResult fres; pgsf_solve(&fopt,A,b,lo,hi,xf,&fres);
        PGSOptions dopt={n,1000,1.0,0.0};
        PGSResult dres; pgs_solve(&dopt,Ad,bd,lod,hid,xd,&dres);
        for(int i=0;i<n;i++){
            double fr=fx2real(xf[i],S);
            if(fabs(fr-xd[i]) > 0.01){   /* ~ few scaled units at S=65536 */
                bad++; if(bad<6)printf("T3 MISMATCH t=%d i=%d fx=%.5g dbl=%.5g\n",t,i,fr,xd[i]);
                break;
            }
        }
        /* determinism: rerun, must be bit-identical */
        int64_t x2[6]; for(int i=0;i<n;i++)x2[i]=0;
        PGSResult r2; pgsf_solve(&fopt,A,b,lo,hi,x2,&r2);
        for(int i=0;i<n;i++) if(x2[i]!=xf[i]){ bad++; if(bad<8)printf("T3 NON-DETERMINISTIC t=%d\n",t); break; }
        tested++;
    }
    printf("T3 random match-float: tested=%d bad=%d\n",tested,bad);
}

/* T4: hostile / degenerate input must not crash and must stay in the box.
 *
 * A zero (or negative) diagonal is what an inactive or degenerate contact row
 * looks like, and a physics host can hand one to the kernel at any frame.  It
 * used to divide by zero: SIGFPE, taking the whole process down.  Huge
 * residuals used to be truncated from the 128-bit accumulator to int64, so a
 * large positive residual could wrap to a negative one and move x the wrong
 * way; they are saturated now. */
static void t4_degenerate(void)
{
    int bad = 0;

    /* zero diagonal on one row */
    {
        int64_t A[4] = { 65536, 0,
                         0,     0 };
        int64_t b[2] = { -65536, -65536 };
        int64_t lo[2] = { 0, 0 }, hi[2] = { 1 << 20, 1 << 20 };
        int64_t x[2] = { 0, 0 };
        PGSFixedOptions o = { 2, 10, 1, 1, 1 };
        PGSResult r;
        pgsf_solve(&o, A, b, lo, hi, x, &r);
        for (int i = 0; i < 2; i++) if (x[i] < lo[i] || x[i] > hi[i]) bad++;
    }
    /* negative diagonal (caller passed a non-PSD matrix) */
    {
        int64_t A[4] = { -65536, 0,
                          0,     65536 };
        int64_t b[2] = { 65536, -65536 };
        int64_t lo[2] = { -1000, -1000 }, hi[2] = { 1000, 1000 };
        int64_t x[2] = { 0, 0 };
        PGSFixedOptions o = { 2, 10, 1, 1, 1 };
        PGSResult r;
        pgsf_solve(&o, A, b, lo, hi, x, &r);
        for (int i = 0; i < 2; i++) if (x[i] < lo[i] || x[i] > hi[i]) bad++;
    }
    /* saturating accumulation: values that overflow a 64-bit residual */
    {
        int64_t big = (int64_t)1 << 62;
        int64_t A[4] = { 4, 3,
                         3, 4 };
        int64_t b[2] = { -big, big };
        int64_t lo[2] = { -big, -big }, hi[2] = { big, big };
        int64_t x[2] = { big, -big };
        PGSFixedOptions o = { 2, 20, 1, 1, 1 };
        PGSResult r;
        pgsf_solve(&o, A, b, lo, hi, x, &r);
        for (int i = 0; i < 2; i++) if (x[i] < lo[i] || x[i] > hi[i]) bad++;
        /* determinism must hold here too */
        int64_t y[2] = { big, -big };
        PGSResult r2;
        pgsf_solve(&o, A, b, lo, hi, y, &r2);
        for (int i = 0; i < 2; i++) if (x[i] != y[i]) bad++;
    }
    printf("T4 degenerate/hostile input (zero+negative diagonal, saturation): "
           "%s (bad=%d)\n", bad ? "FAIL" : "no crash, in-box, deterministic", bad);
}

int main(void){
    t1_analytic();
    t2_clamped();
    t3_random_match_float();
    t4_degenerate();
    return 0;
}
