/*
 * wb_hlod.h -- hierarchical LOD clustering (LP-15 stone 2).
 *
 * HLOD: instead of drawing every object in a distant group, draw one aggregate
 * proxy for the whole group. The first step is deciding the groups -- objects
 * that are close together on screen merge into a cluster, and a cluster beyond
 * a distance threshold is drawn as a single imposter.
 *
 * wb_hlod_cluster does the merge: a union-find over objects whose surfaces are
 * within `cluster_radius` of each other. Deterministic -- cluster ids are
 * assigned in order of first appearance.
 *
 * C11, no deps.
 */
#ifndef WB_HLOD_H
#define WB_HLOD_H

#include <stdint.h>

typedef struct {
    float    x, y, z;
    float    radius;   /* bounding radius */
    uint32_t id;
} wb_hlod_object;

/* Group objects whose surfaces are within `cluster_radius`. Writes a cluster id
 * per object into `cluster_of` (n entries) and returns the cluster count. */
uint32_t wb_hlod_cluster(const wb_hlod_object *objs, uint32_t n,
                         float cluster_radius, uint32_t *cluster_of);

#endif /* WB_HLOD_H */
