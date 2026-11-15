/*
 * wb_hkdf.c -- HKDF-SHA256 (RFC 5869).
 */
#include "wb_hkdf.h"
#include "wb_sha256.h"

#include <stdlib.h>
#include <string.h>

/* Expand: T(n) = HMAC(PRK, T(n-1) | info | n). HMAC, not a bare hash -- the PRK
 * is the HMAC key, so it has to go through the ipad/opad pads. */
static int hkdf_expand(const uint8_t *prk, size_t prk_len,
                       const uint8_t *info, size_t info_len,
                       uint8_t *okm, size_t okm_len) {
    if (okm_len == 0 || okm_len > 255u * 32u) return -1;
    if (!prk) return -1;
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
        wb_hmac_sha256(prk, prk_len, msg, m, t);
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

int wb_hkdf_extract_sha256(const uint8_t *salt, size_t salt_len,
                           const uint8_t *ikm, size_t ikm_len,
                           uint8_t prk[32]) {
    /* PRK = HMAC-Hash(salt, IKM). A zero-length salt is a full block of zeroes,
     * not an empty key. */
    if (salt_len == 0) {
        uint8_t zeros[32];
        memset(zeros, 0, sizeof zeros);
        wb_hmac_sha256(zeros, sizeof zeros, ikm, ikm_len, prk);
    } else {
        wb_hmac_sha256(salt, salt_len, ikm, ikm_len, prk);
    }
    return 0;
}

int wb_hkdf_sha256(const uint8_t *salt, size_t salt_len,
                   const uint8_t *ikm, size_t ikm_len,
                   const uint8_t *info, size_t info_len,
                   uint8_t *okm, size_t okm_len) {
    uint8_t prk[32];
    if (wb_hkdf_extract_sha256(salt, salt_len, ikm, ikm_len, prk) != 0) return -1;
    return hkdf_expand(prk, sizeof prk, info, info_len, okm, okm_len);
}

int wb_hkdf_expand_label(const uint8_t *secret, size_t secret_len,
                         const char *label, const uint8_t *ctx, size_t ctx_len,
                         uint8_t *out, size_t out_len) {
    if (!secret || !label || !out) return -1;
    size_t ll = strlen(label);
    /* HkdfLabel (RFC 8448 section 3):
     *   uint16 length;
     *   opaque label<7..255> = "tls13 " + Label;
     *   opaque context<0..255> = Context;
     * The label vector's 7..255 bound is the TLS 1.3 minimum: "tls13 " is six
     * bytes, so a one-character label is the smallest legal one. */
    if (ll + 6 < 7 || ll + 6 > 255) return -1;
    if (ctx_len > 255 || out_len > 255) return -1;
    uint8_t buf[32 + 255 + 1];
    size_t n = 0;
    buf[n++] = (uint8_t)(out_len >> 8);
    buf[n++] = (uint8_t)(out_len & 0xff);
    buf[n++] = (uint8_t)(ll + 6);
    memcpy(buf + n, "tls13 ", 6);
    n += 6;
    memcpy(buf + n, label, ll);
    n += ll;
    buf[n++] = (uint8_t)ctx_len;
    if (ctx_len) {
        memcpy(buf + n, ctx, ctx_len);
        n += ctx_len;
    }
    return hkdf_expand(secret, secret_len, buf, n, out, out_len);
}
