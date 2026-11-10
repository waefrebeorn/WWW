/*
 * wb_cvar.h -- console variable registry (LP-40).
 *
 * A cvar is a typed, named value with a default, a range, and an optional change
 * callback. Tweaking one at runtime -- from a console, a config file, or a
 * network message -- must not silently corrupt it: numeric values clamp to their
 * range, a string assignment to an int is rejected, and the callback fires only
 * when the value actually changed.
 *
 * C11, no deps.
 */
#ifndef WB_CVAR_H
#define WB_CVAR_H

#include <stdint.h>

typedef enum {
    WB_CVAR_INT = 0,
    WB_CVAR_FLOAT,
    WB_CVAR_BOOL,
    WB_CVAR_STRING
} wb_cvar_type;

typedef struct wb_cvar {
    const char   *name;
    wb_cvar_type  type;
    union { int i; float f; int b; char s[96]; } value;
    union { int i; float f; int b; char s[96]; } def;
    float         lo, hi;      /* the range for numeric types */
    void        (*on_change)(struct wb_cvar *self);
    int           dirty;       /* 1 when a value changed since the last clear */
} wb_cvar;

typedef struct {
    wb_cvar  *vars;
    uint32_t  n, cap;
} wb_cvar_registry;

int  wb_cvar_registry_init(wb_cvar_registry *r, uint32_t cap);
void wb_cvar_registry_free(wb_cvar_registry *r);

/* Register a cvar (zero-initialised to its default). Returns NULL if the name is
 * taken or the table is full. */
wb_cvar *wb_cvar_register(wb_cvar_registry *r, const char *name, wb_cvar_type type);
wb_cvar *wb_cvar_find(wb_cvar_registry *r, const char *name);

int  wb_cvar_set_int(wb_cvar *c, int v);
int  wb_cvar_set_float(wb_cvar *c, float v);
int  wb_cvar_set_bool(wb_cvar *c, int v);
int  wb_cvar_set_string(wb_cvar *c, const char *v);
int  wb_cvar_reset(wb_cvar *c);

int  wb_cvar_dirty(const wb_cvar *c);
void wb_cvar_clear_dirty(wb_cvar *c);

/* Assign the default (used at registration and by reset). */
void wb_cvar_set_default(wb_cvar *c, wb_cvar_type type, int i, float f, int b, const char *s);

#endif /* WB_CVAR_H */
