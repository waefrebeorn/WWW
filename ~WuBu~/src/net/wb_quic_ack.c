/*
 * wb_quic_ack.c -- LP-93 stone 5: the receive side of acknowledgements.
 */
#include "wb_quic_ack.h"

#include "wb_quic.h"
#include "wb_quic_space.h"

#include <string.h>

void wb_quic_ack_track_init(wb_quic_ack_track *t, uint64_t now_us) {
    if (!t) return;
    memset(t, 0, sizeof *t);
    t->first_pending_us = now_us;
    t->has_pending_time = 1;
}

/* Insert `pn` into the ascending array, dropping the oldest when full. */
static void track_insert(wb_quic_ack_track *t, uint64_t pn) {
    int i = 0;
    while (i < t->n && t->pn[i] < pn) i++;
    if (i < t->n && t->pn[i] == pn) return;      /* already tracked */
    if (t->n >= WB_QUIC_ACK_MAX) {
        memmove(&t->pn[0], &t->pn[1], (size_t)(t->n - 1) * sizeof t->pn[0]);
        t->n--;
        if (i > 0) i--;
    }
    memmove(&t->pn[i + 1], &t->pn[i], (size_t)(t->n - i) * sizeof t->pn[0]);
    t->pn[i] = pn;
    t->n++;
}

void wb_quic_ack_track_note(wb_quic_ack_track *t, uint64_t pn,
                            int ack_eliciting, int ecn_ce, uint64_t now_us) {
    if (!t) return;

    /* A packet already tracked changes nothing: it must not be counted twice,
     * and it cannot be out of order. */
    for (int i = 0; i < t->n; i++) {
        if (t->pn[i] == pn) return;
    }

    /* RFC 9000 section 13.2.1: acknowledge without delay when an ack-eliciting
     * packet arrives out of order, or when it leaves a gap below it. */
    if (ack_eliciting && t->n > 0) {
        uint64_t largest = t->pn[t->n - 1];
        if (pn < largest || pn > largest + 1) t->immediate = 1;
    }
    /* ... and a congestion-marked packet is acknowledged at once so the peer
     * reacts without waiting for the frequency rules. */
    if (ecn_ce) t->immediate = 1;

    track_insert(t, pn);

    if (ack_eliciting) t->ack_eliciting_since++;
    else t->non_ack_pending = 1;
    (void)now_us;
}

int wb_quic_ack_track_should_send(const wb_quic_ack_track *t,
                                  uint64_t max_ack_delay_us, uint64_t now_us) {
    if (!t) return 0;
    /* Nothing to acknowledge, or only packets that cannot ask for an ACK. */
    if (t->ack_eliciting_since == 0 && !t->immediate) return 0;

    if (t->immediate) return 1;
    /* RFC 9000 section 13.2.2: after at least two ack-eliciting packets. */
    if (t->ack_eliciting_since >= 2) return 1;
    /* ... or once the peer's max_ack_delay has elapsed. */
    if (t->has_pending_time && now_us - t->first_pending_us >= max_ack_delay_us)
        return 1;
    return 0;
}

size_t wb_quic_ack_track_frame(const wb_quic_ack_track *t, uint64_t largest,
                               uint64_t delay, uint8_t *out, size_t cap) {
    if (!t || t->n == 0) return 0;
    /* The builder wants the largest acknowledged number to be in the set. */
    if (t->pn[t->n - 1] != largest) return 0;

    uint64_t buf[WB_QUIC_ACK_MAX];
    /* Newest ranges first: section 13.2.3 says the most recently received
     * packets are always included and the oldest are dropped first. */
    for (int k = t->n; k >= 1; k--) {
        for (int i = 0; i < k; i++) buf[i] = t->pn[t->n - k + i];
        size_t m = wb_quic_frame_ack(out, cap, 0, largest, delay, buf, (size_t)k,
                                     0, 0, 0);
        if (m > 0) return m;
    }
    return 0;
}

void wb_quic_ack_track_sent(wb_quic_ack_track *t) {
    if (!t) return;
    t->n = 0;
    t->ack_eliciting_since = 0;
    t->immediate = 0;
    t->non_ack_pending = 0;
}

int wb_quic_ack_track_count(const wb_quic_ack_track *t) {
    return t ? t->n : 0;
}

int wb_quic_ack_apply(const wb_quic_ack *ack, wb_quic_space *s, int *sampled) {
    if (!ack || !s) return -1;
    if (sampled) *sampled = 0;

    uint64_t pns[WB_QUIC_ACK_MAX];
    size_t n = wb_quic_ack_expand(ack, pns, WB_QUIC_ACK_MAX);
    int acked = 0;
    for (size_t i = 0; i < n; i++) {
        int before = s->n_sent;
        int r = wb_quic_space_acked(s, pns[i]);
        if (s->n_sent < before) acked++;
        if (r == 0 && sampled) *sampled = 1;
    }
    return acked;
}
