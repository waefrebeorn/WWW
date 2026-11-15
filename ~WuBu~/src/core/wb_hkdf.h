/*
 * wb_hkdf.h -- HKDF-SHA256 (RFC 5869).
 *
 * Turn the X25519 shared secret into a channel key. The shared secret is NOT
 * a uniform random string, so it must go through the extract step rather than
 * being used as a key directly (RFC 5869 section 3.3 says the same thing about
 * Diffie-Hellman values).
 *
 * C11, no external deps.
 */
#ifndef WB_HKDF_H
#define WB_HKDF_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Derive `okm_len` bytes. A zero-length salt means "no salt", which RFC 5869
 * defines as HashLen zero octets. `okm_len` must be <= 255 * 32. Returns 0 on
 * success, -1 if the request is out of range. */
int wb_hkdf_sha256(const uint8_t *salt, size_t salt_len,
                   const uint8_t *ikm, size_t ikm_len,
                   const uint8_t *info, size_t info_len,
                   uint8_t *okm, size_t okm_len);

/* Extract only: PRK = HMAC-Hash(salt, IKM). RFC 9001's initial_secret is the
 * extract step and nothing more, so it must be reachable on its own. */
int wb_hkdf_extract_sha256(const uint8_t *salt, size_t salt_len,
                           const uint8_t *ikm, size_t ikm_len,
                           uint8_t prk[32]);

/* HKDF-Expand-Label, as TLS 1.3 and QUIC use it (RFC 8448 section 3). `secret`
 * is the PRK: this is Expand, not Extract-then-Expand. `ctx` is the label's
 * context, which QUIC passes empty. Returns 0 on success. */
int wb_hkdf_expand_label(const uint8_t *secret, size_t secret_len,
                         const char *label, const uint8_t *ctx, size_t ctx_len,
                         uint8_t *out, size_t out_len);

#ifdef __cplusplus
}
#endif

#endif /* WB_HKDF_H */
