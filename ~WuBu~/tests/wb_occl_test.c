/*
 * wb_occl_test.c -- LP-14 stone 1 gate: screen-space occlusion culling.
 *
 * Devil's advocate cases: full and partial occlusion, nearer and farther
 * occludees, nearest-wins rasterization in both orders, clipping, degenerate
 * rectangles, and determinism.
 */
#include "wb_occl.h"

#include <stdio.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

#define FAR 1.0f

int main(void) {
    wb_occl o;
    CHECK(wb_occl_init(&o, 64, 64) == 0, "buffer created");

    /* ---- an empty buffer occludes nothing ---- */
    wb_occl_clear(&o, FAR);
    CHECK(wb_occl_visible(&o, 0, 0, 10, 10, 0.5f) == 1, "nothing occludes an empty buffer");
    CHECK(wb_occl_depth(&o, 0, 0, 10, 10, FAR) == FAR, "the depth is the far plane");

    /* ---- a full-screen occluder hides what is behind it ---- */
    wb_occl_clear(&o, FAR);
    wb_occl_rasterize(&o, 0, 0, 64, 64, 0.3f);
    CHECK(wb_occl_visible(&o, 10, 10, 10, 10, 0.5f) == 0, "a box behind the occluder is hidden");
    CHECK(wb_occl_visible(&o, 10, 10, 10, 10, 0.1f) == 1, "a box in front of the occluder is visible");
    CHECK(wb_occl_depth(&o, 10, 10, 10, 10, FAR) == 0.3f, "the nearest depth is reported");

    /* ---- partial coverage leaves the box visible ---- */
    wb_occl_clear(&o, FAR);
    wb_occl_rasterize(&o, 0, 0, 10, 10, 0.2f);
    CHECK(wb_occl_visible(&o, 20, 20, 10, 10, 0.5f) == 1, "an uncovered box is visible");
    CHECK(wb_occl_visible(&o, 5, 5, 10, 10, 0.5f) == 1, "a partially covered box is visible");
    CHECK(wb_occl_visible(&o, 0, 0, 10, 10, 0.5f) == 0, "a fully covered box is hidden");

    /* ---- nearest wins, in both rasterization orders ---- */
    wb_occl_clear(&o, FAR);
    wb_occl_rasterize(&o, 0, 0, 10, 10, 0.9f);
    wb_occl_rasterize(&o, 0, 0, 10, 10, 0.1f);
    CHECK(wb_occl_depth(&o, 0, 0, 10, 10, FAR) == 0.1f, "a nearer occluder wins");

    wb_occl_clear(&o, FAR);
    wb_occl_rasterize(&o, 0, 0, 10, 10, 0.1f);
    wb_occl_rasterize(&o, 0, 0, 10, 10, 0.9f);
    CHECK(wb_occl_depth(&o, 0, 0, 10, 10, FAR) == 0.1f, "a farther occluder does not overwrite");

    /* ---- clipping: rectangles outside the buffer are handled ---- */
    wb_occl_clear(&o, FAR);
    wb_occl_rasterize(&o, -100, -100, 150, 150, 0.1f);   /* partially on-screen: covers [0,50)^2 */
    CHECK(wb_occl_visible(&o, 0, 0, 5, 5, 0.5f) == 0, "an off-screen occluder clips and still occludes");
    CHECK(wb_occl_visible(&o, 200, 200, 10, 10, 0.5f) == 0, "a fully off-screen box is not visible");
    CHECK(wb_occl_visible(&o, 60, 60, 100, 100, 0.5f) == 1, "a box extending past the edge is handled");

    /* ---- degenerate rectangles ---- */
    CHECK(wb_occl_visible(&o, 10, 10, 0, 10, 0.5f) == 0, "a zero-width rect is not visible");
    CHECK(wb_occl_visible(&o, 10, 10, 10, 0, 0.5f) == 0, "a zero-height rect is not visible");
    CHECK(wb_occl_visible(&o, -50, -50, 10, 10, 0.5f) == 0, "a fully off-screen rect is not visible");
    float before = wb_occl_depth(&o, 10, 10, 10, 10, FAR);
    wb_occl_rasterize(&o, 10, 10, 0, 0, 0.1f);          /* must not corrupt the buffer */
    CHECK(wb_occl_depth(&o, 10, 10, 10, 10, FAR) == before, "a degenerate occluder changes nothing");

    /* ---- determinism: rebuilding gives the same visibility ---- */
    int a = 1, b = 1;
    for (int pass = 0; pass < 2; pass++) {
        wb_occl_clear(&o, FAR);
        wb_occl_rasterize(&o, 0, 0, 32, 32, 0.25f);
        wb_occl_rasterize(&o, 16, 16, 32, 32, 0.4f);
        int v = wb_occl_visible(&o, 20, 20, 8, 8, 0.5f);
        if (pass == 0) a = v; else b = v;
    }
    CHECK(a == b, "culling is deterministic");

    wb_occl_free(&o);
    CHECK(wb_occl_init(NULL, 10, 10) == -1, "a NULL buffer is rejected");
    CHECK(wb_occl_init(&o, 0, 10) == -1, "a zero-size buffer is rejected");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
