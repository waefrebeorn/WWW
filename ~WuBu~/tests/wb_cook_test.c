/*
 * wb_cook_test.c -- LP-46 stone 1 gate: the cook manifest.
 *
 * Devil's-advocate cases: a tampered hash breaks the chain, the diff reports the
 * first differing slot rather than a boolean, a manifest in the wrong build order
 * is rejected, and a duplicated or missing asset is rejected.
 */
#include "wb_cook.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

/* The six-node fixture: 0 texture, 1 material<-0, 2 mesh<-0, 3 prefab<-1,2,
 * 4 level<-3, 5 standalone. */
static wb_assetgraph *make_graph(void) {
    wb_assetgraph *g = (wb_assetgraph *)malloc(sizeof *g);
    wb_assetgraph_init(g, 6);
    wb_assetgraph_add_dep(g, 1, 0);
    wb_assetgraph_add_dep(g, 2, 0);
    wb_assetgraph_add_dep(g, 3, 1);
    wb_assetgraph_add_dep(g, 3, 2);
    wb_assetgraph_add_dep(g, 4, 3);
    return g;
}

int main(void) {
    wb_cook_manifest m;
    CHECK(wb_cook_init(&m, 8) == 0, "the manifest is created");

    /* ---- a manifest in topological order ---- */
    const uint32_t order[6] = { 0, 1, 2, 3, 4, 5 };
    for (int i = 0; i < 6; i++) CHECK(wb_cook_add(&m, order[i], 1000u + order[i]) == 0, "an entry is added");
    CHECK(m.n == 6, "six entries are stored");
    uint64_t chain = wb_cook_recompute(&m);
    CHECK(chain != 0, "the chain is non-zero");
    CHECK(wb_cook_verify(&m) == 1, "the manifest verifies");

    wb_assetgraph *g = make_graph();
    CHECK(wb_cook_order_ok(&m, g) == 1, "a topological manifest is accepted");

    /* ---- a tampered hash breaks the chain ---- */
    m.entries[3].hash ^= 1u;
    CHECK(wb_cook_verify(&m) == 0, "a tampered hash breaks verification");
    m.entries[3].hash ^= 1u;
    CHECK(wb_cook_verify(&m) == 1, "restoring the hash restores verification");

    /* ---- a reordered manifest is rejected ---- */
    wb_cook_manifest rev;
    wb_cook_init(&rev, 8);
    const uint32_t back[6] = { 5, 4, 3, 2, 1, 0 };
    for (int i = 0; i < 6; i++) wb_cook_add(&rev, back[i], 1000u + back[i]);
    CHECK(wb_cook_order_ok(&rev, g) == 0, "a reversed manifest is rejected");
    CHECK(wb_cook_verify(&rev) == 0, "the reversed manifest's chain was never computed");

    /* ---- a duplicated asset is rejected ---- */
    wb_cook_manifest dup;
    wb_cook_init(&dup, 8);
    for (int i = 0; i < 6; i++) wb_cook_add(&dup, order[i], 1000u + order[i]);
    CHECK(wb_cook_add(&dup, 0, 999u) == 0, "a seventh entry is added to the manifest");
    CHECK(wb_cook_order_ok(&dup, g) == 0, "a duplicated asset is rejected");

    /* ---- a missing asset is rejected ---- */
    wb_cook_manifest miss;
    wb_cook_init(&miss, 8);
    for (int i = 0; i < 5; i++) wb_cook_add(&miss, order[i], 1000u + order[i]);
    CHECK(wb_cook_order_ok(&miss, g) == 0, "a missing asset is rejected");

    /* ---- the diff reports the first differing slot ---- */
    wb_cook_manifest a, b;
    wb_cook_init(&a, 8);
    wb_cook_init(&b, 8);
    for (int i = 0; i < 6; i++) { wb_cook_add(&a, order[i], 1000u + order[i]); wb_cook_add(&b, order[i], 1000u + order[i]); }
    CHECK(wb_cook_diff(&a, &b) == 6, "identical manifests differ at the end");
    b.entries[2].hash = 4242u;
    CHECK(wb_cook_diff(&a, &b) == 2, "the diff reports the first differing slot");
    b.entries[2].hash = 1002u;
    b.entries[4].asset = 3;   /* a swapped asset id at slot 4 */
    CHECK(wb_cook_diff(&a, &b) == 4, "a differing asset id is reported");
    b.entries[4].asset = 4;
    CHECK(wb_cook_diff(&a, &b) == 6, "the manifests agree again");

    /* ---- different lengths ---- */
    wb_cook_manifest short_m, long_m;
    wb_cook_init(&short_m, 8);
    wb_cook_init(&long_m, 8);
    for (int i = 0; i < 3; i++) { wb_cook_add(&short_m, order[i], 1000u + order[i]); wb_cook_add(&long_m, order[i], 1000u + order[i]); }
    wb_cook_add(&long_m, 3, 1003u);
    CHECK(wb_cook_diff(&short_m, &long_m) == 3, "a prefix differs at the shorter length");
    CHECK(wb_cook_diff(&long_m, &short_m) == 3, "the diff is symmetric for a prefix");

    /* ---- clear ---- */
    wb_cook_clear(&m);
    CHECK(m.n == 0, "clear empties the manifest");
    CHECK(wb_cook_verify(&m) == 0, "a cleared manifest with a stale chain does not verify");
    CHECK(wb_cook_recompute(&m) == wb_cook_recompute(&m), "an empty manifest's chain is stable");

    /* ---- determinism ---- */
    wb_cook_manifest d1, d2;
    wb_cook_init(&d1, 8);
    wb_cook_init(&d2, 8);
    for (int i = 0; i < 6; i++) {
        wb_cook_add(&d1, order[i], 1000u + order[i]);
        wb_cook_add(&d2, order[i], 1000u + order[i]);
    }
    wb_cook_recompute(&d1);
    wb_cook_recompute(&d2);
    CHECK(d1.chain == d2.chain, "the chain is deterministic");

    /* ---- degenerate input ---- */
    CHECK(wb_cook_init(NULL, 8) == -1, "a NULL manifest is rejected");
    CHECK(wb_cook_init(&m, 0) == -1, "a zero capacity is rejected");
    CHECK(wb_cook_add(NULL, 0, 0) == -1, "a NULL manifest rejects an add");
    CHECK(wb_cook_verify(NULL) == 0, "a NULL manifest does not verify");
    CHECK(wb_cook_diff(NULL, &a) == 0, "a NULL manifest has no diff");
    CHECK(wb_cook_order_ok(NULL, g) == 0, "a NULL manifest has no valid order");
    CHECK(wb_cook_order_ok(&m, NULL) == 0, "a NULL graph has no valid order");

    wb_cook_free(&m);
    wb_cook_free(&rev);
    wb_cook_free(&dup);
    wb_cook_free(&miss);
    wb_cook_free(&a);
    wb_cook_free(&b);
    wb_cook_free(&short_m);
    wb_cook_free(&long_m);
    wb_cook_free(&d1);
    wb_cook_free(&d2);
    wb_assetgraph_free(g);
    free(g);
    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
