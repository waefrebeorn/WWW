/*
 * wb_galloc.c -- GPU-style suballocator (LP-09 stone 1). See wb_galloc.h.
 */
#include "wb_galloc.h"

#include <stdlib.h>

void wb_galloc_init(wb_galloc *g, size_t capacity) {
    if (!g) return;
    g->capacity = capacity;
    g->used = 0;
    wb_galloc_block *b = (wb_galloc_block *)calloc(1, sizeof(*b));
    if (!b) { g->capacity = 0; return; }
    b->offset = 0;
    b->size = capacity;
    b->free = 1;
    b->heap = 0;
    b->next = NULL;
    g->head = b;
}

void wb_galloc_clear(wb_galloc *g) {
    if (!g) return;
    wb_galloc_block *b = g->head;
    while (b) { wb_galloc_block *nx = b->next; free(b); b = nx; }
    g->head = NULL;
    g->used = 0;
    /* reset to a single free run covering the whole heap */
    if (g->capacity) {
        wb_galloc_block *whole = (wb_galloc_block *)calloc(1, sizeof(*whole));
        if (whole) {
            whole->offset = 0;
            whole->size = g->capacity;
            whole->free = 1;
            whole->next = NULL;
            g->head = whole;
        }
    }
}

int wb_galloc_alloc(wb_galloc *g, size_t size, size_t align, uint32_t heap,
                    wb_galloc_block **out) {
    if (!g || !g->head || size == 0) return -1;
    if (align < 1) align = 1;

    wb_galloc_block *prev = NULL;
    for (wb_galloc_block *b = g->head; b; prev = b, b = b->next) {
        if (!b->free) continue;
        uintptr_t base = (uintptr_t)b->offset;
        uintptr_t aligned = (base + (uintptr_t)align - 1) & ~((uintptr_t)align - 1);
        size_t pad = (size_t)(aligned - base);
        if (b->size < pad || b->size - pad < size) continue;

        /* split the leading padding into its own free block */
        if (pad > 0) {
            wb_galloc_block *lead = (wb_galloc_block *)malloc(sizeof(*lead));
            if (!lead) return -1;
            lead->offset = b->offset;
            lead->size = pad;
            lead->free = 1;
            lead->heap = 0;
            lead->next = b;
            if (prev) prev->next = lead; else g->head = lead;
            b->offset = (size_t)aligned;
            b->size -= pad;
        }
        /* split the trailing remainder into its own free block */
        if (b->size > size) {
            wb_galloc_block *tail = (wb_galloc_block *)malloc(sizeof(*tail));
            if (!tail) return -1;
            tail->offset = b->offset + size;
            tail->size = b->size - size;
            tail->free = 1;
            tail->heap = 0;
            tail->next = b->next;
            b->next = tail;
            b->size = size;
        }
        b->free = 0;
        b->heap = heap;
        g->used += size;
        if (out) *out = b;
        return 0;
    }
    return -1;   /* does not fit */
}

void wb_galloc_release(wb_galloc *g, wb_galloc_block *blk) {
    if (!g || !blk || blk->free) return;
    blk->free = 1;
    if (blk->size <= g->used) g->used -= blk->size; else g->used = 0;
    blk->heap = 0;

    /* coalesce with free neighbours */
    wb_galloc_block *b = g->head;
    while (b && b->next) {
        if (b->free && b->next->free) {
            wb_galloc_block *nx = b->next;
            b->size += nx->size;
            b->next = nx->next;
            free(nx);
        } else {
            b = b->next;
        }
    }
}

size_t wb_galloc_used(const wb_galloc *g) { return g ? g->used : 0; }

size_t wb_galloc_free(const wb_galloc *g) {
    if (!g) return 0;
    size_t total = 0;
    for (const wb_galloc_block *b = g->head; b; b = b->next)
        if (b->free) total += b->size;
    return total;
}

size_t wb_galloc_largest_free(const wb_galloc *g) {
    if (!g) return 0;
    size_t best = 0;
    for (const wb_galloc_block *b = g->head; b; b = b->next)
        if (b->free && b->size > best) best = b->size;
    return best;
}
