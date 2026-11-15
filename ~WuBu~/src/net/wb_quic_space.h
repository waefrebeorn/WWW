/*
 * wb_quic_space.h -- LP-93 stone 4: a QUIC packet number space.
 *
 * RFC 9000 keeps three packet number spaces (Initial, Handshake, Application),
 * and each one is an independent sequence. A space is what answers four
 * questions a transport has to ask:
 *
 *   - which packet number the next packet takes;
 *   - which of those are declared lost, by the packet and time thresholds of
 *     RFC 9002 section 6.1 and its appendix A.10;
 *   - how long until the next packet could be lost, which is what a sender
 *     waits on rather than a fixed timer.
 *
 * Two thresholds, both from RFC 9002:
 *
 *   - packet threshold -- a packet at least kPacketThreshold (3) numbers below
 *     the largest acknowledged one is lost, on the grounds that a later packet
 *     arrived and this one did not;
 *   - time threshold -- a packet sent before `now() - loss_delay` is lost, where
 *     loss_delay is max(9/8 * max(smoothed_rtt, latest_rtt), kGranularity).
 *
 * Packets above the largest acknowledged one are never candidates. The clock is
 * the caller's, so a loss decision is reproducible exactly.
 *
 * The RTT estimators are RFC 9002 section 5.3's: the first sample seeds both
 * the smoothed value and the variance at half itself; later samples move the
 * variance by a quarter of the error and the smoothed value by an eighth. A
 * sample is only taken when the largest acknowledged packet is newly
 * acknowledged, which is what rules out retransmission ambiguity.
 *
 * C11, no external deps.
 */
#ifndef WB_QUIC_SPACE_H
#define WB_QUIC_SPACE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RFC 9002 section 6.1.1. */
#define WB_QUIC_PACKET_THRESHOLD 3

/* The time threshold is kTimeThreshold * max(smoothed_rtt, latest_rtt), and
 * 9/8 is the RFC's constant, expressed in eighths to stay in integers. */
#define WB_QUIC_TIME_THRESHOLD_NUM 9
#define WB_QUIC_TIME_THRESHOLD_DEN 8

/* kGranularity: the smallest clock step a loss decision will notice. */
#define WB_QUIC_GRANULARITY_US 1000

/* No loss pending. */
#define WB_QUIC_NO_LOSS ((uint64_t)-1)

#define WB_QUIC_SPACE_MAX 256

typedef struct {
    uint64_t pn;
    uint64_t sent_us;
    uint32_t bytes;
    int      ack_eliciting;
    int      in_flight;
} wb_quic_sent;

typedef struct {
    uint64_t next_pn;
    uint64_t now_us;
    int64_t  min_rtt_us;
    int64_t  smoothed_rtt_us;
    int64_t  rttvar_us;
    int64_t  latest_rtt_us;
    uint64_t largest_acked_pn;
    uint64_t largest_acked_sent_us;
    int      has_largest_acked;
    int      has_loss_time;
    uint64_t loss_time_us;
    wb_quic_sent sent[WB_QUIC_SPACE_MAX];
    int      n_sent;
} wb_quic_space;

/* Start a space with no packets, no RTT estimate and no loss pending. */
void wb_quic_space_init(wb_quic_space *s, uint64_t now_us);

/* Advance the injected clock. */
void wb_quic_space_tick(wb_quic_space *s, uint64_t now_us);

/* Assign the next packet number and record the packet. `ack_eliciting` is
 * whether the packet contains anything but ACK, PADDING and CONNECTION_CLOSE;
 * `padding` makes a packet count as in flight even though nothing in it asks
 * for an acknowledgement. Returns the number. */
uint64_t wb_quic_space_send(wb_quic_space *s, uint32_t bytes,
                            int ack_eliciting, int padding);

/* Record an acknowledgement. Returns 0 if the sample updated the RTT
 * estimators, -1 if it was ignored because it is not for the largest
 * acknowledged packet or because it is a retransmission (A.9). */
int wb_quic_space_acked(wb_quic_space *s, uint64_t pn);

/* Largest acknowledged packet number so far, and whether there is one. */
uint64_t wb_quic_space_largest_acked(const wb_quic_space *s, int *has);

/* Recompute the loss set. Writes up to `max` lost packet numbers, ascending,
 * and returns the count. `*loss_time` is when the next packet will be declared
 * lost, or WB_QUIC_NO_LOSS. */
size_t wb_quic_space_lost(wb_quic_space *s, uint64_t *pns, size_t max,
                          uint64_t *loss_time);

/* The current loss delay: max(9/8 * max(smoothed, latest), kGranularity). */
uint64_t wb_quic_space_loss_delay(const wb_quic_space *s);

/* The probe timeout, RFC 9002 section 6.2.1:
 *   (smoothed_rtt + max(4*rttvar, kGranularity)) * 2^backoff + max_ack_delay
 * The max_ack_delay term is skipped while the handshake is unconfirmed, because
 * the peer is not expected to delay Initial or Handshake acknowledgements. */
uint64_t wb_quic_space_pto(const wb_quic_space *s, uint64_t max_ack_delay_us,
                           int handshake_confirmed, int backoff);

#ifdef __cplusplus
}
#endif

#endif /* WB_QUIC_SPACE_H */
