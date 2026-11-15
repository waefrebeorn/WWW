/*
 * wb_stun.c -- LP-95 stone 1: STUN (RFC 8489).
 *
 * STUN is big-endian on the wire, unlike the rest of this net stack, so the
 * codec here carries its own byte order rather than reusing wb_net_transport's
 * little-endian helpers.
 */
#include "wb_stun.h"

#include <stdlib.h>
#include <string.h>

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

int wb_stun_is_binding(uint16_t type) {
    /* The method is the 12 bits that are not the two class bits (bit 4 and
     * bit 8), so Binding is method 1: 0x0001, 0x0011, 0x0101, 0x0111. */
    return (type & 0x0EEFu) == (uint16_t)WB_STUN_BINDING_REQUEST;
}

size_t wb_stun_build_binding_request(uint8_t *out, size_t cap,
                                     const uint8_t txid[WB_STUN_TXID_LEN]) {
    if (!out || cap < WB_STUN_HEADER) return 0;
    out[0] = 0;
    out[1] = 0;
    be16(out + 0, WB_STUN_BINDING_REQUEST);
    be16(out + 2, 0);                       /* no attributes */
    be32(out + 4, WB_STUN_MAGIC_COOKIE);
    memcpy(out + 8, txid, WB_STUN_TXID_LEN);
    return WB_STUN_HEADER;
}

int wb_stun_parse_header(const uint8_t *buf, size_t len, uint16_t *type,
                         const uint8_t **txid) {
    if (!buf || len < WB_STUN_HEADER) return -1;
    if ((buf[0] & 0xC0u) != 0) return -1;   /* the top two bits are zero */
    uint16_t t = rd16(buf + 0);
    uint16_t mlen = rd16(buf + 2);
    if (rd32(buf + 4) != WB_STUN_MAGIC_COOKIE) return -1;
    /* the length counts the attributes and must be a multiple of four */
    if (mlen % 4 != 0) return -1;
    if ((size_t)WB_STUN_HEADER + mlen != len) return -1;
    if (type) *type = t;
    if (txid) *txid = buf + 8;
    return 0;
}

/* Encode one address attribute. `xor` selects XOR-MAPPED-ADDRESS. */
static size_t put_addr(uint8_t *out, uint16_t attr, int xor,
                       const uint8_t txid[WB_STUN_TXID_LEN],
                       const wb_stun_addr *a) {
    size_t alen = (a->family == 6) ? 16u : 4u;
    size_t vlen = 4u + alen;
    be16(out, attr);
    be16(out + 2, (uint16_t)vlen);
    out[4] = 0;                             /* reserved */
    out[5] = (uint8_t)(a->family == 6 ? 0x02 : 0x01);
    uint16_t port = a->port;
    if (xor) port = (uint16_t)(port ^ (uint16_t)(WB_STUN_MAGIC_COOKIE >> 16));
    be16(out + 6, port);
    const uint8_t *src = a->addr;
    for (size_t i = 0; i < alen; i++) {
        uint8_t x = src[i];
        if (xor) {
            if (a->family == 6) {
                x = (uint8_t)(x ^ (i < 4 ? ((WB_STUN_MAGIC_COOKIE >> (24 - 8 * i)) & 0xFFu)
                                          : txid[i - 4]));
            } else {
                x = (uint8_t)(x ^ (uint8_t)((WB_STUN_MAGIC_COOKIE >> (24 - 8 * i)) & 0xFFu));
            }
        }
        out[8 + i] = x;
    }
    /* the attribute is padded to four bytes; the padding is not counted in the
     * length but is present on the wire */
    size_t pad = (4u - (vlen % 4u)) % 4u;
    memset(out + 8 + alen, 0, pad);
    return 8u + alen + pad;
}

static int get_addr(const uint8_t *val, size_t vlen, int xor,
                    const uint8_t txid[WB_STUN_TXID_LEN], wb_stun_addr *out) {
    if (vlen < 8) return -1;
    if (val[0] != 0) return -1;
    uint8_t fam = val[1];
    if (fam != 0x01 && fam != 0x02) return -1;
    size_t alen = (fam == 0x02) ? 16u : 4u;
    if (vlen != 4u + alen) return -1;
    uint16_t port = rd16(val + 2);
    if (xor) port = (uint16_t)(port ^ (uint16_t)(WB_STUN_MAGIC_COOKIE >> 16));
    out->family = (fam == 0x02) ? 6 : 4;
    out->port = port;
    for (size_t i = 0; i < alen; i++) {
        uint8_t x = val[4 + i];
        if (xor) {
            if (fam == 0x02) {
                x = (uint8_t)(x ^ (i < 4 ? ((WB_STUN_MAGIC_COOKIE >> (24 - 8 * i)) & 0xFFu)
                                          : txid[i - 4]));
            } else {
                x = (uint8_t)(x ^ (uint8_t)((WB_STUN_MAGIC_COOKIE >> (24 - 8 * i)) & 0xFFu));
            }
        }
        out->addr[i] = x;
    }
    return 0;
}

int wb_stun_find_mapped(const uint8_t *buf, size_t len, wb_stun_addr *out) {
    uint16_t type;
    const uint8_t *txid;
    if (wb_stun_parse_header(buf, len, &type, &txid) != 0) return -1;

    int have_xor = -1, have_plain = -1;
    size_t p = WB_STUN_HEADER;
    while (p + 4 <= len) {
        uint16_t at = rd16(buf + p);
        uint16_t al = rd16(buf + p + 2);
        if (p + 4 + al > len) return -1;
        if (at == WB_STUN_ATTR_XOR_MAPPED_ADDRESS && have_xor < 0) {
            have_xor = (int)p;
        } else if (at == WB_STUN_ATTR_MAPPED_ADDRESS && have_plain < 0) {
            have_plain = (int)p;
        }
        p += 4 + al;
        while (p % 4 != 0) p++;
    }

    int pick = (have_xor >= 0) ? have_xor : have_plain;
    if (pick < 0) return -1;
    p = (size_t)pick;
    uint16_t al = rd16(buf + p + 2);
    return get_addr(buf + p + 4, al, pick == have_xor, txid, out);
}

size_t wb_stun_build_binding_success(uint8_t *out, size_t cap,
                                     const uint8_t txid[WB_STUN_TXID_LEN],
                                     const wb_stun_addr *mapped) {
    if (!out || !mapped || cap < WB_STUN_HEADER) return 0;
    size_t vlen = (mapped->family == 6) ? 20u : 8u;
    size_t total = WB_STUN_HEADER + 4u + vlen;
    if (cap < total) return 0;

    out[0] = 0;
    out[1] = 0;
    be16(out + 0, WB_STUN_BINDING_SUCCESS);
    be16(out + 2, (uint16_t)(4u + vlen));
    be32(out + 4, WB_STUN_MAGIC_COOKIE);
    memcpy(out + 8, txid, WB_STUN_TXID_LEN);
    put_addr(out + WB_STUN_HEADER, WB_STUN_ATTR_XOR_MAPPED_ADDRESS, 1, txid, mapped);
    return total;
}

struct wb_stun_server {
    unsigned long long requests;
};

wb_stun_server *wb_stun_server_new(void) {
    return (wb_stun_server *)calloc(1, sizeof(wb_stun_server));
}

void wb_stun_server_free(wb_stun_server *s) { free(s); }

unsigned long long wb_stun_server_requests(const wb_stun_server *s) {
    return s ? s->requests : 0;
}

size_t wb_stun_server_handle(wb_stun_server *s, const uint8_t *buf, size_t len,
                             int peer, const wb_stun_addr *observed,
                             wb_stun_send_fn send, void *ud,
                             uint8_t *out, size_t cap) {
    uint16_t type;
    const uint8_t *txid;
    if (wb_stun_parse_header(buf, len, &type, &txid) != 0) return 0;
    /* a request is the only thing a Binding server answers; an indication gets
     * no response, and a response is not the server's to make */
    if (type != WB_STUN_BINDING_REQUEST) return 0;
    if (!observed) return 0;

    size_t n = wb_stun_build_binding_success(out, cap, txid, observed);
    if (n == 0) return 0;
    s->requests++;
    if (send) send(ud, peer, out, n);
    return n;
}
