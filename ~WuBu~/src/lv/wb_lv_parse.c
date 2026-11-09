/*
 * wb_lv_parse.c -- the grammar: expressions, statements and blocks, plus the
 * AST pool. See wb_lv_internal.h.
 */
#include "wb_lv_internal.h"

#include <stdlib.h>
#include <string.h>

static ex *espawn(pool *p) {
    if (p->cnt >= WB_LV_MAX_EX) return NULL;
    memset(&p->n[p->cnt], 0, sizeof(ex));
    return &p->n[p->cnt++];
}

static int tok_is(parser *P, int type) { return P->cur.type == type; }

static void expect(parser *P, int type, const char *what) {
    if (!tok_is(P, type)) wb_lv_perr(P, what);
    wb_lv_next_tok(P);
}

static ex *parse_expr(parser *P);
static ex *parse_stmt(parser *P);
static ex *parse_block(parser *P, const char *until, const char *until2, int *matched);
static ex *parse_bin(parser *P, int minprec);

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

static ex *parse_primary(parser *P) {
    ex *e = NULL;
    tok t = P->cur;
    switch (t.type) {
    case T_NUM:  { e = espawn(P->pl); if(!e){wb_lv_perr(P,"pool");return NULL;} e->t=N_NUM; e->num=t.num; wb_lv_next_tok(P); break; }
    case T_STR:  { e = espawn(P->pl); if(!e){wb_lv_perr(P,"pool");return NULL;} e->t=N_STR; e->str=t.str; wb_lv_next_tok(P); break; }
    case T_TRUE: { e = espawn(P->pl); if(!e){wb_lv_perr(P,"pool");return NULL;} e->t=N_BOOL; e->boolean=1; wb_lv_next_tok(P); break; }
    case T_FALSE:{ e = espawn(P->pl); if(!e){wb_lv_perr(P,"pool");return NULL;} e->t=N_BOOL; e->boolean=0; wb_lv_next_tok(P); break; }
    case T_NIL:  { e = espawn(P->pl); if(!e){wb_lv_perr(P,"pool");return NULL;} e->t=N_NIL; wb_lv_next_tok(P); break; }
    case T_NAME: { e = espawn(P->pl); if(!e){wb_lv_perr(P,"pool");return NULL;} e->t=N_NAME; e->name=t.str; wb_lv_next_tok(P); break; }
    case '{': {   /* table literal is a PRIMARY (t = {...}) */
        wb_lv_next_tok(P);
        ex *tab = espawn(P->pl); if(!tab){wb_lv_perr(P,"pool");return NULL;}
        tab->t = N_TABLE;
        ex **vals=NULL,**keys=NULL; int nv=0,cap=0;
        while (!tok_is(P, '}')) {
            ex *key = NULL;
            /* bare-name field: name = value  (key becomes the interned name) */
            if (tok_is(P, T_NAME)) {
                const char *nm = P->cur.str;
                wb_lv_next_tok(P);
                if (tok_is(P, '=')) {
                    key = espawn(P->pl); if(!key){wb_lv_perr(P,"pool");return NULL;}
                    key->t = N_STR; key->str = nm;
                    wb_lv_next_tok(P);          /* consume '=' */
                    ex *val = parse_expr(P);
                    if (nv>=cap){cap=cap?cap*2:8; vals=realloc(vals,cap*sizeof(*vals)); keys=realloc(keys,cap*sizeof(*keys));}
                    vals[nv]=val; keys[nv]=key; nv++;
                    if (tok_is(P, ',')) wb_lv_next_tok(P);
                    continue;
                }
                /* not a name= -- it was a bare expression; fall through with
                 * the name re-tokenized: we already consumed it, so stash a
                 * NAME node as the value and continue. */
                ex *nmval = espawn(P->pl); if(!nmval){wb_lv_perr(P,"pool");return NULL;}
                nmval->t = N_NAME; nmval->name = nm;
                if (nv>=cap){cap=cap?cap*2:8; vals=realloc(vals,cap*sizeof(*vals)); keys=realloc(keys,cap*sizeof(*keys));}
                vals[nv]=nmval; keys[nv]=NULL; nv++;
                if (tok_is(P, ',')) wb_lv_next_tok(P);
                continue;
            }
            if (tok_is(P, '[')) { wb_lv_next_tok(P); key = parse_expr(P); expect(P,']',"expected ']'"); }
            if (tok_is(P, '=')) {
                wb_lv_next_tok(P);
                ex *val = parse_expr(P);
                if (nv>=cap){cap=cap?cap*2:8; vals=realloc(vals,cap*sizeof(*vals)); keys=realloc(keys,cap*sizeof(*keys));}
                vals[nv]=val; keys[nv]=key; nv++;
            } else {
                ex *val = parse_expr(P);
                if (nv>=cap){cap=cap?cap*2:8; vals=realloc(vals,cap*sizeof(*vals)); keys=realloc(keys,cap*sizeof(*keys));}
                vals[nv]=val; keys[nv]=key; nv++;
            }
            if (tok_is(P, ',')) wb_lv_next_tok(P);
        }
        wb_lv_next_tok(P); /* } */
        tab->list = vals; tab->keys = keys; tab->nlist = nv;
        e = tab;
        break;
    }
    case '(': {
        wb_lv_next_tok(P);
        e = parse_expr(P);
        expect(P, ')', "expected ')'");
        break;
    }
    default:
        wb_lv_perr(P, "unexpected token"); wb_lv_next_tok(P); break;
    }
    /* postfix: index and call and method */
    for (;;) {
        if (tok_is(P, '[')) {
            wb_lv_next_tok(P);
            ex *idx = parse_expr(P);
            expect(P, ']', "expected ']'");
            ex *n = espawn(P->pl); if(!n){wb_lv_perr(P,"pool");return NULL;}
            n->t = N_INDEX; n->e1 = e; n->e2 = idx; e = n;
        } else if (tok_is(P, '.')) {
            wb_lv_next_tok(P);
            if (tok_is(P, T_NAME)) {
                ex *n = espawn(P->pl); if(!n){wb_lv_perr(P,"pool");return NULL;}
                ex *key = espawn(P->pl); if(!key){wb_lv_perr(P,"pool");return NULL;}
                key->t = N_STR; key->str = P->cur.str;
                n->t = N_INDEX; n->e1 = e; n->e2 = key;
                wb_lv_next_tok(P); e = n;
            } else wb_lv_perr(P, "expected field name");
        } else if (tok_is(P, '(')) {
            wb_lv_next_tok(P);
            ex *call = espawn(P->pl); if(!call){wb_lv_perr(P,"pool");return NULL;}
            call->t = N_CALL; call->e1 = e;
            /* args */
            if (tok_is(P, ')')) { call->nlist = 0; wb_lv_next_tok(P); }
            else {
                ex **args = NULL; int na = 0, capa = 0;
                for (;;) {
                    if (na >= capa) { capa = capa ? capa*2 : 4; args = realloc(args, capa*sizeof(*args)); }
                    args[na++] = parse_expr(P);
                    if (tok_is(P, ',')) wb_lv_next_tok(P);
                    else break;
                }
                expect(P, ')', "expected ')' in call");
                call->list = args; call->nlist = na;
                /* args array is heap-realloc'd; leaked per call this milestone */
            }
            e = call;
        } else if (tok_is(P, ':')) {     /* method call sugar: obj:name(...) */
            wb_lv_next_tok(P);
            if (tok_is(P, T_NAME)) {
                const char *mname = P->cur.str; wb_lv_next_tok(P);
                if (tok_is(P, '(')) {
                    wb_lv_next_tok(P);
                    ex *method = espawn(P->pl); if(!method){wb_lv_perr(P,"pool");return NULL;}
                    method->t = N_CALL; method->is_method = 1; method->receiver = e;
                    ex *mkey = espawn(P->pl); if(!mkey){wb_lv_perr(P,"pool");return NULL;}
                    mkey->t = N_STR; mkey->str = mname;
                    /* callee = receiver[key] */
                    ex *base = espawn(P->pl); if(!base){wb_lv_perr(P,"pool");return NULL;}
                    base->t = N_INDEX; base->e1 = e; base->e2 = mkey;
                    method->e1 = base;
                    if (tok_is(P, ')')) { method->nlist = 0; wb_lv_next_tok(P); }
                    else {
                        ex **args=NULL; int na=0,capa=0;
                        for (;;) {
                            if (na>=capa){capa=capa?capa*2:4;args=realloc(args,capa*sizeof(*args));}
                            args[na++]=parse_expr(P);
                            if (tok_is(P, ',')) wb_lv_next_tok(P); else break;
                        }
                        expect(P, ')', "expected ')' in method");
                        method->list=args; method->nlist=na;
                    }
                    e = method;
                } else wb_lv_perr(P, "expected '(' after method name");
            } else wb_lv_perr(P, "expected method name");
        } else if (tok_is(P, '{')) {
            /* table constructor: {[k]=v, k=v, v, ...} */
            wb_lv_next_tok(P);
            ex *tab = espawn(P->pl); if(!tab){wb_lv_perr(P,"pool");return NULL;}
            tab->t = N_TABLE;
            ex **vals=NULL,**keys=NULL; int nv=0,cap=0;
            while (!tok_is(P, '}')) {
                /* try [k] = v */
                ex *key = NULL;
                if (tok_is(P, '[')) { wb_lv_next_tok(P); key = parse_expr(P); expect(P,']',"expected ']'"); }
                if (tok_is(P, '=')) {
                    wb_lv_next_tok(P);
                    ex *val = parse_expr(P);
                    if (nv>=cap){cap=cap?cap*2:8; vals=realloc(vals,cap*sizeof(*vals)); keys=realloc(keys,cap*sizeof(*keys));}
                    vals[nv]=val; keys[nv]=key; nv++;
                } else if (key) { wb_lv_perr(P,"expected '=' after [k]"); }
                else { /* positional {a,b} -> keys are integer 1,2,... */
                    ex *val = parse_expr(P);
                    if (nv>=cap){cap=cap?cap*2:8; vals=realloc(vals,cap*sizeof(*vals)); keys=realloc(keys,cap*sizeof(*keys));}
                    vals[nv]=val; keys[nv]=NULL; nv++;
                }
                if (tok_is(P, ',')) wb_lv_next_tok(P);
            }
            wb_lv_next_tok(P); /* } */
            tab->list = vals; tab->keys = keys; tab->nlist = nv;
            e = tab;
        } else break;
    }
    return e;
}

static ex *parse_unary(parser *P) {
    if (tok_is(P, '-') || tok_is(P, T_NOT)) {
        int op = P->cur.type; wb_lv_next_tok(P);
        ex *operand = parse_unary(P);
        ex *e = espawn(P->pl); if(!e){wb_lv_perr(P,"pool");return NULL;}
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
        int op = P->cur.type; wb_lv_next_tok(P);
        ex *rhs = parse_bin(P, pr + 1);
        ex *e = espawn(P->pl); if(!e){wb_lv_perr(P,"pool");return NULL;}
        e->t = N_BINOP; e->e1 = lhs; e->e2 = rhs; e->num = op;
        lhs = e;
    }
    return lhs;
}

static ex *parse_expr(parser *P) { return parse_bin(P, 1); }

/* parse a statement */
static ex *parse_stmt(parser *P) {
    tok t = P->cur;
    ex *s = espawn(P->pl); if(!s){wb_lv_perr(P,"pool");return NULL;}

    if (t.type == T_LOCAL) {
        wb_lv_next_tok(P);
        /* local name [, name2 ...] [= expr [, expr2 ...]] */
        ex **names=NULL; int nn=0,capn=0;
        while (tok_is(P, T_NAME)) {
            if (nn>=capn){capn=capn?capn*2:4;names=realloc(names,capn*sizeof(*names));}
            ex *n=espawn(P->pl); if(!n){wb_lv_perr(P,"pool");return NULL;} n->t=N_NAME; n->name=P->cur.str;
            names[nn++]=n; wb_lv_next_tok(P);
            if (tok_is(P, ',')) wb_lv_next_tok(P); else break;
        }
        s->t = N_LOCAL; s->list=names; s->nlist=nn;
        if (tok_is(P, '=')) {
            wb_lv_next_tok(P);
            ex **vals=NULL; int nv=0,capv=0;
            for (;;) {
                if (nv>=capv){capv=capv?capv*2:4;vals=realloc(vals,capv*sizeof(*vals));}
                vals[nv++]=parse_expr(P);
                if (tok_is(P, ',')) wb_lv_next_tok(P); else break;
            }
            s->keys=vals; s->nlist=nn; s->e1=(ex*)(intptr_t)1;
            s->e3=(ex*)vals;             /* reuse e3 as the value list pointer */
            s->num=(double)nv;
        }
        return s;
    }
    if (t.type == T_FUNCTION) {      /* function name() body -- assign to global */
        wb_lv_next_tok(P);
        if (!tok_is(P, T_NAME)) { wb_lv_perr(P,"expected function name"); return s; }
        const char *fname = P->cur.str; wb_lv_next_tok(P);
        /* params */
        ex **params=NULL; int np=0,cap=0;
        expect(P, '(', "expected '('");
        s->t = N_FUNC; s->name = fname;
        while (!tok_is(P, ')')) {
            if (np>=cap){cap=cap?cap*2:4;params=realloc(params,cap*sizeof(*params));}
            if (!tok_is(P, T_NAME)) { wb_lv_perr(P,"expected param name"); break; }
            ex *p=espawn(P->pl); if(!p){wb_lv_perr(P,"pool");return NULL;} p->t=N_NAME; p->name=P->cur.str; wb_lv_next_tok(P);
            params[np++]=p;
            if (tok_is(P, ',')) wb_lv_next_tok(P); else break;
        }
        expect(P, ')', "expected ')'");
        s->list=params; s->nlist=np;
        /* body */
        int matched=0;
        ex *body = parse_block(P, NULL, "end", &matched);
        if (!matched) wb_lv_perr(P, "expected 'end'");
        s->e1 = body;
        /* this stmt is a function-def assigned to the global `fname` */
        return s;
    }
    if (t.type == T_IF) {
        wb_lv_next_tok(P);
        ex *cond = parse_expr(P);
        expect(P, T_THEN, "expected 'then'");
        int m=0; ex *body = parse_block(P, NULL, "end", &m);
        ex *else_part = NULL;
        /* parse_block consumed the terminator: m==2 means 'else' was hit */
        if (m == 2) { int m2=0; else_part = parse_block(P, NULL, "end", &m2); if(m2!=1) wb_lv_perr(P,"expected 'end' after else"); }
        else if (m != 1) wb_lv_perr(P, "expected 'end'");
        s->t = N_IF; s->e1=cond; s->e2=body; s->e3=else_part;
        return s;
    }
    if (t.type == T_WHILE) {
        wb_lv_next_tok(P);
        ex *cond = parse_expr(P);
        expect(P, T_DO, "expected 'do'");
        int m=0; ex *body = parse_block(P, NULL, "end", &m);
        if (!m) wb_lv_perr(P, "expected 'end'");
        s->t = N_WHILE; s->e1=cond; s->e2=body;
        return s;
    }
    if (t.type == T_FOR) {
        wb_lv_next_tok(P);
        if (!tok_is(P, T_NAME)) { wb_lv_perr(P,"expected loop var"); return s; }
        const char *var = P->cur.str; wb_lv_next_tok(P);
        expect(P, '=', "expected '=' in for");
        ex *start = parse_expr(P);
        expect(P, ',', "expected ',' in for");
        ex *stop = parse_expr(P);
        expect(P, T_DO, "expected 'do'");
        int m=0; ex *body = parse_block(P, NULL, "end", &m);
        if (!m) wb_lv_perr(P, "expected 'end'");
        s->t = N_FOR; s->name=var; s->e1=start; s->e2=stop; s->e3=body;
        return s;
    }
    if (t.type == T_RETURN) {
        wb_lv_next_tok(P);
        s->t = N_RETURN;
        if (!tok_is(P, T_END) && !tok_is(P, T_EOF)) s->e1 = parse_expr(P);
        return s;
    }
    /* assignment or expression statement */
    ex *expr = parse_expr(P);
    if (tok_is(P, '=')) {
        wb_lv_next_tok(P);
        ex *rhs = parse_expr(P);
        s->t = N_ASSIGN;
        if (expr->t == N_NAME) { s->e1 = expr; s->e2 = rhs; s->name = expr->name; }
        else if (expr->t == N_INDEX) { s->e1 = expr; s->e2 = rhs; }
        else { wb_lv_perr(P, "invalid assignment target"); }
        return s;
    }
    /* expression statement (e.g. a call) */
    s->t = N_CALL; s->e1 = expr; s->nlist = 0; s->e2 = NULL;
    return s;
}

static ex *parse_block(parser *P, const char *until, const char *until2, int *matched) {
    (void)until; (void)until2;
    ex *block = espawn(P->pl); if(!block){wb_lv_perr(P,"pool");return NULL;}
    block->t = N_BLOCK;
    ex **stmts=NULL; int ns=0,cap=0;
    *matched = 0;
    for (;;) {
        /* block terminators: end (matched=1), else (matched=2), EOF. We
         * CONSUME end/else so the caller continues past the terminator. */
        if (tok_is(P, T_EOF)) { break; }
        if (tok_is(P, T_END)) { wb_lv_next_tok(P); *matched = 1; break; }
        if (tok_is(P, T_ELSE)) { wb_lv_next_tok(P); *matched = 2; break; }
        ex *st = parse_stmt(P);
        if (ns>=cap){cap=cap?cap*2:8;stmts=realloc(stmts,cap*sizeof(*stmts));}
        stmts[ns++]=st;
        if (P->err) break;
    }
    block->list = stmts; block->nlist = ns;
    return block;
}

const char *wb_lv_parse_driver(wb_lv *L, const char *src, pool *pl, ex **out) {
    parser P;
    memset(&P, 0, sizeof P);
    P.src = src; P.line = 1; P.L = L; P.pl = pl; P.p = 0;
    wb_lv_next_tok(&P);
    int m = 0;
    ex *root = parse_block(&P, NULL, NULL, &m);
    if (P.err || L->has_err) {
        snprintf(L->err, sizeof L->err, "%s", L->err[0] ? L->err : "parse error");
        return "parse error";
    }
    *out = root;
    return NULL;
}
