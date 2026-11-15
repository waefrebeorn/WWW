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

/* ------------------------------------------------------------------ frames */

typedef struct {
    uint8_t *p;
    size_t   cap;
    size_t   n;
    int      overflow;
} qbuf;

static void qb_init(qbuf *b, uint8_t *p, size_t cap) {
    b->p = p;
    b->cap = cap;
    b->n = 0;
    b->overflow = 0;
}

static void qb_bytes(qbuf *b, const void *d, size_t n) {
    if (b->overflow || b->n + n > b->cap) { b->overflow = 1; return; }
    memcpy(b->p + b->n, d, n);
    b->n += n;
}

static void qb_u8(qbuf *b, uint8_t v) {
    if (b->overflow || b->n + 1 > b->cap) { b->overflow = 1; return; }
    b->p[b->n++] = v;
}

static void qb_varint(qbuf *b, uint64_t v) {
    uint8_t t[8];
    size_t n = wb_quic_varint_encode(v, t);
    qb_bytes(b, t, n);
}

static size_t qb_finish(qbuf *b) { return b->overflow ? 0 : b->n; }

/* The Frame Type is a varint and MUST use the shortest form
 * (RFC 9000 section 12.4), which wb_quic_varint_encode already does. */
static void qb_type(qbuf *b, uint64_t type) { qb_varint(b, type); }

/* How many bytes a frame body occupies, given its type. Returns -1 if the body
 * is truncated or the type is not one this codec knows. */
static long frame_body_len(uint64_t type, const uint8_t *p, size_t len) {
    size_t q = 0, u;
    uint64_t v;
    switch (type) {
    case WB_QUIC_FRAME_PING:
    case WB_QUIC_FRAME_HANDSHAKE_DONE:
        return 0;
    case WB_QUIC_FRAME_PADDING:
        /* PADDING runs to the end of the packet, so it consumes what is left */
        return (long)len;
    case WB_QUIC_FRAME_ACK: case 0x03: {
        for (int i = 0; i < 2; i++) {      /* Largest Acknowledged, ACK Delay */
            if (wb_quic_varint_decode(p + q, len - q, &v, &u) != 0) return -1;
            q += u;
        }
        if (wb_quic_varint_decode(p + q, len - q, &v, &u) != 0) return -1;
        uint64_t ranges = v;               /* ACK Range Count */
        q += u;
        if (wb_quic_varint_decode(p + q, len - q, &v, &u) != 0) return -1;
        q += u;                            /* First ACK Range */
        for (uint64_t i = 0; i < ranges; i++) {
            for (int k = 0; k < 2; k++) {
                if (wb_quic_varint_decode(p + q, len - q, &v, &u) != 0) return -1;
                q += u;
            }
        }
        if (type == 0x03) {
            for (int k = 0; k < 3; k++) {
                if (wb_quic_varint_decode(p + q, len - q, &v, &u) != 0) return -1;
                q += u;
            }
        }
        return (long)q;
    }
    case WB_QUIC_FRAME_CRYPTO: {
        for (int i = 0; i < 2; i++) {
            if (wb_quic_varint_decode(p + q, len - q, &v, &u) != 0) return -1;
            q += u;
        }
        return (long)(q + v);
    }
    case WB_QUIC_FRAME_NEW_TOKEN: {
        if (wb_quic_varint_decode(p + q, len - q, &v, &u) != 0) return -1;
        q += u;
        return (long)(q + v);
    }
    case WB_QUIC_FRAME_RESET_STREAM:
    case WB_QUIC_FRAME_STOP_SENDING:
    case WB_QUIC_FRAME_MAX_STREAM_DATA:
    case WB_QUIC_FRAME_STREAM_DATA_BLOCKED:
    case WB_QUIC_FRAME_RETIRE_CONNECTION_ID: {
        int fields = (type == WB_QUIC_FRAME_RESET_STREAM ||
                      type == WB_QUIC_FRAME_STOP_SENDING ||
                      type == WB_QUIC_FRAME_MAX_STREAM_DATA ||
                      type == WB_QUIC_FRAME_STREAM_DATA_BLOCKED) ? 2 : 1;
        for (int i = 0; i < fields; i++) {
            if (wb_quic_varint_decode(p + q, len - q, &v, &u) != 0) return -1;
            q += u;
        }
        return (long)q;
    }
    case WB_QUIC_FRAME_MAX_DATA:
    case WB_QUIC_FRAME_DATA_BLOCKED:
    case WB_QUIC_FRAME_MAX_STREAMS: case 0x13:
    case WB_QUIC_FRAME_STREAMS_BLOCKED: case 0x17: {
        if (wb_quic_varint_decode(p + q, len - q, &v, &u) != 0) return -1;
        return (long)(q + u);
    }
    case WB_QUIC_FRAME_NEW_CONNECTION_ID: {
        for (int i = 0; i < 2; i++) {
            if (wb_quic_varint_decode(p + q, len - q, &v, &u) != 0) return -1;
            q += u;
        }
        if (q + 1 > len) return -1;
        size_t cl = p[q];
        if (cl > 20) return -1;
        return (long)(q + 1 + cl + 16);
    }
    case WB_QUIC_FRAME_PATH_CHALLENGE:
    case WB_QUIC_FRAME_PATH_RESPONSE:
        return (len >= 8) ? 8 : -1;
    case WB_QUIC_FRAME_CONNECTION_CLOSE: case 0x1d: {
        if (wb_quic_varint_decode(p + q, len - q, &v, &u) != 0) return -1;
        q += u;
        if (type == WB_QUIC_FRAME_CONNECTION_CLOSE) {
            if (wb_quic_varint_decode(p + q, len - q, &v, &u) != 0) return -1;
            q += u;
        }
        if (wb_quic_varint_decode(p + q, len - q, &v, &u) != 0) return -1;
        q += u;
        return (long)(q + v);
    }
    default:
        if (type >= WB_QUIC_FRAME_STREAM_BASE &&
            type <= WB_QUIC_FRAME_STREAM_BASE + 7) {
            if (wb_quic_varint_decode(p + q, len - q, &v, &u) != 0) return -1;
            q += u;
            if (type & WB_QUIC_STREAM_OFF) {
                if (wb_quic_varint_decode(p + q, len - q, &v, &u) != 0) return -1;
                q += u;
            }
            if (type & WB_QUIC_STREAM_LEN) {
                if (wb_quic_varint_decode(p + q, len - q, &v, &u) != 0) return -1;
                q += u;
                return (long)(q + v);
            }
            /* no length: the stream data runs to the end of the packet, so
             * the whole remaining body belongs to this frame */
            return (long)len;
        }
        return -1;
    }
}

int wb_quic_frame_next(const uint8_t *buf, size_t len, size_t *off,
                       wb_quic_frame *f) {
    if (!buf || !off || *off > len) return -1;
    if (*off == len) return 0;
    size_t used = 0;
    uint64_t type = 0;
    if (wb_quic_varint_decode(buf + *off, len - *off, &type, &used) != 0) return -1;
    long blen = frame_body_len(type, buf + *off + used, len - *off - used);
    if (blen < 0) return -1;
    if ((size_t)blen > len - *off - used) return -1;
    f->type = type;
    f->body = buf + *off + used;
    f->body_len = (size_t)blen;
    *off = *off + used + (size_t)blen;
    return 1;
}

size_t wb_quic_frame_ping(uint8_t *out, size_t cap) {
    qbuf b;
    qb_init(&b, out, cap);
    qb_type(&b, WB_QUIC_FRAME_PING);
    return qb_finish(&b);
}

size_t wb_quic_frame_padding(uint8_t *out, size_t cap, size_t n) {
    qbuf b;
    qb_init(&b, out, cap);
    for (size_t i = 0; i < n && !b.overflow; i++) qb_u8(&b, 0);
    return qb_finish(&b);
}

size_t wb_quic_frame_handshake_done(uint8_t *out, size_t cap) {
    qbuf b;
    qb_init(&b, out, cap);
    qb_type(&b, WB_QUIC_FRAME_HANDSHAKE_DONE);
    return qb_finish(&b);
}

size_t wb_quic_frame_ack(uint8_t *out, size_t cap, int ecn,
                         uint64_t largest, uint64_t delay,
                         const uint64_t *acked, size_t n_acked,
                         uint64_t ect0, uint64_t ect1, uint64_t ce) {
    if (!acked || n_acked == 0) return 0;

    /* Contiguous runs, ascending. */
    uint64_t rs[WB_QUIC_ACK_MAX_RANGES], re[WB_QUIC_ACK_MAX_RANGES];
    int nrun = 0;
    size_t i = 0;
    while (i < n_acked) {
        size_t j = i;
        while (j + 1 < n_acked && acked[j + 1] == acked[j] + 1) j++;
        if (nrun >= WB_QUIC_ACK_MAX_RANGES) return 0;
        rs[nrun] = acked[i];
        re[nrun] = acked[j];
        nrun++;
        i = j + 1;
    }
    if (acked[n_acked - 1] != largest) return 0;   /* the largest must be acked */

    qbuf b;
    qb_init(&b, out, cap);
    qb_type(&b, ecn ? 0x03u : WB_QUIC_FRAME_ACK);
    qb_varint(&b, largest);
    qb_varint(&b, delay);
    qb_varint(&b, (uint64_t)(nrun - 1));            /* ACK Range Count */
    qb_varint(&b, re[nrun - 1] - rs[nrun - 1] + 1); /* First ACK Range */
    for (int k = nrun - 2; k >= 0; k--) {
        /* the packets strictly between the run below and the run above */
        qb_varint(&b, rs[k + 1] - re[k] - 1);
        qb_varint(&b, re[k] - rs[k] + 1);
    }
    if (ecn) {
        qb_varint(&b, ect0);
        qb_varint(&b, ect1);
        qb_varint(&b, ce);
    }
    return qb_finish(&b);
}

int wb_quic_parse_ack(const uint8_t *body, size_t len, int type,
                      wb_quic_ack *out) {
    if (!body || !out) return -1;
    if (type != WB_QUIC_FRAME_ACK && type != 0x03) return -1;
    memset(out, 0, sizeof *out);
    out->has_ecn = (type == 0x03);
    size_t p = 0, u;
    uint64_t v;
    if (wb_quic_varint_decode(body + p, len - p, &v, &u) != 0) return -1;
    out->largest = v;
    p += u;
    if (wb_quic_varint_decode(body + p, len - p, &v, &u) != 0) return -1;
    out->delay = v;
    p += u;
    if (wb_quic_varint_decode(body + p, len - p, &v, &u) != 0) return -1;
    if (v >= WB_QUIC_ACK_MAX_RANGES) return -1;
    out->range_count = (int)v;
    p += u;
    if (wb_quic_varint_decode(body + p, len - p, &v, &u) != 0) return -1;
    out->first_range = v;
    p += u;
    for (int i = 0; i < out->range_count; i++) {
        if (wb_quic_varint_decode(body + p, len - p, &v, &u) != 0) return -1;
        out->gap[i] = v;
        p += u;
        if (wb_quic_varint_decode(body + p, len - p, &v, &u) != 0) return -1;
        out->range_len[i] = v;
        p += u;
    }
    if (out->has_ecn) {
        if (wb_quic_varint_decode(body + p, len - p, &v, &u) != 0) return -1;
        out->ect0 = v;
        p += u;
        if (wb_quic_varint_decode(body + p, len - p, &v, &u) != 0) return -1;
        out->ect1 = v;
        p += u;
        if (wb_quic_varint_decode(body + p, len - p, &v, &u) != 0) return -1;
        out->ce = v;
        p += u;
    }
    if (p != len) return -1;                       /* no trailing bytes */
    return 0;
}

uint64_t wb_quic_ack_count(const wb_quic_ack *ack) {
    uint64_t n = ack->first_range;
    for (int i = 0; i < ack->range_count; i++) n += ack->range_len[i];
    return n;
}

/* Walk an ACK's ranges and hand back each acknowledged packet number. Stops at
 * `max` entries or when the ranges run out. Returns the count written. */
size_t wb_quic_ack_expand(const wb_quic_ack *ack, uint64_t *out, size_t max) {
    size_t n = 0;
    uint64_t bottom = ack->largest;
    uint64_t top = bottom - ack->first_range + 1;
    for (uint64_t p = top; p <= bottom && n < max; p++) out[n++] = p;
    for (int i = 0; i < ack->range_count; i++) {
        bottom = top - ack->gap[i] - 1;            /* top of the next range */
        top = bottom - ack->range_len[i] + 1;
        for (uint64_t p = top; p <= bottom && n < max; p++) out[n++] = p;
    }
    return n;
}

size_t wb_quic_frame_crypto(uint8_t *out, size_t cap, uint64_t offset,
                            const void *data, size_t len) {
    qbuf b;
    qb_init(&b, out, cap);
    qb_type(&b, WB_QUIC_FRAME_CRYPTO);
    qb_varint(&b, offset);
    qb_varint(&b, len);
    qb_bytes(&b, data, len);
    return qb_finish(&b);
}

int wb_quic_parse_crypto(const uint8_t *body, size_t len, uint64_t *offset,
                         const uint8_t **data, size_t *dlen) {
    size_t p = 0, u;
    uint64_t v;
    if (wb_quic_varint_decode(body + p, len - p, &v, &u) != 0) return -1;
    *offset = v;
    p += u;
    if (wb_quic_varint_decode(body + p, len - p, &v, &u) != 0) return -1;
    p += u;
    if (p + v > len) return -1;
    *data = body + p;
    *dlen = (size_t)v;
    return 0;
}

size_t wb_quic_frame_stream(uint8_t *out, size_t cap, uint64_t stream_id,
                            uint64_t offset, const void *data, size_t len,
                            int fin) {
    uint64_t type = WB_QUIC_FRAME_STREAM_BASE | WB_QUIC_STREAM_OFF |
                    WB_QUIC_STREAM_LEN;
    if (fin) type |= WB_QUIC_STREAM_FIN;
    qbuf b;
    qb_init(&b, out, cap);
    qb_type(&b, type);
    qb_varint(&b, stream_id);
    qb_varint(&b, offset);
    qb_varint(&b, len);
    qb_bytes(&b, data, len);
    return qb_finish(&b);
}

int wb_quic_parse_stream(const uint8_t *body, size_t len, int flags,
                         uint64_t *stream_id, uint64_t *offset, int *has_len,
                         const uint8_t **data, size_t *dlen, int *fin) {
    if (!body || len < 1) return -1;
    size_t p = 0, u;
    uint64_t v;
    if (wb_quic_varint_decode(body + p, len - p, &v, &u) != 0) return -1;
    *stream_id = v;
    p += u;
    uint64_t off = 0;
    if (flags & WB_QUIC_STREAM_OFF) {
        if (wb_quic_varint_decode(body + p, len - p, &v, &u) != 0) return -1;
        off = v;
        p += u;
    }
    *offset = off;
    if (flags & WB_QUIC_STREAM_LEN) {
        if (wb_quic_varint_decode(body + p, len - p, &v, &u) != 0) return -1;
        p += u;
        if (p + v > len) return -1;
        *has_len = 1;
        *data = body + p;
        *dlen = (size_t)v;
    } else {
        /* without a length the stream data runs to the end of the packet */
        *has_len = 0;
        *data = body + p;
        *dlen = len - p;
    }
    *fin = (flags & WB_QUIC_STREAM_FIN) ? 1 : 0;
    return 0;
}


size_t wb_quic_frame_new_token(uint8_t *out, size_t cap, const void *token,
                               size_t len) {
    qbuf b;
    qb_init(&b, out, cap);
    qb_type(&b, WB_QUIC_FRAME_NEW_TOKEN);
    qb_varint(&b, len);
    qb_bytes(&b, token, len);
    return qb_finish(&b);
}

size_t wb_quic_frame_reset_stream(uint8_t *out, size_t cap, uint64_t stream_id,
                                  uint64_t error, uint64_t final_size) {
    qbuf b;
    qb_init(&b, out, cap);
    qb_type(&b, WB_QUIC_FRAME_RESET_STREAM);
    qb_varint(&b, stream_id);
    qb_varint(&b, error);
    qb_varint(&b, final_size);
    return qb_finish(&b);
}

size_t wb_quic_frame_stop_sending(uint8_t *out, size_t cap, uint64_t stream_id,
                                  uint64_t error) {
    qbuf b;
    qb_init(&b, out, cap);
    qb_type(&b, WB_QUIC_FRAME_STOP_SENDING);
    qb_varint(&b, stream_id);
    qb_varint(&b, error);
    return qb_finish(&b);
}

size_t wb_quic_frame_max_data(uint8_t *out, size_t cap, uint64_t maximum) {
    qbuf b;
    qb_init(&b, out, cap);
    qb_type(&b, WB_QUIC_FRAME_MAX_DATA);
    qb_varint(&b, maximum);
    return qb_finish(&b);
}

size_t wb_quic_frame_max_stream_data(uint8_t *out, size_t cap,
                                     uint64_t stream_id, uint64_t maximum) {
    qbuf b;
    qb_init(&b, out, cap);
    qb_type(&b, WB_QUIC_FRAME_MAX_STREAM_DATA);
    qb_varint(&b, stream_id);
    qb_varint(&b, maximum);
    return qb_finish(&b);
}

size_t wb_quic_frame_max_streams(uint8_t *out, size_t cap, int uni,
                                 uint64_t maximum) {
    qbuf b;
    qb_init(&b, out, cap);
    qb_type(&b, uni ? 0x13u : WB_QUIC_FRAME_MAX_STREAMS);
    qb_varint(&b, maximum);
    return qb_finish(&b);
}

size_t wb_quic_frame_data_blocked(uint8_t *out, size_t cap, uint64_t limit) {
    qbuf b;
    qb_init(&b, out, cap);
    qb_type(&b, WB_QUIC_FRAME_DATA_BLOCKED);
    qb_varint(&b, limit);
    return qb_finish(&b);
}

size_t wb_quic_frame_stream_data_blocked(uint8_t *out, size_t cap,
                                         uint64_t stream_id, uint64_t limit) {
    qbuf b;
    qb_init(&b, out, cap);
    qb_type(&b, WB_QUIC_FRAME_STREAM_DATA_BLOCKED);
    qb_varint(&b, stream_id);
    qb_varint(&b, limit);
    return qb_finish(&b);
}

size_t wb_quic_frame_streams_blocked(uint8_t *out, size_t cap, int uni,
                                     uint64_t limit) {
    qbuf b;
    qb_init(&b, out, cap);
    qb_type(&b, uni ? 0x17u : WB_QUIC_FRAME_STREAMS_BLOCKED);
    qb_varint(&b, limit);
    return qb_finish(&b);
}

size_t wb_quic_frame_new_connection_id(uint8_t *out, size_t cap,
                                       uint64_t seq, uint64_t retire_prior_to,
                                       const uint8_t *cid, size_t cid_len,
                                       const uint8_t token[16]) {
    if (cid_len > 20) return 0;
    qbuf b;
    qb_init(&b, out, cap);
    qb_type(&b, WB_QUIC_FRAME_NEW_CONNECTION_ID);
    qb_varint(&b, seq);
    qb_varint(&b, retire_prior_to);
    qb_u8(&b, (uint8_t)cid_len);
    qb_bytes(&b, cid, cid_len);
    qb_bytes(&b, token, 16);
    return qb_finish(&b);
}

int wb_quic_parse_new_connection_id(const uint8_t *body, size_t len,
                                    uint64_t *seq, uint64_t *retire_prior_to,
                                    uint8_t *cid, size_t *cid_len,
                                    uint8_t token[16]) {
    size_t p = 0, u;
    uint64_t v;
    if (wb_quic_varint_decode(body + p, len - p, &v, &u) != 0) return -1;
    *seq = v;
    p += u;
    if (wb_quic_varint_decode(body + p, len - p, &v, &u) != 0) return -1;
    *retire_prior_to = v;
    p += u;
    if (p + 1 > len) return -1;
    size_t cl = body[p++];
    if (cl > 20 || p + cl + 16 > len) return -1;
    memcpy(cid, body + p, cl);
    *cid_len = cl;
    p += cl;
    memcpy(token, body + p, 16);
    p += 16;
    if (p != len) return -1;
    return 0;
}

size_t wb_quic_frame_retire_connection_id(uint8_t *out, size_t cap,
                                          uint64_t seq) {
    qbuf b;
    qb_init(&b, out, cap);
    qb_type(&b, WB_QUIC_FRAME_RETIRE_CONNECTION_ID);
    qb_varint(&b, seq);
    return qb_finish(&b);
}

size_t wb_quic_frame_path_challenge(uint8_t *out, size_t cap,
                                    const uint8_t data[8]) {
    qbuf b;
    qb_init(&b, out, cap);
    qb_type(&b, WB_QUIC_FRAME_PATH_CHALLENGE);
    qb_bytes(&b, data, 8);
    return qb_finish(&b);
}

size_t wb_quic_frame_path_response(uint8_t *out, size_t cap,
                                   const uint8_t data[8]) {
    qbuf b;
    qb_init(&b, out, cap);
    qb_type(&b, WB_QUIC_FRAME_PATH_RESPONSE);
    qb_bytes(&b, data, 8);
    return qb_finish(&b);
}

int wb_quic_parse_path_data(const uint8_t *body, size_t len, uint8_t data[8]) {
    if (!body || len != 8) return -1;
    memcpy(data, body, 8);
    return 0;
}

size_t wb_quic_frame_connection_close(uint8_t *out, size_t cap, int application,
                                      uint64_t error, uint64_t frame_type,
                                      const char *reason) {
    qbuf b;
    qb_init(&b, out, cap);
    qb_type(&b, application ? 0x1du : WB_QUIC_FRAME_CONNECTION_CLOSE);
    qb_varint(&b, error);
    if (!application) qb_varint(&b, frame_type);
    size_t rl = reason ? strlen(reason) : 0;
    qb_varint(&b, rl);
    qb_bytes(&b, reason, rl);
    return qb_finish(&b);
}

int wb_quic_parse_connection_close(const uint8_t *body, size_t len,
                                   int application, uint64_t *error,
                                   uint64_t *frame_type, const char **reason,
                                   size_t *reason_len) {
    size_t p = 0, u;
    uint64_t v;
    if (wb_quic_varint_decode(body + p, len - p, &v, &u) != 0) return -1;
    *error = v;
    p += u;
    uint64_t ft = 0;
    if (!application) {
        if (wb_quic_varint_decode(body + p, len - p, &v, &u) != 0) return -1;
        ft = v;
        p += u;
    }
    *frame_type = ft;
    if (wb_quic_varint_decode(body + p, len - p, &v, &u) != 0) return -1;
    p += u;
    if (p + v > len) return -1;
    *reason = (const char *)(body + p);
    *reason_len = (size_t)v;
    p += (size_t)v;
    if (p != len) return -1;
    return 0;
}
