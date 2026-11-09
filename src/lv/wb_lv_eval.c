/*
 * wb_lv_eval.c -- the tree-walking evaluator: scopes, calls and statements.
 * See wb_lv_internal.h.
 */
#include "wb_lv_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Scopes are linked frames holding a name->value table. Simple and correct
 * for the subset; not as fast as registers -- fine at this milestone. */
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

static wb_lv_value eval(wb_lv *L, ex *e, env *en);
static wb_lv_value exec(wb_lv *L, ex *e, env *en, int *was_return);

/* call a lua function value */
wb_lv_value wb_lv_call_lua(wb_lv *L, wb_lv_func *fn, wb_lv_value *args, int nargs) {
    /* new env: params bound, then body; use exec() so `return` propagates. */
    env *e = WB_LV_ALLOC(env, L);
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
static wb_lv_value eval(wb_lv *L, ex *e, env *en) {
    if (!e) return wb_lv_nil();
    switch (e->t) {
    case N_NUM: return wb_lv_number(e->num);
    case N_STR: return wb_lv_str(e->str);
    case N_BOOL:return wb_lv_bool(e->boolean);
    case N_NIL: return wb_lv_nil();
    case N_NAME: return env_get(L, en, e->name);
    case N_INDEX: {
        wb_lv_value obj = eval(L, e->e1, en);
        wb_lv_value key = eval(L, e->e2, en);
        if (obj.t != WB_LV_TABLE) return wb_lv_nil();
        return wb_lv_table_get(L, obj.as.tab, key);
    }
    case N_UNARY: {
        wb_lv_value v = eval(L, e->e1, en);
        if (e->num == T_NOT) return wb_lv_bool(!wb_lv_truthy(v));
        if (e->num == '-') { double d = wb_lv_tonumber(v, NULL); return wb_lv_number(-d); }
        return wb_lv_nil();
    }
    case N_BINOP: {
        wb_lv_value a = eval(L, e->e1, en), b = eval(L, e->e2, en);
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
            char *buf = (char *)malloc(la+lb+1);
            memcpy(buf,sa,la); memcpy(buf+la,sb,lb); buf[la+lb]=0;
            const char *in = wb_lv_intern(L, buf, la+lb);
            free(buf);
            return wb_lv_str(in);
        }
        }
        return wb_lv_nil();
    }
    case N_TABLE: {
        wb_lv_table *t = wb_lv_table_new(L, 0, e->nlist);
        for (int i = 0; i < e->nlist; i++) {
            wb_lv_value val = eval(L, e->list[i], en);
            if (e->keys[i]) { wb_lv_value k = eval(L, e->keys[i], en); wb_lv_table_set(L, t, k, val); }
            else { wb_lv_table_set(L, t, wb_lv_number(i+1), val); }
        }
        wb_lv_value r; r.t = WB_LV_TABLE; r.as.tab = t; return r;
    }
    case N_FUNC: {
        wb_lv_func *fn = WB_LV_ALLOC(wb_lv_func, L);
        fn->body = e->e1; fn->name = e->name; fn->defining = en;
        fn->nparams = e->nlist;
        const char **params = WB_LV_ALLOCN(const char*, e->nlist ? e->nlist : 1, L);
        for (int i = 0; i < e->nlist; i++) params[i] = e->list[i] ? e->list[i]->name : NULL;
        fn->params = params;
        wb_lv_value r; r.t = WB_LV_FUNCTION; r.as.fn = fn; return r;
    }
    case N_CALL: {
        if (e->is_method) {
            /* receiver passed as first arg; callee is receiver[key] */
            wb_lv_value self = eval(L, e->receiver, en);
            wb_lv_value callee = eval(L, e->e1, en);
            int na = e->nlist + 1;
            wb_lv_value *args = (wb_lv_value *)malloc((na ? na : 1)*sizeof(*args));
            args[0] = self;
            for (int i = 0; i < e->nlist; i++) args[1+i] = eval(L, e->list[i], en);
            if (callee.t == WB_LV_FUNCTION) { wb_lv_value r = wb_lv_call_lua(L, callee.as.fn, args, na); free(args); return r; }
            if (callee.t == WB_LV_CFUNCTION) { wb_lv_value r = callee.as.cf.f(L, args, na, callee.as.cf.ud); free(args); return r; }
            free(args); return wb_lv_nil();
        }
        wb_lv_value callee = eval(L, e->e1, en);
        int na = e->nlist;
        wb_lv_value *args = (wb_lv_value *)malloc((na?na:1)*sizeof(*args));
        for (int i = 0; i < e->nlist; i++) args[i] = eval(L, e->list[i], en);
        if (callee.t == WB_LV_FUNCTION) { wb_lv_value r = wb_lv_call_lua(L, callee.as.fn, args, na); free(args); return r; }
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
static wb_lv_value exec(wb_lv *L, ex *e, env *en, int *was_return) {
    *was_return = 0;
    if (!e) return wb_lv_nil();
    switch (e->t) {
    case N_LOCAL: {
        /* values are in e->e3 (the vals array), count = e->num */
        int n = e->nlist;
        for (int i = 0; i < n; i++) {
            wb_lv_value v = wb_lv_nil();
            if (e->e3 && i < (int)e->num) v = eval(L, ((ex**)e->e3)[i], en);
            const char *nm = e->list[i] ? e->list[i]->name : NULL;
            if (nm) env_set(L, en, nm, v);
        }
        return wb_lv_nil();
    }
    case N_ASSIGN: {
        if (e->e1 && e->e1->t == N_NAME) {
            wb_lv_value v = eval(L, e->e2, en);
            env_assign(L, en, e->e1->name, v);
        } else if (e->e1 && e->e1->t == N_INDEX) {
            wb_lv_value obj = eval(L, e->e1->e1, en);
            wb_lv_value idx = eval(L, e->e1->e2, en);
            wb_lv_value v = eval(L, e->e2, en);
            if (obj.t == WB_LV_TABLE) wb_lv_table_set(L, obj.as.tab, idx, v);
        }
        return wb_lv_nil();
    }
    case N_CALL:   /* bare call statement */
        eval(L, e, en);
        return wb_lv_nil();
    case N_FUNC: {  /* function name(...) body -- define global + assign */
        wb_lv_value f = eval(L, e, en);
        env_assign(L, en, e->name, f);
        return wb_lv_nil();
    }
    case N_IF: {
        wb_lv_value c = eval(L, e->e1, en);
        if (wb_lv_truthy(c)) return exec(L, e->e2, en, was_return);
        if (e->e3) return exec(L, e->e3, en, was_return);
        return wb_lv_nil();
    }
    case N_WHILE: {
        while (wb_lv_truthy(eval(L, e->e1, en))) {
            wb_lv_value r = exec(L, e->e2, en, was_return);
            if (*was_return) return r;
            if (L->has_err) return wb_lv_nil();
        }
        return wb_lv_nil();
    }
    case N_FOR: {
        double start = wb_lv_tonumber(eval(L, e->e1, en), NULL);
        double stop  = wb_lv_tonumber(eval(L, e->e2, en), NULL);
        for (double i = start; i <= stop; i++) {
            env_set(L, en, e->name, wb_lv_number(i));
            wb_lv_value r = exec(L, e->e3, en, was_return);
            if (*was_return) return r;
            if (L->has_err) return wb_lv_nil();
        }
        return wb_lv_nil();
    }
    case N_RETURN:
        *was_return = 1;
        return e->e1 ? eval(L, e->e1, en) : wb_lv_nil();
    case N_BLOCK: {
        for (int i = 0; i < e->nlist; i++) {
            wb_lv_value r = exec(L, e->list[i], en, was_return);
            if (*was_return) return r;
            if (L->has_err) return r;
        }
        return wb_lv_nil();
    }
    default:
        /* expression as statement (N_* value) */
        return eval(L, e, en);
    }
}

wb_lv_value wb_lv_exec_root(wb_lv *L, ex *root) {
    env *top = WB_LV_ALLOC(env, L);
    top->t = wb_lv_table_new(L, 0, 8);
    top->parent = NULL;
    int wr = 0;
    (void)exec(L, root, top, &wr);
    if (L->has_err) return wb_lv_nil();
    return wb_lv_nil();
}
