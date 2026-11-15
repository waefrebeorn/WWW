/*
 * wb_aes_gcm.h -- LP-70 stone 3: AES-256-GCM, the sealed channel's cipher.
 *
 * GCM is an AEAD: it encrypts AND authenticates, which is what a game
 * transport needs -- a swapped, replayed or truncated packet must be rejected,
 * not silently simulated. The tag covers the associated data as well, so the
 * packet's header (sequence, class, lengths) can be authenticated in place
 * rather than trusted.
 *
 * Only the forward cipher is implemented; GCM never needs the inverse.
 *
 * C11, no external deps.
 */
#ifndef WB_AES_GCM_H
#define WB_AES_GCM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wb_aes256 wb_aes256;

/* Key the cipher. Returns NULL only on allocation failure. */
wb_aes256 *wb_aes256_new(const uint8_t key[32]);
void wb_aes256_free(wb_aes256 *a);

/* Encrypt one 16-byte block. Exposed for the GHASH key (E(K, 0)) and for
 * tests. */
void wb_aes256_block(const wb_aes256 *a, const uint8_t in[16], uint8_t out[16]);

typedef struct wb_aes128 wb_aes128;

/* AES-128, which QUIC's Initial packets use. Same shape as wb_aes256. */
wb_aes128 *wb_aes128_new(const uint8_t key[16]);
void wb_aes128_free(wb_aes128 *a);
void wb_aes128_block(const wb_aes128 *a, const uint8_t in[16], uint8_t out[16]);

int wb_aes128_gcm_encrypt(const wb_aes128 *a,
                          const uint8_t *iv, size_t iv_len,
                          const uint8_t *aad, size_t aad_len,
                          const uint8_t *pt, size_t pt_len,
                          uint8_t *ct, uint8_t tag[16]);
int wb_aes128_gcm_decrypt(const wb_aes128 *a,
                          const uint8_t *iv, size_t iv_len,
                          const uint8_t *aad, size_t aad_len,
                          const uint8_t *ct, size_t ct_len,
                          const uint8_t tag[16], uint8_t *pt);

/* Seal `pt` into `ct` (same length) and write the 16-byte tag. `iv` may be any
 * length; 96 bits is the case the spec defines directly and the one the net
 * code uses. `aad` is authenticated but not encrypted. */
int wb_aes_gcm_encrypt(const wb_aes256 *a,
                       const uint8_t *iv, size_t iv_len,
                       const uint8_t *aad, size_t aad_len,
                       const uint8_t *pt, size_t pt_len,
                       uint8_t *ct, uint8_t tag[16]);

/* Open a sealed packet. Returns 0 only if the tag verifies; a mismatched tag
 * is an authentication failure (-1) and `pt` is left untouched. */
int wb_aes_gcm_decrypt(const wb_aes256 *a,
                       const uint8_t *iv, size_t iv_len,
                       const uint8_t *aad, size_t aad_len,
                       const uint8_t *ct, size_t ct_len,
                       const uint8_t tag[16], uint8_t *pt);

#ifdef __cplusplus
}
#endif

#endif /* WB_AES_GCM_H */
