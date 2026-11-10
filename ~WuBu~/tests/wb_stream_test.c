/*
 * wb_stream_test.c -- LP-13 stone 1 gate: streaming budget selection.
 *
 * Devil's-advocate cases: priority order, a request that does not fit is
 * skipped (and a smaller one after it still loads), ties broken by index,
 * a zero budget, a budget larger than everything, and determinism.
 */
#include "wb_stream.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

int main(void) {
    wb_stream_req reqs[5] = {
        { 10, 0.9f, 100 },   /* 0: highest priority */
        { 11, 0.5f, 100 },   /* 1 */
        { 12, 0.7f, 100 },   /* 2 */
        { 13, 0.1f, 100 },   /* 3 */
        { 14, 0.7f, 100 },   /* 4: ties with 2, later index */
    };

    uint32_t sel[5];

    /* ---- priority order within the budget ---- */
    uint32_t n = wb_stream_select(reqs, 5, 300, sel);
    CHECK(n == 3, "three requests fit in 300 bytes");
    CHECK(sel[0] == 0, "the highest priority loads first");
    CHECK(sel[1] == 2, "the next highest loads second");
    CHECK(sel[2] == 4, "the tie is broken by the earlier index");

    /* ---- a request that does not fit is skipped, a later one still loads ---- */
    wb_stream_req big[3] = {
        { 20, 0.9f, 500 },   /* too big for the budget */
        { 21, 0.5f, 100 },
        { 22, 0.4f, 100 },
    };
    n = wb_stream_select(big, 3, 250, sel);
    CHECK(n == 2, "the oversized request is skipped, the rest load");
    CHECK(sel[0] == 1 && sel[1] == 2, "the survivors load in priority order");

    /* ---- an exact fit ---- */
    n = wb_stream_select(reqs, 5, 100, sel);
    CHECK(n == 1 && sel[0] == 0, "an exact fit loads exactly one");

    /* ---- a budget larger than everything ---- */
    n = wb_stream_select(reqs, 5, 1000, sel);
    CHECK(n == 5, "a large budget loads everything");
    CHECK(sel[0] == 0 && sel[1] == 2 && sel[2] == 4 && sel[3] == 1 && sel[4] == 3,
          "everything loads in priority order");

    /* ---- degenerate inputs ---- */
    CHECK(wb_stream_select(reqs, 5, 0, sel) == 0, "a zero budget loads nothing");
    CHECK(wb_stream_select(reqs, 0, 100, sel) == 0, "no requests loads nothing");
    CHECK(wb_stream_select(NULL, 5, 100, sel) == 0, "NULL requests are safe");
    CHECK(wb_stream_select(reqs, 5, 100, NULL) == 0, "NULL output is safe");

    /* ---- determinism ---- */
    uint32_t a[5], b[5];
    memset(a, 0, sizeof a);
    memset(b, 0, sizeof b);
    uint32_t na = wb_stream_select(reqs, 5, 300, a);
    uint32_t nb = wb_stream_select(reqs, 5, 300, b);
    CHECK(na == nb && memcmp(a, b, sizeof a) == 0, "selection is deterministic");

    /* ---- zero-byte requests are free to load ---- */
    wb_stream_req free_req[2] = { { 30, 0.2f, 0 }, { 31, 0.1f, 0 } };
    n = wb_stream_select(free_req, 2, 0, sel);
    CHECK(n == 0, "a zero budget still loads nothing (no bytes are spent)");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
