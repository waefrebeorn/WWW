/*
 * wb_net_qos.c -- LP-97 stone 1: per-client bandwidth cap and DSCP marking.
 */
#include "wb_net_qos.h"

#include <stdlib.h>
#include <string.h>

#define WB_NET_QOS_DEFAULT_QUEUE 65536u
#define WB_NET_QOS_MAX_SLOTS    256

/* DSCP values: EF, AF41, CS1, CS0. */
static const uint8_t WB_NET_QOS_DSCP[WB_NET_QOS_CLASS_COUNT] = {
    0x08u,   /* BACKGROUND: CS1, a queue nobody watches */
    0x00u,   /* BULK: CS0, ordinary best effort */
    0x22u,   /* STATE: AF41, wanted eventually */
    0x2Eu    /* INPUT: EF, worthless late */
};

uint8_t wb_net_qos_class_dscp(int cls) {
    if (cls < 0 || cls >= WB_NET_QOS_CLASS_COUNT) return WB_NET_QOS_DSCP[WB_NET_QOS_CLASS_BULK];
    return WB_NET_QOS_DSCP[cls];
}

uint8_t wb_net_qos_inner_dscp(const uint8_t *inner, size_t inner_len, int cls) {
    if (inner && inner_len >= 2) {
        unsigned v = (unsigned)(inner[0] >> 4);
        if (v == 4) {
            /* IPv4: the DSCP is the top six bits of the second octet. An
             * explicit CS0 is a marking too, so it is returned as-is rather
             * than falling back to the class. */
            return (uint8_t)(inner[1] >> 2);
        }
        if (v == 6) {
            /* IPv6: the traffic class straddles the first two octets. */
            return (uint8_t)(((inner[0] & 0x0Fu) << 4) | (inner[1] >> 4));
        }
    }
    return wb_net_qos_class_dscp(cls);
}

/* ------------------------------------------------------------- token bucket */

struct wb_net_bucket {
    double rate;
    double burst;
    double tokens;
};

wb_net_bucket *wb_net_bucket_new(double bytes_per_sec, double burst_bytes) {
    wb_net_bucket *b = (wb_net_bucket *)calloc(1, sizeof *b);
    if (!b) return NULL;
    wb_net_bucket_set(b, bytes_per_sec, burst_bytes);
    return b;
}

void wb_net_bucket_free(wb_net_bucket *b) { free(b); }

void wb_net_bucket_set(wb_net_bucket *b, double bytes_per_sec, double burst_bytes) {
    if (bytes_per_sec < 0.0) bytes_per_sec = 0.0;
    if (burst_bytes < 0.0) burst_bytes = 0.0;
    b->rate = bytes_per_sec;
    b->burst = burst_bytes;
    b->tokens = burst_bytes;            /* starts full: the first snapshot goes */
}

double wb_net_bucket_rate(const wb_net_bucket *b) { return b->rate; }
double wb_net_bucket_burst(const wb_net_bucket *b) { return b->burst; }
double wb_net_bucket_tokens(const wb_net_bucket *b) { return b->tokens; }

void wb_net_bucket_tick(wb_net_bucket *b, double dt) {
    if (dt <= 0.0 || b->rate == 0.0) return;
    b->tokens += b->rate * dt;
    if (b->tokens > b->burst) b->tokens = b->burst;
}

int wb_net_bucket_fits(const wb_net_bucket *b, size_t bytes) {
    if (b->rate == 0.0) return 1;
    return b->tokens >= (double)bytes;
}

int wb_net_bucket_take(wb_net_bucket *b, size_t bytes) {
    if (!wb_net_bucket_fits(b, bytes)) return -1;
    if (b->rate > 0.0) b->tokens -= (double)bytes;
    return 0;
}

double wb_net_bucket_wait(const wb_net_bucket *b, size_t bytes) {
    if (b->rate == 0.0) return 0.0;
    if ((double)bytes > b->burst) return -1.0;
    double need = (double)bytes - b->tokens;
    if (need <= 0.0) return 0.0;
    return need / b->rate;
}

/* ---------------------------------------------------------------- the shaper */

typedef struct {
    wb_net_bucket *bucket;
    int      cls;
    int      copy_inner;
    uint8_t  override;
    int      has_override;

    size_t   q_bytes;
    size_t   q_cap;          /* slot count */
    size_t   q_head, q_count;
    wb_net_datagram *q;
    unsigned long long dropped;
} qos_peer;

struct wb_net_qos {
    wb_net_transport *inner;
    size_t            queue_bytes;
    qos_peer          peers[WB_NET_MAX_PEERS];
};

wb_net_qos *wb_net_qos_new(wb_net_transport *inner, size_t queue_bytes) {
    if (!inner) return NULL;
    if (queue_bytes == 0) queue_bytes = WB_NET_QOS_DEFAULT_QUEUE;
    wb_net_qos *q = (wb_net_qos *)calloc(1, sizeof *q);
    if (!q) return NULL;
    q->inner = inner;
    q->queue_bytes = queue_bytes;
    for (int i = 0; i < WB_NET_MAX_PEERS; i++) {
        qos_peer *p = &q->peers[i];
        p->bucket = wb_net_bucket_new(0.0, 0.0);
        if (!p->bucket) {
            for (int j = 0; j < i; j++) {
                wb_net_bucket_free(q->peers[j].bucket);
                free(q->peers[j].q);
            }
            free(q);
            return NULL;
        }
        p->cls = WB_NET_QOS_CLASS_BULK;
        p->q_cap = WB_NET_QOS_MAX_SLOTS;
        p->q = (wb_net_datagram *)calloc(p->q_cap, sizeof *p->q);
        if (!p->q) {
            wb_net_bucket_free(p->bucket);
            for (int j = 0; j < i; j++) {
                wb_net_bucket_free(q->peers[j].bucket);
                free(q->peers[j].q);
            }
            free(q);
            return NULL;
        }
    }
    return q;
}

void wb_net_qos_free(wb_net_qos *q) {
    if (!q) return;
    for (int i = 0; i < WB_NET_MAX_PEERS; i++) {
        wb_net_bucket_free(q->peers[i].bucket);
        free(q->peers[i].q);
    }
    free(q);
}

void wb_net_qos_set_rate(wb_net_qos *q, int peer, double bytes_per_sec,
                         double burst_bytes) {
    if (!q || peer < 0 || peer >= WB_NET_MAX_PEERS) return;
    wb_net_bucket_set(q->peers[peer].bucket, bytes_per_sec, burst_bytes);
}

double wb_net_qos_rate(const wb_net_qos *q, int peer) {
    if (!q || peer < 0 || peer >= WB_NET_MAX_PEERS) return 0.0;
    return wb_net_bucket_rate(q->peers[peer].bucket);
}

double wb_net_qos_tokens(const wb_net_qos *q, int peer) {
    if (!q || peer < 0 || peer >= WB_NET_MAX_PEERS) return 0.0;
    return wb_net_bucket_tokens(q->peers[peer].bucket);
}

void wb_net_qos_set_class(wb_net_qos *q, int peer, int cls) {
    if (!q || peer < 0 || peer >= WB_NET_MAX_PEERS) return;
    q->peers[peer].cls = cls;
}

void wb_net_qos_set_copy_inner(wb_net_qos *q, int peer, int enable) {
    if (!q || peer < 0 || peer >= WB_NET_MAX_PEERS) return;
    q->peers[peer].copy_inner = enable;
}

void wb_net_qos_set_override(wb_net_qos *q, int peer, uint8_t dscp) {
    if (!q || peer < 0 || peer >= WB_NET_MAX_PEERS) return;
    q->peers[peer].override = dscp;
    q->peers[peer].has_override = dscp != 0;
}

uint8_t wb_net_qos_dscp(const wb_net_qos *q, int peer, const uint8_t *inner,
                        size_t inner_len) {
    if (!q || peer < 0 || peer >= WB_NET_MAX_PEERS) return 0;
    const qos_peer *p = &q->peers[peer];
    if (p->has_override) return p->override;
    if (p->copy_inner && inner && inner_len) {
        return wb_net_qos_inner_dscp(inner, inner_len, p->cls);
    }
    return wb_net_qos_class_dscp(p->cls);
}

size_t wb_net_qos_queued(const wb_net_qos *q, int peer) {
    if (!q || peer < 0 || peer >= WB_NET_MAX_PEERS) return 0;
    return q->peers[peer].q_bytes;
}

unsigned long long wb_net_qos_dropped(const wb_net_qos *q, int peer) {
    if (!q || peer < 0 || peer >= WB_NET_MAX_PEERS) return 0;
    return q->peers[peer].dropped;
}

static int qos_send_now(wb_net_qos *q, int peer, const wb_net_datagram *d) {
    qos_peer *p = &q->peers[peer];
    if (wb_net_bucket_take(p->bucket, d->len) != 0) return -1;
    if (q->inner->send(q->inner, peer, d) != 0) {
        /* The transport refused it. The tokens are spent, which is right: the
         * budget was consumed by an attempt, not by a delivery. */
        return -1;
    }
    return 0;
}

int wb_net_qos_send(wb_net_qos *q, int peer, const wb_net_datagram *d) {
    if (!q || !d) return -1;
    if (peer < 0 || peer >= WB_NET_MAX_PEERS) return -1;
    qos_peer *p = &q->peers[peer];

    if (wb_net_bucket_fits(p->bucket, d->len)) return qos_send_now(q, peer, d);

    /* A datagram larger than the burst can never be shaped: the bucket never
     * reaches it, so queueing it would stall the peer for good. Refuse it. */
    if (p->bucket->rate > 0.0 && (double)d->len > p->bucket->burst) {
        p->dropped++;
        return -1;
    }

    /* Queue it if there is room; the newest datagram is the one dropped, since
     * the oldest is the one a peer is still waiting for. */
    if (p->q_count >= p->q_cap || p->q_bytes + d->len > q->queue_bytes) {
        p->dropped++;
        return -1;
    }
    size_t slot = (p->q_head + p->q_count) % p->q_cap;
    p->q[slot] = *d;
    p->q_count++;
    p->q_bytes += d->len;
    return 0;
}

int wb_net_qos_poll(wb_net_qos *q, wb_net_recv_fn cb, void *ud) {
    return q->inner->poll(q->inner, cb, ud);
}

void wb_net_qos_tick(wb_net_qos *q, double dt) {
    if (!q) return;
    for (int i = 0; i < WB_NET_MAX_PEERS; i++) {
        qos_peer *p = &q->peers[i];
        wb_net_bucket_tick(p->bucket, dt);
        while (p->q_count > 0) {
            wb_net_datagram *head = &p->q[p->q_head];
            if (wb_net_bucket_take(p->bucket, head->len) != 0) break;
            if (q->inner->send(q->inner, i, head) != 0) break;
            p->q_bytes -= head->len;
            p->q_head = (p->q_head + 1) % p->q_cap;
            p->q_count--;
        }
    }
}
