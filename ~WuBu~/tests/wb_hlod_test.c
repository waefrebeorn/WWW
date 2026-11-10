/*
 * wb_hlod_test.c -- LP-15 stone 2 gate: HLOD clustering.
 *
 * Devil's-advocate cases: grouping by surface distance, a radius that merges
 * everything, a radius that merges nothing, every object in exactly one
 * cluster, determinism, and degenerate input.
 */
#include "wb_hlod.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

#define NOBJ 5
static wb_hlod_object g_objs[NOBJ] = {
    {   0.0f, 0.0f, 0.0f, 1.0f, 0 },   /* A */
    {   2.5f, 0.0f, 0.0f, 1.0f, 1 },   /* B: surface 0.5 from A */
    {  10.0f, 0.0f, 0.0f, 1.0f, 2 },   /* C */
    {  12.5f, 0.0f, 0.0f, 1.0f, 3 },   /* D: surface 0.5 from C */
    { 100.0f, 0.0f, 0.0f, 1.0f, 4 },   /* E: far from everything */
};

int main(void) {
    uint32_t cluster[NOBJ];

    /* ---- a radius that groups the pairs ---- */
    uint32_t n = wb_hlod_cluster(g_objs, NOBJ, 1.0f, cluster);
    CHECK(n == 3, "two close pairs plus an outlier give three clusters");
    CHECK(cluster[0] == cluster[1], "A and B share a cluster");
    CHECK(cluster[2] == cluster[3], "C and D share a cluster");
    CHECK(cluster[4] != cluster[0] && cluster[4] != cluster[2], "E is its own cluster");

    /* ---- a radius that merges nothing ---- */
    n = wb_hlod_cluster(g_objs, NOBJ, 0.4f, cluster);
    CHECK(n == NOBJ, "a radius below every gap merges nothing");
    int all_distinct = 1;
    for (int i = 0; i < NOBJ; i++)
        for (int j = i + 1; j < NOBJ; j++)
            if (cluster[i] == cluster[j]) all_distinct = 0;
    CHECK(all_distinct, "every object is its own cluster");

    /* ---- a radius that bridges the pairs ---- */
    n = wb_hlod_cluster(g_objs, NOBJ, 20.0f, cluster);
    CHECK(n == 2, "a wide radius bridges A-B-C-D but not E");
    CHECK(cluster[0] == cluster[3], "the four near objects share a cluster");

    /* ---- a radius that merges everything ---- */
    n = wb_hlod_cluster(g_objs, NOBJ, 200.0f, cluster);
    CHECK(n == 1, "a huge radius merges the whole set");
    CHECK(cluster[0] == cluster[4], "outlier joins the group");

    /* ---- cluster ids are dense from zero ---- */
    n = wb_hlod_cluster(g_objs, NOBJ, 1.0f, cluster);
    int seen[NOBJ] = {0};
    for (int i = 0; i < NOBJ; i++) seen[cluster[i]] = 1;
    int dense = 1;
    for (uint32_t i = 0; i < n; i++) dense &= seen[i];
    CHECK(dense, "cluster ids are dense from zero");

    /* ---- determinism ---- */
    uint32_t a[NOBJ], b[NOBJ];
    wb_hlod_cluster(g_objs, NOBJ, 1.0f, a);
    wb_hlod_cluster(g_objs, NOBJ, 1.0f, b);
    CHECK(memcmp(a, b, sizeof a) == 0, "clustering is deterministic");

    /* ---- degenerate input ---- */
    CHECK(wb_hlod_cluster(NULL, 0, 1.0f, cluster) == 0, "empty input gives zero clusters");
    CHECK(wb_hlod_cluster(g_objs, NOBJ, -5.0f, cluster) == NOBJ, "a negative radius merges nothing");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
