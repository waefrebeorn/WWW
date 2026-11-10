/*
 * wb_hlod.c -- hierarchical LOD clustering (LP-15 stone 2). See wb_hlod.h.
 */
#include "wb_hlod.h"

#include <math.h>
#include <stdlib.h>

/* ---- tiny union-find ----------------------------------------------------- */

static int uf_find(int *parent, int x) {
    while (parent[x] != x) {
        parent[x] = parent[parent[x]];   /* path halving */
        x = parent[x];
    }
    return x;
}

static void uf_union(int *parent, int a, int b) {
    int ra = uf_find(parent, a), rb = uf_find(parent, b);
    if (ra != rb) parent[rb] = ra;
}

uint32_t wb_hlod_cluster(const wb_hlod_object *objs, uint32_t n,
                         float cluster_radius, uint32_t *cluster_of) {
    if (!objs || !cluster_of || n == 0) return 0;
    if (cluster_radius < 0.0f) cluster_radius = 0.0f;

    int *parent = (int *)malloc((size_t)n * sizeof(int));
    if (!parent) return 0;
    for (uint32_t i = 0; i < n; i++) parent[i] = (int)i;

    /* merge objects whose surfaces are within the radius */
    for (uint32_t i = 0; i < n; i++) {
        for (uint32_t j = i + 1; j < n; j++) {
            float dx = objs[i].x - objs[j].x;
            float dy = objs[i].y - objs[j].y;
            float dz = objs[i].z - objs[j].z;
            float dist = sqrtf(dx * dx + dy * dy + dz * dz);
            if (dist - objs[i].radius - objs[j].radius <= cluster_radius)
                uf_union(parent, (int)i, (int)j);
        }
    }

    /* assign cluster ids in order of first appearance */
    int *root_to_id = (int *)malloc((size_t)n * sizeof(int));
    if (!root_to_id) { free(parent); return 0; }
    for (uint32_t i = 0; i < n; i++) root_to_id[i] = -1;

    uint32_t count = 0;
    for (uint32_t i = 0; i < n; i++) {
        int r = uf_find(parent, (int)i);
        if (root_to_id[r] < 0) root_to_id[r] = (int)count++;
        cluster_of[i] = (uint32_t)root_to_id[r];
    }

    free(parent);
    free(root_to_id);
    return count;
}
