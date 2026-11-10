/*
 * wb_snapshot_test.c -- LP-29 gate: snapshot interpolation.
 *
 * Devil's-advocate cases: the endpoints are exact, the midpoint is correct, the
 * orientation stays unit length, the shortest arc is taken (opposite hemisphere),
 * t outside [0,1] is clamped, and the result is deterministic.
 */
#include "wb_snapshot.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)
#define NEAR(a, b, e) (fabsf((a) - (b)) <= (e))

static wb_snapshot_state g_a = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
static wb_snapshot_state g_b = { 10.0f, 20.0f, 30.0f, 0.0f, 0.0f, -0.70710678f, 0.70710678f };

int main(void) {
    wb_snapshot_state o;

    /* ---- the endpoints are exact ---- */
    wb_snapshot_interp(&g_a, &g_b, 0.0f, &o);
    CHECK(NEAR(o.px, 0.0f, 1e-6f) && NEAR(o.pz, 0.0f, 1e-6f), "t=0 returns the earlier snapshot");
    CHECK(NEAR(o.qw, 1.0f, 1e-6f), "t=0 returns the earlier orientation");

    wb_snapshot_interp(&g_a, &g_b, 1.0f, &o);
    CHECK(NEAR(o.px, 10.0f, 1e-6f) && NEAR(o.py, 20.0f, 1e-6f) && NEAR(o.pz, 30.0f, 1e-6f),
          "t=1 returns the later snapshot");
    CHECK(wb_snapshot_interp_error(&g_a, &g_b, 1.0f, &o) < 1e-6f, "t=1 is unit length");

    /* ---- the midpoint ---- */
    wb_snapshot_interp(&g_a, &g_b, 0.5f, &o);
    CHECK(NEAR(o.px, 5.0f, 1e-6f) && NEAR(o.py, 10.0f, 1e-6f) && NEAR(o.pz, 15.0f, 1e-6f),
          "the position is the exact midpoint");
    CHECK(wb_snapshot_interp_error(&g_a, &g_b, 0.5f, &o) < 1e-6f, "the orientation stays unit length");

    /* ---- the shortest arc: opposite hemispheres are flipped ---- */
    wb_snapshot_state c = { 0, 0, 0, 0.0f, 0.0f, 0.70710678f, -0.70710678f };  /* same rotation as g_b */
    wb_snapshot_interp(&g_a, &c, 1.0f, &o);
    /* the shortest arc negates the opposite-hemisphere quaternion; -q is the
     * same rotation as q, so this is the correct endpoint */
    CHECK(NEAR(o.qz, -0.70710678f, 1e-5f) && NEAR(o.qw, 0.70710678f, 1e-5f),
          "an opposite-hemisphere quaternion takes the short arc");
    CHECK(wb_snapshot_interp_error(&g_a, &c, 1.0f, &o) < 1e-6f, "the flipped path stays unit length");

    /* ---- t outside [0,1] is clamped ---- */
    wb_snapshot_interp(&g_a, &g_b, -5.0f, &o);
    CHECK(NEAR(o.px, 0.0f, 1e-6f), "a negative t clamps to the earlier snapshot");
    wb_snapshot_interp(&g_a, &g_b, 5.0f, &o);
    CHECK(NEAR(o.px, 10.0f, 1e-6f), "an oversized t clamps to the later snapshot");

    /* ---- the whole sweep stays unit length ---- */
    int unit = 1;
    for (int i = 0; i <= 20; i++) {
        wb_snapshot_interp(&g_a, &g_b, (float)i / 20.0f, &o);
        if (wb_snapshot_interp_error(&g_a, &g_b, (float)i / 20.0f, &o) > 1e-5f) unit = 0;
    }
    CHECK(unit, "the quaternion stays unit length across the whole sweep");

    /* ---- determinism ---- */
    wb_snapshot_state x, y;
    for (int i = 0; i <= 10; i++) {
        float t = (float)i / 10.0f;
        wb_snapshot_interp(&g_a, &g_b, t, &x);
        wb_snapshot_interp(&g_a, &g_b, t, &y);
        CHECK(memcmp(&x, &y, sizeof x) == 0, "interpolation is deterministic");
    }

    /* ---- degenerate input ---- */
    wb_snapshot_interp(NULL, &g_b, 0.5f, &o);
    wb_snapshot_interp(&g_a, NULL, 0.5f, &o);
    wb_snapshot_interp(&g_a, &g_b, 0.5f, NULL);
    CHECK(1, "degenerate calls are safe");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
