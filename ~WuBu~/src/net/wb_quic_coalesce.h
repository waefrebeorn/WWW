/*
 * wb_quic_coalesce.h -- LP-93 stone 12: a datagram may hold several packets.
 *
 * RFC 9000 section 12.2: Initial, 0-RTT and Handshake packets carry a Length
 * field, so a sender can put several of them in one UDP datagram and a receiver
 * must split them. A short-header packet has no Length, so it can only be last.
 * Retry and Version Negotiation packets have no Length either and are never
 * coalesced.
 *
 * The split is structural -- it uses the Length field, which is in the clear --
 * so it does not need keys. Every packet in the datagram is separate and
 * complete: the receiver processes and acknowledges each one as though it
 * arrived alone, and if one fails to decrypt it must still attempt the rest.
 *
 * Two rules the splitter enforces, both from section 12.2: a sender must not
 * coalesce packets with different connection IDs, and a packet after a
 * short-header packet is not a legal datagram.
 *
 * C11, no external deps.
 */
#ifndef WB_QUIC_COALESCE_H
#define WB_QUIC_COALESCE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t offset;   /* where the packet starts in the datagram */
    size_t len;      /* its length */
    int    is_short; /* a short-header packet, which can only be last */
    int    type;     /* the long packet type, or -1 for a short header */
} wb_quic_coalesced;

/* Split a datagram into its QUIC packets. Writes up to `max` entries to `out`
 * and sets `*count`. Returns 0 on success, -1 if the datagram is malformed --
 * a Length field that runs past the datagram, a packet after a short header, or
 * two packets with different Destination Connection IDs. */
int wb_quic_coalesce_split(const uint8_t *dgram, size_t len,
                           wb_quic_coalesced *out, int max, int *count);

#ifdef __cplusplus
}
#endif

#endif /* WB_QUIC_COALESCE_H */
