/*
 * wb_lodchain.c -- level-of-detail across levels (LP-101 stone 1). See wb_lodchain.h.
 */
#include "wb_lodchain.h"

#include <stdio.h>
#include <string.h>

void wb_lodchain_init(wb_lodchain *c) {
    if (!c) return;
    c->n = 0;
}

uint32_t wb_lodchain_count(const wb_lodchain *c) {
    return c ? c->n : 0;
}

int wb_lodchain_add(wb_lodchain *c, const char *name, uint32_t depth, uint32_t budget) {
    if (!c || !name || !*name) return -1;
    if (strlen(name) >= WB_LODCHAIN_NAME) return -1;
    if (c->n >= WB_LODCHAIN_MAX) return -1;
    /* strictly increasing, so the chain stays ordered coarse -> fine and a level
     * can never be its own ancestor */
    if (c->n > 0 && depth <= c->levels[c->n - 1].depth) return -1;
    if (depth >= WB_QKEY_MAX) return -1;

    wb_lodchain_level *l = &c->levels[c->n++];
    snprintf(l->name, sizeof l->name, "%s", name);
    l->depth = depth;
    l->budget = budget;
    return 0;
}

const char *wb_lodchain_name(const wb_lodchain *c, uint32_t i) {
    if (!c || i >= c->n) return NULL;
    return c->levels[i].name;
}

int wb_lodchain_level_for_depth(const wb_lodchain *c, uint32_t depth) {
    if (!c || c->n == 0) return -1;
    /* The representing level is the deepest one no finer than the cell. A cell
     * shallower than the coarsest level has no representation at all. */
    int best = -1;
    for (uint32_t i = 0; i < c->n; i++)
        if (c->levels[i].depth <= depth) best = (int)i;
    return best;
}

int wb_lodchain_imposter(const wb_lodchain *c, uint32_t i, const char *cell,
                         char *out, size_t cap) {
    if (!c || !cell || !out || cap == 0) return -1;
    if (i >= c->n) return -1;
    uint32_t cd = wb_qkey_depth(cell);
    uint32_t target = c->levels[i].depth;
    /* a level cannot invent detail: a cell shallower than the level's depth has
     * no representation there */
    if (cd < target) return -1;

    char cur[WB_QKEY_MAX];
    snprintf(cur, sizeof cur, "%s", cell);
    while (wb_qkey_depth(cur) > target) {
        char parent[WB_QKEY_MAX];
        if (wb_qkey_up(cur, parent, sizeof parent) != 0) return -1;
        memcpy(cur, parent, sizeof cur);
    }
    if (strlen(cur) + 1 > cap) return -1;
    memcpy(out, cur, strlen(cur) + 1);
    return 0;
}

int wb_lodchain_budget(const wb_lodchain *c, const char *cell, uint32_t *budget) {
    if (!c || !cell || !budget) return -1;
    int i = wb_lodchain_level_for_depth(c, wb_qkey_depth(cell));
    if (i < 0) return -1;
    *budget = c->levels[i].budget;
    return 0;
}
