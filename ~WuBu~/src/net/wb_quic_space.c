/*
 * wb_quic_space.c -- LP-93 stone 4: a QUIC packet number space.
 */
#include "wb_quic_space.h"

#include <string.h>

void wb_quic_space_init(wb_quic_space *s, uint64_t now_us) {
    if (!s) return;
    memset(s, 0, sizeof *s);
    s->now_us = now_us;
    s->min_rtt_us = -1;
    s->smoothed_rtt_us = -1;
    s->rttvar_us = -1;
    s->latest_rtt_us = -1;
}

void wb_quic_space_tick(wb_quic_space *s, uint64_t now_us) {
    if (!s) return;
    s->now_us = now_us;
}

uint64_t wb_quic_space_send(wb_quic_space *s, uint32_t bytes,
                            int ack_eliciting, int padding) {
    if (!s || s->n_sent >= WB_QUIC_SPACE_MAX) return 0;
    uint64_t pn = s->next_pn++;
    wb_quic_sent *p = &s->sent[s->n_sent++];
    p->pn = pn;
    p->sent_us = s->now_us;
    p->bytes = bytes;
    p->ack_eliciting = ack_eliciting ? 1 : 0;
    /* RFC 9002 section 3: a packet is in flight when it is ack-eliciting or
     * contains PADDING, and has not been acknowledged or declared lost. */
    p->in_flight = (ack_eliciting || padding) ? 1 : 0;
    return pn;
}

int wb_quic_space_acked(wb_quic_space *s, uint64_t pn) {
    if (!s) return -1;

    /* Find it among the in-flight packets and drop it. A packet that is no
     * longer in flight has already been acknowledged or declared lost. */
    int found = -1;
    for (int i = 0; i < s->n_sent; i++) {
        if (s->sent[i].pn == pn) { found = i; break; }
    }
    if (found < 0) return -1;
    wb_quic_sent p = s->sent[found];
    memmove(&s->sent[found], &s->sent[found + 1],
            (size_t)(s->n_sent - found - 1) * sizeof s->sent[0]);
    s->n_sent--;

    /* RFC 9002 section 5.1: a sample needs the largest acknowledged packet to
     * be newly acknowledged, and at least one newly acknowledged packet to be
     * ack-eliciting. This acknowledgement is for one packet, so that packet has
     * to be ack-eliciting and to be the largest so far -- which is also what
     * rules out retransmission ambiguity. */
    if (!p.ack_eliciting) return -1;
    if (s->has_largest_acked && pn < s->largest_acked_pn) return -1;

    uint64_t rtt = s->now_us - p.sent_us;

    if (!s->has_largest_acked) {
        s->min_rtt_us = (int64_t)rtt;
        s->smoothed_rtt_us = (int64_t)rtt;
        /* RFC 9002 section 5.3: the first sample resets the estimator. */
        s->rttvar_us = (int64_t)rtt / 2;
    } else {
        if ((int64_t)rtt < s->min_rtt_us) s->min_rtt_us = (int64_t)rtt;
        int64_t err = (int64_t)rtt - s->smoothed_rtt_us;
        s->rttvar_us = (3 * s->rttvar_us + (err < 0 ? -err : err)) / 4;
        s->smoothed_rtt_us = (7 * s->smoothed_rtt_us + (int64_t)rtt) / 8;
    }
    s->latest_rtt_us = (int64_t)rtt;
    s->largest_acked_pn = pn;
    s->largest_acked_sent_us = p.sent_us;
    s->has_largest_acked = 1;
    return 0;
}

uint64_t wb_quic_space_largest_acked(const wb_quic_space *s, int *has) {
    if (has) *has = s->has_largest_acked;
    return s->largest_acked_pn;
}

uint64_t wb_quic_space_loss_delay(const wb_quic_space *s) {
    int64_t base = s->smoothed_rtt_us;
    if (s->latest_rtt_us > base) base = s->latest_rtt_us;
    if (base < 0) base = 0;
    uint64_t delay = (uint64_t)base * WB_QUIC_TIME_THRESHOLD_NUM
                     / WB_QUIC_TIME_THRESHOLD_DEN;
    if (delay < WB_QUIC_GRANULARITY_US) delay = WB_QUIC_GRANULARITY_US;
    return delay;
}

size_t wb_quic_space_lost(wb_quic_space *s, uint64_t *pns, size_t max,
                          uint64_t *loss_time) {
    if (!s) { if (loss_time) *loss_time = WB_QUIC_NO_LOSS; return 0; }

    /* RFC 9002 A.10 clears the loss timer before it runs. */
    s->has_loss_time = 0;
    s->loss_time_us = WB_QUIC_NO_LOSS;
    if (loss_time) *loss_time = WB_QUIC_NO_LOSS;
    if (!s->has_largest_acked) return 0;

    uint64_t delay = wb_quic_space_loss_delay(s);
    /* `now()` less the delay, without underflowing the clock. */
    uint64_t lost_send_time = s->now_us > delay ? s->now_us - delay : 0;

    size_t n = 0;
    for (int i = 0; i < s->n_sent; ) {
        wb_quic_sent *p = &s->sent[i];
        /* Packets above the largest acknowledged one are not candidates. The
         * array is in ascending packet number order, so nor is anything after
         * the first such packet. */
        if (p->pn > s->largest_acked_pn) break;

        int lost = (p->sent_us <= lost_send_time) ||
                   (s->largest_acked_pn >= p->pn + WB_QUIC_PACKET_THRESHOLD);
        if (lost) {
            if (p->in_flight && n < max) pns[n++] = p->pn;
            memmove(&s->sent[i], &s->sent[i + 1],
                    (size_t)(s->n_sent - i - 1) * sizeof s->sent[0]);
            s->n_sent--;
        } else {
            uint64_t t = p->sent_us + delay;
            if (!s->has_loss_time || t < s->loss_time_us) {
                s->loss_time_us = t;
                s->has_loss_time = 1;
            }
            i++;
        }
    }
    if (s->has_loss_time && loss_time) *loss_time = s->loss_time_us;
    return n;
}

uint64_t wb_quic_space_pto(const wb_quic_space *s, uint64_t max_ack_delay_us,
                           int handshake_confirmed, int backoff) {
    int64_t srtt = s->smoothed_rtt_us < 0 ? 0 : s->smoothed_rtt_us;
    int64_t var = s->rttvar_us < 0 ? 0 : s->rttvar_us;
    int64_t v = 4 * var;
    if (v < WB_QUIC_GRANULARITY_US) v = WB_QUIC_GRANULARITY_US;
    uint64_t duration = (uint64_t)(srtt + v);
    duration <<= (backoff < 0 ? 0 : backoff);
    if (handshake_confirmed) duration += max_ack_delay_us;
    if (duration < WB_QUIC_GRANULARITY_US) duration = WB_QUIC_GRANULARITY_US;
    return duration;
}

int wb_quic_space_pn_exhausted(const wb_quic_space *s) {
    return s && s->next_pn > WB_QUIC_PN_MAX;
}

uint64_t wb_quic_space_decode_pn(uint64_t largest_pn, int has_largest_pn,
                                 uint64_t truncated_pn, int pn_nbits) {
    if (pn_nbits != 8 && pn_nbits != 16 && pn_nbits != 24 && pn_nbits != 32)
        return 0;
    uint64_t expected_pn = has_largest_pn ? largest_pn + 1 : 0;
    uint64_t pn_win = (UINT64_C(1) << pn_nbits);
    uint64_t pn_hwin = pn_win / 2;
    uint64_t pn_mask = pn_win - 1;

    uint64_t candidate_pn = (expected_pn & ~pn_mask) | truncated_pn;

    /* RFC 9000 appendix A.3. The first guard needs `expected_pn >= pn_hwin`:
     * without it the subtraction underflows in unsigned arithmetic and the
     * condition is true for every small expected number. */
    if (has_largest_pn && expected_pn >= pn_hwin &&
        candidate_pn <= expected_pn - pn_hwin &&
        candidate_pn < (UINT64_C(1) << 62) - pn_win)
        return candidate_pn + pn_win;
    if (has_largest_pn && candidate_pn > expected_pn + pn_hwin &&
        candidate_pn >= pn_win)
        return candidate_pn - pn_win;
    return candidate_pn;
}

int wb_quic_space_receive(wb_quic_space *s, uint64_t pn) {
    if (!s) return 0;

    if (!s->has_largest_recv) {
        s->largest_recv_pn = pn;
        s->has_largest_recv = 1;
        s->min_recv_pn = 0;
        s->n_recv = 0;
        s->recv[s->n_recv++] = pn;
        return 1;
    }

    if (pn > s->largest_recv_pn) {
        s->largest_recv_pn = pn;
        /* the floor rises with the largest number, keeping a bounded window */
        s->min_recv_pn = (s->largest_recv_pn > WB_QUIC_RECV_MAX)
                             ? s->largest_recv_pn - WB_QUIC_RECV_MAX
                             : 0;
    } else if (pn < s->min_recv_pn) {
        /* too old to be worth remembering: RFC 9000 section 12.3 lets an
         * endpoint drop these outright rather than grow state forever */
        return 0;
    } else {
        for (int i = 0; i < s->n_recv; i++)
            if (s->recv[i] == pn) return 0;   /* a duplicate */
    }

    /* remember it, newest first, dropping the oldest when full */
    if (s->n_recv >= WB_QUIC_RECV_MAX) {
        for (int i = s->n_recv - 1; i > 0; i--) s->recv[i] = s->recv[i - 1];
        s->n_recv = WB_QUIC_RECV_MAX - 1;
    }
    for (int i = s->n_recv - 1; i >= 0; i--)
        if (s->recv[i] < pn) {
            for (int j = s->n_recv; j > i + 1; j--) s->recv[j] = s->recv[j - 1];
            s->recv[i + 1] = pn;
            s->n_recv++;
            return 1;
        }
    for (int j = s->n_recv; j > 0; j--) s->recv[j] = s->recv[j - 1];
    s->recv[0] = pn;
    s->n_recv++;
    return 1;
}

uint64_t wb_quic_space_largest_received(const wb_quic_space *s, int *has) {
    if (!s) { if (has) *has = 0; return 0; }
    if (has) *has = s->has_largest_recv;
    return s->largest_recv_pn;
}

uint64_t wb_quic_space_min_received(const wb_quic_space *s) {
    return s ? s->min_recv_pn : 0;
}
