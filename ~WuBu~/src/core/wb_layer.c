/*
 * wb_layer.c -- per-level capacity and layering (LP-106 stone 1). See wb_layer.h.
 */
#include "wb_layer.h"

#include <stddef.h>
#include <stdlib.h>

static wb_layer_inst *wb_layer_inst_find(wb_layer_pool *p, uint64_t id) {
    if (!p || !p->layers) return NULL;
    for (uint32_t i = 0; i < p->n; i++)
        if (p->layers[i].id == id) return &p->layers[i];
    return NULL;
}

int wb_layer_init(wb_layer_pool *p, uint32_t cap, uint32_t max_layers,
                  uint32_t layer_capacity, uint32_t mode) {
    if (!p || cap == 0 || layer_capacity == 0) return -1;
    p->layers = (wb_layer_inst *)calloc(cap, sizeof(wb_layer_inst));
    if (!p->layers) return -1;
    p->cap = cap;
    p->n = 0;
    p->max_layers = max_layers;
    p->layer_capacity = layer_capacity;
    p->mode = mode;
    p->next_layer_id = 1;
    return 0;
}

void wb_layer_free(wb_layer_pool *p) {
    if (!p) return;
    free(p->layers);
    p->layers = NULL;
    p->cap = 0;
    p->n = 0;
}

void wb_layer_clear(wb_layer_pool *p) {
    if (!p) return;
    p->n = 0;
    p->next_layer_id = 1;
}

int wb_layer_spawn(wb_layer_pool *p) {
    if (!p || !p->layers) return -1;
    if (p->n >= p->cap) return -1;
    if (p->n >= p->max_layers) return -1;
    wb_layer_inst *l = &p->layers[p->n++];
    l->id = p->next_layer_id++;
    l->capacity = p->layer_capacity;
    l->used = 0;
    l->n = 0;
    return 0;
}

static wb_layer_party *wb_layer_party_find(wb_layer_inst *l, uint64_t party) {
    if (!l) return NULL;
    for (uint32_t i = 0; i < l->n; i++)
        if (l->parties[i].id == party) return &l->parties[i];
    return NULL;
}

int wb_layer_find(const wb_layer_pool *p, uint64_t party, uint64_t *layer) {
    if (!p || !p->layers) return 0;
    for (uint32_t i = 0; i < p->n; i++)
        if (wb_layer_party_find(&p->layers[i], party)) {
            if (layer) *layer = p->layers[i].id;
            return 1;
        }
    return 0;
}

int wb_layer_contains(const wb_layer_pool *p, uint64_t layer, uint64_t party) {
    return wb_layer_party_find(wb_layer_inst_find((wb_layer_pool *)p, layer), party) != NULL;
}

uint32_t wb_layer_used(const wb_layer_pool *p, uint64_t layer) {
    const wb_layer_inst *l = wb_layer_inst_find((wb_layer_pool *)p, layer);
    return l ? l->used : 0;
}

uint32_t wb_layer_free_space(const wb_layer_pool *p, uint64_t layer) {
    const wb_layer_inst *l = wb_layer_inst_find((wb_layer_pool *)p, layer);
    return l ? l->capacity - l->used : 0;
}

uint32_t wb_layer_count(const wb_layer_pool *p) {
    return p ? p->n : 0;
}

wb_layer_result wb_layer_join(wb_layer_pool *p, uint64_t party, uint32_t size) {
    wb_layer_result res;
    res.layer = 0;
    res.reason = WB_LAYER_REFUSED_CEILING;
    if (!p || !p->layers || party == 0 || size == 0) return res;

    /* A party larger than a whole layer is refused rather than split -- co-location
     * is the whole point of the mechanism. */
    if (size > p->layer_capacity) {
        res.reason = WB_LAYER_REFUSED_TOO_BIG;
        return res;
    }

    /* Co-location: a party that is already placed stays where it is. */
    for (uint32_t i = 0; i < p->n; i++) {
        wb_layer_inst *l = &p->layers[i];
        if (!wb_layer_party_find(l, party)) continue;
        /* Growing or shrinking a party replaces its slots: the old size stops
         * counting and the new one starts, so the layer's free space is the
         * current free plus what this party already holds. */
        uint32_t old = wb_layer_party_find(l, party)->size;
        if (l->used - old + size <= l->capacity) {
            wb_layer_party *pp = wb_layer_party_find(l, party);
            l->used = l->used - old + size;
            pp->size = size;
            res.layer = l->id;
            res.reason = WB_LAYER_OK;
        } else {
            res.reason = WB_LAYER_REFUSED_CEILING;
        }
        return res;
    }

    /* Pick a layer. PACK wants the fullest that fits, SPREAD the emptiest; both
     * break ties to the lowest id, so the scan keeps the first strict winner. */
    wb_layer_inst *best = NULL;
    for (uint32_t i = 0; i < p->n; i++) {
        wb_layer_inst *l = &p->layers[i];
        if (l->capacity - l->used < size) continue;
        if (!best) { best = l; continue; }
        uint32_t best_free = best->capacity - best->used;
        uint32_t l_free = l->capacity - l->used;
        int better = (p->mode == WB_LAYER_PACK) ? (l_free < best_free) : (l_free > best_free);
        if (better) best = l;
    }

    /* No layer has room: open a sibling, unless the ceiling says no. */
    if (!best) {
        if (p->n >= p->max_layers || p->n >= p->cap) {
            res.reason = WB_LAYER_REFUSED_CEILING;
            return res;
        }
        if (wb_layer_spawn(p) != 0) {
            res.reason = WB_LAYER_REFUSED_CEILING;
            return res;
        }
        best = &p->layers[p->n - 1];
    }

    best->parties[best->n].id = party;
    best->parties[best->n].size = size;
    best->n++;
    best->used += size;
    res.layer = best->id;
    res.reason = WB_LAYER_OK;
    return res;
}

int wb_layer_leave(wb_layer_pool *p, uint64_t party) {
    if (!p || !p->layers) return -1;
    for (uint32_t i = 0; i < p->n; i++) {
        wb_layer_inst *l = &p->layers[i];
        for (uint32_t j = 0; j < l->n; j++) {
            if (l->parties[j].id != party) continue;
            l->used -= l->parties[j].size;
            /* shift, so the roster keeps its order */
            for (uint32_t k = j; k + 1 < l->n; k++) l->parties[k] = l->parties[k + 1];
            l->n--;
            return 0;
        }
    }
    return -1;
}
