/*
 * wb_net_reliable.c -- sequencing, acks and retransmission. See
 * wb_net_reliable.h.
 *
 * Wire format (16 bytes, little-endian fields written one at a time so no
 * padding or host layout leaks into it):
 *
 *   uint32 seq       sender's sequence for this message (REL/RED)
 *   uint32 ack       cumulative: every seq <= ack was received
 *   uint32 ack_bits  bit i set => seq ack+i+1 was received
 *   uint8  cls       WB_NET_CLASS_*
 *   uint8  flags     bit0: ack-only (no payload, nothing to deliver)
 *   uint16 len       payload length
 *
 * Sequence numbers are 32-bit and compared with wraparound-aware differences;
 * the window is far smaller than 2^31, so the usual `int32_t(a-b)` test is
 * enough.
 */
#include "wb_net_reliable.h"

#include <stdlib.h>
#include <string.h>

#define WB_NET_REL_PENDING   32      /* per peer, awaiting an ack */
#define WB_NET_REL_RX_QUEUE  128     /* per peer, arrived out of order */
#define WB_NET_RED_WINDOW    16      /* recent inputs repeated in every packet */
#define WB_NET_RED_REC_HDR   6       /* uint32 seq + uint16 len */
/* Backoff ceiling, in doublings: 16x the base RTO. A higher ceiling makes a
 * link recovery-ill; a message the wire keeps dropping must stay cheap to
 * retry, not fade out. */
#define WB_NET_REL_RTO_MAX   4

typedef struct {
    int      used;
    uint32_t seq;
    uint16_t len;
    uint8_t  data[WB_NET_MAX_PAYLOAD];
    int      retries;
    double   at;                    /* retransmit at this time */
} wb_net_rel_pend;

typedef struct {
    int      used;
    uint32_t seq;
    uint16_t len;
    uint8_t  data[WB_NET_MAX_PAYLOAD];
} wb_net_rel_rxq;

/* A RED message is a run of records: the last `window` inputs, oldest first.
 * A packet that gets through therefore catches the receiver up no matter which
 * earlier packets were lost -- redundancy replaces the ack. */
typedef struct {
    int      used;
    uint32_t seq;
    uint16_t len;
    uint8_t  data[WB_NET_MAX_PAYLOAD];
} wb_net_red_rec;

typedef struct {
    uint32_t  recv_next;            /* next REL seq to deliver */
    uint32_t  red_next;             /* next RED seq to deliver */
    int       ack_dirty;            /* received something; ack on the next tick */
    wb_net_rel_rxq q[WB_NET_REL_RX_QUEUE];
} wb_net_rel_rx;

struct wb_net_reliable {
    wb_net_transport        *raw;
    wb_net_reliable_recv_fn  cb;
    void                    *ud;
    double                   now, rto;
    uint32_t                 next_seq;
    wb_net_rel_pend          pending[WB_NET_MAX_PEERS][WB_NET_REL_PENDING];
    wb_net_rel_rx            rx[WB_NET_MAX_PEERS];
    wb_net_red_rec           red_hist[WB_NET_MAX_PEERS][WB_NET_RED_WINDOW];
    uint64_t                 sent, resent, delivered;
};

/* ------------------------------------------------------------ internals */

static int rel_valid_peer(int peer) {
    return peer >= 0 && peer < WB_NET_MAX_PEERS;
}

/* Ack the peer's progress against our pending ring. */
static void rel_ack(wb_net_reliable *r, int peer, uint32_t ack, uint32_t bits) {
    wb_net_rel_pend *p = r->pending[peer];
    for (int i = 0; i < WB_NET_REL_PENDING; i++) {
        if (!p[i].used) continue;
        if ((int32_t)(p[i].seq - ack) <= 0) { p[i].used = 0; continue; }
        uint32_t d = p[i].seq - ack - 1u;
        if (d < 32u && (bits & (1u << d))) p[i].used = 0;
    }
}

static int rel_queue_rx(wb_net_rel_rx *rx, uint32_t seq,
                        const uint8_t *data, uint16_t len) {
    for (int i = 0; i < WB_NET_REL_RX_QUEUE; i++)
        if (rx->q[i].used && rx->q[i].seq == seq) return 0;   /* duplicate */
    for (int i = 0; i < WB_NET_REL_RX_QUEUE; i++) {
        if (rx->q[i].used) continue;
        rx->q[i].used = 1;
        rx->q[i].seq = seq;
        rx->q[i].len = len;
        memcpy(rx->q[i].data, data, len);
        return 0;
    }
    return -1;                      /* window full: drop, the sender resends */
}

static void rel_deliver(wb_net_reliable *r, int peer, wb_net_class cls,
                        const uint8_t *data, uint16_t len) {
    r->delivered++;
    if (r->cb) r->cb(r->ud, peer, cls, data, len);
}

/* Emit one wire datagram: header (with our ack progress for this peer) plus
 * payload. Used for the first send and for every retransmit. */
static int rel_emit(wb_net_reliable *r, int peer, wb_net_class cls,
                    uint32_t seq, const uint8_t *data, uint16_t len) {
    wb_net_datagram d;
    d.len = (uint16_t)(16 + len);
    wb_net_put_u32(d.data + 0, seq);
    wb_net_put_u32(d.data + 4, r->rx[peer].recv_next - 1u);   /* cumulative ack */
    wb_net_put_u32(d.data + 8, 0u);                            /* no selective ack */
    d.data[12] = (uint8_t)cls;
    d.data[13] = 0u;
    wb_net_put_u16(d.data + 14, len);
    if (len) memcpy(d.data + 16, data, len);
    return r->raw->send(r->raw, peer, &d);
}

static int rel_send_ack(wb_net_reliable *r, int peer) {
    wb_net_datagram d;
    d.len = 16;
    wb_net_put_u32(d.data + 0, 0u);
    wb_net_put_u32(d.data + 4, r->rx[peer].recv_next - 1u);
    wb_net_put_u32(d.data + 8, 0u);
    d.data[12] = (uint8_t)WB_NET_CLASS_UNS;
    d.data[13] = 0x01u;                     /* ack-only */
    wb_net_put_u16(d.data + 14, 0u);
    return r->raw->send(r->raw, peer, &d);
}

/* --------------------------------------------------------- sender side */

int wb_net_reliable_send(wb_net_reliable *r, int peer, wb_net_class cls,
                         const void *data, uint16_t len) {
    if (!r || !rel_valid_peer(peer)) return -1;
    if (len > WB_NET_MAX_PAYLOAD) return -1;
    if (len && !data) return -1;

    if (cls == WB_NET_CLASS_UNS) {
        r->sent++;
        return rel_emit(r, peer, cls, 0u, data, len);
    }
    if (cls == WB_NET_CLASS_RED) {
        /* A record carries its own 6-byte header, so a RED message must fit
         * the payload budget with that header included. */
        if (len > WB_NET_MAX_PAYLOAD - WB_NET_RED_REC_HDR) return -1;
        uint32_t seq = r->next_seq++;
        wb_net_red_rec *hist = r->red_hist[peer];

        int slot = -1, oldest = 0;
        for (int i = 0; i < WB_NET_RED_WINDOW; i++) {
            if (!hist[i].used) { slot = i; break; }
            if ((int32_t)(hist[i].seq - hist[oldest].seq) < 0) oldest = i;
        }
        if (slot < 0) slot = oldest;            /* ring full: overwrite oldest */
        hist[slot].used = 1;
        hist[slot].seq = seq;
        hist[slot].len = len;
        memcpy(hist[slot].data, data, len);

        /* The payload is the window itself, OLDEST FIRST -- the receiver walks
         * it monotonically, so the records must be in sequence order. Older
         * records are skipped when the budget runs out; the newest is carried. */
        wb_net_red_rec *used[WB_NET_RED_WINDOW];
        int nused = 0;
        for (int i = 0; i < WB_NET_RED_WINDOW; i++)
            if (hist[i].used) used[nused++] = &hist[i];
        for (int i = 1; i < nused; i++) {
            wb_net_red_rec *k = used[i];
            int j = i - 1;
            while (j >= 0 && (int32_t)(used[j]->seq - k->seq) > 0) {
                used[j + 1] = used[j];
                j--;
            }
            used[j + 1] = k;
        }
        uint8_t payload[WB_NET_MAX_PAYLOAD];
        uint16_t plen = 0;
        for (int i = 0; i < nused; i++) {
            uint16_t need = (uint16_t)(WB_NET_RED_REC_HDR + used[i]->len);
            if (plen + need > WB_NET_MAX_PAYLOAD && plen > 0) continue;
            if (plen + need > WB_NET_MAX_PAYLOAD) break;
            wb_net_put_u32(payload + plen + 0, used[i]->seq);
            wb_net_put_u16(payload + plen + 4, used[i]->len);
            memcpy(payload + plen + WB_NET_RED_REC_HDR, used[i]->data, used[i]->len);
            plen += need;
        }
        r->sent++;
        return rel_emit(r, peer, cls, seq, payload, plen);
    }
    /* REL: assign a seq, keep it until acked, then transmit. */
    uint32_t seq = r->next_seq++;
    wb_net_rel_pend *p = r->pending[peer];
    int slot = -1;
    for (int i = 0; i < WB_NET_REL_PENDING; i++) {
        if (!p[i].used) { slot = i; break; }
    }
    if (slot < 0) return -1;                /* outstanding window full */
    p[slot].used = 1;
    p[slot].seq = seq;
    p[slot].len = len;
    if (len) memcpy(p[slot].data, data, len);
    p[slot].retries = 0;
    p[slot].at = r->now + r->rto;
    r->sent++;
    return rel_emit(r, peer, cls, seq, data, len);
}

/* --------------------------------------------------------- receiver side */

/* Deliver the records of a RED payload in sequence order. A record behind
 * `red_next` is a repeat; one ahead of it is a gap this packet cannot fill, so
 * the rest of the packet is dropped and a later window will fill it. */
static void rel_recv_red(wb_net_reliable *r, int peer, const uint8_t *payload,
                         uint16_t len) {
    wb_net_rel_rx *rx = &r->rx[peer];
    uint16_t off = 0;
    while (off + WB_NET_RED_REC_HDR <= len) {
        uint32_t seq = wb_net_get_u32(payload + off);
        uint16_t rlen = wb_net_get_u16(payload + off + 4);
        off += WB_NET_RED_REC_HDR;
        if (off + rlen > len) break;                /* truncated record */
        if ((int32_t)(seq - rx->red_next) < 0) {    /* already delivered */
            off += rlen;
            continue;
        }
        if (seq != rx->red_next) break;             /* gap: stop here */
        rel_deliver(r, peer, WB_NET_CLASS_RED, payload + off, rlen);
        rx->red_next++;
        off += rlen;
    }
}

static void rel_recv(void *ud, int peer, const wb_net_datagram *d) {
    wb_net_reliable *r = (wb_net_reliable *)ud;
    if (!r || !d || d->len < 16) return;
    if (d->len > 16 + WB_NET_MAX_PAYLOAD) return;

    const uint8_t *b = d->data;
    uint32_t seq = wb_net_get_u32(b + 0);
    uint32_t ack = wb_net_get_u32(b + 4);
    uint32_t bits = wb_net_get_u32(b + 8);
    wb_net_class cls = (wb_net_class)b[12];
    int ack_only = (b[13] & 0x01u) != 0;
    uint16_t len = wb_net_get_u16(b + 14);
    if (len != (uint16_t)(d->len - 16)) return;    /* truncated */

    rel_ack(r, peer, ack, bits);
    if (ack_only) return;

    const uint8_t *payload = b + 16;
    wb_net_rel_rx *rx = &r->rx[peer];

    /* Every REL packet -- including a retransmitted duplicate of one already
     * delivered -- asks for an ack. The ack itself may have been lost, and the
     * sender's retransmit is the only hint we get that it needs to hear it
     * again; a duplicate that stayed silent could wedge the link for good. */
    if (cls == WB_NET_CLASS_REL) rx->ack_dirty = 1;

    switch (cls) {
    case WB_NET_CLASS_UNS:
        rel_deliver(r, peer, cls, payload, len);
        break;
    case WB_NET_CLASS_RED:
        rel_recv_red(r, peer, payload, len);
        break;
    case WB_NET_CLASS_REL:
        if ((int32_t)(seq - rx->recv_next) < 0) break;      /* already had it */
        if (seq == rx->recv_next) {
            rel_deliver(r, peer, cls, payload, len);
            rx->recv_next++;
            /* Drain whatever the gap had been hiding. */
            for (;;) {
                int found = -1;
                for (int i = 0; i < WB_NET_REL_RX_QUEUE; i++)
                    if (rx->q[i].used && rx->q[i].seq == rx->recv_next) {
                        found = i; break;
                    }
                if (found < 0) break;
                rel_deliver(r, peer, cls, rx->q[found].data, rx->q[found].len);
                rx->q[found].used = 0;
                rx->recv_next++;
            }
        } else {
            rel_queue_rx(rx, seq, payload, len);
        }
        break;
    }
}

int wb_net_reliable_poll(wb_net_reliable *r, wb_net_reliable_recv_fn cb,
                         void *ud) {
    if (!r) return 0;
    r->cb = cb;
    r->ud = ud;
    return r->raw->poll(r->raw, rel_recv, r);
}

void wb_net_reliable_tick(wb_net_reliable *r, double dt) {
    if (!r) return;
    r->now += dt;
    if (dt > 0.0) r->raw->tick(r->raw, dt);
    for (int peer = 0; peer < WB_NET_MAX_PEERS; peer++) {
        wb_net_rel_pend *p = r->pending[peer];
        for (int i = 0; i < WB_NET_REL_PENDING; i++) {
            if (!p[i].used || r->now < p[i].at) continue;
            if (rel_emit(r, peer, WB_NET_CLASS_REL, p[i].seq,
                         p[i].data, p[i].len) == 0) {
                p[i].retries++;
                int shift = p[i].retries < WB_NET_REL_RTO_MAX
                            ? p[i].retries : WB_NET_REL_RTO_MAX;
                p[i].at = r->now + r->rto * (double)(1 << shift);
                r->resent++;
            }
        }
        /* An ack-only datagram: no payload, nothing to deliver, and it may
         * itself be lost -- the sender's retransmit is the backstop. */
        if (r->rx[peer].ack_dirty) {
            r->rx[peer].ack_dirty = 0;
            rel_send_ack(r, peer);
        }
    }
}

/* ------------------------------------------------------------ telemetry */

uint64_t wb_net_reliable_sent(const wb_net_reliable *r) {
    return r ? r->sent : 0;
}
uint64_t wb_net_reliable_resent(const wb_net_reliable *r) {
    return r ? r->resent : 0;
}
uint64_t wb_net_reliable_delivered(const wb_net_reliable *r) {
    return r ? r->delivered : 0;
}
uint64_t wb_net_reliable_pending(const wb_net_reliable *r) {
    if (!r) return 0;
    uint64_t n = 0;
    for (int peer = 0; peer < WB_NET_MAX_PEERS; peer++)
        for (int i = 0; i < WB_NET_REL_PENDING; i++)
            if (r->pending[peer][i].used) n++;
    return n;
}

wb_net_reliable *wb_net_reliable_new(wb_net_transport *raw, double rto) {
    if (!raw || rto <= 0.0) return NULL;
    wb_net_reliable *r = (wb_net_reliable *)calloc(1, sizeof *r);
    if (!r) return NULL;
    r->raw = raw;
    r->rto = rto;
    /* Seq 0 is reserved as "no sequence": ack 0 means nothing received, so
     * both ends start at 1. */
    r->next_seq = 1;
    for (int peer = 0; peer < WB_NET_MAX_PEERS; peer++) {
        r->rx[peer].recv_next = 1;
        r->rx[peer].red_next = 1;
    }
    return r;
}

void wb_net_reliable_destroy(wb_net_reliable *r) { free(r); }
