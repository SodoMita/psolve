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
    int cap=256,cnt=0; Token*t=(Token*)malloc((size_t)cap*sizeof(Token));
    size_t i=0,L=strlen(src);
    while(i<L){
        char c=src[i];
        if(isspace((unsigned char)c)){i++;continue;}
        if(c=='%'){while(i<L&&src[i]!='\n')i++;continue;}
        if(isalpha((unsigned char)c)||c=='_'){
            size_t s=i; while(i<L&&is_ident_ch((unsigned char)src[i]))i++;
            if(cnt>=cap){cap*=2;t=(Token*)realloc(t,(size_t)cap*sizeof(Token));}
            t[cnt].kind=TK_IDENT;t[cnt].text=strndup(src+s,i-s);t[cnt].start=s;t[cnt].end=i;cnt++;continue;
        }
        if(isdigit((unsigned char)c)||(c=='-'&&i+1<L&&isdigit((unsigned char)src[i+1]))){
            size_t s=i; int isfloat=0; if(src[i]=='-')i++;
            while(i<L&&isdigit((unsigned char)src[i]))i++;
            if(i<L&&src[i]=='.'&&i+1<L&&isdigit((unsigned char)src[i+1])){isfloat=1;i++;while(i<L&&isdigit((unsigned char)src[i]))i++;}
            if(i<L&&(src[i]=='e'||src[i]=='E')){isfloat=1;i++;if(i<L&&(src[i]=='+'||src[i]=='-'))i++;while(i<L&&isdigit((unsigned char)src[i]))i++;}
            if(cnt>=cap){cap*=2;t=(Token*)realloc(t,(size_t)cap*sizeof(Token));}
            char*tmp=strndup(src+s,i-s);
            if(isfloat){t[cnt].kind=TK_FLOAT;t[cnt].fval=atof(tmp);}else{t[cnt].kind=TK_INT;t[cnt].ival=atol(tmp);}
            free(tmp);t[cnt].text=NULL;t[cnt].start=s;t[cnt].end=i;cnt++;continue;
        }
        if(c=='"'){
            size_t s=i;i++;while(i<L&&src[i]!='"'){if(src[i]=='\\')i++;i++;}if(i<L)i++;
            if(cnt>=cap){cap*=2;t=(Token*)realloc(t,(size_t)cap*sizeof(Token));}
            t[cnt].kind=TK_STRING;t[cnt].text=strndup(src+s+1,i-s-2);t[cnt].start=s;t[cnt].end=i;cnt++;continue;
        }
        const char*two[]={"::","->","<-","..","{","}","[","]","(",")",",",";",":",".","=","+","-",0};
        int matched=0;
        for(int k=0;two[k];k++){ size_t Lk=strlen(two[k]);
            if(i+Lk<=L&&strncmp(src+i,two[k],Lk)==0){
                if(cnt>=cap){cap*=2;t=(Token*)realloc(t,(size_t)cap*sizeof(Token));}
                t[cnt].kind=TK_SYM;t[cnt].text=strdup(two[k]);t[cnt].start=i;t[cnt].end=i+Lk;cnt++;i+=Lk;matched=1;break; } }
        if(!matched){free_toks(t,cnt);return -1;}
    }
    if(cnt>=cap){cap++;t=(Token*)realloc(t,(size_t)cap*sizeof(Token));}
    t[cnt].kind=TK_EOF;t[cnt].text=NULL;t[cnt].start=L;t[cnt].end=L;cnt++;
    *out=t;*n=cnt;return 0;
}

/* ------------------------------------------------------------------ */
/* Linear-form helpers                                                 */
/* ------------------------------------------------------------------ */
typedef struct { int n; int*idx; double*coef; double constant; } Lin;

static void lin_term(Lin*l,int idx,double c){
    for(int i=0;i<l->n;i++) if(l->idx[i]==idx){l->coef[i]+=c;return;}
    l->idx=(int*)realloc(l->idx,(size_t)(l->n+1)*sizeof(int));
    l->coef=(double*)realloc(l->coef,(size_t)(l->n+1)*sizeof(double));
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
    Expr e; memset(&e,0,sizeof(e)); e.n=1; e.els=(Lin*)malloc(sizeof(Lin)); memset(&e.els[0],0,sizeof(Lin));
    while(isspace((unsigned char)s[*pos]))(*pos)++;
    char c=s[*pos];
    if(c=='-'){(*pos)++;Expr t=parse_primary(s,pos,m,err);if(!*err){for(int i=0;i<t.n;i++){for(int k=0;k<t.els[i].n;k++)t.els[i].coef[k]=-t.els[i].coef[k];t.els[i].constant=-t.els[i].constant;}}free(e.els);e=t;return e;}
    if(c=='['){(*pos)++;int cap=8;free(e.els);e.els=(Lin*)malloc((size_t)cap*sizeof(Lin));e.n=0;e.is_array=1;
        while(1){while(isspace((unsigned char)s[*pos]))(*pos)++;if(s[*pos]==']'){(*pos)++;break;}
            Expr t=parse_expr(s,pos,m,err);if(*err){expr_free2(&t);expr_free2(&e);return e;}
            if(e.n>=cap){cap*=2;e.els=(Lin*)realloc(e.els,(size_t)cap*sizeof(Lin));}
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
                        *out=(Lin*)malloc((size_t)(d->n?d->n:1)*sizeof(Lin));
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
                        *out=(Lin*)malloc((size_t)(d->n?d->n:1)*sizeof(Lin));
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

/* ------------------------------------------------------------------ */
/* Parser                                                              */
/* ------------------------------------------------------------------ */
static FZDecl*add_decl(FZModel*m){
    if(m->ndecl>=m->cap_decl){m->cap_decl=m->cap_decl?m->cap_decl*2:16;m->decls=(FZDecl*)realloc(m->decls,(size_t)m->cap_decl*sizeof(FZDecl));}
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
    memset(m,0,sizeof(*m));
    FILE*f=fopen(path,"r");if(!f){fprintf(stderr,"cannot open %s\n",path);return -1;}
    fseek(f,0,SEEK_END);long sz=ftell(f);fseek(f,0,SEEK_SET);
    char*src=(char*)malloc((size_t)(sz+1));size_t rd=fread(src,1,(size_t)sz,f);src[rd]=0;fclose(f);
    m->file=strdup(path);
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
                    FZConstr*c=(FZConstr*)calloc(1,sizeof(FZConstr)); c->pred=strdup(toks[ti].text);ti++;
                    if(ti<nt&&toks[ti].kind==TK_SYM&&strcmp(toks[ti].text,"(")==0)ti++;
                    int na=0,cap=8;c->args=(char**)malloc((size_t)cap*sizeof(char*));
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
                        if(na>=cap){cap*=2;c->args=(char**)realloc(c->args,(size_t)cap*sizeof(char*));}
                        c->args[na++]=strndup(src+s,e-s);
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
                    char*ob=strndup(src+s,e-s);
                    Lin ol;memset(&ol,0,sizeof(ol));
                    if(parse_lin(m,ob,&ol)!=0){m->solve_kind=0;free(ob);ti=(j<nt?j+1:j);continue;}
                    /* copy local Lin into FZLin objective */
                    m->objective.n=ol.n;
                    m->objective.idx=(int*)malloc((size_t)(ol.n?ol.n:1)*sizeof(int));
                    m->objective.coef=(double*)malloc((size_t)(ol.n?ol.n:1)*sizeof(double));
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
                        d->lo=(double*)malloc(sizeof(double));d->hi=(double*)malloc(sizeof(double));
                        d->lo[0]=(double)vmin; d->hi[0]=(double)vmax;
                        /* store exact set for SOS1 enforcement in fz_solve */
                        d->nset=nv; d->setvals=(long*)malloc((size_t)nv*sizeof(long));
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
                    d->has_lo=1;d->has_hi=1;d->lo=(double*)malloc(sizeof(double));d->hi=(double*)malloc(sizeof(double));
                    d->lo[0]=lo;d->hi[0]=hi;
                }
                if(ti<nt&&toks[ti].kind==TK_SYM&&strcmp(toks[ti].text,":")==0)ti++;
                if(ti<nt&&toks[ti].kind==TK_IDENT){d->name=strdup(toks[ti].text);ti++;}
                /* annotations may appear before the '=' (e.g. :: output_array).
                   Handle output markers and integer domain (:: lo..hi). */
                while(ti<nt&&toks[ti].kind==TK_SYM&&strcmp(toks[ti].text,"::")==0){
                    size_t as=toks[ti].end;int aj=ti+1;size_t ae=(ti+1<nt)?toks[ti+1].end:as;
                    while(aj<nt&&!(toks[aj].kind==TK_SYM&&(strcmp(toks[aj].text,";")==0||strcmp(toks[aj].text,"=")==0||strcmp(toks[aj].text,"::")==0))){ae=toks[aj].end;aj++;}
                    char*ann=strndup(src+as,ae-as);
                    if(strstr(ann,"output_var")||strstr(ann,"output_array"))d->is_output=1;
                    /* domain :: lo..hi (including decimal endpoints) */
                    if(!d->has_lo){
                        double lo,hi;
                        if(annotation_range(ann,&lo,&hi)==0){
                            d->has_lo=1;d->has_hi=1;
                            d->lo=(double*)malloc(sizeof(double));d->hi=(double*)malloc(sizeof(double));
                            d->lo[0]=lo;d->hi[0]=hi;
                        }
                    }
                    free(ann);
                    ti=aj;
                }
                if(ti<nt&&toks[ti].kind==TK_SYM&&strcmp(toks[ti].text,"=")==0){
                    ti++;size_t s=toks[ti].start;int j=ti;size_t e=toks[ti].end;
                    while(j<nt&&!(toks[j].kind==TK_SYM&&strcmp(toks[j].text,";")==0)){e=toks[j].end;j++;}
                    char*rhs=strndup(src+s,e-s);
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
                        d->alias_idx=(int*)calloc((size_t)(narr?narr:1),sizeof(int));
                        d->alias_const=(double*)calloc((size_t)(narr?narr:1),sizeof(double));
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
                        d->n=nel;d->par=(double*)calloc((size_t)nel,sizeof(double));d->par_int=(int*)calloc((size_t)nel,sizeof(int));
                        Lin*arr;int narr;
                        if(parse_array(m,rhs,&arr,&narr)==0&&narr==nel){for(int q=0;q<nel;q++){d->par[q]=arr[q].constant;d->par_int[q]=(int)llround(arr[q].constant);}free_lins(arr,narr);}
                    } else if(is_var){
                        d->n=1;
                        Lin l;memset(&l,0,sizeof(l));
                        if(parse_lin(m,rhs,&l)!=0 ||
                           !(l.n==0 || (l.n==1&&fabs(l.coef[0]-1.0)<=1e-12&&fabs(l.constant)<=1e-12))){
                            lin_free(&l);free(rhs);free_toks(toks,nt);free(src);return -1;
                        }
                        d->is_alias=1;d->alias_idx=(int*)calloc(1,sizeof(int));d->alias_const=(double*)calloc(1,sizeof(double));
                        if(l.n==0){d->alias_idx[0]=-1;d->alias_const[0]=l.constant;d->base_idx=-1;}
                        else {d->alias_idx[0]=l.idx[0];d->base_idx=l.idx[0];}
                        lin_free(&l);
                    } else {
                        d->n=1;d->par=(double*)calloc(1,sizeof(double));d->par_int=(int*)calloc(1,sizeof(int));
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
                    char*ann=strndup(src+s,e-s);
                    if(strstr(ann,"output_var")||strstr(ann,"output_array"))d->is_output=1;
                    /* domain like "lo..hi", with decimal endpoints allowed */
                    {
                        double lo,hi;
                        if(annotation_range(ann,&lo,&hi)==0){
                            d->has_lo=1;d->has_hi=1;
                            d->lo=(double*)malloc(sizeof(double));d->hi=(double*)malloc(sizeof(double));
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
typedef struct { int nvars;int varcap;double*lo,*hi;int*haslo,*hashi;Row*rows;int nrows,cap; } Builder;

/* The LP core needs a finite starting box.  This is only a bridge sentinel,
   never a mathematical FlatZinc bound; optimization results that rely on it
   are reported as UNKNOWN rather than a fictitious optimum at +/-1e9. */
#define FZ_BIG_BOUND 1e9
#define FZ_BIG_HIT_TOL 1e-5
static int b_newvar(Builder*b, double lo, double hi){
    if(b->nvars>=b->varcap){
        int nc=b->varcap?b->varcap*2:16;
        psolve_realloc((void**)&b->lo,(size_t)nc*sizeof(double));
        psolve_realloc((void**)&b->hi,(size_t)nc*sizeof(double));
        psolve_realloc((void**)&b->haslo,(size_t)nc*sizeof(int));
        psolve_realloc((void**)&b->hashi,(size_t)nc*sizeof(int));
        b->varcap=nc;
    }
    int v=b->nvars++;
    b->lo[v]=lo;b->hi[v]=hi;b->haslo[v]=1;b->hashi[v]=1;
    return v;
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
        else return -1;
        return 0;
    }
    if(which==0)return add_int_ne(b,d);
    if(which==1)b_put(b,'>',1.0,d);
    else if(which==2)b_put(b,'>',0.0,d);
    else if(which==3)b_put(b,'<',-1.0,d);
    else if(which==4)b_put(b,'<',0.0,d);
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
        if(strcmp(p,"int_lin_le")==0||strcmp(p,"bool_lin_le")==0||strcmp(p,"float_lin_le")==0) rel='<';
        else if(strcmp(p,"int_lin_lt")==0||strcmp(p,"bool_lin_lt")==0){rel='<';rhs=-1.0;}
        else if(strcmp(p,"int_lin_ge")==0||strcmp(p,"bool_lin_ge")==0||strcmp(p,"float_lin_ge")==0) rel='>';
        else if(strcmp(p,"int_lin_gt")==0||strcmp(p,"bool_lin_gt")==0){rel='>';rhs=1.0;}
        b_put(b,rel,rhs,&lin);
        lin_free(&lin);lin_free(&d);free_lins(arr,narr);free_lins(parr,nparr);
        return 0;
    }
    if(strcmp(p,"int_eq")==0||strcmp(p,"bool_eq")==0||strcmp(p,"float_eq")==0||
       strcmp(p,"int_le")==0||strcmp(p,"bool_le")==0||strcmp(p,"float_le")==0||
       strcmp(p,"int_ge")==0||strcmp(p,"bool_ge")==0||strcmp(p,"float_ge")==0||
       strcmp(p,"int_lt")==0||strcmp(p,"bool_lt")==0||strcmp(p,"int_gt")==0||strcmp(p,"bool_gt")==0){
        if(c->nargs<2)return -1;
        if(parse_lin(m,c->args[0],&l1)!=0)return -1;
        if(parse_lin(m,c->args[1],&l2)!=0){lin_free(&l1);return -1;}
        Lin dd;memset(&dd,0,sizeof(dd));
        lin_into(&dd,&l1,1.0);lin_into(&dd,&l2,-1.0);
        char rel='='; double rhs=0.0;
        if(strcmp(p,"int_le")==0||strcmp(p,"bool_le")==0||strcmp(p,"float_le")==0)rel='<';
        else if(strcmp(p,"int_ge")==0||strcmp(p,"bool_ge")==0||strcmp(p,"float_ge")==0)rel='>';
        else if(strcmp(p,"int_lt")==0||strcmp(p,"bool_lt")==0){rel='<';rhs=-1.0;}
        else if(strcmp(p,"int_gt")==0||strcmp(p,"bool_gt")==0){rel='>';rhs=1.0;}
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
        /* t = sum(pos) - sum(neg) + n_neg = number of true literals */
        Lin t;memset(&t,0,sizeof(t));
        int nli=0;
        for(int i=0;i<np;i++){if(pos[i].n==1){lin_term(&t,pos[i].idx[0],1.0);nli++;}}
        for(int i=0;i<ng;i++){if(neg[i].n==1){lin_term(&t,neg[i].idx[0],-1.0);nli++;t.constant+=1.0;}}
        if(reified){
            /* r <= t ;  r >= t/nli */
            Lin d1;memset(&d1,0,sizeof(d1));lin_term(&d1,r,1.0);lin_into(&d1,&t,-1.0);b_put(b,'<',0.0,&d1);
            Lin d2;memset(&d2,0,sizeof(d2));lin_term(&d2,r,1.0);
            if(nli>0)for(int i=0;i<t.n;i++)lin_term(&d2,t.idx[i],-1.0/(double)nli);
            d2.constant = -t.constant/(double)nli;
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
           for OR:  r >= x_i (all), r <= sum */
        Lin sum;memset(&sum,0,sizeof(sum));lin_term(&sum,r,0.0);
        int nvars=0;
        for(int i=0;i<narr;i++){ if(arr[i].n==1){lin_term(&sum,arr[i].idx[0],1.0);nvars++;} }
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
    /* int_neg/float_neg(a,b): b = -a */
    if(strcmp(p,"int_neg")==0||strcmp(p,"float_neg")==0){
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
       (par); if both are variables it is bilinear -> unhandled (UNKNOWN). */
    if(strcmp(p,"int_times")==0||strcmp(p,"float_times")==0){
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
        }
        lin_free(&l1);lin_free(&l2);lin_free(&l3);
        return 1;   /* bilinear -> unhandled */
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
    if(strcmp(p,"int_abs")==0){
        if(c->nargs<2)return -1;
        if(parse_lin(m,c->args[0],&l1)!=0)return -1;
        if(parse_lin(m,c->args[1],&l2)!=0){lin_free(&l1);return -1;}
        if(l1.n!=1||l2.n!=1){lin_free(&l1);lin_free(&l2);return 1;}
        int x=l1.idx[0], y=l2.idx[0];
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
    if(strcmp(p,"int_max")==0||strcmp(p,"int_min")==0){
        int ismax=(strcmp(p,"int_max")==0);
        if(c->nargs<3)return -1;
        if(parse_lin(m,c->args[0],&l1)!=0)return -1;
        if(parse_lin(m,c->args[1],&l2)!=0){lin_free(&l1);return -1;}
        if(parse_lin(m,c->args[2],&l3)!=0){lin_free(&l1);lin_free(&l2);return -1;}
        if(l1.n!=1||l2.n!=1||l3.n!=1){lin_free(&l1);lin_free(&l2);lin_free(&l3);return 1;}
        int a=l1.idx[0], bb=l2.idx[0], mm=l3.idx[0];
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
    /* set_in(x, {set of ints}): x must be one of the values.  Only handle
       when x is a variable and the set has values -> add as box bounds if
       contiguous or as a union; for a discrete set use x in {v1,v2,..} via
       x - vk (rel) via a disjunction which we cannot model linearly without
       binaries.  For a single value or contiguous range we handle it; else
       UNKNOWN. */
    if(strcmp(p,"set_in")==0){
        if(c->nargs<2)return -1;
        Lin xl; if(parse_lin(m,c->args[0],&xl)!=0)return -1;
        if(xl.n!=1){lin_free(&xl);return 1;}
        int x=xl.idx[0];
        const char*set=c->args[1];
        char setbuf[512]; strncpy(setbuf,set,511); setbuf[511]=0;
        char*br=strchr(setbuf,'{');
        if(br){ br++; char*br2=strchr(br,'}'); if(br2)*br2=0; }
        else br=setbuf;
        long vals[256]; int nvals=0; long vmin=1000000000L,vmax=-1000000000L;
        char*dd=strchr(br,'.');
        if(dd){
            *dd=0; long lo=atol(br), hi=atol(dd+2);
            if(lo<=hi && hi-lo+1<=256){ for(long v=lo;v<=hi;v++){vals[nvals++]=(int)v; if(v<vmin)vmin=v; if(v>vmax)vmax=v;} }
            else { lin_free(&xl); return 1; }
        } else {
            char*tok=strtok(br,", \t");
            while(tok && nvals<256){ long v=atol(tok); vals[nvals++]=(int)v; if(nvals==1||v<vmin)vmin=v; if(v>vmax)vmax=v; tok=strtok(NULL,", \t"); }
        }
        if(nvals==0){lin_free(&xl);return 1;}
        if(nvals==1){ b->lo[x]=vals[0];b->haslo[x]=1;b->hi[x]=vals[0];b->hashi[x]=1; lin_free(&xl); return 0; }
        /* multi-value: SOS1 via binaries b_i, x = sum b_i*v_i, sum b_i = 1 */
        int *bs=(int*)malloc((size_t)nvals*sizeof(int));
        for(int i=0;i<nvals;i++) bs[i]=b_newvar(b,0.0,1.0);
        Lin eq;memset(&eq,0,sizeof(eq));
        lin_term(&eq,x,1.0);
        for(int i=0;i<nvals;i++) lin_term(&eq,bs[i],-(double)vals[i]);
        b_put(b,'=',0.0,&eq);
        Lin sum;memset(&sum,0,sizeof(sum));
        for(int i=0;i<nvals;i++) lin_term(&sum,bs[i],1.0);
        sum.constant=-1.0;
        b_put(b,'=',0.0,&sum);
        lin_free(&eq);lin_free(&sum);
        free(bs);lin_free(&xl);
        return 0;
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
        /* domain: min/max over all vars' bounds */
        double dlo=1e18, dhi=-1e18; int anyvar=0;
        for(int i=0;i<narr;i++){
            if(arr[i].n!=1){free_lins(arr,narr);return 1;}
            int v=arr[i].idx[0];
            if(v<0||v>=b->nvars){free_lins(arr,narr);return 1;}
            if(b->lo[v]<dlo)dlo=b->lo[v];
            if(b->hi[v]>dhi)dhi=b->hi[v];
            anyvar=1;
        }
        if(!anyvar){free_lins(arr,narr);return 1;}
        long ndom=(long)(dhi-dlo)+1;
        if(ndom<=0||ndom>128){free_lins(arr,narr);return 1;}   /* too big */
        /* y_{i,v}: index = i*ndom + (v-dlo) */
        int *y=(int*)malloc((size_t)narr*ndom*sizeof(int));
        for(int i=0;i<narr;i++)for(int vv=0;vv<ndom;vv++){
            y[i*ndom+vv]=b_newvar(b,0.0,1.0);
        }
        /* each var takes one value: sum_v y = 1 ;  x_i = sum v*y */
        for(int i=0;i<narr;i++){
            Lin s;memset(&s,0,sizeof(s));
            for(int vv=0;vv<ndom;vv++) lin_term(&s,y[i*ndom+vv],1.0);
            s.constant=-1.0; b_put(b,'=',0.0,&s); lin_free(&s);
            Lin e;memset(&e,0,sizeof(e)); lin_term(&e,arr[i].idx[0],1.0);
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
         sum b_i = 1, index = sum i*b_i + offset, val = sum arr[i]*b_i. */
    if(strcmp(p,"array_int_element")==0||strcmp(p,"fzn_array_int_element")==0||
       strcmp(p,"gecode_int_element")==0||strcmp(p,"array_var_int_element")==0){
        int gec = (strcmp(p,"gecode_int_element")==0);
        if(gec){
            if(c->nargs<4)return -1;
            /* index, offset, [arr], val */
            Lin il; if(parse_lin(m,c->args[0],&il)!=0)return -1;
            Lin ol; if(parse_lin(m,c->args[1],&ol)!=0){lin_free(&il);return -1;}
            Lin*arr;int narr;
            if(parse_array(m,c->args[2],&arr,&narr)!=0){lin_free(&il);lin_free(&ol);return -1;}
            Lin vl; if(parse_lin(m,c->args[3],&vl)!=0){lin_free(&il);lin_free(&ol);free_lins(arr,narr);return -1;}
            if(il.n!=1||vl.n!=1){lin_free(&il);lin_free(&ol);lin_free(&vl);free_lins(arr,narr);return 1;}
            int idx=il.idx[0], val=vl.idx[0];
            double offset = ol.constant;
            /* build SOS1 */
            int *bs=(int*)malloc((size_t)narr*sizeof(int));
            for(int i=0;i<narr;i++) bs[i]=b_newvar(b,0.0,1.0);
            Lin s;memset(&s,0,sizeof(s)); for(int i=0;i<narr;i++) lin_term(&s,bs[i],1.0); s.constant=-1.0; b_put(b,'=',0.0,&s); lin_free(&s);
            Lin ie;memset(&ie,0,sizeof(ie)); lin_term(&ie,idx,1.0);
            for(int i=0;i<narr;i++) lin_term(&ie,bs[i],-(double)(i+offset));
            b_put(b,'=',0.0,&ie); lin_free(&ie);
            Lin ve;memset(&ve,0,sizeof(ve)); lin_term(&ve,val,1.0);
            for(int i=0;i<narr;i++) lin_term(&ve,bs[i],-arr[i].constant);
            b_put(b,'=',0.0,&ve); lin_free(&ve);
            free(bs); lin_free(&il);lin_free(&ol);lin_free(&vl);free_lins(arr,narr);
            return 0;
        }
        /* array_int_element(index, [arr], val)  -- 3-arg MiniZinc form */
        if(c->nargs<3)return -1;
        Lin il; if(parse_lin(m,c->args[0],&il)!=0)return -1;
        Lin*arr;int narr;
        if(parse_array(m,c->args[1],&arr,&narr)!=0){lin_free(&il);return -1;}
        Lin vl; if(parse_lin(m,c->args[2],&vl)!=0){lin_free(&il);free_lins(arr,narr);return -1;}
        if(il.n!=1||vl.n!=1){lin_free(&il);lin_free(&vl);free_lins(arr,narr);return 1;}
        int idx=il.idx[0], val=vl.idx[0];
        int *bs=(int*)malloc((size_t)narr*sizeof(int));
        for(int i=0;i<narr;i++) bs[i]=b_newvar(b,0.0,1.0);
        Lin s;memset(&s,0,sizeof(s)); for(int i=0;i<narr;i++) lin_term(&s,bs[i],1.0); s.constant=-1.0; b_put(b,'=',0.0,&s); lin_free(&s);
        Lin ie;memset(&ie,0,sizeof(ie)); lin_term(&ie,idx,1.0);
        for(int i=0;i<narr;i++) lin_term(&ie,bs[i],-(double)(i+1));
        b_put(b,'=',0.0,&ie); lin_free(&ie);
        Lin ve;memset(&ve,0,sizeof(ve)); lin_term(&ve,val,1.0);
        for(int i=0;i<narr;i++) lin_term(&ve,bs[i],-arr[i].constant);
        b_put(b,'=',0.0,&ve); lin_free(&ve);
        free(bs); lin_free(&il);lin_free(&vl);free_lins(arr,narr);
        return 0;
    }
    /* array_int_maximum/minimum(m, x[]): exact selector formulation.  The
       selected element equals m while the remaining inequalities make m the
       true extremum; this also works when compiler propagation leaves constants
       inside a var-array view. */
    if(strcmp(p,"array_int_maximum")==0||strcmp(p,"array_int_minimum")==0||
       strcmp(p,"fzn_array_int_maximum")==0||strcmp(p,"fzn_array_int_minimum")==0){
        int ismax=(strcmp(p,"array_int_maximum")==0||strcmp(p,"fzn_array_int_maximum")==0);
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
        for(int i=1;i<nx;i++)ord[i-1]=b_newvar(b,1.0,(double)(nx-1));
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
       strcmp(p,"int_gt_reif")==0||strcmp(p,"bool_eq_reif")==0||
       strcmp(p,"bool_le_reif")==0||strcmp(p,"bool_lt_reif")==0||
       strcmp(p,"bool_ge_reif")==0||strcmp(p,"bool_gt_reif")==0){
        if(c->nargs<3)return -1;
        Lin l1,l2,rb;
        if(parse_lin(m,c->args[0],&l1)!=0)return -1;
        if(parse_lin(m,c->args[1],&l2)!=0){lin_free(&l1);return -1;}
        if(parse_lin(m,c->args[2],&rb)!=0){lin_free(&l1);lin_free(&l2);return -1;}
        Lin d;memset(&d,0,sizeof(d));lin_into(&d,&l1,1.0);lin_into(&d,&l2,-1.0);
        int which;
        if(strcmp(p,"int_eq_reif")==0||strcmp(p,"bool_eq_reif")==0)which=0;
        else if(strcmp(p,"int_le_reif")==0||strcmp(p,"bool_le_reif")==0)which=1;
        else if(strcmp(p,"int_lt_reif")==0||strcmp(p,"bool_lt_reif")==0)which=2;
        else if(strcmp(p,"int_ge_reif")==0||strcmp(p,"bool_ge_reif")==0)which=3;
        else which=4;
        int rr;
        if(rb.n==0){
            /* The reifier may be a compile-time true/false literal. */
            if(fabs(rb.constant)>1e-12&&fabs(rb.constant-1.0)>1e-12)rr=1;
            else rr=add_int_relation_constant(b,&d,which,rb.constant>=0.5);
        } else {
            int r;
            if(lin_unit_var(&rb,&r)!=0)rr=1;
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
    /* fzn_count_eq(x[], v, n): exactly n of the variables x equal value v.
       For small domains use binaries b_i = [x_i == v]: x_i = v + ... (big-M)
       and sum b_i = n.  With bounded integer vars, encode [x_i == v] via
       x_i >= v - M*(1-b_i), x_i <= v + M*(1-b_i), and for b_i=0 force
       |x_i - v| >= 1: x_i >= v+1 - M*b_i, x_i <= v-1 + M*b_i.
       Then sum_i b_i = n.  (Equivalent to among / count.) */
    if(strcmp(p,"fzn_count_eq")==0||strcmp(p,"fzn_among_eq")==0){
        if(c->nargs<3)return -1;
        Lin*arr;int narr;
        if(parse_array(m,c->args[0],&arr,&narr)!=0)return -1;
        Lin vl; if(parse_lin(m,c->args[1],&vl)!=0){free_lins(arr,narr);return -1;}
        Lin nl; if(parse_lin(m,c->args[2],&nl)!=0){free_lins(arr,narr);lin_free(&vl);return -1;}
        double v = vl.constant, ntarget = nl.constant;
        /* b_i = [x_i == v], s_i = [x_i > v] (side selector when x_i != v). */
        int *bs=(int*)malloc((size_t)(narr?narr:1)*sizeof(int));
        for(int i=0;i<narr;i++){
            if(arr[i].n!=1){free(bs);free_lins(arr,narr);lin_free(&vl);lin_free(&nl);return 1;}
            int x=arr[i].idx[0];
            double xlo=b->lo[x], xhi=b->hi[x];
            double M = fmax(fabs(xlo),fabs(xhi)); M=fmax(M,1.0)+1.0;
            int bi=b_newvar(b,0.0,1.0);
            int si=b_newvar(b,0.0,1.0);
            bs[i]=bi;
            /* b=1 -> x==v :  x >= v - M*(1-b) ;  x <= v + M*(1-b)
               => x + M*b >= v ;  x - M*b <= v */
            Lin r1;memset(&r1,0,sizeof(r1));lin_term(&r1,x,1.0);lin_term(&r1,bi,M);b_put(b,'>',v,&r1);
            Lin r2;memset(&r2,0,sizeof(r2));lin_term(&r2,x,1.0);lin_term(&r2,bi,-M);b_put(b,'<',v,&r2);
            /* b=0 -> x != v via side s:
               x >= v+1 - M*s      (s=0 -> x>=v+1 ; s=1 -> loose)
               x <= v-1 + M*(1-s)  (s=1 -> x<=v-1 ; s=0 -> loose)
               => x + M*s >= v+1 ;  x - M*s <= v-1 + M */
            Lin r3;memset(&r3,0,sizeof(r3));lin_term(&r3,x,1.0);lin_term(&r3,si,M);b_put(b,'>',v+1.0,&r3);
            Lin r4;memset(&r4,0,sizeof(r4));lin_term(&r4,x,1.0);lin_term(&r4,si,-M);b_put(b,'<',v-1.0+M,&r4);
            lin_free(&r1);lin_free(&r2);lin_free(&r3);lin_free(&r4);
        }
        /* sum b_i = ntarget */
        Lin s;memset(&s,0,sizeof(s));
        for(int i=0;i<narr;i++) lin_term(&s,bs[i],1.0);
        s.constant=-ntarget; b_put(b,'=',0.0,&s);
        lin_free(&s);free(bs);free_lins(arr,narr);lin_free(&vl);lin_free(&nl);
        return 0;
    }
    /* everything else: unhandled (return UNKNOWN at top level) */
    return 1;
}

void fz_solve(const FZModel*m,FZSolution*sol)
{
    long node_limit_in = sol->node_limit;   /* input knob, preserved across memset */
    memset(sol,0,sizeof(*sol));
    sol->node_limit = node_limit_in;
    int nv=m->nvars; sol->nvars=nv; sol->x=(double*)calloc((size_t)(nv?nv:1),sizeof(double));
    /* A compiler can propagate every decision variable to a literal (for
       example a table call on x = [1,3]).  Keep one fixed dummy column so the
       LP/MIP bridge can still certify SAT/UNSAT instead of reporting UNKNOWN. */
    int base_nv=nv?nv:1;
    Builder b;memset(&b,0,sizeof(b));b.nvars=base_nv;b.varcap=base_nv?base_nv:16;
    b.lo=(double*)malloc((size_t)b.varcap*sizeof(double));b.hi=(double*)malloc((size_t)b.varcap*sizeof(double));
    b.haslo=(int*)calloc((size_t)b.varcap,sizeof(int));b.hashi=(int*)calloc((size_t)b.varcap,sizeof(int));
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
        int *bs=(int*)malloc((size_t)decl->nset*sizeof(int));
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

    if(unhandled){for(int r=0;r<b.nrows;r++){free(b.rows[r].idx);free(b.rows[r].coef);}free(b.rows);free(b.lo);free(b.hi);free(b.haslo);free(b.hashi);sol->status=2;return;}

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
    for(int vi=nv;vi<ntot;vi++){isint[vi]=1;any_int=1;}
    sol->nvars=ntot;
    sol->x=(double*)psolve_realloc((void**)&sol->x,(size_t)(ntot?ntot:1)*sizeof(double));
    if(any_int){
        MIP mip;memset(&mip,0,sizeof(mip));
        mip.n=ntot;mip.m=b.nrows;mip.c=lp.c;mip.Acolptr=lp.Acolptr;mip.Arow=lp.Arow;mip.Aval=lp.Aval;
        mip.rel=lp.rel;mip.b=lp.b;mip.l=lp.l;mip.u=lp.u;mip.maximize=lp.maximize;
        mip.isint=isint;mip.mip_gap=1e-4;
        mip.stop_at_feasible = (m->solve_kind==0);   /* satisfy: first feasible is enough */
        mip.node_limit = (sol->node_limit>0)?sol->node_limit:200000;
        mip.lp_iter_limit=2000000;
        MIPResult mr;mip_solve(&mip,&mr);
        sol->nodes=mr.nodes; sol->best_bound=mr.best_bound;
        if(mr.status==0){memcpy(sol->x,mr.x,(size_t)ntot*sizeof(double));sol->obj=mr.obj+m->objective.constant;sol->iters=mr.lp_iters;sol->status=0;}
        else if(mr.status==1)sol->status=1;
        else if(mr.status==3||mr.status==4||mr.status==5||mr.status==6)sol->status=4;
        else sol->status=2;
        mip_result_free(&mr);
    } else {
        Solver*s=solver_create(&lp);
        int rr=solver_solve(s);
        if(rr==0){double*xo=(double*)malloc((size_t)nv*sizeof(double));double obj;solver_optimum(s,xo,&obj);memcpy(sol->x,xo,(size_t)nv*sizeof(double));sol->obj=obj+m->objective.constant;sol->iters=s->iters;sol->status=0;free(xo);}
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
    free(isint);
    free(lp.c);free(lp.l);free(lp.u);free(lp.b);free(lp.rel);free(lp.Acolptr);free(lp.Arow);free(lp.Aval);
    for(int r=0;r<b.nrows;r++){free(b.rows[r].idx);free(b.rows[r].coef);}free(b.rows);free(b.lo);free(b.hi);free(b.haslo);free(b.hashi);
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

static int fz_decl_value(const FZDecl*d,const FZSolution*sol,int e,double*out)
{
    if(e<0||e>=d->n)return -1;
    if(d->is_var){
        if(d->alias_idx){
            int v=d->alias_idx[e];
            if(v>=0){if(v>=sol->nvars)return -1;*out=sol->x[v];}
            else *out=d->alias_const[e];
            return 0;
        }
        if(d->base_idx<0||d->base_idx+e>=sol->nvars)return -1;
        *out=sol->x[d->base_idx+e];return 0;
    }
    if(d->par){*out=d->par[e];return 0;}
    if(d->par_int){*out=d->par_int[e];return 0;}
    return -1;
}

void fz_print_solution(const FZModel*m,const FZSolution*sol)
{
    if(sol->status==0){
        for(int d=0;d<m->ndecl;d++){FZDecl*decl=&m->decls[d];if(!decl->is_output)continue;
            double first;
            if(fz_decl_value(decl,sol,0,&first)!=0)continue;
            if(decl->is_array){
                int hi=decl->index_lo+decl->n-1;
                printf("%s = array1d(%d..%d, [",decl->name,decl->index_lo,hi);
                for(int e=0;e<decl->n;e++){double v;if(e)printf(", ");if(fz_decl_value(decl,sol,e,&v)!=0)v=0;fz_print_value(decl->kind,v);}
                printf("]);\n");
            } else {
                printf("%s = ",decl->name);fz_print_value(decl->kind,first);printf(";\n");
            }
        }
        printf("----------\n");
    } else if(sol->status==1)printf("=====UNSATISFIABLE=====\n");
    else printf("=====UNKNOWN=====\n");
}
void fz_solution_free(FZSolution*sol){free(sol->x);memset(sol,0,sizeof(*sol));}
void fz_model_free(FZModel*m){
    for(int i=0;i<m->ndecl;i++){FZDecl*d=&m->decls[i];free(d->name);free(d->alias_idx);free(d->alias_const);free(d->par);free(d->par_int);free(d->lo);free(d->hi);free(d->setvals);}
    free(m->decls);
    FZConstr*c=m->constr;while(c){FZConstr*nx=c->next;for(int i=0;i<c->nargs;i++)free(c->args[i]);free(c->args);free(c->pred);free(c);c=nx;}
    free(m->objective.idx);free(m->objective.coef);free(m->file);
    memset(m,0,sizeof(*m));
}
