/*
 * wb_meshlet.h -- meshlet partitioning (LP-10 stone 2).
 *
 * A meshlet is a small, self-contained cluster of triangles (typically <= 64
 * vertices and <= 128 triangles) whose vertices are shared within the cluster.
 * Mesh shaders and cluster culling consume meshlets directly; even without a
 * mesh pipeline, a meshlet partition bounds the working set of a single
 * geometry task and makes per-cluster culling cheap.
 *
 * The builder is greedy over the triangle order it is given -- feed it a
 * vertex-cache-optimized order (wb_geom_optimize_vertex_cache) for the best
 * reuse. Deterministic: the same input order always yields the same partition.
 *
 * C11, no deps.
 */
#ifndef WB_MESHLET_H
#define WB_MESHLET_H

#include <stdint.h>

typedef struct {
    uint32_t vert_offset;   /* first entry in `verts` for this meshlet */
    uint32_t index_offset;  /* first entry in `indices` for this meshlet */
    uint32_t nverts;
    uint32_t ntris;
} wb_meshlet_header;

typedef struct {
    wb_meshlet_header *headers;
    uint32_t          *verts;     /* concatenated global vertex indices */
    uint32_t          *indices;   /* concatenated local indices (into verts) */
    uint32_t           n_meshlets;
} wb_meshlets;

/* Partition `indices` (nindices entries, 3 per triangle) into meshlets of at
 * most `max_verts` vertices and `max_tris` triangles. `out` is zeroed first;
 * call wb_meshlets_free() when done. Returns 0 on success, -1 on bad input or
 * out of memory. */
int  wb_meshlets_build(const uint32_t *indices, uint32_t nindices,
                       int max_verts, int max_tris, wb_meshlets *out);
void wb_meshlets_free(wb_meshlets *m);

#endif /* WB_MESHLET_H */
