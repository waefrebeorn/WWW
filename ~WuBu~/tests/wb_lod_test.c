/*
 * wb_lod_test.c -- LP-15 stone 1 gate: LOD selection with hysteresis.
 *
 * Devil's-advocate cases: correct band selection, the anti-pop margin in both
 * directions, that a large jump still lands on the right level, determinism,
 * and degenerate chains.
 */
#include "wb_lod.h"

#include <stdio.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static const float k_sizes[3] = { 100.0f, 50.0f, 20.0f };
#define H 0.1f

int main(void) {
    /* ---- the stateless mapping picks the right band ---- */
    wb_lod l;
    wb_lod_init(&l, k_sizes, 3, H);
    CHECK(wb_lod_level_for(&l, 80.0f) == 0, "80px is LOD 0");
    CHECK(wb_lod_level_for(&l, 40.0f) == 1, "40px is LOD 1");
    CHECK(wb_lod_level_for(&l, 10.0f) == 2, "10px is LOD 2");
    CHECK(wb_lod_level_for(&l, 100.0f) == 0, "exactly at a threshold stays finer");

    /* ---- a fresh chain follows the size ---- */
    wb_lod_init(&l, k_sizes, 3, H);
    CHECK(wb_lod_pick(&l, 80.0f) == 0, "start fine");
    CHECK(wb_lod_pick(&l, 40.0f) == 1, "crossing down moves coarser");
    CHECK(wb_lod_pick(&l, 30.0f) == 1, "still inside the band");
    CHECK(wb_lod_pick(&l, 10.0f) == 2, "crossing further down moves coarser again");
    CHECK(wb_lod_current(&l) == 2, "current is tracked");

    /* ---- hysteresis holds the level near a boundary ---- */
    wb_lod_init(&l, k_sizes, 3, H);
    wb_lod_pick(&l, 40.0f);                        /* -> 1 */
    CHECK(wb_lod_pick(&l, 46.0f) == 1, "just below the threshold does not pop");
    CHECK(wb_lod_pick(&l, 17.0f) == 2, "clearly below the threshold does switch");

    wb_lod_init(&l, k_sizes, 3, H);
    wb_lod_pick(&l, 10.0f);                        /* -> 2 */
    CHECK(wb_lod_pick(&l, 54.0f) == 2, "just above the threshold does not pop");
    CHECK(wb_lod_pick(&l, 56.0f) == 1, "clearly past the threshold does switch");

    /* ---- a large jump still lands on the correct level ---- */
    wb_lod_init(&l, k_sizes, 3, H);
    wb_lod_pick(&l, 10.0f);                        /* -> 2 */
    CHECK(wb_lod_pick(&l, 115.0f) == 0, "a large jump goes all the way fine");
    CHECK(wb_lod_pick(&l, 95.0f) == 0, "and stays there");

    /* ---- hysteresis disabled behaves like the stateless mapping ---- */
    wb_lod hd;
    wb_lod_init(&hd, k_sizes, 3, 0.0f);
    CHECK(wb_lod_pick(&hd, 80.0f) == 0, "h=0: 80px is LOD 0");
    CHECK(wb_lod_pick(&hd, 40.0f) == 1, "h=0: 40px is LOD 1");
    CHECK(wb_lod_pick(&hd, 10.0f) == 2, "h=0: 10px is LOD 2");

    /* ---- determinism: the same sequence gives the same levels ---- */
    int a[6], b[6];
    const float seq[6] = { 80.0f, 40.0f, 46.0f, 10.0f, 56.0f, 115.0f };
    wb_lod la, lb;
    wb_lod_init(&la, k_sizes, 3, H);
    wb_lod_init(&lb, k_sizes, 3, H);
    for (int i = 0; i < 6; i++) { a[i] = wb_lod_pick(&la, seq[i]); b[i] = wb_lod_pick(&lb, seq[i]); }
    int same = 1;
    for (int i = 0; i < 6; i++) same &= (a[i] == b[i]);
    CHECK(same, "selection is deterministic");

    /* ---- degenerate chains are safe ---- */
    wb_lod none;
    wb_lod_init(&none, NULL, 0, H);
    CHECK(wb_lod_pick(&none, 10.0f) == 0, "an empty chain returns level 0");
    CHECK(wb_lod_current(NULL) == 0, "NULL chain is safe");
    CHECK(wb_lod_level_for(&none, 10.0f) == 0, "level_for on an empty chain is 0");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
