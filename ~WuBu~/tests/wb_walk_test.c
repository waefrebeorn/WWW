/*
 * wb_walk_test.c -- gate for the procedural walk cycle.
 *
 * The cycle is one double advanced by distance travelled, so the checks are
 * mostly about the INVARIANTS that make it animation rather than a random
 * number: it advances with speed, freezes at zero, wraps instead of drifting,
 * and the two terms a limb needs (swing, lift) are always in range and never
 * both maximal at once.
 */
#include "wb_walk.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } \
} while (0)

int main(void) {
    /* ---- initial state -------------------------------------------------- */
    wb_walk w;
    wb_walk_init(&w, WB_WALK_RATE_PER_METRE);
    CHECK(w.phase == 0.0, "a fresh cycle is at zero");
    CHECK(w.speed == 0.0, "a fresh cycle has no speed");
    CHECK(wb_walk_swing(&w) == 0.0, "a fresh cycle does not swing");
    CHECK(wb_walk_lift(&w) == 0.0, "a fresh cycle does not lift");

    wb_walk nullw;
    memset(&nullw, 0, sizeof nullw);
    CHECK(wb_walk_swing(NULL) == 0.0, "NULL swings zero");
    CHECK(wb_walk_lift(NULL) == 0.0, "NULL lifts zero");
    wb_walk_step(NULL, 1.0, 5.0);
    wb_walk_init(NULL, 1.0);
    CHECK(1, "NULL is harmless");

    /* ---- it advances with distance, not with time ------------------------ */
    /* Ten seconds at 4 m/s is 40 m; at 4.49 rad/m that is 179.6 rad, which is
     * 179.6 / 2*pi = 28.58 cycles. The phase is wrapped, so compare against the
     * wrapped expectation rather than the raw accumulation. */
    wb_walk a;
    wb_walk_init(&a, WB_WALK_RATE_PER_METRE);
    wb_walk_step(&a, 10.0, 4.0);
    double want = fmod(40.0 * WB_WALK_RATE_PER_METRE, 2.0 * 3.14159265358979);
    CHECK(fabs(a.phase - want) < 1e-9, "phase advances by distance travelled");
    CHECK(fabs(a.speed - 4.0) < 1e-12, "the last speed is remembered");

    /* Double the speed, double the distance, double the phase advance. */
    wb_walk b;
    wb_walk_init(&b, WB_WALK_RATE_PER_METRE);
    wb_walk_step(&b, 10.0, 8.0);
    CHECK(fabs(b.phase - fmod(2.0 * 40.0 * WB_WALK_RATE_PER_METRE,
                              2.0 * 3.14159265358979)) < 1e-9,
          "phase is proportional to speed");

    /* ---- zero speed freezes the pose ------------------------------------- */
    wb_walk c;
    wb_walk_init(&c, WB_WALK_RATE_PER_METRE);
    wb_walk_step(&c, 1.0, 3.0);
    double frozen = c.phase;
    for (int i = 0; i < 100; i++) wb_walk_step(&c, 1.0 / 60.0, 0.0);
    CHECK(c.phase == frozen, "a stopped body holds its pose");
    CHECK(c.speed == 0.0, "a stopped body reports zero speed");

    /* ---- negative time cannot rewind the cycle --------------------------- */
    wb_walk d;
    wb_walk_init(&d, WB_WALK_RATE_PER_METRE);
    wb_walk_step(&d, 1.0, 5.0);
    double before = d.phase;
    wb_walk_step(&d, -5.0, 5.0);
    CHECK(d.phase == before, "negative dt is clamped, not applied");

    /* ---- the phase stays bounded over a very long run -------------------- */
    wb_walk e;
    wb_walk_init(&e, WB_WALK_RATE_PER_METRE);
    for (int i = 0; i < 100000; i++) wb_walk_step(&e, 1.0 / 60.0, 20.0);
    CHECK(e.phase >= 0.0 && e.phase < 2.0 * 3.14159265358979,
          "phase wraps instead of growing without bound");
    /* ...and it is still the right value modulo 2*pi. */
    double total = 100000.0 * (1.0 / 60.0) * 20.0 * WB_WALK_RATE_PER_METRE;
    CHECK(fabs(e.phase - fmod(total, 2.0 * 3.14159265358979)) < 1e-6,
          "a long run lands on the same phase as the raw accumulation");

    /* ---- the two terms a limb needs -------------------------------------- */
    /* swing is sin(phase) and lift is its positive part, so:
     *   - both are always within range,
     *   - lift is never negative,
     *   - lift is 0 exactly when swing is not positive,
     *   - lift is 0 for half the cycle, so a foot is planted half the time. */
    int lift_zero_when_swing_negative = 1;
    int lift_zero_count = 0;
    int swing_negative_count = 0;
    wb_walk f;
    wb_walk_init(&f, WB_WALK_RATE_PER_METRE);
    for (int i = 0; i < 2000; i++) {
        double s = wb_walk_swing(&f);
        double l = wb_walk_lift(&f);
        if (s < -1.0000001 || s > 1.0000001) { CHECK(0, "swing leaves [-1,1]"); break; }
        if (l < -1e-12 || l > 1.0000001) { CHECK(0, "lift leaves [0,1]"); break; }
        if (l > 0.0 && s <= 0.0) lift_zero_when_swing_negative = 0;
        if (l == 0.0) lift_zero_count++;
        if (s < 0.0) swing_negative_count++;
        wb_walk_step(&f, 1.0 / 2000.0 * 2.0 * 3.14159265358979 /
                        WB_WALK_RATE_PER_METRE, 1.0);
    }
    CHECK(lift_zero_when_swing_negative,
          "lift is zero whenever the swing is not positive");
    CHECK(lift_zero_count > 0, "a foot is planted for part of every cycle");
    CHECK(swing_negative_count > 0, "the swing goes negative for part of a cycle");
    CHECK(lift_zero_count < 2000, "a foot is lifted for part of every cycle");

    /* ---- a stride is a sensible length ----------------------------------- */
    /* One full cycle is 2*pi rad, which at the default rate is 1.4 m -- two
     * steps. A body at 4 m/s should therefore cycle about 2.9 times a second. */
    wb_walk g;
    wb_walk_init(&g, WB_WALK_RATE_PER_METRE);
    wb_walk_step(&g, 1.0, 4.0);
    double cycles = g.phase / (2.0 * 3.14159265358979);
    CHECK(cycles > 0.5 && cycles < 5.0,
          "a 4 m/s body cycles a plausible number of times per second");

    /* ---- the rate is configurable ---------------------------------------- */
    wb_walk h;
    wb_walk_init(&h, 1.0);              /* one radian per metre */
    wb_walk_step(&h, 1.0, 1.0);         /* one metre */
    CHECK(fabs(h.phase - 1.0) < 1e-12, "a custom rate is honoured");

    printf("=== WALK TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
