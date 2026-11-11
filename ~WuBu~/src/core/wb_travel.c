/*
 * wb_travel.c -- cross-level authority and travel (LP-100 stone 1). See wb_travel.h.
 */
#include "wb_travel.h"

#include <stddef.h>
#include <stdlib.h>

static wb_travel_rec *wb_travel_find(wb_travel_ledger *l, uint64_t traveler) {
    if (!l || !l->recs) return NULL;
    for (uint32_t i = 0; i < l->n; i++)
        if (l->recs[i].traveler == traveler) return &l->recs[i];
    return NULL;
}

static const wb_travel_rec *wb_travel_find_c(const wb_travel_ledger *l, uint64_t traveler) {
    return wb_travel_find((wb_travel_ledger *)l, traveler);
}

int wb_travel_init(wb_travel_ledger *l, uint32_t cap) {
    if (!l || cap == 0) return -1;
    l->recs = (wb_travel_rec *)calloc(cap, sizeof(wb_travel_rec));
    if (!l->recs) return -1;
    l->cap = cap;
    l->n = 0;
    l->epoch = 1;
    return 0;
}

void wb_travel_free(wb_travel_ledger *l) {
    if (!l) return;
    free(l->recs);
    l->recs = NULL;
    l->cap = 0;
    l->n = 0;
}

void wb_travel_clear(wb_travel_ledger *l) {
    if (!l) return;
    l->n = 0;
    l->epoch = 1;
}

int wb_travel_arrive(wb_travel_ledger *l, uint64_t traveler, uint64_t level) {
    if (!l || !l->recs) return -1;
    if (traveler == 0 || level == 0) return -1;      /* 0 means "unknown" */
    if (wb_travel_find(l, traveler)) return -1;      /* settle is once-only */
    if (l->n >= l->cap) return -1;

    wb_travel_rec *r = &l->recs[l->n++];
    r->traveler = traveler;
    r->owner = level;
    r->pending = 0;
    r->state = WB_TRAVEL_SETTLED;
    r->epoch = 0;
    l->epoch++;
    return 0;
}

int wb_travel_begin(wb_travel_ledger *l, uint64_t traveler, uint64_t to_level,
                    uint64_t *epoch_out) {
    if (!l || !l->recs) return -1;
    if (to_level == 0) return -1;
    wb_travel_rec *r = wb_travel_find(l, traveler);
    if (!r) return -1;
    if (r->state != WB_TRAVEL_SETTLED) return -1;    /* already moving */
    if (r->owner == to_level) return -1;             /* nowhere to go */
    r->pending = to_level;
    r->state = WB_TRAVEL_IN_FLIGHT;
    l->epoch++;
    if (epoch_out) *epoch_out = l->epoch;
    return 0;
}

int wb_travel_commit(wb_travel_ledger *l, uint64_t traveler, uint64_t expect_epoch) {
    if (!l || !l->recs) return -1;
    wb_travel_rec *r = wb_travel_find(l, traveler);
    if (!r) return -1;
    if (r->state != WB_TRAVEL_IN_FLIGHT) return -1;
    /* The ledger must not have moved since the begin: a commit from an earlier
     * attempt is refused rather than applied to a stale intent. */
    if (l->epoch != expect_epoch) return -1;

    r->owner = r->pending;        /* the atomic flip */
    r->pending = 0;
    r->state = WB_TRAVEL_SETTLED;
    r->epoch++;                   /* this traveler's own fence advances */
    l->epoch++;
    return 0;
}

int wb_travel_abort(wb_travel_ledger *l, uint64_t traveler) {
    if (!l || !l->recs) return -1;
    wb_travel_rec *r = wb_travel_find(l, traveler);
    if (!r) return -1;
    if (r->state != WB_TRAVEL_IN_FLIGHT) return -1;
    r->pending = 0;               /* the source keeps ownership */
    r->state = WB_TRAVEL_SETTLED;
    l->epoch++;
    return 0;
}

uint64_t wb_travel_owner(const wb_travel_ledger *l, uint64_t traveler) {
    const wb_travel_rec *r = wb_travel_find_c(l, traveler);
    return r ? r->owner : 0;
}

int wb_travel_state(const wb_travel_ledger *l, uint64_t traveler, uint32_t *state) {
    const wb_travel_rec *r = wb_travel_find_c(l, traveler);
    if (!r) return 0;
    if (state) *state = r->state;
    return 1;
}

uint64_t wb_travel_pending(const wb_travel_ledger *l, uint64_t traveler) {
    const wb_travel_rec *r = wb_travel_find_c(l, traveler);
    return r ? r->pending : 0;
}

uint64_t wb_travel_epoch(const wb_travel_ledger *l) {
    return l ? l->epoch : 0;
}

uint32_t wb_travel_count(const wb_travel_ledger *l) {
    return l ? l->n : 0;
}
