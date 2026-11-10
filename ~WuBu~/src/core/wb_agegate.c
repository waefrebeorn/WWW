/*
 * wb_agegate.c -- age gating (LP-54 stone 1). See wb_agegate.h.
 */
#include "wb_agegate.h"

#include <stddef.h>

const wb_agegate_level *wb_agegate_tier(const wb_agegate_ladder *ladder, uint32_t rating) {
    if (!ladder || !ladder->tiers || rating >= ladder->ntiers) return NULL;
    return &ladder->tiers[rating];
}

/* The highest descriptor floor among the entry's set bits, or 0 for none. */
static uint32_t wb_agegate_descriptor_floor(const wb_agegate_ladder *ladder,
                                            const wb_agegate_entry *e) {
    if (!ladder || !ladder->descs || !e) return 0;
    uint32_t worst = 0;
    for (uint32_t i = 0; i < ladder->ndescs; i++) {
        const wb_agegate_descriptor *d = &ladder->descs[i];
        if ((e->flags & d->bit) == 0) continue;
        if (d->min_age > worst) worst = d->min_age;
    }
    return worst;
}

uint32_t wb_agegate_min_age(const wb_agegate_ladder *ladder, const wb_agegate_entry *e) {
    if (!ladder || !e) return 0;
    const wb_agegate_level *t = wb_agegate_tier(ladder, e->rating);
    if (!t) return 0;
    /* the tier's own implied descriptors are already reflected in its age, so
     * they are not counted a second time */
    uint32_t floor = wb_agegate_descriptor_floor(ladder, e);
    return t->min_age > floor ? t->min_age : floor;
}

int wb_agegate_allowed(const wb_agegate_ladder *ladder, const wb_agegate_entry *e, uint32_t age) {
    if (!ladder || !e) return 0;
    if (!wb_agegate_tier(ladder, e->rating)) return 0;
    return age >= wb_agegate_min_age(ladder, e);
}

uint32_t wb_agegate_descriptors(const wb_agegate_ladder *ladder, const wb_agegate_entry *e,
                                const char **out, uint32_t cap) {
    if (!ladder || !e || !out || cap == 0 || !ladder->descs) return 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < ladder->ndescs && n < cap; i++) {
        const wb_agegate_descriptor *d = &ladder->descs[i];
        if (e->flags & d->bit) out[n++] = d->code;
    }
    return n;
}
