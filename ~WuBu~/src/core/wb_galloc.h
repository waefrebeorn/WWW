/*
 * wb_galloc.h -- GPU-style suballocator (LP-09 stone 1).
 *
 * A GPU heap is not malloc: resources are placed into large heaps, alignment
 * is explicit, and memory is aliased across frames or heaps when lifetimes do
 * not overlap. This module is the allocator half -- a first-fit suballocator
 * over one contiguous range with:
 *   - explicit alignment (placed resources)
 *   - per-block heap tagging (aliasing: a freed block can serve another heap)
 *   - coalescing on release
 *
 * It is CPU-side bookkeeping: no GPU is touched, so the whole policy is
 * testable. C11, no deps.
 */
#ifndef WB_GALLOC_H
#define WB_GALLOC_H

#include <stddef.h>
#include <stdint.h>

typedef struct wb_galloc_block {
    size_t   offset;
    size_t   size;
    int      free;
    uint32_t heap;                 /* aliasing tag */
    struct wb_galloc_block *next;
} wb_galloc_block;

typedef struct {
    size_t          capacity;
    wb_galloc_block *head;
    size_t          used;          /* bytes currently allocated */
} wb_galloc;

void wb_galloc_init(wb_galloc *g, size_t capacity);
void wb_galloc_clear(wb_galloc *g);          /* releases every block */

/* Place `size` bytes at an `align`ed offset in `heap`. On success *out is the
 * block handle to pass to wb_galloc_release. Returns 0 on success, -1 if the
 * request does not fit. */
int  wb_galloc_alloc(wb_galloc *g, size_t size, size_t align, uint32_t heap,
                     wb_galloc_block **out);

/* Release a block back to the heap and coalesce with free neighbours. */
void wb_galloc_release(wb_galloc *g, wb_galloc_block *blk);

size_t wb_galloc_used(const wb_galloc *g);
size_t wb_galloc_free(const wb_galloc *g);
/* The largest contiguous free run -- what a single large resource could place. */
size_t wb_galloc_largest_free(const wb_galloc *g);

#endif /* WB_GALLOC_H */
