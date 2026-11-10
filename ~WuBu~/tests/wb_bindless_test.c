/*
 * wb_bindless_test.c -- LP-11 gate: bindless resource table.
 *
 * Devil's-advocate cases: dense stable indices, LIFO slot recycling, table
 * exhaustion, double removal, out-of-range reads, a zero (invalid) resource,
 * and determinism.
 */
#include "wb_bindless.h"

#include <stdio.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

#define CAP 8

int main(void) {
    wb_bindless t;
    CHECK(wb_bindless_init(&t, CAP) == 0, "table created");
    CHECK(wb_bindless_live(&t) == 0, "a fresh table is empty");

    /* ---- dense, stable indices ---- */
    uint32_t ia, ib, ic;
    CHECK(wb_bindless_add(&t, 1001, &ia) == 0, "first add");
    CHECK(wb_bindless_add(&t, 1002, &ib) == 0, "second add");
    CHECK(wb_bindless_add(&t, 1003, &ic) == 0, "third add");
    CHECK(ia == 0 && ib == 1 && ic == 2, "indices are dense from zero");
    CHECK(wb_bindless_get(&t, ia) == 1001 && wb_bindless_get(&t, ic) == 1003, "indices resolve to their resources");
    CHECK(wb_bindless_live(&t) == 3, "live count tracks the table");

    /* ---- removal clears the slot and recycles it (LIFO) ---- */
    wb_bindless_remove(&t, ib);
    CHECK(wb_bindless_get(&t, ib) == 0, "a removed slot reads as empty");
    CHECK(wb_bindless_live(&t) == 2, "live count drops");
    uint32_t id;
    CHECK(wb_bindless_add(&t, 2001, &id) == 0, "add after removal");
    CHECK(id == ib, "the freed slot is recycled");
    CHECK(wb_bindless_get(&t, id) == 2001, "the recycled slot holds the new resource");

    /* ---- double removal is a no-op ---- */
    wb_bindless_remove(&t, ib);
    uint32_t live_after = wb_bindless_live(&t);
    wb_bindless_remove(&t, ib);
    CHECK(wb_bindless_live(&t) == live_after, "removing an empty slot changes nothing");

    /* ---- out-of-range and invalid reads ---- */
    CHECK(wb_bindless_get(&t, CAP) == 0, "an out-of-range index reads as empty");
    CHECK(wb_bindless_get(&t, 99999u) == 0, "a far out-of-range index reads as empty");
    CHECK(wb_bindless_add(&t, 0, &id) == -1, "a zero (invalid) resource is rejected");

    /* ---- fill to capacity ---- */
    while (wb_bindless_live(&t) < CAP) {
        uint32_t idx;
        if (wb_bindless_add(&t, 3000 + wb_bindless_live(&t), &idx) != 0) break;
    }
    CHECK(wb_bindless_live(&t) == CAP, "the table fills to capacity");
    uint32_t extra;
    CHECK(wb_bindless_add(&t, 4000, &extra) == -1, "a full table rejects a new resource");

    /* ---- determinism ---- */
    wb_bindless a, b;
    wb_bindless_init(&a, CAP);
    wb_bindless_init(&b, CAP);
    int same = 1;
    for (int i = 0; i < 6; i++) {
        uint32_t xa, xb;
        int ra = wb_bindless_add(&a, 100 + i, &xa);
        int rb = wb_bindless_add(&b, 100 + i, &xb);
        same &= (ra == rb) && (xa == xb);
    }
    wb_bindless_remove(&a, 2); wb_bindless_remove(&b, 2);
    uint32_t ya, yb;
    same &= (wb_bindless_add(&a, 999, &ya) == 0) && (wb_bindless_add(&b, 999, &yb) == 0) && (ya == yb);
    CHECK(same, "slot allocation is deterministic");
    wb_bindless_free(&a);
    wb_bindless_free(&b);

    /* ---- degenerate init ---- */
    CHECK(wb_bindless_init(NULL, CAP) == -1, "NULL table is rejected");
    CHECK(wb_bindless_init(&a, 0) == -1, "a zero-capacity table is rejected");

    wb_bindless_free(&t);
    CHECK(wb_bindless_live(&t) == 0, "free resets the table");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
