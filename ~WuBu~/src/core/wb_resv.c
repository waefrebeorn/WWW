/*
 * wb_resv.c -- the nested-volume reservation ledger (LP-107 stone 1). See wb_resv.h.
 */
#include "wb_resv.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static wb_resv_entry *wb_resv_find(wb_resv_ledger *l, uint64_t volume) {
    if (!l || !l->entries) return NULL;
    for (uint32_t i = 0; i < l->n; i++)
        if (l->entries[i].volume == volume) return &l->entries[i];
    return NULL;
}

static const wb_resv_entry *wb_resv_find_c(const wb_resv_ledger *l, uint64_t volume) {
    return wb_resv_find((wb_resv_ledger *)l, volume);
}

int wb_resv_init(wb_resv_ledger *l, uint32_t cap) {
    if (!l || cap == 0) return -1;
    l->entries = (wb_resv_entry *)calloc(cap, sizeof(wb_resv_entry));
    if (!l->entries) return -1;
    l->cap = cap;
    l->n = 0;
    l->epoch = 1;
    return 0;
}

void wb_resv_free(wb_resv_ledger *l) {
    if (!l) return;
    free(l->entries);
    l->entries = NULL;
    l->cap = 0;
    l->n = 0;
}

void wb_resv_clear(wb_resv_ledger *l) {
    if (!l) return;
    l->n = 0;
    l->epoch = 1;
}

int wb_resv_add(wb_resv_ledger *l, uint64_t volume, uint64_t parent, uint32_t flags) {
    if (!l || !l->entries) return -1;
    if (l->n >= l->cap) return -1;
    if (wb_resv_find(l, volume)) return -1;              /* a duplicate id */
    if (volume == 0) return -1;                          /* 0 is "no parent" */
    if (parent != 0 && !wb_resv_find(l, parent)) return -1;   /* the parent must exist */

    /* Reject a cycle: the new volume must not be its own ancestor. */
    uint64_t p = parent;
    while (p != 0) {
        if (p == volume) return -1;
        const wb_resv_entry *e = wb_resv_find_c(l, p);
        if (!e) break;
        p = e->parent;
    }

    wb_resv_entry *e = &l->entries[l->n++];
    e->volume = volume;
    e->parent = parent;
    e->holder = 0;
    e->flags = flags;
    l->epoch++;
    return 0;
}

int wb_resv_is_ancestor(const wb_resv_ledger *l, uint64_t volume, uint64_t maybe_ancestor) {
    if (!l || !l->entries) return 0;
    uint64_t p = volume;
    while (p != 0) {
        if (p == maybe_ancestor) return 1;
        const wb_resv_entry *e = wb_resv_find_c(l, p);
        if (!e) return 0;
        p = e->parent;
    }
    return 0;
}

int wb_resv_reserve(wb_resv_ledger *l, uint64_t volume, uint64_t holder) {
    if (!l || !l->entries) return -1;
    if (holder == 0) return -1;                          /* 0 is "no holder" */
    wb_resv_entry *e = wb_resv_find(l, volume);
    if (!e) return -1;

    /* Already held: a repeat by the same holder is a harmless retry, by anyone
     * else it is a conflict. */
    if (e->holder != 0) return e->holder == holder ? 0 : -1;

    /* An ancestor held by someone else blocks this, unless it delegated. */
    uint64_t p = e->parent;
    while (p != 0) {
        const wb_resv_entry *a = wb_resv_find_c(l, p);
        if (!a) break;
        if (a->holder != 0 && a->holder != holder && !(a->flags & WB_RESV_DELEGABLE)) return -1;
        p = a->parent;
    }

    /* A descendant held by someone else blocks this, unless this delegated. */
    if (!(e->flags & WB_RESV_DELEGABLE)) {
        for (uint32_t i = 0; i < l->n; i++) {
            const wb_resv_entry *d = &l->entries[i];
            if (d->holder == 0 || d->holder == holder) continue;
            if (wb_resv_is_ancestor(l, d->volume, volume)) return -1;
        }
    }

    e->holder = holder;
    l->epoch++;
    return 0;
}

int wb_resv_release(wb_resv_ledger *l, uint64_t volume, uint64_t holder) {
    if (!l || !l->entries) return -1;
    wb_resv_entry *e = wb_resv_find(l, volume);
    if (!e) return -1;
    if (e->holder == 0) return -1;                       /* already free */
    if (e->holder != holder) return -1;                  /* not yours to release */
    e->holder = 0;
    l->epoch++;
    return 0;
}

uint64_t wb_resv_holder(const wb_resv_ledger *l, uint64_t volume) {
    const wb_resv_entry *e = wb_resv_find_c(l, volume);
    return e ? e->holder : 0;
}

int wb_resv_is_held(const wb_resv_ledger *l, uint64_t volume) {
    return wb_resv_holder(l, volume) != 0;
}

int wb_resv_is_delegable(const wb_resv_ledger *l, uint64_t volume) {
    const wb_resv_entry *e = wb_resv_find_c(l, volume);
    return e && (e->flags & WB_RESV_DELEGABLE) != 0;
}

int wb_resv_covers(const wb_resv_ledger *l, uint64_t volume, uint64_t holder) {
    if (!l || !l->entries || holder == 0) return 0;
    uint64_t p = volume;
    while (p != 0) {
        const wb_resv_entry *e = wb_resv_find_c(l, p);
        if (!e) return 0;
        if (e->holder == holder) return 1;
        p = e->parent;
    }
    return 0;
}

uint64_t wb_resv_epoch(const wb_resv_ledger *l) {
    return l ? l->epoch : 0;
}

uint32_t wb_resv_count(const wb_resv_ledger *l) {
    return l ? l->n : 0;
}
