/*
 * wb_bindless.h -- bindless resource table (LP-11).
 *
 * Bindless binding replaces per-draw descriptor sets with one global table: a
 * draw references a resource by a stable u32 index instead of binding it. That
 * removes the per-draw CPU cost of descriptor management and lets a material
 * reference any texture/buffer without a rebind.
 *
 * This is the CPU-side table: dense indices, stable for a resource's lifetime,
 * recycled on removal, and safe to read from any thread. The GPU-side half is
 * just a heap of resources indexed by the same u32.
 *
 * C11, no deps.
 */
#ifndef WB_BINDLESS_H
#define WB_BINDLESS_H

#include <stdint.h>

typedef struct {
    uint32_t *slots;      /* index -> resource handle (0 = empty) */
    uint32_t  cap;
    uint32_t  high_water; /* the next never-used slot */
    uint32_t *free_list;  /* indices available for reuse */
    uint32_t  n_free, free_cap;
    uint32_t  live;       /* resources currently bound */
} wb_bindless;

int  wb_bindless_init(wb_bindless *t, uint32_t capacity);
void wb_bindless_free(wb_bindless *t);

/* Allocate a slot for `resource`; returns 0 on success and writes the index. */
int  wb_bindless_add(wb_bindless *t, uint32_t resource, uint32_t *out_index);

/* Release a slot (the resource handle is cleared; the index is recycled). */
void wb_bindless_remove(wb_bindless *t, uint32_t index);

uint32_t wb_bindless_get(const wb_bindless *t, uint32_t index);   /* 0 if empty */
uint32_t wb_bindless_live(const wb_bindless *t);

#endif /* WB_BINDLESS_H */
