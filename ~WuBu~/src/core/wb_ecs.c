/*
 * wb_ecs.c -- WuBu Engine entity-component-system core. See wb_ecs.h.
 */
#include "wb_ecs.h"
#include <stdlib.h>
#include <string.h>

/* A registered component type: name + element size for its SoA column. */
typedef struct {
    const char *name;
    size_t      elem_size;
} wb_comp_desc;

/* Component column: contiguous SoA storage, grown by pool doubling. */
typedef struct {
    uint8_t *data;      /* elem_size * capacity */
    uint8_t *present;   /* bitmask per index: slot has this comp */
    size_t   elem_size;
    size_t   capacity;
} wb_column;

struct wb_world {
    /* dense entity pool */
    uint32_t *      gen;        /* generation per dense slot */
    uint8_t *       alive;      /* slot occupied */
    uint32_t        count;      /* live entities */
    uint32_t        next_index; /* bump allocator index (before recycle) */

    /* component registry */
    wb_comp_desc    descs[WB_MAX_COMP_TYPES];
    wb_column       cols[WB_MAX_COMP_TYPES];
    uint32_t        comp_count;

    uint64_t        tick;
};

/* bit helpers */
static inline int bit_get(const uint8_t *bm, uint32_t i){return (bm[i>>3]>>(i&7))&1;}
static inline void bit_set(uint8_t *bm, uint32_t i){bm[i>>3] |= (1u<<(i&7));}
static inline void bit_clear(uint8_t *bm, uint32_t i){bm[i>>3] &= ~(1u<<(i&7));}

/* --- world lifecycle --- */

wb_world *wb_world_new(void) {
    wb_world *w = (wb_world *)calloc(1, sizeof *w);
    if (!w) return NULL;
    w->gen = (uint32_t *)calloc(WB_MAX_ENTITIES, sizeof(uint32_t));
    w->alive = (uint8_t *)calloc(WB_MAX_ENTITIES, 1);
    if (!w->gen || !w->alive) {
        free(w->gen);
        free(w->alive);
        free(w);
        return NULL;
    }
    return w;
}

void wb_world_destroy(wb_world *w) {
    if (!w) return;
    for (uint32_t t = 0; t < WB_MAX_COMP_TYPES; t++) {
        if (w->cols[t].data) free(w->cols[t].data);
        if (w->cols[t].present) free(w->cols[t].present);
    }
    free(w->gen);
    free(w->alive);
    free(w);
}

/* --- read-only accessors --- */

uint32_t wb_world_next_index(const wb_world *w) { return w ? w->next_index : 0; }
int      wb_world_slot_alive(const wb_world *w, uint32_t i) {
    return w && i < w->next_index && w->alive[i];
}
uint32_t wb_world_slot_gen(const wb_world *w, uint32_t i) {
    return (w && i < WB_MAX_ENTITIES) ? w->gen[i] : 0;
}

wb_comp_type wb_world_find_comp(const wb_world *w, const char *name) {
    if (!w || !name) return (wb_comp_type)-1;
    for (uint32_t i = 0; i < w->comp_count; i++)
        if (w->descs[i].name && strcmp(w->descs[i].name, name) == 0) return (wb_comp_type)i;
    return (wb_comp_type)-1;
}

size_t wb_world_comp_elem_size(const wb_world *w, wb_comp_type t) {
    return (w && t < w->comp_count) ? w->cols[t].elem_size : 0;
}

void *wb_world_comp_data(const wb_world *w, wb_comp_type t) {
    return (w && t < w->comp_count) ? w->cols[t].data : NULL;
}

size_t wb_world_comp_capacity(const wb_world *w, wb_comp_type t) {
    return (w && t < w->comp_count) ? w->cols[t].capacity : 0;
}

int wb_world_comp_present(const wb_world *w, wb_comp_type t, uint32_t i) {
    if (!w || t >= w->comp_count) return 0;
    const wb_column *c = &w->cols[t];
    return i < c->capacity && bit_get(c->present, i);
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
    c->data = (uint8_t *)realloc(c->data, cap * c->elem_size);
    c->present = (uint8_t *)realloc(c->present, cap);
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

uint32_t wb_entity_count(const wb_world *w) { return w ? w->count : 0; }

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
    uint32_t n = w->next_index < c->capacity ? w->next_index : (uint32_t)c->capacity;
    for (uint32_t i = 0; i < n; i++) {
        if (bit_get(c->present, i))
            cb(c->data + (size_t)i * c->elem_size, user);
    }
}
