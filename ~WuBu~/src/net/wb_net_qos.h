/*
 * wb_net_qos.h -- LP-97 stone 1: the per-client bandwidth cap and DSCP marking.
 *
 * Two separate problems, both about spending a scarce resource deliberately.
 *
 * The cap. A client with a 40 kB/s uplink and a 200 Hz simulation will happily
 * saturate its own link; the queue then grows until the latency is seconds and
 * the netcode is dead. Source's `rate` exists for exactly this. The shape is a
 * token bucket: `rate` tokens arrive per second, at most `burst` may accumulate,
 * and a datagram spends its byte count to go out. The burst is what lets a
 * regular 16 Hz snapshot go out together instead of being smeared.
 *
 * DSCP. The value is only useful if it reflects what the packet IS, not who
 * sent it: a lockstep input is worthless late, a delta snapshot is worthless
 * never, and marking both "expedited" wastes the queue they would otherwise
 * share. WireGuard's discipline is to carry the inner packet's DSCP out with
 * it, so a tunnel does not hide the marking; a direct sender has to make the
 * same decision at the edge.
 *
 * C11, no external deps.
 */
#ifndef WB_NET_QOS_H
#define WB_NET_QOS_H

#include "wb_net_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wb_net_qos wb_net_qos;

/* Token bucket for one peer's outbound bytes. */
typedef struct wb_net_bucket wb_net_bucket;

wb_net_bucket *wb_net_bucket_new(double bytes_per_sec, double burst_bytes);
void wb_net_bucket_free(wb_net_bucket *b);

/* A rate of 0 means unlimited. */
void wb_net_bucket_set(wb_net_bucket *b, double bytes_per_sec, double burst_bytes);
double wb_net_bucket_rate(const wb_net_bucket *b);
double wb_net_bucket_burst(const wb_net_bucket *b);

/* Advance the bucket by `dt` seconds. */
void wb_net_bucket_tick(wb_net_bucket *b, double dt);

/* Tokens currently available. */
double wb_net_bucket_tokens(const wb_net_bucket *b);

/* True if `bytes` fit right now. */
int wb_net_bucket_fits(const wb_net_bucket *b, size_t bytes);

/* Consume `bytes`. Returns 0 on success, -1 if they do not fit. */
int wb_net_bucket_take(wb_net_bucket *b, size_t bytes);

/* Seconds until `bytes` would fit, given the current rate. 0 if it fits now.
 * Returns -1 if the rate is 0 or the request exceeds the burst. */
double wb_net_bucket_wait(const wb_net_bucket *b, size_t bytes);

/* --- DSCP classes --- */
enum {
    WB_NET_QOS_CLASS_BACKGROUND = 0,   /* bulk, a delta snapshot nobody waits for */
    WB_NET_QOS_CLASS_BULK      = 1,   /* ordinary game traffic */
    WB_NET_QOS_CLASS_STATE     = 2,   /* reliable state, wanted eventually */
    WB_NET_QOS_CLASS_INPUT     = 3,   /* lockstep input, worthless late */
    WB_NET_QOS_CLASS_COUNT     = 4
};

/* DSCP for a class. EF for the input a peer is waiting on, AF41 for state,
 * CS1/CS0 for the rest. */
uint8_t wb_net_qos_class_dscp(int cls);

/* WireGuard's discipline: the outer marking carries the inner one, so a
 * tunnel does not hide it. `inner` is the encapsulated packet's first octets.
 * Falls back to the class DSCP when there is nothing to read. */
uint8_t wb_net_qos_inner_dscp(const uint8_t *inner, size_t inner_len, int cls);

/* --- the shaper --- */

/* Wrap `inner`. `queue_bytes` bounds what may wait for tokens, per peer;
 * 0 means a reasonable default. Returns NULL on allocation failure. */
wb_net_qos *wb_net_qos_new(wb_net_transport *inner, size_t queue_bytes);
void wb_net_qos_free(wb_net_qos *q);

/* Per-peer cap. rate 0 disables shaping for that peer. */
void wb_net_qos_set_rate(wb_net_qos *q, int peer, double bytes_per_sec,
                         double burst_bytes);
double wb_net_qos_rate(const wb_net_qos *q, int peer);
double wb_net_qos_tokens(const wb_net_qos *q, int peer);

/* Per-peer DSCP: mark by class, or copy from the inner packet. `dscp_override`
 * disables both and forces a single value (0 disables the override). */
void wb_net_qos_set_class(wb_net_qos *q, int peer, int cls);
void wb_net_qos_set_copy_inner(wb_net_qos *q, int peer, int enable);
void wb_net_qos_set_override(wb_net_qos *q, int peer, uint8_t dscp);
uint8_t wb_net_qos_dscp(const wb_net_qos *q, int peer, const uint8_t *inner,
                        size_t inner_len);

/* Queue a datagram. 0 if it was sent or queued; -1 if the peer is bad or the
 * queue is full (the newest datagram is the one dropped). */
int wb_net_qos_send(wb_net_qos *q, int peer, const wb_net_datagram *d);

/* Deliver whatever has arrived. */
int wb_net_qos_poll(wb_net_qos *q, wb_net_recv_fn cb, void *ud);

/* Advance the buckets and flush what they now afford. */
void wb_net_qos_tick(wb_net_qos *q, double dt);

/* Bytes queued, and bytes dropped by the cap, per peer. */
size_t wb_net_qos_queued(const wb_net_qos *q, int peer);
unsigned long long wb_net_qos_dropped(const wb_net_qos *q, int peer);

#ifdef __cplusplus
}
#endif

#endif /* WB_NET_QOS_H */
