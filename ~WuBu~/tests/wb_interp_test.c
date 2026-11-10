/*
 * wb_interp_test.c -- LP-04 gate: fixed-timestep render interpolation.
 *
 * Proves the interpolation is exact at the endpoints, linear in between,
 * clamped to [0,1], deterministic, and driven correctly by the fixed-step
 * accumulator's remainder (the alpha the renderer would use).
 */
#include "wb_interp.h"
#include "wb_det.h"

#include <stdio.h>
#include <math.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)
#define NEAR(a, b, eps) (fabsf((a) - (b)) <= (eps))

int main(void) {
    float out[3];

    /* endpoints are exact */
    wb_interp_lerp3((const float[]){0, 0, 0}, (const float[]){10, 20, 30}, 0.0f, out);
    CHECK(out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f, "alpha 0 returns the previous state");
    wb_interp_lerp3((const float[]){0, 0, 0}, (const float[]){10, 20, 30}, 1.0f, out);
    CHECK(out[0] == 10.0f && out[1] == 20.0f && out[2] == 30.0f, "alpha 1 returns the current state");

    /* linear in between */
    wb_interp_lerp3((const float[]){0, 0, 0}, (const float[]){10, 20, 30}, 0.25f, out);
    CHECK(NEAR(out[0], 2.5f, 1e-6f) && NEAR(out[1], 5.0f, 1e-6f) && NEAR(out[2], 7.5f, 1e-6f),
          "alpha 0.25 is a quarter of the way");
    wb_interp_lerp3((const float[]){-5, 5, 0}, (const float[]){5, -5, 10}, 0.5f, out);
    CHECK(NEAR(out[0], 0.0f, 1e-6f) && NEAR(out[1], 0.0f, 1e-6f) && NEAR(out[2], 5.0f, 1e-6f),
          "interpolates across sign changes");

    /* alpha from the accumulator remainder, clamped to [0,1] */
    CHECK(wb_interp_alpha(0.0, 1.0 / 64.0) == 0.0f, "zero remainder -> alpha 0");
    CHECK(NEAR(wb_interp_alpha(1.0 / 128.0, 1.0 / 64.0), 0.5f, 1e-6f), "half a step -> alpha 0.5");
    CHECK(wb_interp_alpha(1.0 / 64.0, 1.0 / 64.0) == 1.0f, "a full step -> alpha 1");
    CHECK(wb_interp_alpha(-1.0, 1.0 / 64.0) == 0.0f, "negative remainder clamps to 0");
    CHECK(wb_interp_alpha(2.0, 1.0 / 64.0) == 1.0f, "oversized remainder clamps to 1");
    CHECK(wb_interp_alpha(1.0, 0.0) == 0.0f, "zero step is safe");

    /* integration: the alpha the renderer would use comes from the accumulator */
    wb_det_fixed acc;
    wb_det_fixed_init(&acc, 1.0 / 64.0);
    int steps = wb_det_fixed_advance(&acc, 1.0 / 60.0);   /* one 60 Hz frame */
    float alpha = wb_interp_alpha(wb_det_fixed_remainder(&acc), 1.0 / 64.0);
    CHECK(steps >= 0 && steps <= 1, "a 60Hz frame consumes at most one 64Hz step");
    CHECK(alpha >= 0.0f && alpha < 1.0f, "render alpha stays in [0,1) after a frame");
    CHECK(NEAR(alpha, (float)(wb_det_fixed_remainder(&acc) * 64.0), 1e-6f),
          "alpha equals remainder/step");

    /* deterministic: identical inputs give identical outputs */
    float a1[3], a2[3];
    wb_interp_lerp3((const float[]){1, 2, 3}, (const float[]){4, 5, 6}, 0.37f, a1);
    wb_interp_lerp3((const float[]){1, 2, 3}, (const float[]){4, 5, 6}, 0.37f, a2);
    CHECK(a1[0] == a2[0] && a1[1] == a2[1] && a1[2] == a2[2], "interpolation is deterministic");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
