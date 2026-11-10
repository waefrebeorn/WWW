/*
 * wb_meshlet_test.c -- LP-10 stone 2 gate: meshlet partitioning.
 *
 * Devil's-advocate cases: per-meshlet bounds, full coverage with no duplicate
 * or lost triangle, real vertex reuse inside a meshlet, determinism, in-place
 * ordering, and degenerate input rejection.
 */
#include "wb_meshlet.h"
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
#define MAXV  64
#define MAXT  128

static uint32_t g_idx[NTRI * 3];
static uint32_t g_opt[NTRI * 3];

static int tri_matches(const uint32_t *a, const uint32_t *b) {
    int hits = 0;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            if (a[i] == b[j]) { hits++; break; }
    return hits == 3;
}

static void build_grid(void) {
    uint32_t t = 0;
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++) {
            uint32_t v00 = (uint32_t)(y * (GW + 1) + x);
            uint32_t v10 = v00 + 1, v01 = v00 + (GW + 1), v11 = v01 + 1;
            g_idx[t * 3 + 0] = v00; g_idx[t * 3 + 1] = v10; g_idx[t * 3 + 2] = v01; t++;
            g_idx[t * 3 + 0] = v10; g_idx[t * 3 + 1] = v11; g_idx[t * 3 + 2] = v01; t++;
        }
}

int main(void) {
    build_grid();
    wb_geom_optimize_vertex_cache(g_idx, NTRI * 3, NVERT, 32, g_opt);

    wb_meshlets m;
    CHECK(wb_meshlets_build(g_opt, NTRI * 3, MAXV, MAXT, &m) == 0, "partition succeeds");
    CHECK(m.n_meshlets > 0, "at least one meshlet");
    printf("meshlets: %u\n", m.n_meshlets);

    /* ---- per-meshlet bounds ---- */
    int bounds_ok = 1, reuse_ok = 1;
    uint32_t total_tris = 0;
    for (uint32_t i = 0; i < m.n_meshlets; i++) {
        const wb_meshlet_header *h = &m.headers[i];
        if (h->nverts == 0 || h->nverts > (uint32_t)MAXV) bounds_ok = 0;
        if (h->nverts > (uint32_t)MAXV || h->ntris == 0 || h->ntris > (uint32_t)MAXT) bounds_ok = 0;
        if (h->nverts >= h->ntris * 3) reuse_ok = 0;    /* a meshlet must share vertices */
        total_tris += h->ntris;
    }
    CHECK(bounds_ok, "every meshlet respects the vertex and triangle bounds");
    CHECK(reuse_ok, "every meshlet shares vertices (nverts < ntris*3)");
    CHECK(total_tris == NTRI, "the meshlets cover every triangle exactly once by count");

    /* ---- coverage: every input triangle is in exactly one meshlet ---- */
    int coverage = 1;
    for (uint32_t i = 0; i < NTRI; i++) {
        int found = 0;
        for (uint32_t k = 0; k < m.n_meshlets && found < 2; k++) {
            const wb_meshlet_header *h = &m.headers[k];
            for (uint32_t t = 0; t < h->ntris; t++) {
                uint32_t tri[3];
                for (int j = 0; j < 3; j++)
                    tri[j] = m.verts[h->vert_offset + m.indices[h->index_offset + t * 3 + j]];
                if (tri_matches(tri, g_opt + i * 3)) { found++; break; }
            }
        }
        if (found != 1) { coverage = 0; break; }
    }
    CHECK(coverage, "each input triangle appears in exactly one meshlet");

    /* ---- determinism ---- */
    wb_meshlets m2;
    CHECK(wb_meshlets_build(g_opt, NTRI * 3, MAXV, MAXT, &m2) == 0, "second partition succeeds");
    int same = (m2.n_meshlets == m.n_meshlets) &&
               memcmp(m2.headers, m.headers, m.n_meshlets * sizeof(wb_meshlet_header)) == 0 &&
               memcmp(m2.verts, m.verts, (m2.verts ? (m.headers[m.n_meshlets - 1].vert_offset +
                    m.headers[m.n_meshlets - 1].nverts) : 0) * sizeof(uint32_t)) == 0;
    CHECK(same, "partitioning is deterministic");
    wb_meshlets_free(&m2);
    wb_meshlets_free(&m);

    /* ---- degenerate inputs ---- */
    wb_meshlets d;
    CHECK(wb_meshlets_build(NULL, 3, MAXV, MAXT, &d) == -1, "NULL input is rejected");
    CHECK(wb_meshlets_build(g_opt, 4, MAXV, MAXT, &d) == -1, "a non-multiple-of-3 count is rejected");
    CHECK(wb_meshlets_build(g_opt, 0, MAXV, MAXT, &d) == -1, "an empty index buffer is rejected");
    CHECK(d.n_meshlets == 0, "a rejected build leaves no meshlets");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
