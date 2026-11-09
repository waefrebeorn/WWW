/*
 * wb_lv_internal.h -- private interface of the slerm-Lua interpreter.
 *
 * Included only by the wb_lv*.c translation units. The public header keeps
 * wb_lv / wb_lv_value / wb_lv_func / wb_lv_table opaque; this is where the
 * state, the AST and the parser live, so no caller can reach into them.
 */
#ifndef WB_LV_INTERNAL_H
#define WB_LV_INTERNAL_H

#include "wb_lv.h"

#include <stddef.h>
#include <stdio.h>

/* ---------------------------------------------------------------- arena */
/*
 * NON-intrusive arena: an 8-byte header precedes each block, so it works on
 * any struct. (An intrusive arena that writes `next` into the block's first
 * field silently corrupts interners and tables.)
 */
typedef struct anode { struct anode *next; } anode;

void *wb_lv_arena_alloc(wb_lv *L, size_t n);
#define WB_LV_ALLOC(T, L)     ((T *)wb_lv_arena_alloc((L), sizeof(T)))
#define WB_LV_ALLOCN(T, n, L) ((T *)wb_lv_arena_alloc((L), (n) * sizeof(T)))

/* ---------------------------------------------------------------- state */

struct wb_lv {
    struct anode *root;
    struct istr **buckets; size_t nbuckets;
    wb_lv_table *globals;
    wb_lv_value *stack; int sp, scap;
    char err[512]; int has_err;
    int depth;
};

/* --------------------------------------------------------- string pool */

typedef struct istr { char *s; size_t len; unsigned hash; struct istr *next; } istr;

/* --------------------------------------------------------------- tables */

typedef struct wb_lv_kv { wb_lv_value k, v; int used; } wb_lv_kv;
struct wb_lv_table { wb_lv_kv *e; size_t cap, count; };

/* ------------------------------------------------------------------ AST */

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

#define WB_LV_MAX_EX 8192
typedef struct { ex n[WB_LV_MAX_EX]; int cnt; } pool;

/* --------------------------------------------------------------- parser */

typedef enum {
    T_EOF=0, T_NUM=256, T_STR, T_NAME,
    T_IF,T_THEN,T_ELSE,T_ELSEIF,T_END,T_WHILE,T_DO,T_FOR,T_FUNCTION,
    T_LOCAL,T_RETURN,T_IN,T_TRUE,T_FALSE,T_NIL,T_NOT,T_AND,T_OR,
    T_NE,T_LE,T_GE,T_CONCAT,T_EQ,   /* multi-char/compound */
} tktype;

typedef struct { int type; double num; const char *str; int line; } tok;

#define WB_LV_TKCHAR(c) ((int)(unsigned char)(c))   /* single-char tokens < 256 */

typedef struct {
    const char *src; size_t p; int line;
    tok cur;
    wb_lv *L; pool *pl; int err;
} parser;

void wb_lv_next_tok(parser *P);
void wb_lv_perr(parser *P, const char *msg);

ex *wb_lv_parse_expr(parser *P);
ex *wb_lv_parse_stmt(parser *P);
ex *wb_lv_parse_block(parser *P, const char *until, const char *until2, int *matched);
const char *wb_lv_parse_driver(wb_lv *L, const char *src, pool *pl, ex **out);

/* -------------------------------------------------------------- runtime */

typedef struct env { struct env *parent; wb_lv_table *t; } env;

/* a closure references its defining env; calling sets up a new frame */
struct wb_lv_func {
    ex *body;                  /* N_BLOCK */
    const char *name;
    env *defining;
    int nparams;               /* number of param names */
    const char **params;       /* param names */
};

wb_lv_value wb_lv_eval(wb_lv *L, ex *e, env *en);
wb_lv_value wb_lv_exec(wb_lv *L, ex *e, env *en, int *was_return);
wb_lv_value wb_lv_call_lua(wb_lv *L, wb_lv_func *fn, wb_lv_value *args, int nargs);
wb_lv_value wb_lv_exec_root(wb_lv *L, ex *root);

/* ----------------------------------------------------------------- host */

wb_lv *wb_lv_alloc(void);
void   wb_lv_register_builtins(wb_lv *L);

#endif /* WB_LV_INTERNAL_H */
