#include "wb_ecs.h"
#include <stdlib.h>
#include <string.h>

/* bit helpers (declared here; wb_ecs.h keeps the iter API) */
static inline int bit_get(const uint8_t *bm, uint32_t i){return (bm[i>>3]>>(i&7))&1;}
static inline void bit_set(uint8_t *bm, uint32_t i){bm[i>>3] |= (1u<<(i&7));}
static inline void bit_clear(uint8_t *bm, uint32_t i){bm[i>>3] &= ~(1u<<(i&7));}

/* --- world lifecycle --- */

void wb_world_init(wb_world *w) {
    memset(w, 0, sizeof(*w));
    w->gen = calloc(WB_MAX_ENTITIES, sizeof(uint32_t));
    w->alive = calloc(WB_MAX_ENTITIES, 1);
    w->count = 0;
    w->next_index = 0;
}

void wb_world_free(wb_world *w) {
    if (!w) return;
    for (uint32_t t = 0; t < WB_MAX_COMP_TYPES; t++) {
        if (w->cols[t].data) free(w->cols[t].data);
        if (w->cols[t].present) free(w->cols[t].present);
    }
    free(w->gen);
    free(w->alive);
    memset(w, 0, sizeof(*w));
}

/* --- component registration --- */

wb_comp_type wb_register_comp(wb_world *w, const char *name, size_t elem_size) {
    if (w->comp_count >= WB_MAX_COMP_TYPES) return (uint32_t)-1;
    wb_comp_type t = w->comp_count++;
    w->descs[t].name = name;
    w->descs[t].elem_size = elem_size;
    w->cols[t].elem_size = elem_size;
    w->cols[t].capacity = 0;
    return t;
}

/* grow a column's SoA buffers to hold 'need' dense slots. */
static void col_grow(wb_column *c, uint32_t need) {
    if (need <= c->capacity) return;
    size_t cap = c->capacity ? c->capacity : 8u;
    while (cap < need) cap *= 2u;
    c->data = realloc(c->data, cap * c->elem_size);
    c->present = realloc(c->present, cap);
    if (c->capacity) {
        memset(c->present + c->capacity, 0, cap - c->capacity);
    } else {
        memset(c->present, 0, cap);
    }
    c->capacity = cap;
}

/* --- entities --- */

wb_entity wb_entity_new(wb_world *w) {
    uint32_t idx;
    /* free-list scan for a dead slot keeps pool dense-ish */
    for (idx = 0; idx < w->next_index; idx++)
        if (!w->alive[idx]) break;
    if (idx >= w->next_index) {
        w->gen[idx] = 0;
        w->next_index++;
    }
    if (w->next_index > WB_MAX_ENTITIES) return (wb_entity){0, 0};
    w->alive[idx] = 1;
    w->count++;
    for (uint32_t t = 0; t < w->comp_count; t++) {
        if (idx >= w->cols[t].capacity) col_grow(&w->cols[t], idx + 1);
    }
    return (wb_entity){idx, w->gen[idx]};
}

void wb_entity_free(wb_world *w, wb_entity e) {
    if (!wb_entity_valid(w, e) || e.index >= w->next_index) return;
    w->alive[e.index] = 0;
    w->gen[e.index]++;            /* invalidate stale handles */
    for (uint32_t t = 0; t < w->comp_count; t++) {
        bit_clear(w->cols[t].present, e.index);
    }
    w->count--;
}

int wb_entity_valid(const wb_world *w, wb_entity e) {
    return e.index < w->next_index && w->alive[e.index] && w->gen[e.index] == e.gen;
}

uint32_t wb_entity_count(const wb_world *w) { return w->count; }

/* --- components --- */

void *wb_comp_add(wb_world *w, wb_entity e, wb_comp_type t) {
    if (!wb_entity_valid(w, e) || t >= w->comp_count) return NULL;
    wb_column *c = &w->cols[t];
    if (e.index >= c->capacity) col_grow(c, e.index + 1);
    bit_set(c->present, e.index);
    return c->data + (size_t)e.index * c->elem_size;
}

void *wb_comp_get(wb_world *w, wb_entity e, wb_comp_type t) {
    if (!wb_entity_valid(w, e) || t >= w->comp_count) return NULL;
    wb_column *c = &w->cols[t];
    if (e.index >= c->capacity || !bit_get(c->present, e.index)) return NULL;
    return c->data + (size_t)e.index * c->elem_size;
}

void wb_comp_remove(wb_world *w, wb_entity e, wb_comp_type t) {
    if (!wb_entity_valid(w, e) || t >= w->comp_count) return;
    wb_column *c = &w->cols[t];
    if (e.index < c->capacity) bit_clear(c->present, e.index);
}

void wb_comp_iter(const wb_world *w, wb_comp_type t, wb_comp_iter_cb cb, void *user) {
    if (t >= w->comp_count) return;
    const wb_column *c = &w->cols[t];
    uint32_t n = w->next_index < c->capacity ? w->next_index : c->capacity;
    for (uint32_t i = 0; i < n; i++) {
        if (bit_get(c->present, i))
            cb(c->data + (size_t)i * c->elem_size, user);
    }
}
