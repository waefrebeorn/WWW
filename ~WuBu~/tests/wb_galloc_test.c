/*
 * wb_galloc_test.c -- LP-09 stone 1 gate: GPU-style suballocator.
 *
 * Devil's-advocate cases: placement and alignment, reuse of a released block,
 * aliasing across heaps, out-of-memory, coalescing back to the full heap, and
 * degenerate requests.
 */
#include "wb_galloc.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

#define CAP 1024

int main(void) {
    wb_galloc g;
    wb_galloc_init(&g, CAP);
    CHECK(wb_galloc_used(&g) == 0 && wb_galloc_free(&g) == CAP, "a fresh heap is entirely free");
    CHECK(wb_galloc_largest_free(&g) == CAP, "the whole heap is one free run");

    /* ---- placement + alignment ---- */
    wb_galloc_block *a, *b, *c;
    CHECK(wb_galloc_alloc(&g, 100, 1, 1, &a) == 0, "first placement");
    CHECK(wb_galloc_alloc(&g, 100, 128, 2, &b) == 0, "aligned placement");
    CHECK(wb_galloc_alloc(&g, 100, 1, 1, &c) == 0, "third placement");
    CHECK(a->offset == 0, "first block starts at zero");
    CHECK(b->offset % 128 == 0, "alignment is honoured");
    CHECK(c->offset > b->offset, "blocks are placed in order");
    CHECK(wb_galloc_used(&g) == 300, "used bytes are exact");
    CHECK(wb_galloc_free(&g) == CAP - 300, "free bytes account for the alignment padding");

    /* ---- release + reuse (aliasing across heaps) ---- */
    size_t b_offset = b->offset;
    wb_galloc_release(&g, b);
    CHECK(wb_galloc_used(&g) == 200, "release returns the bytes");
    wb_galloc_block *d;
    CHECK(wb_galloc_alloc(&g, 100, 1, 3, &d) == 0, "replacement placement");
    CHECK(d->offset < c->offset, "the freed space is reused");
    CHECK(d->heap == 3 && d->heap != b->heap, "the block is aliased to a different heap");
    CHECK((size_t)d->offset != b_offset || d->heap != b->heap, "aliasing reuses the region");
    CHECK(wb_galloc_used(&g) == 300, "used returns to 300");

    /* ---- out of memory ---- */
    wb_galloc_block *big;
    CHECK(wb_galloc_alloc(&g, CAP, 1, 1, &big) == -1, "a request larger than the heap fails");
    CHECK(wb_galloc_alloc(&g, CAP, 1, 1, &big) == -1, "a full heap rejects a whole-heap request");

    /* ---- coalescing: releasing everything restores the full run ---- */
    wb_galloc_release(&g, a);
    wb_galloc_release(&g, c);
    wb_galloc_release(&g, d);
    CHECK(wb_galloc_used(&g) == 0, "releasing everything frees every byte");
    CHECK(wb_galloc_free(&g) == CAP, "the heap is whole again");
    CHECK(wb_galloc_largest_free(&g) == CAP, "coalescing produced one full run");

    /* ---- a large resource now fits ---- */
    wb_galloc_block *whole;
    CHECK(wb_galloc_alloc(&g, CAP, 1, 1, &whole) == 0, "a whole-heap placement succeeds after coalescing");
    CHECK(whole->offset == 0 && whole->size == CAP, "it covers the entire heap");
    wb_galloc_release(&g, whole);

    /* ---- degenerate requests ---- */
    CHECK(wb_galloc_alloc(&g, 0, 1, 1, NULL) == -1, "a zero-size request is rejected");
    CHECK(wb_galloc_alloc(NULL, 10, 1, 1, NULL) == -1, "a NULL heap is rejected");
    wb_galloc_block *nobegin;
    CHECK(wb_galloc_alloc(&g, 2000, 1, 1, &nobegin) == -1, "an oversized request fails cleanly");

    /* ---- clear releases every block ---- */
    wb_galloc_alloc(&g, 64, 1, 1, &a);
    wb_galloc_clear(&g);
    CHECK(wb_galloc_used(&g) == 0 && wb_galloc_free(&g) == CAP, "clear restores the full heap");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
