/*
 * wb_hkdf.c -- HKDF-SHA256 (RFC 5869).
 */
#include "wb_hkdf.h"
#include "wb_sha256.h"

#include <stdlib.h>
#include <string.h>

int wb_hkdf_sha256(const uint8_t *salt, size_t salt_len,
                   const uint8_t *ikm, size_t ikm_len,
                   const uint8_t *info, size_t info_len,
                   uint8_t *okm, size_t okm_len) {
    if (okm_len == 0 || okm_len > 255u * 32u) return -1;

    /* Extract: PRK = HMAC-Hash(salt, IKM). A zero-length salt is a full block
     * of zeroes, not an empty key. */
    uint8_t prk[32];
    if (salt_len == 0) {
        uint8_t zeros[32];
        memset(zeros, 0, sizeof zeros);
        wb_hmac_sha256(zeros, sizeof zeros, ikm, ikm_len, prk);
    } else {
        wb_hmac_sha256(salt, salt_len, ikm, ikm_len, prk);
    }

    /* Expand: T(n) = HMAC(PRK, T(n-1) | info | n). HMAC, not a bare hash --
     * the PRK is the HMAC key, so it has to go through the ipad/opad pads. */
    uint8_t *msg = (uint8_t *)malloc(32 + info_len + 1);
    if (!msg) return -1;
    uint8_t t[32];
    size_t t_len = 0;
    uint8_t counter = 1;
    size_t done = 0;
    while (done < okm_len) {
        size_t m = 0;
        if (t_len) {
            memcpy(msg, t, t_len);
            m = t_len;
        }
        if (info_len) {
            memcpy(msg + m, info, info_len);
            m += info_len;
        }
        msg[m++] = counter;
        wb_hmac_sha256(prk, sizeof prk, msg, m, t);
        t_len = 32;
        size_t take = okm_len - done;
        if (take > 32) take = 32;
        memcpy(okm + done, t, take);
        done += take;
        counter++;
    }
    free(msg);
    return 0;
}
