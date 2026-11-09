/*
 * wb_lv_value.c -- values, string interning and tables. See wb_lv.h.
 *
 * Open addressing, no-delete: replace on hit, grow by doubling. Keys are
 * compared by identity for interned strings and by value for numbers.
 */
#include "wb_lv_internal.h"

#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- values */

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

/* The single type-name table, shared by the error path and the `type`
 * builtin so the two cannot drift. */
const char *wb_lv_type_name(wb_lv_type t) {
    switch (t) {
    case WB_LV_NIL: return "nil";
    case WB_LV_BOOL: return "boolean";
    case WB_LV_NUM: return "number";
    case WB_LV_STR: return "string";
    case WB_LV_TABLE: return "table";
    case WB_LV_FUNCTION:
    case WB_LV_CFUNCTION: return "function";
    }
    return "unknown";
}

/* -------------------------------------------------------- string interning */

static unsigned strhash(const char *s, size_t n) {
    unsigned h = 2166136261u;
    for (size_t i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 16777619u; }
    return h;
}

const char *wb_lv_intern(wb_lv *L, const char *bytes, size_t len) {
    unsigned h = strhash(bytes, len), b = h % L->nbuckets;
    for (istr *it = L->buckets[b]; it; it = it->next)
        if (it->len == len && memcmp(it->s, bytes, len) == 0) return it->s;
    istr *n = (istr *)wb_lv_arena_alloc(L, sizeof(*n) + len + 1);
    if (!n) return NULL;
    n->len = len; n->hash = h; n->s = (char *)(n + 1);
    memcpy(n->s, bytes, len); n->s[len] = 0;
    n->next = L->buckets[b]; L->buckets[b] = n;
    return n->s;
}

/* ---------------------------------------------------------------- tables */

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
    wb_lv_kv *ne = (wb_lv_kv *)wb_lv_arena_alloc(L, nc * sizeof(*ne));
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
    return WB_LV_ALLOC(wb_lv_table, L);
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
