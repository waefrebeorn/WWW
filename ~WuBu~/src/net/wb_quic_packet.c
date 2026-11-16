/*
 * wb_quic_packet.c -- LP-93 stone 7: a packet on the wire.
 */
#include "wb_quic_packet.h"

#include <string.h>

static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

int wb_quic_packet_build(uint8_t *out, size_t *len, size_t cap,
                         const wb_quic_keys *k, int type,
                         const uint8_t *dcid, size_t dcid_len,
                         const uint8_t *scid, size_t scid_len,
                         uint64_t pn, int pn_len,
                         const uint8_t *payload, size_t payload_len,
                         const uint8_t *token, size_t token_len) {
    if (!out || !len || !k) return -1;
    if (pn_len < 1 || pn_len > 4) return -1;
    if (dcid_len > 20 || scid_len > 20) return -1;
    if (type != WB_QUIC_INITIAL && type != WB_QUIC_ZERO_RTT &&
        type != WB_QUIC_HANDSHAKE)
        return -1;
    /* The number has to fit the encoding the receiver will read. */
    if (pn >= (UINT64_C(1) << (8 * pn_len))) return -1;

    uint8_t tlen[8], lenb[8], pnbuf[4];
    size_t tl = wb_quic_varint_encode(token_len, tlen);
    /* RFC 9000 section 17.2: the Length field covers the Packet Number and the
     * Payload, and the Payload is the AEAD output with its tag. */
    size_t ll = wb_quic_varint_encode((uint64_t)pn_len + payload_len + 16u, lenb);
    for (int i = 0; i < pn_len; i++)
        pnbuf[pn_len - 1 - i] = (uint8_t)(pn >> (8 * i));

    size_t fixed = 1u + 4u + 1u + dcid_len + 1u + scid_len;
    size_t n = fixed;
    if (type == WB_QUIC_INITIAL) n += tl + token_len + ll;
    else n += ll;
    n += (size_t)pn_len + payload_len;

    size_t need = n + WB_QUIC_GCM_TAG_LEN;   /* room for the seal */
    if (cap < need) return -1;

    out[0] = (uint8_t)(0xc0u | ((uint8_t)(type & 3) << 4) |
                       (uint8_t)(pn_len - 1));
    put_be32(out + 1, WB_QUIC_VERSION_1);
    out[5] = (uint8_t)dcid_len;
    if (dcid_len) memcpy(out + 6, dcid, dcid_len);
    out[6 + dcid_len] = (uint8_t)scid_len;
    if (scid_len) memcpy(out + 7 + dcid_len, scid, scid_len);

    size_t p = fixed;
    if (type == WB_QUIC_INITIAL) {
        memcpy(out + p, tlen, tl);
        p += tl;
        if (token_len) {
            memcpy(out + p, token, token_len);
            p += token_len;
        }
    }
    memcpy(out + p, lenb, ll);
    p += ll;
    size_t pn_offset = p;
    memcpy(out + p, pnbuf, (size_t)pn_len);
    p += (size_t)pn_len;
    if (payload_len) {
        memcpy(out + p, payload, payload_len);
        p += payload_len;
    }
    *len = p;

    return wb_quic_protect(k, out, len, cap, pn_offset, (size_t)pn_len, pn);
}

int wb_quic_packet_open(uint8_t *pkt, size_t *len,
                        uint8_t *scratch, size_t scratch_cap,
                        const wb_quic_keys *k, uint32_t version,
                        wb_quic_hdr *hdr, uint64_t *pn, size_t *payload_off) {
    if (!pkt || !len || !k || !hdr) return -1;
    if (scratch_cap < *len) return -1;
    if (wb_quic_parse_long_header(pkt, *len, hdr) != 0) return -1;
    if (hdr->version != version) return -1;
    if (hdr->type != WB_QUIC_INITIAL && hdr->type != WB_QUIC_ZERO_RTT &&
        hdr->type != WB_QUIC_HANDSHAKE)
        return -1;

    size_t pn_len = (size_t)(pkt[0] & 0x03u) + 1u;
    size_t p = hdr->header_len;
    if (hdr->type == WB_QUIC_INITIAL) {
        uint64_t token_len = 0;
        size_t used = 0;
        if (wb_quic_varint_decode(pkt + p, *len - p, &token_len, &used) != 0)
            return -1;
        p += used + (size_t)token_len;
    }
    /* The Length field follows the token for an Initial and the fixed header
     * otherwise; the packet number follows it. */
    {
        uint64_t body = 0;
        size_t used = 0;
        if (wb_quic_varint_decode(pkt + p, *len - p, &body, &used) != 0) return -1;
        p += used;
    }
    size_t pn_offset = p;

    /* The packet number's encoded length is itself protected, so the receiver
     * does not know it. RFC 9001 section 5.4.2 takes the sample at a fixed
     * offset of four bytes past the packet number for exactly this reason.
     * Try each length and take the one whose restored header agrees. */
    uint8_t *clear = scratch;
    int found = 0;
    for (int pnl = 1; pnl <= 4 && !found; pnl++) {
        memcpy(clear, pkt, *len);
        size_t cl = *len;
        if (wb_quic_unprotect(k, clear, &cl, pn_offset, (size_t)pnl, 1) != 0)
            continue;
        if (((clear[0] & 0x03u) + 1u) != (size_t)pnl) continue;
        memcpy(pkt, clear, cl);
        *len = cl;
        pn_len = (size_t)pnl;
        found = 1;
    }
    if (!found) return -1;

    uint64_t num = 0;
    for (size_t i = 0; i < pn_len; i++) num = (num << 8) | pkt[pn_offset + i];
    if (pn) *pn = num;
    if (payload_off) *payload_off = pn_offset + pn_len;
    return 0;
}
