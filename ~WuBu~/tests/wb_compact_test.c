/*
 * wb_compact_test.c -- LP-16 stone 1 gate: draw compaction.
 *
 * Devil's-advocate cases: all/some/none visible, original order preserved,
 * a zero stride, NULL inputs, and determinism.
 */
#include "wb_compact.h"
#include "wb_material.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

#define ND 5

int main(void) {
    wb_draw draws[ND] = {
        { 1, 1, 1.0f, 0 },
        { 2, 2, 2.0f, 1 },
        { 3, 3, 3.0f, 2 },
        { 4, 4, 4.0f, 3 },
        { 5, 5, 5.0f, 4 },
    };
    wb_draw out[ND];

    /* ---- everything visible: a dense copy in order ---- */
    uint8_t all[ND] = { 1, 1, 1, 1, 1 };
    uint32_t n = wb_compact(draws, ND, sizeof(wb_draw), all, out);
    CHECK(n == ND, "all visible compacts to the full count");
    int order_ok = 1;
    for (int i = 0; i < ND; i++) order_ok &= (out[i].order == (uint32_t)i);
    CHECK(order_ok, "the original order is preserved");

    /* ---- some culled: only survivors, in order ---- */
    uint8_t some[ND] = { 1, 0, 1, 0, 1 };
    n = wb_compact(draws, ND, sizeof(wb_draw), some, out);
    CHECK(n == 3, "the culled count is exact");
    CHECK(out[0].order == 0 && out[1].order == 2 && out[2].order == 4, "survivors keep their order");

    /* ---- nothing visible ---- */
    uint8_t none[ND] = { 0, 0, 0, 0, 0 };
    n = wb_compact(draws, ND, sizeof(wb_draw), none, out);
    CHECK(n == 0, "nothing visible compacts to zero");

    /* ---- a single survivor ---- */
    uint8_t one[ND] = { 0, 0, 0, 1, 0 };
    n = wb_compact(draws, ND, sizeof(wb_draw), one, out);
    CHECK(n == 1 && out[0].order == 3, "a single survivor is copied");

    /* ---- degenerate inputs ---- */
    CHECK(wb_compact(draws, 0, sizeof(wb_draw), all, out) == 0, "zero items compacts to zero");
    CHECK(wb_compact(draws, ND, 0, all, out) == 0, "a zero stride is rejected");
    CHECK(wb_compact(NULL, ND, sizeof(wb_draw), all, out) == 0, "NULL items are safe");
    CHECK(wb_compact(draws, ND, sizeof(wb_draw), NULL, out) == 0, "NULL visibility is safe");
    CHECK(wb_compact(draws, ND, sizeof(wb_draw), all, NULL) == 0, "NULL output is safe");

    /* ---- determinism ---- */
    uint8_t mask[ND] = { 1, 0, 1, 1, 0 };
    wb_draw a[ND], b[ND];
    memset(a, 0, sizeof a);
    memset(b, 0, sizeof b);
    uint32_t na = wb_compact(draws, ND, sizeof(wb_draw), mask, a);
    uint32_t nb = wb_compact(draws, ND, sizeof(wb_draw), mask, b);
    CHECK(na == nb && memcmp(a, b, sizeof a) == 0, "compaction is deterministic");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
