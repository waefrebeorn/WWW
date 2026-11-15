/*
 * wb_sha256.h -- SHA-256 and HMAC-SHA256.
 *
 * The hash the net crypto needs: HKDF uses HMAC, and the channel salts its key
 * derivation with the two public keys. Self-contained, no external deps.
 *
 * C11.
 */
#ifndef WB_SHA256_H
#define WB_SHA256_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wb_sha256 {
    uint32_t h[8];
    uint64_t bits;          /* total input length, in bits */
    uint8_t buf[64];
    size_t n;               /* bytes buffered */
} wb_sha256;

wb_sha256 *wb_sha256_new(void);
void wb_sha256_free(wb_sha256 *s);

void wb_sha256_init(wb_sha256 *s);
void wb_sha256_update(wb_sha256 *s, const void *data, size_t len);
void wb_sha256_final(wb_sha256 *s, uint8_t out[32]);

/* One-shot. */
void wb_sha256_digest(const void *data, size_t len, uint8_t out[32]);

/* HMAC-SHA256 (RFC 2104). */
void wb_hmac_sha256(const uint8_t *key, size_t key_len,
                    const void *msg, size_t msg_len, uint8_t out[32]);

#ifdef __cplusplus
}
#endif

#endif /* WB_SHA256_H */
