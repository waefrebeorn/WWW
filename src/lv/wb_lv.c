/*
 * wb_lv.c -- WuBu LV: slerm-Lua interpreter (slice 8 foundation).
 *
 * A clean tree-walking subset interpreter. This milestone is about a portable,
 * correct, host-bindable script engine that meets the wb_script contract.
 * The register VS fast and benchmark-beats-stock-Lua claim is the NEXT
 * milestone -- no speed number is made here.
 *
 * Subset: nil/bool/number/string(interned)/table/function; local + assign;
 * if/elseif/else; while; numeric for; table literals; index t[k] and t.k;
 * function defs capturing the creation scope; calls (lua + host cfn);
 * return; arithmetic/comparison/logical/concat. No coroutines/metamethods.
 */
#include "wb_lv.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ========================================================================
 * arena memory (NON-intrusive: an 8-byte header precedes each block, so the
 * arena works on any struct -- the old intrusive version wrote into the
 * FIRST field of every allocation and corrupted interners/tables).
 * ======================================================================== */
typedef struct anode { struct anode *next; } anode;

struct wb_lv {
    struct anode *root;
    struct istr **buckets; size_t nbuckets;
    wb_lv_table *globals;
    wb_lv_value *stack; int sp, scap;
    char err[512]; int has_err;
    int depth;
};

static void *arena_alloc(wb_lv *L, size_t n) {
    anode *h = calloc(1, sizeof(anode) + n);
    if (!h) return NULL;
    h->next = L->root; L->root = h;
    return (void *)(h + 1);
}
#define XALLOC(T, L)     ((T*)arena_alloc((L), sizeof(T)))
#define XALLOCN(T, n, L) ((T*)arena_alloc((L), (n) * sizeof(T)))

/* ========================================================================
 * values
 * ======================================================================== */
int wb_lv_truthy(wb_lv_value v) {
    if (v.t == WB_LV_NIL) return 0;
    if (v.t == WB_LV_BOOL) return v.as.b;
    return 1;
}
int wb_lv_equal(wb_lv_value a, wb_lv_value b) {
    if (a.t != b.t) return 0;
    switch (a.t) {
    case WB_LV_NIL: return 1;
    case WB_LV_BOOL: return a.as.b == b.as.b;
    case WB_LV_NUM: return a.as.n == b.as.n;
    case WB_LV_STR: return strcmp(a.as.s, b.as.s) == 0;
    case WB_LV_TABLE: return a.as.tab == b.as.tab;
    case WB_LV_FUNCTION: return a.as.fn == b.as.fn;
    case WB_LV_CFUNCTION: return a.as.cf.f == b.as.cf.f;
    }
    return 0;
}
double wb_lv_tonumber(wb_lv_value v, int *ok) {
    if (v.t == WB_LV_NUM) { if (ok) *ok = 1; return v.as.n; }
    if (ok) *ok = 0;
    return v.as.n;
}
const char *wb_lv_tostring(wb_lv_value v) {
    static char fb[64];
    if (v.t == WB_LV_STR) return v.as.s;
    if (v.t == WB_LV_NUM) { snprintf(fb, sizeof fb, "%g", v.as.n); return fb; }
    return "(value)";
}

/* ========================================================================
 * string interning
 * ======================================================================== */
typedef struct istr { char *s; size_t len; unsigned hash; struct istr *next; } istr;
static unsigned strhash(const char *s, size_t n) {
    unsigned h = 2166136261u;
    for (size_t i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 16777619u; }
    return h;
}
const char *wb_lv_intern(wb_lv *L, const char *bytes, size_t len) {
    unsigned h = strhash(bytes, len), b = h % L->nbuckets;
    for (istr *it = L->buckets[b]; it; it = it->next)
        if (it->len == len && memcmp(it->s, bytes, len) == 0) return it->s;
    istr *n = arena_alloc(L, sizeof(*n) + len + 1);
    if (!n) return NULL;
    n->len = len; n->hash = h; n->s = (char *)(n + 1);
    memcpy(n->s, bytes, len); n->s[len] = 0;
    n->next = L->buckets[b]; L->buckets[b] = n;
    return n->s;
}

/* ========================================================================
 * tables (open addressing, no-delete; replace on hit, grow by doubling)
 * ======================================================================== */
typedef struct wb_lv_kv { wb_lv_value k, v; int used; } wb_lv_kv;
struct wb_lv_table { wb_lv_kv *e; size_t cap, count; };

static unsigned valhash(wb_lv_value k) {
    if (k.t == WB_LV_STR) return strhash(k.as.s, strlen(k.as.s));
    if (k.t == WB_LV_NUM) { double d = k.as.n; return (unsigned)((unsigned long long)(d * 2654435761.0)); }
    return (unsigned)(uintptr_t)k.as.tab | (unsigned)(uintptr_t)k.as.fn;
}
static int keyeq(wb_lv_value a, wb_lv_value b) {
    if (a.t != b.t) return 0;
    if (a.t == WB_LV_NUM) return a.as.n == b.as.n;
    if (a.t == WB_LV_STR) return a.as.s == b.as.s;   /* interned + same hash table */
    return wb_lv_equal(a, b);
}
static void tgrow(wb_lv *L, wb_lv_table *t) {
    size_t nc = t->cap ? t->cap * 2 : 16;
    wb_lv_kv *ne = arena_alloc(L, nc * sizeof(*ne));
    if (t->e) for (size_t i = 0; i < t->cap; i++)
        if (t->e[i].used) {
            unsigned hi = valhash(t->e[i].k) % nc, j = hi;
            while (ne[j].used) j = (j + 1) % nc;
            ne[j] = t->e[i];
        }
    t->e = ne; t->cap = nc;
}
static wb_lv_kv *tfind(wb_lv_table *t, wb_lv_value k) {
    if (!t->cap) return NULL;
    unsigned hi = valhash(k) % t->cap, j = hi;
    while (t->e[j].used) {
        if (keyeq(t->e[j].k, k)) return &t->e[j];
        j = (j + 1) % t->cap;
    }
    return NULL;
}
wb_lv_table *wb_lv_table_new(wb_lv *L, int narr, int nrec) {
    (void)narr; (void)nrec;
    return XALLOC(wb_lv_table, L);
}
void wb_lv_table_set(wb_lv *L, wb_lv_table *t, wb_lv_value k, wb_lv_value v) {
    if (t->count * 2 >= t->cap) tgrow(L, t);
    wb_lv_kv *f = tfind(t, k);
    if (f) { f->v = v; return; }
    unsigned hi = valhash(k) % t->cap, j = hi;
    while (t->e[j].used) j = (j + 1) % t->cap;
    t->e[j].k = k; t->e[j].v = v; t->e[j].used = 1; t->count++;
}
wb_lv_value wb_lv_table_get(wb_lv *L, const wb_lv_table *tt, wb_lv_value k) {
    (void)L;
    wb_lv_kv *f = tfind((wb_lv_table *)tt, k);
    return f ? f->v : wb_lv_nil();
}

/* ----------------------------------------------------------------------
 * AST
 * ---------------------------------------------------------------------- */
typedef enum {
    N_NUM, N_STR, N_BOOL, N_NIL, N_NAME, N_INDEX, N_BINOP, N_UNARY, N_TABLE,
    N_CALL, N_FUNC, N_LOCAL, N_ASSIGN, N_IF, N_WHILE, N_FOR, N_RETURN, N_BLOCK,
} ntype;

typedef struct ex {
    ntype t; int line;
    double num; const char *str; int boolean; const char *name;
    struct ex *e1, *e2, *e3;          /* operands / cond / body / else */
    struct ex **list; int nlist;      /* stmts / args / params / fields */
    struct ex **keys;                 /* table: {list=values, keys=key-expr} */
    int is_method; struct ex *receiver;
} ex;

#define MAX_EX 8192
typedef struct { ex n[MAX_EX]; int cnt; } pool;
static ex *espawn(pool *p) {
    if (p->cnt >= MAX_EX) return NULL;
    memset(&p->n[p->cnt], 0, sizeof(ex)); return &p->n[p->cnt++];
}

/* ----------------------------------------------------------------------
 * parser
 * ---------------------------------------------------------------------- */
typedef enum {
    T_EOF=0, T_NUM=256, T_STR, T_NAME,
    T_IF,T_THEN,T_ELSE,T_ELSEIF,T_END,T_WHILE,T_DO,T_FOR,T_FUNCTION,
    T_LOCAL,T_RETURN,T_IN,T_TRUE,T_FALSE,T_NIL,T_NOT,T_AND,T_OR,
    T_NE,T_LE,T_GE,T_CONCAT,T_EQ,   /* multi-char/compound */
} tktype;

typedef struct { int type; double num; const char *str; int line; } tok;

#define TKCHAR(c) ((int)(unsigned char)(c))     /* single-char tokens < 256 */

typedef struct {
    const char *src; size_t p; int line;
    tok cur;
    wb_lv *L; pool *pl; int err;
} parser;

/* forward */
static ex *parse_expr(parser *P);
static ex *parse_stmt(parser *P);
static ex *parse_block(parser *P, const char *until, const char *until2, int *matched);

static void perr(parser *P, const char *msg) {
    if (!P->err) { snprintf(P->L->err, sizeof P->L->err, "%s (line %d)", msg, P->line); P->L->has_err = 1; P->err = 1; }
}

static void next_tok(parser *P) {
    const char *s = P->src; size_t n = strlen(s);
    /* skip whitespace + comments */
    for (;;) {
        while (P->p < n && (s[P->p] == ' ' || s[P->p] == '\t' || s[P->p] == '\r')) P->p++;
        if (P->p < n && s[P->p] == '\n') { P->line++; P->p++; continue; }
        if (P->p + 1 < n && s[P->p] == '-' && s[P->p+1] == '-') {
            P->p += 2; while (P->p < n && s[P->p] != '\n') P->p++; continue;
        }
        break;
    }
    tok t; memset(&t, 0, sizeof t); t.line = P->line;
    if (P->p >= n) { t.type = T_EOF; P->cur = t; return; }
    char c = s[P->p];
    (void)t.str;
    /* number */
    if (c >= '0' && c <= '9') {
        double v = 0;
        int isfrac = 0; double scale = 0.1;
        while (P->p < n) {
            char d = s[P->p];
            if (d >= '0' && d <= '9') {
                if (!isfrac) v = v * 10 + (d - '0');
                else { v += (d - '0') * scale; scale *= 0.1; }
                P->p++;
            } else if (d == '.' && !isfrac) { isfrac = 1; P->p++; }
            else break;
        }
        /* exponent (simple, optional) */
        if (P->p < n && (s[P->p] == 'e' || s[P->p] == 'E')) {
            size_t save = P->p; P->p++;
            int sgn = 1, expdig = 0;
            if (P->p < n && (s[P->p] == '+' || s[P->p] == '-')) { if (s[P->p] == '-') sgn = -1; P->p++; }
            double e = 0;
            while (P->p < n && s[P->p] >= '0' && s[P->p] <= '9') { e = e * 10 + (s[P->p] - '0'); expdig = 1; P->p++; }
            if (expdig) v *= pow(10.0, sgn * e);
            else P->p = save;   /* not actually an exponent */
        }
        P->cur.type = T_NUM; P->cur.num = v;
        return;
    }
    /* identifier or keyword */
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
        size_t st = P->p;
        while (P->p < n && ((s[P->p]>='a'&&s[P->p]<='z')||(s[P->p]>='A'&&s[P->p]<='Z')
               ||s[P->p]=='_'||(s[P->p]>='0'&&s[P->p]<='9'))) P->p++;
        size_t wl = P->p - st;
        char w[64]; if (wl >= sizeof w) wl = sizeof w - 1; memcpy(w, s+st, wl); w[wl]=0;
        int kw = 1;
        if      (!strcmp(w,"if"))        t.type=T_IF;
        else if (!strcmp(w,"then"))      t.type=T_THEN;
        else if (!strcmp(w,"else"))      t.type=T_ELSE;
        else if (!strcmp(w,"elseif"))    t.type=T_ELSEIF;
        else if (!strcmp(w,"end"))       t.type=T_END;
        else if (!strcmp(w,"while"))     t.type=T_WHILE;
        else if (!strcmp(w,"do"))        t.type=T_DO;
        else if (!strcmp(w,"for"))       t.type=T_FOR;
        else if (!strcmp(w,"function"))  t.type=T_FUNCTION;
        else if (!strcmp(w,"local"))     t.type=T_LOCAL;
        else if (!strcmp(w,"return"))    t.type=T_RETURN;
        else if (!strcmp(w,"in"))        t.type=T_IN;
        else if (!strcmp(w,"true"))      t.type=T_TRUE;
        else if (!strcmp(w,"false"))     t.type=T_FALSE;
        else if (!strcmp(w,"nil"))       t.type=T_NIL;
        else if (!strcmp(w,"not"))       t.type=T_NOT;
        else if (!strcmp(w,"and"))       t.type=T_AND;
        else if (!strcmp(w,"or"))        t.type=T_OR;
        else kw = 0;
        if (!kw) { t.type = T_NAME; t.str = wb_lv_intern(P->L, w, wl); }
        else t.type = t.type;
        P->cur = t; return;
    }
    if (c == '"' || c == '\'') {
        char q = c; P->p++;
        char buf[256]; size_t bl = 0;
        while (P->p < n && s[P->p] != q) {
            if (s[P->p] == '\\' && P->p + 1 < n) { P->p++; buf[bl++] = s[P->p++]; }
            else buf[bl++] = s[P->p++];
        }
        if (P->p < n) P->p++; /* closing quote */
        buf[bl] = 0;
        t.type = T_STR; t.str = wb_lv_intern(P->L, buf, bl);
        P->cur = t; return;
    }
    /* punctuation */
    P->p++;
    switch (c) {
    case '=': if (s[P->p]=='='){P->p++; t.type=T_EQ;} else t.type='='; break;
    case '<': if (s[P->p]=='='){P->p++; t.type=T_LE;} else t.type='<'; break;
    case '>': if (s[P->p]=='='){P->p++; t.type=T_GE;} else t.type='>'; break;
    case '~': if (s[P->p]=='='){P->p++; t.type=T_NE;} else t.type='~'; break;
    case '.': if (s[P->p]=='.'){P->p++; t.type=T_CONCAT;} else t.type='.'; break;
    default: t.type = TKCHAR(c); break;
    }
    P->cur = t;
}

/* token accessor */
static int tok_is(parser *P, int type) { return P->cur.type == type; }
static void expect(parser *P, int type, const char *what) {
    if (!tok_is(P, type)) perr(P, what);
    next_tok(P);
}

/* precedence climbing for binary expressions */
static ex *parse_bin(parser *P, int minprec);

static ex *parse_primary(parser *P) {
    ex *e = NULL;
    tok t = P->cur;
    switch (t.type) {
    case T_NUM:  { e = espawn(P->pl); if(!e){perr(P,"pool");return NULL;} e->t=N_NUM; e->num=t.num; next_tok(P); break; }
    case T_STR:  { e = espawn(P->pl); if(!e){perr(P,"pool");return NULL;} e->t=N_STR; e->str=t.str; next_tok(P); break; }
    case T_TRUE: { e = espawn(P->pl); if(!e){perr(P,"pool");return NULL;} e->t=N_BOOL; e->boolean=1; next_tok(P); break; }
    case T_FALSE:{ e = espawn(P->pl); if(!e){perr(P,"pool");return NULL;} e->t=N_BOOL; e->boolean=0; next_tok(P); break; }
    case T_NIL:  { e = espawn(P->pl); if(!e){perr(P,"pool");return NULL;} e->t=N_NIL; next_tok(P); break; }
    case T_NAME: { e = espawn(P->pl); if(!e){perr(P,"pool");return NULL;} e->t=N_NAME; e->name=t.str; next_tok(P); break; }
    case '{': {   /* table literal is a PRIMARY (t = {...}) */
        next_tok(P);
        ex *tab = espawn(P->pl); if(!tab){perr(P,"pool");return NULL;}
        tab->t = N_TABLE;
        ex **vals=NULL,**keys=NULL; int nv=0,cap=0;
        while (!tok_is(P, '}')) {
            ex *key = NULL;
            /* bare-name field: name = value  (key becomes the interned name) */
            if (tok_is(P, T_NAME)) {
                const char *nm = P->cur.str;
                next_tok(P);
                if (tok_is(P, '=')) {
                    key = espawn(P->pl); if(!key){perr(P,"pool");return NULL;}
                    key->t = N_STR; key->str = nm;
                    next_tok(P);          /* consume '=' */
                    ex *val = parse_expr(P);
                    if (nv>=cap){cap=cap?cap*2:8; vals=realloc(vals,cap*sizeof(*vals)); keys=realloc(keys,cap*sizeof(*keys));}
                    vals[nv]=val; keys[nv]=key; nv++;
                    if (tok_is(P, ',')) next_tok(P);
                    continue;
                }
                /* not a name= -- it was a bare expression; fall through with
                 * the name re-tokenized: we already consumed it, so stash a
                 * NAME node as the value and continue. */
                ex *nmval = espawn(P->pl); if(!nmval){perr(P,"pool");return NULL;}
                nmval->t = N_NAME; nmval->name = nm;
                if (nv>=cap){cap=cap?cap*2:8; vals=realloc(vals,cap*sizeof(*vals)); keys=realloc(keys,cap*sizeof(*keys));}
                vals[nv]=nmval; keys[nv]=NULL; nv++;
                if (tok_is(P, ',')) next_tok(P);
                continue;
            }
            if (tok_is(P, '[')) { next_tok(P); key = parse_expr(P); expect(P,']',"expected ']'"); }
            if (tok_is(P, '=')) {
                next_tok(P);
                ex *val = parse_expr(P);
                if (nv>=cap){cap=cap?cap*2:8; vals=realloc(vals,cap*sizeof(*vals)); keys=realloc(keys,cap*sizeof(*keys));}
                vals[nv]=val; keys[nv]=key; nv++;
            } else {
                ex *val = parse_expr(P);
                if (nv>=cap){cap=cap?cap*2:8; vals=realloc(vals,cap*sizeof(*vals)); keys=realloc(keys,cap*sizeof(*keys));}
                vals[nv]=val; keys[nv]=key; nv++;
            }
            if (tok_is(P, ',')) next_tok(P);
        }
        next_tok(P); /* } */
        tab->list = vals; tab->keys = keys; tab->nlist = nv;
        e = tab;
        break;
    }
    case '(': {
        next_tok(P);
        e = parse_expr(P);
        expect(P, ')', "expected ')'");
        break;
    }
    default:
        perr(P, "unexpected token"); next_tok(P); break;
    }
    /* postfix: index and call and method */
    for (;;) {
        if (tok_is(P, '[')) {
            next_tok(P);
            ex *idx = parse_expr(P);
            expect(P, ']', "expected ']'");
            ex *n = espawn(P->pl); if(!n){perr(P,"pool");return NULL;}
            n->t = N_INDEX; n->e1 = e; n->e2 = idx; e = n;
        } else if (tok_is(P, '.')) {
            next_tok(P);
            if (tok_is(P, T_NAME)) {
                ex *n = espawn(P->pl); if(!n){perr(P,"pool");return NULL;}
                ex *key = espawn(P->pl); if(!key){perr(P,"pool");return NULL;}
                key->t = N_STR; key->str = P->cur.str;
                n->t = N_INDEX; n->e1 = e; n->e2 = key;
                next_tok(P); e = n;
            } else perr(P, "expected field name");
        } else if (tok_is(P, '(')) {
            next_tok(P);
            ex *call = espawn(P->pl); if(!call){perr(P,"pool");return NULL;}
            call->t = N_CALL; call->e1 = e;
            /* args */
            if (tok_is(P, ')')) { call->nlist = 0; next_tok(P); }
            else {
                ex **args = NULL; int na = 0, capa = 0;
                for (;;) {
                    if (na >= capa) { capa = capa ? capa*2 : 4; args = realloc(args, capa*sizeof(*args)); }
                    args[na++] = parse_expr(P);
                    if (tok_is(P, ',')) next_tok(P);
                    else break;
                }
                expect(P, ')', "expected ')' in call");
                call->list = args; call->nlist = na;
                /* note: args array is heap-realloc'd; transfer ownership to arena-ish by
                   leaking (simplification for subset) */
            }
            e = call;
        } else if (tok_is(P, ':')) {     /* method call sugar: obj:name(...) */
            next_tok(P);
            if (tok_is(P, T_NAME)) {
                const char *mname = P->cur.str; next_tok(P);
                if (tok_is(P, '(')) {
                    next_tok(P);
                    ex *method = espawn(P->pl); if(!method){perr(P,"pool");return NULL;}
                    method->t = N_CALL; method->is_method = 1; method->receiver = e;
                    ex *mkey = espawn(P->pl); if(!mkey){perr(P,"pool");return NULL;}
                    mkey->t = N_STR; mkey->str = mname;
                    /* callee = receiver[key] */
                    ex *base = espawn(P->pl); if(!base){perr(P,"pool");return NULL;}
                    base->t = N_INDEX; base->e1 = e; base->e2 = mkey;
                    method->e1 = base;
                    if (tok_is(P, ')')) { method->nlist = 0; next_tok(P); }
                    else {
                        ex **args=NULL; int na=0,capa=0;
                        for (;;) {
                            if (na>=capa){capa=capa?capa*2:4;args=realloc(args,capa*sizeof(*args));}
                            args[na++]=parse_expr(P);
                            if (tok_is(P, ',')) next_tok(P); else break;
                        }
                        expect(P, ')', "expected ')' in method"); 
                        method->list=args; method->nlist=na;
                    }
                    e = method;
                } else perr(P, "expected '(' after method name");
            } else perr(P, "expected method name");
        } else if (tok_is(P, '{')) {
            /* table constructor: {[k]=v, k=v, v, ...} */
            next_tok(P);
            ex *tab = espawn(P->pl); if(!tab){perr(P,"pool");return NULL;}
            tab->t = N_TABLE;
            ex **vals=NULL,**keys=NULL; int nv=0,cap=0;
            while (!tok_is(P, '}')) {
                /* try [k] = v */
                ex *key = NULL;
                if (tok_is(P, '[')) { next_tok(P); key = parse_expr(P); expect(P,']' ,"expected ']'"); }
                if (tok_is(P, '=')) {
                    next_tok(P);
                    ex *val = parse_expr(P);
                    if (nv>=cap){cap=cap?cap*2:8; vals=realloc(vals,cap*sizeof(*vals)); keys=realloc(keys,cap*sizeof(*keys));}
                    if (!key) { /* bare name -> string key */
                        if (val && val->t == N_NAME) { ; } 
                    }
                    vals[nv]=val; keys[nv]=key; nv++;
                } else if (key) { perr(P,"expected '=' after [k]"); }
                else { /* positional {a,b} -> keys are integer 1,2,... */
                    ex *val = parse_expr(P);
                    if (nv>=cap){cap=cap?cap*2:8; vals=realloc(vals,cap*sizeof(*vals)); keys=realloc(keys,cap*sizeof(*keys));}
                    vals[nv]=val; keys[nv]=NULL; nv++;
                }
                if (tok_is(P, ',')) next_tok(P);
            }
            next_tok(P); /* } */
            tab->list = vals; tab->keys = keys; tab->nlist = nv;
            e = tab;
        } else break;
    }
    return e;
}

static int binprec(int type) {
    switch (type) {
    case T_OR: return 1;
    case T_AND: return 2;
    case '<': case '>': case T_LE: case T_GE: case T_NE: case T_EQ: return 3;
    case T_CONCAT: return 4;
    case '+': case '-': return 5;
    case '*': case '/': case '%': return 6;
    }
    return 0;
}

static ex *parse_unary(parser *P) {
    if (tok_is(P, '-') || tok_is(P, T_NOT)) {
        int op = P->cur.type; next_tok(P);
        ex *operand = parse_unary(P);
        ex *e = espawn(P->pl); if(!e){perr(P,"pool");return NULL;}
        e->t = N_UNARY; e->e1 = operand; e->num = op;
        return e;
    }
    return parse_primary(P);
}

static ex *parse_bin(parser *P, int minprec) {
    ex *lhs = parse_unary(P);
    for (;;) {
        int pr = binprec(P->cur.type);
        if (pr < minprec) break;
        int op = P->cur.type; next_tok(P);
        ex *rhs = parse_bin(P, pr + 1);
        ex *e = espawn(P->pl); if(!e){perr(P,"pool");return NULL;}
        e->t = N_BINOP; e->e1 = lhs; e->e2 = rhs; e->num = op;
        lhs = e;
    }
    return lhs;
}

static ex *parse_expr(parser *P) { return parse_bin(P, 1); }

/* parse a statement */
static ex *parse_stmt(parser *P) {
    tok t = P->cur;
    ex *s = espawn(P->pl); if(!s){perr(P,"pool");return NULL;}

    if (t.type == T_LOCAL) {
        next_tok(P);
        /* local name [, name2 ...] [= expr [, expr2 ...]] */
        ex **names=NULL; int nn=0,capn=0;
        while (tok_is(P, T_NAME)) {
            if (nn>=capn){capn=capn?capn*2:4;names=realloc(names,capn*sizeof(*names));}
            ex *n=espawn(P->pl); if(!n){perr(P,"pool");return NULL;} n->t=N_NAME; n->name=P->cur.str;
            names[nn++]=n; next_tok(P);
            if (tok_is(P, ',')) next_tok(P); else break;
        }
        s->t = N_LOCAL; s->list=names; s->nlist=nn;
        if (tok_is(P, '=')) {
            next_tok(P);
            ex **vals=NULL; int nv=0,capv=0;
            for (;;) {
                if (nv>=capv){capv=capv?capv*2:4;vals=realloc(vals,capv*sizeof(*vals));}
                vals[nv++]=parse_expr(P);
                if (tok_is(P, ',')) next_tok(P); else break;
            }
            s->keys=vals; s->nlist=nn; s->e1=(ex*)(intptr_t)1;
            s->e3=(ex*)vals;             /* reuse e3 as the value list pointer */
            s->num=(double)nv;
        }
        return s;
    }
    if (t.type == T_FUNCTION) {      /* function name() body -- assign to global */
        next_tok(P);
        if (!tok_is(P, T_NAME)) { perr(P,"expected function name"); return s; }
        const char *fname = P->cur.str; next_tok(P);
        /* params */
        ex **params=NULL; int np=0,cap=0;
        expect(P, '(', "expected '('");
        s->t = N_FUNC; s->name = fname;
        while (!tok_is(P, ')')) {
            if (np>=cap){cap=cap?cap*2:4;params=realloc(params,cap*sizeof(*params));}
            if (!tok_is(P, T_NAME)) { perr(P,"expected param name"); break; }
            ex *p=espawn(P->pl); if(!p){perr(P,"pool");return NULL;} p->t=N_NAME; p->name=P->cur.str; next_tok(P);
            params[np++]=p;
            if (tok_is(P, ',')) next_tok(P); else break;
        }
        expect(P, ')', "expected ')'");
        s->list=params; s->nlist=np;
        /* body */
        int matched=0;
        ex *body = parse_block(P, NULL, "end", &matched);
        if (!matched) perr(P, "expected 'end'");
        s->e1 = body;
        /* wrap as assignment to global fname */
        ex *assign = espawn(P->pl); if(!assign){perr(P,"pool");return NULL;}
        assign->t = N_ASSIGN; assign->e1 = (ex*)(intptr_t)&s->num;   /* placeholder; handled below */
        /* We instead set a flag: this stmt is a function-def assigned to global name. */
        s->t = N_FUNC; s->name = fname; s->e1 = body;
        return s;
    }
    if (t.type == T_IF) {
        next_tok(P);
        ex *cond = parse_expr(P);
        expect(P, T_THEN, "expected 'then'");
        int m=0; ex *body = parse_block(P, NULL, "end", &m);
        ex *else_part = NULL;
        /* parse_block consumed the terminator: m==2 means 'else' was hit */
        if (m == 2) { int m2=0; else_part = parse_block(P, NULL, "end", &m2); if(m2!=1) perr(P,"expected 'end' after else"); }
        else if (m != 1) perr(P, "expected 'end'");
        s->t = N_IF; s->e1=cond; s->e2=body; s->e3=else_part;
        return s;
    }
    if (t.type == T_WHILE) {
        next_tok(P);
        ex *cond = parse_expr(P);
        expect(P, T_DO, "expected 'do'");
        int m=0; ex *body = parse_block(P, NULL, "end", &m);
        if (!m) perr(P, "expected 'end'");
        s->t = N_WHILE; s->e1=cond; s->e2=body;
        return s;
    }
    if (t.type == T_FOR) {
        next_tok(P);
        if (!tok_is(P, T_NAME)) { perr(P,"expected loop var"); return s; }
        const char *var = P->cur.str; next_tok(P);
        expect(P, '=', "expected '=' in for");
        ex *start = parse_expr(P);
        expect(P, ',', "expected ',' in for");
        ex *stop = parse_expr(P);
        expect(P, T_DO, "expected 'do'");
        int m=0; ex *body = parse_block(P, NULL, "end", &m);
        if (!m) perr(P, "expected 'end'");
        s->t = N_FOR; s->name=var; s->e1=start; s->e2=stop; s->e3=body;
        return s;
    }
    if (t.type == T_RETURN) {
        next_tok(P);
        s->t = N_RETURN;
        if (!tok_is(P, T_END) && !tok_is(P, T_EOF)) s->e1 = parse_expr(P);
        return s;
    }
    /* assignment or expression statement */
    ex *expr = parse_expr(P);
    if (tok_is(P, '=')) {
        next_tok(P);
        ex *rhs = parse_expr(P);
        s->t = N_ASSIGN;
        if (expr->t == N_NAME) { s->e1 = expr; s->e2 = rhs; s->name = expr->name; }
        else if (expr->t == N_INDEX) { s->e1 = expr; s->e2 = rhs; }
        else { perr(P, "invalid assignment target"); }
        return s;
    }
    /* expression statement (e.g. a call) */
    s->t = N_CALL; s->e1 = expr; s->nlist = 0; s->e2 = NULL;
    return s;
}

static ex *parse_block(parser *P, const char *until, const char *until2, int *matched) {
    (void)until; (void)until2;
    ex *block = espawn(P->pl); if(!block){perr(P,"pool");return NULL;}
    block->t = N_BLOCK;
    ex **stmts=NULL; int ns=0,cap=0;
    *matched = 0;
    for (;;) {
        /* block terminators: end (matched=1), else (matched=2), EOF. We
         * CONSUME end/else so the caller continues past the terminator. */
        if (tok_is(P, T_EOF)) { break; }
        if (tok_is(P, T_END)) { next_tok(P); *matched = 1; break; }
        if (tok_is(P, T_ELSE)) { next_tok(P); *matched = 2; break; }
        ex *st = parse_stmt(P);
        if (ns>=cap){cap=cap?cap*2:8;stmts=realloc(stmts,cap*sizeof(*stmts));}
        stmts[ns++]=st;
        if (P->err) break;
    }
    block->list = stmts; block->nlist = ns;
    return block;
}

/* ----------------------------------------------------------------------
 * evaluator: environment = a table; locals are an env stack.
 * We model scopes as linked frames holding a name->value table (simple and
 * correct for the subset; not as fast as registers -- fine here).
 * ---------------------------------------------------------------------- */
typedef struct env { struct env *parent; wb_lv_table *t; } env;

/* a closure references its defining env; calling sets up a new frame */
struct wb_lv_func {
    ex *body;                  /* N_BLOCK */
    const char *name;
    env *defining;
    int nparams;               /* number of param names */
    const char **params;       /* param names */
};

static void env_set(wb_lv *L, env *e, const char *name, wb_lv_value v) {
    if (!e) return;
    wb_lv_table_set(L, e->t, wb_lv_str(wb_lv_intern(L, name, strlen(name))), v);
}
static wb_lv_value env_get(wb_lv *L, env *e, const char *name) {
    for (env *c = e; c; c = c->parent) {
        wb_lv_value r = wb_lv_table_get(L, c->t, wb_lv_str(wb_lv_intern(L, name, strlen(name))));
        if (r.t != WB_LV_NIL) return r;
    }
    /* fall through to globals */
    return wb_lv_table_get(L, L->globals, wb_lv_str(wb_lv_intern(L, name, strlen(name))));
}
static void env_assign(wb_lv *L, env *e, const char *name, wb_lv_value v) {
    /* find nearest frame with the name; else global */
    for (env *c = e; c; c = c->parent) {
        wb_lv_value r = wb_lv_table_get(L, c->t, wb_lv_str(wb_lv_intern(L, name, strlen(name))));
        if (r.t != WB_LV_NIL) { env_set(L, c, name, v); return; }
    }
    wb_lv_table_set(L, L->globals, wb_lv_str(wb_lv_intern(L, name, strlen(name))), v);
}

static wb_lv_value eval(wb_lv *L, ex *e, env *env);
static wb_lv_value exec(wb_lv *L, ex *e, env *env, int *was_return);   /* fwd */
static wb_lv_value call_lua(wb_lv *L, wb_lv_func *fn, wb_lv_value *args, int nargs); /* fwd */

/* call a lua function value */
static wb_lv_value call_lua(wb_lv *L, wb_lv_func *fn, wb_lv_value *args, int nargs) {
    /* new env: params bound, then body; use exec() so `return` propagates. */
    env *e = XALLOC(env, L);
    e->t = wb_lv_table_new(L, 0, 8);
    e->parent = fn->defining;
    for (int i = 0; i < fn->nparams && i < nargs; i++) env_set(L, e, fn->params[i], args[i]);
    for (int i = nargs; i < fn->nparams; i++) env_set(L, e, fn->params[i], wb_lv_nil());
    wb_lv_value ret = wb_lv_nil();
    if (fn->body && fn->body->t == N_BLOCK) {
        int wr = 0;
        for (int i = 0; i < fn->body->nlist; i++) {
            ret = exec(L, fn->body->list[i], e, &wr);
            if (wr) return ret;                 /* `return expr` fired */
            if (L->has_err) return ret;
        }
    }
    return wb_lv_nil();
}

/* run the value of an expression */
static wb_lv_value eval(wb_lv *L, ex *e, env *env) {
    if (!e) return wb_lv_nil();
    /* eager value stack save not needed since we don't recurse into C with
       host results except calls; we return by value and host cfn gets a
       malloc'd args buffer (leaked per call this milestone). */
    switch (e->t) {
    case N_NUM: return wb_lv_number(e->num);
    case N_STR: return wb_lv_str(e->str);
    case N_BOOL:return wb_lv_bool(e->boolean);
    case N_NIL: return wb_lv_nil();
    case N_NAME: return env_get(L, env, e->name);
    case N_INDEX: {
        wb_lv_value obj = eval(L, e->e1, env);
        wb_lv_value key = eval(L, e->e2, env);
        if (obj.t != WB_LV_TABLE) return wb_lv_nil();
        return wb_lv_table_get(L, obj.as.tab, key);
    }
    case N_UNARY: {
        wb_lv_value v = eval(L, e->e1, env);
        if (e->num == T_NOT) return wb_lv_bool(!wb_lv_truthy(v));
        if (e->num == '-') { double d = wb_lv_tonumber(v, NULL); return wb_lv_number(-d); }
        return wb_lv_nil();
    }
    case N_BINOP: {
        wb_lv_value a = eval(L, e->e1, env), b = eval(L, e->e2, env);
        int op = (int)e->num;
        if (op == T_AND) return wb_lv_truthy(a) ? b : a;
        if (op == T_OR)  return wb_lv_truthy(a) ? a : b;
        double x = a.t==WB_LV_NUM?a.as.n:0, y = b.t==WB_LV_NUM?b.as.n:0;
        switch (op) {
        case '+': return wb_lv_number(x+y);
        case '-': return wb_lv_number(x-y);
        case '*': return wb_lv_number(x*y);
        case '/': return y!=0 ? wb_lv_number(x/y) : wb_lv_nil();
        case '%': return y!=0 ? wb_lv_number(fmod(x,y)) : wb_lv_nil();
        case '<': return wb_lv_bool(x<y);
        case '>': return wb_lv_bool(x>y);
        case T_LE: return wb_lv_bool(x<=y);
        case T_GE: return wb_lv_bool(x>=y);
        case T_EQ: return wb_lv_bool(wb_lv_equal(a,b));
        case T_NE: return wb_lv_bool(!wb_lv_equal(a,b));
        case T_CONCAT: {
            const char *sa = wb_lv_tostring(a), *sb = wb_lv_tostring(b);
            size_t la=strlen(sa), lb=strlen(sb);
            char *buf = malloc(la+lb+1); memcpy(buf,sa,la); memcpy(buf+la,sb,lb); buf[la+lb]=0;
            const char *in = wb_lv_intern(L, buf, la+lb); free(buf);
            return wb_lv_str(in);
        }
        }
        return wb_lv_nil();
    }
    case N_TABLE: {
        wb_lv_table *t = wb_lv_table_new(L, 0, e->nlist);
        for (int i = 0; i < e->nlist; i++) {
            wb_lv_value val = eval(L, e->list[i], env);
            if (e->keys[i]) { wb_lv_value k = eval(L, e->keys[i], env); wb_lv_table_set(L, t, k, val); }
            else { wb_lv_table_set(L, t, wb_lv_number(i+1), val); }
        }
        wb_lv_value r; r.t = WB_LV_TABLE; r.as.tab = t; return r;
    }
    case N_FUNC: {
        wb_lv_func *fn = XALLOC(wb_lv_func, L);
        fn->body = e->e1; fn->name = e->name; fn->defining = env;
        fn->nparams = e->nlist;
        const char **params = XALLOCN(const char*, e->nlist ? e->nlist : 1, L);
        for (int i = 0; i < e->nlist; i++) params[i] = e->list[i] ? e->list[i]->name : NULL;
        fn->params = params;
        wb_lv_value r; r.t = WB_LV_FUNCTION; r.as.fn = fn; return r;
    }
    case N_CALL: {
        if (e->is_method) {
            /* receiver arg prepended: handled by callee = receiver[key],
             * but we also need self. This milestone: evaluate receiver and
             * pass it as first arg. */
            wb_lv_value self = eval(L, e->receiver, env);
            wb_lv_value callee = eval(L, e->e1, env);
            int na = e->nlist + 1;
            wb_lv_value *args = malloc((na ? na : 1)*sizeof(*args));
            args[0] = self;
            for (int i = 0; i < e->nlist; i++) args[1+i] = eval(L, e->list[i], env);
            if (callee.t == WB_LV_FUNCTION) { wb_lv_value r = call_lua(L, callee.as.fn, args, na); free(args); return r; }
            if (callee.t == WB_LV_CFUNCTION) { wb_lv_value r = callee.as.cf.f(L, args, na, callee.as.cf.ud); free(args); return r; }
            free(args); return wb_lv_nil();
        }
        wb_lv_value callee = eval(L, e->e1, env);
        int na = e->nlist;
        wb_lv_value *args = malloc((na?na:1)*sizeof(*args));
        for (int i = 0; i < e->nlist; i++) args[i] = eval(L, e->list[i], env);
        if (callee.t == WB_LV_FUNCTION) { wb_lv_value r = call_lua(L, callee.as.fn, args, na); free(args); return r; }
        if (callee.t == WB_LV_CFUNCTION) { wb_lv_value r = callee.as.cf.f(L, args, na, callee.as.cf.ud); free(args); return r; }
        free(args); return wb_lv_nil();
    }
    case N_LOCAL:
    case N_ASSIGN:
    case N_IF:
    case N_WHILE:
    case N_FOR:
    case N_RETURN:
    case N_BLOCK:
        /* statements handled by exec() */
        return wb_lv_nil();
    }
    return wb_lv_nil();
}

/* exec a statement; returns a "return value" if e is a return (else NIL w/ flag) */
static wb_lv_value exec(wb_lv *L, ex *e, env *env, int *was_return) {
    *was_return = 0;
    if (!e) return wb_lv_nil();
    switch (e->t) {
    case N_LOCAL: {
        /* e->name unused; local decl: list[] have N_NAME nodes */
        int n = e->nlist;
        /* values are in e->e3 (the vals array), count = e->num */
        for (int i = 0; i < n; i++) {
            wb_lv_value v = wb_lv_nil();
            if (e->e3 && i < (int)e->num) v = eval(L, ((ex**)e->e3)[i], env);
            const char *nm = e->list[i] ? e->list[i]->name : NULL;
            if (nm) env_set(L, env, nm, v);
        }
        return wb_lv_nil();
    }
    case N_ASSIGN: {
        if (e->e1 && e->e1->t == N_NAME) {
            wb_lv_value v = eval(L, e->e2, env);
            env_assign(L, env, e->e1->name, v);
        } else if (e->e1 && e->e1->t == N_INDEX) {
            wb_lv_value obj = eval(L, e->e1->e1, env);
            wb_lv_value idx = eval(L, e->e1->e2, env);
            wb_lv_value v = eval(L, e->e2, env);
            if (obj.t == WB_LV_TABLE) wb_lv_table_set(L, obj.as.tab, idx, v);
        }
        return wb_lv_nil();
    }
    case N_CALL:   /* bare call statement */
        eval(L, e, env);
        return wb_lv_nil();
    case N_FUNC: {  /* function name(...) body -- define global + assign */
        wb_lv_value f = eval(L, e, env);
        env_assign(L, env, e->name, f);
        return wb_lv_nil();
    }
    case N_IF: {
        wb_lv_value c = eval(L, e->e1, env);
        if (wb_lv_truthy(c)) return exec(L, e->e2, env, was_return);
        if (e->e3) return exec(L, e->e3, env, was_return);
        return wb_lv_nil();
    }
    case N_WHILE: {
        while (wb_lv_truthy(eval(L, e->e1, env))) {
            wb_lv_value r = exec(L, e->e2, env, was_return);
            if (*was_return) return r;
            if (L->has_err) return wb_lv_nil();
        }
        return wb_lv_nil();
    }
    case N_FOR: {
        double start = wb_lv_tonumber(eval(L, e->e1, env), NULL);
        double stop  = wb_lv_tonumber(eval(L, e->e2, env), NULL);
        for (double i = start; i <= stop; i++) {
            env_set(L, env, e->name, wb_lv_number(i));
            wb_lv_value r = exec(L, e->e3, env, was_return);
            if (*was_return) return r;
            if (L->has_err) return wb_lv_nil();
        }
        return wb_lv_nil();
    }
    case N_RETURN:
        *was_return = 1;
        return e->e1 ? eval(L, e->e1, env) : wb_lv_nil();
    case N_BLOCK: {
        for (int i = 0; i < e->nlist; i++) {
            wb_lv_value r = exec(L, e->list[i], env, was_return);
            if (*was_return) return r;
            if (L->has_err) return r;
        }
        return wb_lv_nil();
    }
    default:
        /* expression as statement (N_* value) */
        return eval(L, e, env);
    }
}

/* ========================================================================
 * driver API
 * ======================================================================== */
static void register_builtins(wb_lv *L);

static const char *parse_driver(wb_lv *L, const char *src, pool *pl, ex **out) {
    parser P; memset(&P, 0, sizeof P);
    P.src = src; P.line = 1; P.L = L; P.pl = pl; P.p = 0;
    next_tok(&P);
    int m = 0;
    ex *root = parse_block(&P, NULL, NULL, &m);
    if (P.err || L->has_err) { snprintf(L->err, sizeof L->err, "%s", L->err[0] ? L->err : "parse error"); return "parse error"; }
    *out = root;
    return NULL;
}

static wb_lv_value exec_root(wb_lv *L, ex *root) {
    env *top = XALLOC(env, L);
    top->t = wb_lv_table_new(L, 0, 8);
    top->parent = NULL;
    int wr = 0;
    (void)exec(L, root, top, &wr);
    if (L->has_err) return wb_lv_nil();
    return wb_lv_nil();
}

static wb_lv *lv_alloc(void) {
    wb_lv *L = calloc(1, sizeof(*L));
    if (!L) return NULL;
    L->nbuckets = 64; L->buckets = arena_alloc(L, L->nbuckets * sizeof(*L->buckets));
    L->globals = wb_lv_table_new(L, 0, 8);
    L->scap = 64; L->stack = arena_alloc(L, L->scap * sizeof(*L->stack));
    return L;
}

wb_lv *wb_lv_new(void) {
    wb_lv *L = lv_alloc();
    if (L) register_builtins(L);
    return L;
}
void wb_lv_free(wb_lv *L) {
    anode *n = L->root, *nx;
    while (n) { nx = n->next; free(n); n = nx; }
    free(L);
}

void wb_lv_register_global(wb_lv *L, const char *name, wb_lv_cfn fn, void *ud) {
    wb_lv_value v; v.t = WB_LV_CFUNCTION; v.as.cf.f = fn; v.as.cf.ud = ud;
    wb_lv_table_set(L, L->globals, wb_lv_str(wb_lv_intern(L, name, strlen(name))), v);
}
wb_lv_value wb_lv_get_global(wb_lv *L, const char *name) {
    return wb_lv_table_get(L, L->globals, wb_lv_str(wb_lv_intern(L, name, strlen(name))));
}
void wb_lv_set_global(wb_lv *L, const char *name, wb_lv_value v) {
    wb_lv_table_set(L, L->globals, wb_lv_str(wb_lv_intern(L, name, strlen(name))), v);
}

const char *wb_lv_run(wb_lv *L, const char *src, const char *chunkname) {
    (void)chunkname;
    L->has_err = 0; L->err[0] = 0;
    pool pl; memset(&pl, 0, sizeof pl);
    ex *root = NULL;
    if (parse_driver(L, src, &pl, &root)) {
        /* stack pool is on the C stack (pl is local); the AST pointers we
           stored into closures would dangle, but for a run() that executes
           immediately in one shot it is fine (documented limitation). */
        return L->err[0] ? L->err : "parse error";
    }
    exec_root(L, root);
    return L->has_err ? L->err : NULL;
}

wb_lv_value wb_lv_call(wb_lv *L, wb_lv_value fn, wb_lv_value *args, int nargs) {
    if (fn.t == WB_LV_FUNCTION) return call_lua(L, fn.as.fn, args, nargs);
    if (fn.t == WB_LV_CFUNCTION) return fn.as.cf.f(L, args, nargs, fn.as.cf.ud);
    return wb_lv_nil();
}

/* ---- builtin host functions (a tiny stdio/tonumber-ish floor) ---- */
static wb_lv_value cfn_print(wb_lv *L, wb_lv_value *args, int n, void *ud) {
    (void)L; (void)ud;
    printf("%s\n", n>0 ? wb_lv_tostring(args[0]) : "");
    return wb_lv_nil();
}
static wb_lv_value cfn_tonumber(wb_lv *L, wb_lv_value *args, int n, void *ud) {
    (void)L; (void)ud;
    if (n>0 && args[0].t == WB_LV_NUM) return wb_lv_number(args[0].as.n);
    return wb_lv_number(0);
}
static wb_lv_value cfn_type(wb_lv *L, wb_lv_value *args, int n, void *ud) {
    (void)L; (void)ud;
    const char *s = "nil";
    if (n>0) {
        switch (args[0].t) {
        case WB_LV_NIL: s="nil"; break;
        case WB_LV_BOOL: s="boolean"; break;
        case WB_LV_NUM: s="number"; break;
        case WB_LV_STR: s="string"; break;
        case WB_LV_TABLE: s="table"; break;
        case WB_LV_FUNCTION: case WB_LV_CFUNCTION: s="function"; break;
        }
    }
    return wb_lv_str(wb_lv_intern(L, s, strlen(s)));
}
static void register_builtins(wb_lv *L) {
    wb_lv_register_global(L, "print", cfn_print, NULL);
    wb_lv_register_global(L, "tonumber", cfn_tonumber, NULL);
    wb_lv_register_global(L, "type", cfn_type, NULL);
}
