/*
 * wb_ecs.h -- WuBu Engine entity-component-system core.
 *
 * Data-oriented, sparse-set + SoA. Entities are stable handles into a dense
 * list; components live in per-type contiguous arrays indexed by the dense
 * slot. Cache-friendly, O(1) lookup, dynamic component sets.
 *
 * C11, no external deps beyond libc. Game logic lives in Lua (see lua_host.c);
 * this core owns memory + iteration only.
 */
#ifndef WB_ECS_H
#define WB_ECS_H

#include <stdint.h>
#include <stddef.h>

#define WB_MAX_ENTITIES   262144u  /* dense pool */
#define WB_MAX_COMP_TYPES 64u      /* distinct component archetypes */

/* Entity handle: dense index + generation (dangles on reuse). */
typedef struct { uint32_t index; uint32_t gen; } wb_entity;

typedef uint32_t wb_comp_type;

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

typedef struct {
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
} wb_world;

void        wb_world_init(wb_world *w);
void        wb_world_free(wb_world *w);

/* Component types are registered once at init; returns type id. */
wb_comp_type wb_register_comp(wb_world *w, const char *name, size_t elem_size);

wb_entity   wb_entity_new(wb_world *w);
void        wb_entity_free(wb_world *w, wb_entity e);
int         wb_entity_valid(const wb_world *w, wb_entity e);

/* Add/get/remove a component on an entity; returns typed pointer or NULL. */
void       *wb_comp_add(wb_world *w, wb_entity e, wb_comp_type t);
void       *wb_comp_get(wb_world *w, wb_entity e, wb_comp_type t);
void        wb_comp_remove(wb_world *w, wb_entity e, wb_comp_type t);

/* Iterate a column densely (cache-friendly). cb receives pointer to each elem. */
typedef void (*wb_comp_iter_cb)(void *comp, void *user);
void        wb_comp_iter(const wb_world *w, wb_comp_type t, wb_comp_iter_cb cb, void *user);

uint32_t    wb_entity_count(const wb_world *w);

#endif /* WB_ECS_H */
