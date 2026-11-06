/*
 * wb_lv.h -- WuBu LV: slerm-Lua (slice 8 foundation, INTERPRETER stage).
 *
 * WHY: stock Lua is the placeholder to get the engine running. slerm-Lua
 * exists for C11 SPEED + control -- it is the substrate that lets people
 * write the best mods and lets other games be slermed INTO our world space,
 * a distribution surface the world boots in WuBuOS.
 *
 * THIS milestone is the semantic foundation: a clean tree-walking subset
 * interpreter that meets the wb_script contract (bind world, call wb.*, run
 * step). Correctness + portability are the goals here -- NOT yet speed. The
 * register bytecode VM that actually beats stock Lua is the NEXT milestone;
 * that benchmark claim is made only when it exists.
 *
 * Subset: nil/bool/number/string(interned)/table/function; local + assign;
 * if/else; while; numeric for; table literals; index t[k]/t.k; method t:f();
 * calls (lua + host cfn); return. No coroutines/metamethods/varargs.
 */
#ifndef WB_LV_H
#define WB_LV_H

#include <stdint.h>
#include <stddef.h>

typedef struct wb_lv       wb_lv;
typedef struct wb_lv_value wb_lv_value;
typedef struct wb_lv_func  wb_lv_func;
typedef struct wb_lv_table wb_lv_table;

typedef wb_lv_value (*wb_lv_cfn)(wb_lv *L, wb_lv_value *args, int nargs, void *ud);

/* --------------------------------------------------------------- values */
typedef enum {
    WB_LV_NIL = 0, WB_LV_BOOL, WB_LV_NUM, WB_LV_STR, WB_LV_TABLE,
    WB_LV_FUNCTION, WB_LV_CFUNCTION,
} wb_lv_type;

struct wb_lv_value {
    wb_lv_type t;
    union {
        int         b;
        double      n;
        const char *s;
        wb_lv_table *tab;
        wb_lv_func  *fn;
        struct { wb_lv_cfn f; void *ud; } cf;
    } as;
};

#define wb_lv_nil()     ((wb_lv_value){.t=WB_LV_NIL})
#define wb_lv_bool(B)   ((wb_lv_value){.t=WB_LV_BOOL, .as.b=(B)})
#define wb_lv_number(N) ((wb_lv_value){.t=WB_LV_NUM,  .as.n=(N)})
#define wb_lv_str(S)    ((wb_lv_value){.t=WB_LV_STR,  .as.s=(S)})

int wb_lv_truthy(wb_lv_value v);
int wb_lv_equal(wb_lv_value a, wb_lv_value b);

/* --------------------------------------------------------------- state */
wb_lv *wb_lv_new(void);
void   wb_lv_free(wb_lv *L);

const char *wb_lv_intern(wb_lv *L, const char *bytes, size_t len);
double      wb_lv_tonumber(wb_lv_value v, int *ok);
const char *wb_lv_tostring(wb_lv_value v);

/* --------------------------------------------------------------- tables */
wb_lv_table *wb_lv_table_new(wb_lv *L, int narr, int nrec);
void         wb_lv_table_set(wb_lv *L, wb_lv_table *t, wb_lv_value k, wb_lv_value v);
wb_lv_value  wb_lv_table_get(wb_lv *L, const wb_lv_table *t, wb_lv_value k);

/* --------------------------------------------------------------- driver */
const char *wb_lv_run(wb_lv *L, const char *src, const char *chunkname);
void        wb_lv_register_global(wb_lv *L, const char *name, wb_lv_cfn fn, void *ud);
wb_lv_value wb_lv_get_global(wb_lv *L, const char *name);
void        wb_lv_set_global(wb_lv *L, const char *name, wb_lv_value v);

/* host-driven call: invoke a function value (e.g. step) with args */
wb_lv_value wb_lv_call(wb_lv *L, wb_lv_value fn, wb_lv_value *args, int nargs);

#endif /* WB_LV_H */
