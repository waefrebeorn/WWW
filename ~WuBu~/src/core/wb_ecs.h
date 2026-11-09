/*
 * wb_ecs.h -- WuBu Engine entity-component-system core.
 *
 * Data-oriented, sparse-set + SoA. Entities are stable handles into a dense
 * list; components live in per-type contiguous arrays indexed by the dense
 * slot. Cache-friendly, O(1) lookup, dynamic component sets.
 *
 * `wb_world` is an opaque handle: the column layout is an implementation
 * detail, so this header carries only the operations (and the few read-only
 * accessors a tight loop like wb_physics_tick needs).
 *
 * C11, no external deps beyond libc. Game logic lives in Lua (see wb_script.h);
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

/* Opaque world. Create with wb_world_new(), release with wb_world_destroy(). */
typedef struct wb_world wb_world;

wb_world *wb_world_new(void);
void      wb_world_destroy(wb_world *w);

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

/* ------------------------------------------------- read-only accessors */
/* These exist so a tight per-entity loop (integration, sync) does not have to
 * go through a per-entity call, and so no caller needs the struct layout. */

/* High-water mark of dense slots ever handed out. */
uint32_t wb_world_next_index(const wb_world *w);

/* Slot occupancy and generation (for iterating live entities by index). */
int      wb_world_slot_alive(const wb_world *w, uint32_t index);
uint32_t wb_world_slot_gen(const wb_world *w, uint32_t index);

/* Component type id by registered name, or (wb_comp_type)-1. */
wb_comp_type wb_world_find_comp(const wb_world *w, const char *name);

/* Column layout: element stride, base pointer, capacity, per-slot presence. */
size_t   wb_world_comp_elem_size(const wb_world *w, wb_comp_type t);
void    *wb_world_comp_data(const wb_world *w, wb_comp_type t);
size_t   wb_world_comp_capacity(const wb_world *w, wb_comp_type t);
int      wb_world_comp_present(const wb_world *w, wb_comp_type t, uint32_t index);

#endif /* WB_ECS_H */
