/*
 * wb_quota.c -- per-volume capacity and quotas (LP-113 stone 1). See wb_quota.h.
 */
#include "wb_quota.h"

#include <stddef.h>
#include <stdlib.h>

static int wb_quota_vol_index(const wb_quota_ledger *l, uint64_t volume) {
    if (!l || !l->vols) return -1;
    for (uint32_t i = 0; i < l->vn; i++)
        if (l->vols[i].volume == volume) return (int)i;
    return -1;
}

static int wb_quota_use_index(const wb_quota_ledger *l, uint64_t volume, uint64_t holder) {
    if (!l || !l->uses) return -1;
    for (uint32_t i = 0; i < l->un; i++)
        if (l->uses[i].volume == volume && l->uses[i].holder == holder)
            return (int)i;
    return -1;
}

/* 1 if `anc` is `vol` or an ancestor of it. The ledger has no cycles (wb_quota_add
 * refuses one), so the walk terminates; the guard is belt and braces. */
static int wb_quota_is_ancestor(const wb_quota_ledger *l, int vol, uint64_t anc) {
    uint32_t guard = 0;
    for (;;) {
        if (l->vols[vol].volume == anc) return 1;
        if (l->vols[vol].parent == 0) return 0;
        int p = wb_quota_vol_index(l, l->vols[vol].parent);
        if (p < 0 || guard++ > l->vn) return 0;
        vol = p;
    }
}

/* The parent chain of `vi`, `vi` itself first. Returns the number of entries
 * written (at most cap), or -1 if the chain is broken. */
static int wb_quota_chain(const wb_quota_ledger *l, int vi, int *out, uint32_t cap) {
    uint32_t n = 0;
    uint32_t guard = 0;
    for (;;) {
        if (n >= cap) return -1;
        out[n++] = vi;
        if (l->vols[vi].parent == 0) break;
        int p = wb_quota_vol_index(l, l->vols[vi].parent);
        if (p < 0 || guard++ > l->vn) return -1;
        vi = p;
    }
    return (int)n;
}

int wb_quota_init(wb_quota_ledger *l, uint32_t vcap, uint32_t ucap) {
    if (!l || vcap == 0 || ucap == 0) return -1;
    l->vols = (wb_quota_vol *)calloc(vcap, sizeof(wb_quota_vol));
    l->uses = (wb_quota_use *)calloc(ucap, sizeof(wb_quota_use));
    if (!l->vols || !l->uses) {
        free(l->vols);
        free(l->uses);
        l->vols = NULL;
        l->uses = NULL;
        return -1;
    }
    l->vcap = vcap;
    l->vn = 0;
    l->ucap = ucap;
    l->un = 0;
    l->epoch = 1;
    return 0;
}

void wb_quota_free(wb_quota_ledger *l) {
    if (!l) return;
    free(l->vols);
    free(l->uses);
    l->vols = NULL;
    l->uses = NULL;
    l->vcap = 0;
    l->vn = 0;
    l->ucap = 0;
    l->un = 0;
}

void wb_quota_clear(wb_quota_ledger *l) {
    if (!l) return;
    l->vn = 0;
    l->un = 0;
    l->epoch = 1;
}

uint32_t wb_quota_count(const wb_quota_ledger *l) {
    return l ? l->vn : 0;
}

uint64_t wb_quota_epoch(const wb_quota_ledger *l) {
    return l ? l->epoch : 0;
}

int wb_quota_add(wb_quota_ledger *l, uint64_t volume, uint64_t parent, uint32_t capacity) {
    if (!l || !l->vols) return -1;
    if (volume == 0) return -1;                       /* 0 means "unknown" */
    if (wb_quota_vol_index(l, volume) >= 0) return -1;
    if (l->vn >= l->vcap) return -1;
    if (parent != 0 && wb_quota_vol_index(l, parent) < 0) return -1;

    wb_quota_vol *v = &l->vols[l->vn++];
    v->volume = volume;
    v->parent = parent;
    v->capacity = capacity;
    v->used = 0;
    return 0;
}

int wb_quota_set(wb_quota_ledger *l, uint64_t volume, uint64_t holder, uint32_t limit) {
    if (!l || !l->vols) return -1;
    if (wb_quota_vol_index(l, volume) < 0) return -1;
    if (holder == 0) return -1;
    int i = wb_quota_use_index(l, volume, holder);
    if (limit == 0) {                 /* clear the quota, keep the usage */
        if (i >= 0) l->uses[i].limit = 0;
        return 0;
    }
    if (i >= 0) {
        /* A quota cannot be set below what the holder already holds in the
         * subtree: the holder would be over a limit that was just announced,
         * with no operation to blame. */
        if (wb_quota_subtree_used(l, volume, holder) > limit) return -1;
        l->uses[i].limit = limit;
        return 0;
    }
    if (l->un >= l->ucap) return -1;
    wb_quota_use *u = &l->uses[l->un++];
    u->volume = volume;
    u->holder = holder;
    u->limit = limit;
    u->used = 0;
    return 0;
}

int wb_quota_reserve(wb_quota_ledger *l, uint64_t volume, uint64_t holder, uint32_t units) {
    if (!l || !l->vols) return -1;
    if (holder == 0 || units == 0) return -1;
    int vi = wb_quota_vol_index(l, volume);
    if (vi < 0) return -1;

    int chain[WB_QUOTA_MAX_VOLS];
    int n = wb_quota_chain(l, vi, chain, WB_QUOTA_MAX_VOLS);
    if (n < 0) return -1;

    /* Capacity: the volume and every ancestor must have room. */
    for (int k = 0; k < n; k++) {
        const wb_quota_vol *v = &l->vols[chain[k]];
        if (v->used + units > v->capacity) return -1;
    }
    /* Quota: every quota the holder holds on the chain must still hold. */
    for (int k = 0; k < n; k++) {
        int ui = wb_quota_use_index(l, l->vols[chain[k]].volume, holder);
        if (ui >= 0 && l->uses[ui].limit > 0) {
            uint32_t sub = wb_quota_subtree_used(l, l->vols[chain[k]].volume, holder);
            if (sub + units > l->uses[ui].limit) return -1;
        }
    }

    /* Everything is checked; make sure the usage entry exists before applying, so
     * a full usage table refuses without having moved anything. */
    int ui = wb_quota_use_index(l, volume, holder);
    if (ui < 0) {
        if (l->un >= l->ucap) return -1;
        ui = (int)l->un++;
        l->uses[ui].volume = volume;
        l->uses[ui].holder = holder;
        l->uses[ui].limit = 0;
        l->uses[ui].used = 0;
    }

    for (int k = 0; k < n; k++) l->vols[chain[k]].used += units;
    l->uses[ui].used += units;
    l->epoch++;
    return 0;
}

int wb_quota_release(wb_quota_ledger *l, uint64_t volume, uint64_t holder, uint32_t units) {
    if (!l || !l->vols) return -1;
    if (holder == 0 || units == 0) return -1;
    int vi = wb_quota_vol_index(l, volume);
    if (vi < 0) return -1;
    int ui = wb_quota_use_index(l, volume, holder);
    if (ui < 0 || l->uses[ui].used < units) return -1;

    int chain[WB_QUOTA_MAX_VOLS];
    int n = wb_quota_chain(l, vi, chain, WB_QUOTA_MAX_VOLS);
    if (n < 0) return -1;

    for (int k = 0; k < n; k++) l->vols[chain[k]].used -= units;
    l->uses[ui].used -= units;
    l->epoch++;
    return 0;
}

uint32_t wb_quota_capacity(const wb_quota_ledger *l, uint64_t volume) {
    int i = wb_quota_vol_index(l, volume);
    return i < 0 ? 0 : l->vols[i].capacity;
}

uint32_t wb_quota_used(const wb_quota_ledger *l, uint64_t volume) {
    int i = wb_quota_vol_index(l, volume);
    return i < 0 ? 0 : l->vols[i].used;
}

uint32_t wb_quota_room(const wb_quota_ledger *l, uint64_t volume) {
    int i = wb_quota_vol_index(l, volume);
    if (i < 0) return 0;
    return l->vols[i].used < l->vols[i].capacity
               ? l->vols[i].capacity - l->vols[i].used
               : 0;
}

uint32_t wb_quota_available(const wb_quota_ledger *l, uint64_t volume) {
    int vi = wb_quota_vol_index(l, volume);
    if (vi < 0) return 0;
    int chain[WB_QUOTA_MAX_VOLS];
    int n = wb_quota_chain(l, vi, chain, WB_QUOTA_MAX_VOLS);
    if (n < 0) return 0;
    uint32_t best = 0;
    for (int k = 0; k < n; k++) {
        const wb_quota_vol *v = &l->vols[chain[k]];
        uint32_t f = v->used < v->capacity ? v->capacity - v->used : 0;
        if (k == 0 || f < best) best = f;
    }
    return best;
}

uint32_t wb_quota_limit(const wb_quota_ledger *l, uint64_t volume, uint64_t holder) {
    int i = wb_quota_use_index(l, volume, holder);
    return i < 0 ? 0 : l->uses[i].limit;
}

uint32_t wb_quota_holder_used(const wb_quota_ledger *l, uint64_t volume, uint64_t holder) {
    int i = wb_quota_use_index(l, volume, holder);
    return i < 0 ? 0 : l->uses[i].used;
}

uint32_t wb_quota_subtree_used(const wb_quota_ledger *l, uint64_t volume, uint64_t holder) {
    if (!l || !l->uses) return 0;
    if (wb_quota_vol_index(l, volume) < 0) return 0;
    uint32_t total = 0;
    for (uint32_t i = 0; i < l->un; i++) {
        if (l->uses[i].holder != holder) continue;
        int ui = wb_quota_vol_index(l, l->uses[i].volume);
        if (ui >= 0 && wb_quota_is_ancestor(l, ui, volume)) total += l->uses[i].used;
    }
    return total;
}
