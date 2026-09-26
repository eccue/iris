/* ============================================================
   IRIS — an AI-native programming language
   Version 6.0 "GC"
   File extension: .ir
   Build: clang -std=c99 -O2 -o $PREFIX/bin/iris iris.c -lm
   ============================================================ */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static char *xs(const char *s){size_t n=strlen(s)+1;char*p=malloc(n);memcpy(p,s,n);return p;}
static void die(const char *m,int l){fprintf(stderr,"iris: %s (line %d)\n",m,l);exit(1);}

/* ============================================================
   1. LEXER
   ============================================================ */
typedef enum{
  T_EOF,T_NUM,T_STR,T_IDENT,
  T_LET,T_PRINT,T_FN,T_IF,T_ELSE,T_WHILE,T_FOR,T_IN,T_RETURN,
  T_TRUE,T_FALSE,
  T_PLUS,T_MINUS,T_STAR,T_SLASH,T_PERCENT,T_AT,
  T_LPAREN,T_RPAREN,T_LBRACE,T_RBRACE,T_COMMA,T_SEMI,
  T_ASSIGN,T_EQ,T_NEQ,T_LT,T_GT,T_LE,T_GE,T_AND,T_OR,T_NOT
}TokKind;

typedef struct{TokKind kind;double num;char*str;int line;}Token;

static Token *lex(const char *src,int *out_n){
  int cap=256,n=0,line=1,i=0;Token*t=malloc(sizeof(Token)*cap);
#define PUSH(x) do{if(n>=cap){cap*=2;t=realloc(t,sizeof(Token)*cap);}t[n++]=(x);}while(0)
  while(src[i]){
    char c=src[i];
    if(c=='\n'){line++;i++;continue;}
    if(c==' '||c=='\t'||c=='\r'){i++;continue;}
    if(c=='#'){while(src[i]&&src[i]!='\n')i++;continue;}
    if(isdigit((unsigned char)c)){
      int s=i;while(isdigit((unsigned char)src[i]))i++;
      if(src[i]=='.'){i++;while(isdigit((unsigned char)src[i]))i++;}
      char buf[64];int L=i-s;if(L>63)L=63;memcpy(buf,src+s,L);buf[L]=0;
      Token tk={T_NUM,atof(buf),NULL,line};PUSH(tk);continue;
    }
    if(c=='"'){
      i++;int cap2=64,len=0;char*buf=malloc(cap2);
      while(src[i]&&src[i]!='"'){
        char ch=src[i];
        if(ch=='\\'&&src[i+1]){i++;char e=src[i];
          if(e=='n')ch='\n';else if(e=='t')ch='\t';else if(e=='"')ch='"';else if(e=='\\')ch='\\';else ch=e;}
        if(len+1>=cap2){cap2*=2;buf=realloc(buf,cap2);}
        buf[len++]=ch;i++;
      }
      if(src[i]!='"')die("unterminated string",line);
      i++;buf[len]=0;
      Token tk={T_STR,0,buf,line};PUSH(tk);continue;
    }
    if(isalpha((unsigned char)c)||c=='_'){
      int s=i;while(isalnum((unsigned char)src[i])||src[i]=='_')i++;
      int L=i-s;char*buf=malloc(L+1);memcpy(buf,src+s,L);buf[L]=0;
      TokKind k=T_IDENT;
      if(!strcmp(buf,"let"))k=T_LET;
      else if(!strcmp(buf,"print"))k=T_PRINT;
      else if(!strcmp(buf,"fn"))k=T_FN;
      else if(!strcmp(buf,"if"))k=T_IF;
      else if(!strcmp(buf,"else"))k=T_ELSE;
      else if(!strcmp(buf,"while"))k=T_WHILE;
      else if(!strcmp(buf,"for"))k=T_FOR;
      else if(!strcmp(buf,"in"))k=T_IN;
      else if(!strcmp(buf,"return"))k=T_RETURN;
      else if(!strcmp(buf,"true"))k=T_TRUE;
      else if(!strcmp(buf,"false"))k=T_FALSE;
      else if(!strcmp(buf,"and"))k=T_AND;
      else if(!strcmp(buf,"or"))k=T_OR;
      else if(!strcmp(buf,"not"))k=T_NOT;
      Token tk={k,0,buf,line};PUSH(tk);continue;
    }
#define OP2(a,b,k2) if(c==a&&src[i+1]==b){Token tk={k2,0,NULL,line};PUSH(tk);i+=2;continue;}
    OP2('=','=',T_EQ)OP2('!','=',T_NEQ)OP2('<','=',T_LE)OP2('>','=',T_GE)
    OP2('&','&',T_AND)OP2('|','|',T_OR)
#undef OP2
    TokKind k;
    switch(c){
      case '+':k=T_PLUS;break;case '-':k=T_MINUS;break;
      case '*':k=T_STAR;break;case '/':k=T_SLASH;break;
      case '%':k=T_PERCENT;break;case '@':k=T_AT;break;
      case '(':k=T_LPAREN;break;case ')':k=T_RPAREN;break;
      case '{':k=T_LBRACE;break;case '}':k=T_RBRACE;break;
      case ',':k=T_COMMA;break;case ';':k=T_SEMI;break;
      case '=':k=T_ASSIGN;break;case '<':k=T_LT;break;
      case '>':k=T_GT;break;case '!':k=T_NOT;break;
      default:die("unexpected character",line);
    }
    Token tk={k,0,NULL,line};PUSH(tk);i++;
  }
  Token eof={T_EOF,0,NULL,line};PUSH(eof);*out_n=n;
#undef PUSH
  return t;
}

/* ============================================================
   2. AST
   ============================================================ */
typedef enum{
  N_NUM,N_STR,N_BOOL,N_IDENT,N_BIN,N_UNARY,N_CALL,N_ASSIGN,
  N_LET,N_PRINT,N_EXPR,N_BLOCK,N_IF,N_WHILE,N_FOR,N_FN,N_RETURN
}NodeKind;

typedef struct Node Node;
struct Node{NodeKind kind;int op;double num;char*str;
  Node*a,*b,*c;Node**list;char**params;int count;};

static Node *mk(NodeKind k){Node*n=calloc(1,sizeof(Node));n->kind=k;return n;}

/* ============================================================
   3. PARSER
   ============================================================ */
typedef struct{Token*t;int n;int i;}Parser;
static Token*pk(Parser*p){return &p->t[p->i];}
static Token*nxp(Parser*p){return &p->t[p->i++];}
static int ck(Parser*p,TokKind k){return p->t[p->i].kind==k;}
static int mt(Parser*p,TokKind k){if(ck(p,k)){p->i++;return 1;}return 0;}
static Token*ex(Parser*p,TokKind k,const char*w){
  if(!ck(p,k)){fprintf(stderr,"iris: expected %s (line %d)\n",w,pk(p)->line);exit(1);}
  return nxp(p);
}
static Node *parse_expr(Parser*),*parse_stmt(Parser*),*parse_block(Parser*);
static Node *parse_primary(Parser*),*parse_call(Parser*),*parse_unary(Parser*);

static Node *parse_primary(Parser*p){
  Token*t=pk(p);
  if(t->kind==T_NUM){nxp(p);Node*n=mk(N_NUM);n->num=t->num;return n;}
  if(t->kind==T_STR){nxp(p);Node*n=mk(N_STR);n->str=t->str;return n;}
  if(t->kind==T_TRUE||t->kind==T_FALSE){nxp(p);Node*n=mk(N_BOOL);n->num=(t->kind==T_TRUE);return n;}
  if(t->kind==T_IDENT){nxp(p);Node*n=mk(N_IDENT);n->str=t->str;return n;}
  if(t->kind==T_LPAREN){nxp(p);Node*e=parse_expr(p);ex(p,T_RPAREN,"')'");return e;}
  fprintf(stderr,"iris: unexpected token (line %d)\n",t->line);exit(1);
}
static Node *parse_call(Parser*p){
  Node*e=parse_primary(p);
  while(ck(p,T_LPAREN)){
    nxp(p);Node*c=mk(N_CALL);c->a=e;
    int cap=4,cnt=0;Node**args=malloc(sizeof(Node*)*cap);
    if(!ck(p,T_RPAREN))do{
      if(cnt>=cap){cap*=2;args=realloc(args,sizeof(Node*)*cap);}
      args[cnt++]=parse_expr(p);
    }while(mt(p,T_COMMA));
    ex(p,T_RPAREN,"')'");
    c->list=args;c->count=cnt;e=c;
  }
  return e;
}
static Node *parse_unary(Parser*p){
  if(ck(p,T_MINUS)||ck(p,T_NOT)){
    TokKind op=nxp(p)->kind;Node*n=mk(N_UNARY);n->op=op;n->a=parse_unary(p);return n;
  }
  return parse_call(p);
}
static Node *pbin(Parser*p,Node*(*nxt)(Parser*),TokKind*ops,int nops){
  Node*l=nxt(p);
  for(;;){
    TokKind k=pk(p)->kind;int f=0;
    for(int j=0;j<nops;j++)if(ops[j]==k){f=1;break;}
    if(!f)break;nxp(p);
    Node*r=nxt(p);Node*n=mk(N_BIN);n->op=k;n->a=l;n->b=r;l=n;
  }
  return l;
}
static Node *pmul(Parser*p){TokKind o[]={T_STAR,T_SLASH,T_PERCENT,T_AT};return pbin(p,parse_unary,o,4);}
static Node *padd(Parser*p){TokKind o[]={T_PLUS,T_MINUS};return pbin(p,pmul,o,2);}
static Node *pcmp(Parser*p){TokKind o[]={T_LT,T_GT,T_LE,T_GE};return pbin(p,padd,o,4);}
static Node *peq(Parser*p){TokKind o[]={T_EQ,T_NEQ};return pbin(p,pcmp,o,2);}
static Node *pand(Parser*p){TokKind o[]={T_AND};return pbin(p,peq,o,1);}
static Node *por(Parser*p){TokKind o[]={T_OR};return pbin(p,pand,o,1);}
static Node *parse_expr(Parser*p){return por(p);}

static Node *parse_block(Parser*p){
  ex(p,T_LBRACE,"'{'");Node*b=mk(N_BLOCK);
  int cap=8,cnt=0;Node**list=malloc(sizeof(Node*)*cap);
  while(!ck(p,T_RBRACE)&&!ck(p,T_EOF)){
    if(cnt>=cap){cap*=2;list=realloc(list,sizeof(Node*)*cap);}
    list[cnt++]=parse_stmt(p);
  }
  ex(p,T_RBRACE,"'}'");b->list=list;b->count=cnt;return b;
}
static Node *parse_stmt(Parser*p){
  Token*t=pk(p);
  if(t->kind==T_LET){
    nxp(p);Token*name=ex(p,T_IDENT,"identifier");
    ex(p,T_ASSIGN,"'='");Node*e=parse_expr(p);ex(p,T_SEMI,"';'");
    Node*n=mk(N_LET);n->str=name->str;n->a=e;return n;
  }
  if(t->kind==T_PRINT){
    nxp(p);Node*e=parse_expr(p);ex(p,T_SEMI,"';'");
    Node*n=mk(N_PRINT);n->a=e;return n;
  }
  if(t->kind==T_IF){
    nxp(p);ex(p,T_LPAREN,"'('");Node*c=parse_expr(p);ex(p,T_RPAREN,"')'");
    Node*th=parse_block(p);Node*el=NULL;
    if(mt(p,T_ELSE))el=ck(p,T_IF)?parse_stmt(p):parse_block(p);
    Node*n=mk(N_IF);n->a=c;n->b=th;n->c=el;return n;
  }
  if(t->kind==T_WHILE){
    nxp(p);ex(p,T_LPAREN,"'('");Node*c=parse_expr(p);ex(p,T_RPAREN,"')'");
    Node*b=parse_block(p);Node*n=mk(N_WHILE);n->a=c;n->b=b;return n;
  }
  if(t->kind==T_FOR){
    nxp(p);Token*var=ex(p,T_IDENT,"loop variable");
    ex(p,T_IN,"'in'");Token*rg=ex(p,T_IDENT,"'range'");
    if(strcmp(rg->str,"range"))die("expected 'range'",rg->line);
    ex(p,T_LPAREN,"'('");Node*lim=parse_expr(p);ex(p,T_RPAREN,"')'");
    Node*body=parse_block(p);Node*n=mk(N_FOR);
    n->str=var->str;n->a=lim;n->b=body;return n;
  }
  if(t->kind==T_FN){
    nxp(p);Token*name=ex(p,T_IDENT,"function name");
    ex(p,T_LPAREN,"'('");
    int cap=4,cnt=0;char**params=malloc(sizeof(char*)*cap);
    if(!ck(p,T_RPAREN))do{
      if(cnt>=cap){cap*=2;params=realloc(params,sizeof(char*)*cap);}
      params[cnt++]=ex(p,T_IDENT,"parameter")->str;
    }while(mt(p,T_COMMA));
    ex(p,T_RPAREN,"')'");
    Node*body=parse_block(p);Node*fn=mk(N_FN);
    fn->str=name->str;fn->params=params;fn->count=cnt;fn->a=body;return fn;
  }
  if(t->kind==T_RETURN){
    nxp(p);Node*e=NULL;
    if(!ck(p,T_SEMI))e=parse_expr(p);
    ex(p,T_SEMI,"';'");
    Node*n=mk(N_RETURN);n->a=e;return n;
  }
  if(t->kind==T_LBRACE)return parse_block(p);
  if(t->kind==T_IDENT&&p->t[p->i+1].kind==T_ASSIGN){
    nxp(p);nxp(p);
    Node*e=parse_expr(p);ex(p,T_SEMI,"';'");
    Node*n=mk(N_ASSIGN);n->str=t->str;n->a=e;return n;
  }
  Node*e=parse_expr(p);ex(p,T_SEMI,"';'");
  Node*n=mk(N_EXPR);n->a=e;return n;
}

/* ============================================================
   4. TENSORS + GC ROOTS
   ============================================================ */
typedef struct TNode{
  int ndims;int dims[4];int size;
  double*data;double*grad;
  struct TNode*a,*b;
  int op;double scalar;
  int mark;
} TNode;

enum{
  OP_NONE=0,OP_ADD,OP_SUB,OP_MUL,OP_DIV,
  OP_SCALAR_ADD,OP_SCALAR_SUB,OP_SCALAR_MUL,OP_SCALAR_DIV,
  OP_MATMUL,OP_RELU,OP_SIGMOID,OP_TANH,OP_MSE,OP_SUM,
  OP_TRANSPOSE,OP_SOFTMAX,OP_LAYERNORM,OP_GELU,OP_CROSS_ENTROPY,
  OP_EMBEDDING,OP_DROPOUT,OP_CONCAT,OP_POSENC,OP_MEAN,OP_BCE
};

/* every tensor ever created is on this list */
static TNode **g_all=NULL;
static int g_all_n=0,g_all_cap=0;
static void track(TNode*t){
  if(g_all_n>=g_all_cap){
    g_all_cap=g_all_cap?g_all_cap*2:1024;
    g_all=realloc(g_all,sizeof(TNode*)*g_all_cap);
  }
  g_all[g_all_n++]=t;
}

static TNode *tn_new(int ndims,int*dims){
  TNode*t=calloc(1,sizeof(TNode));
  t->ndims=ndims;t->size=1;
  for(int i=0;i<ndims&&i<4;i++){t->dims[i]=dims[i];t->size*=dims[i];}
  for(int i=ndims;i<4;i++)t->dims[i]=1;
  t->data=calloc(t->size,sizeof(double));
  track(t);
  return t;
}
static void tn_gr(TNode*t){if(!t->grad)t->grad=calloc(t->size,sizeof(double));}

/* tape */
static TNode **g_tape=NULL;static int g_tape_n=0,g_tape_cap=0;
static void tape_push(TNode*t){
  if(g_tape_n>=g_tape_cap){
    g_tape_cap=g_tape_cap?g_tape_cap*2:1024;
    g_tape=realloc(g_tape,sizeof(TNode*)*g_tape_cap);
  }
  g_tape[g_tape_n++]=t;
}

static int g_training=1;

/* ---- tensor ops ---- */
#define MB 32
static TNode *tn_matmul(TNode*a,TNode*b){
  if(a->ndims!=2||b->ndims!=2||a->dims[1]!=b->dims[0]){
    fprintf(stderr,"iris: matmul shape error: a=(%d,%d) b=(%d,%d)\n",
            a->dims[0],a->dims[1],b->dims[0],b->dims[1]);exit(1);
  }
  int M=a->dims[0],K=a->dims[1],N=b->dims[1];
  int d[2]={M,N};TNode*o=tn_new(2,d);
  o->op=OP_MATMUL;o->a=a;o->b=b;
  for(int ii=0;ii<M;ii+=MB){
    int im=ii+MB<M?ii+MB:M;
    for(int kk=0;kk<K;kk+=MB){
      int km=kk+MB<K?kk+MB:K;
      for(int jj=0;jj<N;jj+=MB){
        int jm=jj+MB<N?jj+MB:N;
        for(int i=ii;i<im;i++)
          for(int k=kk;k<km;k++){
            double aik=a->data[i*K+k];
            double*c=o->data+i*N;double*brow=b->data+k*N;
            for(int j=jj;j<jm;j++)c[j]+=aik*brow[j];
          }
      }
    }
  }
  tape_push(o);return o;
}
static TNode *tn_op2(TNode*a,TNode*b,int op){
  if(a->size!=b->size){fprintf(stderr,"iris: size mismatch\n");exit(1);}
  TNode*o=tn_new(a->ndims,a->dims);o->op=op;o->a=a;o->b=b;
  for(int i=0;i<a->size;i++){
    double x=a->data[i],y=b->data[i];
    if(op==OP_ADD)o->data[i]=x+y;
    else if(op==OP_SUB)o->data[i]=x-y;
    else if(op==OP_MUL)o->data[i]=x*y;
    else if(op==OP_DIV)o->data[i]=x/y;
  }
  tape_push(o);return o;
}
static TNode *tn_scalar(TNode*a,double s,int op){
  TNode*o=tn_new(a->ndims,a->dims);o->op=op;o->a=a;o->scalar=s;
  for(int i=0;i<a->size;i++){
    double x=a->data[i];
    if(op==OP_SCALAR_ADD)o->data[i]=x+s;
    else if(op==OP_SCALAR_SUB)o->data[i]=x-s;
    else if(op==OP_SCALAR_MUL)o->data[i]=x*s;
    else if(op==OP_SCALAR_DIV)o->data[i]=x/s;
  }
  tape_push(o);return o;
}
static TNode *tn_relu(TNode*a){
  TNode*o=tn_new(a->ndims,a->dims);o->op=OP_RELU;o->a=a;
  for(int i=0;i<a->size;i++)o->data[i]=a->data[i]>0?a->data[i]:0;
  tape_push(o);return o;
}
static TNode *tn_sigmoid(TNode*a){
  TNode*o=tn_new(a->ndims,a->dims);o->op=OP_SIGMOID;o->a=a;
  for(int i=0;i<a->size;i++)o->data[i]=1.0/(1.0+exp(-a->data[i]));
  tape_push(o);return o;
}
static TNode *tn_tanh(TNode*a){
  TNode*o=tn_new(a->ndims,a->dims);o->op=OP_TANH;o->a=a;
  for(int i=0;i<a->size;i++)o->data[i]=tanh(a->data[i]);
  tape_push(o);return o;
}
static TNode *tn_gelu(TNode*a){
  TNode*o=tn_new(a->ndims,a->dims);o->op=OP_GELU;o->a=a;
  const double c=0.7978845608028654;
  for(int i=0;i<a->size;i++){
    double x=a->data[i];double x3=x*x*x;
    double u=c*(x+0.044715*x3);
    o->data[i]=0.5*x*(1.0+tanh(u));
  }
  tape_push(o);return o;
}
static TNode *tn_mse(TNode*p,TNode*t){
  if(p->size!=t->size)die("mse size mismatch",0);
  int d[1]={1};TNode*o=tn_new(1,d);o->op=OP_MSE;o->a=p;o->b=t;
  double s=0;for(int i=0;i<p->size;i++){double e=p->data[i]-t->data[i];s+=e*e;}
  o->data[0]=s/p->size;tape_push(o);return o;
}
static TNode *tn_bce(TNode*p,TNode*t){
  if(p->size!=t->size)die("bce size mismatch",0);
  int d[1]={1};TNode*o=tn_new(1,d);o->op=OP_BCE;o->a=p;o->b=t;
  double eps=1e-7,s=0;
  for(int i=0;i<p->size;i++){
    double pi=p->data[i];if(pi<eps)pi=eps;if(pi>1-eps)pi=1-eps;
    s+=-t->data[i]*log(pi)-(1-t->data[i])*log(1-pi);
  }
  o->data[0]=s/p->size;tape_push(o);return o;
}
static TNode *tn_sum(TNode*a){
  int d[1]={1};TNode*o=tn_new(1,d);o->op=OP_SUM;o->a=a;
  double s=0;for(int i=0;i<a->size;i++)s+=a->data[i];
  o->data[0]=s;tape_push(o);return o;
}
static TNode *tn_mean(TNode*a){
  int d[1]={1};TNode*o=tn_new(1,d);o->op=OP_MEAN;o->a=a;
  double s=0;for(int i=0;i<a->size;i++)s+=a->data[i];
  o->data[0]=s/a->size;tape_push(o);return o;
}
static TNode *tn_trans(TNode*a){
  if(a->ndims!=2)die("trans needs 2D",0);
  int d[2]={a->dims[1],a->dims[0]};
  TNode*o=tn_new(2,d);o->op=OP_TRANSPOSE;o->a=a;
  int M=a->dims[0],N=a->dims[1];
  for(int i=0;i<M;i++)for(int j=0;j<N;j++)o->data[j*M+i]=a->data[i*N+j];
  tape_push(o);return o;
}
static TNode *tn_softmax(TNode*a){
  if(a->ndims!=2)die("softmax needs 2D",0);
  int N=a->dims[0],C=a->dims[1];
  TNode*o=tn_new(2,a->dims);o->op=OP_SOFTMAX;o->a=a;
  for(int i=0;i<N;i++){
    double mx=a->data[i*C];
    for(int j=1;j<C;j++)if(a->data[i*C+j]>mx)mx=a->data[i*C+j];
    double sum=0;
    for(int j=0;j<C;j++){o->data[i*C+j]=exp(a->data[i*C+j]-mx);sum+=o->data[i*C+j];}
    for(int j=0;j<C;j++)o->data[i*C+j]/=sum;
  }
  tape_push(o);return o;
}
static TNode *tn_layernorm(TNode*a){
  if(a->ndims!=2)die("layernorm needs 2D",0);
  int N=a->dims[0],C=a->dims[1];double eps=1e-5;
  TNode*o=tn_new(2,a->dims);o->op=OP_LAYERNORM;o->a=a;
  for(int i=0;i<N;i++){
    double mean=0;for(int j=0;j<C;j++)mean+=a->data[i*C+j];mean/=C;
    double var=0;for(int j=0;j<C;j++){double d=a->data[i*C+j]-mean;var+=d*d;}var/=C;
    double inv=1.0/sqrt(var+eps);
    for(int j=0;j<C;j++)o->data[i*C+j]=(a->data[i*C+j]-mean)*inv;
  }
  tape_push(o);return o;
}
static TNode *tn_cross_entropy(TNode*logits,TNode*targets){
  if(logits->ndims!=2)die("cross_entropy needs 2D logits",0);
  int N=logits->dims[0],C=logits->dims[1];
  int d[1]={1};TNode*o=tn_new(1,d);o->op=OP_CROSS_ENTROPY;o->a=logits;o->b=targets;
  double loss=0;
  for(int i=0;i<N;i++){
    double mx=logits->data[i*C];
    for(int j=1;j<C;j++)if(logits->data[i*C+j]>mx)mx=logits->data[i*C+j];
    double sum=0;for(int j=0;j<C;j++)sum+=exp(logits->data[i*C+j]-mx);
    int tgt=(int)targets->data[i];
    if(tgt<0)tgt=0;if(tgt>=C)tgt=C-1;
    loss-=(logits->data[i*C+tgt]-mx)-log(sum);
  }
  o->data[0]=loss/N;tape_push(o);return o;
}
static TNode *tn_attention(TNode*Q,TNode*K,TNode*V){
  TNode*Kt=tn_trans(K);
  TNode*s=tn_matmul(Q,Kt);
  double sc=1.0/sqrt((double)Q->dims[1]);
  TNode*ss=tn_scalar(s,sc,OP_SCALAR_MUL);
  TNode*at=tn_softmax(ss);
  return tn_matmul(at,V);
}
static TNode *tn_embedding(TNode*tokens,TNode*W){
  if(W->ndims!=2)die("embedding: W must be 2D",0);
  int N=tokens->size,D=W->dims[1];
  int d[2]={N,D};TNode*o=tn_new(2,d);
  o->op=OP_EMBEDDING;o->a=tokens;o->b=W;
  for(int i=0;i<N;i++){
    int tok=(int)tokens->data[i];
    if(tok<0)tok=0;if(tok>=W->dims[0])tok=W->dims[0]-1;
    for(int j=0;j<D;j++)o->data[i*D+j]=W->data[tok*D+j];
  }
  tape_push(o);return o;
}
static TNode *tn_posenc(TNode*x){
  int seq=x->dims[0],d=x->dims[1];
  TNode*o=tn_new(2,x->dims);o->op=OP_POSENC;o->a=x;
  for(int i=0;i<seq;i++){
    for(int j=0;j<d;j++){
      double pe;
      if(j%2==0)pe=sin(i/pow(10000.0,(double)j/d));
      else      pe=cos(i/pow(10000.0,(double)(j-1)/d));
      o->data[i*d+j]=x->data[i*d+j]+pe;
    }
  }
  tape_push(o);return o;
}
static TNode *tn_dropout(TNode*a,double p){
  TNode*o=tn_new(a->ndims,a->dims);o->op=OP_DROPOUT;o->a=a;o->scalar=p;
  for(int i=0;i<a->size;i++){
    if(g_training&&((double)rand()/RAND_MAX)<p)o->data[i]=0;
    else o->data[i]=g_training?a->data[i]/(1.0-p):a->data[i];
  }
  tape_push(o);return o;
}
static TNode *tn_concat(TNode*a,TNode*b){
  if(a->ndims!=2||b->ndims!=2||a->dims[0]!=b->dims[0])
    die("concat: shape mismatch",0);
  int N=a->dims[0],d1=a->dims[1],d2=b->dims[1];
  int d[2]={N,d1+d2};TNode*o=tn_new(2,d);o->op=OP_CONCAT;o->a=a;o->b=b;
  for(int i=0;i<N;i++){
    for(int j=0;j<d1;j++)o->data[i*(d1+d2)+j]=a->data[i*d1+j];
    for(int j=0;j<d2;j++)o->data[i*(d1+d2)+d1+j]=b->data[i*d2+j];
  }
  tape_push(o);return o;
}

/* ============================================================
   5. AUTODIFF
   ============================================================ */
static void bwd_step(TNode*o){
  if(!o->grad)return;
  TNode*a=o->a,*b=o->b;
  switch(o->op){
    case OP_ADD:
      tn_gr(a);tn_gr(b);
      for(int i=0;i<o->size;i++){a->grad[i]+=o->grad[i];b->grad[i]+=o->grad[i];}
      break;
    case OP_SUB:
      tn_gr(a);tn_gr(b);
      for(int i=0;i<o->size;i++){a->grad[i]+=o->grad[i];b->grad[i]-=o->grad[i];}
      break;
    case OP_MUL:
      tn_gr(a);tn_gr(b);
      for(int i=0;i<o->size;i++){a->grad[i]+=o->grad[i]*b->data[i];b->grad[i]+=o->grad[i]*a->data[i];}
      break;
    case OP_DIV:
      tn_gr(a);tn_gr(b);
      for(int i=0;i<o->size;i++){
        a->grad[i]+=o->grad[i]/b->data[i];
        b->grad[i]-=o->grad[i]*a->data[i]/(b->data[i]*b->data[i]);
      }
      break;
    case OP_SCALAR_ADD:case OP_SCALAR_SUB:
      tn_gr(a);for(int i=0;i<o->size;i++)a->grad[i]+=o->grad[i];
      break;
    case OP_SCALAR_MUL:
      tn_gr(a);for(int i=0;i<o->size;i++)a->grad[i]+=o->grad[i]*o->scalar;
      break;
    case OP_SCALAR_DIV:
      tn_gr(a);for(int i=0;i<o->size;i++)a->grad[i]+=o->grad[i]/o->scalar;
      break;
    case OP_MATMUL:{
      int M=a->dims[0],K=a->dims[1],N=b->dims[1];
      tn_gr(a);tn_gr(b);
      for(int i=0;i<M;i++)
        for(int k=0;k<K;k++){
          double s=0;for(int j=0;j<N;j++)s+=o->grad[i*N+j]*b->data[k*N+j];
          a->grad[i*K+k]+=s;
        }
      for(int k=0;k<K;k++)
        for(int j=0;j<N;j++){
          double s=0;for(int i=0;i<M;i++)s+=a->data[i*K+k]*o->grad[i*N+j];
          b->grad[k*N+j]+=s;
        }
      break;
    }
    case OP_RELU:
      tn_gr(a);for(int i=0;i<o->size;i++)a->grad[i]+=o->grad[i]*(a->data[i]>0?1.0:0.0);
      break;
    case OP_SIGMOID:
      tn_gr(a);for(int i=0;i<o->size;i++)a->grad[i]+=o->grad[i]*o->data[i]*(1.0-o->data[i]);
      break;
    case OP_TANH:
      tn_gr(a);for(int i=0;i<o->size;i++)a->grad[i]+=o->grad[i]*(1.0-o->data[i]*o->data[i]);
      break;
    case OP_GELU:{
      tn_gr(a);const double c=0.7978845608028654;
      for(int i=0;i<o->size;i++){
        double x=a->data[i];double x3=x*x*x;
        double u=c*(x+0.044715*x3);double t=tanh(u);
        double du=c*(1.0+3.0*0.044715*x*x);
        double dg=0.5*(1.0+t)+0.5*x*(1.0-t*t)*du;
        a->grad[i]+=o->grad[i]*dg;
      }
      break;
    }
    case OP_MSE:{
      tn_gr(a);tn_gr(b);double inv=2.0/a->size;
      for(int i=0;i<a->size;i++){
        double d=a->data[i]-b->data[i];
        a->grad[i]+=o->grad[0]*d*inv;
        b->grad[i]-=o->grad[0]*d*inv;
      }
      break;
    }
    case OP_BCE:{
      tn_gr(a);double eps=1e-7;
      for(int i=0;i<a->size;i++){
        double pi=a->data[i];if(pi<eps)pi=eps;if(pi>1-eps)pi=1-eps;
        double d=(pi-b->data[i])/(pi*(1-pi));
        a->grad[i]+=o->grad[0]*d/a->size;
      }
      break;
    }
    case OP_SUM:
      tn_gr(a);for(int i=0;i<a->size;i++)a->grad[i]+=o->grad[0];
      break;
    case OP_MEAN:
      tn_gr(a);
      for(int i=0;i<a->size;i++)a->grad[i]+=o->grad[0]/a->size;
      break;
    case OP_TRANSPOSE:{
      tn_gr(a);int M=a->dims[0],N=a->dims[1];
      for(int i=0;i<M;i++)for(int j=0;j<N;j++)a->grad[i*N+j]+=o->grad[j*M+i];
      break;
    }
    case OP_SOFTMAX:{
      tn_gr(a);int N=a->dims[0],C=a->dims[1];
      for(int i=0;i<N;i++){
        double dot=0;
        for(int j=0;j<C;j++)dot+=o->grad[i*C+j]*o->data[i*C+j];
        for(int j=0;j<C;j++)a->grad[i*C+j]+=o->data[i*C+j]*(o->grad[i*C+j]-dot);
      }
      break;
    }
    case OP_LAYERNORM:{
      tn_gr(a);int N=a->dims[0],C=a->dims[1];double eps=1e-5;
      for(int i=0;i<N;i++){
        double mean=0;for(int j=0;j<C;j++)mean+=a->data[i*C+j];mean/=C;
        double var=0;for(int j=0;j<C;j++){double d=a->data[i*C+j]-mean;var+=d*d;}var/=C;
        double inv=1.0/sqrt(var+eps);
        double sdy=0,sdyx=0;
        for(int j=0;j<C;j++){
          double xh=(a->data[i*C+j]-mean)*inv;
          sdy+=o->grad[i*C+j];sdyx+=o->grad[i*C+j]*xh;
        }
        for(int j=0;j<C;j++){
          double xh=(a->data[i*C+j]-mean)*inv;
          a->grad[i*C+j]+=inv*(o->grad[i*C+j]-sdy/C-xh*sdyx/C);
        }
      }
      break;
    }
    case OP_CROSS_ENTROPY:{
      TNode*L=o->a,*T=o->b;int N=L->dims[0],C=L->dims[1];
      tn_gr(L);double g=o->grad[0]/N;
      for(int i=0;i<N;i++){
        double mx=L->data[i*C];
        for(int j=1;j<C;j++)if(L->data[i*C+j]>mx)mx=L->data[i*C+j];
        double sum=0;for(int j=0;j<C;j++)sum+=exp(L->data[i*C+j]-mx);
        int tgt=(int)T->data[i];if(tgt<0)tgt=0;if(tgt>=C)tgt=C-1;
        for(int j=0;j<C;j++){
          double p=exp(L->data[i*C+j]-mx)/sum;
          double ind=(j==tgt)?1.0:0.0;
          L->grad[i*C+j]+=g*(p-ind);
        }
      }
      break;
    }
    case OP_EMBEDDING:{
      TNode*tok=o->a,*W=o->b;
      int N=tok->size,D=W->dims[1];
      tn_gr(W);
      for(int i=0;i<N;i++){
        int t=(int)tok->data[i];if(t<0)t=0;if(t>=W->dims[0])t=W->dims[0]-1;
        for(int j=0;j<D;j++)W->grad[t*D+j]+=o->grad[i*D+j];
      }
      break;
    }
    case OP_POSENC:
      tn_gr(a);for(int i=0;i<a->size;i++)a->grad[i]+=o->grad[i];
      break;
    case OP_DROPOUT:
      tn_gr(a);
      for(int i=0;i<o->size;i++){
        if(o->data[i]==0)a->grad[i]+=0;
        else a->grad[i]+=o->grad[i]*(g_training?1.0/(1.0-o->scalar):1.0);
      }
      break;
    case OP_CONCAT:{
      tn_gr(a);tn_gr(b);
      int N=a->dims[0],d1=a->dims[1],d2=b->dims[1];
      for(int i=0;i<N;i++){
        for(int j=0;j<d1;j++)a->grad[i*d1+j]+=o->grad[i*(d1+d2)+j];
        for(int j=0;j<d2;j++)b->grad[i*d2+j]+=o->grad[i*(d1+d2)+d1+j];
      }
      break;
    }
  }
}
static void backward(TNode*loss){
  for(int i=0;i<g_tape_n;i++)
    if(g_tape[i]->grad)memset(g_tape[i]->grad,0,g_tape[i]->size*sizeof(double));
  tn_gr(loss);
  for(int i=0;i<loss->size;i++)loss->grad[i]=1.0;
  for(int i=g_tape_n-1;i>=0;i--)bwd_step(g_tape[i]);
}

/* ============================================================
   6. ADAM
   ============================================================ */
typedef struct{char*name;double*m;double*v;int t;int size;}AdamState;
static AdamState*g_adam=NULL;static int g_adam_n=0,g_adam_cap=0;
static AdamState *adam_get(const char*name,int size){
  for(int i=0;i<g_adam_n;i++)
    if(!strcmp(g_adam[i].name,name))return &g_adam[i];
  if(g_adam_n>=g_adam_cap){
    g_adam_cap=g_adam_cap?g_adam_cap*2:16;
    g_adam=realloc(g_adam,sizeof(AdamState)*g_adam_cap);
  }
  AdamState*s=&g_adam[g_adam_n++];
  s->name=xs(name);s->m=calloc(size,sizeof(double));
  s->v=calloc(size,sizeof(double));s->t=0;s->size=size;
  return s;
}
static void adam_step(const char*name,double*param,double*grad,int size,
                     double lr,double b1,double b2,double eps){
  AdamState*s=adam_get(name,size);
  s->t++;
  double bc1=1.0-pow(b1,s->t);
  double bc2=1.0-pow(b2,s->t);
  for(int i=0;i<size;i++){
    s->m[i]=b1*s->m[i]+(1-b1)*grad[i];
    s->v[i]=b2*s->v[i]+(1-b2)*grad[i]*grad[i];
    double mh=s->m[i]/bc1;
    double vh=s->v[i]/bc2;
    param[i]-=lr*mh/(sqrt(vh)+eps);
  }
}

/* ============================================================
   7. VALUES + ENVIRONMENTS
   ============================================================ */
typedef enum{V_NUM,V_STR,V_BOOL,V_FN,V_NIL,V_TENSOR}VType;
typedef struct Env Env;
typedef struct Value Value;
typedef Value(*NativeFn)(Value*,int);
typedef struct Fn{char*name;char**params;int nparams;Node*body;Env*closure;NativeFn native;}Fn;
struct Value{VType type;double num;char*str;Fn*fn;TNode*tensor;};

static Value vnum(double d){Value v={V_NUM,d,NULL,NULL,NULL};return v;}
static Value vbool(int b){Value v={V_BOOL,b?1:0,NULL,NULL,NULL};return v;}
static Value vnil(void){Value v={V_NIL,0,NULL,NULL,NULL};return v;}
static Value vfn(Fn*f){Value v={V_FN,0,NULL,f,NULL};return v;}
static Value vstr(const char*s){Value v={V_STR,0,xs(s?s:""),NULL,NULL};return v;}
static Value vt(TNode*t){Value v={V_TENSOR,0,NULL,NULL,t};return v;}

struct Env{Env*parent;char**names;Value*vals;int n,cap;};
static Env *env_new(Env*p){
  Env*e=calloc(1,sizeof(Env));e->parent=p;e->cap=8;
  e->names=malloc(sizeof(char*)*e->cap);
  e->vals=malloc(sizeof(Value)*e->cap);return e;
}
static void env_set(Env*e,const char*name,Value v){
  for(int i=0;i<e->n;i++)if(!strcmp(e->names[i],name)){e->vals[i]=v;return;}
  if(e->n>=e->cap){
    e->cap*=2;e->names=realloc(e->names,sizeof(char*)*e->cap);
    e->vals=realloc(e->vals,sizeof(Value)*e->cap);
  }
  e->names[e->n]=xs(name);e->vals[e->n]=v;e->n++;
}
static int env_get(Env*e,const char*name,Value*out){
  for(Env*c=e;c;c=c->parent)
    for(int i=0;i<c->n;i++)
      if(!strcmp(c->names[i],name)){*out=c->vals[i];return 1;}
  return 0;
}
static void env_assign(Env*e,const char*name,Value v){
  for(Env*c=e;c;c=c->parent)
    for(int i=0;i<c->n;i++)
      if(!strcmp(c->names[i],name)){c->vals[i]=v;return;}
  fprintf(stderr,"iris: undefined variable '%s'\n",name);exit(1);
}

/* ============================================================
   8. GARBAGE COLLECTOR (mark & sweep from current env chain)
   ============================================================ */
static Env *g_current_env = NULL;

static void mark_tensor(TNode*t){
  if(!t||t->mark)return;
  t->mark=1;
  mark_tensor(t->a);
  mark_tensor(t->b);
}
static void mark_env(Env*e){
  for(;e;e=e->parent){
    for(int i=0;i<e->n;i++)
      if(e->vals[i].type==V_TENSOR)mark_tensor(e->vals[i].tensor);
  }
}
static void gc_from(Env*root){
  if(!root)return;
  for(int i=0;i<g_all_n;i++)g_all[i]->mark=0;
  mark_env(root);
  int j=0;
  for(int i=0;i<g_all_n;i++){
    TNode*t=g_all[i];
    if(t->mark){g_all[j++]=t;}
    else{free(t->data);if(t->grad)free(t->grad);free(t);}
  }
  g_all_n=j;
}

static void tape_reset(void){
  g_tape_n=0;
  gc_from(g_current_env);
}

/* ============================================================
   9. HELPERS
   ============================================================ */
static int truthy(Value v){
  switch(v.type){
    case V_NUM:return v.num!=0;case V_BOOL:return v.num!=0;
    case V_STR:return v.str&&v.str[0];case V_NIL:return 0;
    default:return 1;
  }
}
static char *to_str(Value v){
  char buf[512];
  if(v.type==V_TENSOR){
    TNode*t=v.tensor;int cap=512,len=0;char*o=malloc(cap);
    len+=snprintf(o+len,cap-len,"tensor([");
    for(int i=0;i<t->ndims;i++)
      len+=snprintf(o+len,cap-len,"%d%s",t->dims[i],i<t->ndims-1?", ":"");
    len+=snprintf(o+len,cap-len,"], data=[");
    for(int i=0;i<t->size;i++){
      if(len>cap-64){cap*=2;o=realloc(o,cap);}
      len+=snprintf(o+len,cap-len,"%.4g%s",t->data[i],i<t->size-1?", ":"");
    }
    snprintf(o+len,cap-len,"])");return o;
  }
  switch(v.type){
    case V_STR:return xs(v.str?v.str:"");
    case V_NUM:
      if(v.num==(long long)v.num)snprintf(buf,sizeof buf,"%lld",(long long)v.num);
      else snprintf(buf,sizeof buf,"%g",v.num);
      return xs(buf);
    case V_BOOL:return xs(v.num?"true":"false");
    case V_NIL:return xs("nil");
    case V_FN:snprintf(buf,sizeof buf,"<fn %s>",v.fn->name);return xs(buf);
    default:return xs("");
  }
}
static Value binop(int op,Value L,Value R){
  if(op==T_AT&&L.type==V_TENSOR&&R.type==V_TENSOR)return vt(tn_matmul(L.tensor,R.tensor));
  if(L.type==V_TENSOR&&R.type==V_TENSOR){
    int t=0;
    if(op==T_PLUS)t=OP_ADD;else if(op==T_MINUS)t=OP_SUB;
    else if(op==T_STAR)t=OP_MUL;else if(op==T_SLASH)t=OP_DIV;
    if(t)return vt(tn_op2(L.tensor,R.tensor,t));
  }
  if(L.type==V_TENSOR&&R.type==V_NUM){
    int t=0;
    if(op==T_PLUS)t=OP_SCALAR_ADD;else if(op==T_MINUS)t=OP_SCALAR_SUB;
    else if(op==T_STAR)t=OP_SCALAR_MUL;else if(op==T_SLASH)t=OP_SCALAR_DIV;
    if(t)return vt(tn_scalar(L.tensor,R.num,t));
  }
  if(L.type==V_NUM&&R.type==V_TENSOR){
    if(op==T_PLUS)return vt(tn_scalar(R.tensor,L.num,OP_SCALAR_ADD));
    if(op==T_STAR)return vt(tn_scalar(R.tensor,L.num,OP_SCALAR_MUL));
    if(op==T_MINUS)return vt(tn_scalar(R.tensor,-L.num,OP_SCALAR_ADD));
  }
  if(op==T_PLUS&&(L.type==V_STR||R.type==V_STR)){
    char*a=to_str(L),*b=to_str(R);
    char*o=malloc(strlen(a)+strlen(b)+1);
    strcpy(o,a);strcat(o,b);
    Value v=vstr(o);free(a);free(b);free(o);return v;
  }
  if(L.type==V_NUM&&R.type==V_NUM){
    switch(op){
      case T_PLUS:return vnum(L.num+R.num);
      case T_MINUS:return vnum(L.num-R.num);
      case T_STAR:return vnum(L.num*R.num);
      case T_SLASH:return vnum(L.num/R.num);
      case T_PERCENT:return vnum(fmod(L.num,R.num));
      case T_EQ:return vbool(L.num==R.num);
      case T_NEQ:return vbool(L.num!=R.num);
      case T_LT:return vbool(L.num<R.num);
      case T_GT:return vbool(L.num>R.num);
      case T_LE:return vbool(L.num<=R.num);
      case T_GE:return vbool(L.num>=R.num);
    }
  }
  if(L.type==V_STR&&R.type==V_STR){
    if(op==T_EQ)return vbool(strcmp(L.str,R.str)==0);
    if(op==T_NEQ)return vbool(strcmp(L.str,R.str)!=0);
  }
  fprintf(stderr,"iris: invalid operands\n");exit(1);
}

/* ============================================================
   10. EVALUATOR
   ============================================================ */
static int g_ret=0;static Value g_retv;
static Value eval(Node*,Env*);

static Value call_fn(Fn*f,Value*args,int nargs){
  if(f->nparams>=0&&nargs!=f->nparams){
    fprintf(stderr,"iris: '%s' expects %d args, got %d\n",f->name,f->nparams,nargs);exit(1);
  }
  if(f->native)return f->native(args,nargs);
  Env*saved_env=g_current_env;
  Env*e=env_new(f->closure);
  g_current_env=e;
  for(int i=0;i<nargs;i++)env_set(e,f->params[i],args[i]);
  int s=g_ret;g_ret=0;
  eval(f->body,e);
  Value r=vnil();
  if(g_ret){r=g_retv;g_ret=0;}
  g_ret=s;
  g_current_env=saved_env;
  return r;
}

static Value eval(Node*n,Env*env){
  if(!n)return vnil();
  switch(n->kind){
    case N_NUM:return vnum(n->num);
    case N_STR:return vstr(n->str);
    case N_BOOL:return vbool((int)n->num);
    case N_IDENT:{
      Value v;
      if(!env_get(env,n->str,&v)){fprintf(stderr,"iris: undefined '%s'\n",n->str);exit(1);}
      return v;
    }
    case N_BIN:{
      Value L=eval(n->a,env);
      if(n->op==T_AND){if(!truthy(L))return vbool(0);return vbool(truthy(eval(n->b,env)));}
      if(n->op==T_OR){if(truthy(L))return vbool(1);return vbool(truthy(eval(n->b,env)));}
      return binop(n->op,L,eval(n->b,env));
    }
    case N_UNARY:{
      Value v=eval(n->a,env);
      if(n->op==T_MINUS){
        if(v.type==V_TENSOR)return vt(tn_scalar(v.tensor,-1.0,OP_SCALAR_MUL));
        if(v.type!=V_NUM)die("unary '-' on non-number",0);
        return vnum(-v.num);
      }
      if(n->op==T_NOT)return vbool(!truthy(v));
      break;
    }
    case N_CALL:{
      Value c=eval(n->a,env);
      if(c.type!=V_FN)die("not callable",0);
      int na=n->count;Value*args=malloc(sizeof(Value)*(na?na:1));
      for(int i=0;i<na;i++)args[i]=eval(n->list[i],env);
      Value r=call_fn(c.fn,args,na);
      free(args);return r;
    }
    case N_LET:env_set(env,n->str,eval(n->a,env));return vnil();
    case N_ASSIGN:env_assign(env,n->str,eval(n->a,env));return vnil();
    case N_PRINT:{Value v=eval(n->a,env);char*s=to_str(v);printf("%s\n",s);free(s);return vnil();}
    case N_EXPR:return eval(n->a,env);
    case N_BLOCK:{
      Env*saved_env=g_current_env;
      Env*be=env_new(env);
      g_current_env=be;
      for(int i=0;i<n->count;i++){eval(n->list[i],be);if(g_ret)break;}
      g_current_env=saved_env;
      return vnil();
    }
    case N_IF:
      if(truthy(eval(n->a,env)))eval(n->b,env);
      else if(n->c)eval(n->c,env);
      return vnil();
    case N_WHILE:
      while(truthy(eval(n->a,env))){eval(n->b,env);if(g_ret)break;}
      return vnil();
    case N_FOR:{
      double lim=eval(n->a,env).num;
      env_set(env,n->str,vnum(0));
      for(int i=0;i<(int)lim;i++){
        env_assign(env,n->str,vnum(i));
        eval(n->b,env);
        if(g_ret)break;
      }
      return vnil();
    }
    case N_FN:{
      Fn*f=calloc(1,sizeof(Fn));
      f->name=n->str;f->params=n->params;f->nparams=n->count;
      f->body=n->a;f->closure=env;
      env_set(env,n->str,vfn(f));
      return vnil();
    }
    case N_RETURN:
      g_retv=n->a?eval(n->a,env):vnil();
      g_ret=1;return vnil();
  }
  return vnil();
}

/* ============================================================
   11. BUILTINS
   ============================================================ */
#define B1(n,e) static Value bi_##n(Value*a,int nargs){(void)nargs;return e;}
B1(sqrt,vnum(sqrt(a[0].num)))
B1(abs,vnum(fabs(a[0].num)))
B1(floor,vnum(floor(a[0].num)))
B1(ceil,vnum(ceil(a[0].num)))
B1(exp,vnum(exp(a[0].num)))
B1(log,vnum(log(a[0].num)))
B1(sin_,vnum(sin(a[0].num)))
B1(cos_,vnum(cos(a[0].num)))

static Value bi_pow(Value*a,int n){(void)n;return vnum(pow(a[0].num,a[1].num));}
static Value bi_min(Value*a,int n){(void)n;return vnum(a[0].num<a[1].num?a[0].num:a[1].num);}
static Value bi_max(Value*a,int n){(void)n;return vnum(a[0].num>a[1].num?a[0].num:a[1].num);}
static Value bi_len(Value*a,int n){
  (void)n;
  if(a[0].type==V_STR)return vnum((double)strlen(a[0].str));
  if(a[0].type==V_TENSOR)return vnum(a[0].tensor->size);
  die("len expects string or tensor",0);return vnil();
}
static Value bi_str(Value*a,int n){(void)n;char*s=to_str(a[0]);Value v=vstr(s);free(s);return v;}
static Value bi_num(Value*a,int n){
  (void)n;
  switch(a[0].type){
    case V_NUM:return a[0];
    case V_BOOL:return vnum(a[0].num);
    case V_STR:return vnum(atof(a[0].str));
    case V_TENSOR:
      if(a[0].tensor->size>0)return vnum(a[0].tensor->data[0]);
      return vnum(0);
    default:return vnum(0);
  }
}
static Value bi_rand(Value*a,int n){(void)a;(void)n;return vnum((double)rand()/RAND_MAX);}
static Value bi_seed(Value*a,int n){(void)n;srand((unsigned)a[0].num);return vnil();}

static Value bi_zeros(Value*a,int n){int d[4];for(int i=0;i<n;i++)d[i]=(int)a[i].num;return vt(tn_new(n,d));}
static Value bi_ones(Value*a,int n){
  int d[4];for(int i=0;i<n;i++)d[i]=(int)a[i].num;
  TNode*t=tn_new(n,d);for(int i=0;i<t->size;i++)t->data[i]=1.0;
  return vt(t);
}
static Value bi_randn(Value*a,int n){
  int d[4];for(int i=0;i<n;i++)d[i]=(int)a[i].num;
  TNode*t=tn_new(n,d);
  for(int i=0;i<t->size;i++){
    double u1=(rand()+1.0)/(RAND_MAX+2.0);
    double u2=(rand()+1.0)/(RAND_MAX+2.0);
    t->data[i]=sqrt(-2*log(u1))*cos(2*M_PI*u2);
  }
  return vt(t);
}
static Value bi_arange(Value*a,int n){
  (void)n;int L=(int)a[0].num;int d[1]={L};
  TNode*t=tn_new(1,d);
  for(int i=0;i<L;i++)t->data[i]=(double)i;
  return vt(t);
}
static Value bi_shape(Value*a,int n){
  (void)n;TNode*t=a[0].tensor;int d[1]={t->ndims};
  TNode*o=tn_new(1,d);
  for(int i=0;i<t->ndims;i++)o->data[i]=(double)t->dims[i];
  return vt(o);
}
static Value bi_data(Value*a,int n){
  (void)n;TNode*t=a[0].tensor;int d[1]={t->size};
  TNode*o=tn_new(1,d);memcpy(o->data,t->data,t->size*sizeof(double));
  return vt(o);
}
static Value bi_relu(Value*a,int n){(void)n;return vt(tn_relu(a[0].tensor));}
static Value bi_sigmoid(Value*a,int n){(void)n;return vt(tn_sigmoid(a[0].tensor));}
static Value bi_tanh(Value*a,int n){(void)n;return vt(tn_tanh(a[0].tensor));}
static Value bi_gelu(Value*a,int n){(void)n;return vt(tn_gelu(a[0].tensor));}
static Value bi_mse(Value*a,int n){(void)n;return vt(tn_mse(a[0].tensor,a[1].tensor));}
static Value bi_bce(Value*a,int n){(void)n;return vt(tn_bce(a[0].tensor,a[1].tensor));}
static Value bi_sum(Value*a,int n){(void)n;return vt(tn_sum(a[0].tensor));}
static Value bi_mean(Value*a,int n){(void)n;return vt(tn_mean(a[0].tensor));}
static Value bi_trans(Value*a,int n){(void)n;return vt(tn_trans(a[0].tensor));}
static Value bi_softmax(Value*a,int n){(void)n;return vt(tn_softmax(a[0].tensor));}
static Value bi_layernorm(Value*a,int n){(void)n;return vt(tn_layernorm(a[0].tensor));}
static Value bi_cross_entropy(Value*a,int n){(void)n;return vt(tn_cross_entropy(a[0].tensor,a[1].tensor));}
static Value bi_attention(Value*a,int n){(void)n;return vt(tn_attention(a[0].tensor,a[1].tensor,a[2].tensor));}
static Value bi_embedding(Value*a,int n){(void)n;return vt(tn_embedding(a[0].tensor,a[1].tensor));}
static Value bi_posenc(Value*a,int n){(void)n;return vt(tn_posenc(a[0].tensor));}
static Value bi_dropout(Value*a,int n){(void)n;return vt(tn_dropout(a[0].tensor,a[1].num));}
static Value bi_concat(Value*a,int n){(void)n;return vt(tn_concat(a[0].tensor,a[1].tensor));}
static Value bi_train(Value*a,int n){(void)a;(void)n;g_training=1;return vnil();}
static Value bi_eval_mode(Value*a,int n){(void)a;(void)n;g_training=0;return vnil();}
static Value bi_backward(Value*a,int n){(void)n;backward(a[0].tensor);return vnil();}

static Value bi_grad(Value*a,int n){
  (void)n;TNode*t=a[0].tensor;
  TNode*o=tn_new(t->ndims,t->dims);
  if(t->grad){
    memcpy(o->data,t->grad,t->size*sizeof(double));
    memset(t->grad,0,t->size*sizeof(double));
  }
  return vt(o);
}
static Value bi_clip(Value*a,int n){
  (void)n;TNode*t=a[0].tensor;double lim=a[1].num;
  TNode*o=tn_new(t->ndims,t->dims);
  for(int i=0;i<t->size;i++){
    double v=t->data[i];
    if(v>lim)v=lim;else if(v<-lim)v=-lim;
    o->data[i]=v;
  }
  return vt(o);
}
static Value bi_reset(Value*a,int n){(void)a;(void)n;tape_reset();return vnil();}
static Value bi_tapesize(Value*a,int n){(void)a;(void)n;return vnum((double)g_tape_n);}

static Value bi_adam(Value*a,int n){
  (void)n;
  if(a[0].type!=V_STR)die("adam: name must be string",0);
  TNode*P=a[1].tensor,*G=a[2].tensor;
  if(P->size!=G->size)die("adam: size mismatch",0);
  adam_step(a[0].str,P->data,G->data,P->size,a[3].num,0.9,0.999,1e-8);
  return vnil();
}
static Value bi_sgd(Value*a,int n){
  (void)n;TNode*P=a[0].tensor,*G=a[1].tensor;
  if(P->size!=G->size)die("sgd: size mismatch",0);
  double lr=a[2].num;
  for(int i=0;i<P->size;i++)P->data[i]-=lr*G->data[i];
  return vnil();
}
static Value bi_read_text(Value*a,int n){
  (void)n;
  if(a[0].type!=V_STR)die("read_text: string path required",0);
  FILE*f=fopen(a[0].str,"rb");
  if(!f){fprintf(stderr,"iris: cannot open %s\n",a[0].str);exit(1);}
  fseek(f,0,SEEK_END);long z=ftell(f);fseek(f,0,SEEK_SET);
  char*b=malloc(z+1);
  if(fread(b,1,z,f)!=(size_t)z){}
  b[z]=0;fclose(f);
  Value v=vstr(b);free(b);return v;
}
static Value bi_write_text(Value*a,int n){
  (void)n;
  if(a[0].type!=V_STR||a[1].type!=V_STR)die("write_text: (path, text)",0);
  FILE*f=fopen(a[0].str,"wb");
  if(!f){fprintf(stderr,"iris: cannot write %s\n",a[0].str);exit(1);}
  fwrite(a[1].str,1,strlen(a[1].str),f);fclose(f);
  return vnil();
}
static Value bi_save(Value*a,int n){
  if(n!=2)die("save(tensor, path)",0);
  if(a[0].type!=V_TENSOR||a[1].type!=V_STR)die("save: (tensor, path)",0);
  FILE*f=fopen(a[1].str,"wb");
  if(!f)die("save: cannot open",0);
  fwrite("IRIS6\0",1,6,f);
  int32_t nd=a[0].tensor->ndims,sz=a[0].tensor->size;
  int32_t dims[4];for(int i=0;i<4;i++)dims[i]=a[0].tensor->dims[i];
  fwrite(&nd,4,1,f);fwrite(dims,4,4,f);fwrite(&sz,4,1,f);
  fwrite(a[0].tensor->data,sizeof(double),sz,f);
  fclose(f);return vnil();
}
static Value bi_load(Value*a,int n){
  (void)n;
  if(a[0].type!=V_STR)die("load: path required",0);
  FILE*f=fopen(a[0].str,"rb");if(!f)die("load: cannot open",0);
  char mg[6];if(fread(mg,1,6,f)!=6)die("load: short",0);
  if(memcmp(mg,"IRIS6\0",6))die("load: bad magic (not Iris v6)",0);
  int32_t nd,sz,dims[4];
  if(fread(&nd,4,1,f)!=1)die("load",0);
  if(fread(dims,4,4,f)!=4)die("load",0);
  if(fread(&sz,4,1,f)!=1)die("load",0);
  TNode*t=tn_new(nd,dims);
  if(fread(t->data,sizeof(double),sz,f)!=(size_t)sz)die("load: short data",0);
  fclose(f);return vt(t);
}
static Value bi_tokenize(Value*a,int n){
  (void)n;
  if(a[0].type!=V_STR)die("tokenize: string required",0);
  int len=strlen(a[0].str);
  int d[1]={len};TNode*t=tn_new(1,d);
  for(int i=0;i<len;i++)t->data[i]=(unsigned char)a[0].str[i];
  return vt(t);
}
static Value bi_detokenize(Value*a,int n){
  (void)n;
  if(a[0].type!=V_TENSOR)die("detokenize: tensor required",0);
  TNode*t=a[0].tensor;
  char*buf=malloc(t->size+1);
  for(int i=0;i<t->size;i++){
    int v=(int)t->data[i];
    if(v<0)v=0;if(v>255)v=255;
    buf[i]=(char)v;
  }
  buf[t->size]=0;
  Value v=vstr(buf);free(buf);return v;
}
static Value bi_vocab_size(Value*a,int n){(void)a;(void)n;return vnum(256);}
static Value bi_sample(Value*a,int n){
  (void)n;
  TNode*L=a[0].tensor;
  int C=L->size;
  double mx=L->data[0];
  for(int i=1;i<C;i++)if(L->data[i]>mx)mx=L->data[i];
  double sum=0;
  double*probs=malloc(sizeof(double)*C);
  for(int i=0;i<C;i++){probs[i]=exp(L->data[i]-mx);sum+=probs[i];}
  for(int i=0;i<C;i++)probs[i]/=sum;
  double r=(double)rand()/RAND_MAX,acc=0;
  int picked=C-1;
  for(int i=0;i<C;i++){acc+=probs[i];if(r<=acc){picked=i;break;}}
  free(probs);
  return vnum((double)picked);
}
static Value bi_slice(Value*a,int n){
  (void)n;TNode*t=a[0].tensor;
  int start=(int)a[1].num,len=(int)a[2].num;
  if(start<0)start=0;if(start+len>t->size)len=t->size-start;
  if(len<0)len=0;
  int d[1]={len};TNode*o=tn_new(1,d);
  for(int i=0;i<len;i++)o->data[i]=t->data[start+i];
  return vt(o);
}
static Value bi_at(Value*a,int n){
  (void)n;TNode*t=a[0].tensor;int i=(int)a[1].num;
  if(i<0||i>=t->size)return vnum(0);
  return vnum(t->data[i]);
}
static Value bi_set_at(Value*a,int n){
  (void)n;TNode*t=a[0].tensor;int i=(int)a[1].num;double v=a[2].num;
  if(i>=0&&i<t->size)t->data[i]=v;
  return a[0];
}
static Value bi_push(Value*a,int n){
  (void)n;TNode*t=a[0].tensor;double v=a[1].num;
  int d[1]={t->size+1};TNode*o=tn_new(1,d);
  for(int i=0;i<t->size;i++)o->data[i]=t->data[i];
  o->data[t->size]=v;
  return vt(o);
}
static Value bi_row(Value*a,int n){
  (void)n;TNode*t=a[0].tensor;
  if(t->ndims!=2)die("row: needs 2D tensor",0);
  int i=(int)a[1].num,cols=t->dims[1];
  if(i<0||i>=t->dims[0])die("row: out of bounds",0);
  int d[1]={cols};TNode*o=tn_new(1,d);
  for(int j=0;j<cols;j++)o->data[j]=t->data[i*cols+j];
  return vt(o);
}
static Value bi_argmax(Value*a,int n){
  (void)n;TNode*t=a[0].tensor;int best=0;
  for(int i=1;i<t->size;i++)if(t->data[i]>t->data[best])best=i;
  return vnum((double)best);
}
static Value bi_chr(Value*a,int n){
  (void)n;int v=(int)a[0].num;
  if(v<0)v=0;if(v>255)v=255;
  char b[2]={(char)v,0};return vstr(b);
}
static Value bi_clock(Value*a,int n){
  (void)a;(void)n;
  struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);
  return vnum(ts.tv_sec+ts.tv_nsec/1e9);
}
static Value bi_sleep_ms(Value*a,int n){
  (void)n;int ms=(int)a[0].num;if(ms<0)ms=0;
  struct timespec ts={ms/1000,(long)(ms%1000)*1000000L};
  nanosleep(&ts,NULL);return vnil();
}
static Value bi_gc(Value*a,int n){(void)a;(void)n;gc_from(g_current_env);return vnum((double)g_all_n);}
static Value bi_meminfo(Value*a,int n){
  (void)a;(void)n;
  long total=0;
  for(int i=0;i<g_all_n;i++)total+=g_all[i]->size*8;
  printf("tensors=%d  bytes=%ld\n",g_all_n,total);
  return vnum((double)total);
}

static void def(Env*e,const char*name,NativeFn fn,int arity){
  Fn*f=calloc(1,sizeof(Fn));
  f->name=xs(name);f->nparams=arity;f->native=fn;
  env_set(e,name,vfn(f));
}
static void reg_builtins(Env*e){
  def(e,"sqrt",bi_sqrt,1);def(e,"abs",bi_abs,1);
  def(e,"floor",bi_floor,1);def(e,"ceil",bi_ceil,1);
  def(e,"pow",bi_pow,2);def(e,"min",bi_min,2);def(e,"max",bi_max,2);
  def(e,"exp",bi_exp,1);def(e,"log",bi_log,1);
  def(e,"sin",bi_sin_,1);def(e,"cos",bi_cos_,1);
  def(e,"relu",bi_relu,1);def(e,"sigmoid",bi_sigmoid,1);
  def(e,"tanh",bi_tanh,1);def(e,"gelu",bi_gelu,1);
  def(e,"mse",bi_mse,2);def(e,"bce",bi_bce,2);
  def(e,"cross_entropy",bi_cross_entropy,2);
  def(e,"sum",bi_sum,1);def(e,"mean",bi_mean,1);
  def(e,"trans",bi_trans,1);def(e,"softmax",bi_softmax,1);
  def(e,"layernorm",bi_layernorm,1);
  def(e,"attention",bi_attention,3);
  def(e,"embedding",bi_embedding,2);
  def(e,"posenc",bi_posenc,1);
  def(e,"dropout",bi_dropout,2);
  def(e,"concat",bi_concat,2);
  def(e,"train",bi_train,0);
  def(e,"eval_mode",bi_eval_mode,0);
  def(e,"backward",bi_backward,1);
  def(e,"grad",bi_grad,1);
  def(e,"clip",bi_clip,2);
  def(e,"reset",bi_reset,0);
  def(e,"tapesize",bi_tapesize,0);
  def(e,"gc",bi_gc,0);
  def(e,"meminfo",bi_meminfo,0);
  def(e,"adam",bi_adam,4);
  def(e,"sgd",bi_sgd,3);
  def(e,"zeros",bi_zeros,-1);def(e,"ones",bi_ones,-1);def(e,"randn",bi_randn,-1);
  def(e,"arange",bi_arange,1);def(e,"shape",bi_shape,1);def(e,"data",bi_data,1);
  def(e,"len",bi_len,1);def(e,"str",bi_str,1);def(e,"num",bi_num,1);
  def(e,"rand",bi_rand,0);def(e,"seed",bi_seed,1);
  def(e,"save",bi_save,2);def(e,"load",bi_load,1);
  def(e,"read_text",bi_read_text,1);
  def(e,"write_text",bi_write_text,2);
  def(e,"tokenize",bi_tokenize,1);
  def(e,"detokenize",bi_detokenize,1);
  def(e,"vocab_size",bi_vocab_size,0);
  def(e,"sample",bi_sample,1);
  def(e,"slice",bi_slice,3);
  def(e,"at",bi_at,2);
  def(e,"set_at",bi_set_at,3);
  def(e,"push",bi_push,2);
  def(e,"row",bi_row,2);
  def(e,"argmax",bi_argmax,1);
  def(e,"chr",bi_chr,1);
  def(e,"clock",bi_clock,0);
  def(e,"sleep_ms",bi_sleep_ms,1);
}

/* ============================================================
   12. DRIVER
   ============================================================ */
static void run(const char*src,Env*env){
  int n;Token*toks=lex(src,&n);Parser p={toks,n,0};
  g_ret=0;
  Env*saved=g_current_env;
  g_current_env=env;
  while(!ck(&p,T_EOF)){
    Node*s=parse_stmt(&p);
    eval(s,env);
    if(g_ret){g_ret=0;break;}
  }
  g_current_env=saved;
}
static char *slurp(const char*p){
  FILE*f=fopen(p,"rb");if(!f){fprintf(stderr,"iris: cannot open %s\n",p);exit(1);}
  fseek(f,0,SEEK_END);long z=ftell(f);fseek(f,0,SEEK_SET);
  char*b=malloc(z+1);
  if(fread(b,1,z,f)!=(size_t)z){}
  b[z]=0;fclose(f);return b;
}
int main(int argc,char**argv){
  srand((unsigned)time(NULL));
  Env*g=env_new(NULL);
  g_current_env=g;
  reg_builtins(g);
  if(argc>1){char*s=slurp(argv[1]);run(s,g);free(s);return 0;}
  printf("Iris 6.0 — an AI-native language\n");
  printf("Type 'exit' to quit.\n\n");
  char line[4096];
  for(;;){
    printf("iris> ");fflush(stdout);
    if(!fgets(line,sizeof line,stdin))break;
    if(!strncmp(line,"exit",4))break;
    run(line,g);
  }
  return 0;
}
