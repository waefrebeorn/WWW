/*
 * wb_quic.c -- LP-93 stone 1: QUIC's wire primitives.
 */
#include "wb_quic.h"

#include <limits.h>
#include <string.h>

static void be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

static uint32_t rd32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* ---- variable-length integers ---- */

size_t wb_quic_varint_encode(uint64_t v, uint8_t out[8]) {
    if (v <= 63u) {
        out[0] = (uint8_t)v;
        return 1;
    }
    if (v <= 16383u) {
        out[0] = (uint8_t)(0x40u | (v >> 8));
        out[1] = (uint8_t)v;
        return 2;
    }
    if (v <= 1073741823u) {
        out[0] = (uint8_t)(0x80u | (v >> 24));
        out[1] = (uint8_t)(v >> 16);
        out[2] = (uint8_t)(v >> 8);
        out[3] = (uint8_t)v;
        return 4;
    }
    out[0] = (uint8_t)(0xC0u | (v >> 56));
    out[1] = (uint8_t)(v >> 48);
    out[2] = (uint8_t)(v >> 40);
    out[3] = (uint8_t)(v >> 32);
    out[4] = (uint8_t)(v >> 24);
    out[5] = (uint8_t)(v >> 16);
    out[6] = (uint8_t)(v >> 8);
    out[7] = (uint8_t)v;
    return 8;
}

int wb_quic_varint_decode(const uint8_t *buf, size_t len, uint64_t *v,
                          size_t *used) {
    if (!buf || len < 1) return -1;
    size_t n = (size_t)1 << (buf[0] >> 6);
    if (len < n) return -1;
    uint64_t val = (uint64_t)(buf[0] & 0x3Fu);
    for (size_t i = 1; i < n; i++) val = (val << 8) | (uint64_t)buf[i];
    if (v) *v = val;
    if (used) *used = n;
    return 0;
}

/* ---- packet numbers ---- */

size_t wb_quic_pn_encode(uint64_t full_pn, int have_largest,
                         uint64_t largest_acked, uint8_t out[4]) {
    /* RFC 9000 appendix A.2. The receiver needs a window twice the number of
     * contiguous unacknowledged packets, so the encoding needs one more bit
     * than log2 of that, rounded up to whole bytes. */
    uint64_t unacked;
    if (!have_largest) {
        unacked = full_pn + 1;
    } else {
        unacked = full_pn - largest_acked;
    }
    /* The window the receiver keeps is twice the unacked count, so the field
     * needs the smallest number of bits whose power of two reaches it. */
    uint64_t need = unacked * 2u;
    int bits = 0;
    while (bits < 64 && ((uint64_t)1 << bits) < need) bits++;
    size_t bytes = (size_t)((bits + 7) / 8);
    if (bytes < 1) bytes = 1;
    if (bytes > 4) bytes = 4;
    for (size_t i = 0; i < bytes; i++) {
        out[bytes - 1 - i] = (uint8_t)(full_pn >> (8 * i));
    }
    return bytes;
}

uint64_t wb_quic_pn_decode(uint64_t largest_pn, uint64_t truncated,
                           int pn_nbits) {
    /* RFC 9000 appendix A.3, verbatim. */
    uint64_t expected = largest_pn + 1;
    uint64_t win = (uint64_t)1 << (unsigned)pn_nbits;   /* 8, 16, 24 or 32 */
    uint64_t hwin = win / 2;
    uint64_t mask = win - 1;
    uint64_t candidate = (expected & ~mask) | (truncated & mask);
    /* the window comparisons are only meaningful when they do not wrap */
    if (expected >= hwin &&
        candidate <= expected - hwin &&
        candidate < ((uint64_t)1 << 62) - win) {
        return candidate + win;
    }
    if (expected <= (UINT64_MAX - hwin) &&
        candidate > expected + hwin && candidate >= win) {
        return candidate - win;
    }
    return candidate;
}

/* ---- the long header ---- */

size_t wb_quic_build_long_header(uint8_t *out, size_t cap, int type,
                                 uint32_t version,
                                 const uint8_t *dcid, size_t dcid_len,
                                 const uint8_t *scid, size_t scid_len,
                                 int pn_len) {
    if (!out || pn_len < 1 || pn_len > 4) return 0;
    if (dcid_len > 20 || scid_len > 20) return 0;
    size_t n = 1u + 4u + 1u + dcid_len + 1u + scid_len;
    if (cap < n) return 0;

    out[0] = (uint8_t)(0x80u | 0x40u | ((uint8_t)(type & 3) << 4) |
                       (uint8_t)(pn_len - 1));
    be32(out + 1, version);
    out[5] = (uint8_t)dcid_len;
    if (dcid_len) memcpy(out + 6, dcid, dcid_len);
    out[6 + dcid_len] = (uint8_t)scid_len;
    if (scid_len) memcpy(out + 7 + dcid_len, scid, scid_len);
    return n;
}

int wb_quic_parse_long_header(const uint8_t *buf, size_t len, wb_quic_hdr *h) {
    if (!buf || len < 1) return -1;
    memset(h, 0, sizeof *h);
    h->form = (buf[0] & 0x80u) ? 1 : 0;
    if (!h->form) return -1;                    /* short header: not handled here */
    if ((buf[0] & 0x40u) == 0) return -1;       /* a zero fixed bit is not valid */
    h->type = (buf[0] >> 4) & 0x03;
    if (len < 6) return -1;
    h->version = rd32(buf + 1);
    if (h->version == 0) {
        /* Version Negotiation: the type bits are unused */
        h->type = -1;
    }
    size_t p = 5;
    if (len < p + 1) return -1;
    size_t dl = buf[p++];
    if (dl > 20 || len < p + dl) return -1;
    h->dcid_len = dl;
    memcpy(h->dcid, buf + p, dl);
    p += dl;
    if (len < p + 1) return -1;
    size_t sl = buf[p++];
    if (sl > 20 || len < p + sl) return -1;
    h->scid_len = sl;
    memcpy(h->scid, buf + p, sl);
    p += sl;
    h->header_len = p;
    return 0;
}

/* ---- the Initial packet ---- */

size_t wb_quic_build_initial(uint8_t *out, size_t cap,
                             const uint8_t *dcid, size_t dcid_len,
                             const uint8_t *scid, size_t scid_len,
                             const wb_quic_initial *in) {
    if (!out || !in) return 0;

    uint8_t pnbuf[4];
    size_t pn_bytes = wb_quic_pn_encode(in->packet_number, 0, 0, pnbuf);
    uint64_t body = (uint64_t)pn_bytes + in->payload_len;

    uint8_t tlen[8], lenb[8];
    size_t tl = wb_quic_varint_encode(in->token_len, tlen);
    size_t ll = wb_quic_varint_encode(body, lenb);

    size_t hlen = wb_quic_build_long_header(out, cap, WB_QUIC_INITIAL,
                                            WB_QUIC_VERSION_1, dcid, dcid_len,
                                            scid, scid_len, (int)pn_bytes);
    if (!hlen) return 0;
    size_t n = hlen;
    if (cap < n + tl + in->token_len + ll + pn_bytes + in->payload_len) return 0;

    memcpy(out + n, tlen, tl);
    n += tl;
    if (in->token_len) {
        memcpy(out + n, in->token, in->token_len);
        n += in->token_len;
    }
    memcpy(out + n, lenb, ll);
    n += ll;
    memcpy(out + n, pnbuf, pn_bytes);
    n += pn_bytes;
    if (in->payload_len) {
        memcpy(out + n, in->payload, in->payload_len);
        n += in->payload_len;
    }
    return n;
}

int wb_quic_parse_initial(const uint8_t *buf, size_t len, uint32_t version,
                          wb_quic_hdr *hdr, wb_quic_initial *in) {
    (void)version;
    if (wb_quic_parse_long_header(buf, len, hdr) != 0) return -1;
    if (hdr->type != WB_QUIC_INITIAL) return -1;

    size_t p = hdr->header_len;
    uint64_t token_len = 0;
    size_t used = 0;
    if (wb_quic_varint_decode(buf + p, len - p, &token_len, &used) != 0) return -1;
    p += used;
    if (p + token_len > len) return -1;
    in->token = buf + p;
    in->token_len = (size_t)token_len;
    p += (size_t)token_len;

    uint64_t body = 0;
    if (wb_quic_varint_decode(buf + p, len - p, &body, &used) != 0) return -1;
    p += used;

    size_t pn_bytes = (size_t)(buf[0] & 0x03u) + 1u;
    if (p + pn_bytes > len) return -1;
    uint64_t raw = 0;
    for (size_t i = 0; i < pn_bytes; i++) raw = (raw << 8) | buf[p + i];
    in->packet_number = raw;      /* truncated; the caller decodes it */
    p += pn_bytes;

    if (body < pn_bytes) return -1;
    size_t plen = (size_t)(body - pn_bytes);
    if (p + plen > len) return -1;
    in->payload = buf + p;
    in->payload_len = plen;
    return 0;
}
