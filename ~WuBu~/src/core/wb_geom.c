/*
 * wb_geom.c -- geometry optimization (LP-10). See wb_geom.h.
 */
#include "wb_geom.h"

#include <stdlib.h>
#include <string.h>

#define WB_GEOM_MAX_CACHE 64
#define WB_GEOM_MAX_VERTS 262144

/* ---- cache simulation ---------------------------------------------------- */

uint32_t wb_geom_cache_misses(const uint32_t *indices, uint32_t nindices, int cache_size) {
    if (!indices || nindices == 0 || cache_size < 1) return 0;
    if (cache_size > WB_GEOM_MAX_CACHE) cache_size = WB_GEOM_MAX_CACHE;
    int cache[WB_GEOM_MAX_CACHE];
    int cache_n = 0;
    uint32_t misses = 0;

    for (uint32_t i = 0; i < nindices; i++) {
        uint32_t v = indices[i];
        int found = -1;
        for (int k = 0; k < cache_n; k++)
            if ((uint32_t)cache[k] == v) { found = k; break; }
        if (found >= 0) {
            /* hit: move to the front (LRU) */
            for (int k = found; k > 0; k--) cache[k] = cache[k - 1];
            cache[0] = (int)v;
        } else {
            misses++;
            for (int k = cache_n; k > 0; k--) cache[k] = cache[k - 1];
            cache[0] = (int)v;
            if (cache_n < cache_size) cache_n++;
        }
    }
    return misses;
}

/* ---- Forsyth reordering --------------------------------------------------- */

static float wb_geom_vertex_score(int v, const int *cache, int cache_n, int cache_size, int valence) {
    /* in-cache: the more recently used, the higher the score */
    for (int i = 0; i < cache_n; i++)
        if (cache[i] == v) return 1.0f + (float)(cache_size - i) / (float)(cache_size + 1);
    /* out of cache: prefer vertices that still have many triangles left */
    return -0.5f * (float)valence;
}

uint32_t wb_geom_optimize_vertex_cache(const uint32_t *indices, uint32_t nindices,
                                       uint32_t nverts, int cache_size,
                                       uint32_t *out_indices) {
    if (!indices || !out_indices || nindices == 0 || nindices % 3 != 0) return 0;
    if (cache_size < 1) cache_size = 32;
    if (cache_size > WB_GEOM_MAX_CACHE) cache_size = WB_GEOM_MAX_CACHE;
    if (nverts == 0 || nverts > WB_GEOM_MAX_VERTS) return 0;

    uint32_t ntri = nindices / 3;
    int *valence = (int *)calloc(nverts, sizeof(int));
    unsigned char *emitted = (unsigned char *)calloc(ntri, 1);
    if (!valence || !emitted) { free(valence); free(emitted); return 0; }

    for (uint32_t i = 0; i < nindices; i++) valence[indices[i]]++;

    int cache[WB_GEOM_MAX_CACHE];
    int cache_n = 0;
    uint32_t written = 0;

    for (uint32_t t = 0; t < ntri; t++) {
        /* pick the unemitted triangle with the best vertex score */
        uint32_t best = (uint32_t)-1;
        float best_score = -1e30f;
        for (uint32_t k = 0; k < ntri; k++) {
            if (emitted[k]) continue;
            const uint32_t *tri = indices + k * 3;
            float s = 0.0f;
            for (int j = 0; j < 3; j++) {
                float vs = wb_geom_vertex_score((int)tri[j], cache, cache_n, cache_size, valence[tri[j]]);
                if (vs > s) s = vs;
            }
            if (s > best_score) { best_score = s; best = k; }
        }
        if (best == (uint32_t)-1) break;

        emitted[best] = 1;
        const uint32_t *tri = indices + best * 3;
        out_indices[written * 3 + 0] = tri[0];
        out_indices[written * 3 + 1] = tri[1];
        out_indices[written * 3 + 2] = tri[2];
        written++;

        /* update the cache (most recent first) and the valences */
        for (int j = 0; j < 3; j++) {
            int v = (int)tri[j];
            int at = -1;
            for (int k = 0; k < cache_n; k++) if (cache[k] == v) { at = k; break; }
            if (at >= 0) {
                for (int k = at; k > 0; k--) cache[k] = cache[k - 1];
                cache[0] = v;
            } else {
                for (int k = cache_n; k > 0; k--) cache[k] = cache[k - 1];
                cache[0] = v;
                if (cache_n < cache_size) cache_n++;
            }
            if (valence[v] > 0) valence[v]--;
        }
    }

    free(valence);
    free(emitted);
    return written;
}
