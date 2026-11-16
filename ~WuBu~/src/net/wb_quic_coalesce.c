/*
 * wb_quic_coalesce.c -- LP-93 stone 12: a datagram may hold several packets.
 */
#include "wb_quic_coalesce.h"

#include "wb_quic.h"

#include <string.h>

int wb_quic_coalesce_split(const uint8_t *dgram, size_t len,
                           wb_quic_coalesced *out, int max, int *count) {
    if (!dgram || !out || !count || max <= 0) return -1;

    int n = 0;
    size_t p = 0;
    uint8_t first_dcid[20];
    size_t first_dcid_len = 0;
    int have_first_dcid = 0;

    while (p < len) {
        if (n >= max) return -1;

        uint8_t b0 = dgram[p];
        if (b0 & 0x80u) {
            /* a long header: the Length field says where this packet ends */
            wb_quic_hdr h;
            if (wb_quic_parse_long_header(dgram + p, len - p, &h) != 0) return -1;
            if (h.type == -1) return -1;   /* Version Negotiation: never coalesced */

            /* the connection ID must match the first packet's */
            if (!have_first_dcid) {
                memcpy(first_dcid, h.dcid, h.dcid_len);
                first_dcid_len = h.dcid_len;
                have_first_dcid = 1;
            } else if (h.dcid_len != first_dcid_len ||
                       memcmp(h.dcid, first_dcid, h.dcid_len) != 0) {
                return -1;
            }

            size_t q = p + h.header_len;
            if (h.type == WB_QUIC_INITIAL) {
                uint64_t token_len = 0;
                size_t used = 0;
                if (wb_quic_varint_decode(dgram + q, len - q,
                                          &token_len, &used) != 0)
                    return -1;
                q += used + (size_t)token_len;
            }
            uint64_t body = 0;
            size_t used = 0;
            if (wb_quic_varint_decode(dgram + q, len - q, &body, &used) != 0)
                return -1;
            q += used;

            /* RFC 9000 section 17.2: the Length covers the Packet Number and
             * the Payload, so the packet ends that far past the number. `q` is
             * an absolute offset, so the length is relative to `p`. */
            uint64_t pkt_len = (uint64_t)(q - p) + body;
            if (pkt_len > (uint64_t)(len - p)) return -1;

            out[n].offset = p;
            out[n].len = (size_t)pkt_len;
            out[n].is_short = 0;
            out[n].type = h.type;
            n++;
            p += (size_t)pkt_len;
        } else {
            /* A short header has no Length, so it is necessarily the last
             * packet: everything from here to the end of the datagram is its
             * payload (RFC 9000 section 12.2). Nothing can follow it, so there
             * is nothing to check. */
            out[n].offset = p;
            out[n].len = len - p;
            out[n].is_short = 1;
            out[n].type = -1;
            n++;
            p = len;
        }
    }

    *count = n;
    return 0;
}
