/*
 * wb_assetgraph_test.c -- LP-36 stone 1 gate: the asset dependency graph.
 *
 * Devil's-advocate cases: the topological order respects every edge, a cycle is
 * rejected at the edge that closes it, the dirty closure is the transitive
 * dependent set, a standalone asset's closure is itself, and the result is
 * deterministic.
 */
#include "wb_assetgraph.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

#define N 6
/* 0 texture, 1 material(0), 2 mesh(0), 3 prefab(1,2), 4 level(3), 5 standalone */
static const uint32_t k_deps[N][3] = {
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 1, 2, 0 },
    { 3, 0, 0 },
    { 0, 0, 0 },
};
static const uint32_t k_ndeps[N] = { 0, 1, 1, 2, 1, 0 };

static void build(wb_assetgraph *g) {
    wb_assetgraph_init(g, N);
    for (uint32_t a = 0; a < N; a++)
        for (uint32_t k = 0; k < k_ndeps[a]; k++)
            wb_assetgraph_add_dep(g, a, k_deps[a][k]);
}

int main(void) {
    wb_assetgraph g;
    build(&g);

    /* ---- the dependency counts ---- */
    CHECK(wb_assetgraph_dep_count(&g, 0) == 0, "the texture has no dependencies");
    CHECK(wb_assetgraph_dep_count(&g, 3) == 2, "the prefab has two dependencies");

    /* ---- topological order ---- */
    uint32_t order[N];
    CHECK(wb_assetgraph_topo(&g, order) == N, "the topological sort covers every asset");
    int ordered = 1;
    for (uint32_t a = 0; a < N; a++)
        for (uint32_t k = 0; k < k_ndeps[a]; k++) {
            uint32_t dep = k_deps[a][k], pa = 0, pd = 0;
            for (uint32_t i = 0; i < N; i++) { if (order[i] == a) pa = i; if (order[i] == dep) pd = i; }
            if (pd > pa) ordered = 0;
        }
    CHECK(ordered, "every asset comes after its dependencies");

    /* ---- a cycle is rejected at the closing edge ---- */
    CHECK(wb_assetgraph_add_dep(&g, 0, 4) == -1, "the edge that closes a cycle is rejected");
    CHECK(wb_assetgraph_add_dep(&g, 1, 3) == -1, "a shorter cycle is rejected too");
    CHECK(wb_assetgraph_add_dep(&g, 2, 2) == -1, "a self-dependency is rejected");
    CHECK(wb_assetgraph_add_dep(&g, 99, 0) == -1, "an out-of-range asset is rejected");
    CHECK(wb_assetgraph_add_dep(&g, 0, 99) == -1, "an out-of-range dependency is rejected");

    /* ---- the dirty closure is the transitive dependent set ---- */
    uint32_t closure[N];
    uint32_t n = wb_assetgraph_dirty_closure(&g, 0, closure);
    CHECK(n == 5, "the texture dirties five assets");
    uint8_t seen[N] = {0};
    for (uint32_t i = 0; i < n; i++) seen[closure[i]] = 1;
    CHECK(seen[0] && seen[1] && seen[2] && seen[3] && seen[4], "the closure is the texture and its dependents");
    CHECK(!seen[5], "the standalone asset is not dirtied");

    n = wb_assetgraph_dirty_closure(&g, 3, closure);
    CHECK(n == 2 && seen[3], "the prefab dirties only itself and the level");
    CHECK(closure[0] == 3 && closure[1] == 4, "the closure is the prefab and its dependent");

    n = wb_assetgraph_dirty_closure(&g, 5, closure);
    CHECK(n == 1 && closure[0] == 5, "a standalone asset dirties only itself");

    /* ---- determinism ---- */
    wb_assetgraph h;
    build(&h);
    uint32_t o1[N], o2[N];
    wb_assetgraph_topo(&g, o1);
    wb_assetgraph_topo(&h, o2);
    CHECK(memcmp(o1, o2, sizeof o1) == 0, "the topological order is deterministic");
    uint32_t c1[N], c2[N];
    memset(c1, 0, sizeof c1);
    memset(c2, 0, sizeof c2);
    wb_assetgraph_dirty_closure(&g, 0, c1);
    wb_assetgraph_dirty_closure(&h, 0, c2);
    CHECK(memcmp(c1, c2, sizeof c1) == 0, "the dirty closure is deterministic");

    /* ---- degenerate input ---- */
    CHECK(wb_assetgraph_init(NULL, N) == -1, "a NULL graph is rejected");
    CHECK(wb_assetgraph_init(&g, 0) == -1, "a zero-size graph is rejected");
    CHECK(wb_assetgraph_topo(&g, NULL) == -1, "a NULL output is rejected");
    CHECK(wb_assetgraph_dirty_closure(&g, 99, closure) == 0, "an out-of-range asset dirties nothing");

    wb_assetgraph_free(&g);
    wb_assetgraph_free(&h);
    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
