/*
 * wb_frame_test.c -- LP-02 stone 1 gate: the frame allocator.
 *
 * Proves sequential allocation does not overlap, alignment is honoured, reset
 * returns the buffer to empty and the same addresses are reused, and an
 * oversized request fails cleanly (the caller's fallback path).
 */
#include "wb_frame.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

#define CAP 4096
static unsigned char g_buf[CAP];

int main(void) {
    wb_frame f;
    wb_frame_init(&f, g_buf, sizeof g_buf);
    CHECK(wb_frame_used(&f) == 0 && wb_frame_left(&f) == CAP, "starts empty");

    /* ---- alignment ---- */
    void *p1 = wb_frame_alloc(&f, 1, 1);
    void *p2 = wb_frame_alloc(&f, 8, 64);
    void *p3 = wb_frame_alloc(&f, 3, 16);
    CHECK(p1 && p2 && p3, "allocations succeed");
    CHECK(((unsigned long long)p2 % 64) == 0, "64-byte alignment honoured");
    CHECK(((unsigned long long)p3 % 16) == 0, "16-byte alignment honoured");

    /* ---- no overlap ---- */
    unsigned char *b1 = (unsigned char *)p1, *b2 = (unsigned char *)p2, *b3 = (unsigned char *)p3;
    CHECK(b1 + 1 <= b2 && b2 + 8 <= b3, "blocks do not overlap");

    /* ---- reset reuses the same addresses ---- */
    size_t used_before = wb_frame_used(&f);
    wb_frame_reset(&f);
    CHECK(wb_frame_used(&f) == 0 && wb_frame_left(&f) == CAP, "reset empties the frame");
    void *q1 = wb_frame_alloc(&f, 1, 1);
    CHECK(q1 == p1, "the first block is reused after reset");
    wb_frame_reset(&f);
    CHECK(wb_frame_used(&f) == 0, "reset is idempotent");

    /* ---- out of memory fails cleanly ---- */
    void *big = wb_frame_alloc(&f, CAP + 1, 1);
    CHECK(big == NULL, "an oversized request fails");
    CHECK(wb_frame_used(&f) == 0, "a failed allocation consumes nothing");
    void *edge = wb_frame_alloc(&f, CAP, 1);
    CHECK(edge == g_buf, "an exact-fit request consumes the whole buffer");
    CHECK(wb_frame_left(&f) == 0, "the buffer is now full");
    CHECK(wb_frame_alloc(&f, 1, 1) == NULL, "allocation from a full frame fails");

    /* ---- zero-size is rejected ---- */
    wb_frame_reset(&f);
    CHECK(wb_frame_alloc(&f, 0, 1) == NULL, "a zero-size allocation is rejected");

    /* ---- determinism: the same sequence yields the same addresses ---- */
    wb_frame_reset(&f);
    void *a1 = wb_frame_alloc(&f, 13, 8);
    void *a2 = wb_frame_alloc(&f, 29, 8);
    wb_frame_reset(&f);
    void *c1 = wb_frame_alloc(&f, 13, 8);
    void *c2 = wb_frame_alloc(&f, 29, 8);
    CHECK(a1 == c1 && a2 == c2, "allocation is deterministic across resets");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
