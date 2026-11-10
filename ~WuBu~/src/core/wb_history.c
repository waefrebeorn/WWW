/*
 * wb_history.c -- lag-compensation rewind buffer (LP-30 stone 1). See wb_history.h.
 */
#include "wb_history.h"

#include <stdlib.h>
#include <string.h>

int wb_history_init(wb_history *h, uint32_t cap, uint32_t nentities) {
    if (!h || cap == 0 || nentities == 0) return -1;
    h->frames = (wb_history_frame *)calloc(cap, sizeof(wb_history_frame));
    if (!h->frames) return -1;
    for (uint32_t i = 0; i < cap; i++) {
        h->frames[i].states = (wb_snapshot_state *)calloc(nentities, sizeof(wb_snapshot_state));
        if (!h->frames[i].states) {
            for (uint32_t k = 0; k < i; k++) free(h->frames[k].states);
            free(h->frames);
            h->frames = NULL;
            return -1;
        }
    }
    h->cap = cap;
    h->nentities = nentities;
    h->count = 0;
    h->head = 0;
    return 0;
}

void wb_history_free(wb_history *h) {
    if (!h) return;
    if (h->frames) {
        for (uint32_t i = 0; i < h->cap; i++) free(h->frames[i].states);
        free(h->frames);
    }
    h->frames = NULL;
    h->cap = h->nentities = h->count = h->head = 0;
}

void wb_history_clear(wb_history *h) {
    if (!h) return;
    h->count = 0;
    h->head = 0;
}

int wb_history_push(wb_history *h, double time, const wb_snapshot_state *states) {
    if (!h || !h->frames || !states) return -1;
    uint32_t slot;
    if (h->count < h->cap) {
        slot = (h->head + h->count) % h->cap;
        h->count++;
    } else {
        slot = h->head;                 /* overwrite the oldest */
        h->head = (h->head + 1) % h->cap;
    }
    h->frames[slot].time = time;
    memcpy(h->frames[slot].states, states, h->nentities * sizeof(wb_snapshot_state));
    return 0;
}

static wb_history_frame *wb_history_frame_at(const wb_history *h, uint32_t i) {
    return &h->frames[(h->head + i) % h->cap];
}

int wb_history_sample(const wb_history *h, double time, wb_snapshot_state *out) {
    if (!h || !h->frames || !out || h->count == 0) return -1;

    if (time <= wb_history_frame_at(h, 0)->time) {
        memcpy(out, wb_history_frame_at(h, 0)->states, h->nentities * sizeof(wb_snapshot_state));
        return 0;
    }
    uint32_t last = h->count - 1;
    if (time >= wb_history_frame_at(h, last)->time) {
        memcpy(out, wb_history_frame_at(h, last)->states, h->nentities * sizeof(wb_snapshot_state));
        return 0;
    }

    /* find the bracketing pair */
    for (uint32_t i = 0; i + 1 < h->count; i++) {
        wb_history_frame *a = wb_history_frame_at(h, i);
        wb_history_frame *b = wb_history_frame_at(h, i + 1);
        if (time >= a->time && time <= b->time) {
            double span = b->time - a->time;
            float t = span > 0.0 ? (float)((time - a->time) / span) : 0.0f;
            for (uint32_t e = 0; e < h->nentities; e++)
                wb_snapshot_interp(&a->states[e], &b->states[e], t, &out[e]);
            return 0;
        }
    }
    return -1;
}

uint32_t wb_history_count(const wb_history *h) { return h ? h->count : 0; }
