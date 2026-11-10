/*
 * wb_netmodel.h -- deterministic network model (LP-32 stone 1).
 *
 * Netcode is only as good as the network it was tested against, and a test that
 * runs on a clean loopback proves nothing. This model injects latency, jitter,
 * loss and reordering deterministically (a seeded PRNG), so a netcode change
 * can be exercised against the same worst case every build.
 *
 * C11, no deps.
 */
#ifndef WB_NETMODEL_H
#define WB_NETMODEL_H

#include <stdint.h>

typedef struct {
    float    latency;   /* base one-way delay, in ticks */
    float    jitter;    /* +/- random delay, in ticks */
    float    loss;      /* 0..1 drop probability */
    uint32_t seed;      /* the PRNG seed */
} wb_netmodel_cfg;

typedef struct {
    uint32_t seq;
    double   deliver_at;
} wb_netmodel_packet;

typedef struct {
    wb_netmodel_cfg    cfg;
    wb_netmodel_packet *pkts;
    uint32_t           n, cap;
    uint32_t           rng;
} wb_netmodel;

void wb_netmodel_init(wb_netmodel *m, const wb_netmodel_cfg *cfg);
void wb_netmodel_free(wb_netmodel *m);
void wb_netmodel_clear(wb_netmodel *m);

/* Queue a packet sent at `now`. Returns 1 if it entered the network, 0 if the
 * model dropped it. */
int  wb_netmodel_send(wb_netmodel *m, uint32_t seq, double now);

/* Deliver the earliest packet whose time has come (FIFO by delivery time).
 * Returns 1 and writes its sequence number, or 0 when nothing is due. */
int  wb_netmodel_poll(wb_netmodel *m, double now, uint32_t *seq);

uint32_t wb_netmodel_pending(const wb_netmodel *m);

#endif /* WB_NETMODEL_H */
