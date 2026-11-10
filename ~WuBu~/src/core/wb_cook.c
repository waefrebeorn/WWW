/*
 * wb_cook.c -- cook manifest (LP-46 stone 1). See wb_cook.h.
 */
#include "wb_cook.h"

#include <stdlib.h>
#include <string.h>

static uint64_t wb_cook_mix(uint64_t h, uint64_t v) {
    for (int b = 0; b < 8; b++) {
        h ^= (v >> (b * 8)) & 0xFFu;
        h *= 1099511628211ull;
    }
    return h;
}

static uint64_t wb_cook_chain(const wb_cook_manifest *m) {
    uint64_t h = 1469598103934665603ull;
    for (uint32_t i = 0; i < m->n; i++) {
        h = wb_cook_mix(h, m->entries[i].asset);
        h = wb_cook_mix(h, m->entries[i].hash);
    }
    return h;
}

int wb_cook_init(wb_cook_manifest *m, uint32_t cap) {
    if (!m || cap == 0) return -1;
    m->entries = (wb_cook_entry *)calloc(cap, sizeof(wb_cook_entry));
    if (!m->entries) return -1;
    m->n = 0;
    m->cap = cap;
    m->chain = 0;
    return 0;
}

void wb_cook_free(wb_cook_manifest *m) {
    if (!m) return;
    free(m->entries);
    m->entries = NULL;
    m->n = m->cap = 0;
}

void wb_cook_clear(wb_cook_manifest *m) {
    if (!m) return;
    m->n = 0;
    m->chain = 0;
}

int wb_cook_add(wb_cook_manifest *m, uint32_t asset, uint64_t hash) {
    if (!m || !m->entries) return -1;
    if (m->n == m->cap) return -1;
    m->entries[m->n].asset = asset;
    m->entries[m->n].hash = hash;
    m->n++;
    return 0;
}

uint64_t wb_cook_recompute(wb_cook_manifest *m) {
    if (!m) return 0;
    m->chain = wb_cook_chain(m);
    return m->chain;
}

int wb_cook_verify(const wb_cook_manifest *m) {
    if (!m || !m->entries) return 0;
    return wb_cook_chain(m) == m->chain;
}

uint32_t wb_cook_diff(const wb_cook_manifest *a, const wb_cook_manifest *b) {
    if (!a || !b) return 0;
    uint32_t n = a->n < b->n ? a->n : b->n;
    for (uint32_t i = 0; i < n; i++) {
        if (a->entries[i].asset != b->entries[i].asset) return i;
        if (a->entries[i].hash != b->entries[i].hash) return i;
    }
    return n;   /* identical up to the shorter length */
}

int wb_cook_order_ok(const wb_cook_manifest *m, const wb_assetgraph *g) {
    if (!m || !m->entries || !g || !g->nodes || g->n == 0) return 0;
    if (m->n != g->n) return 0;

    /* position[asset] = its slot; every asset must appear exactly once */
    int32_t *pos = (int32_t *)malloc((size_t)g->n * sizeof(int32_t));
    if (!pos) return 0;
    for (uint32_t i = 0; i < g->n; i++) pos[i] = -1;
    int ok = 1;
    for (uint32_t i = 0; i < m->n; i++) {
        uint32_t a = m->entries[i].asset;
        if (a >= g->n || pos[a] != -1) { ok = 0; break; }   /* out of range or duplicated */
        pos[a] = (int32_t)i;
    }
    if (ok) {
        for (uint32_t i = 0; i < m->n; i++) {
            uint32_t a = m->entries[i].asset;
            for (uint32_t d = 0; d < g->nodes[a].ndeps; d++) {
                uint32_t dep = g->nodes[a].deps[d];
                if (pos[dep] >= (int32_t)i) { ok = 0; break; }  /* not earlier */
            }
            if (!ok) break;
        }
    }
    free(pos);
    return ok;
}
