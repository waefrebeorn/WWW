/*
 * wb_net_transport.c -- the in-process lossy pair. See wb_net_transport.h.
 *
 * This is the transport the tests and local play use, and it is also the
 * reference implementation: any real transport (UDP next) must satisfy exactly
 * this contract -- send returns 0 even when it drops, delivery is ordered, and
 * the clock only moves when the caller ticks it.
 */
#include "wb_net_transport.h"

#include <stdlib.h>
#include <string.h>

#define WB_NET_PAIR_INBOUND 256
#define WB_NET_PAIR_ENDPOINTS 2

typedef struct {
    int      used;
    double   at;                    /* deliver at this local time */
    int      from;
    uint16_t len;
    uint8_t  data[WB_NET_MAX_WIRE];
} wb_net_pair_slot;

typedef struct wb_net_endpoint {
    wb_net_transport       vt;
    struct wb_net_pair    *pair;
    int                    index;
    uint32_t               rng;
    double                 now;
    wb_net_pair_slot       inq[WB_NET_PAIR_INBOUND];
    int                    head, count;
} wb_net_endpoint;

struct wb_net_pair {
    wb_net_endpoint ep[WB_NET_PAIR_ENDPOINTS];
    float           loss;
    double          latency;
    int             refs;
};

static uint32_t pair_rng(uint32_t *s) {
    *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5;
    return *s;
}

static int pair_send(wb_net_transport *t, int peer, const wb_net_datagram *d) {
    wb_net_endpoint *e = (wb_net_endpoint *)t;
    if (!e || !d) return -1;
    if (peer < 0 || peer >= WB_NET_PAIR_ENDPOINTS || peer == e->index) return -1;
    if (d->len == 0 || d->len > WB_NET_MAX_WIRE) return -1;

    struct wb_net_pair *p = e->pair;
    if (p->loss > 0.0f) {
        uint32_t r = pair_rng(&e->rng) & 0xFFFFFFu;
        float f = (float)r / (float)0x1000000u;
        if (f < p->loss) return 0;      /* dropped: the sender cannot tell */
    }

    wb_net_endpoint *dst = &p->ep[peer];
    if (dst->count >= WB_NET_PAIR_INBOUND) return -1;   /* bounded, no overload */

    int slot = (dst->head + dst->count) % WB_NET_PAIR_INBOUND;
    wb_net_pair_slot *s = &dst->inq[slot];
    s->used = 1;
    s->at = dst->now + p->latency;
    s->from = e->index;
    s->len = d->len;
    memcpy(s->data, d->data, d->len);
    dst->count++;
    return 0;
}

static int pair_poll(wb_net_transport *t, wb_net_recv_fn cb, void *ud) {
    wb_net_endpoint *e = (wb_net_endpoint *)t;
    if (!e) return 0;
    int delivered = 0;
    while (e->count > 0) {
        wb_net_pair_slot *s = &e->inq[e->head];
        if (s->at > e->now) break;              /* not due yet */
        wb_net_datagram d;
        d.len = s->len;
        memcpy(d.data, s->data, s->len);
        e->head = (e->head + 1) % WB_NET_PAIR_INBOUND;
        e->count--;
        if (cb) cb(ud, s->from, &d);
        delivered++;
    }
    return delivered;
}

static void pair_tick(wb_net_transport *t, double dt) {
    wb_net_endpoint *e = (wb_net_endpoint *)t;
    if (e && dt > 0.0) e->now += dt;
}

static void pair_destroy(wb_net_transport *t) {
    wb_net_endpoint *e = (wb_net_endpoint *)t;
    if (!e) return;
    struct wb_net_pair *p = e->pair;
    if (p && --p->refs <= 0) free(p);
}

wb_net_transport *wb_net_transport_pair_new(float loss_rate, double latency_s,
                                            wb_net_transport **other) {
    struct wb_net_pair *p = (struct wb_net_pair *)calloc(1, sizeof *p);
    if (!p) return NULL;
    p->loss = loss_rate < 0.0f ? 0.0f : (loss_rate > 1.0f ? 1.0f : loss_rate);
    p->latency = latency_s < 0.0 ? 0.0 : latency_s;
    p->refs = WB_NET_PAIR_ENDPOINTS;
    for (int i = 0; i < WB_NET_PAIR_ENDPOINTS; i++) {
        wb_net_endpoint *e = &p->ep[i];
        e->vt.send = pair_send;
        e->vt.poll = pair_poll;
        e->vt.tick = pair_tick;
        e->vt.destroy = pair_destroy;
        e->vt.impl = e;
        e->pair = p;
        e->index = i;
        /* Distinct, fixed seeds per endpoint: the loss pattern is a property
         * of the pair, not of the run, so a gate that fails can be replayed. */
        e->rng = 0x9E3779B9u ^ ((uint32_t)(i + 1) * 0x85EBCA6Bu);
    }
    if (other) *other = (wb_net_transport *)&p->ep[1];
    return (wb_net_transport *)&p->ep[0];
}
