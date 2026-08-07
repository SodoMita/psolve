#include "fzn.h"
#include "solver.h"
#include "err.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
/* Lexer                                                               */
/* ------------------------------------------------------------------ */
typedef enum { TK_EOF, TK_IDENT, TK_INT, TK_FLOAT, TK_STRING, TK_SYM } TK;
typedef struct { TK kind; char*text; long ival; double fval; size_t start,end; } Token;

static int is_ident_ch(int c){ return isalnum(c)||c=='_'; }

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
        const char*two[]={"::","->","<-","..","{","}","[","]","(",")",",",";",":",".","=",0};
        int matched=0;
        for(int k=0;two[k];k++){ size_t Lk=strlen(two[k]);
            if(i+Lk<=L&&strncmp(src+i,two[k],Lk)==0){
                if(cnt>=cap){cap*=2;t=(Token*)realloc(t,(size_t)cap*sizeof(Token));}
                t[cnt].kind=TK_SYM;t[cnt].text=strdup(two[k]);t[cnt].start=i;t[cnt].end=i+Lk;cnt++;i+=Lk;matched=1;break; } }
        if(!matched){free(t);return -1;}
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
    while(s[*pos]==' ')(*pos)++;
    char c=s[*pos];
    if(c=='-'){(*pos)++;Expr t=parse_primary(s,pos,m,err);if(!*err){for(int i=0;i<t.n;i++){for(int k=0;k<t.els[i].n;k++)t.els[i].coef[k]=-t.els[i].coef[k];t.els[i].constant=-t.els[i].constant;}}e=t;return e;}
    if(c=='['){(*pos)++;int cap=8;free(e.els);e.els=(Lin*)malloc((size_t)cap*sizeof(Lin));e.n=0;e.is_array=1;
        while(1){while(s[*pos]==' ')(*pos)++;if(s[*pos]==']'){(*pos)++;break;}
            Expr t=parse_expr(s,pos,m,err);if(*err)return e;
            if(e.n>=cap){cap*=2;e.els=(Lin*)realloc(e.els,(size_t)cap*sizeof(Lin));}
            e.els[e.n]=t.els[0];e.n++;free(t.els);
            while(s[*pos]==' ')(*pos)++;if(s[*pos]==','){(*pos)++;continue;}if(s[*pos]==']'){(*pos)++;break;}*err=1;return e;}
        return e;}
    if(isdigit((unsigned char)c)||(c=='.'&&isdigit((unsigned char)s[*pos+1]))){
        char num[64];int k=0;
        while(k<63&&(isdigit((unsigned char)s[*pos])||s[*pos]=='.'||s[*pos]=='e'||s[*pos]=='E'||s[*pos]=='+'||s[*pos]=='-'))num[k++]=s[(*pos)++];
        num[k]=0; e.els[0].constant=atof(num); return e;}
    if(isalpha((unsigned char)c)||c=='_'){
        char name[128];int k=0;while(k<127&&is_ident_ch((unsigned char)s[*pos]))name[k++]=s[(*pos)++];name[k]=0;
        while(s[*pos]==' ')(*pos)++;
        if(s[*pos]=='['){/* array element */
            (*pos)++;Expr ix=parse_expr(s,pos,m,err);if(*err){lin_free(&e.els[0]);free(e.els);return e;}
            long ival=(long)ix.els[0].constant;lin_free(&ix.els[0]);free(ix.els);
            FZDecl*d=find_decl(m,name);
            if(!d){*err=1;return e;}
            if(d->is_var){ lin_term(&e.els[0], d->base_idx+(int)(ival-1), 1.0); }
            else e.els[0].constant = d->par?d->par[ival-1]:(d->par_int?d->par_int[ival-1]:0);
            return e;}
        FZDecl*d=find_decl(m,name);
        if(d){ if(d->is_var){lin_term(&e.els[0],d->base_idx,1.0);} else e.els[0].constant=d->par?d->par[0]:(d->par_int?d->par_int[0]:0); }
        else { if(strcmp(name,"true")==0)e.els[0].constant=1; else if(strcmp(name,"false")==0)e.els[0].constant=0; else *err=1; }
        return e;}
    if(c=='('){(*pos)++;Expr t=parse_expr(s,pos,m,err);while(s[*pos]==' ')(*pos)++;if(s[*pos]==')')(*pos)++;e=t;return e;}
    *err=1;return e;
}

static Expr parse_expr(const char*s,size_t*pos,FZModel*m,int*err)
{
    Expr e=parse_primary(s,pos,m,err); if(*err)return e;
    while(1){ while(s[*pos]==' ')(*pos)++;
        char op=s[*pos]; if(op!='+'&&op!='-')break; (*pos)++;
        Expr t=parse_primary(s,pos,m,err); if(*err)return e;
        double sg=(op=='+')?1.0:-1.0;
        /* combine: only handle scalar scalar-compatible; for arrays we take
           elementwise sum when both scalar-expanded */
        if(e.n!=t.n){ expr_free2(&e); expr_free2(&t); *err=1; return e; }
        for(int i=0;i<e.n;i++){ lin_into(&e.els[i],&t.els[i],sg); lin_free(&t.els[i]); }
        free(t.els);
    }
    return e;
}
static void expr_free2(Expr*e){ if(e->n>0){for(int i=0;i<e->n;i++)lin_free(&e->els[i]);free(e->els);} }

/* parse a scalar expression into a Lin */
static int parse_lin(FZModel*m,const char*s,Lin*out)
{
    size_t pos=0;int err=0;
    Expr e=parse_expr(s,&pos,m,&err);
    if(err||e.is_array||e.n!=1){ if(e.n>0){for(int i=0;i<e.n;i++)lin_free(&e.els[i]);free(e.els);} return -1; }
    *out=e.els[0]; free(e.els);
    return 0;
}
/* parse an array expression into an array of Lin */
static int parse_array(FZModel*m,const char*s,Lin**out,int*outn)
{
    size_t pos=0;int err=0;
    Expr e=parse_expr(s,&pos,m,&err);
    if(err||!e.is_array){ if(e.n>0){for(int i=0;i<e.n;i++)lin_free(&e.els[i]);free(e.els);} return -1; }
    *out=e.els;*outn=e.n; return 0;
}
static void free_lins(Lin*arr,int n){for(int i=0;i<n;i++)lin_free(&arr[i]);free(arr);}

/* ------------------------------------------------------------------ */
/* Parser                                                              */
/* ------------------------------------------------------------------ */
static FZDecl*add_decl(FZModel*m){
    if(m->ndecl>=m->cap_decl){m->cap_decl=m->cap_decl?m->cap_decl*2:16;m->decls=(FZDecl*)realloc(m->decls,(size_t)m->cap_decl*sizeof(FZDecl));}
    FZDecl*d=&m->decls[m->ndecl++];memset(d,0,sizeof(*d));d->base_idx=-1;return d;
}
FZDecl*find_decl(FZModel*m,const char*name){
    for(int i=0;i<m->ndecl;i++)if(m->decls[i].name&&strcmp(m->decls[i].name,name)==0)return &m->decls[i];
    return NULL;
}

static int is_kw(const char*s,const char*k){return s&&strcmp(s,k)==0;}

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
            if(is_kw(kw,"par")||is_kw(kw,"var")){
                int is_var=is_kw(kw,"var");FZDecl*d=add_decl(m);d->is_var=is_var;ti++;
                if(ti<nt&&is_kw(toks[ti].text,"array")){d->is_array=1;ti++;while(ti<nt&&!(toks[ti].kind==TK_SYM&&strcmp(toks[ti].text,"]")==0))ti++;if(ti<nt)ti++;if(ti<nt&&is_kw(toks[ti].text,"of"))ti++;}
                if(ti<nt&&toks[ti].kind==TK_IDENT){if(is_kw(toks[ti].text,"int"))d->kind=FZ_K_INT;else if(is_kw(toks[ti].text,"float"))d->kind=FZ_K_FLOAT;else if(is_kw(toks[ti].text,"bool"))d->kind=FZ_K_BOOL;ti++;}
                /* FlatZinc shorthand: var 1..10: x  (domain before ':') */
                if(ti<nt&&(toks[ti].kind==TK_INT||toks[ti].kind==TK_FLOAT)){
                    char lo[64],hi[64];snprintf(lo,sizeof(lo),"%ld",(long)toks[ti].ival);ti++;
                    if(ti<nt&&toks[ti].kind==TK_SYM&&strcmp(toks[ti].text,"..")==0)ti++;
                    if(ti<nt&&toks[ti].kind==TK_INT){snprintf(hi,sizeof(hi),"%ld",(long)toks[ti].ival);ti++;}
                    else if(ti<nt&&toks[ti].kind==TK_FLOAT){snprintf(hi,sizeof(hi),"%g",toks[ti].fval);ti++;}
                    d->has_lo=1;d->has_hi=1;d->lo=(double*)malloc(sizeof(double));d->hi=(double*)malloc(sizeof(double));
                    d->lo[0]=atof(lo);d->hi[0]=atof(hi);
                }
                if(ti<nt&&toks[ti].kind==TK_SYM&&strcmp(toks[ti].text,":")==0)ti++;
                if(ti<nt&&toks[ti].kind==TK_IDENT){d->name=strdup(toks[ti].text);ti++;}
                if(ti<nt&&toks[ti].kind==TK_SYM&&strcmp(toks[ti].text,"=")==0){
                    ti++;size_t s=toks[ti].start;int j=ti;size_t e=toks[ti].end;
                    while(j<nt&&!(toks[j].kind==TK_SYM&&strcmp(toks[j].text,";")==0)){e=toks[j].end;j++;}
                    char*rhs=strndup(src+s,e-s);
                    if(d->is_array){
                        int nel=1;for(size_t p=0;p<strlen(rhs);p++)if(rhs[p]==',')nel++;
                        d->n=nel;d->par=(double*)calloc((size_t)nel,sizeof(double));d->par_int=(int*)calloc((size_t)nel,sizeof(int));
                        Lin*arr;int narr;
                        if(parse_array(m,rhs,&arr,&narr)==0&&narr==nel){for(int q=0;q<nel;q++){d->par[q]=arr[q].constant;d->par_int[q]=(int)llround(arr[q].constant);}free_lins(arr,narr);}
                    } else {
                        d->n=1;d->par=(double*)calloc(1,sizeof(double));d->par_int=(int*)calloc(1,sizeof(int));
                        Lin l;if(parse_lin(m,rhs,&l)==0){d->par[0]=l.constant;d->par_int[0]=(int)llround(l.constant);lin_free(&l);}
                    }
                    free(rhs);ti=(j<nt?j+1:j);
                    if(is_var){d->base_idx=m->nvars;m->nvars+=d->n;}
                    continue;
                }
                /* var decl without initializer: count array size from domain
                   annotation :: lo..hi if array; else n=1. base index assigned. */
                d->n=1;
                d->base_idx=m->nvars;
                m->nvars+=d->n;
                /* annotations */
                while(ti<nt&&toks[ti].kind==TK_SYM&&strcmp(toks[ti].text,"::")==0){
                    ti++; int got_domain=0;
                    /* capture annotation: identifier or expression; detect output_var/output_array */
                    size_t s=toks[ti].start; int j=ti; size_t e=toks[ti].end;
                    while(j<nt&&!(toks[j].kind==TK_SYM&&(strcmp(toks[j].text,";")==0||strcmp(toks[j].text,"::")==0))){e=toks[j].end;j++;}
                    char*ann=strndup(src+s,e-s);
                    if(strstr(ann,"output_var")||strstr(ann,"output_array"))d->is_output=1;
                    /* domain like "lo..hi" */
                    if(strchr(ann,'.')){
                        char lo[64],hi[64];int k=0;while(ann[k]&&ann[k]!='.'){lo[k]=ann[k];k++;}lo[k]=0;
                        const char*p=ann+k;while(*p=='.')p++;k=0;while(p[k]&&p[k]!=' ') {hi[k]=p[k];k++;}hi[k]=0;
                        d->has_lo=1;d->has_hi=1;
                        d->lo=(double*)malloc(sizeof(double));d->hi=(double*)malloc(sizeof(double));
                        d->lo[0]=atof(lo);d->hi[0]=atof(hi);
                        got_domain=1;
                    }
                    free(ann);
                    ti=(j<nt?j:j); /* advance to '::' or ';' boundary */
                }
                if(ti<nt&&toks[ti].kind==TK_SYM&&strcmp(toks[ti].text,";")==0)ti++;
                continue;
            }
            /* unknown: skip to ';' */
            while(ti<nt&&!(toks[ti].kind==TK_SYM&&strcmp(toks[ti].text,";")==0))ti++; if(ti<nt)ti++; continue;
        }
        ti++;
    }
    free(toks);free(src);
    return 0;
}

/* ------------------------------------------------------------------ */
/* LP bridge                                                           */
/* ------------------------------------------------------------------ */
typedef struct { int n;int*idx;double*coef;double rhs;char rel; } Row;
typedef struct { int nvars;double*lo,*hi;int*haslo,*hashi;Row*rows;int nrows,cap; } Builder;
static void b_add(Builder*b){if(b->nrows>=b->cap){b->cap=b->cap?b->cap*2:16;b->rows=(Row*)realloc(b->rows,(size_t)b->cap*sizeof(Row));}memset(&b->rows[b->nrows],0,sizeof(Row));}
static void b_put(Builder*b,char rel,double rhs,const Lin*l){
    b_add(b);Row*r=&b->rows[b->nrows++];r->rel=rel;r->rhs=rhs-l->constant;
    r->n=l->n;r->idx=(int*)malloc((size_t)(l->n?l->n:1)*sizeof(int));r->coef=(double*)malloc((size_t)(l->n?l->n:1)*sizeof(double));
    for(int i=0;i<l->n;i++){r->idx[i]=l->idx[i];r->coef[i]=l->coef[i];}
}

/* dispatch: 0 handled, 1 unhandled, -1 malformed */
static int handle_constraint(FZModel*m,Builder*b,FZConstr*c)
{
    const char*p=c->pred; Lin l1,l2,l3;
    if(strcmp(p,"int_lin_eq")==0||strcmp(p,"int_lin_le")==0||
       strcmp(p,"bool_lin_eq")==0||strcmp(p,"bool_lin_le")==0){
        if(c->nargs<3)return -1;
        Lin*arr;int narr;
        if(parse_array(m,c->args[0],&arr,&narr)!=0)return -1;
        Lin*parr;int nparr;
        if(parse_array(m,c->args[1],&parr,&nparr)!=0){free_lins(arr,narr);return -1;}
        Lin d; if(parse_lin(m,c->args[2],&d)!=0){free_lins(arr,narr);free_lins(parr,nparr);return -1;}
        /* sum_i arr[i]*parr[i] - d (rel) 0 */
        Lin lin;memset(&lin,0,sizeof(lin));
        for(int i=0;i<narr&&i<nparr;i++){
            lin_into(&lin,&parr[i],arr[i].constant);
        }
        lin.constant-=d.constant;
        char rel=(strcmp(p,"int_lin_le")==0||strcmp(p,"bool_lin_le")==0)?'<':'=';
        b_put(b,rel,0.0,&lin);
        lin_free(&lin);lin_free(&d);free_lins(arr,narr);free_lins(parr,nparr);
        return 0;
    }
    if(strcmp(p,"int_eq")==0||strcmp(p,"bool_eq")==0||strcmp(p,"float_eq")==0||
       strcmp(p,"int_le")==0||strcmp(p,"bool_le")==0||strcmp(p,"float_le")==0||
       strcmp(p,"int_lt")==0||strcmp(p,"int_ge")==0||strcmp(p,"int_gt")==0||strcmp(p,"bool_lt")==0){
        if(c->nargs<2)return -1;
        if(parse_lin(m,c->args[0],&l1)!=0)return -1;
        if(parse_lin(m,c->args[1],&l2)!=0){lin_free(&l1);return -1;}
        Lin dd;memset(&dd,0,sizeof(dd));
        lin_into(&dd,&l1,1.0);lin_into(&dd,&l2,-1.0);
        char rel;
        if(strcmp(p,"int_eq")==0||strcmp(p,"bool_eq")==0||strcmp(p,"float_eq")==0)rel='=';
        else if(strcmp(p,"int_le")==0||strcmp(p,"bool_le")==0||strcmp(p,"float_le")==0)rel='<';
        else if(strcmp(p,"int_lt")==0||strcmp(p,"bool_lt")==0)rel='<';
        else rel='>';
        b_put(b,rel,0.0,&dd);
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
    /* everything else: unhandled (return UNKNOWN at top level) */
    return 1;
}

void fz_solve(const FZModel*m,FZSolution*sol)
{
    memset(sol,0,sizeof(*sol));
    int nv=m->nvars; sol->nvars=nv; sol->x=(double*)calloc((size_t)(nv?nv:1),sizeof(double));
    if(nv==0){sol->status=3;return;}
    Builder b;memset(&b,0,sizeof(b));b.nvars=nv;
    b.lo=(double*)malloc((size_t)nv*sizeof(double));b.hi=(double*)malloc((size_t)nv*sizeof(double));
    b.haslo=(int*)calloc((size_t)nv,sizeof(int));b.hashi=(int*)calloc((size_t)nv,sizeof(int));
    /* FlatZinc `var int/float` are unbounded by default; our LP solver needs
       finite bounds, so clamp undecorated variables to a big-M interval. */
    const double BIG = 1e9;
    for(int i=0;i<nv;i++){b.lo[i]=-BIG;b.hi[i]=BIG;}
    for(int d=0;d<m->ndecl;d++){FZDecl*decl=&m->decls[d];if(!decl->is_var||decl->base_idx<0)continue;
        for(int e=0;e<decl->n;e++){int vi=decl->base_idx+e;
            if(decl->has_lo){b.lo[vi]=decl->lo[0];b.haslo[vi]=1;}
            if(decl->has_hi){b.hi[vi]=decl->hi[0];b.hashi[vi]=1;}}}
    int unhandled=0;
    for(FZConstr*c=m->constr;c;c=c->next){int r=handle_constraint((FZModel*)m,&b,c);if(r!=0)unhandled=1;}
    if(unhandled){for(int r=0;r<b.nrows;r++){free(b.rows[r].idx);free(b.rows[r].coef);}free(b.rows);free(b.lo);free(b.hi);free(b.haslo);free(b.hashi);sol->status=2;return;}

    LP lp;memset(&lp,0,sizeof(lp));
    lp.n=nv;lp.m=b.nrows;lp.maximize=1;
    lp.c=(double*)calloc((size_t)nv,sizeof(double));
    if(m->solve_kind==2){for(int i=0;i<m->objective.n;i++){int vi=m->objective.idx[i];if(vi>=0&&vi<nv)lp.c[vi]+=m->objective.coef[i];}}
    else if(m->solve_kind==1){lp.maximize=0;for(int i=0;i<m->objective.n;i++){int vi=m->objective.idx[i];if(vi>=0&&vi<nv)lp.c[vi]+=m->objective.coef[i];}}
    lp.l=(double*)malloc((size_t)nv*sizeof(double));lp.u=(double*)malloc((size_t)nv*sizeof(double));
    for(int i=0;i<nv;i++){lp.l[i]=b.lo[i];lp.u[i]=b.hi[i];}
    long nnz=0;for(int r=0;r<b.nrows;r++)nnz+=b.rows[r].n;
    lp.b=(double*)malloc((size_t)(b.nrows?b.nrows:1)*sizeof(double));lp.rel=(char*)malloc((size_t)(b.nrows?b.nrows:1));
    for(int r=0;r<b.nrows;r++){lp.b[r]=b.rows[r].rhs;lp.rel[r]=b.rows[r].rel;}
    lp.Acolptr=(int*)calloc((size_t)(nv+1),sizeof(int));
    lp.Arow=(int*)malloc((size_t)(nnz?nnz:1)*sizeof(int));lp.Aval=(double*)malloc((size_t)(nnz?nnz:1)*sizeof(double));
    for(int r=0;r<b.nrows;r++)for(int k=0;k<b.rows[r].n;k++){int j=b.rows[r].idx[k];if(j>=0&&j<nv)lp.Acolptr[j+1]++;}
    for(int j=0;j<nv;j++)lp.Acolptr[j+1]+=lp.Acolptr[j];
    int*ff=(int*)malloc((size_t)nv*sizeof(int));for(int j=0;j<nv;j++)ff[j]=lp.Acolptr[j];
    for(int r=0;r<b.nrows;r++)for(int k=0;k<b.rows[r].n;k++){int j=b.rows[r].idx[k];if(j>=0&&j<nv){lp.Arow[ff[j]]=r;lp.Aval[ff[j]]=b.rows[r].coef[k];ff[j]++;}}
    free(ff);

    Solver*s=solver_create(&lp);
    int rr=solver_solve(s);
    if(rr==0){double*xo=(double*)malloc((size_t)nv*sizeof(double));double obj;solver_optimum(s,xo,&obj);memcpy(sol->x,xo,(size_t)nv*sizeof(double));sol->obj=obj;sol->iters=s->iters;sol->status=0;free(xo);}
    else if(rr==1)sol->status=1;
    else sol->status=2;
    solver_destroy(s);
    free(lp.c);free(lp.l);free(lp.u);free(lp.b);free(lp.rel);free(lp.Acolptr);free(lp.Arow);free(lp.Aval);
    for(int r=0;r<b.nrows;r++){free(b.rows[r].idx);free(b.rows[r].coef);}free(b.rows);free(b.lo);free(b.hi);free(b.haslo);free(b.hashi);
}

void fz_print_solution(const FZModel*m,const FZSolution*sol)
{
    if(sol->status==0){
        for(int d=0;d<m->ndecl;d++){FZDecl*decl=&m->decls[d];if(!decl->is_output||decl->base_idx<0)continue;
            if(decl->is_array){printf("%s = [",decl->name);for(int e=0;e<decl->n;e++){if(e)printf(", ");printf("%d",(int)llround(sol->x[decl->base_idx+e]));}printf("];\n");}
            else printf("%s = %d;\n",decl->name,(int)llround(sol->x[decl->base_idx]));}
        printf("----------\n");
    } else if(sol->status==1)printf("=====UNSATISFIABLE=====\n");
    else printf("=====UNKNOWN=====\n");
}
void fz_solution_free(FZSolution*sol){free(sol->x);memset(sol,0,sizeof(*sol));}
void fz_model_free(FZModel*m){
    for(int i=0;i<m->ndecl;i++){FZDecl*d=&m->decls[i];free(d->name);free(d->par);free(d->par_int);free(d->lo);free(d->hi);}
    free(m->decls);
    FZConstr*c=m->constr;while(c){FZConstr*nx=c->next;for(int i=0;i<c->nargs;i++)free(c->args[i]);free(c->args);free(c->pred);free(c);c=nx;}
    free(m->objective.idx);free(m->objective.coef);free(m->file);
    memset(m,0,sizeof(*m));
}
