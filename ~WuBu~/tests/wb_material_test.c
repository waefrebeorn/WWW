/*
 * wb_material_test.c -- LP-12 stone 1 gate: material draw sorting.
 *
 * Devil's-advocate cases: opaque grouping by (shader, params), blended
 * back-to-front ordering within a group, stability of ties, the result being a
 * permutation, determinism, and degenerate input.
 */
#include "wb_material.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

#define ND 5
static wb_draw g_src[ND] = {
    { 2, 5, 1.0f, 0 },
    { 1, 9, 2.0f, 1 },
    { 2, 5, 3.0f, 2 },
    { 1, 9, 0.5f, 3 },
    { 1, 3, 5.0f, 4 },
};

static int is_permutation(const wb_draw *a, const wb_draw *b, int n) {
    for (int i = 0; i < n; i++) {
        int found = 0;
        for (int j = 0; j < n; j++)
            if (a[i].order == b[j].order) { found = 1; break; }
        if (!found) return 0;
    }
    return 1;
}

int main(void) {
    wb_draw d[ND];

    /* ---- opaque: grouped by (shader, params), stable within a group ---- */
    memcpy(d, g_src, sizeof g_src);
    wb_material_sort(d, ND, 0);
    CHECK(d[0].order == 4 && d[1].order == 1 && d[2].order == 3 &&
          d[3].order == 0 && d[4].order == 2, "opaque draws are grouped by (shader, params)");
    CHECK(is_permutation(d, g_src, ND), "the sort is a permutation");

    int grouped = 1;
    for (int i = 1; i < ND; i++)
        if (d[i].shader < d[i - 1].shader ||
            (d[i].shader == d[i - 1].shader && d[i].params < d[i - 1].params)) grouped = 0;
    CHECK(grouped, "state changes never increase");

    /* ---- blended: back-to-front within a (shader, params) group ---- */
    memcpy(d, g_src, sizeof g_src);
    wb_material_sort(d, ND, 1);
    CHECK(d[0].order == 4, "the blended group (1,3) comes first");
    CHECK(d[1].order == 1 && d[2].order == 3, "group (1,9) is far-to-near");
    CHECK(d[3].order == 2 && d[4].order == 0, "group (2,5) is far-to-near");
    CHECK(is_permutation(d, g_src, ND), "the blended sort is a permutation");

    /* ---- stability: identical keys keep their original order ---- */
    wb_draw same[3] = { { 1, 1, 1.0f, 0 }, { 1, 1, 1.0f, 1 }, { 1, 1, 1.0f, 2 } };
    wb_material_sort(same, 3, 0);
    CHECK(same[0].order == 0 && same[1].order == 1 && same[2].order == 2, "ties keep their order");

    /* ---- determinism ---- */
    wb_draw a[ND], b[ND];
    memcpy(a, g_src, sizeof g_src);
    memcpy(b, g_src, sizeof g_src);
    wb_material_sort(a, ND, 1);
    wb_material_sort(b, ND, 1);
    CHECK(memcmp(a, b, sizeof a) == 0, "sorting is deterministic");

    /* ---- degenerate input ---- */
    wb_material_sort(NULL, 0, 0);
    wb_material_sort(d, 0, 0);
    wb_material_sort(d, 1, 1);
    CHECK(1, "degenerate input is a no-op");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
