#include "fx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
/* Exact rational arithmetic (fixed point: every value is a fraction) */
/* ------------------------------------------------------------------ */

Fx fx_from_ll(long long v){ Fx r; r.num=v; r.den=1; return r; }

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
/* Build a reduced fraction from (n,d).  Every Fx in the solver is created by
 * fx_mk / fx_from_ll / fx_zalloc, so den>0 and no den==0 ever reaches the
 * arithmetic below (no per-call cleanup branch needed in the hot loop). */
static Fx fx_mk(long long n, long long d){
    Fx r;
    if(n==0){ r.num=0; r.den=1; return r; }
    if(d<0){ n=-n; d=-d; }
    long long g=ll_gcd(n,d);
    r.num=n/g; r.den=d/g;
    return r;
}
/* Clean a stored value (reader/var-transform path only; the hot loop never
 * sees a den==0 entry because all solver arrays are fx_zalloc'd to 0/1). */
static Fx fx_cln(Fx a){ if(a.den==0){ a.num=0; a.den=1; } return a; }
/* Allocate an Fx array initialized to exactly 0/1 (instead of calloc's 0/0),
 * so the fast arithmetic never needs the den==0 cleanup branch.  Overflow of
 * n*sizeof(Fx) is checked (this also silences -Walloc-size-larger-than, which
 * cannot bound the table dimensions M,K itself). */
static Fx *fx_zalloc(size_t n){
    if(n==0) n=1;
    size_t bytes;
    if(__builtin_mul_overflow(n, sizeof(Fx), &bytes)) return NULL;
    Fx *a=(Fx*)malloc(bytes);
    if(a){ for(size_t i=0;i<n;i++){ a[i].num=0; a[i].den=1; } }
    return a;
}
static inline Fx fx_neg(Fx a){ a.num=-a.num; return a; }
static inline Fx fx_add(Fx a, Fx b){ return fx_mk((long long)((__int128)a.num*b.den + (__int128)b.num*a.den), (long long)((__int128)a.den*b.den)); }
static inline Fx fx_sub(Fx a, Fx b){ return fx_mk((long long)((__int128)a.num*b.den - (__int128)b.num*a.den), (long long)((__int128)a.den*b.den)); }
static inline Fx fx_mul(Fx a, Fx b){ return fx_mk((long long)((__int128)a.num*b.num), (long long)((__int128)a.den*b.den)); }
static inline Fx fx_div(Fx a, Fx b){ return fx_mk((long long)((__int128)a.num*b.den), (long long)((__int128)a.den*b.num)); }
static inline int fx_cmp(Fx a, Fx b){ __int128 l=(__int128)a.num*b.den, r=(__int128)b.num*a.den; return l<r?-1:(l>r?1:0); }
static inline int fx_zero(Fx a){ return a.num==0; }

double fx_todouble(Fx r){ return (double)r.num/(double)r.den; }

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
        while(isdigit((unsigned char)*p)){ fp=fp*10+(*p-'0'); frlen++; p++; }
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
    *out=fx_mk((long long)num,den);
    return 0;
}

/* Format an exact value as a decimal with `prec` fractional digits. */
int fx_fmt(char*buf,int buflen,Fx r,int prec){
    /* handle sign */
    int neg = r.num<0; if(neg) r.num=-r.num;
    long long ip = r.num/r.den; r.num %= r.den;
    int n=0;
    if(neg && buflen>1){ buf[n++]='-'; }
    /* integer part */
    char ib[32]; int il=0; long long t=ip;
    if(t==0) ib[il++]='0';
    while(t>0){ ib[il++]=(char)('0'+(t%10)); t/=10; }
    while(il>0 && n<buflen-1) buf[n++]=ib[--il];
    if(prec>0 && n<buflen-1){ buf[n++]='.'; }
    for(int k=0;k<prec && n<buflen-1;k++){
        r.num*=10; long long dg=r.num/r.den; r.num%=r.den;
        buf[n++]=(char)('0'+dg);
    }
    buf[n]='\0';
    return n;
}

/* ------------------------------------------------------------------ */
/* LP reader (exact rational; same format as lp_read)                 */
/* ------------------------------------------------------------------ */

#define FX_INF_SENT  0x7fffffffffffffffLL

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
/* Exact two-phase full-tableau simplex                               */
/* ------------------------------------------------------------------ */

typedef struct {
    int M;         /* constraint rows */
    int K;         /* total variables = V y-vars + M artificials */
    int V;         /* number of y variables */
    Fx *T;         /* (M+1) x (K+1) row-major; row M = objective */
    int *basis;    /* M */
    int *activeVar;/* K */
    int *activeRow;/* M */
    int *isart;    /* K: artificial flag */
} Tab;

static Fx tab(const Tab*t,int r,int c){ return t->T[(size_t)r*(t->K+1)+c]; }
static void tabset(Tab*t,int r,int c,Fx v){ t->T[(size_t)r*(t->K+1)+c]=v; }

/* pivot on column j, leaving row r (0..M-1).  Updates rows 0..M (M=obj). */
static void pivot(Tab*t,int r,int j){
    Fx p=tab(t,r,j);
    int K=t->K, M=t->M, S=K+1;
    Fx *T=t->T;
    Fx *Tr=&T[(size_t)r*S];
    /* normalize pivot row */
    for(int c=0;c<=K;c++) Tr[c]=fx_div(Tr[c],p);
    /* eliminate rows i != r (including the objective row M).  The inner loop
     * is the hot spot of the exact solver: skip the (common) zero entries of
     * the normalized pivot row so we do no gcd-reduced work on no-ops. */
    for(int i=0;i<=M;i++){
        if(i==r)continue;
        Fx *Ti=&T[(size_t)i*S];
        Fx f=Ti[j];
        if(f.num==0)continue;
        for(int c=0;c<=K;c++){
            Fx b=Tr[c];
            if(b.num==0)continue;
            Ti[c]=fx_sub(Ti[c],fx_mul(f,b));
        }
    }
    t->basis[r]=j;
}

/* simplex maximize with Bland's rule over active vars/rows.  objrow is the
 * objective row index M.  Returns 0 optimal, 2 unbounded, 3 iteration limit. */
/* Maximize the objective stored in tableau row M, in the convention
 *   z + sum_{nonbasic j} T[M][j] x_j = T[M][K]      (T[M][j] = -rc_j).
 * An entering nonbasic variable has T[M][j] < 0; the optimum is reached when
 * every active nonbasic variable has T[M][j] >= 0.  The final value is z =
 * T[M][K].  Bland's rule (lowest-index entering, lowest-basic leaving on ratio
 * ties) guarantees termination in exact arithmetic.  Returns 0 optimal, 2
 * unbounded, 3 iteration limit; *iters receives the pivot count. */
static int simplex(Tab*t,long itercap,long*iters){
    int M=t->M, K=t->K;
    long niter=0;
    /* Entering rule: Dantzig (most-negative reduced cost) converges in far
     * fewer pivots than Bland's rule, but can cycle on degenerate problems.
     * In exact arithmetic the objective z=T[M][K] is non-decreasing, so a long
     * run of pivots that never improve z signals cycling -> fall back to Bland
     * (smallest-index entering), which is guaranteed to terminate. */
    int use_bland=0;
    long stale=0;
    for(;;){
        int ej=-1;
        if(!use_bland){
            for(int j=0;j<K;j++){
                if(!t->activeVar[j])continue;
                int basic=0; for(int r=0;r<M;r++) if(t->basis[r]==j){basic=1;break;}
                if(basic)continue;
                if(fx_cmp(tab(t,M,j),fx_from_ll(0))<0){
                    if(ej<0||fx_cmp(tab(t,M,j),tab(t,M,ej))<0) ej=j;
                }
            }
        } else {
            for(int j=0;j<K;j++){
                if(!t->activeVar[j])continue;
                int basic=0; for(int r=0;r<M;r++) if(t->basis[r]==j){basic=1;break;}
                if(basic)continue;
                if(fx_cmp(tab(t,M,j),fx_from_ll(0))<0){ ej=j; break; }
            }
        }
        *iters=niter;
        if(ej<0) return 0;   /* optimal */
        int lr=-1; Fx best;
        for(int r=0;r<M;r++){
            if(!t->activeRow[r])continue;
            Fx a=tab(t,r,ej);
            if(fx_cmp(a,fx_from_ll(0))<=0)continue;
            Fx ratio=fx_div(tab(t,r,K),a);
            if(lr<0||fx_cmp(ratio,best)<0||(fx_cmp(ratio,best)==0&&t->basis[r]<t->basis[lr])){
                lr=r; best=ratio;
            }
        }
        if(lr<0) return 2;   /* unbounded */
        Fx zbefore=tab(t,M,K);
        pivot(t,lr,ej);
        if(fx_cmp(tab(t,M,K),zbefore)==0) stale++; else stale=0;
        if(stale > (long)(t->V + M + 2)) use_bland=1;
        niter++;
        if(niter>=itercap){ *iters=niter; return 3; }
    }
}

void fx_result_free(FxResult*res){
    if(!res)return;
    free(res->x); res->x=NULL;
}

int fx_solve(const FxLP*lp, FxResult*res){
    memset(res,0,sizeof(*res));
    int n=lp->n, m=lp->m;

    /* ---- transform variables: x_j = base_j + sign_j * y_j, y_j>=0 ---- */
    Fx *base=fx_zalloc((size_t)(n?n:1));
    int *sign=(int*)calloc((size_t)(n?n:1),sizeof(int));
    int *yidx=(int*)malloc((size_t)(n?n:1)*sizeof(int));  /* -1 = fixed */
    for(int j=0;j<n;j++){ yidx[j]=-1; }
    int V=0, ubrows=0;
    for(int j=0;j<n;j++){
        if(lp->lfinite[j]&&lp->ufinite[j]&&fx_cmp(lp->l[j],lp->u[j])==0){
            /* fixed variable: fold into constants (no y var) */
            base[j]=fx_cln(lp->l[j]); sign[j]=1; yidx[j]=-1;
        } else {
            int use_lo=lp->lfinite[j];
            base[j]= use_lo ? fx_cln(lp->l[j]) : fx_cln(lp->u[j]);
            sign[j]= use_lo ? 1 : -1;
            yidx[j]=V++;
            if(use_lo&&lp->ufinite[j]) ubrows++;   /* add upper-bound row */
        }
    }
    int M=m+ubrows;

    /* build M rows: y-coefficients (M x V), rhs, relation.  Rows with rhs<0
       are negated (flipping the relation) so every rhs >= 0. */
    Fx *A=fx_zalloc((size_t)(M?(size_t)M*V:1));
    Fx *b=fx_zalloc((size_t)(M?M:1));
    char *rel=(char*)malloc((size_t)(M?M:1));
    int row=0;
    for(int i=0;i<m;i++){
        Fx rhs=lp->b[i]; char r=lp->rel[i];
        for(int j=0;j<n;j++){
            Fx a=lp->A[(size_t)i*n+j];
            if(fx_zero(a))continue;
            if(yidx[j]<0){ rhs=fx_sub(rhs,fx_mul(a,base[j])); }
            else { A[(size_t)row*V+yidx[j]]=fx_mul(a,fx_from_ll(sign[j])); rhs=fx_sub(rhs,fx_mul(a,base[j])); }
        }
        if(fx_cmp(rhs,fx_from_ll(0))<0){
            for(int j=0;j<V;j++) A[(size_t)row*V+j]=fx_neg(A[(size_t)row*V+j]);
            rhs=fx_neg(rhs);
            if(r=='<')r='>'; else if(r=='>')r='<';
        }
        b[row]=rhs; rel[row]=r; row++;
    }
    for(int j=0;j<n;j++){
        if(yidx[j]<0)continue;
        if(lp->lfinite[j]&&lp->ufinite[j]){
            A[(size_t)row*V+yidx[j]]=fx_from_ll(1);
            b[row]=fx_sub(lp->u[j],lp->l[j]); rel[row]='<'; row++;
        }
    }

    /* count slack/surplus vars (one per non-'=' row) */
    int S=0;
    for(int r=0;r<M;r++) if(rel[r]!='=') S++;
    /* slack column index per row (or -1); use calloc (recognized by the
       compiler, so no -Walloc-size-larger-than, and overflow-safe) */
    int *slackcol=(int*)calloc((size_t)(M?M:1),sizeof(int));
    if(!slackcol){ res->status=2; free(base);free(sign);free(yidx);free(A);free(b);free(rel);
        return 2; }
    { int cur=V; for(int r=0;r<M;r++){ if(rel[r]!='='){ slackcol[r]=cur++; } else slackcol[r]=-1; } }

    /* objective constant and internal linear coefficients */
    int sense_mult = lp->maximize ? 1 : -1;
    Fx C0=fx_from_ll(0);
    for(int j=0;j<n;j++) C0=fx_add(C0,fx_mul(lp->c[j],base[j]));
    Fx *coef=fx_zalloc((size_t)(V?V:1));
    for(int j=0;j<n;j++) if(yidx[j]>=0)
        coef[yidx[j]]=fx_mul(lp->c[j],fx_from_ll((long long)(sense_mult*sign[j])));

    /* ---- tableau: y vars [0,V), slack [V,V+S), artificial [V+S,V+S+M) ---- */
    int K=V+S+M;
    Tab t; memset(&t,0,sizeof(t));
    t.M=M; t.K=K; t.V=V;
    t.T=fx_zalloc((size_t)((size_t)(M+1)*(size_t)(K+1)));
    t.basis=(int*)malloc((size_t)(M?M:1)*sizeof(int));
    t.activeVar=(int*)malloc((size_t)(K?K:1)*sizeof(int));
    t.activeRow=(int*)malloc((size_t)(M?M:1)*sizeof(int));
    t.isart=(int*)calloc((size_t)(K?K:1),sizeof(int));
    for(int j=0;j<K;j++)t.activeVar[j]=1;
    for(int r=0;r<M;r++)t.activeRow[r]=1;

    for(int r=0;r<M;r++){
        for(int j=0;j<V;j++) tabset(&t,r,j,A[(size_t)r*V+j]);
        if(slackcol[r]>=0) tabset(&t,r,slackcol[r], fx_from_ll(rel[r]=='<'?1:-1));
        int av=V+S+r;
        tabset(&t,r,av,fx_from_ll(1)); t.isart[av]=1;
        tabset(&t,r,K,b[r]);
        t.basis[r]=av;
    }

    long iters=0; int status=0;
    /* ---- Phase I: maximize -sum(artificials); objective row stores -rc.
           With all artificials basic (cB=-1) and c_j=-1 for artificials:
             rc_j = sum_r T[r][j] (y/slack),  rc_art=0,  z0 = -sum_r T[r][K]. */
    for(int j=0;j<K;j++){
        if(t.isart[j]){ tabset(&t,M,j,fx_from_ll(0)); continue; }
        Fx s=fx_from_ll(0); for(int r=0;r<M;r++) s=fx_add(s,tab(&t,r,j));
        tabset(&t,M,j,fx_neg(s));
    }
    Fx z0=fx_from_ll(0); for(int r=0;r<M;r++) z0=fx_sub(z0,tab(&t,r,K));
    tabset(&t,M,K,z0);

    long capI = 10000 + 200L*(M+V+S);
    long sub=0;
    status=simplex(&t,capI,&sub); iters+=sub;
    if(status==3){ res->status=3; goto done; }
    if(status==2){ res->status=3; goto done; }
    if(fx_cmp(tab(&t,M,K),fx_from_ll(0))!=0){ res->status=1; goto done; }

    /* feasible.  Clean up: pivot basic artificials out with a real var, else
       drop the redundant row. */
    for(int a=0;a<M;a++){
        int av=V+S+a;
        int r=-1; for(int rr=0;rr<M;rr++) if(t.basis[rr]==av){r=rr;break;}
        if(r<0)continue;
        int ej=-1;
        for(int j=0;j<V+S;j++) if(t.activeVar[j] && !fx_zero(tab(&t,r,j))){ ej=j; break; }
        if(ej>=0){ pivot(&t,r,ej); }
        else { t.activeRow[r]=0; t.activeVar[av]=0; }
    }
    for(int a=0;a<M;a++) t.activeVar[V+S+a]=0;

    /* ---- Phase II: maximize coef over y (artificials gone) ---- */
    for(int j=0;j<K;j++){
        if(!t.activeVar[j]){ tabset(&t,M,j,fx_from_ll(0)); continue; }
        Fx rc= fx_from_ll(0);
        for(int r=0;r<M;r++){
            if(!t.activeRow[r])continue;
            int bv=t.basis[r];
            Fx cB = (bv<V)? coef[bv] : fx_from_ll(0);
            rc=fx_sub(rc,fx_mul(cB,tab(&t,r,j)));
        }
        if(j<V) rc=fx_add(rc,coef[j]);
        tabset(&t,M,j,fx_neg(rc));
    }
    Fx z02=fx_from_ll(0);
    for(int r=0;r<M;r++){ if(!t.activeRow[r])continue; int bv=t.basis[r]; Fx cB=(bv<V)?coef[bv]:fx_from_ll(0); z02=fx_add(z02,fx_mul(cB,tab(&t,r,K))); }
    tabset(&t,M,K,z02);

    long capII = 10000 + 200L*(V+M+S);
    long sub2=0;
    status=simplex(&t,capII,&sub2); iters+=sub2;
    if(status==3){ res->status=3; goto done; }
    if(status==2){ res->status=2; goto done; }

    /* optimal */
    Fx maxval=tab(&t,M,K);
    res->obj = (sense_mult==1)? fx_add(C0,maxval) : fx_sub(C0,maxval);
    res->iters=iters;
    res->status=0;
    res->x=(Fx*)calloc((size_t)(n?n:1),sizeof(Fx));
    for(int j=0;j<n;j++){
        if(yidx[j]<0){ res->x[j]=base[j]; }
        else {
            int k=yidx[j];
            Fx yv=fx_from_ll(0);
            for(int r=0;r<M;r++) if(t.basis[r]==k){ yv=tab(&t,r,K); break; }
            res->x[j]=fx_add(base[j],fx_mul(fx_from_ll(sign[j]),yv));
        }
    }
done:
    free(base);free(sign);free(yidx);free(A);free(b);free(rel);free(slackcol);free(coef);
    free(t.T);free(t.basis);free(t.activeVar);free(t.activeRow);free(t.isart);
    return res->status;
}
