/*
 * wb_net_reliable.h -- LP-70: reliability over any datagram transport.
 *
 * The three-message-class model the whole netcode is shaped like:
 *
 *   UNS  unreliable, latest-wins   movement. A dropped one is simply old; the
 *                                  next snapshot supersedes it, so spending a
 *                                  retransmit on it is wasted.
 *   REL  reliable, ordered         events. Sequenced, acked, retransmitted
 *                                  until the peer says it arrived.
 *   RED  reliable by redundancy    inputs. Sent as plain repeats and deduped
 *                                  by the receiver -- no ack, no retransmit,
 *                                  because the next input is already on its way
 *                                  and a late copy is worthless.
 *
 * The layer owns a `wb_net_transport` (the in-process pair, a UDP socket, a
 * relay) and adds the header, the ack window and the retransmit timers. It
 * never touches sockets itself.
 *
 * C11, no external deps.
 */
#ifndef WB_NET_RELIABLE_H
#define WB_NET_RELIABLE_H

#include <stdint.h>

#include "wb_net_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WB_NET_CLASS_UNS = 0,
    WB_NET_CLASS_REL = 1,
    WB_NET_CLASS_RED = 2
} wb_net_class;

/* A delivered message: the class is part of the delivery, because what the
 * receiver must do with it depends on it. */
typedef void (*wb_net_reliable_recv_fn)(void *ud, int peer, wb_net_class cls,
                                        const void *data, uint16_t len);

typedef struct wb_net_reliable wb_net_reliable;

/* Wrap `raw`. `rto` is the first retransmission timeout in seconds; it doubles
 * on each retry up to a ceiling. Takes ownership of nothing: `raw` is borrowed
 * and must outlive the layer. */
wb_net_reliable *wb_net_reliable_new(wb_net_transport *raw, double rto);
void wb_net_reliable_destroy(wb_net_reliable *r);

/* Send one message of `cls` to `peer`. Returns 0 on success, -1 on refusal
 * (bad peer, or `len` over WB_NET_MAX_PAYLOAD -- a datagram larger than the
 * wire budget is a programming error, not a condition to fragment silently). */
int  wb_net_reliable_send(wb_net_reliable *r, int peer, wb_net_class cls,
                          const void *data, uint16_t len);

/* Deliver everything that has arrived, in arrival order. REL messages come out
 * in SEND order even when the wire reordered them. */
int  wb_net_reliable_poll(wb_net_reliable *r, wb_net_reliable_recv_fn cb,
                          void *ud);

/* Advance the clock: move the transport, then retransmit whatever is past its
 * timeout. */
void wb_net_reliable_tick(wb_net_reliable *r, double dt);

/* Telemetry, for the netcode QoS pad (LP-72): what the layer actually did. */
uint64_t wb_net_reliable_sent(const wb_net_reliable *r);        /* msgs accepted */
uint64_t wb_net_reliable_resent(const wb_net_reliable *r);      /* retransmits */
uint64_t wb_net_reliable_delivered(const wb_net_reliable *r);   /* to the caller */
uint64_t wb_net_reliable_pending(const wb_net_reliable *r);     /* awaiting ack */

#ifdef __cplusplus
}
#endif

#endif /* WB_NET_RELIABLE_H */
