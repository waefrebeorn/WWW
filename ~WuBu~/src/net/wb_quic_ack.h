/*
 * wb_quic_ack.h -- LP-93 stone 5: the receive side of acknowledgements.
 *
 * The ACK frame codec (slice 42) turns a set of packet numbers into bytes; this
 * decides *which* numbers go in it and *when* to send one. The rules are
 * RFC 9000's:
 *
 *   - section 13.2.1: acknowledge immediately when an ack-eliciting packet
 *     arrives out of order -- its number is below another ack-eliciting packet
 *     already received -- or when there is a gap below it. A packet marked
 *     ECN-CE is acknowledged immediately too, so the peer reacts to congestion
 *     without waiting.
 *   - section 13.2.2: otherwise send an ACK after at least two ack-eliciting
 *     packets, or when the peer's max_ack_delay has elapsed, whichever comes
 *     first.
 *   - section 13.2.1 again: never answer a non-ack-eliciting packet with a
 *     packet that is itself non-ack-eliciting. That would be an infinite loop
 *     of acknowledgements; those packets are acknowledged later, alongside
 *     ack-eliciting ones.
 *   - section 13.2.3: the most recently received packets are always included,
 *     and older ranges are dropped first when the frame would not fit.
 *
 * The tracker keeps a bounded, sorted set of received packet numbers. The frame
 * builder takes the newest ones up to the capacity it is given.
 *
 * C11, no external deps.
 */
#ifndef WB_QUIC_ACK_H
#define WB_QUIC_ACK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WB_QUIC_ACK_MAX 128

typedef struct {
    uint64_t pn[WB_QUIC_ACK_MAX];
    int      n;
    int      ack_eliciting_since;   /* ack-eliciting packets since the last ACK */
    int      immediate;             /* an out-of-order or CE packet was seen */
    int      non_ack_pending;       /* a non-ack-eliciting packet is waiting */
    uint64_t first_pending_us;
    int      has_pending_time;
} wb_quic_ack_track;

void wb_quic_ack_track_init(wb_quic_ack_track *t, uint64_t now_us);

/* Record a received packet. `ack_eliciting` is whether it contained anything
 * but ACK, PADDING and CONNECTION_CLOSE; `ecn_ce` is its ECN marking. */
void wb_quic_ack_track_note(wb_quic_ack_track *t, uint64_t pn,
                            int ack_eliciting, int ecn_ce, uint64_t now_us);

/* Whether an ACK frame should be sent now. `max_ack_delay_us` is the peer's
 * advertised value. */
int wb_quic_ack_track_should_send(const wb_quic_ack_track *t,
                                  uint64_t max_ack_delay_us, uint64_t now_us);

/* Build an ACK frame body from the tracked set, newest first, dropping the
 * oldest ranges when `cap` is too small. Returns the byte count, or 0 if
 * nothing is tracked or it does not fit. `largest` is the largest acknowledged
 * packet number, which the caller supplies because it is the same value the
 * sender will expect. */
size_t wb_quic_ack_track_frame(const wb_quic_ack_track *t, uint64_t largest,
                               uint64_t delay, uint8_t *out, size_t cap);

/* Clear the tracker after an ACK has been sent. */
void wb_quic_ack_track_sent(wb_quic_ack_track *t);

/* How many packet numbers are tracked. */
int wb_quic_ack_track_count(const wb_quic_ack_track *t);

#ifdef __cplusplus
}
#endif

#endif /* WB_QUIC_ACK_H */
