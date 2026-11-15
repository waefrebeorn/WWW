/*
 * wb_net_frag.c -- LP-94: fragmentation, reassembly, MTU/PSN discipline.
 */
#include "wb_net_frag.h"

#include <stdlib.h>
#include <string.h>

#define WB_NET_FRAG_TYPE_WHOLE 0u
#define WB_NET_FRAG_TYPE_FRAG  1u
#define WB_NET_FRAG_HDR       7u   /* type + PSN + index + count */

typedef struct {
    uint16_t psn;
    uint16_t count;
    uint16_t payload;      /* bytes per fragment, from the sender's MTU */
    uint16_t received;
    uint16_t total;
    uint32_t seen[(WB_NET_FRAG_MAX_FRAGS + 31u) / 32u];
    uint8_t  buf[WB_NET_MAX_WIRE];
} wb_net_frag_group;

struct wb_net_frag {
    wb_net_transport *inner;
    uint16_t mtu;
    uint16_t next_psn;
    wb_net_frag_group groups[WB_NET_MAX_PEERS];
};

/* 16-bit modular ordering: a is before b if a-b, read as signed, is negative.
 * Only meaningful within half the PSN space, which is exactly the window a
 * reassembly layer needs. */
static int frag_psn_less(uint16_t a, uint16_t b) {
    return (int16_t)(a - b) < 0;
}

wb_net_frag *wb_net_frag_new(wb_net_transport *inner, uint16_t mtu) {
    if (!inner) return NULL;
    wb_net_frag *f = (wb_net_frag *)calloc(1, sizeof *f);
    if (!f) return NULL;
    f->inner = inner;
    if (mtu == 0) mtu = (uint16_t)WB_NET_MTU_CONSERVATIVE;
    /* The datagram type is a fixed 16 + 1200 bytes, so that budget binds
     * before the network's 1472 does. Grow WB_NET_MAX_WIRE to use more. */
    if (mtu > WB_NET_MAX_WIRE) mtu = (uint16_t)WB_NET_MAX_WIRE;
    if (mtu < WB_NET_FRAG_HDR + 1) mtu = (uint16_t)(WB_NET_FRAG_HDR + 1);
    f->mtu = mtu;
    return f;
}

void wb_net_frag_free(wb_net_frag *f) { free(f); }

uint16_t wb_net_frag_mtu(const wb_net_frag *f) { return f->mtu; }

void wb_net_frag_tick(wb_net_frag *f, double dt) { f->inner->tick(f->inner, dt); }

int wb_net_frag_send(wb_net_frag *f, int peer, const wb_net_datagram *d) {
    if (!f || !d) return -1;
    if (peer < 0 || peer >= WB_NET_MAX_PEERS) return -1;
    if (d->len > WB_NET_MAX_WIRE) return -1;

    wb_net_datagram out;
    if ((uint32_t)d->len + 1u <= f->mtu) {
        out.len = (uint16_t)(d->len + 1u);
        out.data[0] = (uint8_t)WB_NET_FRAG_TYPE_WHOLE;
        memcpy(out.data + 1, d->data, d->len);
        return f->inner->send(f->inner, peer, &out);
    }

    uint16_t payload = (uint16_t)(f->mtu - WB_NET_FRAG_HDR);
    if (payload == 0) return -1;
    uint32_t nfrag = ((uint32_t)d->len + payload - 1u) / payload;
    if (nfrag == 0 || nfrag > WB_NET_FRAG_MAX_FRAGS) return -1;

    uint16_t psn = f->next_psn++;
    for (uint32_t i = 0; i < nfrag; i++) {
        uint32_t off = (uint32_t)i * payload;
        uint32_t n = (uint32_t)d->len - off;
        if (n > payload) n = payload;
        out.data[0] = (uint8_t)WB_NET_FRAG_TYPE_FRAG;
        wb_net_put_u16(out.data + 1, psn);
        wb_net_put_u16(out.data + 3, (uint16_t)i);
        wb_net_put_u16(out.data + 5, (uint16_t)nfrag);
        memcpy(out.data + WB_NET_FRAG_HDR, d->data + off, n);
        out.len = (uint16_t)(WB_NET_FRAG_HDR + n);
        if (f->inner->send(f->inner, peer, &out) != 0) return -1;
    }
    return 0;
}

/* Pull a completed message out of a group and reset it. Returns 0 if the
 * group is not complete. */
static int frag_take(wb_net_frag *f, int peer, wb_net_datagram *out) {
    wb_net_frag_group *g = &f->groups[peer];
    (void)f;
    if (g->count == 0 || g->received != g->count) return 0;
    if (g->total == 0 || g->total > WB_NET_MAX_WIRE) return 0;
    out->len = g->total;
    memcpy(out->data, g->buf, g->total);
    g->count = 0;
    g->received = 0;
    g->total = 0;
    memset(g->seen, 0, sizeof g->seen);
    return 1;
}

/* Feed one wire datagram. Returns 1 if it completed a message. */
int wb_net_frag_feed(wb_net_frag *f, int peer, const uint8_t *p, uint16_t len,
                     wb_net_datagram *out) {
    if (peer < 0 || peer >= WB_NET_MAX_PEERS) return 0;
    if (len < 1) return 0;

    if (p[0] == WB_NET_FRAG_TYPE_WHOLE) {
        if ((uint32_t)len - 1u > WB_NET_MAX_WIRE) return 0;
        out->len = (uint16_t)(len - 1u);
        memcpy(out->data, p + 1, (size_t)len - 1u);
        return 1;
    }
    if (p[0] != WB_NET_FRAG_TYPE_FRAG) return 0;
    if (len < WB_NET_FRAG_HDR) return 0;

    uint16_t psn = wb_net_get_u16(p + 1);
    uint16_t idx = wb_net_get_u16(p + 3);
    uint16_t cnt = wb_net_get_u16(p + 5);
    if (cnt == 0 || cnt > WB_NET_FRAG_MAX_FRAGS) return 0;
    if (idx >= cnt) return 0;

    wb_net_frag_group *g = &f->groups[peer];
    if (g->count == 0) {
        /* start */
    } else if (g->psn == psn) {
        /* continue */
    } else if (frag_psn_less(psn, g->psn)) {
        return 0;                       /* stale fragment */
    } else {
        g->count = 0;                   /* ahead: abandon the old group */
    }

    if (g->count == 0) {
        g->psn = psn;
        g->count = cnt;
        g->payload = (uint16_t)(f->mtu - WB_NET_FRAG_HDR);
        g->received = 0;
        g->total = 0;
        memset(g->seen, 0, sizeof g->seen);
    } else if (g->count != cnt) {
        return 0;                       /* inconsistent group */
    }

    if (g->seen[idx >> 5] & (1u << (idx & 31))) return 0;   /* duplicate */

    uint16_t n = (uint16_t)(len - WB_NET_FRAG_HDR);
    uint32_t off = (uint32_t)idx * g->payload;
    if (off + n > WB_NET_MAX_WIRE) return 0;
    memcpy(g->buf + off, p + WB_NET_FRAG_HDR, n);
    if (idx == cnt - 1) g->total = (uint16_t)(off + n);

    g->seen[idx >> 5] |= 1u << (idx & 31);
    g->received++;
    return frag_take(f, peer, out);
}

typedef struct {
    wb_net_frag *f;
    wb_net_recv_fn cb;
    void *ud;
} frag_poll_ctx;

static void frag_on_recv(void *ud, int peer, const wb_net_datagram *d) {
    frag_poll_ctx *ctx = (frag_poll_ctx *)ud;
    wb_net_datagram out;
    if (wb_net_frag_feed(ctx->f, peer, d->data, d->len, &out)) {
        ctx->cb(ctx->ud, peer, &out);
    }
}

int wb_net_frag_poll(wb_net_frag *f, wb_net_recv_fn cb, void *ud) {
    frag_poll_ctx ctx;
    ctx.f = f;
    ctx.cb = cb;
    ctx.ud = ud;
    return f->inner->poll(f->inner, frag_on_recv, &ctx);
}
