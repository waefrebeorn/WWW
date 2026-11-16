/*
 * wb_quic_packet.h -- LP-93 stone 7: a packet on the wire.
 *
 * Every earlier stone is a piece of this: the header (stone 1), the frames
 * (stone 2), the protection (stone 3) and the keys per space (stone 6). This
 * composes them, which is the first place a frame actually becomes a packet.
 *
 * Building takes the sender's choices -- which long type, which connection IDs,
 * which packet number and how many bytes to spend encoding it, and an optional
 * token for an Initial -- plus the already-encoded frame bytes, and returns the
 * protected packet. The Length field is written for the protected packet, so it
 * counts the packet number, the payload and the 16-byte authentication tag
 * (RFC 9000 section 17.2, RFC 9001 section 5.1).
 *
 * Opening is the reverse: strip header protection, read the packet number and
 * the header, and hand back the payload offset. A packet that fails
 * authentication is left untouched and reported as an error.
 *
 * Only long headers are wired. Short headers carry 1-RTT data and need a key
 * update protocol that this stack does not have yet.
 *
 * C11, no external deps.
 */
#ifndef WB_QUIC_PACKET_H
#define WB_QUIC_PACKET_H

#include <stddef.h>
#include <stdint.h>

#include "wb_quic.h"
#include "wb_quic_protect.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build a protected long-header packet.
 *
 * `type` is WB_QUIC_INITIAL, WB_QUIC_ZERO_RTT or WB_QUIC_HANDSHAKE. `token` is
 * only read for an Initial and may be NULL when `token_len` is 0. `pn_len` is
 * 1..4 and must be enough to hold `pn`. `payload` is the encoded frames; it is
 * copied, not retained.
 *
 * Returns 0 and sets `*len`, or -1 if the arguments are inconsistent or the
 * buffer is too small. */
int wb_quic_packet_build(uint8_t *out, size_t *len, size_t cap,
                         const wb_quic_keys *k, int type,
                         const uint8_t *dcid, size_t dcid_len,
                         const uint8_t *scid, size_t scid_len,
                         uint64_t pn, int pn_len,
                         const uint8_t *payload, size_t payload_len,
                         const uint8_t *token, size_t token_len);

/* Unprotect a long-header packet in place and describe it. On success `pkt`
 * holds the clear packet of `*len` bytes, `*hdr` the parsed header, `*pn` the
 * packet number and `*payload_off` where the frames start.
 *
 * `scratch` must hold at least `*len` bytes and is used because the encoded
 * packet number length is itself protected: RFC 9001 section 5.4.2 says the
 * receiver "will not know the length of the Packet Number field", so each of
 * the four lengths is tried against a copy until one authenticates and its
 * restored header agrees. Returns -1 if the version is wrong or none
 * authenticates, leaving `pkt` untouched. */
int wb_quic_packet_open(uint8_t *pkt, size_t *len,
                        uint8_t *scratch, size_t scratch_cap,
                        const wb_quic_keys *k, uint32_t version,
                        wb_quic_hdr *hdr, uint64_t *pn, size_t *payload_off);

#ifdef __cplusplus
}
#endif

#endif /* WB_QUIC_PACKET_H */
