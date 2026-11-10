/*
 * wb_bindless.c -- bindless resource table (LP-11). See wb_bindless.h.
 */
#include "wb_bindless.h"

#include <stdlib.h>

int wb_bindless_init(wb_bindless *t, uint32_t capacity) {
    if (!t || capacity == 0) return -1;
    t->slots = (uint32_t *)calloc(capacity, sizeof(uint32_t));
    if (!t->slots) return -1;
    t->cap = capacity;
    t->high_water = 0;
    t->free_list = NULL;
    t->n_free = 0;
    t->free_cap = 0;
    t->live = 0;
    return 0;
}

void wb_bindless_free(wb_bindless *t) {
    if (!t) return;
    free(t->slots);
    free(t->free_list);
    t->slots = NULL;
    t->free_list = NULL;
    t->cap = 0;
    t->high_water = 0;
    t->n_free = t->free_cap = 0;
    t->live = 0;
}

int wb_bindless_add(wb_bindless *t, uint32_t resource, uint32_t *out_index) {
    if (!t || !t->slots || resource == 0) return -1;
    uint32_t idx;
    if (t->n_free > 0) {
        idx = t->free_list[--t->n_free];        /* LIFO reuse */
    } else if (t->high_water < t->cap) {
        idx = t->high_water++;                  /* a fresh slot */
    } else {
        return -1;                              /* table full */
    }
    t->slots[idx] = resource;
    t->live++;
    if (out_index) *out_index = idx;
    return 0;
}

void wb_bindless_remove(wb_bindless *t, uint32_t index) {
    if (!t || !t->slots || index >= t->cap) return;
    if (t->slots[index] == 0) return;           /* already empty */
    t->slots[index] = 0;
    if (t->live > 0) t->live--;
    if (t->n_free == t->free_cap) {
        uint32_t nc = t->free_cap ? t->free_cap * 2 : 16;
        uint32_t *nf = (uint32_t *)realloc(t->free_list, nc * sizeof(uint32_t));
        if (!nf) return;                        /* the slot stays allocated (no reuse) */
        t->free_list = nf;
        t->free_cap = nc;
    }
    t->free_list[t->n_free++] = index;
}

uint32_t wb_bindless_get(const wb_bindless *t, uint32_t index) {
    if (!t || !t->slots || index >= t->cap) return 0;
    return t->slots[index];
}

uint32_t wb_bindless_live(const wb_bindless *t) { return t ? t->live : 0; }
