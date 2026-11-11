/*
 * wb_heightfield.c -- a terrain heightfield over a tile payload (LP-43 stone 1).
 * See wb_heightfield.h.
 */
#include "wb_heightfield.h"

#include <math.h>
#include <stdlib.h>

static float wb_heightfield_bilinear(const int16_t *h, uint32_t res,
                                     float u, float v) {
    if (!h || res < 2) return 0.0f;
    if (u < 0.0f) u = 0.0f;
    if (u > 1.0f) u = 1.0f;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;

    float x = u * (float)(res - 1);
    float y = v * (float)(res - 1);
    uint32_t x0 = (uint32_t)x, y0 = (uint32_t)y;
    uint32_t x1 = x0 + 1 < res ? x0 + 1 : x0;
    uint32_t y1 = y0 + 1 < res ? y0 + 1 : y0;
    float fx = x - (float)x0, fy = y - (float)y0;

    float h00 = (float)h[y0 * res + x0];
    float h10 = (float)h[y0 * res + x1];
    float h01 = (float)h[y1 * res + x0];
    float h11 = (float)h[y1 * res + x1];
    float a = h00 + (h10 - h00) * fx;
    float b = h01 + (h11 - h01) * fx;
    return a + (b - a) * fy;
}

void wb_heightfield_init(wb_heightfield *hf, uint32_t res, const int16_t *h) {
    if (!hf) return;
    hf->res = res;
    hf->h = h;
}

uint32_t wb_heightfield_res(const wb_heightfield *hf) {
    return hf ? hf->res : 0;
}

int wb_heightfield_valid(const wb_heightfield *hf) {
    if (!hf || !hf->h) return 0;
    return hf->res >= 3 && (hf->res & 1u) != 0;
}

uint32_t wb_heightfield_max_level(const wb_heightfield *hf) {
    if (!wb_heightfield_valid(hf)) return 0;
    uint32_t n = 0;
    while ((1u << (n + 1)) <= hf->res - 1) n++;
    return n;
}

uint32_t wb_heightfield_level_res(const wb_heightfield *hf, uint32_t level) {
    if (!wb_heightfield_valid(hf)) return 0;
    if (level > wb_heightfield_max_level(hf)) level = wb_heightfield_max_level(hf);
    return ((hf->res - 1) >> level) + 1;
}

float wb_heightfield_sample(const wb_heightfield *hf, float u, float v) {
    if (!wb_heightfield_valid(hf)) return 0.0f;
    return wb_heightfield_bilinear(hf->h, hf->res, u, v);
}

void wb_heightfield_normal(const wb_heightfield *hf, float u, float v,
                           float *nx, float *ny, float *nz) {
    if (nx) *nx = 0.0f;
    if (ny) *ny = 0.0f;
    if (nz) *nz = 1.0f;
    if (!wb_heightfield_valid(hf)) return;

    float e = 1.0f / (float)(hf->res - 1);   /* one sample step */
    float du = wb_heightfield_sample(hf, u + e, v) -
               wb_heightfield_sample(hf, u - e, v);
    float dv = wb_heightfield_sample(hf, u, v + e) -
               wb_heightfield_sample(hf, u, v - e);
    float gx = du / (2.0f * e);
    float gy = dv / (2.0f * e);
    float len = sqrtf(gx * gx + gy * gy + 1.0f);
    if (len > 0.0f) {
        if (nx) *nx = -gx / len;
        if (ny) *ny = -gy / len;
        if (nz) *nz = 1.0f / len;
    }
}

/* Build mip `level` by decimating the full field: every 2^level-th sample, which
 * is what keeps both endpoints and makes a linear field exact at every level. */
static void wb_heightfield_build_level(const wb_heightfield *hf, uint32_t level,
                                       int16_t *out) {
    uint32_t cr = wb_heightfield_level_res(hf, level);
    uint32_t step = 1u << level;
    for (uint32_t j = 0; j < cr; j++)
        for (uint32_t i = 0; i < cr; i++)
            out[j * cr + i] = hf->h[(j * step) * hf->res + (i * step)];
}

float wb_heightfield_level_error(const wb_heightfield *hf, uint32_t level) {
    if (!wb_heightfield_valid(hf)) return 0.0f;
    uint32_t max = wb_heightfield_max_level(hf);
    if (level > max) level = max;
    if (level == 0) return 0.0f;             /* the full field is exact */

    uint32_t cr = wb_heightfield_level_res(hf, level);
    int16_t *coarse = (int16_t *)malloc((size_t)cr * cr * sizeof(int16_t));
    if (!coarse) return 0.0f;
    wb_heightfield_build_level(hf, level, coarse);

    float worst = 0.0f;
    for (uint32_t j = 0; j < hf->res; j++) {
        for (uint32_t i = 0; i < hf->res; i++) {
            /* Both grids span the same normalized [0,1] range; only the sample
             * count differs. */
            float u = (float)i / (float)(hf->res - 1);
            float v = (float)j / (float)(hf->res - 1);
            float up = wb_heightfield_bilinear(coarse, cr, u, v);
            float d = (float)hf->h[j * hf->res + i] - up;
            if (d < 0.0f) d = -d;
            if (d > worst) worst = d;
        }
    }
    free(coarse);
    return worst;
}

uint32_t wb_heightfield_pick_level(const wb_heightfield *hf, float tolerance) {
    if (!wb_heightfield_valid(hf)) return 0;
    uint32_t best = 0;
    uint32_t max = wb_heightfield_max_level(hf);
    for (uint32_t l = 0; l <= max; l++)
        if (wb_heightfield_level_error(hf, l) <= tolerance) best = l;
    return best;
}

int32_t wb_heightfield_min(const wb_heightfield *hf) {
    if (!wb_heightfield_valid(hf)) return 0;
    int32_t best = hf->h[0];
    for (uint32_t i = 1; i < hf->res * hf->res; i++)
        if (hf->h[i] < best) best = hf->h[i];
    return best;
}

int32_t wb_heightfield_max(const wb_heightfield *hf) {
    if (!wb_heightfield_valid(hf)) return 0;
    int32_t best = hf->h[0];
    for (uint32_t i = 1; i < hf->res * hf->res; i++)
        if (hf->h[i] > best) best = hf->h[i];
    return best;
}
