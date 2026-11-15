/*
 * wb_net_transport.h -- LP-70: the datagram transport abstraction.
 *
 * The netcode above (server, client, loopback) speaks in datagrams; this is the
 * layer that moves them. It is deliberately the smallest thing that can be
 * true: a transport sends datagrams to a peer, delivers the ones that arrive,
 * and advances its own clock. It does not sequence, retransmit or encrypt --
 * reliability is `wb_net_reliable`, which wraps any transport.
 *
 * `wb_net.h` promised a `wb_net_transport` so the same netcode would run over
 * UDP, a loopback pipe or a relay. This is that type, actually defined.
 *
 * C11, no external deps.
 */
#ifndef WB_NET_TRANSPORT_H
#define WB_NET_TRANSPORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Wire budget: a 16-byte reliability header plus payload, under the 1400-byte
 * WB_NET_MAX_PACKET budget the rest of the net stack assumes. */
#define WB_NET_MAX_PAYLOAD 1200
#define WB_NET_MAX_WIRE    (16 + WB_NET_MAX_PAYLOAD)
#define WB_NET_MAX_PEERS   64

/* What the transport moves. Fixed size and copied by value: a datagram must not
 * point into a buffer the caller is about to reuse. */
typedef struct {
    uint16_t len;
    uint8_t  data[WB_NET_MAX_WIRE];
} wb_net_datagram;

/* Delivery callback: `peer` is the far endpoint's id, `d` the datagram. */
typedef void (*wb_net_recv_fn)(void *ud, int peer, const wb_net_datagram *d);

typedef struct wb_net_transport wb_net_transport;

/* The vtable every transport implements. `impl` is the private state; the
 * struct is public only so a layer (reliability, encryption) can wrap another
 * transport without a second dispatch mechanism. */
struct wb_net_transport {
    /* Queue one datagram for `peer`. 0 on success, -1 on refusal (bad peer,
     * oversized). A lossy transport returns 0 for a datagram it intends to
     * DROP: the sender cannot tell the wire from the void, which is the point. */
    int  (*send)(wb_net_transport *t, int peer, const wb_net_datagram *d);
    /* Deliver every datagram that has arrived, in arrival order. */
    int  (*poll)(wb_net_transport *t, wb_net_recv_fn cb, void *ud);
    /* Advance the transport's clock by `dt` seconds (latency, retransmission). */
    void (*tick)(wb_net_transport *t, double dt);
    void (*destroy)(wb_net_transport *t);
    void *impl;
};

/* The in-process PAIR: two endpoints that deliver to each other with a
 * deterministic loss rate and a fixed latency. `loss_rate` is 0..1. The drops
 * come from a seeded xorshift, so the same rate always produces the same
 * pattern and a failing gate is reproducible. `other` receives endpoint 1;
 * endpoint 0 is the return value. Destroy either endpoint to drop the pair. */
wb_net_transport *wb_net_transport_pair_new(float loss_rate, double latency_s,
                                            wb_net_transport **other);

#ifdef __cplusplus
}
#endif

#endif /* WB_NET_TRANSPORT_H */
