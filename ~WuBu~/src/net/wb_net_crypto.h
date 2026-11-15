/*
 * wb_net_crypto.h -- LP-70 stone 5: the sealed channel.
 *
 * Two hosts exchange X25519 public keys, derive a channel key with HKDF, and
 * then seal every packet with AES-256-GCM. This is the pad's deliverable: the
 * transport, the reliability classes, the lockstep stepper and the anti-cheat
 * surface (LP-35) all get their confidentiality and integrity from here.
 *
 * The key derivation follows RFC 7748 section 6.1: the shared secret is NOT
 * used as the key directly. It is the IKM, and the salt is both public keys,
 * so the key depends on which peers are talking, not just on the DH value.
 * RFC 5869 section 3.3 says the same about Diffie-Hellman values.
 *
 * The packet sequence number is part of the nonce, so a replayed packet fails
 * authentication rather than being delivered twice.
 *
 * C11, no external deps.
 */
#ifndef WB_NET_CRYPTO_H
#define WB_NET_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wb_net_crypto wb_net_crypto;

/* Create one side of a channel from a 32-byte private scalar. The scalar is
 * clamped by X25519 internally, so any 32 bytes are usable. Fills `pub_out`
 * with this side's public key, which is what gets sent to the peer.
 * Returns NULL only on allocation failure. */
wb_net_crypto *wb_net_crypto_new(const uint8_t priv[32], uint8_t pub_out[32]);
void wb_net_crypto_free(wb_net_crypto *c);

/* This side's public key, for the handshake. */
void wb_net_crypto_public(const wb_net_crypto *c, uint8_t pub[32]);

/* Finish the handshake with the peer's public key. Returns 0 on success, -1 if
 * the exchange produced nothing usable (a low-order peer) or the channel is not
 * in a state to accept one. A channel is ready after a successful handshake. */
int wb_net_crypto_handshake(wb_net_crypto *c, const uint8_t peer_pub[32]);

int wb_net_crypto_ready(const wb_net_crypto *c);

/* Seal one packet. `seq` is the packet sequence number and forms part of the
 * nonce, so the same plaintext at a different sequence authenticates
 * differently. `aad` (typically the packet header) is authenticated in clear.
 * Returns 0 on success, -1 if the channel is not ready. */
int wb_net_crypto_seal(wb_net_crypto *c, uint32_t seq,
                       const void *aad, size_t aad_len,
                       const void *pt, size_t pt_len,
                       uint8_t *ct, uint8_t tag[16]);

/* Open a sealed packet. Returns 0 only if the tag verifies; -1 means the
 * packet was tampered with, replayed, or addressed to a different sequence,
 * and `pt` is left untouched. */
int wb_net_crypto_open(wb_net_crypto *c, uint32_t seq,
                       const void *aad, size_t aad_len,
                       const void *ct, size_t ct_len,
                       const uint8_t tag[16], void *pt);

#ifdef __cplusplus
}
#endif

#endif /* WB_NET_CRYPTO_H */
