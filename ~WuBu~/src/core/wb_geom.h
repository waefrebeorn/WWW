/*
 * wb_geom.h -- geometry optimization (LP-10).
 *
 * Vertex-cache optimization a la Forsyth: reorder a triangle index buffer so a
 * fixed-size post-transform vertex cache misses less. The GPU re-shaders a
 * vertex only on a cache miss, so the reordered buffer is a direct bandwidth
 * saving on every frame -- no asset change, no visual change.
 *
 * Deterministic: the same input always yields the same output order.
 *
 * C11, no deps.
 */
#ifndef WB_GEOM_H
#define WB_GEOM_H

#include <stdint.h>

/* Reorder `indices` (nindices entries, 3 per triangle) into `out_indices` for a
 * post-transform cache of `cache_size` entries. `out_indices` must hold
 * nindices entries and may alias `indices`. `nverts` is the vertex count.
 * Returns the number of triangles written, or 0 on bad input. */
uint32_t wb_geom_optimize_vertex_cache(const uint32_t *indices, uint32_t nindices,
                                       uint32_t nverts, int cache_size,
                                       uint32_t *out_indices);

/* Simulate a FIFO post-transform cache of `cache_size` entries over the index
 * buffer and return the number of vertex cache misses. */
uint32_t wb_geom_cache_misses(const uint32_t *indices, uint32_t nindices, int cache_size);

#endif /* WB_GEOM_H */
