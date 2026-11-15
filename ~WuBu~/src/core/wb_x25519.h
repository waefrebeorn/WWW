/*
 * wb_x25519.h -- LP-70 stone 4: the Curve25519 key exchange.
 *
 * X25519 turns a 32-byte private scalar and a peer's 32-byte public key into a
 * shared secret. It is the exchange that lets two hosts agree on a channel key
 * before any packet is sealed with `wb_aes_gcm`.
 *
 * The ladder runs in constant time with respect to the scalar: the same field
 * operations in the same order for every bit, and the conditional swaps are
 * done with a mask rather than a branch.
 *
 * C11. Field arithmetic uses the compiler's 128-bit integer for the 51x51-bit
 * limb products, as `wb_math` and `wb_det` already do.
 */
#ifndef WB_X25519_H
#define WB_X25519_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Derive this side's public key from a 32-byte private scalar. The scalar is
 * clamped internally, so any 32 bytes are a valid private key. */
void wb_x25519_public(uint8_t pub[32], const uint8_t priv[32]);

/* Derive the shared secret with a peer. Returns 0 normally. Returns -1 if the
 * result is the all-zero value, which happens when the peer's public key is a
 * low-order point; a caller MUST refuse such a peer (RFC 7748 section 6.1). */
int wb_x25519_shared(uint8_t shared[32], const uint8_t priv[32],
                     const uint8_t peer[32]);

/* Raw X25519: a scalar and a u-coordinate in, a u-coordinate out. Exposed for
 * the test vectors, which are stated in terms of the function itself. */
void wb_x25519(uint8_t out[32], const uint8_t scalar[32],
               const uint8_t u[32]);

#ifdef __cplusplus
}
#endif

#endif /* WB_X25519_H */
