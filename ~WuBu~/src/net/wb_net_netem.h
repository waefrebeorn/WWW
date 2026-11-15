/*
 * wb_net_netem.h -- LP-97 stone 2: a netem-style link emulator.
 *
 * The gates for the transport, the reliability classes, the fragmenter and the
 * clock all need a link worse than a loopback to be worth anything, and a real
 * network is not reproducible. This is the stand-in: the same knobs tc-netem
 * exposes, driven by a seeded stream, so a failing gate can be replayed.
 *
 *   delay      minimum latency, seconds
 *   jitter     uniform +/- on top of the delay
 *   loss       fraction dropped, 0..1
 *   duplicate  fraction sent twice, 0..1
 *   corrupt    fraction with one bit flipped, 0..1
 *   reorder    fraction held back past the packet behind it, 0..1
 *   rate       wire rate in kbit/s, 0 = unlimited
 *
 * Reordering is modelled as "held back one slot": a packet that is reordered
 * waits an extra `delay`, so it lands after the one that was sent after it.
 * That is the property the netcode cares about -- arrival order -- rather than
 * a full queue-position model.
 *
 * C11, no external deps.
 */
#ifndef WB_NET_NETEM_H
#define WB_NET_NETEM_H

#include "wb_net_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wb_net_netem wb_net_netem;

wb_net_netem *wb_net_netem_new(wb_net_transport *inner, uint32_t seed);
void wb_net_netem_free(wb_net_netem *n);

void wb_net_netem_set(wb_net_netem *n, double delay, double jitter, double loss,
                      double duplicate, double corrupt, double reorder,
                      double rate_kbits);

/* Queue a datagram. 0 unless the peer is bad or the link is full. */
int wb_net_netem_send(wb_net_netem *n, int peer, const wb_net_datagram *d);

/* Deliver everything whose time has come. */
int wb_net_netem_poll(wb_net_netem *n, wb_net_recv_fn cb, void *ud);

/* Advance the link's clock. */
void wb_net_netem_tick(wb_net_netem *n, double dt);

/* Packets still in flight, and the counters. */
size_t wb_net_netem_pending(const wb_net_netem *n);
unsigned long long wb_net_netem_dropped(const wb_net_netem *n);
unsigned long long wb_net_netem_duplicated(const wb_net_netem *n);
unsigned long long wb_net_netem_corrupted(const wb_net_netem *n);
unsigned long long wb_net_netem_reordered(const wb_net_netem *n);

#ifdef __cplusplus
}
#endif

#endif /* WB_NET_NETEM_H */
