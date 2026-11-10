/*
 * wb_handoff.c -- server meshing authority handoff (LP-33 stone 1). See wb_handoff.h.
 */
#include "wb_handoff.h"

void wb_handoff_init(wb_handoff *h) {
    if (!h) return;
    h->entity = 0;
    h->source_server = 0;
    h->target_server = 0;
    h->state = WB_HANDOFF_IDLE;
    h->epoch = 0;
    h->attempts = 0;
}

int wb_handoff_begin(wb_handoff *h, uint32_t entity, uint32_t source, uint32_t target) {
    if (!h) return -1;
    if (h->state != WB_HANDOFF_IDLE) return -1;
    if (source == target) return -1;
    h->entity = entity;
    h->source_server = source;
    h->target_server = target;
    h->state = WB_HANDOFF_FREEZING;
    h->epoch++;                 /* a new epoch fences every stale message */
    h->attempts++;
    return 0;
}

int wb_handoff_freeze_done(wb_handoff *h) {
    if (!h || h->state != WB_HANDOFF_FREEZING) return -1;
    h->state = WB_HANDOFF_HANDSHAKE;
    return 0;
}

int wb_handoff_accept(wb_handoff *h) {
    if (!h || h->state != WB_HANDOFF_HANDSHAKE) return -1;
    h->state = WB_HANDOFF_ACCEPTED;
    return 0;
}

int wb_handoff_reject(wb_handoff *h) {
    if (!h || h->state != WB_HANDOFF_HANDSHAKE) return -1;
    h->state = WB_HANDOFF_REJECTED;
    return 0;
}

int wb_handoff_commit(wb_handoff *h) {
    if (!h || h->state != WB_HANDOFF_ACCEPTED) return -1;
    h->state = WB_HANDOFF_IDLE;
    return 0;
}

int wb_handoff_rollback(wb_handoff *h) {
    if (!h || h->state != WB_HANDOFF_REJECTED) return -1;
    h->state = WB_HANDOFF_IDLE;
    return 0;
}

int wb_handoff_is_stale(const wb_handoff *h, uint32_t message_epoch) {
    if (!h) return 1;
    return message_epoch != h->epoch;
}
