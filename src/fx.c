#include "fx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/* Exact rational arithmetic (fixed point: every value is a fraction)  */
/*                                                                     */
/* The public Fx type is an int64 numerator/denominator pair.  The     */
/* simplex core (fx_core.inc) is instantiated twice — once on int64    */
/* (fast path, __int128 intermediates) and once on __int128 (wide      */
/* retry) — and *every* operation is overflow-checked.  Before this,   */
/* products were computed in __int128 and truncated back to int64      */
/* without a check, so coefficient growth silently produced wrong      */
/* "exact" answers (an infeasible LP could be reported OPTIMAL 0) or   */
/* thrashing that ended in a bogus ITERATION_LIMIT.                    */
/* ------------------------------------------------------------------ */

Fx fx_from_ll(long long v){ Fx r; r.num=v; r.den=1; return r; }

int fx_from_double(double v, Fx *out){
    if(!isfinite(v)) return -1;
    double rv = rint(v);
    if(fabs(v - rv) > 1e-9) return -1;      /* only exactly-integral values */
    if(rv < -9.2e18 || rv > 9.2e18) return -1;
    out->num = (long long)rv; out->den = 1;
    return 0;
}

double fx_todouble(Fx r){ return (double)r.num/(double)r.den; }

const char *fx_status_name(int status){
    switch(status){
    case FX_OPTIMAL:    return "OPTIMAL";
    case FX_INFEASIBLE: return "INFEASIBLE";
    case FX_UNBOUNDED:  return "UNBOUNDED";
    case FX_ITER_LIMIT: return "ITERATION_LIMIT";
    case FX_OVERFLOW:   return "OVERFLOW";
    case FX_ALLOC_FAIL: return "OUT_OF_MEMORY";
    default:            return "UNKNOWN";
    }
}

/* Euclidean GCD on unsigned magnitudes.  (A binary/Stein GCD is measurably
 * *slower* here because the compiler's single hardware `%` beats its
 * shift/subtract loop; the exact solver spends ~all its time in gcd, so this
 * matters.) */
static long long ll_gcd(long long a, long long b){
    unsigned long long u = a<0 ? (unsigned long long)(-(a+1))+1ULL : (unsigned long long)a;
    unsigned long long v = b<0 ? (unsigned long long)(-(b+1))+1ULL : (unsigned long long)b;
    while(v){ unsigned long long t=u%v; u=v; v=t; }
    return (long long)u;
}
/* Build a reduced public fraction (reader path only; the solve cores have
 * their own checked constructors). */
static Fx fx_mk(long long n, long long d){
    Fx r;
    if(n==0){ r.num=0; r.den=1; return r; }
    if(d<0){ n=-n; d=-d; }
    long long g=ll_gcd(n,d);
    r.num=n/g; r.den=d/g;
    return r;
}

/* Parse a decimal / integer / exponent string into an exact Fx.
 * Returns 0 on success, -1 on malformed input or overflow. */
static int fx_from_str(const char*s, Fx*out){
    const char*p=s;
    long long sign=1;
    while(*p==' ')p++;
    if(*p=='-'){sign=-1;p++;}
    else if(*p=='+'){p++;}
    if(!isdigit((unsigned char)*p)) return -1;
    /* integer part */
    long long ip=0;
    while(isdigit((unsigned char)*p)){ if(ip>((long long)9e17))return -1; ip=ip*10+(*p-'0'); p++; }
    long long fp=0; int frlen=0;
    if(*p=='.'){ p++;
        while(isdigit((unsigned char)*p)){
            if(frlen>=18) { p++; continue; }   /* ignore digits beyond int64 range */
            fp=fp*10+(*p-'0'); frlen++; p++;
        }
    }
    int exp=0;
    if(*p=='e'||*p=='E'){ p++; int es=1;
        if(*p=='-'){es=-1;p++;} else if(*p=='+'){p++;}
        if(!isdigit((unsigned char)*p))return -1;
        while(isdigit((unsigned char)*p)){ exp=exp*10+(*p-'0'); if(exp>9)exp=9; p++; }
        exp*=es;
    }
    if(*p!='\0'&&*p!=';'&&*p!=' ') return -1;
    /* build num = (ip*10^frlen + fp)*sign, den = 10^frlen, then 10^exp */
    long long pow10=1;
    for(int i=0;i<frlen;i++){ if(pow10>9000000000000000000LL)return -1; pow10*=10; }
    __int128 num=(__int128)ip*pow10+fp;
    long long den=pow10;
    if(exp>=0){
        for(int i=0;i<exp;i++){ if(num>((__int128)1<<120))return -1; num*=10; }
    } else {
        for(int i=0;i<-exp;i++){ if(den>((long long)1)<<60)return -1; den*=10; }
    }
    num*=sign;
    if(num > (__int128)0x7fffffffffffffffLL || num < -(__int128)0x7fffffffffffffffLL) return -1;
    *out=fx_mk((long long)num,den);
    return 0;
}

/* Format an exact value as a decimal with `prec` fractional digits.
 * The long division runs in __int128: with an int64 denominator the old
 * `r.num *= 10` overflowed and emitted junk characters (a digit computed as
 * -1 printed as '/'), so "exact" values could print as garbage. */
int fx_fmt(char*buf,int buflen,Fx r,int prec){
    if(buflen<=0) return 0;
    if(r.den==0){ if(buflen>3){ buf[0]='n';buf[1]='a';buf[2]='n';buf[3]=0; return 3;} buf[0]=0; return 0; }
    __int128 num=r.num, den=r.den;
    if(den<0){ num=-num; den=-den; }
    int neg = num<0; if(neg) num=-num;
    __int128 ip = num/den; num %= den;
    int n=0;
    if(neg && buflen>1){ buf[n++]='-'; }
    /* integer part */
    char ib[48]; int il=0; __int128 t=ip;
    if(t==0) ib[il++]='0';
    while(t>0 && il<(int)sizeof(ib)){ ib[il++]=(char)('0'+(int)(t%10)); t/=10; }
    while(il>0 && n<buflen-1) buf[n++]=ib[--il];
    if(prec>0 && n<buflen-1){ buf[n++]='.'; }
    for(int k=0;k<prec && n<buflen-1;k++){
        num*=10; int dg=(int)(num/den); num%=den;
        buf[n++]=(char)('0'+dg);
    }
    buf[n]='\0';
    return n;
}

/* ------------------------------------------------------------------ */
/* LP reader (exact rational; same format as lp_read)                 */
/* ------------------------------------------------------------------ */

int fx_read(const char*path, FxLP*lp){
    memset(lp,0,sizeof(*lp));
    FILE*f=fopen(path,"r");
    if(!f){ fprintf(stderr,"cannot open %s\n",path); return -1; }
    char sense[32];
    if(fscanf(f,"%31s",sense)!=1) goto err;
    if(strcmp(sense,"max")==0||strcmp(sense,"maximize")==0) lp->maximize=1;
    else if(strcmp(sense,"min")==0||strcmp(sense,"minimize")==0) lp->maximize=0;
    else { fprintf(stderr,"invalid objective sense '%s'\n",sense); goto err; }
    int n,m;
    if(fscanf(f,"%d %d",&n,&m)!=2) goto err;
    if(n<=0||m<0||n>1000000||m>1000000){ fprintf(stderr,"invalid dims n=%d m=%d\n",n,m); goto err; }
    lp->n=n; lp->m=m;
    lp->c=(Fx*)calloc((size_t)n,sizeof(Fx));
    lp->b=(Fx*)calloc((size_t)(m?m:1),sizeof(Fx));
    lp->l=(Fx*)calloc((size_t)n,sizeof(Fx));
    lp->u=(Fx*)calloc((size_t)n,sizeof(Fx));
    lp->lfinite=(int*)calloc((size_t)n,sizeof(int));
    lp->ufinite=(int*)calloc((size_t)n,sizeof(int));
    lp->rel=(char*)malloc((size_t)(m?m:1));
    lp->A=(Fx*)calloc((size_t)(m?(size_t)m*n:1),sizeof(Fx));
    if(!lp->c||!lp->b||!lp->l||!lp->u||!lp->lfinite||!lp->ufinite||!lp->rel||!lp->A) goto err;
    char tok[80];
    for(int j=0;j<n;j++){ if(fscanf(f,"%79s",tok)!=1||fx_from_str(tok,&lp->c[j])!=0) goto err; }
    for(int i=0;i<m;i++){ if(fscanf(f,"%79s",tok)!=1||fx_from_str(tok,&lp->b[i])!=0) goto err; }
    if(m>0){
        char relbuf[1024]; int c;
        do{ c=fgetc(f); }while(c!=EOF&&isspace(c));
        size_t pos=0;
        for(;c!=EOF&&!isspace(c);c=fgetc(f)){ if(pos<1023)relbuf[pos++]=(char)c; }
        relbuf[pos]=0;
        if(pos!=(size_t)m) goto err;
        for(int i=0;i<m;i++){ if(relbuf[i]!='<'&&relbuf[i]!='>'&&relbuf[i]!='='){ fprintf(stderr,"bad rel %d\n",i);goto err;} lp->rel[i]=relbuf[i]; }
    }
    for(int j=0;j<n;j++){
        char lo[80],hi[80];
        if(fscanf(f,"%79s %79s",lo,hi)!=2) goto err;
        if(strcmp(lo,"inf")==0||strcmp(lo,"+inf")==0){ lp->lfinite[j]=0; lp->l[j].num=FX_INF_SENT; }
        else if(strcmp(lo,"-inf")==0){ lp->lfinite[j]=0; lp->l[j].num=-FX_INF_SENT; }
        else { if(fx_from_str(lo,&lp->l[j])!=0)goto err; lp->lfinite[j]=1; }
        if(strcmp(hi,"inf")==0||strcmp(hi,"+inf")==0){ lp->ufinite[j]=0; lp->u[j].num=FX_INF_SENT; }
        else if(strcmp(hi,"-inf")==0){ lp->ufinite[j]=0; lp->u[j].num=-FX_INF_SENT; }
        else { if(fx_from_str(hi,&lp->u[j])!=0)goto err; lp->ufinite[j]=1; }
        if(!lp->lfinite[j]&&!lp->ufinite[j]){ fprintf(stderr,"free variable (both sides unbounded) at col %d not supported\n",j); goto err; }
    }
    long nnz;
    if(fscanf(f,"%ld",&nnz)!=1) goto err;
    if(nnz<0||nnz>100000000L) goto err;
    for(long k=0;k<nnz;k++){
        int r,c; char v[80];
        if(fscanf(f,"%d %d %79s",&r,&c,v)!=3) goto err;
        if(r<0||r>=m||c<0||c>=n){ fprintf(stderr,"triplet out of range\n"); goto err; }
        if(fx_from_str(v,&lp->A[(size_t)r*n+c])!=0) goto err;
    }
    fclose(f);
    return 0;
err:
    fclose(f);
    fprintf(stderr,"FX LP parse error\n");
    fx_free(lp);
    return -1;
}

void fx_free(FxLP*lp){
    if(!lp) return;
    free(lp->c);free(lp->b);free(lp->l);free(lp->u);
    free(lp->lfinite);free(lp->ufinite);free(lp->rel);free(lp->A);
    memset(lp,0,sizeof(*lp));
}

/* ------------------------------------------------------------------ */
/* Exact two-phase full-tableau simplex, instantiated at two widths    */
/* ------------------------------------------------------------------ */

#define FXP(x)      fx64_##x
#define FXNUM       long long
#define FXUNUM      unsigned long long
#define FXWIDE      __int128
#define FX_HAS_WIDE 1
#include "fx_core.inc"
#undef FXP
#undef FXNUM
#undef FXUNUM
#undef FXWIDE
#undef FX_HAS_WIDE

#define FXP(x)      fx128_##x
#define FXNUM       __int128
#define FXUNUM      unsigned __int128
#define FX_HAS_WIDE 0
#include "fx_core.inc"
#undef FXP
#undef FXNUM
#undef FXUNUM
#undef FX_HAS_WIDE

void fx_result_free(FxResult*res){
    if(!res)return;
    free(res->x); res->x=NULL;
}

/* Solve exactly.  Fast path: int64 rationals.  If the exact arithmetic runs
 * out of range (which used to wrap silently and produce a wrong answer), the
 * whole solve is retried with 128-bit rationals, which covers the coefficient
 * growth of any practically sized model.  Only if *that* overflows do we
 * report FX_OVERFLOW — "no answer", never a wrong one. */
int fx_solve_wide(const FxLP*lp, FxResult*res){
    if(!res) return FX_ALLOC_FAIL;
    memset(res,0,sizeof(*res));
    if(!lp || lp->n <= 0 || lp->m < 0){ res->status = FX_INFEASIBLE; return FX_INFEASIBLE; }
    if(!lp->c || !lp->l || !lp->u || !lp->lfinite || !lp->ufinite || (lp->m > 0 && (!lp->b || !lp->rel || !lp->A))){
        res->status = FX_INFEASIBLE; return FX_INFEASIBLE;
    }
    int st = fx128_solve(lp, res);
    res->width = 128;
    return st;
}

int fx_solve(const FxLP*lp, FxResult*res){
    if(!res) return FX_ALLOC_FAIL;
    memset(res,0,sizeof(*res));
    if(!lp || lp->n <= 0 || lp->m < 0){ res->status = FX_INFEASIBLE; return FX_INFEASIBLE; }
    if(!lp->c || !lp->l || !lp->u || !lp->lfinite || !lp->ufinite || (lp->m > 0 && (!lp->b || !lp->rel || !lp->A))){
        res->status = FX_INFEASIBLE; return FX_INFEASIBLE;
    }
    int st = fx64_solve(lp, res);
    res->width = 64;
    if(st == FX_OVERFLOW){
        fx_result_free(res);
        memset(res,0,sizeof(*res));
        st = fx128_solve(lp, res);
        res->width = 128;
    }
    return st;
}
