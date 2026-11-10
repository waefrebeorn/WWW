/*
 * wb_cvar.c -- console variable registry (LP-40). See wb_cvar.h.
 */
#include "wb_cvar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int wb_cvar_registry_init(wb_cvar_registry *r, uint32_t cap) {
    if (!r || cap == 0) return -1;
    r->vars = (wb_cvar *)calloc(cap, sizeof(wb_cvar));
    if (!r->vars) return -1;
    r->n = 0;
    r->cap = cap;
    return 0;
}

void wb_cvar_registry_free(wb_cvar_registry *r) {
    if (!r) return;
    free(r->vars);
    r->vars = NULL;
    r->n = r->cap = 0;
}

wb_cvar *wb_cvar_find(wb_cvar_registry *r, const char *name) {
    if (!r || !r->vars || !name) return NULL;
    for (uint32_t i = 0; i < r->n; i++)
        if (r->vars[i].name && strcmp(r->vars[i].name, name) == 0) return &r->vars[i];
    return NULL;
}

wb_cvar *wb_cvar_register(wb_cvar_registry *r, const char *name, wb_cvar_type type) {
    if (!r || !r->vars || !name) return NULL;
    if (wb_cvar_find(r, name)) return NULL;
    if (r->n == r->cap) return NULL;
    wb_cvar *c = &r->vars[r->n++];
    memset(c, 0, sizeof *c);
    c->name = name;
    c->type = type;
    c->lo = 0.0f;
    c->hi = 0.0f;
    return c;
}

static void wb_cvar_changed(wb_cvar *c) {
    c->dirty = 1;
    if (c->on_change) c->on_change(c);
}

int wb_cvar_set_int(wb_cvar *c, int v) {
    if (!c || c->type != WB_CVAR_INT) return -1;
    if ((float)v < c->lo) v = (int)c->lo;
    if ((float)v > c->hi) v = (int)c->hi;
    if (c->value.i == v) return 0;
    c->value.i = v;
    wb_cvar_changed(c);
    return 0;
}

int wb_cvar_set_float(wb_cvar *c, float v) {
    if (!c || c->type != WB_CVAR_FLOAT) return -1;
    if (v < c->lo) v = c->lo;
    if (v > c->hi) v = c->hi;
    if (c->value.f == v) return 0;
    c->value.f = v;
    wb_cvar_changed(c);
    return 0;
}

int wb_cvar_set_bool(wb_cvar *c, int v) {
    if (!c || c->type != WB_CVAR_BOOL) return -1;
    int b = v ? 1 : 0;
    if (c->value.b == b) return 0;
    c->value.b = b;
    wb_cvar_changed(c);
    return 0;
}

int wb_cvar_set_string(wb_cvar *c, const char *v) {
    if (!c || c->type != WB_CVAR_STRING || !v) return -1;
    if (strcmp(c->value.s, v) == 0) return 0;
    snprintf(c->value.s, sizeof c->value.s, "%s", v);
    wb_cvar_changed(c);
    return 0;
}

int wb_cvar_reset(wb_cvar *c) {
    if (!c) return -1;
    switch (c->type) {
    case WB_CVAR_INT:    return wb_cvar_set_int(c, c->def.i);
    case WB_CVAR_FLOAT:  return wb_cvar_set_float(c, c->def.f);
    case WB_CVAR_BOOL:   return wb_cvar_set_bool(c, c->def.b);
    case WB_CVAR_STRING: return wb_cvar_set_string(c, c->def.s);
    }
    return -1;
}

int wb_cvar_dirty(const wb_cvar *c) { return c ? c->dirty : 0; }
void wb_cvar_clear_dirty(wb_cvar *c) { if (c) c->dirty = 0; }

void wb_cvar_set_default(wb_cvar *c, wb_cvar_type type, int i, float f, int b, const char *s) {
    if (!c || c->type != type) return;
    switch (type) {
    case WB_CVAR_INT:    c->def.i = i; c->value.i = i; break;
    case WB_CVAR_FLOAT:  c->def.f = f; c->value.f = f; break;
    case WB_CVAR_BOOL:   c->def.b = b; c->value.b = b; break;
    case WB_CVAR_STRING: snprintf(c->def.s, sizeof c->def.s, "%s", s ? s : "");
                         snprintf(c->value.s, sizeof c->value.s, "%s", s ? s : ""); break;
    }
}
