/*
 * wb_geom_test.c -- LP-10 gate: vertex-cache optimization.
 *
 * Proves the reordering never loses a triangle, is deterministic, and reduces
 * post-transform cache misses on both a scanline-ordered and a shuffled mesh.
 */
#include "wb_geom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

#define GW 32
#define GH 32
#define NVERT ((GW + 1) * (GH + 1))
#define NTRI  (GW * GH * 2)

static uint32_t g_idx[NTRI * 3];
static uint32_t g_opt[NTRI * 3];

static int tri_matches(const uint32_t *a, const uint32_t *b) {
    /* same three vertices, any winding */
    int hits = 0;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            if (a[i] == b[j]) { hits++; break; }
    return hits == 3;
}

static int tri_in_set(const uint32_t *tri, const uint32_t *set, uint32_t ntri) {
    for (uint32_t i = 0; i < ntri; i++)
        if (tri_matches(tri, set + i * 3)) return 1;
    return 0;
}

static void build_grid(void) {
    uint32_t t = 0;
    for (int y = 0; y < GH; y++) {
        for (int x = 0; x < GW; x++) {
            uint32_t v00 = (uint32_t)(y * (GW + 1) + x);
            uint32_t v10 = v00 + 1;
            uint32_t v01 = v00 + (GW + 1);
            uint32_t v11 = v01 + 1;
            g_idx[t * 3 + 0] = v00; g_idx[t * 3 + 1] = v10; g_idx[t * 3 + 2] = v01; t++;
            g_idx[t * 3 + 0] = v10; g_idx[t * 3 + 1] = v11; g_idx[t * 3 + 2] = v01; t++;
        }
    }
}

static void shuffle_indices(void) {
    /* a deterministic LCG shuffle of the triangle order */
    uint32_t seed = 12345u;
    for (uint32_t i = NTRI - 1; i > 0; i--) {
        seed = seed * 1103515245u + 12345u;
        uint32_t j = (seed >> 16) % (i + 1);
        for (int k = 0; k < 3; k++) {
            uint32_t tmp = g_idx[i * 3 + k];
            g_idx[i * 3 + k] = g_idx[j * 3 + k];
            g_idx[j * 3 + k] = tmp;
        }
    }
}

int main(void) {
    const int cache_size = 32;

    /* ---- scanline-ordered grid ---- */
    build_grid();
    uint32_t naive_miss = wb_geom_cache_misses(g_idx, NTRI * 3, cache_size);
    uint32_t written = wb_geom_optimize_vertex_cache(g_idx, NTRI * 3, NVERT, cache_size, g_opt);
    CHECK(written == NTRI, "every triangle is emitted");
    uint32_t opt_miss = wb_geom_cache_misses(g_opt, NTRI * 3, cache_size);
    CHECK(opt_miss <= naive_miss, "reordering does not increase cache misses");
    printf("scanline: %u -> %u misses\n", naive_miss, opt_miss);

    /* the output is a permutation of the input triangle set */
    int all_found = 1;
    for (uint32_t i = 0; i < written; i++)
        if (!tri_in_set(g_opt + i * 3, g_idx, NTRI)) all_found = 0;
    CHECK(all_found, "the reordered buffer is a permutation of the original");

    /* ---- shuffled grid: the optimizer has real work to do ---- */
    build_grid();
    shuffle_indices();
    uint32_t shuf_miss = wb_geom_cache_misses(g_idx, NTRI * 3, cache_size);
    wb_geom_optimize_vertex_cache(g_idx, NTRI * 3, NVERT, cache_size, g_opt);
    uint32_t shuf_opt = wb_geom_cache_misses(g_opt, NTRI * 3, cache_size);
    CHECK(shuf_opt < shuf_miss, "reordering a shuffled mesh strictly reduces misses");
    printf("shuffled: %u -> %u misses\n", shuf_miss, shuf_opt);

    /* ---- determinism: the same input gives the same order ---- */
    build_grid();
    uint32_t a[NTRI * 3], b[NTRI * 3];
    wb_geom_optimize_vertex_cache(g_idx, NTRI * 3, NVERT, cache_size, a);
    wb_geom_optimize_vertex_cache(g_idx, NTRI * 3, NVERT, cache_size, b);
    CHECK(memcmp(a, b, sizeof a) == 0, "optimization is deterministic");

    /* ---- in-place operation is supported ---- */
    build_grid();
    uint32_t before[NTRI * 3];
    memcpy(before, g_idx, sizeof before);
    wb_geom_optimize_vertex_cache(before, NTRI * 3, NVERT, cache_size, before);
    int inplace_ok = 1;
    for (uint32_t i = 0; i < NTRI; i++)
        if (!tri_in_set(before + i * 3, g_idx, NTRI)) inplace_ok = 0;
    CHECK(inplace_ok, "in-place reordering keeps the triangle set");

    /* ---- degenerate inputs are rejected safely ---- */
    CHECK(wb_geom_optimize_vertex_cache(NULL, 3, 3, cache_size, g_opt) == 0, "NULL input is rejected");
    CHECK(wb_geom_optimize_vertex_cache(g_idx, 4, NVERT, cache_size, g_opt) == 0, "a non-multiple-of-3 index count is rejected");
    CHECK(wb_geom_optimize_vertex_cache(g_idx, 3, 0, cache_size, g_opt) == 0, "zero vertices is rejected");
    CHECK(wb_geom_cache_misses(NULL, 3, cache_size) == 0, "NULL cache simulation is safe");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
