/*
 * wb_assetgraph.h -- asset dependency graph (LP-36 stone 1).
 *
 * The asset pipeline is a graph, not a list: an asset lists what it was built
 * from, the cook schedules leaves first, and when one asset changes only its
 * transitive dependents need rebuilding. This module is that graph -- dependency
 * edges with cycle rejection, a topological build order, and the dirty closure
 * (an asset plus everything that transitively depends on it).
 *
 * C11, no deps.
 */
#ifndef WB_ASSETGRAPH_H
#define WB_ASSETGRAPH_H

#include <stdint.h>

typedef struct {
    uint32_t *deps;          /* assets this one was built from */
    uint32_t  ndeps, dep_cap;
    int       dirty;
} wb_asset_node;

typedef struct {
    wb_asset_node *nodes;
    uint32_t       n;
} wb_assetgraph;

int  wb_assetgraph_init(wb_assetgraph *g, uint32_t n);
void wb_assetgraph_free(wb_assetgraph *g);

/* Add an edge asset -> depends_on. Returns 0 on success, -1 if the edge would
 * create a cycle or the indices are out of range. */
int  wb_assetgraph_add_dep(wb_assetgraph *g, uint32_t asset, uint32_t depends_on);

/* Topological order into `out` (n entries): every asset after its dependencies.
 * Returns the count, or -1 if the graph has a cycle. */
int  wb_assetgraph_topo(const wb_assetgraph *g, uint32_t *out);

/* The asset plus everything that transitively depends on it, into `out`
 * (n entries). Returns the count. */
uint32_t wb_assetgraph_dirty_closure(const wb_assetgraph *g, uint32_t asset, uint32_t *out);

uint32_t wb_assetgraph_dep_count(const wb_assetgraph *g, uint32_t asset);

#endif /* WB_ASSETGRAPH_H */
