/*
 * wb_history_test.c -- LP-30 stone 1 gate: the lag-compensation rewind buffer.
 *
 * Devil's-advocate cases: interpolation between bracketing frames, clamping
 * outside the recorded range, ring overwrite past capacity, clearing, and
 * determinism.
 */
#include "wb_history.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)
#define NEAR(a, b, e) (fabsf((a) - (b)) <= (e))

#define CAP 4
#define NENT 2

static wb_snapshot_state g_s[NENT];

static void set_state(float x0, float x1) {
    for (int e = 0; e < NENT; e++) {
        g_s[e].px = e == 0 ? x0 : x1;
        g_s[e].py = 0.0f; g_s[e].pz = 0.0f;
        g_s[e].qx = 0.0f; g_s[e].qy = 0.0f; g_s[e].qz = 0.0f; g_s[e].qw = 1.0f;
    }
}

int main(void) {
    wb_history h;
    CHECK(wb_history_init(&h, CAP, NENT) == 0, "buffer created");

    /* ---- record three frames: entity 0 at 0, 10, 20 ---- */
    set_state(0.0f, 100.0f);   wb_history_push(&h, 0.0, g_s);
    set_state(10.0f, 110.0f);  wb_history_push(&h, 1.0, g_s);
    set_state(20.0f, 120.0f);  wb_history_push(&h, 2.0, g_s);
    CHECK(wb_history_count(&h) == 3, "three frames recorded");

    wb_snapshot_state out[NENT];

    /* ---- interpolate between bracketing frames ---- */
    CHECK(wb_history_sample(&h, 0.5, out) == 0, "sample between frames 0 and 1");
    CHECK(NEAR(out[0].px, 5.0f, 1e-5f) && NEAR(out[1].px, 105.0f, 1e-5f), "the midpoint is interpolated");

    CHECK(wb_history_sample(&h, 1.5, out) == 0, "sample between frames 1 and 2");
    CHECK(NEAR(out[0].px, 15.0f, 1e-5f), "the second bracket is interpolated");

    /* ---- exact frame times return that frame ---- */
    CHECK(wb_history_sample(&h, 1.0, out) == 0, "sample at an exact frame time");
    CHECK(NEAR(out[0].px, 10.0f, 1e-6f), "the exact frame is returned");

    /* ---- clamping outside the recorded range ---- */
    CHECK(wb_history_sample(&h, -10.0, out) == 0, "a time before the range samples");
    CHECK(NEAR(out[0].px, 0.0f, 1e-6f), "a time before the range clamps to the first frame");
    CHECK(wb_history_sample(&h, 100.0, out) == 0, "a time after the range samples");
    CHECK(NEAR(out[0].px, 20.0f, 1e-6f), "a time after the range clamps to the last frame");

    /* ---- ring overwrite past capacity ---- */
    set_state(30.0f, 130.0f);  wb_history_push(&h, 3.0, g_s);
    set_state(40.0f, 140.0f);  wb_history_push(&h, 4.0, g_s);   /* wraps: drops t=0 */
    CHECK(wb_history_count(&h) == CAP, "the buffer never exceeds its capacity");
    CHECK(wb_history_sample(&h, 0.0, out) == 0, "a sample before the ring's oldest still succeeds");
    CHECK(NEAR(out[0].px, 10.0f, 1e-6f), "the oldest surviving frame is used after wrap");
    CHECK(wb_history_sample(&h, 3.5, out) == 0, "a sample inside the wrapped range succeeds");
    CHECK(NEAR(out[0].px, 35.0f, 1e-5f), "the wrapped range interpolates correctly");

    /* ---- clear ---- */
    wb_history_clear(&h);
    CHECK(wb_history_count(&h) == 0, "clear empties the buffer");
    CHECK(wb_history_sample(&h, 1.0, out) == -1, "sampling an empty buffer fails");

    /* ---- determinism ---- */
    set_state(0.0f, 0.0f);  wb_history_push(&h, 0.0, g_s);
    set_state(1.0f, 1.0f);  wb_history_push(&h, 1.0, g_s);
    wb_snapshot_state a[NENT], b[NENT];
    wb_history_sample(&h, 0.5, a);
    wb_history_sample(&h, 0.5, b);
    CHECK(memcmp(a, b, sizeof a) == 0, "sampling is deterministic");

    /* ---- degenerate input ---- */
    CHECK(wb_history_init(NULL, CAP, NENT) == -1, "a NULL buffer is rejected");
    CHECK(wb_history_init(&h, 0, NENT) == -1, "a zero capacity is rejected");
    CHECK(wb_history_init(&h, CAP, 0) == -1, "zero entities is rejected");

    wb_history_free(&h);
    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
