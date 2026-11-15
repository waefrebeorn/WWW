/*
 * wb_net_netem.c -- LP-97 stone 2: a netem-style link emulator.
 */
#include "wb_net_netem.h"

#include <stdlib.h>
#include <string.h>

#define WB_NET_NETEM_MAX_PENDING 1024

typedef struct {
    int      used;
    double   due;
    int      peer;
    uint16_t len;
    uint8_t  data[WB_NET_MAX_WIRE];
} netem_slot;

struct wb_net_netem {
    wb_net_transport *inner;
    uint32_t rng;
    double   now;

    double   delay, jitter, loss, duplicate, corrupt, reorder, rate_kbits;
    double   wire_left;          /* bytes the rate limit still allows */
    double   last_tick;

    netem_slot slots[WB_NET_NETEM_MAX_PENDING];
    int  n;

    unsigned long long dropped, duplicated, corrupted, reordered;
};

wb_net_netem *wb_net_netem_new(wb_net_transport *inner, uint32_t seed) {
    if (!inner) return NULL;
    wb_net_netem *n = (wb_net_netem *)calloc(1, sizeof *n);
    if (!n) return NULL;
    n->inner = inner;
    n->rng = seed ? seed : 0x9E3779B9u;
    n->delay = 0.0;
    n->jitter = 0.0;
    return n;
}

void wb_net_netem_free(wb_net_netem *n) { free(n); }

void wb_net_netem_set(wb_net_netem *n, double delay, double jitter, double loss,
                      double duplicate, double corrupt, double reorder,
                      double rate_kbits) {
    if (!n) return;
    if (delay < 0.0) delay = 0.0;
    if (jitter < 0.0) jitter = 0.0;
    if (jitter > delay) jitter = delay;     /* never a negative latency */
    n->delay = delay;
    n->jitter = jitter;
    n->loss = loss < 0.0 ? 0.0 : (loss > 1.0 ? 1.0 : loss);
    n->duplicate = duplicate < 0.0 ? 0.0 : (duplicate > 1.0 ? 1.0 : duplicate);
    n->corrupt = corrupt < 0.0 ? 0.0 : (corrupt > 1.0 ? 1.0 : corrupt);
    n->reorder = reorder < 0.0 ? 0.0 : (reorder > 1.0 ? 1.0 : reorder);
    n->rate_kbits = rate_kbits < 0.0 ? 0.0 : rate_kbits;
}

size_t wb_net_netem_pending(const wb_net_netem *n) { return (size_t)n->n; }
unsigned long long wb_net_netem_dropped(const wb_net_netem *n) { return n->dropped; }
unsigned long long wb_net_netem_duplicated(const wb_net_netem *n) { return n->duplicated; }
unsigned long long wb_net_netem_corrupted(const wb_net_netem *n) { return n->corrupted; }
unsigned long long wb_net_netem_reordered(const wb_net_netem *n) { return n->reordered; }

static uint32_t netem_rng(uint32_t *s) {
    *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5;
    return *s;
}

static double netem_unit(wb_net_netem *n) {
    return (double)(netem_rng(&n->rng) & 0xFFFFFFu) / (double)0x1000000u;
}

static int netem_put(wb_net_netem *n, int peer, const uint8_t *data, uint16_t len,
                     double due) {
    if (n->n >= WB_NET_NETEM_MAX_PENDING) { n->dropped++; return -1; }
    /* insert by due time, so delivery is in arrival order */
    int i = n->n;
    while (i > 0 && n->slots[i - 1].due > due) {
        n->slots[i] = n->slots[i - 1];
        i--;
    }
    netem_slot *s = &n->slots[i];
    s->used = 1;
    s->due = due;
    s->peer = peer;
    s->len = len;
    memcpy(s->data, data, len);
    n->n++;
    return 0;
}

int wb_net_netem_send(wb_net_netem *n, int peer, const wb_net_datagram *d) {
    if (!n || !d) return -1;
    if (peer < 0 || peer >= WB_NET_MAX_PEERS) return -1;

    if (n->loss > 0.0 && netem_unit(n) < n->loss) {
        n->dropped++;
        return 0;                       /* dropped: the sender cannot tell */
    }

    /* The rate limit is a budget, not a block: a packet waits for the bytes it
     * needs rather than being refused. Bytes on both sides of the comparison --
     * `rate_kbits` is converted once, not per packet. */
    double wait = 0.0;
    double bps = n->rate_kbits * 1000.0 / 8.0;
    if (bps > 0.0) {
        if ((double)d->len > n->wire_left) {
            wait = ((double)d->len - n->wire_left) / bps;
            n->wire_left = 0.0;
        } else {
            n->wire_left -= (double)d->len;
        }
    }

    double span = n->delay - n->jitter;
    if (span < 0.0) span = 0.0;
    double d1 = span + netem_unit(n) * (n->delay + n->jitter - span);

    int copies = 1;
    if (n->duplicate > 0.0 && netem_unit(n) < n->duplicate) {
        copies = 2;
        n->duplicated++;
    }

    for (int c = 0; c < copies; c++) {
        uint8_t buf[WB_NET_MAX_WIRE];
        memcpy(buf, d->data, d->len);
        if (n->corrupt > 0.0 && netem_unit(n) < n->corrupt) {
            buf[netem_rng(&n->rng) % d->len] ^= (uint8_t)(1u << (netem_rng(&n->rng) & 7u));
            n->corrupted++;
        }
        double when = n->now + wait + d1;
        if (n->reorder > 0.0 && netem_unit(n) < n->reorder) {
            when += (n->delay + n->jitter) > 0.0 ? (n->delay + n->jitter) : 0.001;
            n->reordered++;
        }
        if (netem_put(n, peer, buf, d->len, when) != 0) return -1;
    }
    return 0;
}

int wb_net_netem_poll(wb_net_netem *n, wb_net_recv_fn cb, void *ud) {
    if (!n) return 0;
    int delivered = 0;
    while (n->n > 0 && n->slots[0].due <= n->now) {
        netem_slot *s = &n->slots[0];
        wb_net_datagram d;
        d.len = s->len;
        memcpy(d.data, s->data, s->len);
        if (cb) cb(ud, s->peer, &d);
        delivered++;
        for (int i = 1; i < n->n; i++) n->slots[i - 1] = n->slots[i];
        n->n--;
    }
    return delivered;
}

void wb_net_netem_tick(wb_net_netem *n, double dt) {
    if (!n || dt <= 0.0) return;
    n->now += dt;
    if (n->rate_kbits > 0.0) {
        double bps = n->rate_kbits * 1000.0 / 8.0;
        n->wire_left += bps * dt;
        /* the budget is one second of burst, not an unbounded accumulation */
        if (n->wire_left > bps) n->wire_left = bps;
    }
}
