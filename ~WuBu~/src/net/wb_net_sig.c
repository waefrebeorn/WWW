/*
 * wb_net_sig.c -- LP-95 stone 4: candidate exchange and the connect-token
 * gateway.
 */
#include "wb_net_sig.h"

#include <stdlib.h>
#include <string.h>

#define WB_ICE_SIG_MAGIC0 'I'
#define WB_ICE_SIG_MAGIC1 'C'
#define WB_ICE_SIG_VERSION 1u

#define WB_NET_GATEWAY_MAX_ROOMS 64

static void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint16_t get16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put_addr_rec(uint8_t *p, int family, uint16_t port,
                         const uint8_t addr[16]) {
    p[0] = (uint8_t)family;
    put16(p + 1, port);
    if (family == 6) {
        memcpy(p + 3, addr, 16);
    } else {
        memset(p + 3, 0, 16);
        memcpy(p + 3, addr, 4);
    }
}

/* ---- the offer / answer ---- */

size_t wb_ice_sig_encode(uint8_t *out, size_t cap, const wb_ice_creds *creds,
                         const wb_ice_candidate *cands, size_t n, int answer) {
    if (!out || !creds) return 0;
    if (n > WB_ICE_SIG_MAX_CANDIDATES) return 0;
    size_t total = WB_ICE_SIG_HEADER + n * WB_ICE_SIG_RECORD;
    if (cap < total) return 0;

    memset(out, 0, total);
    out[0] = WB_ICE_SIG_MAGIC0;
    out[1] = WB_ICE_SIG_MAGIC1;
    out[2] = WB_ICE_SIG_VERSION;
    out[3] = (uint8_t)(answer ? 1 : 0);
    put16(out + 4, (uint16_t)n);
    /* the credentials are fixed-width and NUL padded, so no length can
     * disagree with the buffer */
    size_t u = strlen(creds->ufrag);
    if (u > WB_ICE_UFRAG_MAX - 1) u = WB_ICE_UFRAG_MAX - 1;
    memcpy(out + 6, creds->ufrag, u);
    size_t pw = strlen(creds->password);
    if (pw > WB_ICE_PASSWORD_MAX - 1) pw = WB_ICE_PASSWORD_MAX - 1;
    memcpy(out + 30, creds->password, pw);
    out[94] = creds->controlling ? 1 : 0;

    uint8_t *rec = out + WB_ICE_SIG_HEADER;
    for (size_t i = 0; i < n; i++) {
        const wb_ice_candidate *c = &cands[i];
        memset(rec, 0, WB_ICE_SIG_RECORD);
        rec[0] = (uint8_t)c->type;
        put_addr_rec(rec + 1, c->addr.family, c->addr.port, c->addr.addr);
        put16(rec + 20, (uint16_t)c->component);
        put32(rec + 22, c->priority);
        put32(rec + 26, c->foundation);
        put_addr_rec(rec + 30, c->base.family, c->base.port, c->base.addr);
        rec += WB_ICE_SIG_RECORD;
    }
    return total;
}

int wb_ice_sig_decode(const uint8_t *buf, size_t len, wb_ice_creds *creds,
                      wb_ice_candidate *cands, size_t max, size_t *n) {
    if (!buf || len < WB_ICE_SIG_HEADER) return -1;
    if (buf[0] != WB_ICE_SIG_MAGIC0 || buf[1] != WB_ICE_SIG_MAGIC1) return -1;
    if (buf[2] != WB_ICE_SIG_VERSION) return -1;

    uint16_t count = get16(buf + 4);
    size_t total = WB_ICE_SIG_HEADER + (size_t)count * WB_ICE_SIG_RECORD;
    if (total != len) return -1;              /* count and length must agree */
    if (count > max) return -1;

    if (creds) {
        memset(creds, 0, sizeof *creds);
        memcpy(creds->ufrag, buf + 6, WB_ICE_UFRAG_MAX - 1);
        memcpy(creds->password, buf + 30, WB_ICE_PASSWORD_MAX - 1);
        creds->controlling = buf[94] ? 1 : 0;
    }

    const uint8_t *rec = buf + WB_ICE_SIG_HEADER;
    for (size_t i = 0; i < count; i++) {
        int fam = rec[1];
        int base_fam = rec[30];
        if ((fam != 4 && fam != 6) || (base_fam != 4 && base_fam != 6)) return -1;
        if (cands) {
            wb_ice_candidate *c = &cands[i];
            memset(c, 0, sizeof *c);
            c->type = rec[0];
            c->addr.family = fam;
            c->addr.port = get16(rec + 2);
            memcpy(c->addr.addr, rec + 4, (fam == 6) ? 16u : 4u);
            c->component = get16(rec + 20);
            c->priority = get32(rec + 22);
            c->foundation = get32(rec + 26);
            c->base.family = base_fam;
            c->base.port = get16(rec + 31);
            memcpy(c->base.addr, rec + 33, (base_fam == 6) ? 16u : 4u);
        }
        rec += WB_ICE_SIG_RECORD;
    }
    if (n) *n = count;
    return 0;
}

/* ---- the connect-token gateway ---- */

typedef struct {
    int   used;
    uint8_t token[16];
    int   peers[2];
    double expires;
} gw_room;

struct wb_net_gateway {
    uint32_t lifetime;
    double   now;
    gw_room  rooms[WB_NET_GATEWAY_MAX_ROOMS];
    unsigned long long issued;
    unsigned long long connected;
    unsigned long long refused;
};

/* A room token is a deterministic 128-bit hash of the room name, which is what
 * makes it usable as a shareable code: both clients compute the same token
 * rather than one being sent it. It also means a token cannot be guessed from
 * anything but the room name itself. */
static void room_hash(const char *room, uint8_t out[16]) {
    uint64_t h1 = 0x811C9DC5u;
    uint64_t h2 = 0x100000001B3ull;
    for (const char *p = room; *p; p++) {
        h1 = (h1 ^ (uint8_t)*p) * 0x00000100000001B3ull;
        h2 = (h2 ^ (uint8_t)*p) + 0x9E3779B97F4A7C15ull;
    }
    h2 ^= h1;
    for (int i = 0; i < 8; i++) {
        out[i] = (uint8_t)(h1 >> (8 * i));
        out[8 + i] = (uint8_t)(h2 >> (8 * i));
    }
}

wb_net_gateway *wb_net_gateway_new(uint32_t lifetime) {
    wb_net_gateway *g = (wb_net_gateway *)calloc(1, sizeof *g);
    if (!g) return NULL;
    g->lifetime = lifetime ? lifetime : 600u;
    return g;
}

void wb_net_gateway_free(wb_net_gateway *g) { free(g); }

static gw_room *find_room(wb_net_gateway *g, const uint8_t token[16]) {
    for (int i = 0; i < WB_NET_GATEWAY_MAX_ROOMS; i++) {
        if (g->rooms[i].used && memcmp(g->rooms[i].token, token, 16) == 0) {
            return &g->rooms[i];
        }
    }
    return NULL;
}

static gw_room *new_room(wb_net_gateway *g, const uint8_t token[16]) {
    gw_room *r = find_room(g, token);
    if (r) return r;
    for (int i = 0; i < WB_NET_GATEWAY_MAX_ROOMS; i++) {
        if (g->rooms[i].used) continue;
        gw_room *n = &g->rooms[i];
        memset(n, 0, sizeof *n);
        n->used = 1;
        memcpy(n->token, token, 16);
        n->peers[0] = -1;
        n->peers[1] = -1;
        n->expires = g->now + (double)g->lifetime;
        return n;
    }
    return NULL;
}

void wb_net_gateway_issue(wb_net_gateway *g, const char *room, uint8_t token[16]) {
    if (!g || !room || !token) return;
    room_hash(room, token);
    gw_room *r = new_room(g, token);
    if (!r) return;
    /* re-issuing a room keeps its peers and extends its life */
    r->expires = g->now + (double)g->lifetime;
    g->issued++;
}

int wb_net_gateway_redeem(wb_net_gateway *g, const uint8_t token[16], int peer) {
    if (!g) return 0;
    gw_room *r = find_room(g, token);
    if (!r || (double)r->expires <= g->now) {
        g->refused++;
        return 0;
    }
    for (int i = 0; i < 2; i++) {
        if (r->peers[i] == peer) {
            g->refused++;               /* one redemption per peer */
            return 0;
        }
    }
    for (int i = 0; i < 2; i++) {
        if (r->peers[i] == -1) {
            r->peers[i] = peer;
            if (i == 1) g->connected++;
            return 1;
        }
    }
    g->refused++;                       /* the room is full */
    return 0;
}

int wb_net_gateway_connected(wb_net_gateway *g, const uint8_t token[16]) {
    gw_room *r = find_room(g, token);
    if (!r) return 0;
    return r->peers[0] != -1 && r->peers[1] != -1;
}

int wb_net_gateway_peer(wb_net_gateway *g, const uint8_t token[16], int peer) {
    gw_room *r = find_room(g, token);
    if (!r) return -1;
    for (int i = 0; i < 2; i++) {
        if (r->peers[i] != -1 && r->peers[i] != peer) return r->peers[i];
    }
    return -1;
}

void wb_net_gateway_tick(wb_net_gateway *g, double dt) {
    if (!g || dt <= 0.0) return;
    g->now += dt;
    for (int i = 0; i < WB_NET_GATEWAY_MAX_ROOMS; i++) {
        if (g->rooms[i].used && (double)g->rooms[i].expires <= g->now) {
            g->rooms[i].used = 0;
        }
    }
}

unsigned long long wb_net_gateway_issued(const wb_net_gateway *g) { return g->issued; }
unsigned long long wb_net_gateway_connected_rooms(const wb_net_gateway *g) {
    return g->connected;
}
unsigned long long wb_net_gateway_refused(const wb_net_gateway *g) { return g->refused; }
