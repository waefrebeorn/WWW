/*
 * wb_lv_host.c -- the interpreter's public driver: state lifecycle, global
 * registration, run/call entry points and the builtin host functions.
 * See wb_lv.h.
 */
#include "wb_lv_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void wb_lv_set_err(wb_lv *L, const char *fmt, ...) {
    if (!L) return;
    /* First error wins. A guard (nesting limit) returning nil makes every
     * enclosing frame see a nil callee; without this, the cascading
     * "attempt to call a nil value" would overwrite the real cause. */
    if (L->has_err) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(L->err, sizeof L->err, fmt, ap);
    va_end(ap);
    L->has_err = 1;
}

wb_lv *wb_lv_alloc(void) {
    wb_lv *L = (wb_lv *)calloc(1, sizeof(*L));
    if (!L) return NULL;
    L->nbuckets = 64;
    L->buckets = (istr **)wb_lv_arena_alloc(L, L->nbuckets * sizeof(*L->buckets));
    L->globals = wb_lv_table_new(L, 0, 8);
    L->scap = 64;
    L->stack = (wb_lv_value *)wb_lv_arena_alloc(L, L->scap * sizeof(*L->stack));
    return L;
}

wb_lv *wb_lv_new(void) {
    wb_lv *L = wb_lv_alloc();
    if (L) wb_lv_register_builtins(L);
    return L;
}

void wb_lv_free(wb_lv *L) {
    if (!L) return;
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

    /* The AST pool is arena-backed, NOT on the C stack. wb_lv_call() may be
     * used on a function defined by an earlier wb_lv_run() (that is the
     * engine-loop contract), so closures must keep their body/params alive
     * after run() returns. One pool per state, allocated lazily and never
     * reset: nodes only accumulate for the life of the state. */
    if (!L->pool) {
        L->pool = WB_LV_ALLOC(pool, L);
        if (!L->pool) {
            wb_lv_set_err(L, "out of memory");
            return L->err;
        }
    }

    ex *root = NULL;
    if (wb_lv_parse_driver(L, src, L->pool, &root))
        return L->err[0] ? L->err : "parse error";
    wb_lv_exec_root(L, root);
    return L->has_err ? L->err : NULL;
}

wb_lv_value wb_lv_call(wb_lv *L, wb_lv_value fn, wb_lv_value *args, int nargs) {
    if (fn.t == WB_LV_FUNCTION) return wb_lv_call_lua(L, fn.as.fn, args, nargs);
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
    (void)ud;
    return wb_lv_str(wb_lv_intern(L,
                                  wb_lv_type_name(n > 0 ? args[0].t : WB_LV_NIL),
                                  strlen(wb_lv_type_name(n > 0 ? args[0].t : WB_LV_NIL))));
}

void wb_lv_register_builtins(wb_lv *L) {
    wb_lv_register_global(L, "print", cfn_print, NULL);
    wb_lv_register_global(L, "tonumber", cfn_tonumber, NULL);
    wb_lv_register_global(L, "type", cfn_type, NULL);
}
