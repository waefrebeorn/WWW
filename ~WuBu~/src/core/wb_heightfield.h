/*
 * wb_heightfield.h -- a terrain heightfield over a tile payload (LP-43 stone 1).
 *
 * The quadkey tile store can already hold terrain bytes; there was no layer over
 * them. A tile's payload is a res x res grid of 16-bit heights, and this module
 * is that grid: it samples it, estimates its normals, and decides how much of it
 * a given view actually needs.
 *
 * The resolution must be ODD -- 2^n + 1 is the intended shape -- because that is
 * what makes the mip chain exact: every level halves the span and keeps both
 * endpoints, so level L has (res-1) >> L + 1 samples and no sample is ever
 * invented or dropped at the borders.
 *
 * The LOD rule is error-driven rather than distance-driven. For each level the
 * field is DECIMATED to that level's resolution -- every 2^L-th sample, which
 * keeps both endpoints -- and reconstructed back to full size by bilinear
 * interpolation; the largest deviation from the original is that level's
 * GEOMETRIC ERROR, in height units. The level a view uses is then the coarsest
 * one whose error is within the caller's tolerance -- so a flat plain or a
 * uniform slope collapses to almost nothing and a jagged ridge does not, at any
 * distance. That is the CDLOD/geomipmap contract: the level is chosen by what the
 * simplification would cost, not by how far away the camera happens to be.
 *
 * C11, no deps beyond libm's sqrtf. Deterministic.
 */
#ifndef WB_HEIGHTFIELD_H
#define WB_HEIGHTFIELD_H

#include <stdint.h>

typedef struct {
    uint32_t       res;   /* the grid resolution, res x res; odd and >= 3 */
    const int16_t *h;     /* res*res samples, row-major, in height units */
} wb_heightfield;

void wb_heightfield_init(wb_heightfield *hf, uint32_t res, const int16_t *h);
uint32_t wb_heightfield_res(const wb_heightfield *hf);

/* 1 if the field is usable: a non-NULL grid and an odd resolution of at least 3. */
int      wb_heightfield_valid(const wb_heightfield *hf);

/* The deepest mip level: log2(res-1). */
uint32_t wb_heightfield_max_level(const wb_heightfield *hf);

/* The resolution of mip `level`: (res-1) >> level, plus one. */
uint32_t wb_heightfield_level_res(const wb_heightfield *hf, uint32_t level);

/* The bilinear height at (u,v) in [0,1]; outside, it clamps to the edge. */
float wb_heightfield_sample(const wb_heightfield *hf, float u, float v);

/* The unit surface normal at (u,v), from central differences. Any output pointer
 * may be NULL. */
void wb_heightfield_normal(const wb_heightfield *hf, float u, float v,
                           float *nx, float *ny, float *nz);

/* The largest deviation, in height units, between the full field and mip `level`
 * reconstructed by bilinear interpolation. Level 0 is exact. */
float wb_heightfield_level_error(const wb_heightfield *hf, uint32_t level);

/* The coarsest level whose error is within `tolerance`, clamped to the deepest
 * level. A tolerance of 0 returns the full field. */
uint32_t wb_heightfield_pick_level(const wb_heightfield *hf, float tolerance);

int32_t wb_heightfield_min(const wb_heightfield *hf);
int32_t wb_heightfield_max(const wb_heightfield *hf);

#endif /* WB_HEIGHTFIELD_H */
