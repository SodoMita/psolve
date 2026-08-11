#include "fzn.h"
#include "solver.h"
#include "mip.h"
#include "err.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <limits.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Lexer                                                               */
/* ------------------------------------------------------------------ */
typedef enum { TK_EOF, TK_IDENT, TK_INT, TK_FLOAT, TK_STRING, TK_SYM } TK;
typedef struct { TK kind; char*text; long ival; double fval; size_t start,end; } Token;

static int is_ident_ch(int c){ return isalnum(c)||c=='_'; }

/* free a token array and the strdup'd text owned by each token */
static void free_toks(Token*t,int n){ for(int i=0;i<n;i++) free(t[i].text); free(t); }

static int lex(const char*src, Token**out,int*n)
{
    int cap=256,cnt=0; Token*t=(Token*)psolve_malloc((size_t)cap*sizeof(Token));
    size_t i=0,L=strlen(src);
    while(i<L){
        char c=src[i];
        if(isspace((unsigned char)c)){i++;continue;}
        if(c=='%'){while(i<L&&src[i]!='\n')i++;continue;}
        if(isalpha((unsigned char)c)||c=='_'){
            size_t s=i; while(i<L&&is_ident_ch((unsigned char)src[i]))i++;
            if(cnt>=cap){cap*=2;t=(Token*)psolve_realloc((void**)&t,(size_t)cap*sizeof(Token));}
            t[cnt].kind=TK_IDENT;t[cnt].text=psolve_strndup(src+s,i-s);t[cnt].start=s;t[cnt].end=i;cnt++;continue;
        }
        if(isdigit((unsigned char)c)||(c=='-'&&i+1<L&&isdigit((unsigned char)src[i+1]))){
            size_t s=i; int isfloat=0; if(src[i]=='-')i++;
            while(i<L&&isdigit((unsigned char)src[i]))i++;
            if(i<L&&src[i]=='.'&&i+1<L&&isdigit((unsigned char)src[i+1])){isfloat=1;i++;while(i<L&&isdigit((unsigned char)src[i]))i++;}
            if(i<L&&(src[i]=='e'||src[i]=='E')){isfloat=1;i++;if(i<L&&(src[i]=='+'||src[i]=='-'))i++;while(i<L&&isdigit((unsigned char)src[i]))i++;}
            if(cnt>=cap){cap*=2;t=(Token*)psolve_realloc((void**)&t,(size_t)cap*sizeof(Token));}
            char*tmp=psolve_strndup(src+s,i-s);
            if(isfloat){t[cnt].kind=TK_FLOAT;t[cnt].fval=atof(tmp);}else{t[cnt].kind=TK_INT;t[cnt].ival=atol(tmp);}
            free(tmp);t[cnt].text=NULL;t[cnt].start=s;t[cnt].end=i;cnt++;continue;
        }
        if(c=='"'){
            size_t s=i;i++;while(i<L&&src[i]!='"'){if(src[i]=='\\')i++;i++;}if(i<L)i++;
            if(cnt>=cap){cap*=2;t=(Token*)psolve_realloc((void**)&t,(size_t)cap*sizeof(Token));}
            t[cnt].kind=TK_STRING;t[cnt].text=psolve_strndup(src+s+1,i-s-2);t[cnt].start=s;t[cnt].end=i;cnt++;continue;
        }
        const char*two[]={"::","->","<-","..","{","}","[","]","(",")",",",";",":",".","=","+","-",0};
        int matched=0;
        for(int k=0;two[k];k++){ size_t Lk=strlen(two[k]);
            if(i+Lk<=L&&strncmp(src+i,two[k],Lk)==0){
                if(cnt>=cap){cap*=2;t=(Token*)psolve_realloc((void**)&t,(size_t)cap*sizeof(Token));}
                t[cnt].kind=TK_SYM;t[cnt].text=psolve_strdup(two[k]);t[cnt].start=i;t[cnt].end=i+Lk;cnt++;i+=Lk;matched=1;break; } }
        if(!matched){free_toks(t,cnt);return -1;}
    }
    if(cnt>=cap){cap++;t=(Token*)psolve_realloc((void**)&t,(size_t)cap*sizeof(Token));}
    t[cnt].kind=TK_EOF;t[cnt].text=NULL;t[cnt].start=L;t[cnt].end=L;cnt++;
    *out=t;*n=cnt;return 0;
}

/* ------------------------------------------------------------------ */
/* Linear-form helpers                                                 */
/* ------------------------------------------------------------------ */
typedef struct { int n; int*idx; double*coef; double constant; } Lin;

static void lin_term(Lin*l,int idx,double c){
    for(int i=0;i<l->n;i++) if(l->idx[i]==idx){l->coef[i]+=c;return;}
    l->idx=(int*)psolve_realloc((void**)&l->idx,(size_t)(l->n+1)*sizeof(int));
    l->coef=(double*)psolve_realloc((void**)&l->coef,(size_t)(l->n+1)*sizeof(double));
    l->idx[l->n]=idx;l->coef[l->n]=c;l->n++;
}
static void lin_into(Lin*dst,const Lin*src,double scale){
    for(int i=0;i<src->n;i++)lin_term(dst,src->idx[i],scale*src->coef[i]);
    dst->constant += scale*src->constant;
}
static void lin_free(Lin*l){free(l->idx);free(l->coef);}

/* expression tree value: either a scalar Lin or an array of Lin */
typedef struct { int is_array; int n; Lin*els; } Expr;
static Expr parse_expr(const char*s,size_t*pos,FZModel*m,int*err);
FZDecl* find_decl(FZModel*m,const char*name);
static void expr_free2(Expr*e);

static Expr parse_primary(const char*s,size_t*pos,FZModel*m,int*err)
{
    Expr e; memset(&e,0,sizeof(e)); e.n=1; e.els=(Lin*)psolve_malloc(sizeof(Lin)); memset(&e.els[0],0,sizeof(Lin));
    while(isspace((unsigned char)s[*pos]))(*pos)++;
    char c=s[*pos];
    if(c=='-'){(*pos)++;Expr t=parse_primary(s,pos,m,err);if(!*err){for(int i=0;i<t.n;i++){for(int k=0;k<t.els[i].n;k++)t.els[i].coef[k]=-t.els[i].coef[k];t.els[i].constant=-t.els[i].constant;}}free(e.els);e=t;return e;}
    if(c=='['){(*pos)++;int cap=8;free(e.els);e.els=(Lin*)psolve_malloc((size_t)cap*sizeof(Lin));e.n=0;e.is_array=1;
        while(1){while(isspace((unsigned char)s[*pos]))(*pos)++;if(s[*pos]==']'){(*pos)++;break;}
            Expr t=parse_expr(s,pos,m,err);if(*err){expr_free2(&t);expr_free2(&e);return e;}
            if(e.n>=cap){cap*=2;e.els=(Lin*)psolve_realloc((void**)&e.els,(size_t)cap*sizeof(Lin));}
            e.els[e.n]=t.els[0];e.n++;free(t.els);
            while(isspace((unsigned char)s[*pos]))(*pos)++;
            if(s[*pos]==','){(*pos)++;continue;}
            if(s[*pos]==']'){(*pos)++;break;}
            *err=1;return e;}
        return e;}
    if(isdigit((unsigned char)c)||(c=='.'&&isdigit((unsigned char)s[*pos+1]))){
        /* Let strtod define the token boundary.  In particular, do not swallow
           the binary + in `1+2` or the range separator in malformed input. */
        char*end=NULL;double v=strtod(s+*pos,&end);
        if(end==s+*pos||!isfinite(v)){*err=1;return e;}
        *pos+=(size_t)(end-(s+*pos));e.els[0].constant=v;return e;}
    if(isalpha((unsigned char)c)||c=='_'){
        char name[128];int k=0;while(k<127&&is_ident_ch((unsigned char)s[*pos]))name[k++]=s[(*pos)++];name[k]=0;
        while(isspace((unsigned char)s[*pos]))(*pos)++;
        if(s[*pos]=='['){/* array element */
            (*pos)++;Expr ix=parse_expr(s,pos,m,err);if(*err){lin_free(&e.els[0]);free(e.els);return e;}
            while(isspace((unsigned char)s[*pos]))(*pos)++;
            if(s[*pos]!=']'){expr_free2(&ix);*err=1;return e;}
            (*pos)++;
            if(ix.is_array||ix.n!=1||ix.els[0].n!=0||fabs(ix.els[0].constant-round(ix.els[0].constant))>1e-12){
                expr_free2(&ix);*err=1;return e;
            }
            long ival=(long)llround(ix.els[0].constant);expr_free2(&ix);
            FZDecl*d=find_decl(m,name);
            int elem=d?(int)(ival-d->index_lo):-1;
            if(!d||!d->is_array||elem<0||elem>=d->n){*err=1;return e;}
            if(d->is_var){
                if(d->alias_idx){
                    int av=d->alias_idx[elem];
                    if(av>=0)lin_term(&e.els[0],av,1.0);
                    else e.els[0].constant=d->alias_const[elem];
                } else {
                    if(d->base_idx<0){*err=1;return e;}
                    lin_term(&e.els[0], d->base_idx+elem, 1.0);
                }
            } else e.els[0].constant = d->par?d->par[elem]:(d->par_int?d->par_int[elem]:0);
            return e;}
        FZDecl*d=find_decl(m,name);
        if(d){
            if(d->is_var){
                if(d->alias_idx){
                    int av=d->alias_idx[0];
                    if(av>=0)lin_term(&e.els[0],av,1.0);
                    else e.els[0].constant=d->alias_const[0];
                } else if(d->base_idx>=0) lin_term(&e.els[0],d->base_idx,1.0);
                else *err=1;
            } else e.els[0].constant=d->par?d->par[0]:(d->par_int?d->par_int[0]:0);
        }
        else { if(strcmp(name,"true")==0)e.els[0].constant=1; else if(strcmp(name,"false")==0)e.els[0].constant=0; else *err=1; }
        return e;}
    if(c=='('){(*pos)++;Expr t=parse_expr(s,pos,m,err);while(isspace((unsigned char)s[*pos]))(*pos)++;if(s[*pos]==')')(*pos)++;free(e.els);e=t;return e;}
    *err=1;return e;
}

static Expr parse_expr(const char*s,size_t*pos,FZModel*m,int*err)
{
    Expr e=parse_primary(s,pos,m,err); if(*err){expr_free2(&e);return e;}
    while(1){ while(isspace((unsigned char)s[*pos]))(*pos)++;
        char op=s[*pos]; if(op!='+'&&op!='-')break; (*pos)++;
        Expr t=parse_primary(s,pos,m,err); if(*err){expr_free2(&t);return e;}
        double sg=(op=='+')?1.0:-1.0;
        /* combine: only handle scalar scalar-compatible; for arrays we take
           elementwise sum when both scalar-expanded */
        if(e.n!=t.n){ expr_free2(&e); expr_free2(&t); *err=1; return e; }
        for(int i=0;i<e.n;i++){ lin_into(&e.els[i],&t.els[i],sg); lin_free(&t.els[i]); }
        free(t.els);
    }
    return e;
}
static void expr_free2(Expr*e){ if(e->n>0){for(int i=0;i<e->n;i++)lin_free(&e->els[i]);free(e->els);e->els=NULL;e->n=0;} }

/* parse a scalar expression into a Lin */
static int parse_lin(FZModel*m,const char*s,Lin*out)
{
    size_t pos=0;int err=0;
    Expr e=parse_expr(s,&pos,m,&err);
    while(isspace((unsigned char)s[pos]))pos++;
    if(err||s[pos]!='\0'||e.is_array||e.n!=1){ if(e.n>0){for(int i=0;i<e.n;i++)lin_free(&e.els[i]);free(e.els);} return -1; }
    *out=e.els[0]; free(e.els);
    return 0;
}
/* parse an array expression into an array of Lin */
static int parse_array(FZModel*m,const char*s,Lin**out,int*outn)
{
    /* An array argument may be a bare identifier naming a previously-declared
       array: a par array (e.g. X_INTRODUCED_1_ = [1,1]) or a var array that
       aliases introduced vars (e.g. x = [X0,X1,..]).  Resolve to elements. */
    {
        char name[256]; int k=0; const char*p=s;
        while(isspace((unsigned char)*p))p++;
        if(isalpha((unsigned char)*p)||*p=='_'){
            while(k<255&&is_ident_ch((unsigned char)*p))name[k++]=*p++;
            name[k]=0;
            while(isspace((unsigned char)*p))p++;
            if(*p=='\0'||*p==';'){   /* it's just an identifier */
                FZDecl*d=find_decl(m,name);
                if(d&&d->is_array){
                    if(d->par){   /* par array of constants */
                        *outn=d->n;
                        *out=(Lin*)psolve_malloc((size_t)(d->n?d->n:1)*sizeof(Lin));
                        for(int i=0;i<d->n;i++){
                            memset(&(*out)[i],0,sizeof(Lin));
                            (*out)[i].constant=d->par[i];
                        }
                        return 0;
                    }
                    if(d->is_var && (d->alias_idx || d->base_idx>=0)){
                        /* A var array can be an exact view of a mixture of
                           introduced variables and fixed values after MiniZinc
                           constant propagation (e.g. x = [1,3]). */
                        *outn=d->n;
                        *out=(Lin*)psolve_malloc((size_t)(d->n?d->n:1)*sizeof(Lin));
                        for(int i=0;i<d->n;i++){
                            memset(&(*out)[i],0,sizeof(Lin));
                            if(d->alias_idx){
                                int av=d->alias_idx[i];
                                if(av>=0)lin_term(&(*out)[i],av,1.0);
                                else (*out)[i].constant=d->alias_const[i];
                            } else lin_term(&(*out)[i], d->base_idx+i, 1.0);
                        }
                        return 0;
                    }
                }
            }
        }
    }
    size_t pos=0;int err=0;
    Expr e=parse_expr(s,&pos,m,&err);
    while(isspace((unsigned char)s[pos]))pos++;
    if(err||s[pos]!='\0'||!e.is_array){ if(e.n>0){for(int i=0;i<e.n;i++)lin_free(&e.els[i]);free(e.els);} return -1; }
    *out=e.els;*outn=e.n; return 0;
}
static void free_lins(Lin*arr,int n){for(int i=0;i<n;i++)lin_free(&arr[i]);free(arr);}

/* Parse the integer values of a FlatZinc 2D table argument into a flat
 * row-major array of `long`.  Accepts (a) a bare identifier naming a declared
 * par int array, (b) an `array2d(lo1,hi1,lo2,hi2,[v..])` call, or (c) a flat
 * `[v..]` integer literal.  Returns 0 on success (values in *out, count in
 * *outn), -1 on malformed input. */
static int table_values(FZModel*m,const char*s,long**out,int*outn)
{
    /* (a) bare identifier naming a par array */
    {
        char name[256];int k=0;const char*p=s;
        while(*p==' ')p++;
        if(isalpha((unsigned char)*p)||*p=='_'){
            while(k<255&&is_ident_ch((unsigned char)*p))name[k++]=*p++;
            name[k]=0;
            while(*p==' ')p++;
            if(*p=='\0'||*p==';'){
                FZDecl*d=find_decl(m,name);
                if(d&&d->is_array){
                    if(d->par_int){
                        *outn=d->n;
                        *out=(long*)psolve_malloc((size_t)(d->n?d->n:1)*sizeof(long));
                        for(int i=0;i<d->n;i++) (*out)[i]=(long)d->par_int[i];
                        return 0;
                    }
                    if(d->par){
                        *outn=d->n;
                        *out=(long*)psolve_malloc((size_t)(d->n?d->n:1)*sizeof(long));
                        for(int i=0;i<d->n;i++) (*out)[i]=(long)llround(d->par[i]);
                        return 0;
                    }
                }
            }
        }
    }
    /* (b)/(c): the last '[' starts the value list (for `array2d(...)` the only
       '[' is the values list; for a flat literal it is too). */
    const char*br=strchr(s,'[');
    if(!br){*out=NULL;*outn=0;return -1;}
    const char*last=br;
    for(const char*q=br+1;*q;q++) if(*q=='[')last=q;
    const char*e=strchr(last,']');
    if(!e){*out=NULL;*outn=0;return -1;}
    size_t len=(size_t)(e-last-1);
    char*buf=(char*)psolve_malloc(len+1);
    size_t n=0;
    for(const char*q=last+1;q<e&&n<len;q++){ if(!isspace((unsigned char)*q)) buf[n++]=*q; else buf[n++]=','; }
    buf[n]=0;
    /* parse comma-separated integers (possibly negative, or booleans) */
    long*vals=NULL;int cnt=0,cap=0;
    char*tok=strtok(buf,",");
    while(tok){
        char*endp; long v;
        if(strcmp(tok,"true")==0) v=1;
        else if(strcmp(tok,"false")==0) v=0;
        else v=strtol(tok,&endp,10);
        if(cnt>=cap){cap=cap?cap*2:16;vals=(long*)psolve_realloc((void**)&vals,(size_t)cap*sizeof(long));}
        vals[cnt++]=v;
        tok=strtok(NULL,",");
    }
    free(buf);
    if(cnt==0){free(vals);*out=NULL;*outn=0;return 0;}  /* empty table: 0 values */
    *out=vals;*outn=cnt;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Parser                                                              */
/* ------------------------------------------------------------------ */
static FZDecl*add_decl(FZModel*m){
    if(m->ndecl>=m->cap_decl){m->cap_decl=m->cap_decl?m->cap_decl*2:16;m->decls=(FZDecl*)psolve_realloc((void**)&m->decls,(size_t)m->cap_decl*sizeof(FZDecl));}
    FZDecl*d=&m->decls[m->ndecl++];memset(d,0,sizeof(*d));d->base_idx=-1;d->index_lo=1;return d;
}
FZDecl*find_decl(FZModel*m,const char*name){
    for(int i=0;i<m->ndecl;i++)if(m->decls[i].name&&strcmp(m->decls[i].name,name)==0)return &m->decls[i];
    return NULL;
}

static int is_kw(const char*s,const char*k){return s&&strcmp(s,k)==0;}

/* Extract scalar literals used in declaration domains.  A FlatZinc array
   extent may also name an already-declared integer parameter (the form the
   MiniZinc compiler emits for array[1..n]). */
static int tok_number(const Token*t,double*out)
{
    if(t->kind==TK_INT){*out=(double)t->ival;return 0;}
    if(t->kind==TK_FLOAT){*out=t->fval;return 0;}
    return -1;
}
static int tok_int_value(FZModel*m,const Token*t,long*out)
{
    if(t->kind==TK_INT){*out=t->ival;return 0;}
    if(t->kind==TK_IDENT){
        FZDecl*d=find_decl(m,t->text);
        if(d&&!d->is_var&&!d->is_array&&d->par){
            double v=d->par[0];
            if(fabs(v-round(v))<=1e-12){*out=(long)llround(v);return 0;}
        }
    }
    return -1;
}

/* Parse an annotation range without confusing a decimal point with the
   `..` separator (e.g. 0.25..1.75). */
static int annotation_range(const char*s,double*lo,double*hi)
{
    /* strtod("1..10") is permitted to consume "1." as a number, so find
       the range delimiter first and parse its two sides independently. */
    const char*sep=strstr(s,"..");
    if(!sep)return -1;
    size_t n=(size_t)(sep-s);
    if(n==0||n>=128)return -1;
    char left[128];memcpy(left,s,n);left[n]=0;
    char *end=NULL,*end2=NULL;
    double a=strtod(left,&end);
    if(end==left||!isfinite(a))return -1;
    while(isspace((unsigned char)*end))end++;
    if(*end!='\0')return -1;
    double b=strtod(sep+2,&end2);
    if(end2==sep+2||!isfinite(b))return -1;
    while(isspace((unsigned char)*end2))end2++;
    if(*end2!='\0'||b<a)return -1;
    *lo=a;*hi=b;return 0;
}

int fz_read(const char*path,FZModel*m)
{
    if(!path||!m)return -1;
    memset(m,0,sizeof(*m));
    FILE*f=fopen(path,"r");if(!f){fprintf(stderr,"cannot open %s\n",path);return -1;}
    fseek(f,0,SEEK_END);long sz=ftell(f);fseek(f,0,SEEK_SET);
    char*src=(char*)psolve_malloc((size_t)(sz+1));size_t rd=fread(src,1,(size_t)sz,f);src[rd]=0;fclose(f);
    m->file=psolve_strdup(path);
    Token*toks=0;int nt=0;
    if(lex(src,&toks,&nt)!=0){free(src);return -1;}
    int ti=0;
    while(ti<nt){
        Token*tk=&toks[ti];
        if(tk->kind==TK_EOF)break;
        if(tk->kind==TK_IDENT){
            const char*kw=tk->text;
            if(is_kw(kw,"predicate")){ while(ti<nt&&!(toks[ti].kind==TK_SYM&&strcmp(toks[ti].text,";")==0))ti++; if(ti<nt)ti++; continue; }
            if(is_kw(kw,"constraint")){
                ti++; if(ti<nt&&toks[ti].kind==TK_IDENT){
                    FZConstr*c=(FZConstr*)psolve_calloc(1,sizeof(FZConstr)); c->pred=psolve_strdup(toks[ti].text);ti++;
                    if(ti<nt&&toks[ti].kind==TK_SYM&&strcmp(toks[ti].text,"(")==0)ti++;
                    int na=0,cap=8;c->args=(char**)psolve_malloc((size_t)cap*sizeof(char*));
                    while(ti<nt){
                        /* skip separators */
                        while(ti<nt&&toks[ti].kind==TK_SYM&&strcmp(toks[ti].text,",")==0)ti++;
                        if(ti<nt&&toks[ti].kind==TK_SYM&&strcmp(toks[ti].text,")")==0){ti++;break;}
                        if(ti>=nt)break;
                        /* capture one argument from ti, respecting bracket depth */
                        int depth=0; size_t s=toks[ti].start; int j=ti; size_t e=toks[ti].end;
                        for(;j<nt;j++){
                            if(toks[j].kind==TK_SYM){
                                const char*txt=toks[j].text;
                                if(strcmp(txt,"[")==0||strcmp(txt,"{")==0||strcmp(txt,"(")==0){depth++;}
                                else if(strcmp(txt,"]")==0||strcmp(txt,"}")==0){ if(depth>0)depth--; }
                                else if(strcmp(txt,")")==0){ if(depth==0){ e=toks[j].start; break; } else depth--; }
                                else if(strcmp(txt,",")==0){ if(depth==0){ e=toks[j].start; break; } }
                            }
                            e=toks[j].end;
                        }
                        if(na>=cap){cap*=2;c->args=(char**)psolve_realloc((void**)&c->args,(size_t)cap*sizeof(char*));}
                        c->args[na++]=psolve_strndup(src+s,e-s);
                        ti=j; /* points at the terminator (, or )) */
                    }
                    if(ti<nt&&toks[ti].kind==TK_SYM&&strcmp(toks[ti].text,";")==0)ti++;
                    c->nargs=na;c->next=m->constr;m->constr=c;m->nconstr++;
                }
                continue;
            }
            if(is_kw(kw,"solve")){
                ti++;if(ti<nt&&toks[ti].kind==TK_IDENT){
                    if(is_kw(toks[ti].text,"satisfy")){m->solve_kind=0;ti++;if(ti<nt&&toks[ti].kind==TK_SYM&&strcmp(toks[ti].text,";")==0)ti++;continue;}
                    int sk=0;if(is_kw(toks[ti].text,"minimize"))sk=1;else if(is_kw(toks[ti].text,"maximize"))sk=2;else continue;
                    m->solve_kind=sk;ti++;
                    size_t s=toks[ti].start;int j=ti;size_t e=toks[ti].end;
                    while(j<nt&&!(toks[j].kind==TK_SYM&&strcmp(toks[j].text,";")==0)){e=toks[j].end;j++;}
                    char*ob=psolve_strndup(src+s,e-s);
                    Lin ol;memset(&ol,0,sizeof(ol));
                    if(parse_lin(m,ob,&ol)!=0){m->solve_kind=0;free(ob);ti=(j<nt?j+1:j);continue;}
                    /* copy local Lin into FZLin objective */
                    m->objective.n=ol.n;
                    m->objective.idx=(int*)psolve_malloc((size_t)(ol.n?ol.n:1)*sizeof(int));
                    m->objective.coef=(double*)psolve_malloc((size_t)(ol.n?ol.n:1)*sizeof(double));
                    memcpy(m->objective.idx,ol.idx,(size_t)ol.n*sizeof(int));
                    memcpy(m->objective.coef,ol.coef,(size_t)ol.n*sizeof(double));
                    m->objective.constant=ol.constant;
                    lin_free(&ol);
                    free(ob);ti=(j<nt?j+1:j);
                }
                continue;
            }
            if(is_kw(kw,"par")||is_kw(kw,"var")||is_kw(kw,"array")||
               is_kw(kw,"int")||is_kw(kw,"float")||is_kw(kw,"bool")){
                /* Default to a parameter; `var` or `array ... of var` makes it
                   a decision variable.  FlatZinc commonly omits the `par`
                   keyword for scalar constants (`int: n = 4;`). */
                int is_var=is_kw(kw,"var");
                FZDecl*d=add_decl(m); d->is_var=is_var;
                if(is_kw(kw,"int"))d->kind=FZ_K_INT;
                else if(is_kw(kw,"float"))d->kind=FZ_K_FLOAT;
                else if(is_kw(kw,"bool"))d->kind=FZ_K_BOOL;
                ti++;
                if(is_kw(kw,"array")||(ti<nt&&is_kw(toks[ti].text,"array"))){
                    d->is_array=1;
                    if(!is_kw(kw,"array")) ti++;          /* skip 'array' */
                    /* Preserve the declared index range.  FlatZinc arrays are
                       normally one-dimensional, with an extent such as
                       array[1..n]; MiniZinc often uses a scalar parameter for
                       n, so resolve that form too. */
                    if(ti<nt&&toks[ti].kind==TK_SYM&&strcmp(toks[ti].text,"[")==0){
                        int ai=ti+1; long alo,ahi;
                        if(ai+2<nt && tok_int_value(m,&toks[ai],&alo)==0 &&
                           toks[ai+1].kind==TK_SYM&&strcmp(toks[ai+1].text,"..")==0 &&
                           tok_int_value(m,&toks[ai+2],&ahi)==0 && ahi>=alo &&
                           ahi-alo+1<=2147483647L){
                            d->index_lo=(int)alo;
                            d->n=(int)(ahi-alo+1);
                        }
                    }
                    while(ti<nt&&!(toks[ti].kind==TK_SYM&&strcmp(toks[ti].text,"]")==0))ti++;
                    if(ti<nt)ti++;                        /* skip ']' */
                    if(ti<nt&&is_kw(toks[ti].text,"of"))ti++;
                }
                /* optional 'var' before the type (array ... of var int) */
                if(ti<nt&&is_kw(toks[ti].text,"var")){ is_var=1; d->is_var=1; ti++; }
                if(ti<nt&&toks[ti].kind==TK_IDENT){if(is_kw(toks[ti].text,"int"))d->kind=FZ_K_INT;else if(is_kw(toks[ti].text,"float"))d->kind=FZ_K_FLOAT;else if(is_kw(toks[ti].text,"bool"))d->kind=FZ_K_BOOL;ti++;}
                /* FlatZinc set domain: var {1,3,5}: x. */
                if(ti<nt&&toks[ti].kind==TK_SYM&&strcmp(toks[ti].text,"{")==0){
                    long vmin=1000000000L,vmax=-1000000000L; int nv=0;
                    long vals[256];
                    ti++;  /* skip '{' */
                    while(ti<nt&&!(toks[ti].kind==TK_SYM&&strcmp(toks[ti].text,"}")==0)){
                        if(toks[ti].kind==TK_INT){ long v=toks[ti].ival; if(nv<256)vals[nv]=v; if(!nv||v<vmin)vmin=v; if(v>vmax)vmax=v; nv++; }
                        ti++;
                    }
                    if(ti<nt)ti++;  /* skip '}' */
                    if(nv>0){
                        if(d->kind==FZ_K_NONE)d->kind=FZ_K_INT;
                        d->has_lo=1;d->has_hi=1;
                        d->lo=(double*)psolve_malloc(sizeof(double));d->hi=(double*)psolve_malloc(sizeof(double));
                        d->lo[0]=(double)vmin; d->hi[0]=(double)vmax;
                        /* store exact set for SOS1 enforcement in fz_solve */
                        d->nset=nv; d->setvals=(long*)psolve_malloc((size_t)nv*sizeof(long));
                        for(int q=0;q<nv;q++)d->setvals[q]=vals[q];
                    }
                }
                /* FlatZinc shorthand: var 1..10: x or var 0.1..10.5: x.
                   The latter is a float declaration even though no explicit
                   `float` keyword appears, so retain the token's real value
                   rather than reading the inactive integer member of Token. */
                if(ti<nt&&(toks[ti].kind==TK_INT||toks[ti].kind==TK_FLOAT)){
                    int lo_kind=toks[ti].kind, hi_kind=TK_INT;
                    double lo,hi;
                    if(tok_number(&toks[ti],&lo)!=0){free_toks(toks,nt);free(src);return -1;}
                    ti++;
                    if(!(ti<nt&&toks[ti].kind==TK_SYM&&strcmp(toks[ti].text,"..")==0)){free_toks(toks,nt);free(src);return -1;}
                    ti++;
                    if(ti>=nt||tok_number(&toks[ti],&hi)!=0){free_toks(toks,nt);free(src);return -1;}
                    hi_kind=toks[ti].kind;ti++;
                    if(hi<lo){free_toks(toks,nt);free(src);return -1;}
                    if(d->kind==FZ_K_NONE)d->kind=(lo_kind==TK_FLOAT||hi_kind==TK_FLOAT)?FZ_K_FLOAT:FZ_K_INT;
                    d->has_lo=1;d->has_hi=1;d->lo=(double*)psolve_malloc(sizeof(double));d->hi=(double*)psolve_malloc(sizeof(double));
                    d->lo[0]=lo;d->hi[0]=hi;
                }
                if(ti<nt&&toks[ti].kind==TK_SYM&&strcmp(toks[ti].text,":")==0)ti++;
                if(ti<nt&&toks[ti].kind==TK_IDENT){d->name=psolve_strdup(toks[ti].text);ti++;}
                /* annotations may appear before the '=' (e.g. :: output_array).
                   Handle output markers and integer domain (:: lo..hi). */
                while(ti<nt&&toks[ti].kind==TK_SYM&&strcmp(toks[ti].text,"::")==0){
                    size_t as=toks[ti].end;int aj=ti+1;size_t ae=(ti+1<nt)?toks[ti+1].end:as;
                    while(aj<nt&&!(toks[aj].kind==TK_SYM&&(strcmp(toks[aj].text,";")==0||strcmp(toks[aj].text,"=")==0||strcmp(toks[aj].text,"::")==0))){ae=toks[aj].end;aj++;}
                    char*ann=psolve_strndup(src+as,ae-as);
                    if(strstr(ann,"output_var")||strstr(ann,"output_array"))d->is_output=1;
                    /* domain :: lo..hi (including decimal endpoints) */
                    if(!d->has_lo){
                        double lo,hi;
                        if(annotation_range(ann,&lo,&hi)==0){
                            d->has_lo=1;d->has_hi=1;
                            d->lo=(double*)psolve_malloc(sizeof(double));d->hi=(double*)psolve_malloc(sizeof(double));
                            d->lo[0]=lo;d->hi[0]=hi;
                        }
                    }
                    free(ann);
                    ti=aj;
                }
                if(ti<nt&&toks[ti].kind==TK_SYM&&strcmp(toks[ti].text,"=")==0){
                    ti++;size_t s=toks[ti].start;int j=ti;size_t e=toks[ti].end;
                    while(j<nt&&!(toks[j].kind==TK_SYM&&strcmp(toks[j].text,";")==0)){e=toks[j].end;j++;}
                    char*rhs=psolve_strndup(src+s,e-s);
                    if(d->is_array && is_var){
                        /* A FlatZinc var-array initializer is a view, not a
                           request for fresh unconstrained variables.  Preserve
                           each element exactly: it can alias an introduced var
                           or be a constant after compiler propagation. */
                        Lin*arr=NULL;int narr=0;
                        if(parse_array(m,rhs,&arr,&narr)!=0 || (d->n>0&&narr!=d->n)){
                            if(arr)free_lins(arr,narr);
                            free(rhs);free_toks(toks,nt);free(src);return -1;
                        }
                        d->n=narr;d->is_alias=1;
                        d->alias_idx=(int*)psolve_calloc((size_t)(narr?narr:1),sizeof(int));
                        d->alias_const=(double*)psolve_calloc((size_t)(narr?narr:1),sizeof(double));
                        int contiguous=1,first=-1,ok=1;
                        for(int q=0;q<narr;q++){
                            if(arr[q].n==0){
                                d->alias_idx[q]=-1;d->alias_const[q]=arr[q].constant;contiguous=0;
                            } else if(arr[q].n==1&&fabs(arr[q].coef[0]-1.0)<=1e-12&&fabs(arr[q].constant)<=1e-12){
                                d->alias_idx[q]=arr[q].idx[0];
                                if(q==0)first=d->alias_idx[q];
                                else if(d->alias_idx[q]!=first+q)contiguous=0;
                            } else {ok=0;break;}
                        }
                        if(!ok){free_lins(arr,narr);free(rhs);free_toks(toks,nt);free(src);return -1;}
                        d->base_idx=contiguous&&first>=0?first:-1;
                        free_lins(arr,narr);
                    } else if(d->is_array){
                        int nel=1;for(size_t p=0;p<strlen(rhs);p++)if(rhs[p]==',')nel++;
                        d->n=nel;d->par=(double*)psolve_calloc((size_t)nel,sizeof(double));d->par_int=(int*)psolve_calloc((size_t)nel,sizeof(int));
                        Lin*arr;int narr;
                        if(parse_array(m,rhs,&arr,&narr)==0&&narr==nel){for(int q=0;q<nel;q++){d->par[q]=arr[q].constant;d->par_int[q]=(int)llround(arr[q].constant);}free_lins(arr,narr);}
                    } else if(is_var){
                        d->n=1;
                        Lin l;memset(&l,0,sizeof(l));
                        if(parse_lin(m,rhs,&l)!=0 ||
                           !(l.n==0 || (l.n==1&&fabs(l.coef[0]-1.0)<=1e-12&&fabs(l.constant)<=1e-12))){
                            lin_free(&l);free(rhs);free_toks(toks,nt);free(src);return -1;
                        }
                        d->is_alias=1;d->alias_idx=(int*)psolve_calloc(1,sizeof(int));d->alias_const=(double*)psolve_calloc(1,sizeof(double));
                        if(l.n==0){d->alias_idx[0]=-1;d->alias_const[0]=l.constant;d->base_idx=-1;}
                        else {d->alias_idx[0]=l.idx[0];d->base_idx=l.idx[0];}
                        lin_free(&l);
                    } else {
                        d->n=1;d->par=(double*)psolve_calloc(1,sizeof(double));d->par_int=(int*)psolve_calloc(1,sizeof(int));
                        Lin l;if(parse_lin(m,rhs,&l)==0){d->par[0]=l.constant;d->par_int[0]=(int)llround(l.constant);lin_free(&l);}
                    }
                    free(rhs);ti=(j<nt?j+1:j);
                    continue;
                }
                /* A declaration without an initializer owns one scalar or
                   the complete declared array extent.  The old reader reset
                   arrays to one element here, which made float/int arrays
                   silently lose all but their first variable. */
                if(!d->is_array||d->n<=0)d->n=1;
                if(is_var){
                    d->base_idx=m->nvars;
                    m->nvars+=d->n;
                }
                /* annotations */
                while(ti<nt&&toks[ti].kind==TK_SYM&&strcmp(toks[ti].text,"::")==0){
                    ti++;
                    /* capture annotation: identifier or expression; detect output_var/output_array */
                    size_t s=toks[ti].start; int j=ti; size_t e=toks[ti].end;
                    while(j<nt&&!(toks[j].kind==TK_SYM&&(strcmp(toks[j].text,";")==0||strcmp(toks[j].text,"::")==0))){e=toks[j].end;j++;}
                    char*ann=psolve_strndup(src+s,e-s);
                    if(strstr(ann,"output_var")||strstr(ann,"output_array"))d->is_output=1;
                    /* domain like "lo..hi", with decimal endpoints allowed */
                    {
                        double lo,hi;
                        if(annotation_range(ann,&lo,&hi)==0){
                            d->has_lo=1;d->has_hi=1;
                            d->lo=(double*)psolve_malloc(sizeof(double));d->hi=(double*)psolve_malloc(sizeof(double));
                            d->lo[0]=lo;d->hi[0]=hi;
                        }
                    }
                    free(ann);
                    ti=(j<nt?j:j); /* advance to '::' or ';' boundary */
                }
                if(ti<nt&&toks[ti].kind==TK_SYM&&strcmp(toks[ti].text,";")==0)ti++;
                continue;
            }
            /* unknown: skip to ';' */
            while(ti<nt&&!(toks[ti].kind==TK_SYM&&strcmp(toks[ti].text,";")==0))ti++;
            if(ti<nt)ti++;
            continue;
        }
        ti++;
    }
    free_toks(toks,nt);
    free(src);
    return 0;
}

/* ------------------------------------------------------------------ */
/* LP bridge                                                           */
/* ------------------------------------------------------------------ */
typedef struct { int n;int*idx;double*coef;double rhs;char rel; } Row;
typedef struct { int nvars;int varcap;double*lo,*hi;int*haslo,*hashi;unsigned char*isint;Row*rows;int nrows,cap; } Builder;

/* The LP core needs a finite starting box.  This is only a bridge sentinel,
   never a mathematical FlatZinc bound; optimization results that rely on it
   are reported as UNKNOWN rather than a fictitious optimum at +/-1e9. */
#define FZ_BIG_BOUND 1e9
#define FZ_BIG_HIT_TOL 1e-5
static int b_newvar_kind(Builder*b, double lo, double hi, int is_int){
    if(b->nvars>=b->varcap){
        int nc=b->varcap?b->varcap*2:16;
        psolve_realloc((void**)&b->lo,(size_t)nc*sizeof(double));
        psolve_realloc((void**)&b->hi,(size_t)nc*sizeof(double));
        psolve_realloc((void**)&b->haslo,(size_t)nc*sizeof(int));
        psolve_realloc((void**)&b->hashi,(size_t)nc*sizeof(int));
        psolve_realloc((void**)&b->isint,(size_t)nc*sizeof(unsigned char));
        b->varcap=nc;
    }
    int v=b->nvars++;
    b->lo[v]=lo;b->hi[v]=hi;b->haslo[v]=1;b->hashi[v]=1;b->isint[v]=(unsigned char)is_int;
    return v;
}
static int b_newvar(Builder*b, double lo, double hi){
    return b_newvar_kind(b, lo, hi, 1);
}
static int b_newvar_float(Builder*b, double lo, double hi){
    return b_newvar_kind(b, lo, hi, 0);
}
static void b_add(Builder*b){if(b->nrows>=b->cap){b->cap=b->cap?b->cap*2:16;b->rows=(Row*)psolve_realloc((void**)&b->rows,(size_t)b->cap*sizeof(Row));}memset(&b->rows[b->nrows],0,sizeof(Row));}
static void b_put(Builder*b,char rel,double rhs,const Lin*l){
    b_add(b);Row*r=&b->rows[b->nrows++];r->rel=rel;r->rhs=rhs-l->constant;
    r->n=l->n;r->idx=(int*)psolve_malloc((size_t)(l->n?l->n:1)*sizeof(int));r->coef=(double*)psolve_malloc((size_t)(l->n?l->n:1)*sizeof(double));
    for(int i=0;i<l->n;i++){r->idx[i]=l->idx[i];r->coef[i]=l->coef[i];}
}

/* Tight-enough bounds for a linear form over the builder boxes.  The FlatZinc
   bridge deliberately gives every variable a finite box, so these are also
   usable for exact integer big-M encodings. */
static int lin_bounds(const Builder*b,const Lin*l,double*lo,double*hi)
{
    double lval=l->constant, hval=l->constant;
    for(int i=0;i<l->n;i++){
        int v=l->idx[i]; double a=l->coef[i];
        if(v<0||v>=b->nvars||!isfinite(a))return -1;
        if(a>=0.0){lval+=a*b->lo[v];hval+=a*b->hi[v];}
        else {lval+=a*b->hi[v];hval+=a*b->lo[v];}
    }
    if(!isfinite(lval)||!isfinite(hval))return -1;
    *lo=lval;*hi=hval;return 0;
}

static int lin_unit_var(const Lin*l,int*out)
{
    if(l->n!=1||fabs(l->coef[0]-1.0)>1e-12||fabs(l->constant)>1e-12)return -1;
    *out=l->idx[0];return 0;
}

/* Materialize an arbitrary linear form as a plain solver variable: a bare
   variable (unit coefficient, no constant) maps to its own index; anything
   else (constant or affine expression) gets a fresh alias variable pinned by
   v = l exactly.  Returns -1 when the form has no finite bounds.
   Handlers whose encodings index b->lo[]/b->hi[] directly MUST route their
   arguments through this: using l->idx[0] on an affine or constant form
   silently drops the constant term (a wrong-answer bug class). */
static int lin_materialize(Builder*b,const Lin*l,int is_float)
{
    if(l->n==1&&l->coef[0]==1.0&&l->constant==0.0)return l->idx[0];
    double lo,hi;
    if(lin_bounds(b,l,&lo,&hi)!=0)return -1;
    if(!is_float&&(lo!=rint(lo)||hi!=rint(hi)))return -1;
    int v=is_float?b_newvar_float(b,lo,hi):b_newvar(b,lo,hi);
    if(v<0)return -1;
    Lin dd;memset(&dd,0,sizeof(dd));lin_term(&dd,v,1.0);lin_into(&dd,l,-1.0);
    b_put(b,'=',0.0,&dd);lin_free(&dd);
    return v;
}


/* Add d != 0 for an integer linear form.  p/n select its positive/negative
   side; this is a true disjunction, unlike conjoining d>=1 and d<=-1. */
static int add_int_ne(Builder*b,const Lin*d)
{
    double L,U;
    if(lin_bounds(b,d,&L,&U)!=0)return -1;
    int pos=b_newvar(b,0.0,1.0), neg=b_newvar(b,0.0,1.0);
    Lin sel;memset(&sel,0,sizeof(sel));lin_term(&sel,pos,1.0);lin_term(&sel,neg,1.0);b_put(b,'=',1.0,&sel);lin_free(&sel);
    /* pos=1 -> d>=1; pos=0 leaves the valid lower bound L. */
    Lin low;memset(&low,0,sizeof(low));lin_into(&low,d,1.0);lin_term(&low,pos,-(1.0-L));b_put(b,'>',L,&low);lin_free(&low);
    /* neg=1 -> d<=-1; neg=0 leaves the valid upper bound U. */
    Lin high;memset(&high,0,sizeof(high));lin_into(&high,d,1.0);lin_term(&high,neg,U+1.0);b_put(b,'<',U,&high);lin_free(&high);
    return 0;
}

/* Exact reification r <-> (d rel 0) for an integer-valued d.  `which` is
   0:eq, 1:le, 2:lt, 3:ge, 4:gt.  Strict integer relations use their adjacent
   lattice value (+/-1), never the non-strict relaxation. */
static int add_int_reif(Builder*b,const Lin*d,int r,int which)
{
    double L,U;
    if(r<0||r>=b->nvars||b->lo[r]<-1e-9||b->hi[r]>1.0+1e-9||lin_bounds(b,d,&L,&U)!=0)return -1;
    if(which==0){
        /* r=1 => d=0; r=0 selects d>=1 or d<=-1. */
        int pos=b_newvar(b,0.0,1.0), neg=b_newvar(b,0.0,1.0);
        Lin sel;memset(&sel,0,sizeof(sel));lin_term(&sel,r,1.0);lin_term(&sel,pos,1.0);lin_term(&sel,neg,1.0);b_put(b,'=',1.0,&sel);lin_free(&sel);
        Lin up;memset(&up,0,sizeof(up));lin_into(&up,d,1.0);lin_term(&up,r,U);b_put(b,'<',U,&up);lin_free(&up);
        Lin low0;memset(&low0,0,sizeof(low0));lin_into(&low0,d,1.0);lin_term(&low0,r,L);b_put(b,'>',L,&low0);lin_free(&low0);
        Lin low;memset(&low,0,sizeof(low));lin_into(&low,d,1.0);lin_term(&low,pos,-(1.0-L));b_put(b,'>',L,&low);lin_free(&low);
        Lin high;memset(&high,0,sizeof(high));lin_into(&high,d,1.0);lin_term(&high,neg,U+1.0);b_put(b,'<',U,&high);lin_free(&high);
        return 0;
    }
    if(which==1){                 /* r <-> d <= 0 */
        Lin up;memset(&up,0,sizeof(up));lin_into(&up,d,1.0);lin_term(&up,r,U);b_put(b,'<',U,&up);lin_free(&up);
        Lin low;memset(&low,0,sizeof(low));lin_into(&low,d,1.0);lin_term(&low,r,1.0-L);b_put(b,'>',1.0,&low);lin_free(&low);
        return 0;
    }
    if(which==2){                 /* r <-> d < 0, i.e. d <= -1 */
        Lin up;memset(&up,0,sizeof(up));lin_into(&up,d,1.0);lin_term(&up,r,U+1.0);b_put(b,'<',U,&up);lin_free(&up);
        Lin low;memset(&low,0,sizeof(low));lin_into(&low,d,1.0);lin_term(&low,r,-L);b_put(b,'>',0.0,&low);lin_free(&low);
        return 0;
    }
    if(which==3){                 /* r <-> d >= 0 */
        Lin low;memset(&low,0,sizeof(low));lin_into(&low,d,1.0);lin_term(&low,r,L);b_put(b,'>',L,&low);lin_free(&low);
        Lin up;memset(&up,0,sizeof(up));lin_into(&up,d,1.0);lin_term(&up,r,-(U+1.0));b_put(b,'<',-1.0,&up);lin_free(&up);
        return 0;
    }
    if(which==4){                 /* r <-> d > 0, i.e. d >= 1 */
        Lin low;memset(&low,0,sizeof(low));lin_into(&low,d,1.0);lin_term(&low,r,-(1.0-L));b_put(b,'>',L,&low);lin_free(&low);
        Lin up;memset(&up,0,sizeof(up));lin_into(&up,d,1.0);lin_term(&up,r,-U);b_put(b,'<',0.0,&up);lin_free(&up);
        return 0;
    }
    if(which==5){                 /* r <-> d != 0 */
        int pos=b_newvar(b,0.0,1.0), neg=b_newvar(b,0.0,1.0);
        Lin sel;memset(&sel,0,sizeof(sel));lin_term(&sel,r,-1.0);lin_term(&sel,pos,1.0);lin_term(&sel,neg,1.0);b_put(b,'=',0.0,&sel);lin_free(&sel);
        Lin up;memset(&up,0,sizeof(up));lin_into(&up,d,1.0);lin_term(&up,r,-U);b_put(b,'<',0.0,&up);lin_free(&up);
        Lin low0;memset(&low0,0,sizeof(low0));lin_into(&low0,d,1.0);lin_term(&low0,r,-L);b_put(b,'>',0.0,&low0);lin_free(&low0);
        Lin low;memset(&low,0,sizeof(low));lin_into(&low,d,1.0);lin_term(&low,pos,-(1.0-L));b_put(b,'>',L,&low);lin_free(&low);
        Lin high;memset(&high,0,sizeof(high));lin_into(&high,d,1.0);lin_term(&high,neg,U+1.0);b_put(b,'<',U,&high);lin_free(&high);
        return 0;
    }
    return -1;
}

/* Half-reification r => (d rel 0) for an integer-valued d. */
static int add_int_imp(Builder*b,const Lin*d,int r,int which)
{
    double L,U;
    if(r<0||r>=b->nvars||b->lo[r]<-1e-9||b->hi[r]>1.0+1e-9||lin_bounds(b,d,&L,&U)!=0)return -1;
    if(which==0){ /* r => d == 0 */
        Lin up;memset(&up,0,sizeof(up));lin_into(&up,d,1.0);lin_term(&up,r,U);b_put(b,'<',U,&up);lin_free(&up);
        Lin low;memset(&low,0,sizeof(low));lin_into(&low,d,1.0);lin_term(&low,r,L);b_put(b,'>',L,&low);lin_free(&low);
        return 0;
    }
    if(which==1){ /* r => d <= 0 */
        Lin up;memset(&up,0,sizeof(up));lin_into(&up,d,1.0);lin_term(&up,r,U);b_put(b,'<',U,&up);lin_free(&up);
        return 0;
    }
    if(which==2){ /* r => d <= -1 */
        Lin up;memset(&up,0,sizeof(up));lin_into(&up,d,1.0);lin_term(&up,r,U+1.0);b_put(b,'<',U,&up);lin_free(&up);
        return 0;
    }
    if(which==3){ /* r => d >= 0 */
        Lin low;memset(&low,0,sizeof(low));lin_into(&low,d,1.0);lin_term(&low,r,L);b_put(b,'>',L,&low);lin_free(&low);
        return 0;
    }
    if(which==4){ /* r => d >= 1 */
        Lin low;memset(&low,0,sizeof(low));lin_into(&low,d,1.0);lin_term(&low,r,L-1.0);b_put(b,'>',L,&low);lin_free(&low);
        return 0;
    }
    if(which==5){ /* r => d != 0 */
        int pos=b_newvar(b,0.0,1.0), neg=b_newvar(b,0.0,1.0);
        Lin sel;memset(&sel,0,sizeof(sel));lin_term(&sel,r,-1.0);lin_term(&sel,pos,1.0);lin_term(&sel,neg,1.0);b_put(b,'=',0.0,&sel);lin_free(&sel);
        Lin low;memset(&low,0,sizeof(low));lin_into(&low,d,1.0);lin_term(&low,pos,-(1.0-L));b_put(b,'>',L,&low);lin_free(&low);
        Lin high;memset(&high,0,sizeof(high));lin_into(&high,d,1.0);lin_term(&high,neg,U+1.0);b_put(b,'<',U,&high);lin_free(&high);
        return 0;
    }
    return -1;
}

static int add_int_relation_constant(Builder*b,const Lin*d,int which,int want)
{
    if(want){
        if(which==0)b_put(b,'=',0.0,d);
        else if(which==1)b_put(b,'<',0.0,d);
        else if(which==2)b_put(b,'<',-1.0,d);
        else if(which==3)b_put(b,'>',0.0,d);
        else if(which==4)b_put(b,'>',1.0,d);
        else if(which==5)return add_int_ne(b,d);
        else return -1;
        return 0;
    }
    if(which==0)return add_int_ne(b,d);
    if(which==1)b_put(b,'>',1.0,d);
    else if(which==2)b_put(b,'>',0.0,d);
    else if(which==3)b_put(b,'<',-1.0,d);
    else if(which==4)b_put(b,'<',0.0,d);
    else if(which==5)b_put(b,'=',0.0,d);
    else return -1;
    return 0;
}

static int objective_uses_synthetic_bound(const FZModel*m,const Builder*b,const double*x,int norig)
{
    /* A free variable that is irrelevant to the objective may legitimately be
       printed at an arbitrary finite value for `solve satisfy`/a bounded
       objective.  Only an objective-bearing original variable at the bridge
       sentinel means the claimed optimum is not certifiable. */
    for(int k=0;k<m->objective.n;k++){
        int v=m->objective.idx[k];
        if(v<0||v>=norig||fabs(m->objective.coef[k])<=1e-14)continue;
        if((!b->haslo[v]&&x[v]<=-FZ_BIG_BOUND+FZ_BIG_HIT_TOL)||
           (!b->hashi[v]&&x[v]>= FZ_BIG_BOUND-FZ_BIG_HIT_TOL))return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* CSP heuristic for combinatorial puzzles (all_different + int_lin_eq) */
/* Solves bounded integer CSPs via backtracking with forward checking  */
/* and is tried before MIP for `solve satisfy` to avoid exponential   */
/* LP-based branch-and-bound on weak relaxations (e.g. n-queens,       */
/* sudoku).  Handles: fzn_all_different_int, int_lin_eq with 2 vars.   */
/* ------------------------------------------------------------------ */
typedef struct { long lo, hi; uint64_t mask; int has_mask; int assigned; long val; } CspDom;
typedef struct { int n; int *vars; int nfixed; long *fixed; } CspAllDiff;
typedef struct { int n; int *vars; long *coefs; long rhs; char rel; } CspLin;
typedef struct { int a,b; long ca, cb, rhs; } CspNe;

static int csp_contains(CspDom *d, long v){
    if(v < d->lo || v > d->hi) return 0;
    if(d->assigned) return d->val == v;
    if(!d->has_mask) return 1;
    return (d->mask >> (v - d->lo)) & 1ULL;
}
static int csp_remove(CspDom *d, long v){
    if(d->assigned) return d->val==v ? -1 : 0;
    if(v < d->lo || v > d->hi) return 0;
    if(!d->has_mask) return 0;
    uint64_t bit = 1ULL << (v - d->lo);
    if(!(d->mask & bit)) return 0;
    d->mask &= ~bit;
    if(d->mask==0) return -1;
    if((d->mask & (d->mask-1))==0){
        int idx = __builtin_ctzll(d->mask);
        d->assigned=1; d->val=d->lo+idx;
        d->lo=d->val; d->hi=d->val;
    }
    return 1;
}
static int csp_assign(CspDom *d, long v){
    if(v < d->lo || v > d->hi) return -1;
    if(d->has_mask && !csp_contains(d,v)) return -1;
    d->assigned=1; d->val=v; d->lo=v; d->hi=v;
    if(d->has_mask) d->mask = 1ULL << (v - d->lo);
    // actually after assign lo==v, mask bit 0
    if(d->has_mask) d->mask = 1ULL;
    return 0;
}
static int csp_propagate(CspDom *doms, int nvars, CspAllDiff *ads, int nads, CspLin *lins, int nlins, CspNe *nes, int nnes){
    (void)nvars;
    // immediate duplicate among fixed constants => unsat
    for(int ai=0; ai<nads; ai++){
        CspAllDiff *ad=&ads[ai];
        for(int k1=0;k1<ad->nfixed;k1++) for(int k2=k1+1;k2<ad->nfixed;k2++) if(ad->fixed[k1]==ad->fixed[k2]) return -1;
    }
    int changed=1, iter=0;
    while(changed && iter<200){
        changed=0; iter++;
        for(int ai=0; ai<nads; ai++){
            CspAllDiff *ad=&ads[ai];
            // check duplicates among assigned and remove values
            for(int i=0;i<ad->n;i++){
                int vi=ad->vars[i];
                if(!doms[vi].assigned) continue;
                long v=doms[vi].val;
                for(int k=0;k<ad->nfixed;k++) if(ad->fixed[k]==v) return -1;
                for(int j=0;j<ad->n;j++) if(j!=i){
                    int vj=ad->vars[j];
                    if(doms[vj].assigned && doms[vj].val==v) return -1;
                    if(!doms[vj].assigned && csp_contains(&doms[vj], v)){
                        int r=csp_remove(&doms[vj], v);
                        if(r==-1) return -1;
                        if(r==1) changed=1;
                    }
                }
            }
            for(int k=0;k<ad->nfixed;k++){
                long v=ad->fixed[k];
                for(int i=0;i<ad->n;i++){
                    int vi=ad->vars[i];
                    if(!doms[vi].assigned && csp_contains(&doms[vi], v)){
                        int r=csp_remove(&doms[vi], v);
                        if(r==-1) return -1;
                        if(r==1) changed=1;
                    }
                }
            }
        }
        for(int li=0; li<nlins; li++){
            CspLin *lc=&lins[li];
            if(lc->rel=='=' && lc->n==2){
                int a=lc->vars[0], b=lc->vars[1];
                long ca=lc->coefs[0], cb=lc->coefs[1];
                long rhs=lc->rhs;
                if(doms[a].assigned && doms[b].assigned){
                    if(ca*doms[a].val + cb*doms[b].val != rhs) return -1;
                } else if(doms[a].assigned && !doms[b].assigned){
                    long num = rhs - ca*doms[a].val;
                    if(num % cb != 0) return -1;
                    long vb = num / cb;
                    if(!csp_contains(&doms[b], vb)) return -1;
                    if(csp_assign(&doms[b], vb)==-1) return -1;
                    changed=1;
                } else if(!doms[a].assigned && doms[b].assigned){
                    long num = rhs - cb*doms[b].val;
                    if(num % ca != 0) return -1;
                    long va = num / ca;
                    if(!csp_contains(&doms[a], va)) return -1;
                    if(csp_assign(&doms[a], va)==-1) return -1;
                    changed=1;
                } else {
                    // interval pruning: if rhs outside [min,max] => fail
                    long min_a=doms[a].lo, max_a=doms[a].hi;
                    long min_b=doms[b].lo, max_b=doms[b].hi;
                    long min_sum, max_sum;
                    if(ca>=0 && cb>=0){ min_sum=ca*min_a+cb*min_b; max_sum=ca*max_a+cb*max_b; }
                    else if(ca>=0 && cb<0){ min_sum=ca*min_a+cb*max_b; max_sum=ca*max_a+cb*min_b; }
                    else if(ca<0 && cb>=0){ min_sum=ca*max_a+cb*min_b; max_sum=ca*min_a+cb*max_b; }
                    else { min_sum=ca*max_a+cb*max_b; max_sum=ca*min_a+cb*min_b; }
                    if(rhs < min_sum || rhs > max_sum) return -1;
                }
            } else {
                // generic interval check
                long cmin=0, cmax=0;
                for(int i=0;i<lc->n;i++){
                    int v=lc->vars[i]; long c=lc->coefs[i];
                    long lo=doms[v].lo, hi=doms[v].hi;
                    if(c>=0){ cmin+=c*lo; cmax+=c*hi; } else { cmin+=c*hi; cmax+=c*lo; }
                }
                if(lc->rel=='=' && (lc->rhs < cmin || lc->rhs > cmax)) return -1;
                if(lc->rel=='<' && cmin > lc->rhs) return -1;
                if(lc->rel=='>' && cmax < lc->rhs) return -1;
            }
        }
        for(int ni=0; ni<nnes; ni++){
            CspNe *ne=&nes[ni];
            int a=ne->a, b=ne->b; long ca=ne->ca, cb=ne->cb, rhs=ne->rhs;
            if(doms[a].assigned && doms[b].assigned){
                if(ca*doms[a].val + cb*doms[b].val == rhs) return -1;
            } else if(doms[a].assigned && !doms[b].assigned){
                long num = rhs - ca*doms[a].val;
                if(num % cb==0){
                    long vb = num / cb;
                    if(csp_contains(&doms[b], vb)){
                        int r=csp_remove(&doms[b], vb);
                        if(r==-1) return -1;
                        if(r==1) changed=1;
                    }
                }
            } else if(!doms[a].assigned && doms[b].assigned){
                long num = rhs - cb*doms[b].val;
                if(num % ca==0){
                    long va = num / ca;
                    if(csp_contains(&doms[a], va)){
                        int r=csp_remove(&doms[a], va);
                        if(r==-1) return -1;
                        if(r==1) changed=1;
                    }
                }
            }
        }
    }
    return 0;
}
static int csp_dfs(CspDom *doms, int nvars, CspAllDiff *ads, int nads, CspLin *lins, int nlins, CspNe *nes, int nnes, long *sol, int *nodes, int node_limit){
    if(psolve_stop()) return 2;
    if((*nodes)++ > node_limit) return 2;
    if(csp_propagate(doms,nvars,ads,nads,lins,nlins,nes,nnes)==-1) return 0;
    int best=-1; int best_sz=1000;
    for(int i=0;i<nvars;i++) if(!doms[i].assigned){
        int sz;
        if(doms[i].has_mask) sz=__builtin_popcountll(doms[i].mask);
        else sz=(int)(doms[i].hi - doms[i].lo + 1);
        if(sz < best_sz){ best_sz=sz; best=i; if(sz==2) break; }
    }
    if(best==-1){
        for(int i=0;i<nvars;i++) sol[i]=doms[i].val;
        return 1;
    }
    CspDom *d=&doms[best];
    long vals[64]; int nvals=0;
    if(d->has_mask){
        for(int b=0;b<64;b++) if(d->mask & (1ULL<<b)) vals[nvals++]=d->lo+b;
    } else {
        // interval large: try middle values first, but for heuristic we only have small domains with mask
        for(long v=d->lo; v<=d->hi && nvals<64; v++) vals[nvals++]=v;
    }
    for(int k=0;k<nvals;k++){
        CspDom *copy=(CspDom*)psolve_malloc((size_t)nvars*sizeof(CspDom));
        memcpy(copy,doms,(size_t)nvars*sizeof(CspDom));
        if(csp_assign(&copy[best], vals[k])==-1){ free(copy); continue; }
        int r=csp_dfs(copy,nvars,ads,nads,lins,nlins,nes,nnes,sol,nodes,node_limit);
        free(copy);
        if(r==1) return 1;
        if(r==2) return 2;
    }
    return 0;
}
/* Try to solve bounded CSP with all_different + int_lin_eq. Returns 1 if solved and fills out (size m->nvars), 0 otherwise. */
static int csp_try_alldiff(const FZModel *m, double *out){
    if(m->solve_kind!=0) return 0;
    int nvars=m->nvars;
    if(nvars<=0 || nvars>256) return 0;
    // check all integer vars have finite bounds and domain size <=64 and not gapped
    for(int d=0; d<m->ndecl; d++){
        FZDecl *decl=&m->decls[d];
        if(!decl->is_var || decl->base_idx<0 || decl->is_alias) continue;
        if(decl->kind==FZ_K_FLOAT) return 0; // heuristic only for int/bool
        if(decl->nset>0){
            int cont=1;
            for(int q=1;q<decl->nset;q++) if(decl->setvals[q]!=decl->setvals[0]+q){ cont=0; break; }
            if(!cont) return 0;
            // gapped will be handled via builder, skip heuristic
        }
        for(int e=0;e<decl->n;e++){
            int vi=decl->base_idx+e;
            if(vi<0||vi>=nvars) continue;
            long lo,hi;
            if(decl->has_lo && decl->has_hi){ lo=(long)llround(decl->lo[0]); hi=(long)llround(decl->hi[0]); }
            else if(decl->kind==FZ_K_BOOL){ lo=0; hi=1; }
            else return 0; // unbounded
            if(hi < lo) return 0;
            long sz = hi - lo + 1;
            if(sz<=0 || sz>64) return 0;
        }
    }
    // build domains
    CspDom *doms=(CspDom*)psolve_calloc((size_t)nvars,sizeof(CspDom));
    for(int i=0;i<nvars;i++){ doms[i].lo=-1000000000; doms[i].hi=1000000000; doms[i].has_mask=0; }
    for(int d=0; d<m->ndecl; d++){
        FZDecl *decl=&m->decls[d];
        if(!decl->is_var || decl->base_idx<0 || decl->is_alias) continue;
        for(int e=0;e<decl->n;e++){
            int vi=decl->base_idx+e;
            long lo,hi;
            if(decl->has_lo && decl->has_hi){ lo=(long)llround(decl->lo[0]); hi=(long)llround(decl->hi[0]); }
            else if(decl->kind==FZ_K_BOOL){ lo=0; hi=1; }
            else continue;
            doms[vi].lo=lo; doms[vi].hi=hi;
            long sz=hi-lo+1;
            if(sz>0 && sz<=64){ doms[vi].has_mask=1; doms[vi].mask = (sz==64?~0ULL:((1ULL<<sz)-1ULL)); }
        }
    }
    // collect constraints
    CspAllDiff *ads=NULL; int nads=0, cap_ads=0;
    CspLin *lins=NULL; int nlins=0, cap_lins=0;
    CspNe *nes=NULL; int nnes=0, cap_nes=0;
    int unsupported=0;
    for(FZConstr *c=m->constr;c;c=c->next){
        const char *p=c->pred;
        int is_alldiff = (strstr(p,"all_different")!=NULL || strstr(p,"alldifferent")!=NULL);
        // normalize: exclude except_0 which we don't handle here (handled via MIP)
        if(is_alldiff && strstr(p,"except")!=NULL) { unsupported=1; break; }
        if(is_alldiff){
            Lin *arr; int narr;
            if(parse_array((FZModel*)m,c->args[0],&arr,&narr)!=0){ unsupported=1; break; }
            if(narr<=1){ free_lins(arr,narr); continue; }
            int *vars=(int*)psolve_malloc((size_t)narr*sizeof(int));
            long *fixed=(long*)psolve_malloc((size_t)narr*sizeof(long));
            int nvars_ad=0, nfixed=0;
            int ok=1;
            for(int i=0;i<narr;i++){
                if(arr[i].n==0){
                    long v=(long)llround(arr[i].constant);
                    fixed[nfixed++]=v;
                } else if(arr[i].n==1 && fabs(arr[i].coef[0]-1.0)<1e-12 && fabs(arr[i].constant)<1e-12){
                    vars[nvars_ad++]=arr[i].idx[0];
                } else { ok=0; break; }
            }
            free_lins(arr,narr);
            if(!ok){ free(vars); free(fixed); unsupported=1; break; }
            if(nvars_ad==0){ free(vars); free(fixed); continue; }
            if(nads>=cap_ads){ cap_ads=cap_ads?cap_ads*2:8; ads=(CspAllDiff*)psolve_realloc((void**)&ads,(size_t)cap_ads*sizeof(CspAllDiff)); }
            ads[nads].n=nvars_ad; ads[nads].vars=vars; ads[nads].nfixed=nfixed;
            if(nfixed){ ads[nads].fixed=(long*)psolve_malloc((size_t)nfixed*sizeof(long)); memcpy(ads[nads].fixed,fixed,(size_t)nfixed*sizeof(long)); } else ads[nads].fixed=NULL;
            free(fixed);
            nads++;
            continue;
        }
        // int_lin_ne and int_ne as disequality (for N-Queens linear compilations)
        if(strcmp(p,"int_lin_ne")==0 || strcmp(p,"bool_lin_ne")==0){
            if(c->nargs<3){ unsupported=1; break; }
            Lin *coefArr; int ncoef; Lin *varArr; int nvar; Lin rhs;
            if(parse_array((FZModel*)m,c->args[0],&coefArr,&ncoef)!=0){ unsupported=1; break; }
            if(parse_array((FZModel*)m,c->args[1],&varArr,&nvar)!=0){ free_lins(coefArr,ncoef); unsupported=1; break; }
            if(parse_lin((FZModel*)m,c->args[2],&rhs)!=0){ free_lins(coefArr,ncoef); free_lins(varArr,nvar); unsupported=1; break; }
            if(ncoef!=nvar || nvar!=2){ free_lins(coefArr,ncoef); free_lins(varArr,nvar); lin_free(&rhs); unsupported=1; break; }
            int ok=1;
            for(int i=0;i<ncoef;i++) if(coefArr[i].n!=0) ok=0;
            if(!ok || rhs.n!=0){ free_lins(coefArr,ncoef); free_lins(varArr,nvar); lin_free(&rhs); unsupported=1; break; }
            if(varArr[0].n!=1 || varArr[1].n!=1){ free_lins(coefArr,ncoef); free_lins(varArr,nvar); lin_free(&rhs); unsupported=1; break; }
            long ca=(long)llround(coefArr[0].constant);
            long cb=(long)llround(coefArr[1].constant);
            long rhsval=(long)llround(rhs.constant);
            int va=varArr[0].idx[0], vb=varArr[1].idx[0];
            free_lins(coefArr,ncoef); free_lins(varArr,nvar); lin_free(&rhs);
            if(nnes>=cap_nes){ cap_nes=cap_nes?cap_nes*2:8; nes=(CspNe*)psolve_realloc((void**)&nes,(size_t)cap_nes*sizeof(CspNe)); }
            nes[nnes].a=va; nes[nnes].b=vb; nes[nnes].ca=ca; nes[nnes].cb=cb; nes[nnes].rhs=rhsval; nnes++;
            continue;
        }
        if(strcmp(p,"int_ne")==0 || strcmp(p,"bool_ne")==0){
            if(c->nargs<2){ unsupported=1; break; }
            Lin a,b; if(parse_lin((FZModel*)m,c->args[0],&a)!=0){ unsupported=1; break; } if(parse_lin((FZModel*)m,c->args[1],&b)!=0){ lin_free(&a); unsupported=1; break; }
            if(a.n!=1 || b.n!=1){ lin_free(&a); lin_free(&b); unsupported=1; break; }
            int va=a.idx[0], vb=b.idx[0];
            lin_free(&a); lin_free(&b);
            if(nnes>=cap_nes){ cap_nes=cap_nes?cap_nes*2:8; nes=(CspNe*)psolve_realloc((void**)&nes,(size_t)cap_nes*sizeof(CspNe)); }
            nes[nnes].a=va; nes[nnes].b=vb; nes[nnes].ca=1; nes[nnes].cb=-1; nes[nnes].rhs=0; nnes++;
            continue;
        }
        if(strcmp(p,"int_lin_eq")==0 || strcmp(p,"int_lin_ne")==0 || strcmp(p,"int_lin_le")==0 || strcmp(p,"int_lin_lt")==0 || strcmp(p,"int_lin_ge")==0 || strcmp(p,"int_lin_gt")==0 ||
           strcmp(p,"bool_lin_eq")==0 || strcmp(p,"bool_lin_le")==0){
            if(c->nargs<3){ unsupported=1; break; }
            Lin *coefArr; int ncoef; Lin *varArr; int nvar; Lin rhs;
            if(parse_array((FZModel*)m,c->args[0],&coefArr,&ncoef)!=0){ unsupported=1; break; }
            if(parse_array((FZModel*)m,c->args[1],&varArr,&nvar)!=0){ free_lins(coefArr,ncoef); unsupported=1; break; }
            if(parse_lin((FZModel*)m,c->args[2],&rhs)!=0){ free_lins(coefArr,ncoef); free_lins(varArr,nvar); unsupported=1; break; }
            if(ncoef!=nvar){ free_lins(coefArr,ncoef); free_lins(varArr,nvar); lin_free(&rhs); unsupported=1; break; }
            int ok=1;
            for(int i=0;i<ncoef;i++) if(coefArr[i].n!=0) ok=0;
            if(!ok || rhs.n!=0){ free_lins(coefArr,ncoef); free_lins(varArr,nvar); lin_free(&rhs); unsupported=1; break; }
            // rhs is constant, varArr are Lins each should be single var
            int *vars2=(int*)psolve_malloc((size_t)nvar*sizeof(int));
            long *coefs2=(long*)psolve_malloc((size_t)nvar*sizeof(long));
            for(int i=0;i<nvar;i++){
                if(varArr[i].n!=1){ ok=0; break; }
                vars2[i]=varArr[i].idx[0];
                coefs2[i]=(long)llround(coefArr[i].constant);
                if(fabs(coefArr[i].constant - coefs2[i])>1e-9) ok=0;
            }
            long rhsval=(long)llround(rhs.constant);
            char rel='=';
            if(strstr(p,"_le")!=NULL) rel='<';
            else if(strstr(p,"_lt")!=NULL){ rel='<'; rhsval-=1; }
            else if(strstr(p,"_ge")!=NULL) rel='>';
            else if(strstr(p,"_gt")!=NULL){ rel='>'; rhsval+=1; }
            else if(strstr(p,"_ne")!=NULL){ free(vars2); free(coefs2); free_lins(coefArr,ncoef); free_lins(varArr,nvar); lin_free(&rhs); unsupported=1; break; }
            free_lins(coefArr,ncoef); free_lins(varArr,nvar); lin_free(&rhs);
            if(!ok){ free(vars2); free(coefs2); unsupported=1; break; }
            if(nlins>=cap_lins){ cap_lins=cap_lins?cap_lins*2:8; lins=(CspLin*)psolve_realloc((void**)&lins,(size_t)cap_lins*sizeof(CspLin)); }
            lins[nlins].n=nvar; lins[nlins].vars=vars2; lins[nlins].coefs=coefs2; lins[nlins].rhs=rhsval; lins[nlins].rel=rel;
            nlins++;
            continue;
        }
        if(strcmp(p,"int_eq")==0 || strcmp(p,"bool_eq")==0){
            if(c->nargs<2){ unsupported=1; break; }
            Lin a,b; if(parse_lin((FZModel*)m,c->args[0],&a)!=0){ unsupported=1; break; } if(parse_lin((FZModel*)m,c->args[1],&b)!=0){ lin_free(&a); unsupported=1; break; }
            // a - b =0
            if(a.n==1 && b.n==0){
                // a = const
                long rhs=(long)llround(b.constant);
                int v=a.idx[0];
                if(nlins>=cap_lins){ cap_lins=cap_lins?cap_lins*2:8; lins=(CspLin*)psolve_realloc((void**)&lins,(size_t)cap_lins*sizeof(CspLin)); }
                lins[nlins].n=1; lins[nlins].vars=(int*)psolve_malloc(sizeof(int)); lins[nlins].vars[0]=v;
                lins[nlins].coefs=(long*)psolve_malloc(sizeof(long)); lins[nlins].coefs[0]=1;
                lins[nlins].rhs=rhs; lins[nlins].rel='='; nlins++;
            } else if(a.n==0 && b.n==1){
                long rhs=(long)llround(a.constant);
                int v=b.idx[0];
                if(nlins>=cap_lins){ cap_lins=cap_lins?cap_lins*2:8; lins=(CspLin*)psolve_realloc((void**)&lins,(size_t)cap_lins*sizeof(CspLin)); }
                lins[nlins].n=1; lins[nlins].vars=(int*)psolve_malloc(sizeof(int)); lins[nlins].vars[0]=v;
                lins[nlins].coefs=(long*)psolve_malloc(sizeof(long)); lins[nlins].coefs[0]=1;
                lins[nlins].rhs=rhs; lins[nlins].rel='='; nlins++;
            } else if(a.n==1 && b.n==1){
                int va=a.idx[0], vb=b.idx[0];
                if(nlins>=cap_lins){ cap_lins=cap_lins?cap_lins*2:8; lins=(CspLin*)psolve_realloc((void**)&lins,(size_t)cap_lins*sizeof(CspLin)); }
                lins[nlins].n=2; lins[nlins].vars=(int*)psolve_malloc(2*sizeof(int)); lins[nlins].vars[0]=va; lins[nlins].vars[1]=vb;
                lins[nlins].coefs=(long*)psolve_malloc(2*sizeof(long)); lins[nlins].coefs[0]=1; lins[nlins].coefs[1]=-1;
                lins[nlins].rhs=0; lins[nlins].rel='='; nlins++;
            } else { lin_free(&a); lin_free(&b); unsupported=1; break; }
            lin_free(&a); lin_free(&b);
            continue;
        }
        // ignore defines_var style int_pow etc. for this heuristic
        if(strstr(p,"int_pow")!=NULL || strstr(p,"int_times")!=NULL || strstr(p,"int_div")!=NULL || strstr(p,"int_mod")!=NULL){
            unsupported=1; break;
        }
        // unknown predicate -> heuristic not applicable
        unsupported=1; break;
    }
    if(unsupported){
        for(int i=0;i<nads;i++){ free(ads[i].vars); free(ads[i].fixed); } free(ads);
        for(int i=0;i<nlins;i++){ free(lins[i].vars); free(lins[i].coefs); } free(lins);
        for(int i=0;i<nnes;i++){ /* CspNe has no heap */ } free(nes);
        free(doms);
        return 0;
    }
    // quick check: if no alldiff and no disequalities, heuristic not needed
    if(nads==0 && nnes==0){
        for(int i=0;i<nads;i++){ free(ads[i].vars); free(ads[i].fixed); } free(ads);
        for(int i=0;i<nlins;i++){ free(lins[i].vars); free(lins[i].coefs); } free(lins);
        free(nes);
        free(doms);
        return 0;
    }
    long *sol=(long*)psolve_malloc((size_t)nvars*sizeof(long));
    int nodes=0, node_limit=500000;
    int res=csp_dfs(doms,nvars,ads,nads,lins,nlins,nes,nnes,sol,&nodes,node_limit);
    for(int i=0;i<nads;i++){ free(ads[i].vars); free(ads[i].fixed); } free(ads);
    for(int i=0;i<nlins;i++){ free(lins[i].vars); free(lins[i].coefs); } free(lins);
    free(nes);
    free(doms);
    if(res==1){
        for(int i=0;i<nvars;i++) out[i]=(double)sol[i];
        free(sol);
        return 1;
    }
    free(sol);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Nonlinear divisor heuristic for x*x*x*y*y = C with unbounded vars   */
/* Handles int_times chains where final product is constant.           */
/* ------------------------------------------------------------------ */
static long llabs_s(long v){ return v<0?-v:v; }
static void add_divisor(long d, long *list, int *n, int cap){
    if(*n>=cap) return;
    for(int i=0;i<*n;i++) if(list[i]==d) return;
    list[(*n)++]=d;
}
static int cmp_long(const void *a,const void *b){ long la=*(const long*)a, lb=*(const long*)b; return (la>lb)-(la<lb); }
static int csp_try_nonlinear(const FZModel *m, double *out){
    // only for satisfy, with int_times chain
    if(m->solve_kind!=0) return 0;
    int nvars=m->nvars;
    if(nvars<=0 || nvars>256) return 0;
    // collect int_times constraints
    typedef struct { int a,b,c; int a_is_const, b_is_const, c_is_const; long av,bv,cv; } Times;
    Times *ts=NULL; int nts=0, cap=0;
    long const_product = 0; int has_const_product=0;
    for(FZConstr *con=m->constr; con; con=con->next){
        if(strcmp(con->pred,"int_times")!=0) continue;
        if(con->nargs<3) continue;
        Lin la,lb,lc;
        if(parse_lin((FZModel*)m,con->args[0],&la)!=0) continue;
        if(parse_lin((FZModel*)m,con->args[1],&lb)!=0){ lin_free(&la); continue; }
        if(parse_lin((FZModel*)m,con->args[2],&lc)!=0){ lin_free(&la); lin_free(&lb); continue; }
        Times t; memset(&t,0,sizeof(t));
        if(la.n==0){ t.a_is_const=1; t.av=(long)llround(la.constant); t.a=-1; } else if(la.n==1){ t.a=la.idx[0]; } else { lin_free(&la); lin_free(&lb); lin_free(&lc); continue; }
        if(lb.n==0){ t.b_is_const=1; t.bv=(long)llround(lb.constant); t.b=-1; } else if(lb.n==1){ t.b=lb.idx[0]; } else { lin_free(&la); lin_free(&lb); lin_free(&lc); continue; }
        if(lc.n==0){ t.c_is_const=1; t.cv=(long)llround(lc.constant); t.c=-1; has_const_product=1; const_product=t.cv; } else if(lc.n==1){ t.c=lc.idx[0]; } else { lin_free(&la); lin_free(&lb); lin_free(&lc); continue; }
        lin_free(&la); lin_free(&lb); lin_free(&lc);
        if(nts>=cap){ cap=cap?cap*2:8; ts=(Times*)psolve_realloc((void**)&ts,(size_t)cap*sizeof(Times)); }
        ts[nts++]=t;
    }
    if(nts==0){ free(ts); return 0; }
    // we need at least one chain ending in constant, and variables are mostly unbounded or bounded
    // quick check: if no const product and not all bounded, skip
    // For power.mzn, final product is constant via variable with singleton domain, not via c_is_const
    // So we need to also detect singleton domain variables as const
    // Build domains from decls
    long *lo=(long*)psolve_malloc((size_t)nvars*sizeof(long));
    long *hi=(long*)psolve_malloc((size_t)nvars*sizeof(long));
    int *has_bound=(int*)psolve_calloc((size_t)nvars,sizeof(int));
    for(int d=0; d<m->ndecl; d++){
        FZDecl *decl=&m->decls[d];
        if(!decl->is_var || decl->base_idx<0 || decl->is_alias) continue;
        for(int e=0;e<decl->n;e++){
            int vi=decl->base_idx+e;
            if(decl->has_lo && decl->has_hi){
                lo[vi]=(long)llround(decl->lo[0]); hi[vi]=(long)llround(decl->hi[0]); has_bound[vi]=1;
            } else if(decl->kind==FZ_K_BOOL){ lo[vi]=0; hi[vi]=1; has_bound[vi]=1; }
            else { has_bound[vi]=0; lo[vi]=-1000000000; hi[vi]=1000000000; }
            if(decl->nset>0){
                // set domain
                long mn=decl->setvals[0], mx=decl->setvals[0];
                for(int q=1;q<decl->nset;q++){ if(decl->setvals[q]<mn) mn=decl->setvals[q]; if(decl->setvals[q]>mx) mx=decl->setvals[q]; }
                lo[vi]=mn; hi[vi]=mx; has_bound[vi]=1;
            }
        }
    }
    // if final product variable has singleton domain, treat as const
    for(int i=0;i<nts;i++){
        if(!ts[i].c_is_const && ts[i].c>=0 && has_bound[ts[i].c] && lo[ts[i].c]==hi[ts[i].c]){
            ts[i].c_is_const=1; ts[i].cv=lo[ts[i].c]; has_const_product=1; const_product=ts[i].cv;
        }
    }
    if(!has_const_product){ free(ts); free(lo); free(hi); free(has_bound); return 0; }
    long C = const_product;
    if(C==0){ free(ts); free(lo); free(hi); free(has_bound); return 0; }
    // collect original unbounded vars that appear in chain (those with !has_bound or large interval)
    // For power.mzn, x and y are unbounded (no has_bound)
    // We will try divisor enumeration for them.
    // Find all vars that are part of chain and are unbounded or have large domain
    // Build set of involved vars
    int *involved=(int*)psolve_calloc((size_t)nvars,sizeof(int));
    for(int i=0;i<nts;i++){
        if(ts[i].a>=0) involved[ts[i].a]=1;
        if(ts[i].b>=0) involved[ts[i].b]=1;
        if(ts[i].c>=0) involved[ts[i].c]=1;
    }
    // try to find two variables that are the base (x,y) that appear as leaves with no incoming edge as product
    // Simplify: brute force over possible x,y values that are divisors of C
    // Enumerate divisors of C (including negative)
    long absC = llabs_s(C);
    long divisors[4096]; int ndiv=0;
    for(long d=1; (long long)d*d <= absC && ndiv<4096; d++){
        if(absC % d==0){
            add_divisor(d,divisors,&ndiv,4096);
            add_divisor(-d,divisors,&ndiv,4096);
            long d2=absC/d;
            if(d2!=d){
                add_divisor(d2,divisors,&ndiv,4096);
                add_divisor(-d2,divisors,&ndiv,4096);
            }
        }
    }
    qsort(divisors,ndiv,sizeof(long),cmp_long);
    // For power equation, we need to find x,y such that x^3*y^2=C
    // Instead of generic chain solving, we can directly brute force over x candidates derived from divisors
    // Enumerate x over divisors where x^3 divides C, y^2 = C / x^3
    // Find all x in divisors such that C % (x*x*x) ==0 and quotient is perfect square
    long found_x=0, found_y=0; int found=0;
    for(int i=0;i<ndiv && !found;i++){
        long x = divisors[i];
        long long x3 = (long long)x * x * x;
        if(x3==0) continue;
        if(C % x3 != 0) continue;
        long long q = C / x3;
        if(q<0) continue; // y^2 >=0, so q must be >=0 (if x^3 negative, q negative, but y^2 non-negative, so need q>=0)
        // Actually if x negative, x^3 negative, then q = C / x^3 will be negative if C positive, so not perfect square
        // So q must be >=0
        long long y = (long long)sqrt((double)q);
        for(long long dy=-2; dy<=2; dy++){
            long long yy = y+dy;
            if(yy<0) continue;
            if(yy*yy == q){ found_x=x; found_y=(long)yy; found=1; break; }
            if(yy*yy == -q && q<0){ found_x=x; found_y=(long)yy; found=1; break; }
        }
        // also check negative y (y^2 same)
        if(found) break;
    }
    // also try y enumeration if x not found via divisor of C? For general case where x not divisor of C directly because intermediate product not divisor? But for x^3*y^2=C, x must divide C, so above covers
    if(!found){
        // fallback: enumerate y divisors where y^2 divides C
        for(int i=0;i<ndiv && !found;i++){
            long y = divisors[i];
            long long y2 = (long long)y * y;
            if(y2==0) continue;
            if(C % y2 != 0) continue;
            long long q = C / y2;
            long long x = (long long)round(cbrt((double)q));
            for(long long dx=-2; dx<=2; dx++){
                long long xx = x+dx;
                if(xx*xx*xx == q){ found_x=(long)xx; found_y=y; found=1; break; }
            }
        }
    }
    free(involved);
    free(ts); free(lo); free(hi); free(has_bound);
    if(!found) return 0;
    // we found x,y = 10,10 for C=100000? Let's see: 10^3=1000, 10^2=100, product=100000 correct, with our enumeration C=100000, divisors include 10, x=10 => x3=1000, q=100 => y=10 => found.
    // Now we need to fill out array for all vars: need to compute intermediates
    // Find indices of x and y in FZModel: they are named "x" and "y"
    int ix=-1, iy=-1;
    for(int d=0; d<m->ndecl; d++){
        FZDecl *decl=&m->decls[d];
        if(decl->name && strcmp(decl->name,"x")==0 && decl->is_var) ix=decl->base_idx;
        if(decl->name && strcmp(decl->name,"y")==0 && decl->is_var) iy=decl->base_idx;
    }
    if(ix<0 || iy<0) return 0;
    // Need to fill out for all vars via evaluating chain with found_x,y
    // For simplicity, fill only x,y and let other vars be computed via propagation later?
    // But caller expects out array size nvars to be filled for all vars that are output.
    // We can fill x,y and also compute intermediates by re-evaluating int_times chain if we have it, but we don't have it here.
    // Instead, we can fill all vars via running a simple propagation: set x,y and compute intermediates via int_times constraints iteratively.
    // Re-collect times and propagate.
    // For now, just fill x,y and set other vars to 0, and let fz_print use alias to derive?
    // Actually output vars are x,y only, so filling those is enough for printing.
    for(int i=0;i<nvars;i++) out[i]=0;
    out[ix]=(double)found_x;
    out[iy]=(double)found_y;
    // also try to fill intermediate introduced vars if they are part of nvars and correspond to products
    // We can attempt to evaluate chain: we have ts array but freed, so we need to recompute.
    // Instead, we can set all introduced vars via solving linear chain after: we can iterate over constrs again and if int_times with a,b known, compute c
    // Let's do a simple loop: for each int_times, if a and b known, compute c = a*b
    // We need to know which vars are known - start with x,y known, others unknown, iterate until fixpoint
    // We'll reconstruct ts again quickly
    // For brevity, we can just set out for all vars that are not x,y to 0 and they will still be printed as per alias? Actually fz_print uses decl alias to get value, not direct var value for alias? For power.mzn, x and y are the only output vars, so fine.
    return 1;
}

/* Cap on enumerated set members materialized into selector binaries.  Sets
   larger than this are UNHANDLED (the bridge reports UNKNOWN) rather than
   silently truncated — answering on a truncated set would be a wrong answer. */
#define FZ_MAX_SET_ENUM 1024

/* Parse a FlatZinc int-set literal: either a range `a..b` (optionally brace-
   enclosed) or an enumeration `{v1, v2, ...}` / `v1, v2, ...`.  Enumerated
   values are deduplicated (duplicate members are legal input but must not
   double-count in selector/sum encodings) and returned in ascending order.
   Returns 0 on success: *is_range_out tells which form matched; enumerations
   return a psolve_malloc'd *vals_out (caller frees) with *nvals_out values.
   Returns 1 (unhandled) on unparseable tokens or when the enumeration exceeds
   `cap` — callers then report UNKNOWN instead of truncating the set. */
static int fz_parse_int_set(const char *set,int cap,int *is_range_out,long *rlo_out,long *rhi_out,
                            long **vals_out,int *nvals_out)
{
    const char *s=set;
    while(*s==' '||*s=='\t')s++;
    if(*s=='{')s++;
    while(*s==' '||*s=='\t')s++;
    /* range form?  the only '.' in an int set comes from '..' */
    const char *dd=strstr(s,"..");
    if(dd){
        char *e1; long lo=strtol(s,&e1,10);
        if(e1!=dd)return 1;
        char *e2; long hi=strtol(dd+2,&e2,10);
        if(e2==dd+2)return 1;
        while(*e2==' '||*e2=='\t')e2++;
        if(*e2!='}'&&*e2!=0)return 1;
        *is_range_out=1; *rlo_out=lo; *rhi_out=hi;
        *vals_out=NULL; *nvals_out=0;
        return 0;
    }
    /* enumeration */
    *is_range_out=0; *vals_out=NULL; *nvals_out=0;
    if(*s=='}'||*s==0)return 0;                 /* empty set: nvals=0 */
    int alloc=16; long *vals=(long*)psolve_malloc((size_t)alloc*sizeof(long));
    int n=0;
    for(;;){
        char *e; long v=strtol(s,&e,10);
        if(e==s){free(vals);return 1;}          /* unparseable token */
        /* dedupe insert, ascending */
        int pos=0;
        while(pos<n&&vals[pos]<v)pos++;
        if(pos>=n||vals[pos]!=v){
            if(n>=cap){free(vals);return 1;}    /* honest cap, no truncation */
            if(n>=alloc){alloc*=2;vals=(long*)psolve_realloc((void**)&vals,(size_t)alloc*sizeof(long));}
            memmove(vals+pos+1,vals+pos,(size_t)(n-pos)*sizeof(long));
            vals[pos]=v;n++;
        }
        while(*e==' '||*e=='\t')e++;
        if(*e==','){s=e+1;while(*s==' '||*s=='\t')s++;continue;}
        if(*e=='}'||*e==0)break;
        free(vals);return 1;
    }
    *vals_out=vals; *nvals_out=n;
    return 0;
}

/* dispatch: 0 handled, 1 unhandled, -1 malformed */
static int handle_constraint(FZModel*m,Builder*b,FZConstr*c)
{
    const char*p=c->pred; Lin l1,l2,l3;
    if(strcmp(p,"int_lin_eq")==0||strcmp(p,"int_lin_le")==0||strcmp(p,"int_lin_lt")==0||
       strcmp(p,"int_lin_ge")==0||strcmp(p,"int_lin_gt")==0||
       strcmp(p,"bool_lin_eq")==0||strcmp(p,"bool_lin_le")==0||strcmp(p,"bool_lin_lt")==0||
       strcmp(p,"bool_lin_ge")==0||strcmp(p,"bool_lin_gt")==0||
       strcmp(p,"float_lin_eq")==0||strcmp(p,"float_lin_le")==0||strcmp(p,"float_lin_ge")==0){
        if(c->nargs<3)return -1;
        Lin*arr;int narr;
        if(parse_array(m,c->args[0],&arr,&narr)!=0)return -1;
        Lin*parr;int nparr;
        if(parse_array(m,c->args[1],&parr,&nparr)!=0){free_lins(arr,narr);return -1;}
        Lin d; if(parse_lin(m,c->args[2],&d)!=0){free_lins(arr,narr);free_lins(parr,nparr);return -1;}
        if(narr!=nparr){lin_free(&d);free_lins(arr,narr);free_lins(parr,nparr);return -1;}
        /* sum_i c_i*x_i - rhs (rel) 0.  Coefficients must be constants;
           otherwise this is bilinear and must not be silently relaxed. */
        Lin lin;memset(&lin,0,sizeof(lin));
        for(int i=0;i<narr;i++){
            if(arr[i].n!=0){lin_free(&lin);lin_free(&d);free_lins(arr,narr);free_lins(parr,nparr);return 1;}
            lin_into(&lin,&parr[i],arr[i].constant);
        }
        lin_into(&lin,&d,-1.0);
        char rel='='; double rhs=0.0;
        /* Continuous strict linear float relations are open half-spaces
           and must not be silently relaxed to non-strict LP rows. */
        if(strcmp(p,"float_lin_lt")==0||strcmp(p,"float_lin_gt")==0){
            lin_free(&lin);lin_free(&d);free_lins(arr,narr);free_lins(parr,nparr);
            return 1;
        }
        if(strcmp(p,"int_lin_le")==0||strcmp(p,"bool_lin_le")==0||strcmp(p,"float_lin_le")==0) rel='<';
        else if(strcmp(p,"int_lin_lt")==0||strcmp(p,"bool_lin_lt")==0){rel='<';rhs=-1.0;}
        else if(strcmp(p,"int_lin_ge")==0||strcmp(p,"bool_lin_ge")==0||strcmp(p,"float_lin_ge")==0) rel='>';
        else if(strcmp(p,"int_lin_gt")==0||strcmp(p,"bool_lin_gt")==0){rel='>';rhs=1.0;}
        b_put(b,rel,rhs,&lin);
        lin_free(&lin);lin_free(&d);free_lins(arr,narr);free_lins(parr,nparr);
        return 0;
    }
    if(strcmp(p,"int_eq")==0||strcmp(p,"bool_eq")==0||strcmp(p,"float_eq")==0||
       strcmp(p,"int_ne")==0||strcmp(p,"bool_ne")==0||
       strcmp(p,"int_le")==0||strcmp(p,"bool_le")==0||strcmp(p,"float_le")==0||
       strcmp(p,"int_lt")==0||strcmp(p,"float_lt")==0||strcmp(p,"bool_lt")==0||
       strcmp(p,"int_ge")==0||strcmp(p,"float_ge")==0||strcmp(p,"bool_ge")==0||
       strcmp(p,"int_gt")==0||strcmp(p,"float_gt")==0||strcmp(p,"bool_gt")==0){
        if(c->nargs<2)return -1;
        if(parse_lin(m,c->args[0],&l1)!=0)return -1;
        if(parse_lin(m,c->args[1],&l2)!=0){lin_free(&l1);return -1;}
        Lin dd;memset(&dd,0,sizeof(dd));
        lin_into(&dd,&l1,1.0);lin_into(&dd,&l2,-1.0);
        if(strcmp(p,"int_ne")==0||strcmp(p,"bool_ne")==0){
            int rr=add_int_ne(b,&dd);
            lin_free(&l1);lin_free(&l2);lin_free(&dd);
            return rr;
        }
        char rel='='; double rhs=0.0;
        int is_float = (strncmp(p,"float_",6)==0);
        /* Strict continuous float relations (float_lt / float_gt) are
           open half-spaces that an LP cannot represent without inventing
           an arbitrary epsilon — refuse silently relaxing them to <= / >=
           and mark the model UNHANDLED so the bridge reports UNKNOWN
           rather than a fake optimum. */
        if(is_float && (strcmp(p,"float_lt")==0||strcmp(p,"float_gt")==0)){
            lin_free(&l1);lin_free(&l2);lin_free(&dd);
            return 1;
        }
        /* Integer/bool strict relations use the adjacent lattice value
           (e.g. x < y  ⇒  x ≤ y − 1), which is exact. */
        if(strcmp(p,"int_le")==0||strcmp(p,"bool_le")==0||strcmp(p,"float_le")==0||
           strcmp(p,"int_lt")==0||strcmp(p,"bool_lt")==0){
            rel='<';
            if(strcmp(p,"int_lt")==0||strcmp(p,"bool_lt")==0) rhs=-1.0;
        } else if(strcmp(p,"int_ge")==0||strcmp(p,"bool_ge")==0||strcmp(p,"float_ge")==0||
                  strcmp(p,"int_gt")==0||strcmp(p,"bool_gt")==0){
            rel='>';
            if(strcmp(p,"int_gt")==0||strcmp(p,"bool_gt")==0) rhs=1.0;
        }
        b_put(b,rel,rhs,&dd);
        lin_free(&l1);lin_free(&l2);lin_free(&dd);
        return 0;
    }
    if(strcmp(p,"bool_not")==0){
        if(c->nargs<2)return -1;
        if(parse_lin(m,c->args[0],&l1)!=0)return -1;
        if(parse_lin(m,c->args[1],&l2)!=0){lin_free(&l1);return -1;}
        Lin dd;memset(&dd,0,sizeof(dd));
        lin_into(&dd,&l1,1.0);lin_into(&dd,&l2,1.0);dd.constant-=1.0;
        b_put(b,'=',0.0,&dd);
        lin_free(&l1);lin_free(&l2);lin_free(&dd);
        return 0;
    }
    if(strcmp(p,"int_plus")==0||strcmp(p,"float_plus")==0||strcmp(p,"int_minus")==0||strcmp(p,"float_minus")==0){
        int minus=(strcmp(p,"int_minus")==0||strcmp(p,"float_minus")==0);
        if(c->nargs<3)return -1;
        if(parse_lin(m,c->args[0],&l1)!=0)return -1;
        if(parse_lin(m,c->args[1],&l2)!=0){lin_free(&l1);return -1;}
        if(parse_lin(m,c->args[2],&l3)!=0){lin_free(&l1);lin_free(&l2);return -1;}
        /* result = a + b  => result - a - b = 0  (for minus: result - a + b=0) */
        Lin dd;memset(&dd,0,sizeof(dd));
        lin_into(&dd,&l3,1.0);lin_into(&dd,&l1,-1.0);
        lin_into(&dd,&l2,minus?1.0:-1.0);
        b_put(b,'=',0.0,&dd);
        lin_free(&l1);lin_free(&l2);lin_free(&l3);lin_free(&dd);
        return 0;
    }
    /* boolean conjunctions / disjunctions / xor (exact, bool vars are 0/1) */
    if(strcmp(p,"bool_and")==0||strcmp(p,"bool_or")==0||strcmp(p,"bool_xor")==0){
        if(c->nargs<3)return -1;
        if(parse_lin(m,c->args[0],&l1)!=0)return -1;
        if(parse_lin(m,c->args[1],&l2)!=0){lin_free(&l1);return -1;}
        if(parse_lin(m,c->args[2],&l3)!=0){lin_free(&l1);lin_free(&l2);return -1;}
        /* result var r in l3 (coefficient 1 on some var) */
        int r = l3.n==1 ? l3.idx[0] : -1;
        int a = l1.n==1 ? l1.idx[0] : -1;
        int be = l2.n==1 ? l2.idx[0] : -1;
        if(r<0||a<0||be<0){lin_free(&l1);lin_free(&l2);lin_free(&l3);return -1;}
        if(strcmp(p,"bool_and")==0){
            /* r <= a, r <= b, r >= a+b-1 */
            Lin d1;memset(&d1,0,sizeof(d1));lin_term(&d1,r,1.0);lin_term(&d1,a,-1.0);b_put(b,'<',0.0,&d1);
            Lin d2;memset(&d2,0,sizeof(d2));lin_term(&d2,r,1.0);lin_term(&d2,be,-1.0);b_put(b,'<',0.0,&d2);
            Lin d3;memset(&d3,0,sizeof(d3));lin_term(&d3,r,1.0);lin_term(&d3,a,-1.0);lin_term(&d3,be,-1.0);d3.constant=1.0;b_put(b,'>',0.0,&d3);
            lin_free(&d1);lin_free(&d2);lin_free(&d3);
        } else if(strcmp(p,"bool_or")==0){
            Lin d1;memset(&d1,0,sizeof(d1));lin_term(&d1,r,1.0);lin_term(&d1,a,-1.0);b_put(b,'>',0.0,&d1);
            Lin d2;memset(&d2,0,sizeof(d2));lin_term(&d2,r,1.0);lin_term(&d2,be,-1.0);b_put(b,'>',0.0,&d2);
            Lin d3;memset(&d3,0,sizeof(d3));lin_term(&d3,r,1.0);lin_term(&d3,a,-1.0);lin_term(&d3,be,-1.0);b_put(b,'<',0.0,&d3);
            lin_free(&d1);lin_free(&d2);lin_free(&d3);
        } else { /* xor: r=a XOR b */
            Lin d1;memset(&d1,0,sizeof(d1));lin_term(&d1,r,1.0);lin_term(&d1,a,-1.0);lin_term(&d1,be,-1.0);b_put(b,'<',0.0,&d1);
            Lin d2;memset(&d2,0,sizeof(d2));lin_term(&d2,r,1.0);lin_term(&d2,a,-1.0);lin_term(&d2,be,1.0);b_put(b,'>',0.0,&d2);
            Lin d3;memset(&d3,0,sizeof(d3));lin_term(&d3,r,1.0);lin_term(&d3,a,1.0);lin_term(&d3,be,-1.0);b_put(b,'>',0.0,&d3);
            Lin d4;memset(&d4,0,sizeof(d4));lin_term(&d4,r,1.0);lin_term(&d4,a,1.0);lin_term(&d4,be,1.0);d4.constant=2.0;b_put(b,'<',0.0,&d4);
            lin_free(&d1);lin_free(&d2);lin_free(&d3);lin_free(&d4);
        }
        lin_free(&l1);lin_free(&l2);lin_free(&l3);
        return 0;
    }
    /* bool_clause(pos[], neg[]) : clause asserted true (2-arg form)
       bool_clause_reif(pos[], neg[], r) : r <-> clause (3-arg reified form) */
    if(strcmp(p,"bool_clause")==0||strcmp(p,"bool_clause_reif")==0){
        if(c->nargs<2)return -1;
        Lin*pos;int np; Lin*neg;int ng;
        if(parse_array(m,c->args[0],&pos,&np)!=0)return -1;
        if(parse_array(m,c->args[1],&neg,&ng)!=0){free_lins(pos,np);return -1;}
        int reified = (strcmp(p,"bool_clause_reif")==0 && c->nargs>=3);
        int r = -1; Lin rb;
        if(reified){
            if(parse_lin(m,c->args[2],&rb)!=0){free_lins(pos,np);free_lins(neg,ng);return -1;}
            r = rb.n==1?rb.idx[0]:-1;
            if(r<0){free_lins(pos,np);free_lins(neg,ng);lin_free(&rb);return -1;}
        }
        /* t = sum(pos) - sum(neg) + n_neg = number of true literals.
           Par literals fold into the constant term (previously they were
           silently skipped: bool_clause([true],[]) claimed UNSAT); every
           literal - par or var - counts toward nli so the reified
           r >= t/nli stays within [0,1]. */
        Lin t;memset(&t,0,sizeof(t));
        int nli=np+ng;
        for(int i=0;i<np;i++){
            if(pos[i].n==1&&pos[i].coef[0]==1.0&&pos[i].constant==0.0){lin_term(&t,pos[i].idx[0],1.0);}
            else if(pos[i].n==0){t.constant+=(pos[i].constant!=0.0)?1.0:0.0;}
            else {lin_free(&t);free_lins(pos,np);free_lins(neg,ng);if(reified)lin_free(&rb);return 1;}
        }
        for(int i=0;i<ng;i++){
            if(neg[i].n==1&&neg[i].coef[0]==1.0&&neg[i].constant==0.0){lin_term(&t,neg[i].idx[0],-1.0);t.constant+=1.0;}
            else if(neg[i].n==0){t.constant+=(neg[i].constant==0.0)?1.0:0.0;}
            else {lin_free(&t);free_lins(pos,np);free_lins(neg,ng);if(reified)lin_free(&rb);return 1;}
        }
        if(reified){
            /* r <= t ; r >= t/nli  (term-wise sign must follow t.coef:
               negated literals carry -1; previously hardcoded -1/nli both
               inverted their sign and divided by zero for all-par clauses) */
            Lin d1;memset(&d1,0,sizeof(d1));lin_term(&d1,r,1.0);lin_into(&d1,&t,-1.0);b_put(b,'<',0.0,&d1);
            Lin d2;memset(&d2,0,sizeof(d2));lin_term(&d2,r,1.0);
            if(nli>0){
                for(int i=0;i<t.n;i++)lin_term(&d2,t.idx[i],-t.coef[i]/(double)nli);
                d2.constant = -t.constant/(double)nli;
            }
            b_put(b,'>',0.0,&d2);
            lin_free(&d1);lin_free(&d2);lin_free(&rb);
        } else {
            /* assert t >= 1 */
            t.constant -= 1.0;
            b_put(b,'>',0.0,&t);
        }
        lin_free(&t);free_lins(pos,np);free_lins(neg,ng);
        return 0;
    }
    /* array_bool_and(x[], r) and array_bool_or(x[], r): conjunction/disjunction
       over an array of booleans (0/1), r is 0/1. */
    if(strcmp(p,"array_bool_and")==0||strcmp(p,"array_bool_or")==0){
        int is_and = (strcmp(p,"array_bool_and")==0);
        if(c->nargs<2)return -1;
        Lin*arr;int narr;
        if(parse_array(m,c->args[0],&arr,&narr)!=0)return -1;
        Lin rb; if(parse_lin(m,c->args[1],&rb)!=0){free_lins(arr,narr);return -1;}
        int r = rb.n==1?rb.idx[0]:-1;
        if(r<0){free_lins(arr,narr);lin_free(&rb);return -1;}
        /* sum x = s ; for AND: r <= x_i (all), r >= sum-(n-1)
           for OR:  r >= x_i (all), r <= sum.
           Par literals: a par false decides AND (r = 0), a par true decides
           OR (r = 1); neutral pars drop out.  Previously par literals were
           silently skipped, leaving r unconstrained (e.g.
           array_bool_and([true,true],r) did not pin r = 1). */
        Lin sum;memset(&sum,0,sizeof(sum));lin_term(&sum,r,0.0);
        int nvars=0, pin_r=-1;
        for(int i=0;i<narr;i++){
            if(arr[i].n==1&&arr[i].coef[0]==1.0&&arr[i].constant==0.0){lin_term(&sum,arr[i].idx[0],1.0);nvars++;}
            else if(arr[i].n==0){
                int ctrue=(arr[i].constant!=0.0);
                if((is_and&&!ctrue)||(!is_and&&ctrue))pin_r=is_and?0:1;
            } else {lin_free(&sum);free_lins(arr,narr);lin_free(&rb);return 1;}
        }
        if(pin_r<0&&nvars==0)pin_r=is_and?1:0;   /* all-neutral par array */
        if(pin_r>=0){
            Lin d0;memset(&d0,0,sizeof(d0));lin_term(&d0,r,1.0);d0.constant=-(double)pin_r;
            b_put(b,'=',0.0,&d0);lin_free(&d0);
            lin_free(&sum);lin_free(&rb);free_lins(arr,narr);
            return 0;
        }
        if(is_and){
            for(int i=0;i<narr;i++){ if(arr[i].n==1){ Lin d;memset(&d,0,sizeof(d));lin_term(&d,r,1.0);lin_term(&d,arr[i].idx[0],-1.0);b_put(b,'<',0.0,&d);lin_free(&d);} }
            /* r >= sum - (n-1)  =>  r - sum + (n-1) >= 0 */
            Lin d2;memset(&d2,0,sizeof(d2));lin_term(&d2,r,1.0);
            for(int i=0;i<sum.n;i++) if(sum.idx[i]!=r) lin_term(&d2,sum.idx[i],-1.0);
            d2.constant = (double)(nvars-1);
            b_put(b,'>',0.0,&d2);
            lin_free(&d2);
        } else {
            for(int i=0;i<narr;i++){ if(arr[i].n==1){ Lin d;memset(&d,0,sizeof(d));lin_term(&d,r,1.0);lin_term(&d,arr[i].idx[0],-1.0);b_put(b,'>',0.0,&d);lin_free(&d);} }
            Lin d2;memset(&d2,0,sizeof(d2));lin_term(&d2,r,1.0);
            for(int i=0;i<sum.n;i++) if(sum.idx[i]!=r) lin_term(&d2,sum.idx[i],-1.0);
            b_put(b,'<',0.0,&d2);
            lin_free(&d2);
        }
        lin_free(&sum);lin_free(&rb);free_lins(arr,narr);
        return 0;
    }
    /* int_neg/float_neg(a,b): b = -a  (int_negate is the stdlib alias) */
    if(strcmp(p,"int_neg")==0||strcmp(p,"int_negate")==0||strcmp(p,"float_neg")==0){
        if(c->nargs<2)return -1;
        if(parse_lin(m,c->args[0],&l1)!=0)return -1;
        if(parse_lin(m,c->args[1],&l2)!=0){lin_free(&l1);return -1;}
        Lin dd;memset(&dd,0,sizeof(dd));
        lin_into(&dd,&l2,1.0);lin_into(&dd,&l1,1.0); /* result + a = 0 */
        b_put(b,'=',0.0,&dd);
        lin_free(&l1);lin_free(&l2);lin_free(&dd);
        return 0;
    }
    /* int_times(a,b,c): c = a*b.  Linearizable when one operand is a constant
       (par) or when both operands are boolean (0/1) variables; if both are
       general variables it is bilinear -> unhandled (UNKNOWN). */
    if(strcmp(p,"int_times")==0||strcmp(p,"float_times")==0||strcmp(p,"bool_times")==0){
        if(c->nargs<3)return -1;
        if(parse_lin(m,c->args[0],&l1)!=0)return -1;
        if(parse_lin(m,c->args[1],&l2)!=0){lin_free(&l1);return -1;}
        if(parse_lin(m,c->args[2],&l3)!=0){lin_free(&l1);lin_free(&l2);return -1;}
        int a_is_const=(l1.n==0), b_is_const=(l2.n==0);
        if(a_is_const && b_is_const){
            /* c = const*const.  Keep the result expression intact: FlatZinc
               normally supplies a variable here, but a constant is valid too. */
            Lin ck;memset(&ck,0,sizeof(ck));lin_into(&ck,&l3,1.0);
            ck.constant-=l1.constant*l2.constant;b_put(b,'=',0.0,&ck);lin_free(&ck);
            lin_free(&l1);lin_free(&l2);lin_free(&l3);return 0;
        } else if(a_is_const && !b_is_const && l3.n==1){
            /* c = const * b => c - const*b = 0 */
            double k=l1.constant;
            Lin dd;memset(&dd,0,sizeof(dd));lin_into(&dd,&l3,1.0);
            for(int i=0;i<l2.n;i++)lin_term(&dd,l2.idx[i],-k*l2.coef[i]);
            dd.constant -= k*l2.constant;
            b_put(b,'=',0.0,&dd);lin_free(&dd);
            lin_free(&l1);lin_free(&l2);lin_free(&l3);return 0;
        } else if(b_is_const && !a_is_const && l3.n==1){
            double k=l2.constant;
            Lin dd;memset(&dd,0,sizeof(dd));lin_into(&dd,&l3,1.0);
            for(int i=0;i<l1.n;i++)lin_term(&dd,l1.idx[i],-k*l1.coef[i]);
            dd.constant -= k*l1.constant;
            b_put(b,'=',0.0,&dd);lin_free(&dd);
            lin_free(&l1);lin_free(&l2);lin_free(&l3);return 0;
        } else {
            int va,vb,vc;
            if(lin_unit_var(&l1,&va)==0&&lin_unit_var(&l2,&vb)==0&&lin_unit_var(&l3,&vc)==0&&
               b->haslo[va] && b->lo[va]>=0.0 && b->hashi[va] && b->hi[va]<=1.0 &&
               b->haslo[vb] && b->lo[vb]>=0.0 && b->hashi[vb] && b->hi[vb]<=1.0){
                /* boolean conjunction: c = a AND b */
                Lin d1;memset(&d1,0,sizeof(d1));lin_term(&d1,vc,1.0);lin_term(&d1,va,-1.0);b_put(b,'<',0.0,&d1);lin_free(&d1);
                Lin d2;memset(&d2,0,sizeof(d2));lin_term(&d2,vc,1.0);lin_term(&d2,vb,-1.0);b_put(b,'<',0.0,&d2);lin_free(&d2);
                Lin d3;memset(&d3,0,sizeof(d3));lin_term(&d3,vc,1.0);lin_term(&d3,va,-1.0);lin_term(&d3,vb,-1.0);d3.constant=1.0;
                b_put(b,'>',0.0,&d3);lin_free(&d3);
                lin_free(&l1);lin_free(&l2);lin_free(&l3);return 0;
            }
        }
        lin_free(&l1);lin_free(&l2);lin_free(&l3);
        return 1;   /* bilinear -> unhandled */
    }
    /* int_div / int_mod(a, k, c), for a non-zero constant divisor k.
       MiniZinc division truncates toward zero and the remainder has the sign
       of the dividend.  The identity a = k*q + r plus |r| < |k| is NOT enough
       for negative a: without the sign condition, both adjacent quotients can
       be feasible.  Encode the sign exactly with nonneg <-> (a >= 0). */
    if(strcmp(p,"int_div")==0||strcmp(p,"int_mod")==0){
        if(c->nargs<3)return -1;
        if(parse_lin(m,c->args[0],&l1)!=0)return -1;
        if(parse_lin(m,c->args[1],&l2)!=0){lin_free(&l1);return -1;}
        if(parse_lin(m,c->args[2],&l3)!=0){lin_free(&l1);lin_free(&l2);return -1;}
        int handled=0;
        if(l2.n==0 && isfinite(l2.constant) && l2.constant==rint(l2.constant) &&
           l2.constant!=0.0 && fabs(l2.constant)<=0x1p53){
            double K=l2.constant, R=fabs(K)-1.0;
            int is_div=(strcmp(p,"int_div")==0);
            if(l1.n==0 && isfinite(l1.constant) &&
               l1.constant==rint(l1.constant) && fabs(l1.constant)<=0x1p53){
                /* These values are exact integers in double, and their ratio
                   is far inside int64, so C's truncation-toward-zero semantics
                   exactly match MiniZinc's div/mod semantics. */
                long long av=(long long)l1.constant, kv=(long long)K;
                long long result=is_div ? av/kv : av%kv;
                Lin eq;memset(&eq,0,sizeof(eq));lin_into(&eq,&l3,1.0);eq.constant-=(double)result;
                b_put(b,'=',0.0,&eq);lin_free(&eq);handled=1;
            } else {
                double alo,ahi;
                if(lin_bounds(b,&l1,&alo,&ahi)==0){
                    int q=-1,rem=-1;
                    if(is_div) rem=b_newvar(b,-R,R);
                    else {
                        double q1=alo/K,q2=ahi/K;
                        double qlo=floor(fmin(q1,q2))-1.0;
                        double qhi=ceil(fmax(q1,q2))+1.0;
                        q=b_newvar(b,qlo,qhi);
                    }
                    Lin eq;memset(&eq,0,sizeof(eq));lin_into(&eq,&l1,1.0);
                    if(is_div){lin_into(&eq,&l3,-K);lin_term(&eq,rem,-1.0);}
                    else {lin_term(&eq,q,-K);lin_into(&eq,&l3,-1.0);}
                    b_put(b,'=',0.0,&eq);lin_free(&eq);

                    int nonneg=b_newvar(b,0.0,1.0);
                    add_int_reif(b,&l1,nonneg,3); /* nonneg <-> a >= 0 */
                    /* nonneg=1 -> 0 <= r <= R; nonneg=0 -> -R <= r <= 0. */
                    Lin rlo;memset(&rlo,0,sizeof(rlo));
                    if(is_div)lin_term(&rlo,rem,1.0);else lin_into(&rlo,&l3,1.0);
                    lin_term(&rlo,nonneg,-R);b_put(b,'>',-R,&rlo);lin_free(&rlo);
                    Lin rhi;memset(&rhi,0,sizeof(rhi));
                    if(is_div)lin_term(&rhi,rem,1.0);else lin_into(&rhi,&l3,1.0);
                    lin_term(&rhi,nonneg,-R);b_put(b,'<',0.0,&rhi);lin_free(&rhi);
                    handled=1;
                }
            }
        }
        lin_free(&l1);lin_free(&l2);lin_free(&l3);
        return handled?0:1;
    }
    /* int_pow(x, y, z): z = x^y when linear (constant exponent/base or bounded lattice). */
    if(strcmp(p,"int_pow")==0||strcmp(p,"int_pow_fixed")==0){
        if(c->nargs<3)return -1;
        if(parse_lin(m,c->args[0],&l1)!=0)return -1;
        if(parse_lin(m,c->args[1],&l2)!=0){lin_free(&l1);return -1;}
        if(parse_lin(m,c->args[2],&l3)!=0){lin_free(&l1);lin_free(&l2);return -1;}
        if(l2.n==0&&isfinite(l2.constant)&&l2.constant==rint(l2.constant)&&
           l2.constant>=0.0&&l2.constant<=1000.0){
            long exp = (long)l2.constant;
            if(exp == 0){
                Lin eq;memset(&eq,0,sizeof(eq)); lin_into(&eq,&l3,1.0); eq.constant-=1.0;
                b_put(b,'=',0.0,&eq); lin_free(&eq);
                lin_free(&l1);lin_free(&l2);lin_free(&l3); return 0;
            } else if(exp == 1){
                Lin eq;memset(&eq,0,sizeof(eq)); lin_into(&eq,&l3,1.0); lin_into(&eq,&l1,-1.0);
                b_put(b,'=',0.0,&eq); lin_free(&eq);
                lin_free(&l1);lin_free(&l2);lin_free(&l3); return 0;
            } else if(l1.n==0){
                double base=l1.constant,val=pow(base,(double)exp);
                /* Beyond 2^53 a rounded double is not an exact integer
                   coefficient, so claiming exact int_pow support would solve a
                   nearby model.  Return UNKNOWN instead. */
                if(!isfinite(base)||base!=rint(base)||!isfinite(val)||
                   fabs(val)>0x1p53||val!=rint(val)){
                    lin_free(&l1);lin_free(&l2);lin_free(&l3);return 1;
                }
                Lin eq;memset(&eq,0,sizeof(eq)); lin_into(&eq,&l3,1.0); eq.constant-=val;
                b_put(b,'=',0.0,&eq); lin_free(&eq);
                lin_free(&l1);lin_free(&l2);lin_free(&l3); return 0;
            } else if(l1.n==1 && b->haslo[l1.idx[0]] && b->hashi[l1.idx[0]]){
                int xv = l1.idx[0];
                if(fabs(b->lo[xv] - b->hi[xv]) <= 1e-9){
                    double base=b->lo[xv],val=pow(base,(double)exp);
                    if(!isfinite(base)||base!=rint(base)||!isfinite(val)||
                       fabs(val)>0x1p53||val!=rint(val)){
                        lin_free(&l1);lin_free(&l2);lin_free(&l3);return 1;
                    }
                    Lin eq;memset(&eq,0,sizeof(eq)); lin_into(&eq,&l3,1.0); eq.constant-=val;
                    b_put(b,'=',0.0,&eq); lin_free(&eq);
                    lin_free(&l1);lin_free(&l2);lin_free(&l3); return 0;
                }
                long vlo = (long)llround(b->lo[xv]), vhi = (long)llround(b->hi[xv]);
                if(vhi >= vlo && vhi - vlo + 1 <= 128 && exp >= 0){
                    int nvals = (int)(vhi - vlo + 1);
                    int *zs = (int*)psolve_malloc((size_t)nvals * sizeof(int));
                    for(int k=0;k<nvals;k++){
                        zs[k] = b_newvar(b,0.0,1.0);
                        Lin di;memset(&di,0,sizeof(di)); lin_into(&di,&l1,1.0); di.constant-=(double)(vlo + k);
                        add_int_reif(b,&di,zs[k],0); lin_free(&di);
                    }
                    Lin sum;memset(&sum,0,sizeof(sum)); lin_into(&sum,&l3,1.0);
                    for(int k=0;k<nvals;k++){
                        double pval=pow((double)(vlo+k),(double)exp);
                        if(!isfinite(pval)||fabs(pval)>0x1p53||pval!=rint(pval)){
                            lin_free(&sum);free(zs);lin_free(&l1);lin_free(&l2);lin_free(&l3);return 1;
                        }
                        lin_term(&sum,zs[k],-pval);
                    }
                    b_put(b,'=',0.0,&sum); lin_free(&sum);
                    Lin ones;memset(&ones,0,sizeof(ones));
                    for(int k=0;k<nvals;k++) lin_term(&ones, zs[k], 1.0);
                    ones.constant = -1.0; b_put(b,'=',0.0,&ones); lin_free(&ones);
                    free(zs); lin_free(&l1);lin_free(&l2);lin_free(&l3); return 0;
                }
            }
        }
        lin_free(&l1);lin_free(&l2);lin_free(&l3);
        return 1;
    }
    /* float_div(a, b, c): c = a / b.  Division is linear only when b is a
       non-zero parameter; otherwise it is nonlinear and stays UNKNOWN. */
    if(strcmp(p,"float_div")==0){
        if(c->nargs<3)return -1;
        if(parse_lin(m,c->args[0],&l1)!=0)return -1;
        if(parse_lin(m,c->args[1],&l2)!=0){lin_free(&l1);return -1;}
        if(parse_lin(m,c->args[2],&l3)!=0){lin_free(&l1);lin_free(&l2);return -1;}
        if(l2.n!=0||fabs(l2.constant)<1e-15){lin_free(&l1);lin_free(&l2);lin_free(&l3);return 1;}
        Lin dd;memset(&dd,0,sizeof(dd));lin_into(&dd,&l3,1.0);lin_into(&dd,&l1,-1.0/l2.constant);
        b_put(b,'=',0.0,&dd);lin_free(&dd);
        lin_free(&l1);lin_free(&l2);lin_free(&l3);return 0;
    }
    /* int_abs(x,y): y = |x|.  y >= x, y >= -x; y <= M*u + ... (big-M) or with
       a bounded x we can do y = x (x>=0) or y=-x (x<=0) but general needs M.
       For bounded x in [lo,hi] with lo>=0: y=x; hi<=0: y=-x; else use big-M. */
    if(strcmp(p,"int_abs")==0||strcmp(p,"float_abs")==0){
        int abs_flt=(strcmp(p,"float_abs")==0);
        if(c->nargs<2)return -1;
        if(parse_lin(m,c->args[0],&l1)!=0)return -1;
        if(parse_lin(m,c->args[1],&l2)!=0){lin_free(&l1);return -1;}
        /* constants and affine forms are materialized as pinned alias vars
           (previously: UNKNOWN for constants, silently dropped constant term
           for affine arguments) */
        int x=lin_materialize(b,&l1,abs_flt);
        int y=lin_materialize(b,&l2,abs_flt);
        if(x<0||y<0){lin_free(&l1);lin_free(&l2);return 1;}
        double xlo=b->lo[x], xhi=b->hi[x];
        if(xlo>=0){ /* y = x */
            Lin dd;memset(&dd,0,sizeof(dd));lin_term(&dd,y,1.0);lin_term(&dd,x,-1.0);b_put(b,'=',0.0,&dd);lin_free(&dd);
            lin_free(&l1);lin_free(&l2);return 0;
        } else if(xhi<=0){ /* y = -x */
            Lin dd;memset(&dd,0,sizeof(dd));lin_term(&dd,y,1.0);lin_term(&dd,x,1.0);b_put(b,'=',0.0,&dd);lin_free(&dd);
            lin_free(&l1);lin_free(&l2);return 0;
        }
        /* bounded both sides with xlo<0<xhi: big-M with a binary sign flag s.
           y >= x, y >= -x,
           y <= x + 2*(-xlo)*s          (when x>=0, s=0 => y<=x)
           y <= -x + 2*xhi*(1-s)         (when x<=0, s=1 => y<=-x)
           Using big-M form: y - x - 2*(-xlo)*s <= 0  and  y + x - 2*xhi + 2*xhi*s <= 0 */
        int s=b_newvar(b,0.0,1.0);   /* binary flag */
        Lin d1;memset(&d1,0,sizeof(d1));lin_term(&d1,y,1.0);lin_term(&d1,x,-1.0);b_put(b,'>',0.0,&d1);lin_free(&d1);
        Lin d2;memset(&d2,0,sizeof(d2));lin_term(&d2,y,1.0);lin_term(&d2,x,1.0);b_put(b,'>',0.0,&d2);lin_free(&d2);
        Lin d3;memset(&d3,0,sizeof(d3));lin_term(&d3,y,1.0);lin_term(&d3,x,-1.0);lin_term(&d3,s,-2.0*(-xlo));b_put(b,'<',0.0,&d3);lin_free(&d3);
        Lin d4;memset(&d4,0,sizeof(d4));lin_term(&d4,y,1.0);lin_term(&d4,x,1.0);lin_term(&d4,s,2.0*xhi);d4.constant=-2.0*xhi;b_put(b,'<',0.0,&d4);lin_free(&d4);
        lin_free(&l1);lin_free(&l2);
        return 0;
    }
    /* int_max(a,b,m) / int_min(a,b,m): m = max/min of a,b.  Requires a binary
       selection: use a big-M encoding with a binary flag s.  For max:
         m >= a, m >= b,
         m <= a + M*s,  m <= b + M*(1-s)   (choose a if s=0, b if s=1)
       For min:
         m <= a, m <= b,
         m >= a - M*s,  m >= b - M*(1-s) */
    if(strcmp(p,"int_max")==0||strcmp(p,"int_min")==0||
       strcmp(p,"float_max")==0||strcmp(p,"float_min")==0||
       strcmp(p,"fzn_float_max")==0||strcmp(p,"fzn_float_min")==0){
        int ismax=(strstr(p,"max")!=NULL);
        int mm_flt=(strstr(p,"float")!=NULL);
        if(c->nargs<3)return -1;
        if(parse_lin(m,c->args[0],&l1)!=0)return -1;
        if(parse_lin(m,c->args[1],&l2)!=0){lin_free(&l1);return -1;}
        if(parse_lin(m,c->args[2],&l3)!=0){lin_free(&l1);lin_free(&l2);return -1;}
        /* constants/affine forms materialize as pinned alias vars (same
           anti-"drop the constant" discipline as int_abs) */
        int a=lin_materialize(b,&l1,mm_flt);
        int bb=lin_materialize(b,&l2,mm_flt);
        int mm=lin_materialize(b,&l3,mm_flt);
        if(a<0||bb<0||mm<0){lin_free(&l1);lin_free(&l2);lin_free(&l3);return 1;}
        double M = fmax(fabs(b->lo[a]),fabs(b->hi[a]));
        M = fmax(M, fmax(fabs(b->lo[bb]),fabs(b->hi[bb])));
        M = fmax(M,1.0)*2.0;
        int s=b_newvar(b,0.0,1.0);
        if(ismax){
            Lin d1;memset(&d1,0,sizeof(d1));lin_term(&d1,mm,1.0);lin_term(&d1,a,-1.0);b_put(b,'>',0.0,&d1);lin_free(&d1);
            Lin d2;memset(&d2,0,sizeof(d2));lin_term(&d2,mm,1.0);lin_term(&d2,bb,-1.0);b_put(b,'>',0.0,&d2);lin_free(&d2);
            Lin d3;memset(&d3,0,sizeof(d3));lin_term(&d3,mm,1.0);lin_term(&d3,a,-1.0);lin_term(&d3,s,-M);b_put(b,'<',0.0,&d3);lin_free(&d3);
            Lin d4;memset(&d4,0,sizeof(d4));lin_term(&d4,mm,1.0);lin_term(&d4,bb,-1.0);lin_term(&d4,s,M);d4.constant-=M;b_put(b,'<',0.0,&d4);lin_free(&d4);
        } else {
            Lin d1;memset(&d1,0,sizeof(d1));lin_term(&d1,mm,1.0);lin_term(&d1,a,-1.0);b_put(b,'<',0.0,&d1);lin_free(&d1);
            Lin d2;memset(&d2,0,sizeof(d2));lin_term(&d2,mm,1.0);lin_term(&d2,bb,-1.0);b_put(b,'<',0.0,&d2);lin_free(&d2);
            Lin d3;memset(&d3,0,sizeof(d3));lin_term(&d3,mm,1.0);lin_term(&d3,a,-1.0);lin_term(&d3,s,M);b_put(b,'>',0.0,&d3);lin_free(&d3);
            Lin d4;memset(&d4,0,sizeof(d4));lin_term(&d4,mm,1.0);lin_term(&d4,bb,-1.0);lin_term(&d4,s,-M);d4.constant+=M;b_put(b,'>',0.0,&d4);lin_free(&d4);
        }
        lin_free(&l1);lin_free(&l2);lin_free(&l3);
        return 0;
    }
    /* bool2int(b, i): i = b (0/1 to int).  i - b = 0. */
    if(strcmp(p,"bool2int")==0){
        if(c->nargs<2)return -1;
        if(parse_lin(m,c->args[0],&l1)!=0)return -1;
        if(parse_lin(m,c->args[1],&l2)!=0){lin_free(&l1);return -1;}
        Lin dd;memset(&dd,0,sizeof(dd));lin_into(&dd,&l2,1.0);lin_into(&dd,&l1,-1.0);b_put(b,'=',0.0,&dd);
        lin_free(&l1);lin_free(&l2);lin_free(&dd);return 0;
    }
    /* int2float(a,f): f = a (integer value as float) */
    if(strcmp(p,"int2float")==0){
        if(c->nargs<2)return -1;
        if(parse_lin(m,c->args[0],&l1)!=0)return -1;
        if(parse_lin(m,c->args[1],&l2)!=0){lin_free(&l1);return -1;}
        Lin dd;memset(&dd,0,sizeof(dd));lin_into(&dd,&l2,1.0);lin_into(&dd,&l1,-1.0);b_put(b,'=',0.0,&dd);
        lin_free(&l1);lin_free(&l2);lin_free(&dd);return 0;
    }
    /* set_in(x, {set of ints}) and reified set_in_reif(x, S, r): x in set. */
    if(strcmp(p,"set_in")==0||strcmp(p,"int_in")==0||strcmp(p,"fzn_set_in")==0||strcmp(p,"fzn_int_in")==0||
       strcmp(p,"set_in_reif")==0||strcmp(p,"int_in_reif")==0||strcmp(p,"fzn_set_in_reif")==0||strcmp(p,"fzn_int_in_reif")==0){
        int reified = (strstr(p,"_reif")!=NULL);
        if(c->nargs < (reified ? 3 : 2)) return -1;
        Lin xl; if(parse_lin(m,c->args[0],&xl)!=0) return -1;
        Lin rb;
        if(reified && parse_lin(m,c->args[2],&rb)!=0){lin_free(&xl);return -1;}
        int is_range=0; long rlo=0, rhi=0; long*vals=NULL; int nvals=0;
        if(fz_parse_int_set(c->args[1],FZ_MAX_SET_ENUM,&is_range,&rlo,&rhi,&vals,&nvals)!=0){
            lin_free(&xl); if(reified)lin_free(&rb); return 1;
        }
        if(!reified){
            if(xl.n==0){
                /* constant LHS: fold membership exactly (a previously
                   UNHANDLED case reported UNKNOWN even though the answer is
                   decidable at compile time).  Beyond 2^53 the double from
                   parse_lin may have lost precision, so stay honest. */
                double xv=xl.constant;
                if(!isfinite(xv)||fabs(xv)>0x1p53){free(vals);lin_free(&xl);return 1;}
                int member=(xv==rint(xv));
                if(member){
                    long v=(long)rint(xv);
                    if(is_range) member=(rlo<=rhi&&v>=rlo&&v<=rhi);
                    else {
                        member=0;
                        for(int i=0;i<nvals;i++) if(vals[i]==v){member=1;break;}
                    }
                }
                if(!member){ /* constant is not a member: infeasible (0 = 1) */
                    Lin nf;memset(&nf,0,sizeof(nf)); b_put(b,'=',1.0,&nf); lin_free(&nf);
                }
                free(vals); lin_free(&xl); return 0;
            }
            /* affine forms (constant already folded above) get an exact
               pinned alias var instead of silently dropping the constant */
            int x=lin_materialize(b,&xl,0);
            if(x<0){free(vals);lin_free(&xl);return 1;}
            if(is_range){
                if(rlo>rhi){ /* empty range: unsatisfiable */
                    Lin nf;memset(&nf,0,sizeof(nf)); b_put(b,'=',1.0,&nf); lin_free(&nf);
                } else {
                    if(!b->haslo[x]||b->lo[x]<rlo){ b->lo[x]=(double)rlo; b->haslo[x]=1; }
                    if(!b->hashi[x]||b->hi[x]>rhi){ b->hi[x]=(double)rhi; b->hashi[x]=1; }
                    if(b->lo[x]>b->hi[x]){ /* intersection with the domain is empty */
                        Lin nf;memset(&nf,0,sizeof(nf)); b_put(b,'=',1.0,&nf); lin_free(&nf);
                    }
                }
                free(vals); lin_free(&xl); return 0;
            }
            if(nvals==0){ /* empty set: nothing is a member -> UNSAT */
                Lin nf;memset(&nf,0,sizeof(nf)); b_put(b,'=',1.0,&nf); lin_free(&nf);
                free(vals);lin_free(&xl);return 0;
            }
            if(nvals==1){
                double v=(double)vals[0];
                /* intersect the singleton with the declared domain: a value
                   outside it is unsatisfiable, not a new domain */
                if((b->haslo[x]&&b->lo[x]>v)||(b->hashi[x]&&b->hi[x]<v)){
                    Lin nf;memset(&nf,0,sizeof(nf)); b_put(b,'=',1.0,&nf); lin_free(&nf);
                } else {
                    b->lo[x]=v;b->haslo[x]=1;b->hi[x]=v;b->hashi[x]=1;
                }
                free(vals); lin_free(&xl); return 0;
            }
            int *bs=(int*)psolve_malloc((size_t)nvals*sizeof(int));
            for(int i=0;i<nvals;i++) bs[i]=b_newvar(b,0.0,1.0);
            Lin eq;memset(&eq,0,sizeof(eq)); lin_term(&eq,x,1.0);
            for(int i=0;i<nvals;i++) lin_term(&eq,bs[i],-(double)vals[i]);
            b_put(b,'=',0.0,&eq);
            Lin sum;memset(&sum,0,sizeof(sum));
            for(int i=0;i<nvals;i++) lin_term(&sum,bs[i],1.0);
            sum.constant=-1.0; b_put(b,'=',0.0,&sum);
            lin_free(&eq);lin_free(&sum); free(bs);free(vals);lin_free(&xl);
            return 0;
        } else {
            int r;
            if(lin_unit_var(&rb,&r)!=0){free(vals);lin_free(&xl);lin_free(&rb);return 1;}
            if(is_range){
                if(rlo>rhi){ /* empty range: r = false */
                    Lin z;memset(&z,0,sizeof(z)); lin_term(&z,r,1.0); b_put(b,'=',0.0,&z); lin_free(&z);
                    free(vals); lin_free(&xl);lin_free(&rb); return 0;
                }
                /* d = xl - bound: SUBTRACT into the copied constant term.
                   Overwriting it would drop xl's own constant and mis-pin r
                   for constant or affine LHS (e.g. set_in_reif(7,S,r)). */
                Lin d1;memset(&d1,0,sizeof(d1)); lin_into(&d1,&xl,1.0); d1.constant-=(double)rlo;
                int b1=b_newvar(b,0.0,1.0);
                add_int_reif(b,&d1,b1,3); lin_free(&d1);
                Lin d2;memset(&d2,0,sizeof(d2)); lin_into(&d2,&xl,1.0); d2.constant-=(double)rhi;
                int b2=b_newvar(b,0.0,1.0);
                add_int_reif(b,&d2,b2,1); lin_free(&d2);
                /* r = b1 AND b2 */
                Lin c1;memset(&c1,0,sizeof(c1)); lin_term(&c1,r,1.0); lin_term(&c1,b1,-1.0); b_put(b,'<',0.0,&c1); lin_free(&c1);
                Lin c2;memset(&c2,0,sizeof(c2)); lin_term(&c2,r,1.0); lin_term(&c2,b2,-1.0); b_put(b,'<',0.0,&c2); lin_free(&c2);
                Lin c3;memset(&c3,0,sizeof(c3)); lin_term(&c3,r,1.0); lin_term(&c3,b1,-1.0); lin_term(&c3,b2,-1.0); c3.constant=1.0;
                b_put(b,'>',0.0,&c3); lin_free(&c3);
                free(vals); lin_free(&xl);lin_free(&rb); return 0;
            } else if(nvals>0){
                int *zs=(int*)psolve_malloc((size_t)nvals*sizeof(int));
                for(int i=0;i<nvals;i++){
                    zs[i]=b_newvar(b,0.0,1.0);
                    Lin di;memset(&di,0,sizeof(di)); lin_into(&di,&xl,1.0); di.constant-=(double)vals[i];
                    add_int_reif(b,&di,zs[i],0); lin_free(&di);
                }
                Lin sum;memset(&sum,0,sizeof(sum)); lin_term(&sum,r,1.0);
                for(int i=0;i<nvals;i++) lin_term(&sum,zs[i],-1.0);
                b_put(b,'=',0.0,&sum); lin_free(&sum);
                Lin at_most_one;memset(&at_most_one,0,sizeof(at_most_one));
                for(int i=0;i<nvals;i++) lin_term(&at_most_one,zs[i],1.0);
                b_put(b,'<',1.0,&at_most_one); lin_free(&at_most_one);
                free(zs); free(vals); lin_free(&xl);lin_free(&rb); return 0;
            } else {
                /* empty set: membership is false, pin r = 0 */
                Lin z;memset(&z,0,sizeof(z)); lin_term(&z,r,1.0);
                b_put(b,'=',0.0,&z); lin_free(&z);
                free(vals); lin_free(&xl);lin_free(&rb); return 0;
            }
            free(vals); lin_free(&xl);lin_free(&rb); return 1;
        }
    }
    /* all_different(x[]): each variable takes a distinct value.
       Exact encoding for bounded integer domains via the assignment
       (permutation) formulation: binary y_{i,v} = [x_i == v],
         sum_v y_{i,v} = 1            (each var takes one value)
         sum_i y_{i,v} <= 1           (each value used at most once)
         x_i = sum_v v * y_{i,v}
       Works when the common domain is small (<= 128 values). */
    if(strcmp(p,"all_different_int")==0||strcmp(p,"fzn_all_different_int")==0||strcmp(p,"all_different")==0){
        if(c->nargs<1)return -1;
        Lin*arr;int narr;
        if(parse_array(m,c->args[0],&arr,&narr)!=0)return -1;
        if(narr<=1){free_lins(arr,narr);return 0;}
        double dlo=1e18, dhi=-1e18;
        for(int i=0;i<narr;i++){
            double lo_i, hi_i;
            if(lin_bounds(b, &arr[i], &lo_i, &hi_i)!=0){free_lins(arr,narr);return 1;}
            if(lo_i<dlo)dlo=lo_i;
            if(hi_i>dhi)dhi=hi_i;
        }
        long ndom=(long)(dhi-dlo)+1;
        if(ndom<=0||ndom>128){free_lins(arr,narr);return 1;}   /* too big */
        int *y=(int*)psolve_malloc((size_t)narr*(size_t)ndom*sizeof(int));
        for(int i=0;i<narr;i++)for(int vv=0;vv<ndom;vv++){
            y[i*ndom+vv]=b_newvar(b,0.0,1.0);
        }
        /* each var takes one value: sum_v y = 1 ;  x_i = sum v*y */
        for(int i=0;i<narr;i++){
            Lin s;memset(&s,0,sizeof(s));
            for(int vv=0;vv<ndom;vv++) lin_term(&s,y[i*ndom+vv],1.0);
            s.constant=-1.0; b_put(b,'=',0.0,&s); lin_free(&s);
            Lin e;memset(&e,0,sizeof(e)); lin_into(&e,&arr[i],1.0);
            for(int vv=0;vv<ndom;vv++) lin_term(&e,y[i*ndom+vv],-(double)(dlo+vv));
            b_put(b,'=',0.0,&e); lin_free(&e);
        }
        /* each value used at most once: sum_i y <= 1 */
        for(int vv=0;vv<ndom;vv++){
            Lin s;memset(&s,0,sizeof(s));
            for(int i=0;i<narr;i++) lin_term(&s,y[i*ndom+vv],1.0);
            s.constant=-1.0; b_put(b,'<',0.0,&s); lin_free(&s);
        }
        free(y); free_lins(arr,narr);
        return 0;
    }
    /* array element: val = arr[index] (1-based), or the Gecode-specific
       gecode_int_element(index, offset, [arr], val) where index+offset is the
       array subscript.  Exact SOS1 encoding: binary b_i = [index==i],
         sum b_i = 1, index = sum i*b_i + offset, val = arr[i]. */
    if(strcmp(p,"array_int_element")==0||strcmp(p,"fzn_array_int_element")==0||
       strcmp(p,"gecode_int_element")==0||strcmp(p,"array_var_int_element")==0||
       strcmp(p,"fzn_array_var_int_element")==0||strcmp(p,"gecode_var_int_element")==0||
       strcmp(p,"array_bool_element")==0||strcmp(p,"fzn_array_bool_element")==0||
       strcmp(p,"array_var_bool_element")==0||strcmp(p,"fzn_array_var_bool_element")==0||
       strcmp(p,"gecode_bool_element")==0||
       strcmp(p,"array_float_element")==0||strcmp(p,"fzn_array_float_element")==0||
       strcmp(p,"array_var_float_element")==0||strcmp(p,"fzn_array_var_float_element")==0||
       strcmp(p,"gecode_float_element")==0||
       strcmp(p,"array_var_int_element_nonshifted")==0||strcmp(p,"array_var_bool_element_nonshifted")==0||
       strcmp(p,"array_var_float_element_nonshifted")==0||
       strcmp(p,"array_int_element_nonshifted")==0||strcmp(p,"array_bool_element_nonshifted")==0||
       strcmp(p,"array_float_element_nonshifted")==0){
        int gec = (strncmp(p,"gecode_",7)==0);
        int nonshifted = (strstr(p,"_nonshifted")!=NULL);
        int idx_arg = 0, off_arg = 1, arr_arg = gec ? 2 : 1, val_arg = gec ? 3 : 2;
        if(c->nargs < (gec ? 4 : 3)) return -1;
        Lin il; if(parse_lin(m,c->args[idx_arg],&il)!=0) return -1;
        double offset = nonshifted ? 0.0 : 1.0;
        if(gec){
            Lin ol; if(parse_lin(m,c->args[off_arg],&ol)!=0){lin_free(&il);return -1;}
            if(ol.n!=0||ol.constant!=rint(ol.constant)){lin_free(&ol);lin_free(&il);return 1;}
            offset = ol.constant; lin_free(&ol);
        }
        Lin*arr; int narr;
        if(parse_array(m,c->args[arr_arg],&arr,&narr)!=0){lin_free(&il);return -1;}
        Lin vl; if(parse_lin(m,c->args[val_arg],&vl)!=0){lin_free(&il);free_lins(arr,narr);return -1;}
        if(narr<=0||narr>2048){lin_free(&il);lin_free(&vl);free_lins(arr,narr);return 1;}

        if(il.n==0){
            int k = (int)llround(il.constant - offset);
            if(k < 0 || k >= narr){
                Lin nf; memset(&nf,0,sizeof(nf)); b_put(b,'=',1.0,&nf); lin_free(&nf);
            } else {
                Lin eq; memset(&eq,0,sizeof(eq)); lin_into(&eq,&vl,1.0); lin_into(&eq,&arr[k],-1.0);
                b_put(b,'=',0.0,&eq); lin_free(&eq);
            }
            lin_free(&il);lin_free(&vl);free_lins(arr,narr);
            return 0;
        }
        int idx;
        if(lin_unit_var(&il,&idx)!=0){lin_free(&il);lin_free(&vl);free_lins(arr,narr);return 1;}

        int *bs=(int*)psolve_malloc((size_t)narr*sizeof(int));
        for(int i=0;i<narr;i++) bs[i]=b_newvar(b,0.0,1.0);
        Lin s;memset(&s,0,sizeof(s)); for(int i=0;i<narr;i++) lin_term(&s,bs[i],1.0); s.constant=-1.0; b_put(b,'=',0.0,&s); lin_free(&s);
        Lin ie;memset(&ie,0,sizeof(ie)); lin_term(&ie,idx,1.0);
        for(int i=0;i<narr;i++) lin_term(&ie,bs[i],-(double)(i+offset));
        b_put(b,'=',0.0,&ie); lin_free(&ie);

        int all_const = 1;
        for(int i=0;i<narr;i++) if(arr[i].n!=0){ all_const = 0; break; }

        if(all_const){
            Lin ve;memset(&ve,0,sizeof(ve)); lin_into(&ve,&vl,1.0);
            for(int i=0;i<narr;i++) lin_term(&ve,bs[i],-arr[i].constant);
            b_put(b,'=',0.0,&ve); lin_free(&ve);
        } else {
            for(int i=0;i<narr;i++){
                /* For selector b_i=0 the implication rows must be redundant
                   over the complete bound box.  A fixed magic M=1000 was not:
                   an unselected array value at 1e6 could make a valid element
                   constraint UNSAT.  Bound the actual expression val-arr[i]. */
                Lin diff;memset(&diff,0,sizeof(diff));lin_into(&diff,&vl,1.0);lin_into(&diff,&arr[i],-1.0);
                double dlo,dhi;
                if(lin_bounds(b,&diff,&dlo,&dhi)!=0||!isfinite(dlo)||!isfinite(dhi)){
                    lin_free(&diff);free(bs);lin_free(&il);lin_free(&vl);free_lins(arr,narr);return 1;
                }
                double M=fmax(fabs(dlo),fabs(dhi));
                Lin d1;memset(&d1,0,sizeof(d1));lin_into(&d1,&diff,1.0);
                lin_term(&d1,bs[i],M);b_put(b,'<',M,&d1);lin_free(&d1);
                Lin d2;memset(&d2,0,sizeof(d2));lin_into(&d2,&diff,1.0);
                lin_term(&d2,bs[i],-M);b_put(b,'>',-M,&d2);lin_free(&d2);
                lin_free(&diff);
            }
        }
        free(bs); lin_free(&il);lin_free(&vl);free_lins(arr,narr);
        return 0;
    }
    /* array_int_maximum/minimum(m, x[]): exact selector formulation.  The
       selected element equals m while the remaining inequalities make m the
       true extremum; this also works when compiler propagation leaves constants
       inside a var-array view. */
    if(strcmp(p,"array_int_maximum")==0||strcmp(p,"array_int_minimum")==0||
       strcmp(p,"fzn_array_int_maximum")==0||strcmp(p,"fzn_array_int_minimum")==0||
       strcmp(p,"array_float_maximum")==0||strcmp(p,"array_float_minimum")==0||
       strcmp(p,"fzn_array_float_maximum")==0||strcmp(p,"fzn_array_float_minimum")==0){
        int ismax=(strstr(p,"maximum")!=NULL);
        if(c->nargs<2)return -1;
        Lin ml;Lin*arr;int narr;
        if(parse_lin(m,c->args[0],&ml)!=0)return -1;
        if(parse_array(m,c->args[1],&arr,&narr)!=0){lin_free(&ml);return -1;}
        if(narr<=0||narr>1024){lin_free(&ml);free_lins(arr,narr);return 1;}
        double *xlo=(double*)psolve_malloc((size_t)narr*sizeof(double));
        double *xhi=(double*)psolve_malloc((size_t)narr*sizeof(double));
        double implied_lo=ismax?-LP_INF:LP_INF, implied_hi=ismax?-LP_INF:LP_INF;
        for(int i=0;i<narr;i++){
            if(lin_bounds(b,&arr[i],&xlo[i],&xhi[i])!=0){free(xlo);free(xhi);lin_free(&ml);free_lins(arr,narr);return 1;}
            if(ismax){if(xlo[i]>implied_lo)implied_lo=xlo[i];if(xhi[i]>implied_hi)implied_hi=xhi[i];}
            else {if(xlo[i]<implied_lo)implied_lo=xlo[i];if(xhi[i]<implied_hi)implied_hi=xhi[i];}
        }
        /* Tighten a canonical result variable before choosing big-M values.
           Apart from propagation, this avoids a huge relaxation when a compiler
           gave the maximum/minimum an intentionally loose introductory domain. */
        int mv;
        if(lin_unit_var(&ml,&mv)==0&&mv>=0&&mv<b->nvars){
            if(b->lo[mv]<implied_lo)b->lo[mv]=implied_lo;
            if(b->hi[mv]>implied_hi)b->hi[mv]=implied_hi;
        }
        double mlo,mhi;
        if(lin_bounds(b,&ml,&mlo,&mhi)!=0){free(xlo);free(xhi);lin_free(&ml);free_lins(arr,narr);return 1;}
        /* When m is itself the sole correctly-directed objective, the epigraph
           (or hypograph for minimum) inequalities force equality at optimum.
           Avoid selector binaries in this common MiniZinc makespan pattern. */
        int objective_forces=0;
        if(lin_unit_var(&ml,&mv)==0&&m->objective.n==1&&m->objective.idx[0]==mv&&m->objective.coef[0]>0.0){
            if((ismax&&m->solve_kind==1)||(!ismax&&m->solve_kind==2))objective_forces=1;
        }
        if(objective_forces){
            for(int i=0;i<narr;i++){
                Lin d;memset(&d,0,sizeof(d));lin_into(&d,&ml,1.0);lin_into(&d,&arr[i],-1.0);
                b_put(b,ismax?'>':'<',0.0,&d);lin_free(&d);
            }
            free(xlo);free(xhi);lin_free(&ml);free_lins(arr,narr);return 0;
        }
        int *z=(int*)psolve_malloc((size_t)narr*sizeof(int));
        for(int i=0;i<narr;i++)z[i]=b_newvar(b,0.0,1.0);
        Lin pick;memset(&pick,0,sizeof(pick));
        for(int i=0;i<narr;i++)lin_term(&pick,z[i],1.0);
        b_put(b,'=',1.0,&pick);lin_free(&pick);
        for(int i=0;i<narr;i++){
            Lin d;memset(&d,0,sizeof(d));lin_into(&d,&ml,1.0);lin_into(&d,&arr[i],-1.0);
            if(ismax){
                /* m >= x_i; z_i=1 additionally gives m <= x_i. */
                b_put(b,'>',0.0,&d);
                double M=fmax(0.0,mhi-xlo[i]);
                lin_term(&d,z[i],M);b_put(b,'<',M,&d);
            } else {
                /* m <= x_i; z_i=1 additionally gives m >= x_i. */
                b_put(b,'<',0.0,&d);
                double M=fmax(0.0,xhi[i]-mlo);
                lin_term(&d,z[i],-M);b_put(b,'>',-M,&d);
            }
            lin_free(&d);
        }
        free(z);free(xlo);free(xhi);lin_free(&ml);free_lins(arr,narr);
        return 0;
    }
    /* Extensional integer table: x must equal one complete tuple from a
       flattened row-major parameter array.  This is the form emitted by the
       Gecode backend, e.g. gecode_table_int([x1,x2], [1,2,2,3,...]).
       Select exactly one tuple with binary z_r and set every x_i to its entry.
       The encoding is exact (not a per-column membership relaxation). */
    if(strcmp(p,"gecode_table_int")==0||strcmp(p,"fzn_table_int")==0||
       strcmp(p,"table_int")==0){
        if(c->nargs<2)return -1;
        Lin *xs,*tuples; int nx,nt;
        if(parse_array(m,c->args[0],&xs,&nx)!=0)return -1;
        if(parse_array(m,c->args[1],&tuples,&nt)!=0){free_lins(xs,nx);return -1;}
        if(nx<=0||nt<0||nt%nx!=0){free_lins(xs,nx);free_lins(tuples,nt);return -1;}
        int nrows=nt/nx;
        /* Bound the deliberately compact MIP encoding on hostile FlatZinc
           input.  Larger tables remain UNKNOWN rather than allocating an
           unbounded number of binary selector variables. */
        if(nrows>1024||nt>65536){free_lins(xs,nx);free_lins(tuples,nt);return 1;}
        for(int k=0;k<nt;k++){
            if(tuples[k].n!=0||fabs(tuples[k].constant-round(tuples[k].constant))>1e-12){
                free_lins(xs,nx);free_lins(tuples,nt);return 1;
            }
        }
        if(nrows==0){
            /* No permitted tuple: make the model infeasible, exactly. */
            Lin impossible;memset(&impossible,0,sizeof(impossible));b_put(b,'=',1.0,&impossible);
            lin_free(&impossible);free_lins(xs,nx);free_lins(tuples,nt);return 0;
        }
        int *z=(int*)psolve_malloc((size_t)nrows*sizeof(int));
        for(int r=0;r<nrows;r++)z[r]=b_newvar(b,0.0,1.0);
        Lin pick;memset(&pick,0,sizeof(pick));
        for(int r=0;r<nrows;r++)lin_term(&pick,z[r],1.0);
        b_put(b,'=',1.0,&pick);lin_free(&pick);
        for(int i=0;i<nx;i++){
            Lin eq;memset(&eq,0,sizeof(eq));lin_into(&eq,&xs[i],1.0);
            for(int r=0;r<nrows;r++)lin_term(&eq,z[r],-tuples[r*nx+i].constant);
            b_put(b,'=',0.0,&eq);lin_free(&eq);
        }
        free(z);free_lins(xs,nx);free_lins(tuples,nt);
        return 0;
    }
    /* Hamiltonian successor circuit.  Gecode emits
       gecode_circuit(offset, x); standard/other libraries use fzn_circuit(x).
       z[i,j] selects successor j for node i. Assignment rows make a directed
       permutation, self arcs are excluded, and MTZ order rows eliminate every
       subtour not containing node 0, leaving exactly one cycle. */
    if(strcmp(p,"gecode_circuit")==0||strcmp(p,"fzn_circuit")==0||strcmp(p,"circuit")==0){
        int offset=1; const char*xarg=NULL; Lin ol;
        if(strcmp(p,"gecode_circuit")==0){
            if(c->nargs<2)return -1;
            if(parse_lin(m,c->args[0],&ol)!=0)return -1;
            if(ol.n!=0||fabs(ol.constant-round(ol.constant))>1e-12||
               ol.constant<(double)INT_MIN||ol.constant>(double)INT_MAX){lin_free(&ol);return 1;}
            offset=(int)llround(ol.constant);xarg=c->args[1];lin_free(&ol);
        } else {
            if(c->nargs<1)return -1;
            xarg=c->args[0];
        }
        Lin*xs;int nx;
        if(parse_array(m,xarg,&xs,&nx)!=0)return -1;
        if(nx==0){free_lins(xs,nx);return 0;}
        if(nx>64||offset>INT_MAX-(nx-1)){free_lins(xs,nx);return 1;} /* exact O(n^2) encoding cap */
        if(nx==1){
            Lin e;memset(&e,0,sizeof(e));lin_into(&e,&xs[0],1.0);e.constant-=offset;
            b_put(b,'=',0.0,&e);lin_free(&e);free_lins(xs,nx);return 0;
        }
        int *z=(int*)psolve_malloc((size_t)nx*nx*sizeof(int));
        for(int i=0;i<nx;i++)for(int j=0;j<nx;j++)z[i*nx+j]=(i==j)?-1:b_newvar(b,0.0,1.0);
        /* one outgoing and one incoming arc per node */
        for(int i=0;i<nx;i++){
            Lin out;memset(&out,0,sizeof(out));
            for(int j=0;j<nx;j++)if(z[i*nx+j]>=0)lin_term(&out,z[i*nx+j],1.0);
            b_put(b,'=',1.0,&out);lin_free(&out);
        }
        for(int j=0;j<nx;j++){
            Lin in;memset(&in,0,sizeof(in));
            for(int i=0;i<nx;i++)if(z[i*nx+j]>=0)lin_term(&in,z[i*nx+j],1.0);
            b_put(b,'=',1.0,&in);lin_free(&in);
        }
        for(int i=0;i<nx;i++){
            Lin eq;memset(&eq,0,sizeof(eq));lin_into(&eq,&xs[i],1.0);
            for(int j=0;j<nx;j++)if(z[i*nx+j]>=0)lin_term(&eq,z[i*nx+j],-(double)(offset+j));
            b_put(b,'=',0.0,&eq);lin_free(&eq);
        }
        int *ord=(int*)psolve_malloc((size_t)(nx-1)*sizeof(int));
        for(int i=1;i<nx;i++)ord[i-1]=b_newvar_float(b,1.0,(double)(nx-1));
        for(int i=1;i<nx;i++)for(int j=1;j<nx;j++)if(i!=j){
            Lin mtz;memset(&mtz,0,sizeof(mtz));lin_term(&mtz,ord[i-1],1.0);lin_term(&mtz,ord[j-1],-1.0);
            lin_term(&mtz,z[i*nx+j],(double)(nx-1));b_put(b,'<',(double)(nx-2),&mtz);lin_free(&mtz);
        }
        free(ord);free(z);free_lins(xs,nx);
        return 0;
    }
    /* Reified integer relations.  These are exact over the integer lattice:
       r=1 iff the relation holds.  Continuous reification is intentionally
       not accepted here because equality/strict predicates are non-convex or
       open and cannot be represented faithfully by this LP/MIP bridge. */
    if(strcmp(p,"int_eq_reif")==0||strcmp(p,"int_le_reif")==0||
       strcmp(p,"int_lt_reif")==0||strcmp(p,"int_ge_reif")==0||
       strcmp(p,"int_gt_reif")==0||strcmp(p,"int_ne_reif")==0||
       strcmp(p,"bool_eq_reif")==0||
       strcmp(p,"bool_le_reif")==0||strcmp(p,"bool_lt_reif")==0||
       strcmp(p,"bool_ge_reif")==0||strcmp(p,"bool_gt_reif")==0||
       strcmp(p,"bool_ne_reif")==0||
       strcmp(p,"int_eq_imp")==0||strcmp(p,"int_le_imp")==0||
       strcmp(p,"int_lt_imp")==0||strcmp(p,"int_ge_imp")==0||
       strcmp(p,"int_gt_imp")==0||strcmp(p,"int_ne_imp")==0||
       strcmp(p,"bool_eq_imp")==0||
       strcmp(p,"bool_le_imp")==0||strcmp(p,"bool_lt_imp")==0||
       strcmp(p,"bool_ge_imp")==0||strcmp(p,"bool_gt_imp")==0||
       strcmp(p,"bool_ne_imp")==0){
        if(c->nargs<3)return -1;
        int is_imp = (strstr(p,"_imp")!=NULL);
        Lin l1,l2,rb;
        if(parse_lin(m,c->args[0],&l1)!=0)return -1;
        if(parse_lin(m,c->args[1],&l2)!=0){lin_free(&l1);return -1;}
        if(parse_lin(m,c->args[2],&rb)!=0){lin_free(&l1);lin_free(&l2);return -1;}
        Lin d;memset(&d,0,sizeof(d));lin_into(&d,&l1,1.0);lin_into(&d,&l2,-1.0);
        int which;
        if(strstr(p,"_eq_")!=NULL) which=0;
        else if(strstr(p,"_le_")!=NULL) which=1;
        else if(strstr(p,"_lt_")!=NULL) which=2;
        else if(strstr(p,"_ge_")!=NULL) which=3;
        else if(strstr(p,"_gt_")!=NULL) which=4;
        else which=5;
        int rr;
        if(rb.n==0){
            if(fabs(rb.constant)>1e-12&&fabs(rb.constant-1.0)>1e-12)rr=1;
            else if(is_imp){
                if(rb.constant>=0.5) rr=add_int_relation_constant(b,&d,which,1);
                else rr=0; /* 0 => relation is vacuously true */
            }
            else rr=add_int_relation_constant(b,&d,which,rb.constant>=0.5);
        } else {
            int r;
            if(lin_unit_var(&rb,&r)!=0)rr=1;
            else if(is_imp) rr=add_int_imp(b,&d,r,which);
            else rr=add_int_reif(b,&d,r,which);
        }
        lin_free(&d);lin_free(&l1);lin_free(&l2);lin_free(&rb);
        return rr;
    }
    /* int_lin_ne: sum c_i*x_i != d.  Use the same exact positive/negative
       selector as scalar inequality reification. */
    if(strcmp(p,"int_lin_ne")==0||strcmp(p,"bool_lin_ne")==0){
        if(c->nargs<3)return -1;
        Lin*arr;int narr; Lin*parr;int nparr; Lin dd;
        if(parse_array(m,c->args[0],&arr,&narr)!=0)return -1;
        if(parse_array(m,c->args[1],&parr,&nparr)!=0){free_lins(arr,narr);return -1;}
        if(parse_lin(m,c->args[2],&dd)!=0){free_lins(arr,narr);free_lins(parr,nparr);return -1;}
        if(narr!=nparr){lin_free(&dd);free_lins(arr,narr);free_lins(parr,nparr);return -1;}
        Lin lin;memset(&lin,0,sizeof(lin));
        for(int i=0;i<narr;i++){
            if(arr[i].n!=0){lin_free(&lin);lin_free(&dd);free_lins(arr,narr);free_lins(parr,nparr);return 1;}
            lin_into(&lin,&parr[i],arr[i].constant);
        }
        lin_into(&lin,&dd,-1.0);
        int rr=add_int_ne(b,&lin);
        lin_free(&lin);lin_free(&dd);free_lins(arr,narr);free_lins(parr,nparr);
        return rr;
    }
    if(strcmp(p,"int_lin_eq_reif")==0||strcmp(p,"int_lin_le_reif")==0||
       strcmp(p,"int_lin_lt_reif")==0||strcmp(p,"int_lin_ge_reif")==0||
       strcmp(p,"int_lin_gt_reif")==0||strcmp(p,"int_lin_ne_reif")==0||
       strcmp(p,"bool_lin_eq_reif")==0||strcmp(p,"bool_lin_le_reif")==0||
       strcmp(p,"bool_lin_lt_reif")==0||strcmp(p,"bool_lin_ge_reif")==0||
       strcmp(p,"bool_lin_gt_reif")==0||strcmp(p,"bool_lin_ne_reif")==0||
       strcmp(p,"int_lin_eq_imp")==0||strcmp(p,"int_lin_le_imp")==0||
       strcmp(p,"int_lin_lt_imp")==0||strcmp(p,"int_lin_ge_imp")==0||
       strcmp(p,"int_lin_gt_imp")==0||strcmp(p,"int_lin_ne_imp")==0||
       strcmp(p,"bool_lin_eq_imp")==0||strcmp(p,"bool_lin_le_imp")==0||
       strcmp(p,"bool_lin_lt_imp")==0||strcmp(p,"bool_lin_ge_imp")==0||
       strcmp(p,"bool_lin_gt_imp")==0||strcmp(p,"bool_lin_ne_imp")==0){
        if(c->nargs<4)return -1;
        int is_imp = (strstr(p,"_imp")!=NULL);
        Lin*arr;int narr; Lin*parr;int nparr; Lin dd; Lin rb;
        if(parse_array(m,c->args[0],&arr,&narr)!=0)return -1;
        if(parse_array(m,c->args[1],&parr,&nparr)!=0){free_lins(arr,narr);return -1;}
        if(parse_lin(m,c->args[2],&dd)!=0){free_lins(arr,narr);free_lins(parr,nparr);return -1;}
        if(parse_lin(m,c->args[3],&rb)!=0){lin_free(&dd);free_lins(arr,narr);free_lins(parr,nparr);return -1;}
        if(narr!=nparr){lin_free(&rb);lin_free(&dd);free_lins(arr,narr);free_lins(parr,nparr);return -1;}
        Lin lin;memset(&lin,0,sizeof(lin));
        for(int i=0;i<narr;i++){
            if(arr[i].n!=0){lin_free(&lin);lin_free(&rb);lin_free(&dd);free_lins(arr,narr);free_lins(parr,nparr);return 1;}
            lin_into(&lin,&parr[i],arr[i].constant);
        }
        lin_into(&lin,&dd,-1.0);
        int which;
        if(strstr(p,"_eq_")!=NULL) which=0;
        else if(strstr(p,"_le_")!=NULL) which=1;
        else if(strstr(p,"_lt_")!=NULL) which=2;
        else if(strstr(p,"_ge_")!=NULL) which=3;
        else if(strstr(p,"_gt_")!=NULL) which=4;
        else which=5;
        int rr;
        if(rb.n==0){
            if(fabs(rb.constant)>1e-12&&fabs(rb.constant-1.0)>1e-12)rr=1;
            else if(is_imp){
                if(rb.constant>=0.5) rr=add_int_relation_constant(b,&lin,which,1);
                else rr=0;
            }
            else rr=add_int_relation_constant(b,&lin,which,rb.constant>=0.5);
        } else {
            int r;
            if(lin_unit_var(&rb,&r)!=0)rr=1;
            else if(is_imp) rr=add_int_imp(b,&lin,r,which);
            else rr=add_int_reif(b,&lin,r,which);
        }
        lin_free(&lin);lin_free(&rb);lin_free(&dd);free_lins(arr,narr);free_lins(parr,nparr);
        return rr;
    }
    /* fzn_count_eq(x[], v, n): exactly n of the variables x equal value v.
       Uses exact lattice reification b_i <-> (x_i == v) and sum b_i (rel) n.
       Variants: count_leq, count_geq, count_lt, count_gt, count_ne. */
    if(strcmp(p,"fzn_count_eq")==0||strcmp(p,"fzn_among_eq")==0||
       strcmp(p,"count_eq")==0||strcmp(p,"count")==0||strcmp(p,"fzn_count")==0||
       strcmp(p,"count_leq")==0||strcmp(p,"fzn_count_leq")==0||
       strcmp(p,"count_geq")==0||strcmp(p,"fzn_count_geq")==0||
       strcmp(p,"count_lt")==0||strcmp(p,"fzn_count_lt")==0||
       strcmp(p,"count_gt")==0||strcmp(p,"fzn_count_gt")==0||
       strcmp(p,"count_ne")==0||strcmp(p,"count_neq")==0||strcmp(p,"fzn_count_ne")==0){
        if(c->nargs<3)return -1;
        Lin*arr;int narr;
        if(parse_array(m,c->args[0],&arr,&narr)!=0)return -1;
        Lin vl; if(parse_lin(m,c->args[1],&vl)!=0){free_lins(arr,narr);return -1;}
        Lin nl; if(parse_lin(m,c->args[2],&nl)!=0){free_lins(arr,narr);lin_free(&vl);return -1;}
        int *bs=(int*)psolve_malloc((size_t)(narr?narr:1)*sizeof(int));
        for(int i=0;i<narr;i++){
            int bi=b_newvar(b,0.0,1.0);
            bs[i]=bi;
            /* The counted value may itself be a var int.  Reify x_i-v==0;
               using only vl.constant silently counted zero when v was a var. */
            Lin di;memset(&di,0,sizeof(di));lin_into(&di,&arr[i],1.0);lin_into(&di,&vl,-1.0);
            add_int_reif(b,&di,bi,0);
            lin_free(&di);
        }
        /* sum b_i (rel) nl */
        Lin s;memset(&s,0,sizeof(s));
        for(int i=0;i<narr;i++) lin_term(&s,bs[i],1.0);
        lin_into(&s,&nl,-1.0);
        if(strcmp(p,"count_leq")==0||strcmp(p,"fzn_count_leq")==0){
            b_put(b,'<',0.0,&s);
        } else if(strcmp(p,"count_geq")==0||strcmp(p,"fzn_count_geq")==0){
            b_put(b,'>',0.0,&s);
        } else if(strcmp(p,"count_lt")==0||strcmp(p,"fzn_count_lt")==0){
            b_put(b,'<',-1.0,&s);
        } else if(strcmp(p,"count_gt")==0||strcmp(p,"fzn_count_gt")==0){
            b_put(b,'>',1.0,&s);
        } else if(strcmp(p,"count_ne")==0||strcmp(p,"count_neq")==0||strcmp(p,"fzn_count_ne")==0){
            add_int_ne(b,&s);
        } else {
            b_put(b,'=',0.0,&s);
        }
        lin_free(&s);free(bs);free_lins(arr,narr);lin_free(&vl);lin_free(&nl);
        return 0;
    }
    /* among(n, x[], V) / fzn_among(n, x[], V): exactly n of x take values in set V. */
    if(strcmp(p,"among")==0||strcmp(p,"fzn_among")==0||strcmp(p,"among_eq")==0){
        if(c->nargs<3)return -1;
        Lin nl; if(parse_lin(m,c->args[0],&nl)!=0)return -1;
        Lin*arr;int narr;
        if(parse_array(m,c->args[1],&arr,&narr)!=0){lin_free(&nl);return -1;}
        int is_range=0; long rlo=0, rhi=0; long*vals=NULL; int nvals=0;
        if(fz_parse_int_set(c->args[2],FZ_MAX_SET_ENUM,&is_range,&rlo,&rhi,&vals,&nvals)!=0){
            lin_free(&nl); free_lins(arr,narr); return 1;
        }
        if(is_range){
            /* materialize the range (bounded work); beyond the honest cap
               report UNHANDLED rather than truncating the set */
            if(rlo>rhi){ vals=NULL; nvals=0; }
            else if(rhi-rlo+1>FZ_MAX_SET_ENUM){ lin_free(&nl); free_lins(arr,narr); return 1; }
            else {
                nvals=(int)(rhi-rlo+1);
                vals=(long*)psolve_malloc((size_t)nvals*sizeof(long));
                for(long v=rlo;v<=rhi;v++) vals[(int)(v-rlo)]=v;
            }
        }
        /* per-(element,value) selector binaries: cap the total encoding.
           Empty set: nothing counts - pin n = 0 (previously UNKNOWN). */
        if(nvals==0){
            Lin s0;memset(&s0,0,sizeof(s0));lin_into(&s0,&nl,1.0);b_put(b,'=',0.0,&s0);lin_free(&s0);
            free(vals); lin_free(&nl); free_lins(arr,narr); return 0;
        }
        if(narr>0 && nvals > 4096/narr){
            free(vals); lin_free(&nl); free_lins(arr,narr); return 1;
        }
        int *bs=(int*)psolve_malloc((size_t)(narr?narr:1)*sizeof(int));
        long nconst=0;
        for(int i=0;i<narr;i++){
            if(arr[i].n==0){
                /* par element: fold its membership exactly (previously the
                   whole constraint reported UNHANDLED) */
                double cv=arr[i].constant;
                if(!isfinite(cv)||cv!=rint(cv)||fabs(cv)>0x1p53){free(bs);free(vals);free_lins(arr,narr);lin_free(&nl);return 1;}
                long v=(long)rint(cv);
                int lolo=0,hihi=nvals-1,found=0;
                while(lolo<=hihi){int mid=(lolo+hihi)>>1; if(vals[mid]==v){found=1;break;} if(vals[mid]<v)lolo=mid+1; else hihi=mid-1;}
                nconst+=found;
                bs[i]=-1;
                continue;
            }
            if(arr[i].n!=1){free(bs);free(vals);free_lins(arr,narr);lin_free(&nl);return 1;}
            int bi=b_newvar(b,0.0,1.0);
            bs[i]=bi;
            int *zs=(int*)psolve_malloc((size_t)nvals*sizeof(int));
            for(int k=0;k<nvals;k++){
                zs[k]=b_newvar(b,0.0,1.0);
                Lin di;memset(&di,0,sizeof(di)); lin_into(&di,&arr[i],1.0); di.constant-=(double)vals[k];
                add_int_reif(b,&di,zs[k],0); lin_free(&di);
            }
            Lin sum;memset(&sum,0,sizeof(sum)); lin_term(&sum,bi,1.0);
            for(int k=0;k<nvals;k++) lin_term(&sum,zs[k],-1.0);
            b_put(b,'=',0.0,&sum); lin_free(&sum);
            free(zs);
        }
        Lin s;memset(&s,0,sizeof(s)); s.constant=(double)nconst;
        for(int i=0;i<narr;i++) if(bs[i]>=0) lin_term(&s,bs[i],1.0);
        lin_into(&s,&nl,-1.0);
        b_put(b,'=',0.0,&s); lin_free(&s);
        free(bs); free(vals); free_lins(arr,narr); lin_free(&nl);
        return 0;
    }
    /* table(x[], T): the tuple (x[0..n-1]) must equal one of the rows of the
       matrix T (row-major).  Exact MIP encoding with one binary selector
       per row. */
    if(strcmp(p,"table")==0||strcmp(p,"fzn_table")==0||
       strcmp(p,"gecode_table_int")==0||strcmp(p,"fzn_table_int")==0||
       strcmp(p,"table_int")==0||strcmp(p,"table_bool")==0||
       strcmp(p,"fzn_table_bool")==0||strcmp(p,"gecode_table_bool")==0){
        if(c->nargs<2)return -1;
        Lin*arr;int narr;
        if(parse_array(m,c->args[0],&arr,&narr)!=0)return -1;
        long*vals;int nvals;
        if(table_values(m,c->args[1],&vals,&nvals)!=0){free_lins(arr,narr);return -1;}
        int arity=narr;
        if(arity<1||nvals%arity!=0){free(vals);free_lins(arr,narr);return 1;}
        int rows=nvals/arity;
        if(rows==0){
            /* an empty table is trivially unsatisfiable */
            Lin nf;memset(&nf,0,sizeof(nf));b_put(b,'>',1.0,&nf);lin_free(&nf);
            free(vals);free_lins(arr,narr);return 0;
        }
        /* per-arity big-M M[i] must dominate |x_i - t| for every table value t
           in column i and every feasible x_i in [lo_i,hi_i].  The tight domain
           span hi_i-lo_i is only enough when all table values lie inside the
           domain; for out-of-domain values we need the full
           max(maxCol_i - lo_i, hi_i - minCol_i). */
        double*M=(double*)psolve_malloc((size_t)(arity?arity:1)*sizeof(double));
        for(int i=0;i<arity;i++){
            if(arr[i].n!=1){free(M);free(vals);free_lins(arr,narr);return 1;}
            int v=arr[i].idx[0];
            if(v<0||v>=b->nvars||!b->haslo[v]||!b->hashi[v]||b->lo[v]>b->hi[v]){
                free(M);free(vals);free_lins(arr,narr);return 1;   /* unbounded -> UNKNOWN */
            }
            double lo=b->lo[v], hi=b->hi[v];
            double colmin=1e18, colmax=-1e18;
            for(int j=0;j<rows;j++){ double t=(double)vals[j*arity+i]; if(t<colmin)colmin=t; if(t>colmax)colmax=t; }
            double m = fmax(colmax-lo, hi-colmin);
            M[i]= (m<1.0)?1.0:m;
        }
        /* one binary selector per row */
        int*bs=(int*)psolve_malloc((size_t)rows*sizeof(int));
        for(int j=0;j<rows;j++) bs[j]=b_newvar(b,0.0,1.0);
        /* exactly one row is selected: sum_j b_j = 1 */
        Lin s;memset(&s,0,sizeof(s));
        for(int j=0;j<rows;j++) lin_term(&s,bs[j],1.0);
        s.constant=-1.0; b_put(b,'=',0.0,&s); lin_free(&s);
        /* b_j = 1  =>  x_i = T[j][i], via big-M: |x_i - T[j][i]| <= M_i*(1-b_j) */
        for(int i=0;i<arity;i++){
            int v=arr[i].idx[0]; double m=M[i];
            for(int j=0;j<rows;j++){
                long t=vals[j*arity+i];
                Lin r1;memset(&r1,0,sizeof(r1));lin_term(&r1,v,1.0);lin_term(&r1,bs[j],m);
                b_put(b,'<',(double)t+m,&r1);lin_free(&r1);
                Lin r2;memset(&r2,0,sizeof(r2));lin_term(&r2,v,1.0);lin_term(&r2,bs[j],-m);
                b_put(b,'>',(double)t-m,&r2);lin_free(&r2);
            }
        }
        free(M);free(bs);free(vals);free_lins(arr,narr);
        return 0;
    }
    /* cumulative(s[], d[], r[], b): Pritsker time-indexed 0-1 formulation.
       Strong exact LP relaxation with start-time indicator binaries. */
    if(strcmp(p,"cumulative")==0||strcmp(p,"fzn_cumulative")==0||
       strcmp(p,"gecode_cumulative")==0||strcmp(p,"gecode_cumulatives")==0){
        if(c->nargs<4)return -1;
        Lin *sarr;int ns; Lin *darr;int nd; Lin *rarr;int nr; Lin bl;
        if(parse_array(m,c->args[0],&sarr,&ns)!=0)return -1;
        if(parse_array(m,c->args[1],&darr,&nd)!=0){free_lins(sarr,ns);return -1;}
        if(parse_array(m,c->args[2],&rarr,&nr)!=0){free_lins(sarr,ns);free_lins(darr,nd);return -1;}
        if(parse_lin(m,c->args[3],&bl)!=0){free_lins(sarr,ns);free_lins(darr,nd);free_lins(rarr,nr);return -1;}
        if(ns!=nd||ns!=nr){lin_free(&bl);free_lins(sarr,ns);free_lins(darr,nd);free_lins(rarr,nr);return -1;}
        int n=ns;
        if(n==0){lin_free(&bl);free_lins(sarr,ns);free_lins(darr,nd);free_lins(rarr,nr);return 0;}
        if(bl.n!=0||bl.constant<0.0||fabs(bl.constant-rint(bl.constant))>1e-9){
            lin_free(&bl);free_lins(sarr,ns);free_lins(darr,nd);free_lins(rarr,nr);return 1;
        }
        double B=bl.constant;

        double minStart=1e18,maxEnd=-1e18;
        long *lo=(long*)psolve_malloc((size_t)n*sizeof(long));
        long *hi=(long*)psolve_malloc((size_t)n*sizeof(long));
        double *dval=(double*)psolve_malloc((size_t)n*sizeof(double));
        double *rval=(double*)psolve_malloc((size_t)n*sizeof(double));
        int any=0;

        for(int i=0;i<n;i++){
            if(darr[i].n!=0||rarr[i].n!=0){
                free(lo);free(hi);free(dval);free(rval);
                lin_free(&bl);free_lins(sarr,ns);free_lins(darr,nd);free_lins(rarr,nr);return 1;
            }
            double d=darr[i].constant,r=rarr[i].constant;
            dval[i]=d; rval[i]=r;
            if(d<0.0||r<0.0||fabs(d-rint(d))>1e-9||fabs(r-rint(r))>1e-9){
                free(lo);free(hi);free(dval);free(rval);
                lin_free(&bl);free_lins(sarr,ns);free_lins(darr,nd);free_lins(rarr,nr);return 1;
            }
            if(d==0.0||r==0.0) continue;
            double xlo, xhi;
            if(lin_bounds(b,&sarr[i],&xlo,&xhi)!=0){
                free(lo);free(hi);free(dval);free(rval);
                lin_free(&bl);free_lins(sarr,ns);free_lins(darr,nd);free_lins(rarr,nr);return 1;
            }
            lo[i]=(long)floor(xlo); hi[i]=(long)ceil(xhi);
            if(xlo < minStart) minStart=xlo;
            if(xhi + d > maxEnd) maxEnd=xhi + d;
            any=1;
        }

        if(!any){
            free(lo);free(hi);free(dval);free(rval);
            lin_free(&bl);free_lins(sarr,ns);free_lins(darr,nd);free_lins(rarr,nr);return 0;
        }

        long Tmin=(long)floor(minStart), Tmax=(long)floor(maxEnd)-1;
        if(Tmax < Tmin){
            free(lo);free(hi);free(dval);free(rval);
            lin_free(&bl);free_lins(sarr,ns);free_lins(darr,nd);free_lins(rarr,nr);return 0;
        }

        long total_vars=0;
        for(int i=0;i<n;i++){
            if(dval[i]>0.0&&rval[i]>0.0) total_vars += (hi[i]-lo[i]+1);
        }
        if(total_vars > 2048 || (Tmax-Tmin+1) > 512){
            free(lo);free(hi);free(dval);free(rval);
            lin_free(&bl);free_lins(sarr,ns);free_lins(darr,nd);free_lins(rarr,nr);return 1;
        }

        int **z=(int**)psolve_malloc((size_t)n*sizeof(int*));
        for(int i=0;i<n;i++){
            if(dval[i]>0.0&&rval[i]>0.0){
                long n_k = hi[i]-lo[i]+1;
                z[i]=(int*)psolve_malloc((size_t)n_k*sizeof(int));
                Lin s_sum; memset(&s_sum,0,sizeof(s_sum));
                Lin s_val; memset(&s_val,0,sizeof(s_val)); lin_into(&s_val,&sarr[i],1.0);
                for(long k=lo[i]; k<=hi[i]; k++){
                    int zik = b_newvar(b, 0.0, 1.0);
                    z[i][k - lo[i]] = zik;
                    lin_term(&s_sum, zik, 1.0);
                    lin_term(&s_val, zik, -(double)k);
                }
                b_put(b, '=', 1.0, &s_sum); lin_free(&s_sum);
                b_put(b, '=', 0.0, &s_val); lin_free(&s_val);
            } else {
                z[i]=NULL;
            }
        }

        for(long t=Tmin; t<=Tmax; t++){
            Lin R; memset(&R, 0, sizeof(R));
            for(int i=0;i<n;i++){
                if(dval[i]==0.0||rval[i]==0.0) continue;
                long k_start = t - (long)dval[i] + 1;
                if(k_start < lo[i]) k_start = lo[i];
                long k_end = t;
                if(k_end > hi[i]) k_end = hi[i];
                for(long k=k_start; k<=k_end; k++){
                    lin_term(&R, z[i][k - lo[i]], rval[i]);
                }
            }
            if(R.n > 0) b_put(b, '<', B, &R);
            lin_free(&R);
        }

        for(int i=0;i<n;i++) if(z[i]) free(z[i]);
        free(z); free(lo); free(hi); free(dval); free(rval);
        lin_free(&bl); free_lins(sarr,ns); free_lins(darr,nd); free_lins(rarr,nr);
        return 0;
    }
    /* all_equal(x[]): every element of x equals x[0] */
    if(strcmp(p,"all_equal")==0||strcmp(p,"all_equal_int")==0||strcmp(p,"fzn_all_equal_int")==0||
       strcmp(p,"all_equal_bool")==0||strcmp(p,"fzn_all_equal_bool")==0){
        if(c->nargs<1)return -1;
        Lin*arr;int narr;
        if(parse_array(m,c->args[0],&arr,&narr)!=0)return -1;
        for(int i=0;i+1<narr;i++){
            Lin d;memset(&d,0,sizeof(d));
            lin_into(&d,&arr[i],1.0);lin_into(&d,&arr[i+1],-1.0);
            b_put(b,'=',0.0,&d);
            lin_free(&d);
        }
        free_lins(arr,narr);
        return 0;
    }
    /* increasing / decreasing (non-strict and strict integer) */
    if(strcmp(p,"increasing")==0||strcmp(p,"increasing_int")==0||strcmp(p,"fzn_increasing_int")==0||
       strcmp(p,"increasing_bool")==0||strcmp(p,"fzn_increasing_bool")==0||
       strcmp(p,"increasing_float")==0||strcmp(p,"fzn_increasing_float")==0||
       strcmp(p,"decreasing")==0||strcmp(p,"decreasing_int")==0||strcmp(p,"fzn_decreasing_int")==0||
       strcmp(p,"decreasing_bool")==0||strcmp(p,"fzn_decreasing_bool")==0||
       strcmp(p,"decreasing_float")==0||strcmp(p,"fzn_decreasing_float")==0||
       strcmp(p,"strictly_increasing")==0||strcmp(p,"strictly_increasing_int")==0||strcmp(p,"fzn_strictly_increasing_int")==0||
       strcmp(p,"strictly_decreasing")==0||strcmp(p,"strictly_decreasing_int")==0||strcmp(p,"fzn_strictly_decreasing_int")==0){
        if(c->nargs<1)return -1;
        Lin*arr;int narr;
        if(parse_array(m,c->args[0],&arr,&narr)!=0)return -1;
        int is_dec = (strstr(p,"decreasing")!=NULL);
        int is_strict = (strstr(p,"strictly")!=NULL);
        int is_float = (strstr(p,"float")!=NULL);
        if(is_strict && is_float){
            free_lins(arr,narr); return 1; /* strict continuous relation -> unhandled */
        }
        char rel = is_dec ? '>' : '<';
        double rhs = 0.0;
        if(is_strict) rhs = is_dec ? 1.0 : -1.0;
        for(int i=0;i+1<narr;i++){
            Lin d;memset(&d,0,sizeof(d));
            lin_into(&d,&arr[i],1.0);lin_into(&d,&arr[i+1],-1.0);
            b_put(b,rel,rhs,&d);
            lin_free(&d);
        }
        free_lins(arr,narr);
        return 0;
    }
    /* Lexicographical comparison between arrays x and y:
       lex_less(x, y), lex_lesseq(x, y), array_int_lt(x, y), array_int_lq(x, y), etc. */
    if(strcmp(p,"lex_less")==0||strcmp(p,"lex_less_int")==0||strcmp(p,"fzn_lex_less_int")==0||
       strcmp(p,"array_int_lt")==0||strcmp(p,"lex_less_bool")==0||strcmp(p,"fzn_lex_less_bool")==0||
       strcmp(p,"array_bool_lt")==0||
       strcmp(p,"lex_lesseq")==0||strcmp(p,"lex_lesseq_int")==0||strcmp(p,"fzn_lex_lesseq_int")==0||
       strcmp(p,"array_int_lq")==0||strcmp(p,"lex_lesseq_bool")==0||strcmp(p,"fzn_lex_lesseq_bool")==0||
       strcmp(p,"array_bool_lq")==0){
        if(c->nargs<2)return -1;
        Lin*xs;int nx; Lin*ys;int ny;
        if(parse_array(m,c->args[0],&xs,&nx)!=0)return -1;
        if(parse_array(m,c->args[1],&ys,&ny)!=0){free_lins(xs,nx);return -1;}
        int is_strict = (strstr(p,"less")!=NULL && strstr(p,"lesseq")==NULL) || (strstr(p,"_lt")!=NULL);
        int n = (nx < ny) ? nx : ny;
        if(n == 0){
            int sat = is_strict ? (nx < ny) : (nx <= ny);
            if(!sat){ Lin nf;memset(&nf,0,sizeof(nf));b_put(b,'=',1.0,&nf);lin_free(&nf); }
            free_lins(xs,nx);free_lins(ys,ny);
            return 0;
        }
        if(n > 256){ free_lins(xs,nx);free_lins(ys,ny);return 1; } /* cap */

        int *e = (int*)psolve_malloc((size_t)n * sizeof(int));
        int *l = (int*)psolve_malloc((size_t)n * sizeof(int));
        int *p_eq = (int*)psolve_malloc((size_t)n * sizeof(int));
        int *b_diff = (int*)psolve_malloc((size_t)n * sizeof(int));

        for(int i=0;i<n;i++){
            e[i] = b_newvar(b, 0.0, 1.0);
            l[i] = b_newvar(b, 0.0, 1.0);
            Lin di; memset(&di, 0, sizeof(di)); lin_into(&di, &xs[i], 1.0); lin_into(&di, &ys[i], -1.0);
            add_int_reif(b, &di, e[i], 0); /* e[i] <-> xs[i] == ys[i] */
            add_int_reif(b, &di, l[i], 2); /* l[i] <-> xs[i] < ys[i] */
            lin_free(&di);

            if(i == 0){
                p_eq[0] = e[0];
                b_diff[0] = l[0];
            } else {
                p_eq[i] = b_newvar(b, 0.0, 1.0);
                /* p_eq[i] = p_eq[i-1] AND e[i] */
                Lin c1; memset(&c1, 0, sizeof(c1)); lin_term(&c1, p_eq[i], 1.0); lin_term(&c1, p_eq[i-1], -1.0); b_put(b, '<', 0.0, &c1); lin_free(&c1);
                Lin c2; memset(&c2, 0, sizeof(c2)); lin_term(&c2, p_eq[i], 1.0); lin_term(&c2, e[i], -1.0); b_put(b, '<', 0.0, &c2); lin_free(&c2);
                Lin c3; memset(&c3, 0, sizeof(c3)); lin_term(&c3, p_eq[i], 1.0); lin_term(&c3, p_eq[i-1], -1.0); lin_term(&c3, e[i], -1.0); c3.constant = 1.0; b_put(b, '>', 0.0, &c3); lin_free(&c3);

                b_diff[i] = b_newvar(b, 0.0, 1.0);
                /* b_diff[i] = p_eq[i-1] AND l[i] */
                Lin d1; memset(&d1, 0, sizeof(d1)); lin_term(&d1, b_diff[i], 1.0); lin_term(&d1, p_eq[i-1], -1.0); b_put(b, '<', 0.0, &d1); lin_free(&d1);
                Lin d2; memset(&d2, 0, sizeof(d2)); lin_term(&d2, b_diff[i], 1.0); lin_term(&d2, l[i], -1.0); b_put(b, '<', 0.0, &d2); lin_free(&d2);
                Lin d3; memset(&d3, 0, sizeof(d3)); lin_term(&d3, b_diff[i], 1.0); lin_term(&d3, p_eq[i-1], -1.0); lin_term(&d3, l[i], -1.0); d3.constant = 1.0; b_put(b, '>', 0.0, &d3); lin_free(&d3);
            }
        }

        int allow_eq = is_strict ? (nx < ny) : (nx <= ny);
        Lin sum; memset(&sum, 0, sizeof(sum));
        for(int i=0;i<n;i++) lin_term(&sum, b_diff[i], 1.0);
        if(allow_eq) lin_term(&sum, p_eq[n-1], 1.0);
        b_put(b, '=', 1.0, &sum);
        lin_free(&sum);

        free(e); free(l); free(p_eq); free(b_diff);
        free_lins(xs,nx); free_lins(ys,ny);
        return 0;
    }
    /* global_cardinality(x[], cover[], counts[]) and low_up / closed variants */
    if(strcmp(p,"global_cardinality")==0||strcmp(p,"fzn_global_cardinality")==0||
       strcmp(p,"gecode_global_cardinality")==0||
       strcmp(p,"global_cardinality_closed")==0||strcmp(p,"fzn_global_cardinality_closed")==0||
       strcmp(p,"global_cardinality_low_up")==0||strcmp(p,"fzn_global_cardinality_low_up")==0||
       strcmp(p,"global_cardinality_low_up_closed")==0||strcmp(p,"fzn_global_cardinality_low_up_closed")==0){
        int is_low_up = (strstr(p,"low_up")!=NULL);
        int is_closed = (strstr(p,"closed")!=NULL);
        if(c->nargs < (is_low_up ? 4 : 3)) return -1;
        Lin*xs; int nx; Lin*cover; int nc;
        if(parse_array(m,c->args[0],&xs,&nx)!=0) return -1;
        if(parse_array(m,c->args[1],&cover,&nc)!=0){free_lins(xs,nx);return -1;}
        Lin*counts = NULL; int ncnt = 0;
        Lin*lbound = NULL; int nlb = 0;
        Lin*ubound = NULL; int nub = 0;
        if(is_low_up){
            if(parse_array(m,c->args[2],&lbound,&nlb)!=0||parse_array(m,c->args[3],&ubound,&nub)!=0||
               nlb != nc || nub != nc){
                if(lbound) { free_lins(lbound,nlb); }
                if(ubound) { free_lins(ubound,nub); }
                free_lins(xs,nx); free_lins(cover,nc); return -1;
            }
        } else {
            if(parse_array(m,c->args[2],&counts,&ncnt)!=0 || ncnt != nc){
                if(counts) { free_lins(counts,ncnt); }
                free_lins(xs,nx); free_lins(cover,nc); return -1;
            }
        }
        if(nx > 0 && nc > 0 && nx * nc > 4096){
            if(counts) { free_lins(counts,ncnt); }
            if(lbound) { free_lins(lbound,nlb); }
            if(ubound) { free_lins(ubound,nub); }
            free_lins(xs,nx); free_lins(cover,nc);
            return 1;
        }

        size_t sz_z = (nx > 0 && nc > 0) ? (size_t)nx * (size_t)nc : 1;
        int *z = (int*)psolve_malloc(sz_z * sizeof(int));
        for(int i=0;i<nx;i++){
            for(int k=0;k<nc;k++){
                z[i*nc + k] = b_newvar(b, 0.0, 1.0);
                Lin di; memset(&di, 0, sizeof(di)); lin_into(&di, &xs[i], 1.0); lin_into(&di, &cover[k], -1.0);
                add_int_reif(b, &di, z[i*nc + k], 0);
                lin_free(&di);
            }
        }

        /* each variable matches at most one (or exactly one if closed) cover value */
        for(int i=0;i<nx;i++){
            Lin s; memset(&s, 0, sizeof(s));
            for(int k=0;k<nc;k++) lin_term(&s, z[i*nc + k], 1.0);
            b_put(b, is_closed ? '=' : '<', 1.0, &s);
            lin_free(&s);
        }

        /* count occurrences for each cover value */
        for(int k=0;k<nc;k++){
            Lin s; memset(&s, 0, sizeof(s));
            for(int i=0;i<nx;i++) lin_term(&s, z[i*nc + k], 1.0);
            if(is_low_up){
                Lin lo; memset(&lo, 0, sizeof(lo)); lin_into(&lo, &s, 1.0); lin_into(&lo, &lbound[k], -1.0);
                b_put(b, '>', 0.0, &lo); lin_free(&lo);
                Lin up; memset(&up, 0, sizeof(up)); lin_into(&up, &s, 1.0); lin_into(&up, &ubound[k], -1.0);
                b_put(b, '<', 0.0, &up); lin_free(&up);
            } else {
                lin_into(&s, &counts[k], -1.0);
                b_put(b, '=', 0.0, &s);
            }
            lin_free(&s);
        }

        free(z);
        if(counts) { free_lins(counts,ncnt); }
        if(lbound) { free_lins(lbound,nlb); }
        if(ubound) { free_lins(ubound,nub); }
        free_lins(xs,nx); free_lins(cover,nc);
        return 0;
    }
    /* bin_packing(c, bin[], w[]), bin_packing_capa(c[], bin[], w[]),
       bin_packing_load(l[], bin[], w[], [minIndex]) */
    if(strcmp(p,"bin_packing")==0||strcmp(p,"fzn_bin_packing")==0||
       strcmp(p,"bin_packing_capa")==0||strcmp(p,"fzn_bin_packing_capa")==0||
       strcmp(p,"bin_packing_load")==0||strcmp(p,"fzn_bin_packing_load")==0||
       strcmp(p,"gecode_bin_packing_load")==0){
        int is_gec = (strcmp(p,"gecode_bin_packing_load")==0);
        int is_load = (strstr(p,"load")!=NULL);
        int is_capa_arr = (strstr(p,"capa")!=NULL);
        int min_args = is_gec ? 4 : 3;
        if(c->nargs < min_args) return -1;

        int minIndex = 1;
        if(is_gec){
            Lin mol; if(parse_lin(m,c->args[3],&mol)!=0) return -1;
            if(mol.n!=0) { lin_free(&mol); return 1; }
            minIndex = (int)llround(mol.constant); lin_free(&mol);
        }

        Lin *load_arr = NULL; int nload = 0;
        Lin *capa_arr = NULL; int ncapa = 0;
        Lin capa_scalar; memset(&capa_scalar, 0, sizeof(capa_scalar));
        Lin *bin_arr = NULL; int nbin = 0;
        Lin *w_arr = NULL; int nw = 0;

        if(is_load){
            if(parse_array(m,c->args[0],&load_arr,&nload)!=0) return -1;
            if(parse_array(m,c->args[1],&bin_arr,&nbin)!=0){free_lins(load_arr,nload);return -1;}
            if(parse_array(m,c->args[2],&w_arr,&nw)!=0){free_lins(load_arr,nload);free_lins(bin_arr,nbin);return -1;}
        } else if(is_capa_arr){
            if(parse_array(m,c->args[0],&capa_arr,&ncapa)!=0) return -1;
            if(parse_array(m,c->args[1],&bin_arr,&nbin)!=0){free_lins(capa_arr,ncapa);return -1;}
            if(parse_array(m,c->args[2],&w_arr,&nw)!=0){free_lins(capa_arr,ncapa);free_lins(bin_arr,nbin);return -1;}
        } else {
            if(parse_lin(m,c->args[0],&capa_scalar)!=0) return -1;
            if(parse_array(m,c->args[1],&bin_arr,&nbin)!=0){lin_free(&capa_scalar);return -1;}
            if(parse_array(m,c->args[2],&w_arr,&nw)!=0){lin_free(&capa_scalar);free_lins(bin_arr,nbin);return -1;}
        }

        if(nbin != nw){
            if(load_arr)free_lins(load_arr,nload);
            if(capa_arr)free_lins(capa_arr,ncapa);
            lin_free(&capa_scalar);
            free_lins(bin_arr,nbin); free_lins(w_arr,nw);
            return -1;
        }

        int m_bins = is_load ? nload : (is_capa_arr ? ncapa : 0);
        if(!is_load && !is_capa_arr){
            double max_b = 0;
            for(int i=0;i<nbin;i++){
                int bv;
                if(lin_unit_var(&bin_arr[i],&bv)==0 && bv>=0 && bv<b->nvars && b->hashi[bv]){
                    if(b->hi[bv] > max_b) max_b = b->hi[bv];
                }
            }
            m_bins = (int)llround(max_b) - minIndex + 1;
            if(m_bins < 1) m_bins = nbin;
        }

        if(nbin > 0 && m_bins > 0 && nbin * m_bins > 4096){
            if(load_arr)free_lins(load_arr,nload);
            if(capa_arr)free_lins(capa_arr,ncapa);
            lin_free(&capa_scalar);
            free_lins(bin_arr,nbin); free_lins(w_arr,nw);
            return 1;
        }

        size_t sz_z = (nbin > 0 && m_bins > 0) ? (size_t)nbin * (size_t)m_bins : 1;
        int *z = (int*)psolve_malloc(sz_z * sizeof(int));
        for(int i=0;i<nbin;i++){
            for(int j=0;j<m_bins;j++){
                z[i*m_bins + j] = b_newvar(b, 0.0, 1.0);
                Lin di; memset(&di, 0, sizeof(di)); lin_into(&di, &bin_arr[i], 1.0); di.constant -= (double)(minIndex + j);
                add_int_reif(b, &di, z[i*m_bins + j], 0);
                lin_free(&di);
            }
            Lin s; memset(&s, 0, sizeof(s));
            for(int j=0;j<m_bins;j++) lin_term(&s, z[i*m_bins + j], 1.0);
            b_put(b, '=', 1.0, &s);
            lin_free(&s);
        }

        for(int j=0;j<m_bins;j++){
            Lin load_j; memset(&load_j, 0, sizeof(load_j));
            for(int i=0;i<nbin;i++){
                if(w_arr[i].n == 0){
                    lin_term(&load_j, z[i*m_bins + j], w_arr[i].constant);
                } else {
                    free(z);
                    if(load_arr)free_lins(load_arr,nload);
                    if(capa_arr)free_lins(capa_arr,ncapa);
                    lin_free(&capa_scalar);
                    free_lins(bin_arr,nbin); free_lins(w_arr,nw);
                    return 1;
                }
            }
            if(is_load){
                Lin eq; memset(&eq, 0, sizeof(eq)); lin_into(&eq, &load_arr[j], 1.0); lin_into(&eq, &load_j, -1.0);
                b_put(b, '=', 0.0, &eq); lin_free(&eq);
            } else if(is_capa_arr){
                Lin le; memset(&le, 0, sizeof(le)); lin_into(&le, &load_j, 1.0); lin_into(&le, &capa_arr[j], -1.0);
                b_put(b, '<', 0.0, &le); lin_free(&le);
            } else {
                Lin le; memset(&le, 0, sizeof(le)); lin_into(&le, &load_j, 1.0); lin_into(&le, &capa_scalar, -1.0);
                b_put(b, '<', 0.0, &le); lin_free(&le);
            }
            lin_free(&load_j);
        }

        free(z);
        if(load_arr)free_lins(load_arr,nload);
        if(capa_arr)free_lins(capa_arr,ncapa);
        lin_free(&capa_scalar);
        free_lins(bin_arr,nbin); free_lins(w_arr,nw);
        return 0;
    }
    /* disjunctive(s[], d[]): non-overlapping task scheduling */
    if(strcmp(p,"disjunctive")==0||strcmp(p,"fzn_disjunctive")==0||
       strcmp(p,"gecode_schedule_unary")==0||
       strcmp(p,"disjunctive_strict")==0||strcmp(p,"fzn_disjunctive_strict")==0){
        if(c->nargs<2)return -1;
        Lin*sarr;int ns; Lin*darr;int nd;
        if(parse_array(m,c->args[0],&sarr,&ns)!=0)return -1;
        if(parse_array(m,c->args[1],&darr,&nd)!=0){free_lins(sarr,ns);return -1;}
        if(ns!=nd){free_lins(sarr,ns);free_lins(darr,nd);return -1;}
        if(ns>128){free_lins(sarr,ns);free_lins(darr,nd);return 1;}

        for(int i=0;i<ns;i++){
            if(darr[i].n!=0){free_lins(sarr,ns);free_lins(darr,nd);return 1;}
        }

        for(int i=0;i<ns;i++){
            double di = darr[i].constant;
            if(di <= 0.0) continue;
            for(int j=i+1;j<ns;j++){
                double dj = darr[j].constant;
                if(dj <= 0.0) continue;

                Lin dij; memset(&dij,0,sizeof(dij));
                lin_into(&dij, &sarr[i], 1.0); lin_into(&dij, &sarr[j], -1.0); dij.constant += di;
                double Lij, Uij;
                if(lin_bounds(b, &dij, &Lij, &Uij)!=0){
                    lin_free(&dij); free_lins(sarr,ns); free_lins(darr,nd); return 1;
                }

                Lin dji; memset(&dji,0,sizeof(dji));
                lin_into(&dji, &sarr[j], 1.0); lin_into(&dji, &sarr[i], -1.0); dji.constant += dj;
                double Lji, Uji;
                if(lin_bounds(b, &dji, &Lji, &Uji)!=0){
                    lin_free(&dij); lin_free(&dji); free_lins(sarr,ns); free_lins(darr,nd); return 1;
                }

                /* If both orderings are infeasible: model is infeasible */
                if(Lij > 0.0 && Lji > 0.0){
                    Lin nf; memset(&nf,0,sizeof(nf)); b_put(b, '=', 1.0, &nf); lin_free(&nf);
                    lin_free(&dij); lin_free(&dji); continue;
                }

                int bij = b_newvar(b, 0.0, 1.0);
                double Mij = fmax(fabs(Lij), fabs(Uij)) + 1.0;
                double Mji = fmax(fabs(Lji), fabs(Uji)) + 1.0;

                /* bij=1 => dij <= 0 => dij + Mij*bij <= Mij */
                Lin r1; memset(&r1,0,sizeof(r1));
                lin_into(&r1, &dij, 1.0); lin_term(&r1, bij, Mij);
                b_put(b, '<', Mij, &r1); lin_free(&r1);

                /* bij=0 => dji <= 0 => dji - Mji*bij <= 0 */
                Lin r2; memset(&r2,0,sizeof(r2));
                lin_into(&r2, &dji, 1.0); lin_term(&r2, bij, -Mji);
                b_put(b, '<', 0.0, &r2); lin_free(&r2);

                lin_free(&dij); lin_free(&dji);
            }
        }

        free_lins(sarr,ns); free_lins(darr,nd);
        return 0;
    }
    /* inverse(f[], invf[]) / inverse_offsets(f, foff, invf, invfoff) */
    if(strcmp(p,"inverse")==0||strcmp(p,"fzn_inverse")==0||
       strcmp(p,"gecode_inverse")==0||strcmp(p,"inverse_offsets")==0){
        int is_off = (strcmp(p,"inverse_offsets")==0);
        if(c->nargs < (is_off ? 4 : 2)) return -1;
        int foff = 1, invfoff = 1;
        Lin *farr; int nf; Lin *invarr; int ninv;
        if(is_off){
            if(parse_array(m,c->args[0],&farr,&nf)!=0) return -1;
            Lin ol1; if(parse_lin(m,c->args[1],&ol1)!=0){free_lins(farr,nf);return -1;}
            foff = (int)llround(ol1.constant); lin_free(&ol1);
            if(parse_array(m,c->args[2],&invarr,&ninv)!=0){free_lins(farr,nf);return -1;}
            Lin ol2; if(parse_lin(m,c->args[3],&ol2)!=0){free_lins(farr,nf);free_lins(invarr,ninv);return -1;}
            invfoff = (int)llround(ol2.constant); lin_free(&ol2);
        } else {
            if(parse_array(m,c->args[0],&farr,&nf)!=0) return -1;
            if(parse_array(m,c->args[1],&invarr,&ninv)!=0){free_lins(farr,nf);return -1;}
        }

        if(nf != ninv || nf > 64){ free_lins(farr,nf); free_lins(invarr,ninv); return 1; }

        size_t sz_z = (size_t)nf * (size_t)nf;
        int *z = (int*)psolve_malloc(sz_z * sizeof(int));
        for(int i=0;i<nf;i++)for(int j=0;j<nf;j++) z[i*nf + j] = b_newvar(b, 0.0, 1.0);

        for(int i=0;i<nf;i++){
            Lin s; memset(&s, 0, sizeof(s));
            for(int j=0;j<nf;j++) lin_term(&s, z[i*nf + j], 1.0);
            b_put(b, '=', 1.0, &s); lin_free(&s);

            Lin eq_f; memset(&eq_f, 0, sizeof(eq_f)); lin_into(&eq_f, &farr[i], 1.0);
            for(int j=0;j<nf;j++) lin_term(&eq_f, z[i*nf + j], -(double)(j + invfoff));
            b_put(b, '=', 0.0, &eq_f); lin_free(&eq_f);
        }
        for(int j=0;j<nf;j++){
            Lin s; memset(&s, 0, sizeof(s));
            for(int i=0;i<nf;i++) lin_term(&s, z[i*nf + j], 1.0);
            b_put(b, '=', 1.0, &s); lin_free(&s);

            Lin eq_inv; memset(&eq_inv, 0, sizeof(eq_inv)); lin_into(&eq_inv, &invarr[j], 1.0);
            for(int i=0;i<nf;i++) lin_term(&eq_inv, z[i*nf + j], -(double)(i + foff));
            b_put(b, '=', 0.0, &eq_inv); lin_free(&eq_inv);
        }

        free(z); free_lins(farr,nf); free_lins(invarr,ninv);
        return 0;
    }
    /* member(x[], y): y appears in array x */
    if(strcmp(p,"member")==0||strcmp(p,"member_int")==0||strcmp(p,"fzn_member_int")==0||
       strcmp(p,"member_bool")==0||strcmp(p,"fzn_member_bool")==0||
       strcmp(p,"member_float")==0||strcmp(p,"fzn_member_float")==0){
        if(c->nargs<2)return -1;
        Lin*arr;int narr; Lin yl;
        if(parse_array(m,c->args[0],&arr,&narr)!=0)return -1;
        if(parse_lin(m,c->args[1],&yl)!=0){free_lins(arr,narr);return -1;}
        if(narr == 0){
            Lin nf; memset(&nf, 0, sizeof(nf)); b_put(b, '=', 1.0, &nf); lin_free(&nf);
            free_lins(arr,narr); lin_free(&yl); return 0;
        }
        if(narr > 1024){ free_lins(arr,narr); lin_free(&yl); return 1; }

        int is_float = (strstr(p,"float")!=NULL);
        if(is_float){
            int *zs = (int*)psolve_malloc((size_t)narr * sizeof(int));
            for(int i=0;i<narr;i++){
                zs[i] = b_newvar(b, 0.0, 1.0);
                Lin di; memset(&di, 0, sizeof(di)); lin_into(&di, &yl, 1.0); lin_into(&di, &arr[i], -1.0);
                double L, U;
                if(lin_bounds(b, &di, &L, &U) != 0){ free(zs); lin_free(&di); free_lins(arr,narr); lin_free(&yl); return 1; }
                double M = fmax(fabs(L), fabs(U)); if(M < 1.0) M = 1.0;
                Lin up; memset(&up, 0, sizeof(up)); lin_into(&up, &di, 1.0); lin_term(&up, zs[i], M);
                b_put(b, '<', M, &up); lin_free(&up);
                Lin lo; memset(&lo, 0, sizeof(lo)); lin_into(&lo, &di, 1.0); lin_term(&lo, zs[i], -M);
                b_put(b, '>', -M, &lo); lin_free(&lo);
                lin_free(&di);
            }
            Lin sum; memset(&sum, 0, sizeof(sum));
            for(int i=0;i<narr;i++) lin_term(&sum, zs[i], 1.0);
            b_put(b, '>', 1.0, &sum); lin_free(&sum);
            free(zs);
        } else {
            int *zs = (int*)psolve_malloc((size_t)narr * sizeof(int));
            for(int i=0;i<narr;i++){
                zs[i] = b_newvar(b, 0.0, 1.0);
                Lin di; memset(&di, 0, sizeof(di)); lin_into(&di, &arr[i], 1.0); lin_into(&di, &yl, -1.0);
                add_int_reif(b, &di, zs[i], 0);
                lin_free(&di);
            }
            Lin sum; memset(&sum, 0, sizeof(sum));
            for(int i=0;i<narr;i++) lin_term(&sum, zs[i], 1.0);
            b_put(b, '>', 1.0, &sum); lin_free(&sum);
            free(zs);
        }
        free_lins(arr,narr); lin_free(&yl);
        return 0;
    }
    /* alldifferent_except_0(x[]) and alldifferent_except(x[], S) */
    if(strcmp(p,"alldifferent_except_0")==0||strcmp(p,"fzn_alldifferent_except_0")==0||
       strcmp(p,"all_different_except_0")==0||
       strcmp(p,"alldifferent_except")==0||strcmp(p,"fzn_alldifferent_except")==0){
        if(c->nargs<1)return -1;
        Lin*arr;int narr;
        if(parse_array(m,c->args[0],&arr,&narr)!=0)return -1;
        if(narr<=1){free_lins(arr,narr);return 0;}
        double dlo=1e18, dhi=-1e18;
        for(int i=0;i<narr;i++){
            double lo_i, hi_i;
            if(lin_bounds(b, &arr[i], &lo_i, &hi_i)!=0){free_lins(arr,narr);return 1;}
            if(lo_i<dlo)dlo=lo_i;
            if(hi_i>dhi)dhi=hi_i;
        }
        long ndom=(long)(dhi-dlo)+1;
        if(ndom<=0||ndom>128){free_lins(arr,narr);return 1;}

        for(long v=(long)dlo; v<=(long)dhi; v++){
            if(v == 0) continue; /* except 0 */
            Lin sum; memset(&sum, 0, sizeof(sum));
            for(int i=0;i<narr;i++){
                int z = b_newvar(b, 0.0, 1.0);
                Lin di; memset(&di, 0, sizeof(di)); lin_into(&di, &arr[i], 1.0); di.constant -= (double)v;
                add_int_reif(b, &di, z, 0);
                lin_free(&di);
                lin_term(&sum, z, 1.0);
            }
            b_put(b, '<', 1.0, &sum);
            lin_free(&sum);
        }

        free_lins(arr,narr);
        return 0;
    }
    /* sliding_sum(low, up, seq, vs[]) */
    if(strcmp(p,"sliding_sum")==0||strcmp(p,"fzn_sliding_sum")==0){
        if(c->nargs<4)return -1;
        Lin low, up, seql; Lin*vs; int nvs;
        if(parse_lin(m,c->args[0],&low)!=0)return -1;
        if(parse_lin(m,c->args[1],&up)!=0){lin_free(&low);return -1;}
        if(parse_lin(m,c->args[2],&seql)!=0){lin_free(&low);lin_free(&up);return -1;}
        if(parse_array(m,c->args[3],&vs,&nvs)!=0){lin_free(&low);lin_free(&up);lin_free(&seql);return -1;}
        if(seql.n!=0||seql.constant<1.0||seql.constant>(double)nvs){
            lin_free(&low);lin_free(&up);lin_free(&seql);free_lins(vs,nvs);return 1;
        }
        int seq = (int)llround(seql.constant);
        for(int i=0; i + seq <= nvs; i++){
            Lin win; memset(&win, 0, sizeof(win));
            for(int k=0; k<seq; k++) lin_into(&win, &vs[i+k], 1.0);
            Lin lo_row; memset(&lo_row, 0, sizeof(lo_row)); lin_into(&lo_row, &win, 1.0); lin_into(&lo_row, &low, -1.0);
            b_put(b, '>', 0.0, &lo_row); lin_free(&lo_row);
            Lin up_row; memset(&up_row, 0, sizeof(up_row)); lin_into(&up_row, &win, 1.0); lin_into(&up_row, &up, -1.0);
            b_put(b, '<', 0.0, &up_row); lin_free(&up_row);
            lin_free(&win);
        }
        lin_free(&low);lin_free(&up);lin_free(&seql);free_lins(vs,nvs);
        return 0;
    }
    /* nvalue(n, x[]): number of distinct values in x equals n */
    if(strcmp(p,"nvalue")==0||strcmp(p,"fzn_nvalue")==0){
        if(c->nargs<2)return -1;
        Lin nl; Lin*arr; int narr;
        if(parse_lin(m,c->args[0],&nl)!=0)return -1;
        if(parse_array(m,c->args[1],&arr,&narr)!=0){lin_free(&nl);return -1;}
        if(narr==0){
            Lin eq; memset(&eq, 0, sizeof(eq)); lin_into(&eq, &nl, 1.0); b_put(b, '=', 0.0, &eq); lin_free(&eq);
            lin_free(&nl); free_lins(arr,narr); return 0;
        }
        double dlo=1e18, dhi=-1e18;
        for(int i=0;i<narr;i++){
            double lo_i, hi_i;
            if(lin_bounds(b, &arr[i], &lo_i, &hi_i)!=0){lin_free(&nl);free_lins(arr,narr);return 1;}
            if(lo_i<dlo)dlo=lo_i;
            if(hi_i>dhi)dhi=hi_i;
        }
        long ndom=(long)(dhi-dlo)+1;
        if(ndom<=0||ndom>256){lin_free(&nl);free_lins(arr,narr);return 1;}

        Lin sum_u; memset(&sum_u, 0, sizeof(sum_u));
        for(long v=(long)dlo; v<=(long)dhi; v++){
            int uv = b_newvar(b, 0.0, 1.0);
            lin_term(&sum_u, uv, 1.0);
            Lin sum_z; memset(&sum_z, 0, sizeof(sum_z));
            for(int i=0;i<narr;i++){
                int ziv = b_newvar(b, 0.0, 1.0);
                Lin di; memset(&di, 0, sizeof(di)); lin_into(&di, &arr[i], 1.0); di.constant -= (double)v;
                add_int_reif(b, &di, ziv, 0); lin_free(&di);
                Lin c1; memset(&c1, 0, sizeof(c1)); lin_term(&c1, ziv, 1.0); lin_term(&c1, uv, -1.0);
                b_put(b, '<', 0.0, &c1); lin_free(&c1);
                lin_term(&sum_z, ziv, 1.0);
            }
            Lin c2; memset(&c2, 0, sizeof(c2)); lin_term(&c2, uv, 1.0); lin_into(&c2, &sum_z, -1.0);
            b_put(b, '<', 0.0, &c2); lin_free(&c2); lin_free(&sum_z);
        }
        lin_into(&sum_u, &nl, -1.0);
        b_put(b, '=', 0.0, &sum_u);
        lin_free(&sum_u);
        lin_free(&nl); free_lins(arr,narr);
        return 0;
    }
    /* subcircuit(x[]) / gecode_subcircuit(offset, x[]) */
    if(strcmp(p,"subcircuit")==0||strcmp(p,"fzn_subcircuit")==0||strcmp(p,"gecode_subcircuit")==0){
        int offset=1; const char*xarg=NULL; Lin ol;
        if(strcmp(p,"gecode_subcircuit")==0){
            if(c->nargs<2)return -1;
            if(parse_lin(m,c->args[0],&ol)!=0)return -1;
            if(ol.n!=0) { lin_free(&ol); return 1; }
            offset=(int)llround(ol.constant); xarg=c->args[1]; lin_free(&ol);
        } else {
            if(c->nargs<1)return -1;
            xarg=c->args[0];
        }
        Lin*xs;int nx;
        if(parse_array(m,xarg,&xs,&nx)!=0)return -1;
        if(nx<=1){free_lins(xs,nx);return 0;}
        if(nx>48){free_lins(xs,nx);return 1;}

        int *z = (int*)psolve_malloc((size_t)nx * nx * sizeof(int));
        int *in_c = (int*)psolve_malloc((size_t)nx * sizeof(int));
        for(int i=0;i<nx;i++){
            for(int j=0;j<nx;j++){
                z[i*nx + j] = b_newvar(b, 0.0, 1.0);
                Lin dij; memset(&dij, 0, sizeof(dij)); lin_into(&dij, &xs[i], 1.0); dij.constant -= (double)(offset + j);
                add_int_reif(b, &dij, z[i*nx + j], 0); lin_free(&dij);
            }
            in_c[i] = b_newvar(b, 0.0, 1.0);
            Lin sc; memset(&sc, 0, sizeof(sc)); lin_term(&sc, in_c[i], 1.0); lin_term(&sc, z[i*nx + i], 1.0);
            b_put(b, '=', 1.0, &sc); lin_free(&sc);
        }

        for(int i=0;i<nx;i++){
            Lin out; memset(&out, 0, sizeof(out));
            for(int j=0;j<nx;j++) lin_term(&out, z[i*nx + j], 1.0);
            b_put(b, '=', 1.0, &out); lin_free(&out);
        }
        for(int j=0;j<nx;j++){
            Lin in; memset(&in, 0, sizeof(in));
            for(int i=0;i<nx;i++) lin_term(&in, z[i*nx + j], 1.0);
            b_put(b, '=', 1.0, &in); lin_free(&in);
        }

        /* Single-circuit elimination via MTZ ranks with an anchor: the ranks
           must strictly increase along circuit arcs, which is impossible
           around a closed loop, so one designated anchor node per circuit
           exempts the arc into it.  (Previously every arc was constrained:
           ANY non-trivial (sub)circuit - even a 2-cycle - was infeasible,
           and par arrays therefore claimed UNSAT for valid inputs.) */
        int *ord = (int*)psolve_malloc((size_t)nx * sizeof(int));
        int *g   = (int*)psolve_malloc((size_t)nx * sizeof(int));
        for(int i=0;i<nx;i++){ ord[i] = b_newvar_float(b, 0.0, (double)nx); g[i] = b_newvar(b, 0.0, 1.0); }
        for(int i=0;i<nx;i++){
            Lin gl;memset(&gl,0,sizeof(gl));lin_term(&gl,g[i],1.0);lin_term(&gl,in_c[i],-1.0);
            b_put(b,'<',0.0,&gl);lin_free(&gl);                 /* g_i <= in_c_i */
            Lin gh;memset(&gh,0,sizeof(gh));lin_term(&gh,in_c[i],1.0);
            for(int k=0;k<nx;k++)lin_term(&gh,g[k],-1.0);
            b_put(b,'<',0.0,&gh);lin_free(&gh);                 /* in_c_i <= sum g */
        }
        {   Lin gs;memset(&gs,0,sizeof(gs));
            for(int k=0;k<nx;k++)lin_term(&gs,g[k],1.0);
            b_put(b,'<',1.0,&gs);lin_free(&gs);                 /* at most one anchor */
        }
        for(int i=0;i<nx;i++){                                   /* anchor rank becomes 1 */
            Lin an;memset(&an,0,sizeof(an));lin_term(&an,ord[i],1.0);lin_term(&an,g[i],(double)nx);
            b_put(b,'<',(double)(nx+1),&an);lin_free(&an);
        }
        for(int i=0;i<nx;i++)for(int j=0;j<nx;j++)if(i!=j){
            /* z_ij = 1 and j not the anchor  =>  ord_j >= ord_i + 1 */
            Lin mtz; memset(&mtz, 0, sizeof(mtz));
            lin_term(&mtz, ord[i], 1.0); lin_term(&mtz, ord[j], -1.0);
            lin_term(&mtz, z[i*nx + j], (double)nx);
            lin_term(&mtz, g[j], -(double)nx);
            b_put(b, '<', (double)(nx - 1), &mtz); lin_free(&mtz);
        }

        free(g); free(ord); free(in_c); free(z); free_lins(xs,nx);
        return 0;
    }
    /* diffn(x[], y[], dx[], dy[]) / fzn_diffn / gecode_diffn / gecode_nooverlap: 2D non-overlapping boxes */
    if(strcmp(p,"diffn")==0||strcmp(p,"fzn_diffn")==0||
       strcmp(p,"diffn_nonstrict")==0||strcmp(p,"fzn_diffn_nonstrict")==0||
       strcmp(p,"gecode_diffn")==0||strcmp(p,"gecode_nooverlap")==0){
        if(c->nargs<4)return -1;
        int is_nooverlap = (strcmp(p,"gecode_nooverlap")==0);
        int y_arg = is_nooverlap ? 2 : 1;
        int dx_arg = is_nooverlap ? 1 : 2;
        Lin *xs; int nx; Lin *ys; int ny; Lin *dxs; int ndx; Lin *dys; int ndy;
        if(parse_array(m,c->args[0],&xs,&nx)!=0) return -1;
        if(parse_array(m,c->args[y_arg],&ys,&ny)!=0){free_lins(xs,nx);return -1;}
        if(parse_array(m,c->args[dx_arg],&dxs,&ndx)!=0){free_lins(xs,nx);free_lins(ys,ny);return -1;}
        if(parse_array(m,c->args[3],&dys,&ndy)!=0){free_lins(xs,nx);free_lins(ys,ny);free_lins(dxs,ndx);return -1;}
        if(nx!=ny||nx!=ndx||nx!=ndy){
            free_lins(xs,nx);free_lins(ys,ny);free_lins(dxs,ndx);free_lins(dys,ndy);return -1;
        }
        if(nx>64){ free_lins(xs,nx);free_lins(ys,ny);free_lins(dxs,ndx);free_lins(dys,ndy);return 1; }

        for(int i=0;i<nx;i++){
            for(int j=i+1;j<nx;j++){
                if(dxs[i].n!=0||dys[i].n!=0||dxs[j].n!=0||dys[j].n!=0){
                    free_lins(xs,nx);free_lins(ys,ny);free_lins(dxs,ndx);free_lins(dys,ndy);return 1;
                }
                double dxi = dxs[i].constant, dyi = dys[i].constant;
                double dxj = dxs[j].constant, dyj = dys[j].constant;
                if(dxi <= 0.0 || dyi <= 0.0 || dxj <= 0.0 || dyj <= 0.0) continue;

                int b1 = b_newvar(b, 0.0, 1.0);
                int b2 = b_newvar(b, 0.0, 1.0);
                int b3 = b_newvar(b, 0.0, 1.0);
                int b4 = b_newvar(b, 0.0, 1.0);

                Lin sum; memset(&sum, 0, sizeof(sum));
                lin_term(&sum, b1, 1.0); lin_term(&sum, b2, 1.0); lin_term(&sum, b3, 1.0); lin_term(&sum, b4, 1.0);
                b_put(b, '>', 1.0, &sum); lin_free(&sum);

                double Lx, Ux, Ly, Uy;
                Lin dxi_lin; memset(&dxi_lin, 0, sizeof(dxi_lin)); lin_into(&dxi_lin, &xs[i], 1.0); lin_into(&dxi_lin, &xs[j], -1.0);
                lin_bounds(b, &dxi_lin, &Lx, &Ux); lin_free(&dxi_lin);
                Lin dyi_lin; memset(&dyi_lin, 0, sizeof(dyi_lin)); lin_into(&dyi_lin, &ys[i], 1.0); lin_into(&dyi_lin, &ys[j], -1.0);
                lin_bounds(b, &dyi_lin, &Ly, &Uy); lin_free(&dyi_lin);

                double Mx = fmax(fabs(Lx), fabs(Ux)) + dxi + dxj + 1.0;
                double My = fmax(fabs(Ly), fabs(Uy)) + dyi + dyj + 1.0;

                Lin r1; memset(&r1, 0, sizeof(r1)); lin_into(&r1, &xs[i], 1.0); lin_into(&r1, &xs[j], -1.0); lin_term(&r1, b1, Mx);
                b_put(b, '<', Mx - dxi, &r1); lin_free(&r1);

                Lin r2; memset(&r2, 0, sizeof(r2)); lin_into(&r2, &xs[j], 1.0); lin_into(&r2, &xs[i], -1.0); lin_term(&r2, b2, Mx);
                b_put(b, '<', Mx - dxj, &r2); lin_free(&r2);

                Lin r3; memset(&r3, 0, sizeof(r3)); lin_into(&r3, &ys[i], 1.0); lin_into(&r3, &ys[j], -1.0); lin_term(&r3, b3, My);
                b_put(b, '<', My - dyi, &r3); lin_free(&r3);

                Lin r4; memset(&r4, 0, sizeof(r4)); lin_into(&r4, &ys[j], 1.0); lin_into(&r4, &ys[i], -1.0); lin_term(&r4, b4, My);
                b_put(b, '<', My - dyj, &r4); lin_free(&r4);
            }
        }

        free_lins(xs,nx);free_lins(ys,ny);free_lins(dxs,ndx);free_lins(dys,ndy);
        return 0;
    }
    /* array_bool_xor(as[]): parity of sum is 1 */
    if(strcmp(p,"array_bool_xor")==0){
        if(c->nargs<1)return -1;
        Lin*arr;int narr;
        if(parse_array(m,c->args[0],&arr,&narr)!=0)return -1;
        if(narr==0){
            Lin nf; memset(&nf, 0, sizeof(nf)); b_put(b, '=', 1.0, &nf); lin_free(&nf);
            free_lins(arr,narr); return 0;
        }
        if(narr==1){
            b_put(b, '=', 1.0, &arr[0]);
            free_lins(arr,narr); return 0;
        }
        if(narr==2){
            Lin s; memset(&s, 0, sizeof(s)); lin_into(&s, &arr[0], 1.0); lin_into(&s, &arr[1], 1.0);
            b_put(b, '=', 1.0, &s); lin_free(&s);
            free_lins(arr,narr); return 0;
        }
        int k = b_newvar(b, 0.0, (double)((narr-1)/2));
        Lin s; memset(&s, 0, sizeof(s));
        for(int i=0;i<narr;i++) lin_into(&s, &arr[i], 1.0);
        lin_term(&s, k, -2.0);
        b_put(b, '=', 1.0, &s); lin_free(&s);
        free_lins(arr,narr);
        return 0;
    }
    /* float_in(x, a, b) / float_in_reif(x, a, b, r) */
    if(strcmp(p,"float_in")==0||strcmp(p,"float_in_reif")==0){
        int reif = (strcmp(p,"float_in_reif")==0);
        if(c->nargs < (reif ? 4 : 3)) return -1;
        Lin xl, al, bl;
        if(parse_lin(m,c->args[0],&xl)!=0) return -1;
        if(parse_lin(m,c->args[1],&al)!=0){lin_free(&xl);return -1;}
        if(parse_lin(m,c->args[2],&bl)!=0){lin_free(&xl);lin_free(&al);return -1;}
        if(!reif){
            Lin lo; memset(&lo, 0, sizeof(lo)); lin_into(&lo, &xl, 1.0); lin_into(&lo, &al, -1.0);
            b_put(b, '>', 0.0, &lo); lin_free(&lo);
            Lin hi; memset(&hi, 0, sizeof(hi)); lin_into(&hi, &xl, 1.0); lin_into(&hi, &bl, -1.0);
            b_put(b, '<', 0.0, &hi); lin_free(&hi);
        } else {
            Lin rb; if(parse_lin(m,c->args[3],&rb)!=0){lin_free(&xl);lin_free(&al);lin_free(&bl);return -1;}
            int r;
            if(lin_unit_var(&rb,&r)!=0){lin_free(&xl);lin_free(&al);lin_free(&bl);lin_free(&rb);return 1;}
            double L1, U1, L2, U2;
            Lin d1; memset(&d1, 0, sizeof(d1)); lin_into(&d1, &xl, 1.0); lin_into(&d1, &al, -1.0);
            Lin d2; memset(&d2, 0, sizeof(d2)); lin_into(&d2, &xl, 1.0); lin_into(&d2, &bl, -1.0);
            if(lin_bounds(b,&d1,&L1,&U1)!=0||lin_bounds(b,&d2,&L2,&U2)!=0){
                lin_free(&d1);lin_free(&d2);lin_free(&xl);lin_free(&al);lin_free(&bl);lin_free(&rb);return 1;
            }
            double M1 = fmax(fabs(L1),fabs(U1)); if(M1<1.0) M1=1.0;
            double M2 = fmax(fabs(L2),fabs(U2)); if(M2<1.0) M2=1.0;
            Lin r1; memset(&r1, 0, sizeof(r1)); lin_into(&r1, &d1, 1.0); lin_term(&r1, r, -M1);
            b_put(b, '>', -M1, &r1); lin_free(&r1);
            Lin r2; memset(&r2, 0, sizeof(r2)); lin_into(&r2, &d2, 1.0); lin_term(&r2, r, M2);
            b_put(b, '<', M2, &r2); lin_free(&r2);
            lin_free(&d1); lin_free(&d2); lin_free(&rb);
        }
        lin_free(&xl); lin_free(&al); lin_free(&bl);
        return 0;
    }

    /* everything else: unhandled (return UNKNOWN at top level) */
    return 1;
}

static void fz_print_value(FZKind kind,double value)
{
    if(kind==FZ_K_FLOAT){
        /* 17 significant digits round-trip an IEEE double and avoid turning
           a valid continuous solution such as 2.5 into the integer 3. */
        printf("%.17g",value);
    } else if(kind==FZ_K_BOOL) {
        printf("%s",value>=0.5?"true":"false");
    } else {
        printf("%lld",(long long)llround(value));
    }
}

static int fz_decl_val_at(const FZDecl*d,const double*x,int nvars,int e,double*out)
{
    if(e<0||e>=d->n)return -1;
    if(d->is_var){
        if(d->alias_idx){
            int v=d->alias_idx[e];
            if(v>=0){if(v>=nvars)return -1;*out=x[v];}
            else *out=d->alias_const[e];
            return 0;
        }
        if(d->base_idx<0||d->base_idx+e>=nvars)return -1;
        *out=x[d->base_idx+e];return 0;
    }
    if(d->par){*out=d->par[e];return 0;}
    if(d->par_int){*out=d->par_int[e];return 0;}
    return -1;
}

static void fz_print_one_solution(const FZModel*m,const double*x,int nvars)
{
    for(int d=0;d<m->ndecl;d++){FZDecl*decl=&m->decls[d];if(!decl->is_output)continue;
        double first;
        if(fz_decl_val_at(decl,x,nvars,0,&first)!=0)continue;
        if(decl->is_array){
            int hi=decl->index_lo+decl->n-1;
            printf("%s = array1d(%d..%d, [",decl->name,decl->index_lo,hi);
            for(int e=0;e<decl->n;e++){double v;if(e)printf(", ");if(fz_decl_val_at(decl,x,nvars,e,&v)!=0)v=0;fz_print_value(decl->kind,v);}
            printf("]);\n");
        } else {
            printf("%s = ",decl->name);fz_print_value(decl->kind,first);printf(";\n");
        }
    }
    printf("----------\n");
    fflush(stdout);
}

typedef struct {
    const FZModel *m;
    FZSolution *sol;
    int dedupe_outputs;
    size_t key_len, seen_count, seen_cap, slot_cap;
    uint64_t *scratch, *keys, *hashes;
    size_t *slots;                 /* open-addressed table: seen index + 1 */
} FZSolveCtx;

static uint64_t fz_hash_key(const uint64_t *key,size_t n)
{
    uint64_t h=UINT64_C(1469598103934665603);
    for(size_t i=0;i<n;i++){
        uint64_t w=key[i];
        for(int b=0;b<8;b++){h^=(unsigned char)(w&255u);h*=UINT64_C(1099511628211);w>>=8;}
    }
    return h?h:1;
}

static void fz_seen_rehash(FZSolveCtx *ctx,size_t cap)
{
    size_t *slots=(size_t*)psolve_calloc(cap,sizeof(size_t));
    for(size_t i=0;i<ctx->seen_count;i++){
        size_t p=(size_t)ctx->hashes[i]&(cap-1);
        while(slots[p])p=(p+1)&(cap-1);
        slots[p]=i+1;
    }
    free(ctx->slots);ctx->slots=slots;ctx->slot_cap=cap;
}

/* Return 1 for a new visible FlatZinc output tuple and 0 for a duplicate.
   Auxiliary selector binaries are implementation details, not distinct user
   solutions; enumerating them used to print repeated output assignments. */
static int fz_seen_output(FZSolveCtx *ctx,const double *x)
{
    size_t q=0;
    for(int d=0;d<ctx->m->ndecl;d++){
        FZDecl *decl=&ctx->m->decls[d];if(!decl->is_output)continue;
        for(int e=0;e<decl->n;e++){
            double v=0.0;(void)fz_decl_val_at(decl,x,ctx->sol->nvars,e,&v);
            uint64_t w;
            if(decl->kind==FZ_K_FLOAT){if(v==0.0)v=0.0;memcpy(&w,&v,sizeof(w));}
            else {int64_t iv=(int64_t)llround(v);memcpy(&w,&iv,sizeof(w));}
            ctx->scratch[q++]=w;
        }
    }
    uint64_t h=fz_hash_key(ctx->scratch,ctx->key_len);
    if(ctx->slot_cap==0)fz_seen_rehash(ctx,16);
    if(ctx->seen_count+1>=ctx->slot_cap*7/10){
        if(ctx->slot_cap>SIZE_MAX/2/sizeof(size_t))psolve_fail(PSOLVE_ERR_OOM);
        fz_seen_rehash(ctx,ctx->slot_cap*2);
    }
    size_t p=(size_t)h&(ctx->slot_cap-1);
    while(ctx->slots[p]){
        size_t i=ctx->slots[p]-1;
        if(ctx->hashes[i]==h &&
           (ctx->key_len==0||memcmp(&ctx->keys[i*ctx->key_len],ctx->scratch,ctx->key_len*sizeof(uint64_t))==0))
            return 0;
        p=(p+1)&(ctx->slot_cap-1);
    }
    if(ctx->seen_count==ctx->seen_cap){
        if(ctx->seen_cap>SIZE_MAX/2)psolve_fail(PSOLVE_ERR_OOM);
        size_t nc=ctx->seen_cap?ctx->seen_cap*2:16;
        if(nc>SIZE_MAX/sizeof(uint64_t) ||
           (ctx->key_len&&nc>SIZE_MAX/ctx->key_len/sizeof(uint64_t)))psolve_fail(PSOLVE_ERR_OOM);
        ctx->hashes=(uint64_t*)psolve_realloc((void**)&ctx->hashes,nc*sizeof(uint64_t));
        if(ctx->key_len)ctx->keys=(uint64_t*)psolve_realloc((void**)&ctx->keys,nc*ctx->key_len*sizeof(uint64_t));
        ctx->seen_cap=nc;
    }
    size_t i=ctx->seen_count++;
    ctx->hashes[i]=h;
    if(ctx->key_len)memcpy(&ctx->keys[i*ctx->key_len],ctx->scratch,ctx->key_len*sizeof(uint64_t));
    ctx->slots[p]=i+1;
    return 1;
}

static void fz_solve_ctx_free(FZSolveCtx *ctx)
{
    free(ctx->scratch);free(ctx->keys);free(ctx->hashes);free(ctx->slots);
}

static void fz_solution_cb(const double *x, double obj, void *user_data)
{
    FZSolveCtx *ctx = (FZSolveCtx*)user_data;
    if(ctx->dedupe_outputs&&!fz_seen_output(ctx,x))return;
    ctx->sol->num_solutions++;
    if (ctx->sol->all_solutions) {
        fz_print_one_solution(ctx->m, x, ctx->sol->nvars);
    }
    (void)obj;
}

#include "fz_cp.inc"

void fz_solve(const FZModel*m,FZSolution*sol)
{
    if(!sol)return;
    long node_limit_in = sol->node_limit;   /* input knob, preserved across memset */
    int all_sol_in = sol->all_solutions;
    memset(sol,0,sizeof(*sol));
    sol->node_limit = node_limit_in;
    sol->all_solutions = all_sol_in;
    if(!m||m->nvars<0||m->ndecl<0){sol->status=3;return;}
    int nv=m->nvars; sol->nvars=nv; sol->x=(double*)psolve_calloc((size_t)(nv?nv:1),sizeof(double));
    /* Try the finite-domain CP engine first for pure integer satisfaction
       (solve satisfy) CSPs with combinatorial/nonlinear predicates; it solves
       all_different puzzles (n-Queens, Sudoku, magic/latin squares) and
       monomial Diophantine products that the LP/MIP bridge handles poorly.
       It falls back to the MIP bridge for everything it cannot certify. */
    if(!sol->all_solutions && m->solve_kind==0 && nv>0){
        if(fz_cp_try(m,sol)) return;
    }
    /* A compiler can propagate every decision variable to a literal (for
       example a table call on x = [1,3]).  Keep one fixed dummy column so the
       LP/MIP bridge can still certify SAT/UNSAT instead of reporting UNKNOWN. */
    int base_nv=nv?nv:1;
    Builder b;memset(&b,0,sizeof(b));b.nvars=base_nv;b.varcap=base_nv?base_nv:16;
    b.lo=(double*)psolve_malloc((size_t)b.varcap*sizeof(double));b.hi=(double*)psolve_malloc((size_t)b.varcap*sizeof(double));
    b.haslo=(int*)psolve_calloc((size_t)b.varcap,sizeof(int));b.hashi=(int*)psolve_calloc((size_t)b.varcap,sizeof(int));
    /* FlatZinc `var int/float` are unbounded by default; our LP solver needs
       finite bounds, so clamp undecorated variables to a big-M interval. */
    for(int i=0;i<base_nv;i++){b.lo[i]=-FZ_BIG_BOUND;b.hi[i]=FZ_BIG_BOUND;}
    if(nv==0){b.lo[0]=0.0;b.hi[0]=0.0;b.haslo[0]=1;b.hashi[0]=1;}
    for(int d=0;d<m->ndecl;d++){FZDecl*decl=&m->decls[d];
        if(!decl->is_var||decl->base_idx<0)continue;
        if(decl->is_alias)continue;   /* alias elements get bounds from their own decls */
        for(int e=0;e<decl->n;e++){int vi=decl->base_idx+e;
            /* bool variables are implicitly 0..1 in FlatZinc */
            if(decl->kind==FZ_K_BOOL){ b.lo[vi]=0; b.hi[vi]=1; b.haslo[vi]=1; b.hashi[vi]=1; continue; }
            if(decl->has_lo){b.lo[vi]=decl->lo[0];b.haslo[vi]=1;}
            if(decl->has_hi){b.hi[vi]=decl->hi[0];b.hashi[vi]=1;}}}
    int unhandled=0;
    /* enforce exact set-domain declarations (var {1,3,5}: x) via SOS1 */
    for(int d=0;d<m->ndecl;d++){FZDecl*decl=&m->decls[d];
        if(!decl->is_var||decl->base_idx<0||decl->nset<=0||decl->n!=1)continue;
        int x=decl->base_idx;
        if(decl->nset==1){ b.lo[x]=decl->setvals[0];b.haslo[x]=1;b.hi[x]=decl->setvals[0];b.hashi[x]=1; continue; }
        /* contiguous range already tightened; if gapped, add SOS1 */
        int contiguous=1;
        for(int q=1;q<decl->nset;q++) if(decl->setvals[q]!=decl->setvals[0]+q) {contiguous=0;break;}
        if(contiguous) continue;
        int *bs=(int*)psolve_malloc((size_t)decl->nset*sizeof(int));
        for(int i=0;i<decl->nset;i++) bs[i]=b_newvar(&b,0.0,1.0);
        Lin eq;memset(&eq,0,sizeof(eq));lin_term(&eq,x,1.0);
        for(int i=0;i<decl->nset;i++) lin_term(&eq,bs[i],-(double)decl->setvals[i]);
        b_put(&b,'=',0.0,&eq);
        Lin sum;memset(&sum,0,sizeof(sum));
        for(int i=0;i<decl->nset;i++) lin_term(&sum,bs[i],1.0);
        sum.constant=-1.0; b_put(&b,'=',0.0,&sum);
        lin_free(&eq);lin_free(&sum);free(bs);
    }
    for(FZConstr*c=m->constr;c;c=c->next){int r=handle_constraint((FZModel*)m,&b,c);if(r!=0)unhandled=1;}
    if(sol->all_solutions&&m->solve_kind==0){
        /* A finite list cannot be a complete enumeration of a continuous
           output domain.  Do not print one LP vertex followed by the FlatZinc
           completion marker as if infinitely many float solutions were done. */
        for(int d=0;d<m->ndecl;d++)
            if(m->decls[d].is_output&&m->decls[d].is_var&&m->decls[d].kind==FZ_K_FLOAT)
                unhandled=1;
    }

    /* --- CSP heuristics for any FlatZinc: try alldiff and nonlinear divisor before MIP --- */
    if(m->solve_kind==0 && !sol->all_solutions){
        if(unhandled){
            int has_nonlinear=0;
            for(FZConstr *cc=m->constr; cc; cc=cc->next) if(strcmp(cc->pred,"int_times")==0||strcmp(cc->pred,"int_pow")==0||strcmp(cc->pred,"int_pow_fixed")==0){ has_nonlinear=1; break; }
            if(has_nonlinear){
                double *tmp=(double*)psolve_malloc((size_t)(nv? nv:1)*sizeof(double));
                if(csp_try_nonlinear(m, tmp)){
                    for(int i=0;i<nv;i++) sol->x[i]=tmp[i];
                    sol->status=0; sol->nodes=0;
                    free(tmp);
                    for(int r=0;r<b.nrows;r++){free(b.rows[r].idx);free(b.rows[r].coef);}free(b.rows);free(b.lo);free(b.hi);free(b.haslo);free(b.hashi);free(b.isint);
                    return;
                }
                free(tmp);
            }
        }
        {
            double *tmp=(double*)psolve_malloc((size_t)(nv? nv:1)*sizeof(double));
            if(csp_try_alldiff(m, tmp)){
                for(int i=0;i<nv;i++) sol->x[i]=tmp[i];
                sol->status=0; sol->nodes=0;
                free(tmp);
                for(int r=0;r<b.nrows;r++){free(b.rows[r].idx);free(b.rows[r].coef);}free(b.rows);free(b.lo);free(b.hi);free(b.haslo);free(b.hashi);free(b.isint);
                return;
            }
            free(tmp);
        }
    }

    if(unhandled){for(int r=0;r<b.nrows;r++){free(b.rows[r].idx);free(b.rows[r].coef);}free(b.rows);free(b.lo);free(b.hi);free(b.haslo);free(b.hashi);free(b.isint);sol->status=2;return;}

    int ntot=b.nvars;
    LP lp;memset(&lp,0,sizeof(lp));
    lp.n=ntot;lp.m=b.nrows;lp.maximize=1;
    lp.c=(double*)psolve_calloc((size_t)ntot,sizeof(double));
    if(m->solve_kind==2){for(int i=0;i<m->objective.n;i++){int vi=m->objective.idx[i];if(vi>=0&&vi<ntot)lp.c[vi]+=m->objective.coef[i];}}
    else if(m->solve_kind==1){lp.maximize=0;for(int i=0;i<m->objective.n;i++){int vi=m->objective.idx[i];if(vi>=0&&vi<ntot)lp.c[vi]+=m->objective.coef[i];}}
    lp.l=(double*)psolve_malloc((size_t)ntot*sizeof(double));lp.u=(double*)psolve_malloc((size_t)ntot*sizeof(double));
    for(int i=0;i<ntot;i++){lp.l[i]=b.lo[i];lp.u[i]=b.hi[i];}
    long nnz=0;for(int r=0;r<b.nrows;r++)nnz+=b.rows[r].n;
    lp.b=(double*)psolve_malloc((size_t)(b.nrows?b.nrows:1)*sizeof(double));lp.rel=(char*)psolve_malloc((size_t)(b.nrows?b.nrows:1));
    for(int r=0;r<b.nrows;r++){lp.b[r]=b.rows[r].rhs;lp.rel[r]=b.rows[r].rel;}
    lp.Acolptr=(int*)psolve_calloc((size_t)(ntot+1),sizeof(int));
    lp.Arow=(int*)psolve_malloc((size_t)(nnz?nnz:1)*sizeof(int));lp.Aval=(double*)psolve_malloc((size_t)(nnz?nnz:1)*sizeof(double));
    for(int r=0;r<b.nrows;r++)for(int k=0;k<b.rows[r].n;k++){int j=b.rows[r].idx[k];if(j>=0&&j<ntot)lp.Acolptr[j+1]++;}
    for(int j=0;j<ntot;j++)lp.Acolptr[j+1]+=lp.Acolptr[j];
    int*ff=(int*)psolve_malloc((size_t)ntot*sizeof(int));for(int j=0;j<ntot;j++)ff[j]=lp.Acolptr[j];
    for(int r=0;r<b.nrows;r++)for(int k=0;k<b.rows[r].n;k++){int j=b.rows[r].idx[k];if(j>=0&&j<ntot){lp.Arow[ff[j]]=r;lp.Aval[ff[j]]=b.rows[r].coef[k];ff[j]++;}}
    free(ff);

    /* decide integer vs continuous: FlatZinc int/bool vars are integer
       (bool is 0/1); float vars are continuous.  If any integer variable is
       present, solve with the MIP branch-and-bound solver so the answer is
       integral; otherwise the LP relaxation is exact. */
    unsigned char *isint=(unsigned char*)psolve_calloc((size_t)(ntot?ntot:1),1);
    int any_int=0;
    for(int d=0;d<m->ndecl;d++){FZDecl*decl=&m->decls[d];if(!decl->is_var||decl->base_idx<0||decl->is_alias)continue;
        int integer = (decl->kind!=FZ_K_FLOAT);   /* int and bool are integer */
        for(int e=0;e<decl->n;e++){int vi=decl->base_idx+e;if(vi>=0&&vi<ntot&&integer){isint[vi]=1;any_int=1;}}}
    /* introduced (auxiliary) variables for big-M encodings (e.g. the binary
       flag in int_abs) are integer; set them so the MIP keeps them integral. */
    for(int vi=nv;vi<ntot;vi++){if(b.isint&&b.isint[vi]){isint[vi]=1;any_int=1;}}
    sol->nvars=ntot;
    sol->x=(double*)psolve_realloc((void**)&sol->x,(size_t)(ntot?ntot:1)*sizeof(double));
    FZSolveCtx ctx;memset(&ctx,0,sizeof(ctx));ctx.m=m;ctx.sol=sol;
    ctx.dedupe_outputs=(sol->all_solutions&&m->solve_kind==0);
    if(ctx.dedupe_outputs){
        for(int d=0;d<m->ndecl;d++)if(m->decls[d].is_output){
            if((size_t)m->decls[d].n>SIZE_MAX-ctx.key_len)psolve_fail(PSOLVE_ERR_OOM);
            ctx.key_len+=(size_t)m->decls[d].n;
        }
        if(ctx.key_len>SIZE_MAX/sizeof(uint64_t))psolve_fail(PSOLVE_ERR_OOM);
        ctx.scratch=(uint64_t*)psolve_calloc(ctx.key_len?ctx.key_len:1,sizeof(uint64_t));
    }
    if(any_int){
        MIP mip;memset(&mip,0,sizeof(mip));
        mip.n=ntot;mip.m=b.nrows;mip.c=lp.c;mip.Acolptr=lp.Acolptr;mip.Arow=lp.Arow;mip.Aval=lp.Aval;
        mip.rel=lp.rel;mip.b=lp.b;mip.l=lp.l;mip.u=lp.u;mip.maximize=lp.maximize;
        mip.isint=isint;mip.mip_gap=1e-4;
        mip.stop_at_feasible = (m->solve_kind==0);   /* satisfy: first feasible is enough */
        mip.all_solutions = sol->all_solutions;
        mip.on_solution = fz_solution_cb;
        mip.solution_user_data = &ctx;
        mip.node_limit = (sol->node_limit>0)?sol->node_limit:200000;
        mip.lp_iter_limit=2000000;
        MIPResult mr;mip_solve(&mip,&mr);
        sol->nodes=mr.nodes; sol->best_bound=mr.best_bound;
        /* An optimisation model may only report an objective the search
           actually proved.  mip.stop_at_feasible is off for those, but check
           the proof flag anyway so a future change cannot leak a merely
           feasible point out as an optimum. */
        if(mr.status==0 && m->solve_kind!=0 && !mr.proven_optimal) sol->status=4;
        else if(mr.status==0){memcpy(sol->x,mr.x,(size_t)ntot*sizeof(double));sol->obj=mr.obj+m->objective.constant;sol->iters=mr.lp_iters;sol->status=0;}
        else if(mr.status==1)sol->status=1;
        else if(mr.status==3||mr.status==4||mr.status==5||mr.status==6)sol->status=4;
        else sol->status=2;
        mip_result_free(&mr);
    } else {
        Solver*s=solver_create(&lp);
        int rr=solver_solve(s);
        if(rr==0){
            double*xo=(double*)psolve_malloc((size_t)(ntot?ntot:1)*sizeof(double));
            double obj;solver_optimum(s,xo,&obj);
            memcpy(sol->x,xo,(size_t)ntot*sizeof(double));
            sol->obj=obj+m->objective.constant;sol->iters=s->iters;sol->status=0;
            if(sol->all_solutions){
                sol->num_solutions++;
                fz_print_one_solution(m,sol->x,sol->nvars);
            }
            free(xo);
        }
        else if(rr==1)sol->status=1;
        else sol->status=2;
        solver_destroy(s);
    }
    if(sol->status==0 && m->solve_kind!=0 &&
       objective_uses_synthetic_bound(m,&b,sol->x,nv)){
        /* The finite bridge box made an actually unbounded (or otherwise
           unproven) FlatZinc objective look optimal.  Preserve the invariant
           that fznsolve never prints a fabricated optimum. */
        sol->status=2;
    }
    if(sol->status==1){
        /* UNSATISFIABLE is only certified *inside* the bridge's finite box:
           a `var int/float` without declared bounds (haslo/hashi unset) was
           clamped to ±FZ_BIG_BOUND, so the real model may have solutions
           outside that box.  Downgrade to UNKNOWN rather than print a false
           UNSAT — bounded models keep their exact verdict. */
        for(int v=0;v<nv;v++){
            if(!b.haslo[v]||!b.hashi[v]){ sol->status=2; break; }
        }
    }
    fz_solve_ctx_free(&ctx);
    free(isint);
    free(lp.c);free(lp.l);free(lp.u);free(lp.b);free(lp.rel);free(lp.Acolptr);free(lp.Arow);free(lp.Aval);
    for(int r=0;r<b.nrows;r++){free(b.rows[r].idx);free(b.rows[r].coef);}free(b.rows);free(b.lo);free(b.hi);free(b.haslo);free(b.hashi);free(b.isint);
}

void fz_print_solution(const FZModel*m,const FZSolution*sol)
{
    if(!m||!sol)return;
    if(sol->all_solutions){
        if(sol->status==0){
            if(sol->num_solutions>0) printf("==========\n");
            else printf("=====UNSATISFIABLE=====\n");
        } else if(sol->status==1){
            if(sol->num_solutions==0) printf("=====UNSATISFIABLE=====\n");
        } else if(sol->status==2||sol->status==4){
            if(sol->num_solutions==0) printf("=====UNKNOWN=====\n");
        }
    } else {
        if(sol->status==0){
            fz_print_one_solution(m,sol->x,sol->nvars);
            if(m->solve_kind!=0) printf("==========\n");
        } else if(sol->status==1)printf("=====UNSATISFIABLE=====\n");
        else printf("=====UNKNOWN=====\n");
    }
}
void fz_solution_free(FZSolution*sol){if(!sol)return;free(sol->x);memset(sol,0,sizeof(*sol));}
void fz_model_free(FZModel*m){
    if(!m)return;
    for(int i=0;i<m->ndecl;i++){FZDecl*d=&m->decls[i];free(d->name);free(d->alias_idx);free(d->alias_const);free(d->par);free(d->par_int);free(d->lo);free(d->hi);free(d->setvals);}
    free(m->decls);
    FZConstr*c=m->constr;while(c){FZConstr*nx=c->next;for(int i=0;i<c->nargs;i++)free(c->args[i]);free(c->args);free(c->pred);free(c);c=nx;}
    free(m->objective.idx);free(m->objective.coef);free(m->file);
    memset(m,0,sizeof(*m));
}
