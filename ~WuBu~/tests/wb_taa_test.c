/*
 * wb_taa_test.c -- LP-18 stone 1 gate: temporal accumulation.
 *
 * Devil's-advocate cases: convergence on a static frame, ghost rejection by the
 * history clip, monotone convergence after a step, reprojection with a motion
 * vector, alpha=0 (no accumulation), and determinism.
 */
#include "wb_taa.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

#define W 8
#define H 8

static float g_src[W * H * 3];
static float g_motion[W * H * 2];
static float g_dst[W * H * 3];

static void fill(float v) {
    for (int i = 0; i < W * H * 3; i++) g_src[i] = v;
}
static void zero_motion(void) {
    memset(g_motion, 0, sizeof g_motion);
}
static int all_equal(const float *buf, float v) {
    for (int i = 0; i < W * H * 3; i++) if (buf[i] != v) return 0;
    return 1;
}

int main(void) {
    wb_taa t;
    CHECK(wb_taa_init(&t, W, H) == 0, "accumulator created");
    zero_motion();

    /* ---- a static frame resolves to itself ---- */
    fill(0.5f);
    wb_taa_resolve(&t, g_src, W, H, g_motion, 0.9f, g_dst);
    CHECK(all_equal(g_dst, 0.5f), "a static frame resolves to its own value");

    /* ---- the history clip rejects a stale ghost ---- */
    wb_taa_reset(&t);
    fill(0.0f);
    wb_taa_resolve(&t, g_src, W, H, g_motion, 0.9f, g_dst);
    CHECK(all_equal(g_dst, 0.0f), "the first frame resolves to the input");
    fill(0.5f);
    wb_taa_resolve(&t, g_src, W, H, g_motion, 0.9f, g_dst);
    CHECK(all_equal(g_dst, 0.5f), "a stale history sample is clipped, not ghosted");

    /* ---- convergence after a step ---- */
    wb_taa_reset(&t);
    fill(0.5f);
    for (int i = 0; i < 4; i++) wb_taa_resolve(&t, g_src, W, H, g_motion, 0.9f, g_dst);
    float prev = g_dst[0];
    fill(1.0f);
    int monotone = 1;
    for (int i = 0; i < 20; i++) {
        wb_taa_resolve(&t, g_src, W, H, g_motion, 0.9f, g_dst);
        if (g_dst[0] < prev - 1e-6f) monotone = 0;
        prev = g_dst[0];
    }
    CHECK(monotone && prev > 0.99f, "the output converges monotonically to the new value");

    /* ---- reprojection with a motion vector ---- */
    wb_taa_reset(&t);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            for (int c = 0; c < 3; c++)
                g_src[(y * W + x) * 3 + c] = (float)x / (float)(W - 1);
    wb_taa_resolve(&t, g_src, W, H, g_motion, 0.9f, g_dst);   /* history = the pattern */

    /* the pattern shifts +1 in x; the motion vector points back into history */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int sx = x - 1; if (sx < 0) sx = 0;
            for (int c = 0; c < 3; c++)
                g_src[(y * W + x) * 3 + c] = (float)sx / (float)(W - 1);
            g_motion[(y * W + x) * 2 + 0] = -1.0f;
            g_motion[(y * W + x) * 2 + 1] = 0.0f;
        }
    wb_taa_resolve(&t, g_src, W, H, g_motion, 0.9f, g_dst);
    int reprojected = 1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float expect = (float)(x - 1 < 0 ? 0 : x - 1) / (float)(W - 1);
            if (g_dst[(y * W + x) * 3] != expect) reprojected = 0;
        }
    CHECK(reprojected, "the history is reprojected by the motion vector");

    /* ---- alpha = 0 disables accumulation ---- */
    wb_taa_reset(&t);
    zero_motion();
    fill(0.2f);
    wb_taa_resolve(&t, g_src, W, H, g_motion, 0.0f, g_dst);
    fill(0.8f);
    wb_taa_resolve(&t, g_src, W, H, g_motion, 0.0f, g_dst);
    CHECK(all_equal(g_dst, 0.8f), "alpha 0 uses only the current frame");

    /* ---- determinism ---- */
    float a[W * H * 3], b[W * H * 3];
    wb_taa ta, tb;
    wb_taa_init(&ta, W, H);
    wb_taa_init(&tb, W, H);
    fill(0.3f);
    wb_taa_resolve(&ta, g_src, W, H, g_motion, 0.9f, a);
    wb_taa_resolve(&tb, g_src, W, H, g_motion, 0.9f, b);
    CHECK(memcmp(a, b, sizeof a) == 0, "accumulation is deterministic");
    wb_taa_free(&ta);
    wb_taa_free(&tb);

    /* ---- degenerate input ---- */
    CHECK(wb_taa_init(NULL, W, H) == -1, "a NULL accumulator is rejected");
    CHECK(wb_taa_init(&t, 0, H) == -1, "a zero-size accumulator is rejected");
    wb_taa_resolve(&t, NULL, W, H, g_motion, 0.9f, g_dst);
    wb_taa_resolve(&t, g_src, W, H, NULL, 0.9f, g_dst);
    wb_taa_resolve(&t, g_src, W, H, g_motion, 0.9f, NULL);
    CHECK(1, "degenerate resolve calls are safe");

    wb_taa_free(&t);
    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
