/* ===================================================================== */
/* fz_cp.inc - Finite-domain constraint-propagation + backtracking       */
/* search engine for FlatZinc integer satisfaction (CSP) models.          */
/*                                                                        */
/* Included into fzn.c (after the lexer/parser/linear helpers).  It is    */
/* engaged by fz_solve for `solve satisfy` models whose constraints are   */
/* all understood by the CP engine and which contain at least one         */
/* combinatorial / nonlinear predicate that the LP/MIP bridge handles     */
/* poorly (all_different, or a product/div/mod/pow between variables).    */
/*                                                                        */
/* It solves finite-domain integer CSPs that LP-based branch-and-bound    */
/* struggles with (n-Queens, Sudoku, magic/latin squares, ...) and, via   */
/* factorization, monomial Diophantine products like x*x*x*y*y = 100000.  */
/*                                                                        */
/* Correctness contract:                                                  */
/*   - propagation only removes values that no solution can use;          */
/*   - search enumerates finite domains exhaustively;                     */
/*   - UNSAT is reported only when every integer decision variable was    */
/*     bounded to a finite domain (otherwise we return to the MIP bridge, */
/*     which honestly reports UNKNOWN for unbounded models).              */
/*                                                                        */
/* Variables use solver indices 0..nvars-1 (base declared indices),       */
/* matching fz_print_one_solution / fz_decl_val_at.                       */
/* ===================================================================== */

#ifndef PSOLVE_FZ_CP_INC
#define PSOLVE_FZ_CP_INC

#define CP_MAXVALS   65536L
#define CP_MAXNODES  2000000L
#define CP_BIG       ((int64_t)INT64_C(1)<<50)

typedef struct {
    int     nvars;
    int    *bounded;    /* 1 = explicit finite value list                 */
    int    *isbool;
    int64_t *lo, *hi;
    int64_t **vals;
    int     *nvals, *cap;
    long     nodes;
} CP;

static inline int64_t cp_dmin(const CP*cp,int v){ return cp->bounded[v]?cp->vals[v][0]:cp->lo[v]; }
static inline int64_t cp_dmax(const CP*cp,int v){ return cp->bounded[v]?cp->vals[v][cp->nvals[v]-1]:cp->hi[v]; }

static int cp_contains(const CP*cp,int v,int64_t val){
    if(!cp->bounded[v]) return val>=cp->lo[v] && val<=cp->hi[v];
    int lo=0,hi=cp->nvals[v]-1;
    while(lo<=hi){ int mid=(lo+hi)/2; if(cp->vals[v][mid]==val) return 1; if(cp->vals[v][mid]<val) lo=mid+1; else hi=mid-1; }
    return 0;
}

/* sort int64 (qsort helper) */
static int cp_cmp_i64(const void*a,const void*b){ int64_t x=*(const int64_t*)a,y=*(const int64_t*)b; return (x>y)-(x<y); }

static int cp_set_vals(CP*cp,int v,int64_t *vals,int n){
    if(n>CP_MAXVALS) return -1;
    if(!cp->bounded[v]){ cp->vals[v]=(int64_t*)psolve_malloc((size_t)(n?n:1)*sizeof(int64_t)); cp->cap[v]=n?n:1; cp->bounded[v]=1; }
    else if(cp->cap[v]<n){ cp->vals[v]=(int64_t*)psolve_realloc((void**)&cp->vals[v],(size_t)(n?n:1)*sizeof(int64_t)); cp->cap[v]=n?n:1; }
    for(int i=0;i<n;i++) cp->vals[v][i]=vals[i];
    /* sort + dedupe so binary search and min/max are correct */
    qsort(cp->vals[v],(size_t)n,sizeof(int64_t),cp_cmp_i64);
    int w=0; for(int i=0;i<n;i++) if(w==0||cp->vals[v][i]!=cp->vals[v][w-1]) cp->vals[v][w++]=cp->vals[v][i];
    cp->nvals[v]=w;
    cp->lo[v]= w?cp->vals[v][0]:INT64_MAX;
    cp->hi[v]= w?cp->vals[v][w-1]:INT64_MIN;
    return w?0:1;
}

static int cp_intersect(CP*cp,int v,int64_t nlo,int64_t nhi){
    if(nlo>nhi){ if(cp->bounded[v]) cp->nvals[v]=0; else { cp->lo[v]=1; cp->hi[v]=0; } return 1; }
    if(cp->bounded[v]){
        int w=0;
        for(int i=0;i<cp->nvals[v];i++) if(cp->vals[v][i]>=nlo && cp->vals[v][i]<=nhi) cp->vals[v][w++]=cp->vals[v][i];
        cp->nvals[v]=w;
        cp->lo[v]= w?cp->vals[v][0]:INT64_MAX;
        cp->hi[v]= w?cp->vals[v][w-1]:INT64_MIN;
        return w?0:1;
    } else {
        if(cp->lo[v]<nlo)cp->lo[v]=nlo;
        if(cp->hi[v]>nhi)cp->hi[v]=nhi;
        if(cp->lo[v]>cp->hi[v]) return 1;
        __int128 sz=(__int128)cp->hi[v]-cp->lo[v]+1;
        if(sz>=1 && sz<=CP_MAXVALS){
            int64_t *tmp=(int64_t*)psolve_malloc((size_t)sz*sizeof(int64_t));
            int w=0; for(int64_t q=cp->lo[v];q<=cp->hi[v];q++) tmp[w++]=q;
            cp->vals[v]=tmp; cp->cap[v]=(int)sz; cp->bounded[v]=1; cp->nvals[v]=w;
        }
        return 0;
    }
}

static int cp_remove_val(CP*cp,int v,int64_t val){
    if(!cp->bounded[v]){
        if(val==cp->lo[v]){ cp->lo[v]++; if(cp->lo[v]>cp->hi[v]){ cp->hi[v]=cp->lo[v]-1; return 1;} return 0; }
        if(val==cp->hi[v]){ cp->hi[v]--; if(cp->lo[v]>cp->hi[v]){ cp->lo[v]=cp->hi[v]+1; return 1;} return 0; }
        return 0;
    }
    int lo=0,hi=cp->nvals[v]-1;
    while(lo<=hi){ int mid=(lo+hi)/2;
        if(cp->vals[v][mid]==val){
            for(int k=mid;k<cp->nvals[v]-1;k++) cp->vals[v][k]=cp->vals[v][k+1];
            cp->nvals[v]--;
            cp->lo[v]= cp->nvals[v]?cp->vals[v][0]:INT64_MAX;
            cp->hi[v]= cp->nvals[v]?cp->vals[v][cp->nvals[v]-1]:INT64_MIN;
            return cp->nvals[v]?0:1;
        } else if(cp->vals[v][mid]<val) lo=mid+1; else hi=mid-1;
    }
    return 0;
}

static void cp_assign(CP*cp,int v,int64_t val){
    if(!cp->bounded[v]){ cp->vals[v]=(int64_t*)psolve_malloc(sizeof(int64_t)); cp->cap[v]=1; cp->bounded[v]=1; cp->nvals[v]=1; }
    else if(cp->cap[v]<1){ cp->vals[v]=(int64_t*)psolve_realloc((void**)&cp->vals[v],sizeof(int64_t)); cp->cap[v]=1; cp->nvals[v]=1; }
    cp->vals[v][0]=val; cp->nvals[v]=1;
    cp->lo[v]=cp->hi[v]=val;
}

static int cp_fixed(const CP*cp,int v){ return cp->bounded[v]? cp->nvals[v]==1 : cp->lo[v]==cp->hi[v]; }

static void cp_copy(CP*dst,const CP*src){
    dst->nvars=src->nvars; dst->nodes=src->nodes;
    dst->bounded=(int*)psolve_malloc((size_t)src->nvars*sizeof(int));
    dst->isbool=(int*)psolve_malloc((size_t)src->nvars*sizeof(int));
    dst->lo=(int64_t*)psolve_malloc((size_t)src->nvars*sizeof(int64_t));
    dst->hi=(int64_t*)psolve_malloc((size_t)src->nvars*sizeof(int64_t));
    dst->vals=(int64_t**)psolve_calloc((size_t)src->nvars,sizeof(int64_t*));
    dst->nvals=(int*)psolve_malloc((size_t)src->nvars*sizeof(int));
    dst->cap=(int*)psolve_malloc((size_t)src->nvars*sizeof(int));
    for(int v=0;v<src->nvars;v++){
        dst->bounded[v]=src->bounded[v]; dst->isbool[v]=src->isbool[v];
        dst->lo[v]=src->lo[v]; dst->hi[v]=src->hi[v]; dst->nvals[v]=src->nvals[v]; dst->cap[v]=src->cap[v];
        if(src->bounded[v]&&src->nvals[v]>0){
            dst->vals[v]=(int64_t*)psolve_malloc((size_t)src->nvals[v]*sizeof(int64_t));
            for(int i=0;i<src->nvals[v];i++) dst->vals[v][i]=src->vals[v][i];
        }
    }
}
static void cp_free(CP*cp){
    if(!cp) return;
    if(cp->vals){ for(int v=0;v<cp->nvars;v++) free(cp->vals[v]); }
    free(cp->bounded);free(cp->isbool);free(cp->lo);free(cp->hi);free(cp->vals);free(cp->nvals);free(cp->cap);
    memset(cp,0,sizeof(*cp));
}

/* ------------------------------------------------------------------ */
/* Constraint records                                                  */
/* ------------------------------------------------------------------ */
typedef struct { int v; int isc; int64_t c; } CPT;
typedef struct {
    int kind;      /* CPK_* */
    int n;
    int    *lvars;  long long *lcoef;  long long rhs; int rel;
    CPT   *terms;
    int    nset; long long *setvals;
    CPT   *arr_terms;  /* element array entries (CPT, const or var) */
} CPCR;

#define CPK_LIN 0
#define CPK_ALLDIFF 1
#define CPK_TIMES 2
#define CPK_DIV 3
#define CPK_MOD 4
#define CPK_ABS 5
#define CPK_ELEM 6
#define CPK_SETIN 7
#define CPK_POW 8
#define CPK_REIF 9     /* r <-> inner linear relation; bool var in terms[0], inner in lvars/lcoef/rhs/rel */
#define CPK_CLAUSE 10  /* disjunction of literals; pos bools in lvars, neg bools in terms (as terms[].v) */
#define CPK_MINMAX 11  /* m = min/max(a,b); terms[0]=a,terms[1]=b,terms[2]=m; n set to ismax flag */

static int cp_term_from_lin(const Lin*l,CPT*t){
    if(l->n==0 && fabs(l->constant)<=CP_BIG && l->constant==rint(l->constant)){ t->isc=1; t->c=(int64_t)l->constant; t->v=-1; return 0; }
    if(l->n==1 && fabs(l->coef[0]-1.0)<1e-9 && fabs(l->constant)<1e-9){ t->isc=0; t->v=l->idx[0]; t->c=0; return 0; }
    if(l->n==1 && fabs(l->coef[0]+1.0)<1e-9 && fabs(l->constant)<1e-9){ t->isc=0; t->v=l->idx[0]; t->c=1; return -2; }
    return -1;
}
static int cp_term_value(const CP*cp,const CPT*t,int64_t*out){
    if(t->isc){ *out=t->c; return 1; }
    if(cp_fixed(cp,t->v)){ *out=cp->vals[t->v][0]; return 1; }
    return 0;
}

/* Correct floor/ceil of integer division for any nonzero divisor b.
   floord(a,b): q = a/b trunc-toward-zero; if the quotient is non-integer and a
   and b have opposite signs, truncation rounded up, so subtract one to floor.
   ceild(a,b) = -floord(-a,b). */
static __int128 floord(__int128 a,__int128 b){ if(b==0) return a; __int128 q=a/b, r=a-q*b; if(r!=0 && ((a<0)!=(b<0))) q-=1; return q; }
static __int128 ceild(__int128 a,__int128 b){ if(b==0) return a; return -floord(-a,b); }

/* ------------------------------------------------------------------ */
/* Linear constraint propagation.                                      */
/* rel: 0 eq, 1 le, 2 ge, 3 lt (<= rhs-1), 4 gt (>= rhs+1), 5 ne.      */
/* Returns 1 if inconsistent.                                          */
/* ------------------------------------------------------------------ */
static int cp_prop_lin(CP*cp,int n,int*lvars,long long*lcoef,long long rhs,int rel){
    int nfree=0,lastfree=-1;
    __int128 fixed_sum=0;
    for(int i=0;i<n;i++){ int v=lvars[i]; long long a=lcoef[i]; if(cp_fixed(cp,v)) fixed_sum+=(__int128)a*cp->vals[v][0]; else if(a!=0){ nfree++; lastfree=i; } }
    if(rel==5){ /* ne */
        if(nfree==0){ if(fixed_sum==rhs) return 1; return 0; }
        if(nfree==1){ int v=lvars[lastfree]; long long a=lcoef[lastfree]; if(a!=0){ __int128 need=(__int128)rhs-fixed_sum; if(need%a==0 && cp_remove_val(cp,v,(int64_t)(need/a))) return 1; } }
        if(n==2 && lcoef[0]!=0 && lcoef[1]!=0){
            int v0=lvars[0],v1=lvars[1];
            if(cp_fixed(cp,v0)){ __int128 num=(__int128)rhs - (__int128)lcoef[0]*cp->vals[v0][0]; if(num%lcoef[1]==0 && cp_remove_val(cp,v1,(int64_t)(num/lcoef[1]))) return 1; }
            else if(cp_fixed(cp,v1)){ __int128 num=(__int128)rhs - (__int128)lcoef[1]*cp->vals[v1][0]; if(num%lcoef[0]==0 && cp_remove_val(cp,v0,(int64_t)(num/lcoef[0]))) return 1; }
        }
        return 0;
    }
    if(nfree==0){
        __int128 s=fixed_sum; int okc=0;
        switch(rel){ case 0: okc=(s==rhs); break; case 1: okc=(s<=rhs); break; case 2: okc=(s>=rhs); break; case 3: okc=(s<rhs); break; case 4: okc=(s>rhs); break; }
        return okc?0:1;
    }
    for(int k=0;k<n;k++){
        int v=lvars[k]; long long a=lcoef[k]; if(a==0||cp_fixed(cp,v)) continue;
        __int128 rmin=0,rmax=0;
        for(int i=0;i<n;i++){ if(i==k)continue; int u=lvars[i]; long long c=lcoef[i];
            __int128 mn=cp_dmin(cp,u),mx=cp_dmax(cp,u);
            if(c>0){ rmin+=(__int128)c*mn; rmax+=(__int128)c*mx; } else { rmin+=(__int128)c*mx; rmax+=(__int128)c*mn; } }
        if(rel==0){ __int128 tlo,thi; if(a>0){ tlo=(__int128)rhs-rmax; thi=(__int128)rhs-rmin; } else { tlo=(__int128)rhs-rmin; thi=(__int128)rhs-rmax; }
            int64_t nlo=(int64_t)ceild(tlo,a), nhi=(int64_t)floord(thi,a);
            if(cp_intersect(cp,v,nlo,nhi)) return 1;
        } else if(rel==1||rel==3){ long long b=(rel==1)?rhs:(rhs-1);
            if(a>0){ int64_t nhi=(int64_t)floord((__int128)b-rmin,a); if(cp_intersect(cp,v,cp_dmin(cp,v),nhi)) return 1; }
            else { int64_t nlo=(int64_t)ceild((__int128)b-rmin,a); if(cp_intersect(cp,v,nlo,cp_dmax(cp,v))) return 1; }
        } else { long long b=(rel==2)?rhs:(rhs+1);
            if(a>0){ int64_t nlo=(int64_t)ceild((__int128)b-rmax,a); if(cp_intersect(cp,v,nlo,cp_dmax(cp,v))) return 1; }
            else { int64_t nhi=(int64_t)floord((__int128)b-rmax,a); if(cp_intersect(cp,v,cp_dmin(cp,v),nhi)) return 1; }
        }
    }
    return 0;
}

/* signed divisors of n (n!=0) into out[cap]; returns count (each sign counted). */
static int cp_divisors(int64_t n,int64_t *out,int cap){
    if(n==0) return -1;
    uint64_t a=(uint64_t)(n<0 ? -n : n);
    int64_t *pos=(int64_t*)psolve_malloc(4096*sizeof(int64_t));
    int cnt=0,poscap=4096;
    for(uint64_t d=1; d*d<=a; d++){
        if(a%d==0){
            if(cnt>=poscap){ poscap*=2; pos=(int64_t*)psolve_realloc((void**)&pos,(size_t)poscap*sizeof(int64_t)); }
            pos[cnt++]=(int64_t)d;
            uint64_t o=a/d;
            if(o!=d){ if(cnt>=poscap){ poscap*=2; pos=(int64_t*)psolve_realloc((void**)&pos,(size_t)poscap*sizeof(int64_t)); } pos[cnt++]=(int64_t)o; }
        }
    }
    int total=0;
    for(int i=0;i<cnt;i++){ int64_t p=pos[i]; if(total<cap) out[total++]=p; if(total<cap) out[total++]=-p; }
    free(pos);
    return total;
}

/* union of the signed divisors of every value in the finite list dom(v).
   Returns 1 and fills *out (caller frees), 0 if unbounded/huge/contains 0. */
static int cp_divisor_union(CP*cp,int v,int64_t **out,int *outn){
    if(!cp->bounded[v] || cp->nvals[v]==0 || cp->nvals[v]>4096) return 0;
    for(int i=0;i<cp->nvals[v];i++){
        int64_t z=cp->vals[v][i];
        if(z==0 || z>(int64_t)INT64_C(1)<<40 || z<-(int64_t)INT64_C(1)<<40) return 0;
    }
    int64_t *set=NULL; int sc=0,scap=0;
    int64_t *buf=(int64_t*)psolve_malloc(8192*sizeof(int64_t));
    for(int i=0;i<cp->nvals[v];i++){
        int nd=cp_divisors(cp->vals[v][i],buf,8192);
        for(int k=0;k<nd && k<8192;k++){
            int64_t val=buf[k]; int dup=0;
            for(int q=0;q<sc;q++) if(set[q]==val){dup=1;break;}
            if(!dup){ if(sc>=scap){ scap=scap?scap*2:64; set=(int64_t*)psolve_realloc((void**)&set,(size_t)scap*sizeof(int64_t)); } set[sc++]=val; }
        }
    }
    free(buf);
    *out=set; *outn=sc;
    return sc>0;
}

/* ------------------------------------------------------------------ */
/* Kuhn maximum matching on free variables of an all_different, used   */
/* both for value removal (Régin-style) and Hall feasibility.          */
/*   nfree: number of free vars; fvv[r]: solver var of free var r;     */
/*   vals/vc: distinct values among free vars;                        */
/*   adj row r -> list of value-column indices.                       */
/* Returns matches count.                                             */
/* ------------------------------------------------------------------ */
typedef struct { int *col; int n; } ADJ;
static int cp_aug(int r,int *matchR,int *seen,ADJ*adj,int vc){
    for(int k=0;k<adj[r].n;k++){
        int c=adj[r].col[k];
        if(seen[c]) continue; seen[c]=1;
        if(matchR[c]==-1 || cp_aug(matchR[c],matchR,seen,adj,vc)){
            matchR[c]=r; return 1;
        }
    }
    return 0;
}

/* full all_different propagation; returns 1 if inconsistent */
static int cp_prop_alldiff(CP*cp,CPT*terms,int n){
    int changed=1;
    while(changed){
        changed=0;
        /* constants + singletons remove their value from others */
        for(int i=0;i<n;i++){
            int64_t fv;
            if(cp_term_value(cp,&terms[i],&fv)){
                for(int j=0;j<n;j++) if(j!=i){
                    CPT*t=&terms[j]; if(t->isc) continue;
                    if(cp_remove_val(cp,t->v,fv)) return 1;
                }
            }
        }
        /* matching-based feasibility + value removal (Régin-style).
           (A naive "value appears in only one domain => force it" rule is NOT
           valid for all_different: a value may simply go unused, so we rely
           on the maximum-matching arc consistency below instead.) */
        /* matching-based feasibility + value removal (Régin-style) */
        {
            int nfree=0;
            for(int i=0;i<n;i++) if(!terms[i].isc && cp->bounded[terms[i].v] && cp->nvals[terms[i].v]>1) nfree++;
            if(nfree>0){
                int *fvv=(int*)psolve_malloc((size_t)nfree*sizeof(int));
                int c=0; for(int i=0;i<n;i++) if(!terms[i].isc && cp->bounded[terms[i].v] && cp->nvals[terms[i].v]>1) fvv[c++]=terms[i].v;
                int64_t *vs=NULL; int vc=0,vcap=0;
                for(int r=0;r<nfree;r++){ int v=fvv[r]; for(int k=0;k<cp->nvals[v];k++){ int64_t val=cp->vals[v][k]; int dup=0; for(int q=0;q<vc;q++) if(vs[q]==val){dup=1;break;} if(!dup){ if(vc>=vcap){vcap=vcap?vcap*2:64;vs=(int64_t*)psolve_realloc((void**)&vs,(size_t)vcap*sizeof(int64_t));} vs[vc++]=val; } } }
                if(vc==0){ free(fvv); free(vs); continue; }
                ADJ *adj=(ADJ*)psolve_calloc((size_t)nfree,sizeof(ADJ));
                for(int r=0;r<nfree;r++){
                    int v=fvv[r]; int cap2=0; int *tmp=NULL; int tn=0;
                    for(int k=0;k<vc;k++) if(cp_contains(cp,v,vs[k])){ if(tn>=cap2){cap2=cap2?cap2*2:8;tmp=(int*)psolve_realloc((void**)&tmp,(size_t)cap2*sizeof(int));} tmp[tn++]=k; }
                    adj[r].col=tmp; adj[r].n=tn;
                }
                int *matchR=(int*)psolve_malloc((size_t)(vc?vc:1)*sizeof(int)); for(int k=0;k<vc;k++) matchR[k]=-1;
                int matches=0;
                for(int r=0;r<nfree;r++){ int *seen=(int*)psolve_calloc((size_t)(vc?vc:1),sizeof(int)); if(cp_aug(r,matchR,seen,adj,vc)) matches++; free(seen); }
                if(matches<nfree){ /* Hall violated -> UNSAT */
                    for(int r=0;r<nfree;r++) free(adj[r].col); free(adj); free(matchR); free(fvv); free(vs);
                    return 1;
                }
                /* Value removal: a value val in dom(fvv[r]) is removable iff the edge
                   (r,val) is in NO maximum matching, i.e. forcing (r,val) leaves no
                   perfect matching.  Cheap for the small all_different constraints in
                   FlatZinc CSPs. */
                for(int r=0;r<nfree;r++){
                    int v=fvv[r];
                    for(int k=0;k<adj[r].n;k++){
                        int c=adj[r].col[k];
                        if(matchR[c]==r) continue; /* matched edge is in this matching; keep */
                        int64_t val=vs[c];
                        ADJ *adj2=(ADJ*)psolve_calloc((size_t)nfree,sizeof(ADJ));
                        for(int r2=0;r2<nfree;r2++){
                            int v2=fvv[r2]; int *tmp=NULL; int tn=0,cap2=0;
                            for(int c2=0;c2<vc;c2++){
                                /* force (r,c): var r may only take value c */
                                if(r2==r && c2!=c) continue;
                                if(cp_contains(cp,v2,vs[c2])){ if(tn>=cap2){cap2=cap2?cap2*2:8;tmp=(int*)psolve_realloc((void**)&tmp,(size_t)cap2*sizeof(int));} tmp[tn++]=c2; }
                            }
                            adj2[r2].col=tmp; adj2[r2].n=tn;
                        }
                        int *mR=(int*)psolve_malloc((size_t)(vc?vc:1)*sizeof(int)); for(int z=0;z<vc;z++) mR[z]=-1;
                        int m2=0;
                        for(int r2=0;r2<nfree;r2++){ int *seen2=(int*)psolve_calloc((size_t)(vc?vc:1),sizeof(int)); if(cp_aug(r2,mR,seen2,adj2,vc)) m2++; free(seen2); }
                        int removable=(m2<nfree); /* forcing (r,c) breaks every matching */
                        for(int r2=0;r2<nfree;r2++) free(adj2[r2].col); free(adj2); free(mR);
                        if(removable){
                            if(cp_remove_val(cp,v,val)){
                                for(int r=0;r<nfree;r++) free(adj[r].col); free(adj); free(matchR); free(fvv); free(vs);
                                return 1;
                            }
                            changed=1;
                        }
                    }
                }
                for(int r=0;r<nfree;r++) free(adj[r].col); free(adj); free(matchR); free(fvv); free(vs);
            }
        }
    }
    return 0;
}
/* intersect v's domain with the signed divisor list dvbuf[0..nd).
   Returns 0 ok, 1 empty, -1 too large to materialize. */
static int cp_restrict_divisors(CP*cp,int v,int64_t*dvbuf,int nd){
    if(cp->bounded[v]){
        /* keep only values that are in the divisor list */
        int64_t *nvals=NULL; int nn=0,cap2=0;
        for(int i=0;i<cp->nvals[v];i++){ int64_t val=cp->vals[v][i]; int found=0; for(int k=0;k<nd;k++) if(dvbuf[k]==val){found=1;break;} if(found){ if(nn>=cap2){cap2=cap2?cap2*2:16;nvals=(int64_t*)psolve_realloc((void**)&nvals,(size_t)cap2*sizeof(int64_t));} nvals[nn++]=val; } }
        if(nn==0){ free(nvals); return 1; }
        if(cp_set_vals(cp,v,nvals,nn)<0){ free(nvals); return -1; } free(nvals);
        return 0;
    } else {
        /* interval/unbounded: restrict to divisor list, respecting current bounds */
        int64_t *nvals=NULL; int nn=0,cap2=0;
        for(int k=0;k<nd;k++){ int64_t val=dvbuf[k]; if(val>=cp->lo[v] && val<=cp->hi[v]){ if(nn>=cap2){cap2=cap2?cap2*2:16;nvals=(int64_t*)psolve_realloc((void**)&nvals,(size_t)cap2*sizeof(int64_t));} nvals[nn++]=val; } }
        if(nn==0){ free(nvals); return 1; }
        if(cp_set_vals(cp,v,nvals,nn)<0){ free(nvals); return -1; } free(nvals);
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* Multiplication: c = a * b (a,b,c each either const or var).         */
/* Sound, conservative propagation; search + verification provides     */
/* completeness.  Returns 1 if inconsistent, -1 if uncertifiable.      */
/* ------------------------------------------------------------------ */
static int cp_prop_times(CP*cp,CPT*a,CPT*b,CPT*c){
    int64_t av,bv,cv;
    int aF=cp_term_value(cp,a,&av), bF=cp_term_value(cp,b,&bv), cF=cp_term_value(cp,c,&cv);
    int aV=!a->isc, bV=!b->isc, cV=!c->isc;
    int same=(aV&&bV&&a->v==b->v);

    if(aF && bF){
        __int128 p=(__int128)av*bv;
        if(p<INT64_MIN||p>INT64_MAX) return 1;
        int64_t pv=(int64_t)p;
        if(cF) return pv==cv?0:1;
        return cp_intersect(cp,c->v,pv,pv)?1:0;
    }
    if(aF){ /* c = av*b */
        if(av==0){ if(cV) return cp_intersect(cp,c->v,0,0)?1:0; return cF?(cv!=0):0; }
        if(cV){
            if(bV && cp->bounded[c->v]&&cp->nvals[c->v]<=CP_MAXVALS && cp->bounded[b->v]&&cp->nvals[b->v]<=CP_MAXVALS){
                int64_t *nvals=NULL;int nn=0,cap2=0;
                for(int i=0;i<cp->nvals[c->v];i++){int64_t cv2=cp->vals[c->v][i]; if(cv2%av==0){int64_t bv2=cv2/av; int dup=0;for(int q=0;q<nn;q++)if(nvals[q]==bv2){dup=1;break;}if(!dup){if(nn>=cap2){cap2=cap2?cap2*2:16;nvals=(int64_t*)psolve_realloc((void**)&nvals,(size_t)cap2*sizeof(int64_t));}nvals[nn++]=bv2;}}}
                if(nn==0){free(nvals);return 1;}
                int64_t *keep=NULL;int kn=0;
                for(int i=0;i<nn;i++) if(cp_contains(cp,b->v,nvals[i])){ keep=(int64_t*)psolve_realloc((void**)&keep,(size_t)(kn+1)*sizeof(int64_t)); keep[kn++]=nvals[i]; }
                free(nvals); nvals=keep; nn=kn;
                if(nn==0){free(nvals);return 1;}
                if(cp_set_vals(cp,b->v,nvals,nn)<0){free(nvals);return -1;} free(nvals);
            } else if(bV){
                int64_t clo=cp_dmin(cp,c->v),chi=cp_dmax(cp,c->v);
                int64_t blo=(int64_t)ceild(clo,av), bhi=(int64_t)floord(chi,av);
                if(cp_intersect(cp,b->v,blo,bhi)) return 1;
            }
            int64_t blo2=cp_dmin(cp,b->v),bhi2=cp_dmax(cp,b->v);
            int64_t clo2=(int64_t)(av>=0?av*blo2:av*bhi2), chi2=(int64_t)(av>=0?av*bhi2:av*blo2);
            if(cp_intersect(cp,c->v,clo2,chi2)) return 1;
            return 0;
        } else { /* c fixed */
            if(cv%av!=0) return 1;
            if(bV && cp_intersect(cp,b->v,cv/av,cv/av)) return 1;
            return 0;
        }
    }
    if(bF){ CPT t=*a; return cp_prop_times(cp,b,&t,c); }

    /* both operands are variables */
    if(cF){
        if(cv==0) return 0; /* a*b=0 -> a=0 or b=0; not bounded here */
        if(!same){
            int64_t dv[1024];int nd=cp_divisors(cv,dv,1024);
            if(nd<=0) return 1;
            int ra=cp_restrict_divisors(cp,a->v,dv,nd);
            int rb=cp_restrict_divisors(cp,b->v,dv,nd);
            if(ra==1||rb==1) return 1; if(ra==-1||rb==-1) return -1;
            return 0;
        } else { /* a==b, c fixed */
            if(cv<0) return 1;
            int64_t r=(int64_t)llround(sqrt((double)cv));
            if(r*r!=cv) return 1;
            int64_t cand[2];int ncv=0; if(r!=0){cand[ncv++]=r;cand[ncv++]=-r;} else cand[ncv++]=0;
            int64_t *keep=NULL;int kn=0;
            for(int i=0;i<ncv;i++) if(cp_contains(cp,a->v,cand[i])){ keep=(int64_t*)psolve_realloc((void**)&keep,(size_t)(kn+1)*sizeof(int64_t)); keep[kn++]=cand[i]; }
            if(kn==0){free(keep);return 1;}
            if(cp_set_vals(cp,a->v,keep,kn)<0){free(keep);return -1;} free(keep);
            return 0;
        }
    }

    /* c is a variable */
    {
        int64_t alo=cp_dmin(cp,a->v),ahi=cp_dmax(cp,a->v),blo=cp_dmin(cp,b->v),bhi=cp_dmax(cp,b->v);
        __int128 c1=(__int128)alo*blo,c2=(__int128)alo*bhi,c3=(__int128)ahi*blo,c4=(__int128)ahi*bhi;
        __int128 mn=c1,mx=c1; __int128 cs[3]={c2,c3,c4}; for(int i=0;i<3;i++){ if(cs[i]<mn)mn=cs[i]; if(cs[i]>mx)mx=cs[i]; }
        if(mn>=INT64_MIN && mx<=INT64_MAX){
            if(cp_intersect(cp,c->v,(int64_t)mn,(int64_t)mx)) return 1;
        } /* else bounds beyond int64: no useful interval restriction */
        /* Note: in-search variable*variable propagation is deliberately minimal
           (only fixed-operand cases above).  The product-chain-equals-constant
           case is bounded up-front by cp_bound_diophantine(), so search can
           enumerate the (small) divisor domains with verification. */
    }
    return 0;
}
static int cp_prop_divmod(CP*cp,CPT*a,int64_t k,CPT*c,int is_div){
    if(k==0) return 1;
    int64_t av,cv;
    int aF=cp_term_value(cp,a,&av), cF=cp_term_value(cp,c,&cv);
    if(aF){
        int64_t res= is_div? (av/k) : (av%k);
        if(cF){ if(res!=cv) return 1; }
        else if(!c->isc && cp_intersect(cp,c->v,res,res)) return 1;
        return 0;
    }
    /* Dividend is a variable.  Only propagate when it has finite bounds and the
       result is a variable; otherwise keep it conservative (no over-pruning) and
       let search + verification / the MIP fallback handle it. */
    if(!a->isc){
        if(cp->lo[a->v]==INT64_MIN || cp->hi[a->v]==INT64_MAX) return 0;   /* unbounded */
    }
    /* c (result) must be a variable for pruning; if c fixed handled by aF? no: */
    if(!c->isc){
        if(cp->lo[c->v]==INT64_MIN || cp->hi[c->v]==INT64_MAX) return 0;
    }
    /* --- exact small-domain cases only (sound) --- */
    if(a->isc==0 && c->isc==0 && cp->bounded[a->v] && cp->bounded[c->v] &&
       cp->nvals[a->v]<=CP_MAXVALS && cp->nvals[c->v]<=CP_MAXVALS){
        if(is_div){
            if(cp->nvals[a->v]>1){
                int64_t *nvals=NULL; int nn=0,cap2=0;
                for(int i=0;i<cp->nvals[a->v];i++){ int64_t av2=cp->vals[a->v][i]; if(cp_contains(cp,c->v,av2/k)){ if(nn>=cap2){cap2=cap2?cap2*2:64;nvals=(int64_t*)psolve_realloc((void**)&nvals,(size_t)cap2*sizeof(int64_t));} nvals[nn++]=av2; } }
                if(nn==0){ free(nvals); return 1; }
                if(cp_set_vals(cp,a->v,nvals,nn)<0){ free(nvals); return -1; } free(nvals);
            }
            if(cp->nvals[c->v]>1){
                int64_t *nvals=NULL; int nn=0,cap2=0;
                for(int i=0;i<cp->nvals[a->v];i++){ int64_t av2=cp->vals[a->v][i]; int64_t q=av2/k; if(cp_contains(cp,c->v,q)){ int dup=0; for(int q2=0;q2<nn;q2++) if(nvals[q2]==q){dup=1;break;} if(!dup){ if(nn>=cap2){cap2=cap2?cap2*2:64;nvals=(int64_t*)psolve_realloc((void**)&nvals,(size_t)cap2*sizeof(int64_t));} nvals[nn++]=q; } } }
                if(nn==0){ free(nvals); return 1; }
                if(cp_set_vals(cp,c->v,nvals,nn)<0){ free(nvals); return -1; } free(nvals);
            }
        } else {
            if(cp->nvals[a->v]>1){
                int64_t *nvals=NULL; int nn=0,cap2=0;
                for(int i=0;i<cp->nvals[a->v];i++){ int64_t av2=cp->vals[a->v][i]; if(cp_contains(cp,c->v,av2%k)){ if(nn>=cap2){cap2=cap2?cap2*2:64;nvals=(int64_t*)psolve_realloc((void**)&nvals,(size_t)cap2*sizeof(int64_t));} nvals[nn++]=av2; } }
                if(nn==0){ free(nvals); return 1; }
                if(cp_set_vals(cp,a->v,nvals,nn)<0){ free(nvals); return -1; } free(nvals);
            }
            if(cp->nvals[c->v]>1){
                int64_t *nvals=NULL; int nn=0,cap2=0;
                for(int i=0;i<cp->nvals[a->v];i++){ int64_t av2=cp->vals[a->v][i]; int64_t r=av2%k; if(cp_contains(cp,c->v,r)){ int dup=0; for(int q=0;q<nn;q++) if(nvals[q]==r){dup=1;break;} if(!dup){ if(nn>=cap2){cap2=cap2?cap2*2:64;nvals=(int64_t*)psolve_realloc((void**)&nvals,(size_t)cap2*sizeof(int64_t));} nvals[nn++]=r; } } }
                if(nn==0){ free(nvals); return 1; }
                if(cp_set_vals(cp,c->v,nvals,nn)<0){ free(nvals); return -1; } free(nvals);
            }
        }
    }
    return 0;
}
/* ------------------------------------------------------------------ */
/* int_abs(a,c): c = |a|.                                              */
/* ------------------------------------------------------------------ */
static int cp_prop_abs(CP*cp,CPT*a,CPT*c){
    int64_t av,cv;
    int aF=cp_term_value(cp,a,&av), cF=cp_term_value(cp,c,&cv);
    if(aF){ int64_t r=(av<0?-av:av); if(cF){ if(r!=cv) return 1; } else { if(cp_intersect(cp,c->v,r,r)) return 1; } return 0; }
    if(cF){
        /* c fixed, a = +/-cv */
        if(cv<0) return 1;
        int64_t *nvals=NULL; int nn=0;
        if(cv==0){ nvals=(int64_t*)psolve_malloc(sizeof(int64_t)); nvals[nn++]=0; }
        else { nvals=(int64_t*)psolve_malloc(2*sizeof(int64_t)); nvals[nn++]=cv; nvals[nn++]=-cv; }
        if(cp_set_vals(cp,a->v,nvals,nn)<0){ free(nvals); return -1; } free(nvals);
        return 0;
    }
    /* bounds */
    int64_t alo=cp_dmin(cp,a->v), ahi=cp_dmax(cp,a->v);
    int64_t mn=0, mx=0;
    mx=(int64_t)fmax(fabs((double)alo),fabs((double)ahi));
    mn=0;
    if(cp_intersect(cp,c->v,mn,mx)) return 1;
    /* a in [-mx, mx] */
    if(cp_intersect(cp,a->v,-mx,mx)) return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* element: res = arr[idx - offset], idx in [offset, offset+narr-1].   */
/* terms[0]=idx, terms[1]=res; arr entries in arrt (CPT, const or var).*/
/* ------------------------------------------------------------------ */
static int cp_prop_elem(CP*cp,CPT*idx,CPT*res,CPT*arrt,int narr,int offset){
    int64_t iv,rv;
    int iF=cp_term_value(cp,idx,&iv), rF=cp_term_value(cp,res,&rv);
    if(iF){
        long k=(long)(iv-offset);
        if(k<0||k>=narr) return 1;
        CPT *e=&arrt[k];
        int64_t ev;
        int eF=cp_term_value(cp,e,&ev);
        if(eF){
            if(rF){ if(rv!=ev) return 1; }
            else if(!res->isc && cp_intersect(cp,res->v,ev,ev)) return 1;
        } else if(res->isc){
            /* res fixed, e is a var: e must equal res */
            if(cp_intersect(cp,e->v,res->c,res->c)) return 1;
        } else if(!res->isc && e->v==res->v){
            return 0; /* res == res always true */
        } else if(!res->isc){
            /* res and e both vars: e must equal res -> bound both to intersection */
            int64_t elo=cp_dmin(cp,e->v),ehi=cp_dmax(cp,e->v);
            int64_t rlo=cp_dmin(cp,res->v),rhi=cp_dmax(cp,res->v);
            int64_t nlo=(elo>rlo?elo:rlo), nhi=(ehi<rhi?ehi:rhi);
            if(cp_intersect(cp,e->v,nlo,nhi)) return 1;
            if(cp_intersect(cp,res->v,nlo,nhi)) return 1;
            if(cp_fixed(cp,e->v)&&cp_fixed(cp,res->v)&&cp->vals[e->v][0]!=cp->vals[res->v][0]) return 1;
        }
        return 0;
    }
    /* index not fixed.  If res fixed, restrict index to positions that can hold res. */
    if(rF){
        if(cp->bounded[idx->v] && cp->nvals[idx->v]<=CP_MAXVALS){
            int64_t *nvals=NULL; int nn=0,cap2=0;
            for(int i=0;i<cp->nvals[idx->v];i++){ int64_t iv2=cp->vals[idx->v][i]; long k=(long)(iv2-offset);
                if(k>=0&&k<narr){ CPT*e=&arrt[k]; int64_t ev; int eF=cp_term_value(cp,e,&ev);
                    int can = eF ? (ev==rv) : (!e->isc && cp_contains(cp,e->v,rv));
                    if(can){ if(nn>=cap2){cap2=cap2?cap2*2:8;nvals=(int64_t*)psolve_realloc((void**)&nvals,(size_t)cap2*sizeof(int64_t));} nvals[nn++]=iv2; } } }
            if(nn==0){ free(nvals); return 1; }
            if(cp_set_vals(cp,idx->v,nvals,nn)<0){ free(nvals); return -1; } free(nvals);
        }
    }
    /* index range must be within array */
    if(cp_intersect(cp,idx->v,offset,(int64_t)(offset+narr-1))) return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* set_in(x, S): x must be one of setvals.  Also x != y via set diff.  */
/* ------------------------------------------------------------------ */
static int cp_prop_setin(CP*cp,CPT*x,long long*set,int nset){
    if(cp->bounded[x->v] && cp->nvals[x->v]<=CP_MAXVALS){
        int64_t *nvals=NULL; int nn=0,cap2=0;
        for(int i=0;i<cp->nvals[x->v];i++){ int64_t val=cp->vals[x->v][i]; for(int k=0;k<nset;k++) if(set[k]==val){ if(nn>=cap2){cap2=cap2?cap2*2:8;nvals=(int64_t*)psolve_realloc((void**)&nvals,(size_t)cap2*sizeof(int64_t));} nvals[nn++]=val; break; } }
        if(nn==0){ free(nvals); return 1; }
        if(cp_set_vals(cp,x->v,nvals,nn)<0){ free(nvals); return -1; } free(nvals);
    } else {
        int64_t lo=set[0],hi=set[0]; for(int k=1;k<nset;k++){ if(set[k]<lo)lo=set[k]; if(set[k]>hi)hi=set[k]; }
        if(cp_intersect(cp,x->v,lo,hi)) return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* pow: c = base^exp (exp constant >=0), or base const when linear.    */
/* ------------------------------------------------------------------ */
static int cp_prop_pow(CP*cp,CPT*base,int64_t expo,CPT*c){
    if(expo==0){ int64_t one=1; if(c->isc){ if(c->c!=1) return 1; } else if(cp_intersect(cp,c->v,1,1)) return 1; return 0; }
    int64_t bv,cv;
    int bF=cp_term_value(cp,base,&bv), cF=cp_term_value(cp,c,&cv);
    int cV=!c->isc;
    if(bF){
        __int128 p=1; int ovf=0; for(int64_t i=0;i<expo;i++){ p*=(__int128)bv; if(p>INT64_MAX||p<INT64_MIN){ ovf=1; break; } }
        if(ovf){
            /* power beyond int64 range: UNSAT only if the result is already a
               fixed/bounded value inside int64 (it cannot be that large);
               otherwise be honest (skip) so the MIP bridge reports UNKNOWN. */
            if(cF) return 1;
            if(cV && cp->bounded[c->v]){ int64_t cm=cp_dmax(cp,c->v); if((p<0 && cm>INT64_MIN) || (p>0 && cm<INT64_MAX)) return 1; }
            return 0;
        }
        int64_t pv=(int64_t)p;
        if(cF){ if(pv!=cv) return 1; } else if(cp_intersect(cp,c->v,pv,pv)) return 1;
        return 0;
    }
    if(cF){
        /* base = integer root of cv */
        if(expo==1){ if(cp_intersect(cp,base->v,cv,cv)) return 1; return 0; }
        if((expo&1)==0 && cv<0) return 1; /* even power can't be negative */
        /* find b in dom(base) with b^expo == cv */
        if(cp->bounded[base->v] && cp->nvals[base->v]<=CP_MAXVALS){
            int64_t *nvals=NULL; int nn=0,cap2=0;
            for(int i=0;i<cp->nvals[base->v];i++){ int64_t b2=cp->vals[base->v][i]; __int128 p=1; for(int64_t e=0;e<expo;e++){ p*=(__int128)b2; if(p>INT64_MAX||p<INT64_MIN){ p=INT64_MAX+1; break; } } if(p==cv){ if(nn>=cap2){cap2=cap2?cap2*2:8;nvals=(int64_t*)psolve_realloc((void**)&nvals,(size_t)cap2*sizeof(int64_t));} nvals[nn++]=b2; } }
            if(nn==0){ free(nvals); return 1; }
            if(cp_set_vals(cp,base->v,nvals,nn)<0){ free(nvals); return -1; } free(nvals);
        }
        return 0;
    }
    /* bounds: c in [bmin^e, bmax^e] approx */
    int64_t bmin=cp_dmin(cp,base->v), bmax=cp_dmax(cp,base->v);
    __int128 p1=1,p2=1;
    for(int64_t i=0;i<expo;i++){ p1*=(__int128)bmin; p2*=(__int128)bmax; }
    int64_t clo=(int64_t)fmin((double)p1,(double)p2), chi=(int64_t)fmax((double)p1,(double)p2);
    if(cp_intersect(cp,c->v,clo,chi)) return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Reification: r <-> (sum lcoef*x rel rhs).  terms[0] is the bool var r.  */
/* Conservative but sound: enforce the inner constraint when r is fixed,  */
/* set r when the inner is fully determined.  Arc consistency is left to  */
/* search + verification.  Returns 1 if inconsistent.                     */
/* ------------------------------------------------------------------ */
static int cp_prop_reif(CP*cp,CPCR*r){
    int rv=r->terms[0].v;
    if(r->terms[0].isc || rv<0 || rv>=cp->nvars) return 0;
    /* is the inner fully determined? */
    int allfix=1; __int128 val=0;
    for(int i=0;i<r->n;i++){ int v=r->lvars[i]; if(!cp_fixed(cp,v)){allfix=0;break;} val+=(__int128)r->lcoef[i]*cp->vals[v][0]; }
    if(cp_fixed(cp,rv)){
        int rval=(int)cp->vals[rv][0];
        if(rval==1){ if(cp_prop_lin(cp,r->n,r->lvars,r->lcoef,r->rhs,r->rel)) return 1; }
        else { /* enforce negation */
            int negrel;
            switch(r->rel){case 0:negrel=5;break;case 5:negrel=0;break;case 1:negrel=4;break;case 4:negrel=1;break;case 2:negrel=3;break;case 3:negrel=2;break;default:negrel=r->rel;break;}
            if(cp_prop_lin(cp,r->n,r->lvars,r->lcoef,r->rhs,negrel)) return 1;
        }
        return 0;
    }
    /* r free.  If inner determined, set r. */
    if(allfix){
        int holds=0;
        switch(r->rel){case 0:holds=(val==r->rhs);break;case 1:holds=(val<=r->rhs);break;case 2:holds=(val>=r->rhs);break;case 3:holds=(val<r->rhs);break;case 4:holds=(val>r->rhs);break;case 5:holds=(val!=r->rhs);break;}
        cp_assign(cp,rv,holds?1:0);
        return 0;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* bool_clause(pos[], neg[]): at least one pos true OR one neg false.  */
/* pos bools in lvars (must be 1), neg bools in terms[].v (must be 0).  */
/* Returns 1 if inconsistent.                                          */
/* ------------------------------------------------------------------ */
static int cp_prop_clause(CP*cp,CPCR*c){
    int npos=c->n, nneg=0;
    while(c->terms && nneg<0) ; /* noop */
    /* count neg: terms array sized by nset */
    nneg=c->nset;
    /* determine if satisfied: any pos==1 or any neg==0 */
    int satisfied=0;
    for(int i=0;i<npos;i++){ int v=c->lvars[i]; if(v<0||v>=cp->nvars) continue; if(cp_fixed(cp,v)&&cp->vals[v][0]==1){satisfied=1;break;} }
    if(!satisfied) for(int i=0;i<nneg;i++){ int v=c->terms[i].v; if(v<0||v>=cp->nvars) continue; if(cp_fixed(cp,v)&&cp->vals[v][0]==0){satisfied=1;break;} }
    if(satisfied) return 0;
    /* count undetermined literals; force the last one if exactly one remains */
    int undet=0, lastpos=-1, lastneg=-1;
    for(int i=0;i<npos;i++){ int v=c->lvars[i]; if(v<0||v>=cp->nvars) continue;
        if(cp_fixed(cp,v)){ if(cp->vals[v][0]==0){} else {satisfied=1;} } else { undet++; lastpos=i; } }
    for(int i=0;i<nneg;i++){ int v=c->terms[i].v; if(v<0||v>=cp->nvars) continue;
        if(cp_fixed(cp,v)){ if(cp->vals[v][0]==1){} else {satisfied=1;} } else { undet++; lastneg=i; } }
    if(satisfied) return 0;
    if(undet==0) return 1;  /* all forced to fail */
    if(undet==1){
        if(lastpos>=0){ cp_assign(cp,c->lvars[lastpos],1); return 0; }
        if(lastneg>=0){ cp_assign(cp,c->terms[lastneg].v,0); return 0; }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* minmax: m = min(a,b) (ismax=0) or max(a,b) (ismax=1).               */
/* terms[0]=a, terms[1]=b, terms[2]=m.  Sound, conservative.          */
/* Returns 1 if inconsistent.                                         */
/* ------------------------------------------------------------------ */
static int cp_prop_minmax(CP*cp,CPCR*c){
    int ismax=(c->n!=0);
    CPT*A=&c->terms[0],*B=&c->terms[1],*M=&c->terms[2];
    int64_t av,bv,mv;
    int aF=cp_term_value(cp,A,&av), bF=cp_term_value(cp,B,&bv), mF=cp_term_value(cp,M,&mv);
    if(A->isc && B->isc){
        int64_t res=ismax? (av>bv?av:bv) : (av<bv?av:bv);
        if(M->isc) return res==mv?0:1;
        if(!M->isc && cp_intersect(cp,M->v,res,res)) return 1;
        return 0;
    }
    if(A->isc && !B->isc && M->isc && mF){
        /* m = min/max(av, b).  Bound b. */
        if(ismax){ /* m=max(av,b): if m<av UNSAT; b = m if m>av, else b<=m */
            if(mv<av) return 1;
            if(mv>av){ if(cp_intersect(cp,B->v,mv,mv)) return 1; }
            else { if(cp_intersect(cp,B->v,cp_dmin(cp,B->v),mv)) return 1; }
        } else { /* m=min(av,b): if m>av UNSAT; b = m if m<av, else b>=m */
            if(mv>av) return 1;
            if(mv<av){ if(cp_intersect(cp,B->v,mv,mv)) return 1; }
            else { if(cp_intersect(cp,B->v,mv,cp_dmax(cp,B->v))) return 1; }
        }
        return 0;
    }
    if(!A->isc && B->isc && M->isc && mF){
        CPT t=*A; *A=*B; *B=t;
        int rc=cp_prop_minmax(cp,c);
        *B=*A; *A=t;
        return rc;
    }
    if(A->isc && B->isc==0 && mF==0){ /* conservative bound: m in [min,max] */
        if(M->isc) return 0;
        /* m <= max(a,b), m >= min(a,b) */
        int64_t amn=cp_dmin(cp,A->v),amx=cp_dmax(cp,A->v);
        if(cp_intersect(cp,M->v, amn, amx)) return 1;
        if(!B->isc){ int64_t bmn=cp_dmin(cp,B->v),bmx=cp_dmax(cp,B->v); if(cp_intersect(cp,M->v,bmn,bmx)) return 1; }
        return 0;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Parsing FlatZinc constraints into CPCR records                      */
/* ------------------------------------------------------------------ */

/* build a CPCR linear record from a Lin and a rel code */
static int cp_make_lin(CPCR*cr,const Lin*L,int rel){
    cr->kind=CPK_LIN; cr->rel=rel; cr->n=L->n;
    cr->lvars=(int*)psolve_malloc((size_t)(L->n?L->n:1)*sizeof(int));
    cr->lcoef=(long long*)psolve_malloc((size_t)(L->n?L->n:1)*sizeof(long long));
    for(int i=0;i<L->n;i++){ cr->lvars[i]=L->idx[i]; cr->lcoef[i]=(long long)llround(L->coef[i]); }
    cr->rhs= -(long long)llround(L->constant);
    return 0;
}

/* parse integer set literal "{a,b,c}" or "lo..hi" into setvals; returns count, -1 on fail */
static int parse_int_set_lit(FZModel*m,const char*s,long long**out){
    const char*sp=s; while(isspace((unsigned char)*sp))sp++;
    long long *sv=NULL; int sn=0,scap=0;
    /* bare identifier naming a par set? */
    if(isalpha((unsigned char)*sp)||*sp=='_'){
        char nm[128];int k=0;while(k<127&&is_ident_ch((unsigned char)*sp))nm[k++]=*sp++;nm[k]=0;
        FZDecl*d=find_decl(m,nm);
        if(d&&d->nset>0){ *out=(long long*)psolve_malloc((size_t)d->nset*sizeof(long long)); for(int i=0;i<d->nset;i++)(*out)[i]=(long long)d->setvals[i]; return d->nset; }
        return -1;
    }
    if(*sp=='{'){
        sp++;
        while(*sp && *sp!='}'){
            while(*sp==','||isspace((unsigned char)*sp))sp++;
            if(*sp=='}')break;
            char*end; long long v=strtoll(sp,&end,10);
            if(end==sp)return -1;
            long long a=v; sp=end;
            while(isspace((unsigned char)*sp))sp++;
            if(*sp=='.'&&sp[1]=='.'){ sp+=2; char*end2; long long b=strtoll(sp,&end2,10); sp=end2; if(b<a){long long t=a;a=b;b=t;} for(long long q=a;q<=b;q++){ if(sn>=scap){scap=scap?scap*2:16;sv=(long long*)psolve_realloc((void**)&sv,(size_t)scap*sizeof(long long));} sv[sn++]=q; } }
            else { if(sn>=scap){scap=scap?scap*2:16;sv=(long long*)psolve_realloc((void**)&sv,(size_t)scap*sizeof(long long));} sv[sn++]=a; }
            while(isspace((unsigned char)*sp))sp++;
            if(*sp==',')sp++;
        }
        if(sn==0)return -1;
        *out=sv; return sn;
    }
    /* "lo..hi" without braces */
    {
        char*end; long long a=strtoll(sp,&end,10); if(end==sp)return -1; long long lo=a; sp=end;
        while(isspace((unsigned char)*sp))sp++;
        if(*sp=='.'&&sp[1]=='.'){ sp+=2; char*end2; long long hi=strtoll(sp,&end2,10); if(end2==sp)return -1;
            if(hi-lo+1>CP_MAXVALS)return -1;
            sv=(long long*)psolve_malloc((size_t)(hi-lo+1)*sizeof(long long)); sn=0; for(long long q=lo;q<=hi;q++)sv[sn++]=q; *out=sv; return sn; }
        sv=(long long*)psolve_malloc(sizeof(long long)); sv[0]=lo; *out=sv; return 1;
    }
}

/* detect whether pred name (possibly fzn_-prefixed) matches base */
static int cp_pred_is(const char*p,const char*base){
    if(strcmp(p,base)==0) return 1;
    if(strncmp(p,"fzn_",4)==0) return strcmp(p+4,base)==0;
    if(strncmp(p,"gecode_",7)==0) return strcmp(p+7,base)==0;
    return 0;
}

/* Returns number of CPCR records on success, -1 if any constraint is not
   CP-supported (caller falls back to MIP).  Sets *has_cp_motivating if at
   least one combinatorial/nonlinear predicate warrants CP.  Fills
   *referenced[v] and sets *out_nc. */
static int cp_parse(FZModel*m,CPCR**out_cr,int*out_n,unsigned char*referenced,int*has_cp_motivating){
    int cap=16,nc=0; CPCR*cr=(CPCR*)psolve_calloc((size_t)cap,sizeof(CPCR));
    int motivating=0;
    int ok=1;
    for(FZConstr*c=m->constr;c;c=c->next){
        const char*p=c->pred;
        Lin l1,l2,l3; Lin*arr=NULL; int narr=0;
        CPCR rec; memset(&rec,0,sizeof(rec));
        ok=1;
        /* ---------- linear relations ---------- */
        if(cp_pred_is(p,"int_lin_eq")||cp_pred_is(p,"int_lin_le")||cp_pred_is(p,"int_lin_ge")||
           cp_pred_is(p,"int_lin_lt")||cp_pred_is(p,"int_lin_gt")||cp_pred_is(p,"int_lin_ne")||
           cp_pred_is(p,"bool_lin_eq")||cp_pred_is(p,"bool_lin_le")||cp_pred_is(p,"bool_lin_ge")||
           cp_pred_is(p,"bool_lin_lt")||cp_pred_is(p,"bool_lin_gt")||cp_pred_is(p,"bool_lin_ne")){
            if(c->nargs<3){ok=0;break;}
            Lin*arr;int narr; if(parse_array(m,c->args[0],&arr,&narr)!=0){ok=0;break;}
            Lin*parr;int nparr; if(parse_array(m,c->args[1],&parr,&nparr)!=0){free_lins(arr,narr);ok=0;break;}
            Lin d; if(parse_lin(m,c->args[2],&d)!=0){free_lins(arr,narr);free_lins(parr,nparr);ok=0;break;}
            if(narr!=nparr){free_lins(arr,narr);free_lins(parr,nparr);lin_free(&d);ok=0;break;}
            Lin L;memset(&L,0,sizeof(L));
            int bad=0;
            for(int i=0;i<narr;i++){
                if(arr[i].n!=0 || !isfinite(arr[i].constant)){bad=1;break;}
                lin_into(&L,&parr[i],arr[i].constant);
            }
            lin_into(&L,&d,-1.0);
            free_lins(arr,narr);free_lins(parr,nparr);lin_free(&d);
            if(bad){lin_free(&L);ok=0;break;}
            int rel;
            if(cp_pred_is(p,"int_lin_eq")||cp_pred_is(p,"bool_lin_eq"))rel=0;
            else if(cp_pred_is(p,"int_lin_le")||cp_pred_is(p,"bool_lin_le"))rel=1;
            else if(cp_pred_is(p,"int_lin_ge")||cp_pred_is(p,"bool_lin_ge"))rel=2;
            else if(cp_pred_is(p,"int_lin_lt")||cp_pred_is(p,"bool_lin_lt"))rel=3;
            else if(cp_pred_is(p,"int_lin_gt")||cp_pred_is(p,"bool_lin_gt"))rel=4;
            else rel=5;
            cp_make_lin(&rec,&L,rel); lin_free(&L);
            if(cp_pred_is(p,"int_lin_eq")||cp_pred_is(p,"bool_lin_eq")||
               cp_pred_is(p,"int_lin_ne")||cp_pred_is(p,"bool_lin_ne")) motivating=1;
        }
        /* ---------- scalar relations ---------- */
        else if(cp_pred_is(p,"int_eq")||cp_pred_is(p,"int_ne")||cp_pred_is(p,"int_le")||
                cp_pred_is(p,"int_lt")||cp_pred_is(p,"int_ge")||cp_pred_is(p,"int_gt")||
                cp_pred_is(p,"bool_eq")||cp_pred_is(p,"bool_ne")||cp_pred_is(p,"bool_le")||
                cp_pred_is(p,"bool_lt")||cp_pred_is(p,"bool_ge")||cp_pred_is(p,"bool_gt")){
            if(c->nargs<2){ok=0;break;}
            if(parse_lin(m,c->args[0],&l1)!=0){ok=0;break;}
            if(parse_lin(m,c->args[1],&l2)!=0){lin_free(&l1);ok=0;break;}
            Lin dd;memset(&dd,0,sizeof(dd)); lin_into(&dd,&l1,1.0); lin_into(&dd,&l2,-1.0);
            lin_free(&l1);lin_free(&l2);
            int rel;
            if(cp_pred_is(p,"int_eq")||cp_pred_is(p,"bool_eq"))rel=0;
            else if(cp_pred_is(p,"int_ne")||cp_pred_is(p,"bool_ne"))rel=5;
            else if(cp_pred_is(p,"int_le")||cp_pred_is(p,"bool_le"))rel=1;
            else if(cp_pred_is(p,"int_lt")||cp_pred_is(p,"bool_lt"))rel=3;
            else if(cp_pred_is(p,"int_ge")||cp_pred_is(p,"bool_ge"))rel=2;
            else rel=4;
            cp_make_lin(&rec,&dd,rel); lin_free(&dd);
            if(cp_pred_is(p,"int_ne")||cp_pred_is(p,"bool_ne")) motivating=1;
        }
        /* ---------- bool_not / bool2int / int_plus / int_minus / int_neg ---------- */
        else if(cp_pred_is(p,"bool_not")){
            if(c->nargs<2){ok=0;break;}
            if(parse_lin(m,c->args[0],&l1)!=0){ok=0;break;}
            if(parse_lin(m,c->args[1],&l2)!=0){lin_free(&l1);ok=0;break;}
            Lin dd;memset(&dd,0,sizeof(dd)); lin_into(&dd,&l1,1.0); lin_into(&dd,&l2,1.0); dd.constant-=1.0;
            lin_free(&l1);lin_free(&l2); cp_make_lin(&rec,&dd,0); lin_free(&dd);
        } else if(cp_pred_is(p,"bool2int")){
            if(c->nargs<2){ok=0;break;}
            if(parse_lin(m,c->args[0],&l1)!=0){ok=0;break;}
            if(parse_lin(m,c->args[1],&l2)!=0){lin_free(&l1);ok=0;break;}
            Lin dd;memset(&dd,0,sizeof(dd)); lin_into(&dd,&l2,1.0); lin_into(&dd,&l1,-1.0);
            lin_free(&l1);lin_free(&l2); cp_make_lin(&rec,&dd,0); lin_free(&dd);
        } else if(cp_pred_is(p,"int_plus")||cp_pred_is(p,"int_minus")){
            int minus=(strcmp(p,"int_minus")==0||strcmp(p,"fzn_int_minus")==0);
            if(c->nargs<3){ok=0;break;}
            if(parse_lin(m,c->args[0],&l1)!=0){ok=0;break;}
            if(parse_lin(m,c->args[1],&l2)!=0){lin_free(&l1);ok=0;break;}
            if(parse_lin(m,c->args[2],&l3)!=0){lin_free(&l1);lin_free(&l2);ok=0;break;}
            Lin dd;memset(&dd,0,sizeof(dd)); lin_into(&dd,&l3,1.0); lin_into(&dd,&l1,-1.0); lin_into(&dd,&l2,minus?1.0:-1.0);
            lin_free(&l1);lin_free(&l2);lin_free(&l3); cp_make_lin(&rec,&dd,0); lin_free(&dd);
        } else if(cp_pred_is(p,"int_neg")){
            if(c->nargs<2){ok=0;break;}
            if(parse_lin(m,c->args[0],&l1)!=0){ok=0;break;}
            if(parse_lin(m,c->args[1],&l2)!=0){lin_free(&l1);ok=0;break;}
            Lin dd;memset(&dd,0,sizeof(dd)); lin_into(&dd,&l2,1.0); lin_into(&dd,&l1,1.0);
            lin_free(&l1);lin_free(&l2); cp_make_lin(&rec,&dd,0); lin_free(&dd);
        }
        /* ---------- all_different ---------- */
        else if(cp_pred_is(p,"all_different")||cp_pred_is(p,"all_different_int")){
            if(c->nargs<1){ok=0;break;}
            if(parse_array(m,c->args[0],&arr,&narr)!=0){ok=0;break;}
            motivating=1;
            rec.kind=CPK_ALLDIFF; rec.n=narr; rec.terms=(CPT*)psolve_calloc((size_t)(narr?narr:1),sizeof(CPT));
            int bad=0;
            for(int i=0;i<narr;i++){ if(cp_term_from_lin(&arr[i],&rec.terms[i])!=0){ bad=1; break; } if(!rec.terms[i].isc) referenced[rec.terms[i].v]=1; }
            free_lins(arr,narr);
            if(bad){ free(rec.terms); ok=0; break; }
        }
        /* ---------- int_times ---------- */
        else if(cp_pred_is(p,"int_times")||cp_pred_is(p,"bool_times")){
            if(c->nargs<3){ok=0;break;}
            if(parse_lin(m,c->args[0],&l1)!=0){ok=0;break;}
            if(parse_lin(m,c->args[1],&l2)!=0){lin_free(&l1);ok=0;break;}
            if(parse_lin(m,c->args[2],&l3)!=0){lin_free(&l1);lin_free(&l2);ok=0;break;}
            rec.kind=CPK_TIMES; rec.n=3; rec.terms=(CPT*)psolve_calloc(3,sizeof(CPT));
            if(cp_term_from_lin(&l1,&rec.terms[0])!=0||cp_term_from_lin(&l2,&rec.terms[1])!=0||cp_term_from_lin(&l3,&rec.terms[2])!=0){ ok=0; }
            if(ok){ for(int i=0;i<3;i++) if(!rec.terms[i].isc) referenced[rec.terms[i].v]=1; }
            lin_free(&l1);lin_free(&l2);lin_free(&l3);
            if(!ok){ free(rec.terms); break; }
            motivating=1;
        }
        /* ---------- int_div / int_mod ---------- */
        else if(cp_pred_is(p,"int_div")||cp_pred_is(p,"int_mod")){
            if(c->nargs<3){ok=0;break;}
            if(parse_lin(m,c->args[0],&l1)!=0){ok=0;break;}
            if(parse_lin(m,c->args[1],&l2)!=0){lin_free(&l1);ok=0;break;}
            if(parse_lin(m,c->args[2],&l3)!=0){lin_free(&l1);lin_free(&l2);ok=0;break;}
            /* constant divisor */
            if(l2.n!=0){lin_free(&l1);lin_free(&l2);lin_free(&l3);ok=0;break;}
            double K=l2.constant;
            if(!isfinite(K)||K!=rint(K)||K==0||fabs(K)>CP_BIG){lin_free(&l1);lin_free(&l2);lin_free(&l3);ok=0;break;}
            int is_div=cp_pred_is(p,"int_div");
            rec.kind=is_div?CPK_DIV:CPK_MOD; rec.n=3; rec.terms=(CPT*)psolve_calloc(3,sizeof(CPT));
            if(cp_term_from_lin(&l1,&rec.terms[0])!=0||cp_term_from_lin(&l3,&rec.terms[2])!=0){ ok=0; }
            rec.terms[1].isc=1; rec.terms[1].c=(int64_t)K;
            if(ok){ if(!rec.terms[0].isc) referenced[rec.terms[0].v]=1; if(!rec.terms[2].isc) referenced[rec.terms[2].v]=1; }
            lin_free(&l1);lin_free(&l2);lin_free(&l3);
            if(!ok){ free(rec.terms); break; }
            motivating=1;
        }
        /* ---------- int_abs ---------- */
        else if(cp_pred_is(p,"int_abs")){
            if(c->nargs<2){ok=0;break;}
            if(parse_lin(m,c->args[0],&l1)!=0){ok=0;break;}
            if(parse_lin(m,c->args[1],&l2)!=0){lin_free(&l1);ok=0;break;}
            rec.kind=CPK_ABS; rec.n=2; rec.terms=(CPT*)psolve_calloc(2,sizeof(CPT));
            if(cp_term_from_lin(&l1,&rec.terms[0])!=0||cp_term_from_lin(&l2,&rec.terms[1])!=0){ok=0;}
            if(ok){ if(!rec.terms[0].isc) referenced[rec.terms[0].v]=1; if(!rec.terms[1].isc) referenced[rec.terms[1].v]=1; }
            lin_free(&l1);lin_free(&l2);
            if(!ok){ free(rec.terms); break; }
            motivating=1;
        }
        /* ---------- array element (incl. var/bool/array_var forms) ---------- */
        else if(cp_pred_is(p,"array_int_element")||cp_pred_is(p,"array_var_int_element")||
                cp_pred_is(p,"array_bool_element")||cp_pred_is(p,"array_var_bool_element")||
                cp_pred_is(p,"array_int_element_nonshifted")||cp_pred_is(p,"array_var_int_element_nonshifted")||
                cp_pred_is(p,"array_bool_element_nonshifted")||cp_pred_is(p,"array_var_bool_element_nonshifted")||
                cp_pred_is(p,"gecode_int_element")||cp_pred_is(p,"gecode_bool_element")||
                cp_pred_is(p,"gecode_var_int_element")||cp_pred_is(p,"gecode_var_bool_element")){
            int gec=(strncmp(p,"gecode_",7)==0);
            int nonshifted=(strstr(p,"_nonshifted")!=NULL);
            int na=gec?4:3;
            if(c->nargs<na){ok=0;break;}
            int idx_arg=0, off_arg=1, arr_arg=gec?2:1, val_arg=gec?3:2;
            long offset= nonshifted?0:1;
            if(gec){ Lin ol; if(parse_lin(m,c->args[off_arg],&ol)!=0){ok=0;break;} if(ol.n!=0&&fabs(ol.constant)<=CP_BIG){offset=(long)llround(ol.constant);} else {lin_free(&ol);ok=0;break;} lin_free(&ol); }
            Lin il; if(parse_lin(m,c->args[idx_arg],&il)!=0){ok=0;break;}
            Lin*arr;int narr; if(parse_array(m,c->args[arr_arg],&arr,&narr)!=0){lin_free(&il);ok=0;break;}
            Lin vl; if(parse_lin(m,c->args[val_arg],&vl)!=0){lin_free(&il);free_lins(arr,narr);ok=0;break;}
            rec.kind=CPK_ELEM; rec.n=2; rec.terms=(CPT*)psolve_calloc(2,sizeof(CPT));
            rec.arr_terms=(CPT*)psolve_calloc((size_t)(narr?narr:1),sizeof(CPT));
            rec.nset=narr;
            int bad=0;
            /* array entries may be constants OR variables (the MiniZinc std
               library builds element arrays like [1, X7, X8, X9]). */
            for(int i=0;i<narr;i++){
                if(cp_term_from_lin(&arr[i],&rec.arr_terms[i])!=0){ bad=1; break; }
                if(!rec.arr_terms[i].isc) referenced[rec.arr_terms[i].v]=1;
            }
            free_lins(arr,narr);
            if(!bad){ if(cp_term_from_lin(&il,&rec.terms[0])!=0) bad=1; if(cp_term_from_lin(&vl,&rec.terms[1])!=0) bad=1; }
            lin_free(&il);lin_free(&vl);
            if(bad){ free(rec.terms); free(rec.arr_terms); ok=0; break; }
            if(!rec.terms[0].isc) referenced[rec.terms[0].v]=1;
            if(!rec.terms[1].isc) referenced[rec.terms[1].v]=1;
            rec.lvars=(int*)psolve_malloc(sizeof(int)); rec.lvars[0]=(int)offset; /* store offset */
            motivating=1;
        }
        /* ---------- int_min / int_max ---------- */
        else if(cp_pred_is(p,"int_min")||cp_pred_is(p,"int_max")){
            if(c->nargs<3){ok=0;break;}
            if(parse_lin(m,c->args[0],&l1)!=0){ok=0;break;}
            if(parse_lin(m,c->args[1],&l2)!=0){lin_free(&l1);ok=0;break;}
            if(parse_lin(m,c->args[2],&l3)!=0){lin_free(&l1);lin_free(&l2);ok=0;break;}
            rec.kind=CPK_MINMAX; rec.n= (cp_pred_is(p,"int_max")?1:0); rec.terms=(CPT*)psolve_calloc(3,sizeof(CPT));
            int bad=0;
            if(cp_term_from_lin(&l1,&rec.terms[0])!=0) bad=1;
            if(cp_term_from_lin(&l2,&rec.terms[1])!=0) bad=1;
            if(cp_term_from_lin(&l3,&rec.terms[2])!=0) bad=1;
            lin_free(&l1);lin_free(&l2);lin_free(&l3);
            if(bad){ free(rec.terms); ok=0; break; }
            for(int i=0;i<3;i++) if(!rec.terms[i].isc) referenced[rec.terms[i].v]=1;
            motivating=1;
        }
        /* ---------- reified relations: r <-> (sum coef*x rel rhs) ---------- */
        else if(cp_pred_is(p,"int_lin_eq_reif")||cp_pred_is(p,"int_lin_ne_reif")||
                cp_pred_is(p,"int_lin_le_reif")||cp_pred_is(p,"int_lin_lt_reif")||
                cp_pred_is(p,"int_lin_ge_reif")||cp_pred_is(p,"int_lin_gt_reif")||
                cp_pred_is(p,"bool_lin_eq_reif")||cp_pred_is(p,"bool_lin_ne_reif")||
                cp_pred_is(p,"bool_lin_le_reif")||cp_pred_is(p,"bool_lin_lt_reif")||
                cp_pred_is(p,"bool_lin_ge_reif")||cp_pred_is(p,"bool_lin_gt_reif")){
            if(c->nargs<4){ok=0;break;}
            Lin*arr;int narr; if(parse_array(m,c->args[0],&arr,&narr)!=0){ok=0;break;}
            Lin*parr;int nparr; if(parse_array(m,c->args[1],&parr,&nparr)!=0){free_lins(arr,narr);ok=0;break;}
            Lin d; if(parse_lin(m,c->args[2],&d)!=0){free_lins(arr,narr);free_lins(parr,nparr);ok=0;break;}
            Lin rl; if(parse_lin(m,c->args[3],&rl)!=0){free_lins(arr,narr);free_lins(parr,nparr);lin_free(&d);ok=0;break;}
            if(narr!=nparr){free_lins(arr,narr);free_lins(parr,nparr);lin_free(&d);lin_free(&rl);ok=0;break;}
            Lin L;memset(&L,0,sizeof(L)); int bad=0;
            for(int i=0;i<narr;i++){ if(arr[i].n!=0||!isfinite(arr[i].constant)){bad=1;break;} lin_into(&L,&parr[i],arr[i].constant); }
            lin_into(&L,&d,-1.0);
            free_lins(arr,narr);free_lins(parr,nparr);lin_free(&d);
            if(bad){lin_free(&L);lin_free(&rl);ok=0;break;}
            CPT rb; if(cp_term_from_lin(&rl,&rb)!=0 || rb.isc){ lin_free(&L);lin_free(&rl);ok=0;break; }
            lin_free(&rl);
            int rel;
            if(cp_pred_is(p,"int_lin_eq_reif")||cp_pred_is(p,"bool_lin_eq_reif"))rel=0;
            else if(cp_pred_is(p,"int_lin_ne_reif")||cp_pred_is(p,"bool_lin_ne_reif"))rel=5;
            else if(cp_pred_is(p,"int_lin_le_reif")||cp_pred_is(p,"bool_lin_le_reif"))rel=1;
            else if(cp_pred_is(p,"int_lin_lt_reif")||cp_pred_is(p,"bool_lin_lt_reif"))rel=3;
            else if(cp_pred_is(p,"int_lin_ge_reif")||cp_pred_is(p,"bool_lin_ge_reif"))rel=2;
            else rel=4;
            rec.kind=CPK_REIF; rec.rel=rel; rec.terms=(CPT*)psolve_calloc(1,sizeof(CPT)); rec.terms[0]=rb;
            rec.n=L.n; rec.lvars=(int*)psolve_malloc((size_t)(L.n?L.n:1)*sizeof(int));
            rec.lcoef=(long long*)psolve_malloc((size_t)(L.n?L.n:1)*sizeof(long long));
            for(int i=0;i<L.n;i++){ rec.lvars[i]=L.idx[i]; rec.lcoef[i]=(long long)llround(L.coef[i]); referenced[L.idx[i]]=1; }
            rec.rhs= -(long long)llround(L.constant);
            lin_free(&L);
            referenced[rb.v]=1;
            motivating=1;
        }
        /* ---------- reified scalar relations ---------- */
        else if(cp_pred_is(p,"int_eq_reif")||cp_pred_is(p,"int_ne_reif")||
                cp_pred_is(p,"int_le_reif")||cp_pred_is(p,"int_lt_reif")||
                cp_pred_is(p,"int_ge_reif")||cp_pred_is(p,"int_gt_reif")||
                cp_pred_is(p,"bool_eq_reif")||cp_pred_is(p,"bool_ne_reif")||
                cp_pred_is(p,"bool_le_reif")||cp_pred_is(p,"bool_lt_reif")||
                cp_pred_is(p,"bool_ge_reif")||cp_pred_is(p,"bool_gt_reif")){
            if(c->nargs<3){ok=0;break;}
            if(parse_lin(m,c->args[0],&l1)!=0){ok=0;break;}
            if(parse_lin(m,c->args[1],&l2)!=0){lin_free(&l1);ok=0;break;}
            if(parse_lin(m,c->args[2],&l3)!=0){lin_free(&l1);lin_free(&l2);ok=0;break;}
            Lin dd;memset(&dd,0,sizeof(dd)); lin_into(&dd,&l1,1.0); lin_into(&dd,&l2,-1.0);
            lin_free(&l1);lin_free(&l2);
            CPT rb; if(cp_term_from_lin(&l3,&rb)!=0 || rb.isc){ lin_free(&dd);lin_free(&l3);ok=0;break; }
            lin_free(&l3);
            int rel;
            if(cp_pred_is(p,"int_eq_reif")||cp_pred_is(p,"bool_eq_reif"))rel=0;
            else if(cp_pred_is(p,"int_ne_reif")||cp_pred_is(p,"bool_ne_reif"))rel=5;
            else if(cp_pred_is(p,"int_le_reif")||cp_pred_is(p,"bool_le_reif"))rel=1;
            else if(cp_pred_is(p,"int_lt_reif")||cp_pred_is(p,"bool_lt_reif"))rel=3;
            else if(cp_pred_is(p,"int_ge_reif")||cp_pred_is(p,"bool_ge_reif"))rel=2;
            else rel=4;
            rec.kind=CPK_REIF; rec.rel=rel; rec.terms=(CPT*)psolve_calloc(1,sizeof(CPT)); rec.terms[0]=rb;
            rec.n=dd.n; rec.lvars=(int*)psolve_malloc((size_t)(dd.n?dd.n:1)*sizeof(int));
            rec.lcoef=(long long*)psolve_malloc((size_t)(dd.n?dd.n:1)*sizeof(long long));
            for(int i=0;i<dd.n;i++){ rec.lvars[i]=dd.idx[i]; rec.lcoef[i]=(long long)llround(dd.coef[i]); referenced[dd.idx[i]]=1; }
            rec.rhs= -(long long)llround(dd.constant);
            lin_free(&dd);
            referenced[rb.v]=1;
            motivating=1;
        }
        /* ---------- bool_clause(pos[], neg[]) ---------- */
        else if(cp_pred_is(p,"bool_clause")){
            if(c->nargs<1){ok=0;break;}
            Lin*pos;int npos; if(parse_array(m,c->args[0],&pos,&npos)!=0){ok=0;break;}
            Lin*neg=NULL;int nneg=0;
            if(c->nargs>=2){ if(parse_array(m,c->args[1],&neg,&nneg)!=0){free_lins(pos,npos);ok=0;break;} }
            rec.kind=CPK_CLAUSE; rec.n=npos; rec.nset=nneg;
            rec.lvars=(int*)psolve_calloc((size_t)(npos?npos:1),sizeof(int));
            rec.terms=(CPT*)psolve_calloc((size_t)(nneg?nneg:1),sizeof(CPT));
            int bad=0;
            for(int i=0;i<npos;i++){ CPT t; if(cp_term_from_lin(&pos[i],&t)!=0 || t.isc){bad=1;break;} rec.lvars[i]=t.v; referenced[t.v]=1; }
            for(int i=0;i<nneg;i++){ CPT t; if(cp_term_from_lin(&neg[i],&t)!=0 || t.isc){bad=1;break;} rec.terms[i]=t; referenced[t.v]=1; }
            free_lins(pos,npos); free_lins(neg,nneg);
            if(bad){ free(rec.lvars); free(rec.terms); ok=0; break; }
            motivating=1;
        }
        /* ---------- set_in ---------- */
        else if(cp_pred_is(p,"set_in")||cp_pred_is(p,"int_in")){
            if(c->nargs<2){ok=0;break;}
            Lin xl; if(parse_lin(m,c->args[0],&xl)!=0){ok=0;break;}
            long long*sv; int sn=parse_int_set_lit(m,c->args[1],&sv);
            if(sn<0){lin_free(&xl);ok=0;break;}
            rec.kind=CPK_SETIN; rec.n=1; rec.terms=(CPT*)psolve_calloc(1,sizeof(CPT));
            if(cp_term_from_lin(&xl,&rec.terms[0])!=0){lin_free(&xl);free(sv);ok=0;break;}
            lin_free(&xl);
            if(!rec.terms[0].isc) referenced[rec.terms[0].v]=1;
            rec.setvals=sv; rec.nset=sn;
            motivating=1;
        }
        /* ---------- int_pow ---------- */
        else if(cp_pred_is(p,"int_pow")||cp_pred_is(p,"int_pow_fixed")){
            if(c->nargs<3){ok=0;break;}
            if(parse_lin(m,c->args[0],&l1)!=0){ok=0;break;}
            if(parse_lin(m,c->args[1],&l2)!=0){lin_free(&l1);ok=0;break;}
            if(parse_lin(m,c->args[2],&l3)!=0){lin_free(&l1);lin_free(&l2);ok=0;break;}
            if(l2.n!=0 || !isfinite(l2.constant)||l2.constant!=rint(l2.constant)||l2.constant<0||l2.constant>1000){lin_free(&l1);lin_free(&l2);lin_free(&l3);ok=0;break;}
            rec.kind=CPK_POW; rec.n=3; rec.terms=(CPT*)psolve_calloc(3,sizeof(CPT));
            rec.terms[1].isc=1; rec.terms[1].c=(int64_t)l2.constant;
            if(cp_term_from_lin(&l1,&rec.terms[0])!=0||cp_term_from_lin(&l3,&rec.terms[2])!=0){ok=0;}
            if(ok){ if(!rec.terms[0].isc) referenced[rec.terms[0].v]=1; if(!rec.terms[2].isc) referenced[rec.terms[2].v]=1; }
            lin_free(&l1);lin_free(&l2);lin_free(&l3);
            if(!ok){ free(rec.terms); break; }
            motivating=1;
        }
        else {
            ok=0; /* unsupported predicate */
        }
        if(!ok) break;
        if(nc>=cap){ cap*=2; cr=(CPCR*)psolve_realloc((void**)&cr,(size_t)cap*sizeof(CPCR)); }
        cr[nc++]=rec;
    }
    if(!ok){ for(int i=0;i<nc;i++){ free(cr[i].lvars);free(cr[i].lcoef);free(cr[i].terms);free(cr[i].setvals);free(cr[i].arr_terms);} free(cr); return -1; }
    if(!motivating){ for(int i=0;i<nc;i++){ free(cr[i].lvars);free(cr[i].lcoef);free(cr[i].terms);free(cr[i].setvals);free(cr[i].arr_terms);} free(cr); return -2; }
    *out_cr=cr; *out_n=nc; *has_cp_motivating=motivating;
    return 0;
}

/* total number of values in all (bounded) domains, clamped for intervals */
static __int128 cp_total(const CP*cp){
    __int128 t=0;
    for(int v=0;v<cp->nvars;v++){
        if(cp->bounded[v]) t+=cp->nvals[v];
        else { if(cp->lo[v]<=cp->hi[v]){ __int128 sz=(__int128)cp->hi[v]-cp->lo[v]+1; t+= sz>4096?4096:sz; } }
    }
    return t;
}

/* propagate all constraints to fixpoint. returns 0 ok, 1 inconsistent, -1 uncertifiable. */
static int cp_propagate(CP*cp,CPCR*cr,int nc){
    for(int pass=0;pass<CP_MAXNODES;pass++){
        __int128 before=cp_total(cp);
        for(int i=0;i<nc;i++){
            int r=0;
            switch(cr[i].kind){
                case CPK_LIN: r=cp_prop_lin(cp,cr[i].n,cr[i].lvars,cr[i].lcoef,cr[i].rhs,cr[i].rel); break;
                case CPK_ALLDIFF: r=cp_prop_alldiff(cp,cr[i].terms,cr[i].n); break;
                case CPK_TIMES: r=cp_prop_times(cp,&cr[i].terms[0],&cr[i].terms[1],&cr[i].terms[2]); break;
                case CPK_DIV: r=cp_prop_divmod(cp,&cr[i].terms[0],cr[i].terms[1].c,&cr[i].terms[2],1); break;
                case CPK_MOD: r=cp_prop_divmod(cp,&cr[i].terms[0],cr[i].terms[1].c,&cr[i].terms[2],0); break;
                case CPK_ABS: r=cp_prop_abs(cp,&cr[i].terms[0],&cr[i].terms[1]); break;
                case CPK_ELEM: r=cp_prop_elem(cp,&cr[i].terms[0],&cr[i].terms[1],cr[i].arr_terms,cr[i].nset,(int)cr[i].lvars[0]); break;
                case CPK_SETIN: r=cp_prop_setin(cp,&cr[i].terms[0],cr[i].setvals,cr[i].nset); break;
                case CPK_POW: r=cp_prop_pow(cp,&cr[i].terms[0],cr[i].terms[1].c,&cr[i].terms[2]); break;
                case CPK_REIF: r=cp_prop_reif(cp,&cr[i]); break;
                case CPK_CLAUSE: r=cp_prop_clause(cp,&cr[i]); break;
                case CPK_MINMAX: r=cp_prop_minmax(cp,&cr[i]); break;
            }
            if(r==1) return 1;
            if(r==-1) return -1;
        }
        if(cp_total(cp)==before) break;
    }
    return 0;
}

/* evaluate a term's fixed value from outx */
static int64_t cp_term_out(const CPT*t,const double*outx){
    if(t->isc) return t->c;
    return (int64_t)llround(outx[t->v]);
}

/* verify all constraints at the fixed assignment outx. returns 1 if satisfied. */
static int cp_verify(const CP*cp,CPCR*cr,int nc,const double*outx){
    for(int i=0;i<nc;i++){
        const CPCR*r=&cr[i];
        if(r->kind==CPK_LIN){
            __int128 s=0; for(int k=0;k<r->n;k++) s+=(__int128)r->lcoef[k]*(int64_t)llround(outx[r->lvars[k]]);
            switch(r->rel){case 0: if(s!=r->rhs)return 0;break;case 1: if(s>r->rhs)return 0;break;case 2: if(s<r->rhs)return 0;break;case 3: if(s>=r->rhs)return 0;break;case 4: if(s<=r->rhs)return 0;break;case 5: if(s==r->rhs)return 0;break;}
        } else if(r->kind==CPK_ALLDIFF){
            for(int a=0;a<r->n;a++){ int64_t va=cp_term_out(&r->terms[a],outx); for(int b=a+1;b<r->n;b++){ int64_t vb=cp_term_out(&r->terms[b],outx); if(va==vb)return 0; } }
        } else if(r->kind==CPK_TIMES){
            int64_t a=cp_term_out(&r->terms[0],outx), b=cp_term_out(&r->terms[1],outx), c=cp_term_out(&r->terms[2],outx);
            if((__int128)a*b!=c)return 0;
        } else if(r->kind==CPK_DIV){ if(cp_term_out(&r->terms[2],outx) != cp_term_out(&r->terms[0],outx)/r->terms[1].c) return 0; }
        else if(r->kind==CPK_MOD){ if(cp_term_out(&r->terms[2],outx) != cp_term_out(&r->terms[0],outx)%r->terms[1].c) return 0; }
        else if(r->kind==CPK_ABS){ int64_t a=cp_term_out(&r->terms[0],outx); if(cp_term_out(&r->terms[1],outx) != (a<0?-a:a)) return 0; }
        else if(r->kind==CPK_ELEM){ int64_t idx=cp_term_out(&r->terms[0],outx); long k=(long)(idx - (int)r->lvars[0]); if(k<0||k>=r->nset)return 0; if(cp_term_out(&r->terms[1],outx)!=cp_term_out(&r->arr_terms[k],outx)) return 0; }
        else if(r->kind==CPK_SETIN){ int64_t x=cp_term_out(&r->terms[0],outx); int found=0; for(int k=0;k<r->nset;k++) if(r->setvals[k]==x){found=1;break;} if(!found)return 0; }
        else if(r->kind==CPK_POW){ int64_t b=cp_term_out(&r->terms[0],outx), e=r->terms[1].c, c=cp_term_out(&r->terms[2],outx); __int128 p=1; for(int64_t q=0;q<e;q++){p*=(__int128)b;if(p>INT64_MAX||p<INT64_MIN)return 0;} if(p!=c)return 0; }
        else if(r->kind==CPK_REIF){
            int rv=cp_term_out(&r->terms[0],outx);
            __int128 s=0; for(int k=0;k<r->n;k++) s+=(__int128)r->lcoef[k]*(int64_t)llround(outx[r->lvars[k]]);
            int holds=0; switch(r->rel){case 0:holds=(s==r->rhs);break;case 1:holds=(s<=r->rhs);break;case 2:holds=(s>=r->rhs);break;case 3:holds=(s<r->rhs);break;case 4:holds=(s>r->rhs);break;case 5:holds=(s!=r->rhs);break;}
            if((rv!=0)!=(holds!=0)) return 0;
        }
        else if(r->kind==CPK_CLAUSE){
            int sat=0;
            for(int k=0;k<r->n;k++){ int64_t bv=cp_term_out(&(CPT){.isc=0,.v=r->lvars[k]},outx); if(bv==1){sat=1;break;} }
            if(!sat) for(int k=0;k<r->nset;k++){ int64_t bv=cp_term_out(&r->terms[k],outx); if(bv==0){sat=1;break;} }
            if(!sat) return 0;
        }
        else if(r->kind==CPK_MINMAX){
            int64_t a=cp_term_out(&r->terms[0],outx), b=cp_term_out(&r->terms[1],outx), m=cp_term_out(&r->terms[2],outx);
            int64_t res=(r->n? (a>b?a:b) : (a<b?a:b));
            if(res!=m) return 0;
        }
    }
    return 1;
}

/* search.  Returns:
 *   2 solution found (single mode)
 *   3 all-solutions mode: enumeration of this subtree complete
 *   4 all-solutions mode: node limit reached
 *   0 single mode: exhausted, no solution (UNSAT)
 *  -1 uncertifiable (unbounded referenced var / node limit / verify failed)
 * When `all` is set, each complete assignment is emitted via on_solution and
 * search continues (backtracks) to find the next distinct one.
 */
static int cp_solve_rec(CP*cp,CPCR*cr,int nc,unsigned char*referenced,double*outx,int depth,
                        int all,void(*on_solution)(const double*,double,void*),void*ud){
    cp->nodes++;
    if(cp->nodes>CP_MAXNODES) return all?4:-1;
    int pr=cp_propagate(cp,cr,nc);
    if(pr==1) return 0;
    if(pr==-1) return -1;
    int best=-1,bestsz=INT_MAX;
    for(int v=0;v<cp->nvars;v++){
        if(cp->bounded[v] && !cp_fixed(cp,v)){
            int s=cp->nvals[v]; if(s<bestsz){bestsz=s;best=v;}
        }
    }
    if(best==-1){
        /* all searchable vars fixed. Any referenced unbounded/interval var must be
           fixed (lo==hi) else we cannot certify. */
        for(int v=0;v<cp->nvars;v++){
            if(!cp->bounded[v] && referenced[v] && !cp_fixed(cp,v)) return -1;
        }
        for(int v=0;v<cp->nvars;v++){ if(cp_fixed(cp,v)) outx[v]=(double)cp->lo[v]; else outx[v]=0.0; }
        if(!cp_verify(cp,cr,nc,outx)) return -1;   /* verification failed -> do not emit a wrong answer */
        if(all && on_solution){ on_solution(outx,0.0,ud); return 3; }
        return 2;
    }
    int64_t *vals=cp->vals[best]; int nvals=cp->nvals[best];
    for(int i=0;i<nvals;i++){
        CP br; cp_copy(&br,cp);
        cp_assign(&br,best,vals[i]);
        int rr=cp_solve_rec(&br,cr,nc,referenced,outx,depth+1,all,on_solution,ud);
        cp_free(&br);
        if(rr==2) return 2;   /* single-mode solution found */
        if(rr==4) return 4;   /* all-mode node limit */
        if(rr==-1) return -1; /* uncertifiable */
        /* rr==3 (all-mode subtree done) or rr==0 (dead branch): try next value */
    }
    return all?3:0;
}

/* Bound every variable in a product-chain that equals a fixed nonzero
   constant C to the (small) set of signed divisors of C.  In a monomial
   product equal to C every factor and every intermediate product divides C,
   so this is exact (no valid solution is lost) and makes the search finite.
   Returns 1 if inconsistent (a chain variable's declared domain excludes all
   divisors), -1 on error, 0 otherwise. */
static int cp_bound_diophantine(CP*cp,CPCR*cr,int nc){
    int64_t C=0; int found=0;
    for(int i=0;i<nc;i++) if(cr[i].kind==CPK_TIMES){
        CPT*c=&cr[i].terms[2];
        if(c->isc){ if(c->c!=0){ C=c->c; found=1; } }
        else if(cp_fixed(cp,c->v) && cp->vals[c->v][0]!=0){ C=cp->vals[c->v][0]; found=1; }
        if(found) break;
    }
    if(!found) return 0;
    unsigned char *mark=(unsigned char*)psolve_calloc((size_t)(cp->nvars?cp->nvars:1),1);
    /* seed: operands of a times constraint whose result is fixed to C */
    for(int i=0;i<nc;i++) if(cr[i].kind==CPK_TIMES){
        CPT*c=&cr[i].terms[2];
        int cIsC = c->isc ? (c->c==C) : (cp_fixed(cp,c->v) && cp->vals[c->v][0]==C);
        if(cIsC){ for(int k=0;k<2;k++){ CPT*t=&cr[i].terms[k]; if(!t->isc) mark[t->v]=1; } if(!c->isc) mark[c->v]=1; }
    }
    /* closure over the product graph */
    int changed=1;
    while(changed){ changed=0;
        for(int i=0;i<nc;i++) if(cr[i].kind==CPK_TIMES){
            CPT*t0=&cr[i].terms[0],*t1=&cr[i].terms[1],*t2=&cr[i].terms[2];
            int in = (t0->isc?0:mark[t0->v]) || (t1->isc?0:mark[t1->v]) ||
                     (t2->isc?0:mark[t2->v]);
            if(in){ for(int k=0;k<3;k++){ CPT*t=&cr[i].terms[k]; if(!t->isc && !mark[t->v]){ mark[t->v]=1; changed=1; } } }
        }
    }
    int64_t dv[2048]; int nd=cp_divisors(C,dv,2048);
    int ret=0;
    if(nd<=0) ret=1;
    else for(int v=0;v<cp->nvars;v++) if(mark[v]){ int r=cp_restrict_divisors(cp,v,dv,nd); if(r==1){ret=1;break;} if(r==-1){ret=-1;break;} }
    free(mark);
    return ret;
}

/* top-level CP attempt. returns 1 if handled (sol->status set), 0 to fall back to MIP. */
static int fz_cp_try(const FZModel*m,FZSolution*sol){
    int nv=m->nvars;
    if(nv<=0) return 0;
    int all = sol->all_solutions;
    /* For all-solutions enumeration, only engage CP when every decision
       variable is finitely bounded (so the enumeration is provably complete)
       and the model is pure integer. */
    if(all){
        for(int d=0;d<m->ndecl;d++){ FZDecl*decl=&m->decls[d];
            if(!decl->is_var||decl->base_idx<0||decl->is_alias) continue;
            if(decl->kind==FZ_K_FLOAT) return 0;
            if(!decl->has_lo || !decl->has_hi) return 0;
        }
    }
    /* CP only handles integer models */
    for(int d=0;d<m->ndecl;d++){
        FZDecl*decl=&m->decls[d];
        if(decl->is_var && !decl->is_alias && decl->kind==FZ_K_FLOAT) return 0;
    }
    unsigned char *referenced=(unsigned char*)psolve_calloc((size_t)(nv?nv:1),1);
    CPCR*cr=NULL; int nc=0; int motivating=0;
    int cp_parse_rc=cp_parse(m,&cr,&nc,referenced,&motivating);
    if(cp_parse_rc!=0){ free(referenced); return 0; }

    CP cp; memset(&cp,0,sizeof(cp));
    cp.nvars=nv;
    cp.bounded=(int*)psolve_calloc((size_t)(nv?nv:1),sizeof(int));
    cp.isbool=(int*)psolve_calloc((size_t)(nv?nv:1),sizeof(int));
    cp.lo=(int64_t*)psolve_malloc((size_t)(nv?nv:1)*sizeof(int64_t));
    cp.hi=(int64_t*)psolve_malloc((size_t)(nv?nv:1)*sizeof(int64_t));
    cp.vals=(int64_t**)psolve_calloc((size_t)(nv?nv:1),sizeof(int64_t*));
    cp.nvals=(int*)psolve_calloc((size_t)(nv?nv:1),sizeof(int));
    cp.cap=(int*)psolve_calloc((size_t)(nv?nv:1),sizeof(int));
    for(int v=0;v<nv;v++){ cp.lo[v]=INT64_MIN; cp.hi[v]=INT64_MAX; }

    /* initial domains from declarations */
    for(int d=0;d<m->ndecl;d++){
        FZDecl*decl=&m->decls[d];
        if(!decl->is_var||decl->base_idx<0||decl->is_alias) continue;
        for(int e=0;e<decl->n;e++){
            int v=decl->base_idx+e; if(v<0||v>=nv) continue;
            if(decl->kind==FZ_K_BOOL){ cp.isbool[v]=1; cp_intersect(&cp,v,0,1); continue; }
            if(decl->nset>0 && decl->n==1){
                cp.isbool[v]=0;
                if(cp_set_vals(&cp,v,decl->setvals,decl->nset)<0){ cp_set_vals(&cp,v,decl->setvals,0); }
                continue;
            }
            if(decl->has_lo){ int64_t lo=(int64_t)llround(decl->lo[0]); if(cp.lo[v]<lo)cp.lo[v]=lo; }
            if(decl->has_hi){ int64_t hi=(int64_t)llround(decl->hi[0]); if(cp.hi[v]>hi)cp.hi[v]=hi; }
            if(cp_intersect(&cp,v,cp.lo[v],cp.hi[v])){ /* empty initial domain -> UNSAT */
                cp_free(&cp); for(int i=0;i<nc;i++){ free(cr[i].lvars);free(cr[i].lcoef);free(cr[i].terms);free(cr[i].setvals);free(cr[i].arr_terms);} free(cr); free(referenced);
                sol->status=1; sol->nvars=nv; sol->nodes=0; return 1;
            }
        }
    }

    /* If the model is a monomial product equal to a fixed constant (e.g.
       x*x*x*y*y = 100000 with unbounded x,y), bound the whole product chain
       to the divisors of that constant so search is finite and correct. */
    int bd=cp_bound_diophantine(&cp,cr,nc);
    if(bd==1){ cp_free(&cp); for(int i=0;i<nc;i++){ free(cr[i].lvars);free(cr[i].lcoef);free(cr[i].terms);free(cr[i].setvals);free(cr[i].arr_terms);} free(cr); free(referenced); sol->status=1; sol->nvars=nv; return 1; }
    if(bd==-1){ cp_free(&cp); for(int i=0;i<nc;i++){ free(cr[i].lvars);free(cr[i].lcoef);free(cr[i].terms);free(cr[i].setvals);free(cr[i].arr_terms);} free(cr); free(referenced); return 0; }

    double *outx=sol->x; /* sized nv */

    int rr;
    if(all){
        /* set up the dedup context (as the MIP path does) so fz_solution_cb
           prints each distinct output solution exactly once. */
        FZSolveCtx ctx; memset(&ctx,0,sizeof(ctx)); ctx.m=m; ctx.sol=sol;
        ctx.dedupe_outputs=1;
        for(int d=0;d<m->ndecl;d++)if(m->decls[d].is_output){
            if((size_t)m->decls[d].n>SIZE_MAX-ctx.key_len)psolve_fail(PSOLVE_ERR_OOM);
            ctx.key_len+=(size_t)m->decls[d].n;
        }
        if(ctx.key_len>SIZE_MAX/sizeof(uint64_t))psolve_fail(PSOLVE_ERR_OOM);
        ctx.scratch=(uint64_t*)psolve_calloc(ctx.key_len?ctx.key_len:1,sizeof(uint64_t));
        rr=cp_solve_rec(&cp,cr,nc,referenced,outx,0,1,fz_solution_cb,&ctx);
        fz_solve_ctx_free(&ctx);
    } else {
        rr=cp_solve_rec(&cp,cr,nc,referenced,outx,0,0,NULL,NULL);
    }

    int handled=1;
    if(rr==2){
        sol->status=0; sol->nodes=cp.nodes;
        /* Honesty convention (mirrors the LP/MIP bridge): an undeclared
           `var int/float` (no explicit bounds) is treated as clamped to the
           synthetic ±FZ_BIG_BOUND box.  A solution that touches that box
           cannot be certified as a genuine answer for the truly unbounded
           variable, so downgrade to UNKNOWN rather than print a fabricated
           solution. */
        for(int d=0;d<m->ndecl;d++){ FZDecl*decl=&m->decls[d];
            if(!decl->is_var||decl->base_idx<0||decl->is_alias) continue;
            if(decl->has_lo && decl->has_hi) continue;
            int bad=0;
            for(int e=0;e<decl->n;e++){ int v=decl->base_idx+e; if(v<0||v>=nv) continue;
                double val=sol->x[v];
                if(val>FZ_BIG_BOUND-FZ_BIG_HIT_TOL || val< -FZ_BIG_BOUND+FZ_BIG_HIT_TOL){ bad=1; break; } }
            if(bad){ sol->status=2; break; }
        }
    }
    else if(rr==0){ sol->status=1; sol->nodes=cp.nodes; }
    else if(all && (rr==3)){ sol->status=0; sol->nodes=cp.nodes; }
    else if(all && rr==4){ sol->nodes=cp.nodes; sol->status=4; }
    else { handled=0; } /* fallback */

    cp_free(&cp);
    for(int i=0;i<nc;i++){ free(cr[i].lvars);free(cr[i].lcoef);free(cr[i].terms);free(cr[i].setvals);free(cr[i].arr_terms);} free(cr);
    free(referenced);
    if(!handled) return 0;
    return 1;
}

#endif /* PSOLVE_FZ_CP_INC */
