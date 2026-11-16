/*
 * wb_quic_short.h -- LP-93 stone 8: the short header and 1-RTT.
 *
 * Application data travels in a short header packet (RFC 9000 section 17.3.1).
 * It is the same protection as a long header's, but the header itself carries
 * no Length and no Version -- only the connection ID the recipient chose, then
 * the packet number. That is what makes 1-RTT cheap: one byte plus the
 * connection ID, against a long header's five fixed bytes, a length and a
 * version.
 *
 * Three things a short header has that a long one does not:
 *
 *   - The Fixed Bit (0x40) must be 1; a packet with it clear is not valid and
 *     must be discarded.
 *   - The two Reserved Bits (0x18) must be zero before protection, and a
 *     recipient must treat a non-zero value after removing both protections as
 *     PROTOCOL_VIOLATION. They are protected, so they only read correctly
 *     after unprotecting.
 *   - The Key Phase bit (0x04), which selects the packet protection key set.
 *     It is protected too, so the recipient tries.
 *
 * Key update (RFC 9001 section 6.1): the next write secret is
 * HKDF-Expand-Label(secret, "quic ku", "", 32); the key and IV are re-derived
 * from it and the header protection key is *not* changed. The Key Phase bit
 * toggles.
 *
 * C11, no external deps.
 */
#ifndef WB_QUIC_SHORT_H
#define WB_QUIC_SHORT_H

#include <stddef.h>
#include <stdint.h>

#include "wb_quic_protect.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Advance a key set to the next key phase (RFC 9001 section 6.1). `k` must hold
 * the current secret; on success it holds the next phase's key, IV and secret,
 * the same header protection key, and `key_phase` toggled. Returns 0, or -1 if
 * the derivation fails. */
int wb_quic_keys_update(wb_quic_keys *k);

/* Build a protected short-header packet. `dcid` is the recipient's connection
 * ID, in full and in the clear. `spin` is the latency spin bit (section 17.4)
 * and `key_phase` selects the key set; the caller passes the matching set in
 * `k`. Returns 0 and sets `*len`, or -1 on bad arguments or a small buffer. */
int wb_quic_short_build(uint8_t *out, size_t *len, size_t cap,
                        const wb_quic_keys *k, int spin, int key_phase,
                        const uint8_t *dcid, size_t dcid_len,
                        uint64_t pn, int pn_len,
                        const uint8_t *payload, size_t payload_len);

/* Unprotect a short-header packet in place. `keys[0..key_count-1]` are the key
 * sets the recipient holds; the encoded packet number length and the key phase
 * are both protected, so each is tried against a copy until one authenticates
 * and its restored header agrees with the set used. On success `*len` is the
 * clear length, `*key_phase` which set authenticated, `*pn` the packet number
 * and `*payload_off` where the frames start. Returns -1 if nothing
 * authenticates or the packet is not a valid short header, leaving `pkt`
 * untouched. */
int wb_quic_short_open(uint8_t *pkt, size_t *len,
                       uint8_t *scratch, size_t scratch_cap,
                       const wb_quic_keys *keys, int key_count,
                       const uint8_t *dcid, size_t dcid_len,
                       int *key_phase, uint64_t *pn, size_t *payload_off);

#ifdef __cplusplus
}
#endif

#endif /* WB_QUIC_SHORT_H */
