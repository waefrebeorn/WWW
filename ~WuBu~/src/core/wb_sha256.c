/*
 * wb_sha256.c -- SHA-256 (FIPS 180-4) and HMAC-SHA256 (RFC 2104).
 */
#include "wb_sha256.h"

#include <stdlib.h>
#include <string.h>

static const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

wb_sha256 *wb_sha256_new(void) {
    wb_sha256 *s = (wb_sha256 *)malloc(sizeof *s);
    if (s) wb_sha256_init(s);
    return s;
}

void wb_sha256_free(wb_sha256 *s) { free(s); }

static uint32_t ror32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void wb_sha256_init(wb_sha256 *s) {
    s->h[0] = 0x6a09e667u; s->h[1] = 0xbb67ae85u;
    s->h[2] = 0x3c6ef372u; s->h[3] = 0xa54ff53au;
    s->h[4] = 0x510e527fu; s->h[5] = 0x9b05688cu;
    s->h[6] = 0x1f83d9abu; s->h[7] = 0x5be0cd19u;
    s->bits = 0;
    s->n = 0;
    memset(s->buf, 0, sizeof s->buf);
}

static void sha256_block(wb_sha256 *s, const uint8_t *p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) |
               ((uint32_t)p[4 * i + 2] << 8) | (uint32_t)p[4 * i + 3];
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ror32(w[i - 15], 7) ^ ror32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ror32(w[i - 2], 17) ^ ror32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3];
    uint32_t e = s->h[4], f = s->h[5], g = s->h[6], hh = s->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = hh + S1 + ch + K[i] + w[i];
        uint32_t S0 = ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        hh = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d;
    s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += hh;
}

void wb_sha256_update(wb_sha256 *s, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    s->bits += (uint64_t)len * 8u;
    if (s->n) {
        size_t need = 64 - s->n;
        if (len < need) {
            memcpy(s->buf + s->n, p, len);
            s->n += len;
            return;
        }
        memcpy(s->buf + s->n, p, need);
        sha256_block(s, s->buf);
        s->n = 0;
        p += need;
        len -= need;
    }
    while (len >= 64) {
        sha256_block(s, p);
        p += 64;
        len -= 64;
    }
    if (len) {
        memcpy(s->buf, p, len);
        s->n = len;
    }
}

void wb_sha256_final(wb_sha256 *s, uint8_t out[32]) {
    uint64_t bits = s->bits;
    uint8_t pad[72];
    size_t n = s->n;
    size_t padlen = (n < 56) ? (56 - n) : (120 - n);
    memset(pad, 0, sizeof pad);
    pad[0] = 0x80;
    wb_sha256_update(s, pad, padlen);
    uint8_t lenb[8];
    for (int i = 0; i < 8; i++) lenb[i] = (uint8_t)(bits >> (56 - 8 * i));
    wb_sha256_update(s, lenb, 8);
    for (int i = 0; i < 8; i++) {
        out[4 * i] = (uint8_t)(s->h[i] >> 24);
        out[4 * i + 1] = (uint8_t)(s->h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(s->h[i] >> 8);
        out[4 * i + 3] = (uint8_t)(s->h[i]);
    }
}

void wb_sha256_digest(const void *data, size_t len, uint8_t out[32]) {
    wb_sha256 s;
    wb_sha256_init(&s);
    wb_sha256_update(&s, data, len);
    wb_sha256_final(&s, out);
}

void wb_hmac_sha256(const uint8_t *key, size_t key_len,
                    const void *msg, size_t msg_len, uint8_t out[32]) {
    uint8_t k[64];
    memset(k, 0, sizeof k);
    if (key_len > 64) {
        wb_sha256_digest(key, key_len, k);
    } else if (key_len) {
        memcpy(k, key, key_len);
    }
    uint8_t ki[64], ko[64], inner[32];
    for (int i = 0; i < 64; i++) {
        ki[i] = k[i] ^ 0x36;
        ko[i] = k[i] ^ 0x5c;
    }
    wb_sha256 s;
    wb_sha256_init(&s);
    wb_sha256_update(&s, ki, 64);
    wb_sha256_update(&s, msg, msg_len);
    wb_sha256_final(&s, inner);
    wb_sha256_init(&s);
    wb_sha256_update(&s, ko, 64);
    wb_sha256_update(&s, inner, 32);
    wb_sha256_final(&s, out);
}
