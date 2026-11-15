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

#ifdef __cplusplus
}
#endif

#endif /* WB_HKDF_H */
