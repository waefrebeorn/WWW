/*
 * wb_material.c -- material draw sorting (LP-12 stone 1). See wb_material.h.
 */
#include "wb_material.h"

static int wb_draw_less(const wb_draw *a, const wb_draw *b, int blended) {
    if (a->shader != b->shader) return a->shader < b->shader;
    if (a->params != b->params) return a->params < b->params;
    if (blended && a->depth != b->depth) return a->depth > b->depth;  /* far first */
    return a->order < b->order;                                      /* stable */
}

void wb_material_sort(wb_draw *draws, uint32_t n, int blended) {
    if (!draws || n < 2) return;
    /* insertion sort: stable, and the lists are nearly sorted in practice */
    for (uint32_t i = 1; i < n; i++) {
        wb_draw key = draws[i];
        uint32_t j = i;
        while (j > 0 && wb_draw_less(&key, &draws[j - 1], blended)) {
            draws[j] = draws[j - 1];
            j--;
        }
        draws[j] = key;
    }
}
