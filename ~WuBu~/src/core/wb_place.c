/*
 * wb_place.c -- instance placement at a level (LP-105 stone 1). See wb_place.h.
 */
#include "wb_place.h"

#include <stddef.h>
#include <stdlib.h>

static wb_place_node *wb_place_find(wb_place_pool *p, uint64_t id) {
    if (!p || !p->nodes) return NULL;
    for (uint32_t i = 0; i < p->n; i++)
        if (p->nodes[i].id == id) return &p->nodes[i];
    return NULL;
}

static const wb_place_node *wb_place_find_c(const wb_place_pool *p, uint64_t id) {
    return wb_place_find((wb_place_pool *)p, id);
}

int wb_place_init(wb_place_pool *p, uint32_t cap) {
    if (!p || cap == 0) return -1;
    p->nodes = (wb_place_node *)calloc(cap, sizeof(wb_place_node));
    if (!p->nodes) return -1;
    p->cap = cap;
    p->n = 0;
    return 0;
}

void wb_place_free(wb_place_pool *p) {
    if (!p) return;
    free(p->nodes);
    p->nodes = NULL;
    p->cap = 0;
    p->n = 0;
}

void wb_place_clear(wb_place_pool *p) {
    if (!p) return;
    p->n = 0;
}

int wb_place_add(wb_place_pool *p, uint64_t id, uint64_t address, uint32_t capacity, uint32_t flags) {
    if (!p || !p->nodes) return -1;
    if (p->n >= p->cap) return -1;
    if (wb_place_find(p, id)) return -1;
    if (capacity == 0 || capacity > WB_PLACE_MAX_PER_NODE) return -1;
    wb_place_node *nd = &p->nodes[p->n++];
    nd->id = id;
    nd->address = address;
    nd->capacity = capacity;
    nd->flags = flags;
    nd->n = 0;
    return 0;
}

uint32_t wb_place_slots(const wb_place_pool *p, uint64_t node) {
    const wb_place_node *nd = wb_place_find_c(p, node);
    if (!nd) return 0;
    return (nd->flags & WB_PLACE_DEDICATED) ? 1 : nd->capacity;
}

uint32_t wb_place_load(const wb_place_pool *p, uint64_t node) {
    const wb_place_node *nd = wb_place_find_c(p, node);
    return nd ? nd->n : 0;
}

int wb_place_contains(const wb_place_pool *p, uint64_t node, uint64_t instance) {
    const wb_place_node *nd = wb_place_find_c(p, node);
    if (!nd) return 0;
    for (uint32_t i = 0; i < nd->n; i++)
        if (nd->instances[i] == instance) return 1;
    return 0;
}

wb_place_result wb_place_alloc(wb_place_pool *p, const wb_place_request *r) {
    wb_place_result res;
    res.node = 0;
    res.reason = WB_PLACE_REFUSED_NO_NODE;
    if (!p || !p->nodes || !r || r->id == 0) return res;

    /* Is it already placed? A retry must not consume a second slot. */
    for (uint32_t i = 0; i < p->n; i++)
        if (wb_place_contains(p, p->nodes[i].id, r->id)) {
            res.node = p->nodes[i].id;
            res.reason = WB_PLACE_REFUSED_DUPLICATE;
            return res;
        }

    const wb_place_node *best = NULL;
    int saw_full = 0, saw_occupied = 0;

    for (uint32_t i = 0; i < p->n; i++) {
        const wb_place_node *nd = &p->nodes[i];
        /* A node serves exactly the place it is bound to. A general-purpose node
         * (address WB_PLACE_ANY) serves only requests that do not care -- a
         * request naming a place never silently falls back to a generic node,
         * because a typo'd address must be a refusal, not a wrong placement. */
        if (r->address != WB_PLACE_ANY && nd->address != r->address) continue;
        if (r->address == WB_PLACE_ANY && nd->address != WB_PLACE_ANY) continue;

        if (r->flags & WB_PLACE_DEDICATED) {
            if (nd->n != 0) { saw_occupied = 1; continue; }
        } else {
            if (nd->n >= wb_place_slots(p, nd->id)) { saw_full = 1; continue; }
        }
        /* the lowest node id wins, so the walk must not replace a winner */
        if (!best || nd->id < best->id) best = nd;
    }

    if (!best) {
        if (saw_occupied && !saw_full) res.reason = WB_PLACE_REFUSED_EXCLUSIVE;
        else if (saw_full) res.reason = WB_PLACE_REFUSED_FULL;
        return res;
    }

    wb_place_node *w = wb_place_find(p, best->id);
    w->instances[w->n++] = r->id;
    res.node = w->id;
    res.reason = WB_PLACE_OK;
    return res;
}

int wb_place_release(wb_place_pool *p, uint64_t node, uint64_t instance) {
    wb_place_node *nd = wb_place_find(p, node);
    if (!nd) return -1;
    for (uint32_t i = 0; i < nd->n; i++) {
        if (nd->instances[i] != instance) continue;
        /* shift, so the remaining instances keep their order */
        for (uint32_t j = i; j + 1 < nd->n; j++) nd->instances[j] = nd->instances[j + 1];
        nd->n--;
        return 0;
    }
    return -1;
}
