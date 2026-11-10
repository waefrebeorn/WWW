/*
 * wb_assetgraph.c -- asset dependency graph (LP-36 stone 1). See wb_assetgraph.h.
 */
#include "wb_assetgraph.h"

#include <stdlib.h>

int wb_assetgraph_init(wb_assetgraph *g, uint32_t n) {
    if (!g || n == 0) return -1;
    g->nodes = (wb_asset_node *)calloc(n, sizeof(wb_asset_node));
    if (!g->nodes) return -1;
    g->n = n;
    return 0;
}

void wb_assetgraph_free(wb_assetgraph *g) {
    if (!g) return;
    for (uint32_t i = 0; i < g->n; i++) free(g->nodes[i].deps);
    free(g->nodes);
    g->nodes = NULL;
    g->n = 0;
}

/* can `from` reach `target` by following dependency edges? */
static int wb_assetgraph_reaches(const wb_assetgraph *g, uint32_t from, uint32_t target) {
    if (from == target) return 1;
    for (uint32_t i = 0; i < g->nodes[from].ndeps; i++)
        if (wb_assetgraph_reaches(g, g->nodes[from].deps[i], target)) return 1;
    return 0;
}

int wb_assetgraph_add_dep(wb_assetgraph *g, uint32_t asset, uint32_t depends_on) {
    if (!g || !g->nodes || asset >= g->n || depends_on >= g->n) return -1;
    if (asset == depends_on) return -1;
    /* a cycle appears if `asset` is reachable from `depends_on` */
    if (wb_assetgraph_reaches(g, depends_on, asset)) return -1;

    wb_asset_node *nd = &g->nodes[asset];
    if (nd->ndeps == nd->dep_cap) {
        uint32_t nc = nd->dep_cap ? nd->dep_cap * 2 : 4;
        uint32_t *ndp = (uint32_t *)realloc(nd->deps, nc * sizeof(uint32_t));
        if (!ndp) return -1;
        nd->deps = ndp;
        nd->dep_cap = nc;
    }
    nd->deps[nd->ndeps++] = depends_on;
    return 0;
}

int wb_assetgraph_topo(const wb_assetgraph *g, uint32_t *out) {
    if (!g || !g->nodes || !out) return -1;
    uint32_t n = g->n;
    uint32_t *indeg = (uint32_t *)calloc(n, sizeof(uint32_t));
    if (!indeg) return -1;
    /* the forward in-degree: how many dependencies each asset still has */
    for (uint32_t i = 0; i < n; i++) indeg[i] = g->nodes[i].ndeps;

    uint32_t *queue = (uint32_t *)malloc(n * sizeof(uint32_t));
    if (!queue) { free(indeg); return -1; }
    uint32_t head = 0, tail = 0;
    for (uint32_t i = 0; i < n; i++) if (indeg[i] == 0) queue[tail++] = i;

    uint32_t written = 0;
    while (head < tail) {
        uint32_t u = queue[head++];
        out[written++] = u;
        /* decrement everything that depends on u */
        for (uint32_t v = 0; v < n; v++) {
            for (uint32_t k = 0; k < g->nodes[v].ndeps; k++) {
                if (g->nodes[v].deps[k] == u) {
                    if (indeg[v] > 0) indeg[v]--;
                    if (indeg[v] == 0) queue[tail++] = v;
                    break;
                }
            }
        }
    }
    free(indeg);
    free(queue);
    return written == n ? (int)written : -1;   /* -1 => a cycle slipped through */
}

uint32_t wb_assetgraph_dirty_closure(const wb_assetgraph *g, uint32_t asset, uint32_t *out) {
    if (!g || !g->nodes || asset >= g->n || !out) return 0;
    uint32_t n = g->n;
    uint8_t *seen = (uint8_t *)calloc(n, 1);
    if (!seen) return 0;
    uint32_t *stack = (uint32_t *)malloc(n * sizeof(uint32_t));
    if (!stack) { free(seen); return 0; }

    uint32_t sp = 0, written = 0;
    stack[sp++] = asset;
    seen[asset] = 1;
    while (sp > 0) {
        uint32_t u = stack[--sp];
        out[written++] = u;
        /* every asset that lists u as a dependency */
        for (uint32_t v = 0; v < n; v++) {
            if (seen[v]) continue;
            for (uint32_t k = 0; k < g->nodes[v].ndeps; k++) {
                if (g->nodes[v].deps[k] == u) { seen[v] = 1; stack[sp++] = v; break; }
            }
        }
    }
    free(seen);
    free(stack);
    return written;
}

uint32_t wb_assetgraph_dep_count(const wb_assetgraph *g, uint32_t asset) {
    if (!g || !g->nodes || asset >= g->n) return 0;
    return g->nodes[asset].ndeps;
}
