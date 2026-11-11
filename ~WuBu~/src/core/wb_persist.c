/*
 * wb_persist.c -- per-level persistence (LP-102 stone 1). See wb_persist.h.
 */
#include "wb_persist.h"

#include <stddef.h>
#include <string.h>

int wb_persist_class_durable(wb_persist_class cls) {
    return cls == WB_PERSIST_SEED || cls == WB_PERSIST_AUTHORED ||
           cls == WB_PERSIST_PATCHED;
}

int wb_persist_class_immutable(wb_persist_class cls) {
    return cls == WB_PERSIST_SEED;
}

const char *wb_persist_class_name(wb_persist_class cls) {
    switch (cls) {
    case WB_PERSIST_SEED:      return "seed";
    case WB_PERSIST_DERIVED:   return "derived";
    case WB_PERSIST_AUTHORED:  return "authored";
    case WB_PERSIST_PATCHED:   return "patched";
    case WB_PERSIST_EPHEMERAL: return "ephemeral";
    default:                   return "?";
    }
}

void wb_persist_init(wb_persist_plan *p) {
    if (!p) return;
    memset(p, 0, sizeof *p);
    p->epoch = 1;
}

uint32_t wb_persist_count(const wb_persist_plan *p) {
    return p ? p->n : 0;
}

uint64_t wb_persist_epoch(const wb_persist_plan *p) {
    return p ? p->epoch : 0;
}

static int wb_persist_cls_valid(wb_persist_class cls) {
    return (unsigned)cls <= (unsigned)WB_PERSIST_EPHEMERAL;
}

/* The nearest durable level strictly coarser than `depth`, or -1. Levels are
 * ordered by depth, so the scan can stop at the first one that is not coarser. */
static int wb_persist_source_before(const wb_persist_plan *p, uint32_t depth) {
    int best = -1;
    for (uint32_t i = 0; i < p->n; i++) {
        if (p->levels[i].depth >= depth) break;
        if (wb_persist_class_durable(p->levels[i].cls)) best = (int)i;
    }
    return best;
}

int wb_persist_add(wb_persist_plan *p, const char *name, uint32_t depth,
                   wb_persist_class cls) {
    if (!p || !name || !*name) return -1;
    if (!wb_persist_cls_valid(cls)) return -1;
    if (strlen(name) >= WB_PERSIST_NAME) return -1;
    if (p->n >= WB_PERSIST_MAX) return -1;
    if (p->n > 0 && depth <= p->levels[p->n - 1].depth) return -1;
    /* A derived level is computed from a coarser durable level; with none there
     * is nothing to derive from, so the plan would be lying about its own state. */
    if (cls == WB_PERSIST_DERIVED && wb_persist_source_before(p, depth) < 0)
        return -1;

    wb_persist_level *l = &p->levels[p->n++];
    memset(l, 0, sizeof *l);
    strncpy(l->name, name, WB_PERSIST_NAME - 1);
    l->depth = depth;
    l->cls = (uint32_t)cls;
    l->written = 0;
    return 0;
}

const char *wb_persist_name(const wb_persist_plan *p, uint32_t i) {
    if (!p || i >= p->n) return NULL;
    return p->levels[i].name;
}

uint32_t wb_persist_depth(const wb_persist_plan *p, uint32_t i) {
    if (!p || i >= p->n) return 0;
    return p->levels[i].depth;
}

wb_persist_class wb_persist_level_class(const wb_persist_plan *p, uint32_t i) {
    if (!p || i >= p->n) return (wb_persist_class)0;
    return (wb_persist_class)p->levels[i].cls;
}

uint32_t wb_persist_written(const wb_persist_plan *p, uint32_t i) {
    if (!p || i >= p->n) return 0;
    return p->levels[i].written;
}

int wb_persist_owner_for_depth(const wb_persist_plan *p, uint32_t depth) {
    if (!p) return -1;
    int best = -1;
    for (uint32_t i = 0; i < p->n; i++) {
        if (p->levels[i].depth > depth) break;
        if (wb_persist_class_durable((wb_persist_class)p->levels[i].cls))
            best = (int)i;
    }
    return best;
}

int wb_persist_owner(const wb_persist_plan *p, const char *cell) {
    if (!p || !cell || !wb_qkey_valid(cell)) return -1;
    return wb_persist_owner_for_depth(p, wb_qkey_level(cell));
}

int wb_persist_source(const wb_persist_plan *p, uint32_t level) {
    if (!p || level >= p->n) return -1;
    return wb_persist_source_before(p, p->levels[level].depth);
}

int wb_persist_write(wb_persist_plan *p, const char *cell, uint32_t level,
                     uint64_t *epoch_out) {
    if (epoch_out) *epoch_out = 0;
    if (!p || !cell || !wb_qkey_valid(cell)) return -1;
    if (level >= p->n) return -1;
    const wb_persist_level *l = &p->levels[level];
    /* A derived level owns no bytes and an ephemeral one is never stored. */
    if (!wb_persist_class_durable((wb_persist_class)l->cls)) return -1;
    /* The seed is written once: a second write would silently change the world
     * every lower level is derived from. */
    if (wb_persist_class_immutable((wb_persist_class)l->cls) && l->written)
        return -1;
    /* Only the owning level may accept the write -- a coarser level would lose
     * the finer ones' state, a finer one would store an ancestor's. */
    if (wb_persist_owner_for_depth(p, wb_qkey_level(cell)) != (int)level)
        return -1;

    p->levels[level].written = 1;
    p->epoch++;
    if (epoch_out) *epoch_out = p->epoch;
    return 0;
}
