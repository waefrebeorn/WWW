/*
 * wb_turn.c -- LP-95 stone 3: TURN (RFC 8656).
 */
#include "wb_turn.h"

#include <stdlib.h>
#include <string.h>

#define WB_TURN_MAX_ALLOCATIONS 32
#define WB_TURN_MAX_PERMISSIONS 32
#define WB_TURN_MAX_CHANNELS    64

static void be16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v);
}

static void be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t rd32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* Start a STUN-family message. Returns WB_STUN_HEADER, or 0 if it does not fit. */
static size_t turn_start(uint8_t *out, size_t cap, uint16_t type,
                         const uint8_t txid[WB_STUN_TXID_LEN]) {
    if (!out || cap < WB_STUN_HEADER) return 0;
    be16(out, type);
    be16(out + 2, 0);                       /* filled in by turn_end */
    be32(out + 4, WB_STUN_MAGIC_COOKIE);
    memcpy(out + 8, txid, WB_STUN_TXID_LEN);
    return WB_STUN_HEADER;
}

static size_t turn_end(uint8_t *out, size_t used) {
    be16(out + 2, (uint16_t)(used - WB_STUN_HEADER));
    return used;
}

/* Append a raw attribute, padded to four bytes. */
static size_t turn_attr(uint8_t *out, size_t used, size_t cap, uint16_t type,
                        const void *val, size_t vlen) {
    size_t need = 4u + vlen;
    size_t pad = (4u - (vlen % 4u)) % 4u;
    if (cap < used + need + pad) return used;           /* does not fit */
    be16(out + used, type);
    be16(out + used + 2, (uint16_t)vlen);
    if (vlen) memcpy(out + used + 4, val, vlen);
    memset(out + used + 4 + vlen, 0, pad);
    return used + need + pad;
}

int wb_turn_is_request(uint16_t type) {
    return (type & 0x0110) == 0x0000;
}

int wb_turn_is_success(uint16_t type) {
    return (type & 0x0110) == 0x0100;
}

int wb_turn_is_error(uint16_t type) {
    return (type & 0x0110) == 0x0110;
}

int wb_turn_is_indication(uint16_t type) {
    return (type & 0x0010) != 0 && !wb_turn_is_success(type) && !wb_turn_is_error(type);
}

/* ---- the client side ---- */

size_t wb_turn_build_allocate(uint8_t *out, size_t cap,
                              const uint8_t txid[WB_STUN_TXID_LEN], int udp) {
    size_t n = turn_start(out, cap, WB_TURN_ALLOCATE_REQUEST, txid);
    if (!n) return 0;
    uint32_t transport = udp ? WB_TURN_TRANSPORT_UDP : 6u;
    uint8_t tb[4];
    be32(tb, transport);
    n = turn_attr(out, n, cap, WB_TURN_ATTR_REQUESTED_TRANSPORT, tb, 4);
    return turn_end(out, n);
}

size_t wb_turn_build_refresh(uint8_t *out, size_t cap,
                             const uint8_t txid[WB_STUN_TXID_LEN],
                             uint32_t lifetime) {
    size_t n = turn_start(out, cap, WB_TURN_REFRESH_REQUEST, txid);
    if (!n) return 0;
    uint8_t lb[4];
    be32(lb, lifetime);
    n = turn_attr(out, n, cap, WB_TURN_ATTR_LIFETIME, lb, 4);
    return turn_end(out, n);
}

size_t wb_turn_build_create_permission(uint8_t *out, size_t cap,
                                       const uint8_t txid[WB_STUN_TXID_LEN],
                                       const wb_stun_addr *peer) {
    size_t n = turn_start(out, cap, WB_TURN_CREATE_PERMISSION_REQUEST, txid);
    if (!n) return 0;
    uint8_t val[20];
    size_t v = wb_stun_put_addr(val, WB_TURN_ATTR_XOR_PEER_ADDRESS, 1, txid, peer);
    (void)v;
    n = turn_attr(out, n, cap, WB_TURN_ATTR_XOR_PEER_ADDRESS, val + 4,
                  (peer->family == 6) ? 20u : 8u);
    return turn_end(out, n);
}

size_t wb_turn_build_channel_bind(uint8_t *out, size_t cap,
                                  const uint8_t txid[WB_STUN_TXID_LEN],
                                  uint16_t channel, const wb_stun_addr *peer) {
    size_t n = turn_start(out, cap, WB_TURN_CHANNEL_BIND_REQUEST, txid);
    if (!n) return 0;
    /* RFC 8656 section 12: the value is four bytes -- the channel number then
     * two reserved octets. */
    uint8_t cval[4] = {(uint8_t)(channel >> 8), (uint8_t)channel, 0, 0};
    uint8_t val[20];
    wb_stun_put_addr(val, WB_TURN_ATTR_XOR_PEER_ADDRESS, 1, txid, peer);
    n = turn_attr(out, n, cap, WB_TURN_ATTR_CHANNEL_NUMBER, cval, 4);
    n = turn_attr(out, n, cap, WB_TURN_ATTR_XOR_PEER_ADDRESS, val + 4,
                  (peer->family == 6) ? 20u : 8u);
    return turn_end(out, n);
}

size_t wb_turn_build_send(uint8_t *out, size_t cap,
                          const wb_stun_addr *peer,
                          const void *data, size_t len) {
    /* An indication has no transaction of its own; the transaction ID field is
     * still present and still zero. */
    uint8_t zero_txid[WB_STUN_TXID_LEN] = {0};
    size_t n = turn_start(out, cap, WB_TURN_SEND_INDICATION, zero_txid);
    if (!n) return 0;
    uint8_t val[20];
    wb_stun_put_addr(val, WB_TURN_ATTR_XOR_PEER_ADDRESS, 1, zero_txid, peer);
    n = turn_attr(out, n, cap, WB_TURN_ATTR_XOR_PEER_ADDRESS, val + 4,
                  (peer->family == 6) ? 20u : 8u);
    n = turn_attr(out, n, cap, WB_TURN_ATTR_DATA, data, len);
    return turn_end(out, n);
}

int wb_turn_parse_allocate_success(const uint8_t *buf, size_t len,
                                   wb_stun_addr *relayed, uint32_t *lifetime) {
    uint16_t type;
    const uint8_t *txid;
    if (wb_stun_parse_header(buf, len, &type, &txid) != 0) return -1;
    if (type != WB_TURN_ALLOCATE_SUCCESS) return -1;

    int found_addr = 0, found_life = 0;
    size_t p = WB_STUN_HEADER;
    while (p + 4 <= len) {
        uint16_t at = rd16(buf + p);
        uint16_t al = rd16(buf + p + 2);
        if (p + 4 + al > len) return -1;
        if (at == WB_TURN_ATTR_XOR_RELAYED_ADDRESS && !found_addr) {
            if (wb_stun_get_addr(buf + p + 4, al, 1, txid, relayed) != 0) return -1;
            found_addr = 1;
        } else if (at == WB_TURN_ATTR_LIFETIME && al == 4 && !found_life) {
            *lifetime = rd32(buf + p + 4);
            found_life = 1;
        }
        p += 4 + al;
        while (p % 4 != 0) p++;
    }
    return (found_addr && found_life) ? 0 : -1;
}

/* ---- the server side ---- */

typedef struct {
    int      used;
    wb_stun_addr peer;
    uint32_t expires;
} turn_permission;

typedef struct {
    int      used;
    uint16_t channel;
    wb_stun_addr peer;
    uint32_t expires;
} turn_channel;

typedef struct {
    int           used;
    int           peer;
    wb_stun_addr  relayed;
    uint32_t      expires;
    turn_permission perms[WB_TURN_MAX_PERMISSIONS];
    int           n_perms;
    turn_channel  chans[WB_TURN_MAX_CHANNELS];
    int           n_chans;
} turn_allocation;

struct wb_turn_server {
    wb_stun_addr relay_base;
    uint16_t     next_port;
    uint32_t     lifetime;

    double       now;
    turn_allocation allocs[WB_TURN_MAX_ALLOCATIONS];
    int          n_allocs;

    unsigned long long forwards;
    unsigned long long refused;
};

wb_turn_server *wb_turn_server_new(const wb_stun_addr *relay_base,
                                   uint16_t relay_port_base,
                                   uint32_t lifetime) {
    if (!relay_base) return NULL;
    wb_turn_server *s = (wb_turn_server *)calloc(1, sizeof *s);
    if (!s) return NULL;
    s->relay_base = *relay_base;
    s->next_port = relay_port_base;
    s->lifetime = lifetime ? lifetime : WB_TURN_LIFETIME_DEFAULT;
    return s;
}

void wb_turn_server_free(wb_turn_server *s) { free(s); }

unsigned long long wb_turn_server_allocations(const wb_turn_server *s) {
    return (unsigned long long)s->n_allocs;
}
unsigned long long wb_turn_server_permissions(const wb_turn_server *s) {
    unsigned long long n = 0;
    for (int i = 0; i < s->n_allocs; i++) n += (unsigned long long)s->allocs[i].n_perms;
    return n;
}
unsigned long long wb_turn_server_forwards(const wb_turn_server *s) { return s->forwards; }
unsigned long long wb_turn_server_refused(const wb_turn_server *s) { return s->refused; }

static turn_allocation *find_alloc(wb_turn_server *s, int peer) {
    for (int i = 0; i < s->n_allocs; i++) {
        if (s->allocs[i].used && s->allocs[i].peer == peer) return &s->allocs[i];
    }
    return NULL;
}

static turn_allocation *new_alloc(wb_turn_server *s, int peer) {
    if (find_alloc(s, peer)) return find_alloc(s, peer);
    for (int i = 0; i < WB_TURN_MAX_ALLOCATIONS; i++) {
        if (s->allocs[i].used) continue;
        turn_allocation *a = &s->allocs[i];
        memset(a, 0, sizeof *a);
        a->used = 1;
        a->peer = peer;
        a->relayed = s->relay_base;
        a->relayed.port = s->next_port++;
        a->expires = (uint32_t)(s->now + s->lifetime);
        s->n_allocs++;
        return a;
    }
    return NULL;
}

static void drop_expired(wb_turn_server *s) {
    for (int i = 0; i < WB_TURN_MAX_ALLOCATIONS; i++) {
        turn_allocation *a = &s->allocs[i];
        if (!a->used) continue;
        if ((double)a->expires <= s->now) {
            a->used = 0;
            s->n_allocs--;
            continue;
        }
        int w = 0;
        for (int j = 0; j < a->n_perms; j++) {
            if ((double)a->perms[j].expires > s->now) a->perms[w++] = a->perms[j];
        }
        a->n_perms = w;
        w = 0;
        for (int j = 0; j < a->n_chans; j++) {
            if ((double)a->chans[j].expires > s->now) a->chans[w++] = a->chans[j];
        }
        a->n_chans = w;
    }
}

void wb_turn_server_tick(wb_turn_server *s, double dt) {
    if (!s || dt <= 0.0) return;
    s->now += dt;
    drop_expired(s);
}

static int has_permission(turn_allocation *a, const wb_stun_addr *peer) {
    for (int i = 0; i < a->n_perms; i++) {
        turn_permission *p = &a->perms[i];
        if (p->peer.family != peer->family || p->peer.port != peer->port) continue;
        size_t n = (peer->family == 6) ? 16u : 4u;
        if (memcmp(p->peer.addr, peer->addr, n) == 0) return 1;
    }
    return 0;
}

/* Walk the attributes of a request, returning the first address of a given type
 * and the lifetime if present. */
static int walk(const uint8_t *buf, size_t len, const uint8_t *txid,
                uint16_t addr_attr, wb_stun_addr *addr, int *found_addr,
                uint32_t *lifetime, int *found_life) {
    size_t p = WB_STUN_HEADER;
    while (p + 4 <= len) {
        uint16_t at = rd16(buf + p);
        uint16_t al = rd16(buf + p + 2);
        if (p + 4 + al > len) return -1;
        if (at == addr_attr && !*found_addr) {
            if (wb_stun_get_addr(buf + p + 4, al, 1, txid, addr) != 0) return -1;
            *found_addr = 1;
        } else if (at == WB_TURN_ATTR_LIFETIME && al == 4 && !*found_life) {
            *lifetime = rd32(buf + p + 4);
            *found_life = 1;
        }
        p += 4 + al;
        while (p % 4 != 0) p++;
    }
    return 0;
}

static size_t build_error(uint8_t *out, size_t cap, uint16_t req_type,
                          const uint8_t txid[WB_STUN_TXID_LEN],
                          unsigned class_, unsigned number) {
    size_t n = turn_start(out, cap, (uint16_t)(req_type | 0x0110), txid);
    if (!n) return 0;
    uint8_t code[4] = {0, 0, (uint8_t)class_, (uint8_t)number};
    n = turn_attr(out, n, cap, 0x0009u /* ERROR-CODE */, code, 4);
    return turn_end(out, n);
}

int wb_turn_server_handle(wb_turn_server *s, const uint8_t *buf, size_t len,
                          int peer, const wb_stun_addr *observed,
                          wb_turn_send_fn send, void *send_ud,
                          uint8_t *out, size_t cap,
                          wb_turn_deliver_fn deliver, void *deliver_ud) {
    (void)observed;
    if (!s) return 0;
    uint16_t type;
    const uint8_t *txid;
    if (wb_stun_parse_header(buf, len, &type, &txid) != 0) return 0;

    /* ---- indications ---- */
    if (type == WB_TURN_SEND_INDICATION) {
        wb_stun_addr from;
        int have_from = 0;
        const uint8_t *data = NULL;
        size_t dlen = 0;
        size_t p = WB_STUN_HEADER;
        while (p + 4 <= len) {
            uint16_t at = rd16(buf + p);
            uint16_t al = rd16(buf + p + 2);
            if (p + 4 + al > len) return 0;
            if (at == WB_TURN_ATTR_XOR_PEER_ADDRESS && !have_from) {
                if (wb_stun_get_addr(buf + p + 4, al, 1, txid, &from) != 0) return 0;
                have_from = 1;
            } else if (at == WB_TURN_ATTR_DATA && !data) {
                data = buf + p + 4;
                dlen = al;
            }
            p += 4 + al;
            while (p % 4 != 0) p++;
        }
        if (!have_from || !data) return 0;
        /* the relay only forwards from a peer the client asked it to trust */
        turn_allocation *a = find_alloc(s, peer);
        if (!a || !has_permission(a, &from)) {
            s->refused++;
            return 0;
        }
        if (deliver) deliver(deliver_ud, peer, &from, data, dlen);
        s->forwards++;
        return 1;
    }
    if (type == WB_TURN_DATA_INDICATION) {
        /* peer to client: the client that holds a permission for this peer */
        wb_stun_addr from;
        int have_from = 0;
        const uint8_t *data = NULL;
        size_t dlen = 0;
        size_t p = WB_STUN_HEADER;
        while (p + 4 <= len) {
            uint16_t at = rd16(buf + p);
            uint16_t al = rd16(buf + p + 2);
            if (p + 4 + al > len) return 0;
            if (at == WB_TURN_ATTR_XOR_PEER_ADDRESS && !have_from) {
                if (wb_stun_get_addr(buf + p + 4, al, 1, txid, &from) != 0) return 0;
                have_from = 1;
            } else if (at == WB_TURN_ATTR_DATA && !data) {
                data = buf + p + 4;
                dlen = al;
            }
            p += 4 + al;
            while (p % 4 != 0) p++;
        }
        if (!have_from || !data) return 0;
        for (int i = 0; i < WB_TURN_MAX_ALLOCATIONS; i++) {
            turn_allocation *a = &s->allocs[i];
            if (!a->used || !has_permission(a, &from)) continue;
            if (deliver) deliver(deliver_ud, a->peer, &from, data, dlen);
            s->forwards++;
            return 1;
        }
        s->refused++;
        return 0;
    }

    /* ---- requests ---- */
    if (!wb_turn_is_request(type)) return 0;

    if (type == WB_TURN_ALLOCATE_REQUEST) {
        turn_allocation *a = new_alloc(s, peer);
        if (!a) {
            size_t n = build_error(out, cap, type, txid, 5, 0);   /* 500 Server Error */
            if (n && send) send(send_ud, peer, out, n);
            return n ? 1 : 0;
        }
        size_t n = turn_start(out, cap, (uint16_t)(type | 0x0100), txid);
        if (!n) return 0;
        uint8_t val[20];
        wb_stun_put_addr(val, WB_TURN_ATTR_XOR_RELAYED_ADDRESS, 1, txid, &a->relayed);
        n = turn_attr(out, n, cap, WB_TURN_ATTR_XOR_RELAYED_ADDRESS, val + 4,
                      (a->relayed.family == 6) ? 20u : 8u);
        uint8_t lb[4];
        be32(lb, s->lifetime);
        n = turn_attr(out, n, cap, WB_TURN_ATTR_LIFETIME, lb, 4);
        n = turn_end(out, n);
        if (send) send(send_ud, peer, out, n);
        return 1;
    }

    if (type == WB_TURN_REFRESH_REQUEST) {
        turn_allocation *a = find_alloc(s, peer);
        uint32_t life = s->lifetime;
        int have_life = 0;
        wb_stun_addr tmp;
        int have_addr = 0;
        if (walk(buf, len, txid, 0, &tmp, &have_addr, &life, &have_life) != 0) return 0;
        if (!a) {
            size_t n = build_error(out, cap, type, txid, 4, 437);  /* allocation mismatch */
            if (n && send) send(send_ud, peer, out, n);
            return n ? 1 : 0;
        }
        if (have_life) s->lifetime = life ? life : s->lifetime;
        a->expires = (uint32_t)(s->now + s->lifetime);
        size_t n = turn_start(out, cap, (uint16_t)(type | 0x0100), txid);
        if (!n) return 0;
        uint8_t lb[4];
        be32(lb, s->lifetime);
        n = turn_attr(out, n, cap, WB_TURN_ATTR_LIFETIME, lb, 4);
        n = turn_end(out, n);
        if (send) send(send_ud, peer, out, n);
        return 1;
    }

    if (type == WB_TURN_CREATE_PERMISSION_REQUEST) {
        turn_allocation *a = find_alloc(s, peer);
        if (!a) {
            size_t n = build_error(out, cap, type, txid, 4, 437);
            if (n && send) send(send_ud, peer, out, n);
            return n ? 1 : 0;
        }
        wb_stun_addr paddr;
        int have = 0;
        uint32_t life;
        int have_life;
        if (walk(buf, len, txid, WB_TURN_ATTR_XOR_PEER_ADDRESS, &paddr, &have,
                 &life, &have_life) != 0 || !have) {
            size_t n = build_error(out, cap, type, txid, 4, 0);
            if (n && send) send(send_ud, peer, out, n);
            return n ? 1 : 0;
        }
        /* one permission per peer; refresh it rather than duplicating */
        for (int i = 0; i < a->n_perms; i++) {
            turn_permission *pp = &a->perms[i];
            size_t n2 = (paddr.family == 6) ? 16u : 4u;
            if (pp->peer.family == paddr.family && pp->peer.port == paddr.port &&
                memcmp(pp->peer.addr, paddr.addr, n2) == 0) {
                pp->expires = (uint32_t)(s->now + WB_TURN_PERMISSION_LIFETIME);
                goto perm_ok;
            }
        }
        if (a->n_perms >= WB_TURN_MAX_PERMISSIONS) {
            size_t n = build_error(out, cap, type, txid, 5, 0);
            if (n && send) send(send_ud, peer, out, n);
            return n ? 1 : 0;
        }
        {
            turn_permission *pp = &a->perms[a->n_perms++];
            pp->used = 1;
            pp->peer = paddr;
            pp->expires = (uint32_t)(s->now + WB_TURN_PERMISSION_LIFETIME);
        }
    perm_ok:
        {
            size_t n = turn_start(out, cap, (uint16_t)(type | 0x0100), txid);
            if (!n) return 0;
            n = turn_end(out, n);
            if (send) send(send_ud, peer, out, n);
            return 1;
        }
    }

    if (type == WB_TURN_CHANNEL_BIND_REQUEST) {
        turn_allocation *a = find_alloc(s, peer);
        wb_stun_addr paddr;
        int have_addr = 0;
        uint32_t life;
        int have_life;
        uint16_t channel = 0;
        int have_chan = 0;
        if (walk(buf, len, txid, WB_TURN_ATTR_XOR_PEER_ADDRESS, &paddr, &have_addr,
                 &life, &have_life) != 0 || !have_addr) {
            size_t n = build_error(out, cap, type, txid, 4, 0);
            if (n && send) send(send_ud, peer, out, n);
            return n ? 1 : 0;
        }
        /* the channel number lives alongside, so walk for it separately */
        size_t p = WB_STUN_HEADER;
        while (p + 4 <= len) {
            if (rd16(buf + p) == WB_TURN_ATTR_CHANNEL_NUMBER && rd16(buf + p + 2) == 4) {
                channel = rd16(buf + p + 4);
                have_chan = 1;
            }
            p += 4 + rd16(buf + p + 2);
            while (p % 4 != 0) p++;
        }
        if (!a || !have_chan) {
            s->refused++;
            size_t n = build_error(out, cap, type, txid, 4, 0);
            if (n && send) send(send_ud, peer, out, n);
            return n ? 1 : 0;
        }
        /* RFC 8656 section 12: the usable range is 0x4000..0x4FFF, and a
         * channel must not be bound to two peers. */
        if (channel < WB_TURN_CHANNEL_MIN || channel > 0x4FFF) {
            s->refused++;
            size_t n = build_error(out, cap, type, txid, 4, 400);
            if (n && send) send(send_ud, peer, out, n);
            return n ? 1 : 0;
        }
        if (!has_permission(a, &paddr)) {
            s->refused++;
            size_t n = build_error(out, cap, type, txid, 4, 403);
            if (n && send) send(send_ud, peer, out, n);
            return n ? 1 : 0;
        }
        for (int i = 0; i < a->n_chans; i++) {
            turn_channel *ch = &a->chans[i];
            if (ch->channel == channel) {
                size_t n2 = (ch->peer.family == paddr.family) ? 16u : 4u;
                if (ch->peer.family == paddr.family && ch->peer.port == paddr.port &&
                    memcmp(ch->peer.addr, paddr.addr, n2) == 0) {
                    ch->expires = (uint32_t)(s->now + WB_TURN_CHANNEL_LIFETIME);
                    goto chan_ok;
                }
                s->refused++;
                size_t n = build_error(out, cap, type, txid, 4, 400);
                if (n && send) send(send_ud, peer, out, n);
                return n ? 1 : 0;
            }
        }
        if (a->n_chans >= WB_TURN_MAX_CHANNELS) {
            size_t n = build_error(out, cap, type, txid, 5, 0);
            if (n && send) send(send_ud, peer, out, n);
            return n ? 1 : 0;
        }
        {
            turn_channel *ch = &a->chans[a->n_chans++];
            ch->used = 1;
            ch->channel = channel;
            ch->peer = paddr;
            ch->expires = (uint32_t)(s->now + WB_TURN_CHANNEL_LIFETIME);
        }
    chan_ok:
        {
            size_t n = turn_start(out, cap, (uint16_t)(type | 0x0100), txid);
            if (!n) return 0;
            n = turn_end(out, n);
            if (send) send(send_ud, peer, out, n);
            return 1;
        }
    }

    return 0;
}
