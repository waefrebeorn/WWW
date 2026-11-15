/*
 * wb_aes_gcm.c -- AES-256 and GCM. See wb_aes_gcm.h.
 *
 * The GCM arithmetic follows McGrew & Viega, "The Galois/Counter Mode of
 * Operation (GCM)", Appendix A; the test vectors are its Appendix B cases
 * 13-16 (AES-256).
 *
 * GHASH multiplies in GF(2^128) modulo x^128 + x^7 + x^2 + x + 1. Blocks are
 * read as bit strings with the first byte most significant, so the reduction
 * constant is 0xE1 in the HIGH word and the shift is a plain right shift of the
 * big-endian pair.
 */
#include "wb_aes_gcm.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ AES */

static const uint8_t k_sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

struct wb_aes256 {
    uint8_t rk[15][16];         /* 14 rounds + 1, AES-256 */
};

#define AES_ROTL8(x, n) ((uint8_t)(((x) << (n)) | ((x) >> (8 - (n)))))

static void aes256_expand(wb_aes256 *a, const uint8_t key[32]) {
    static const uint8_t rcon[8] = { 0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40 };
    uint8_t *w = &a->rk[0][0];
    memcpy(w, key, 32);
    for (int i = 8; i < 60; i++) {
        uint8_t t[4];
        memcpy(t, w + 4 * (i - 1), 4);
        if (i % 8 == 0) {
            uint8_t s = t[0];
            t[0] = k_sbox[t[1]] ^ rcon[i / 8];
            t[1] = k_sbox[t[2]];
            t[2] = k_sbox[t[3]];
            t[3] = k_sbox[s];
        } else if (i % 8 == 4) {
            for (int j = 0; j < 4; j++) t[j] = k_sbox[t[j]];
        }
        for (int j = 0; j < 4; j++) w[4 * i + j] = w[4 * (i - 8) + j] ^ t[j];
    }
}

static void aes256_add_round_key(uint8_t st[16], const uint8_t *rk) {
    for (int i = 0; i < 16; i++) st[i] ^= rk[i];
}

static void aes256_sub_bytes(uint8_t st[16]) {
    for (int i = 0; i < 16; i++) st[i] = k_sbox[st[i]];
}

static void aes256_shift_rows(uint8_t st[16]) {
    /* state[r][c] = st[r + 4c]; row r rotates left by r. */
    uint8_t t;
    t = st[1]; st[1] = st[5]; st[5] = st[9]; st[9] = st[13]; st[13] = t;
    t = st[2]; st[2] = st[10]; st[10] = t;
    t = st[6]; st[6] = st[14]; st[14] = t;
    t = st[15]; st[15] = st[11]; st[11] = st[7]; st[7] = st[3]; st[3] = t;
}

static void aes256_mix_columns(uint8_t st[16]) {
    for (int c = 0; c < 4; c++) {
        uint8_t *p = st + 4 * c;
        uint8_t a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];
        uint8_t x = a0 ^ a1 ^ a2 ^ a3;
        p[0] ^= x ^ (uint8_t)((a0 ^ a1) << 1 ^ ((a0 ^ a1) >> 7) * 0x1B);
        p[1] ^= x ^ (uint8_t)((a1 ^ a2) << 1 ^ ((a1 ^ a2) >> 7) * 0x1B);
        p[2] ^= x ^ (uint8_t)((a2 ^ a3) << 1 ^ ((a2 ^ a3) >> 7) * 0x1B);
        p[3] ^= x ^ (uint8_t)((a3 ^ a0) << 1 ^ ((a3 ^ a0) >> 7) * 0x1B);
    }
}

void wb_aes256_block(const wb_aes256 *a, const uint8_t in[16], uint8_t out[16]) {
    uint8_t st[16];
    memcpy(st, in, 16);
    aes256_add_round_key(st, a->rk[0]);
    for (int round = 1; round <= 13; round++) {
        aes256_sub_bytes(st);
        aes256_shift_rows(st);
        aes256_mix_columns(st);
        aes256_add_round_key(st, a->rk[round]);
    }
    aes256_sub_bytes(st);
    aes256_shift_rows(st);
    aes256_add_round_key(st, a->rk[14]);
    memcpy(out, st, 16);
}

wb_aes256 *wb_aes256_new(const uint8_t key[32]) {
    wb_aes256 *a = (wb_aes256 *)calloc(1, sizeof *a);
    if (!a) return NULL;
    aes256_expand(a, key);
    return a;
}

void wb_aes256_free(wb_aes256 *a) { free(a); }

/* ------------------------------------------------------------------ GCM */

typedef struct { uint64_t hi, lo; } wb_gf128;

static wb_gf128 gf_from_block(const uint8_t b[16]) {
    wb_gf128 x;
    x.hi = x.lo = 0;
    for (int i = 0; i < 8; i++) x.hi = (x.hi << 8) | b[i];
    for (int i = 8; i < 16; i++) x.lo = (x.lo << 8) | b[i];
    return x;
}

static void gf_to_block(wb_gf128 x, uint8_t out[16]) {
    for (int i = 0; i < 8; i++) out[i]     = (uint8_t)(x.hi >> (56 - 8 * i));
    for (int i = 0; i < 8; i++) out[8 + i] = (uint8_t)(x.lo >> (56 - 8 * i));
}

/* Multiply X by Y in GF(2^128) modulo the GCM polynomial. */
static wb_gf128 gf_mul(wb_gf128 x, wb_gf128 y) {
    wb_gf128 z;
    z.hi = z.lo = 0;
    for (int i = 0; i < 128; i++) {
        uint64_t bit = (i < 64) ? (x.hi >> (63 - i)) : (x.lo >> (63 - (i - 64)));
        if (bit & 1) { z.hi ^= y.hi; z.lo ^= y.lo; }
        uint64_t lsb = y.lo & 1;
        y.lo = (y.lo >> 1) | (y.hi << 63);
        y.hi >>= 1;
        if (lsb) y.hi ^= 0xE100000000000000ull;
    }
    return z;
}

/* GHASH over a byte stream already laid out as the spec requires. */
static void ghash(wb_gf128 h, const uint8_t *data, size_t len, wb_gf128 *x) {
    for (size_t off = 0; off < len; off += 16) {
        uint8_t block[16];
        size_t n = len - off < 16 ? len - off : 16;
        memset(block, 0, sizeof block);
        memcpy(block, data + off, n);
        wb_gf128 b = gf_from_block(block);
        wb_gf128 t;
        t.hi = x->hi ^ b.hi;
        t.lo = x->lo ^ b.lo;
        *x = gf_mul(t, h);
    }
}

/* Hash AAD || pad || C || pad || len(A) || len(C), the GCM input string. */
static wb_gf128 ghash_message(wb_gf128 h,
                              const uint8_t *aad, size_t aad_len,
                              const uint8_t *ct, size_t ct_len) {
    size_t total = ((aad_len + 15) & ~(size_t)15) +
                   ((ct_len + 15) & ~(size_t)15) + 16;
    uint8_t *buf = (uint8_t *)calloc(1, total ? total : 1);
    if (!buf) return (wb_gf128){ 0, 0 };
    if (aad_len) memcpy(buf, aad, aad_len);
    size_t off = ((aad_len + 15) & ~(size_t)15);
    if (ct_len) memcpy(buf + off, ct, ct_len);
    off += ((ct_len + 15) & ~(size_t)15);
    uint64_t abits = (uint64_t)aad_len * 8;
    uint64_t cbits = (uint64_t)ct_len * 8;
    for (int i = 0; i < 8; i++) buf[off + i]     = (uint8_t)(abits >> (56 - 8 * i));
    for (int i = 0; i < 8; i++) buf[off + 8 + i] = (uint8_t)(cbits >> (56 - 8 * i));
    wb_gf128 x;
    x.hi = x.lo = 0;
    ghash(h, buf, total, &x);
    free(buf);
    return x;
}

/* J0: for a 96-bit IV it is IV || 0^31 || 1; otherwise GHASH of the padded IV
 * and its length. */
static void gcm_j0(const uint8_t *iv, size_t iv_len,
                   wb_gf128 h, uint8_t j0[16]) {
    if (iv_len == 12) {
        memcpy(j0, iv, 12);
        j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1;
        return;
    }
    uint8_t lenbuf[8];
    uint64_t ibits = (uint64_t)iv_len * 8;
    for (int i = 0; i < 8; i++) lenbuf[i] = (uint8_t)(ibits >> (56 - 8 * i));
    size_t total = ((iv_len + 15) & ~(size_t)15) + 16;
    uint8_t *buf = (uint8_t *)calloc(1, total);
    if (!buf) { memset(j0, 0, 16); return; }
    memcpy(buf, iv, iv_len);
    memcpy(buf + ((iv_len + 15) & ~(size_t)15), lenbuf, 8);
    wb_gf128 x;
    x.hi = x.lo = 0;
    ghash(h, buf, total, &x);
    gf_to_block(x, j0);
    free(buf);
}

static void inc32(uint8_t ctr[16]) {
    for (int i = 15; i >= 12; i--) {
        if (++ctr[i] != 0) break;
    }
}

static void gcm_crypt(const wb_aes256 *a, const uint8_t j0[16],
                      const uint8_t *in, size_t len, uint8_t *out) {
    uint8_t ctr[16], ks[16];
    memcpy(ctr, j0, 16);
    for (size_t off = 0; off < len; off += 16) {
        inc32(ctr);
        wb_aes256_block(a, ctr, ks);
        size_t n = len - off < 16 ? len - off : 16;
        for (size_t i = 0; i < n; i++) out[off + i] = in[off + i] ^ ks[i];
    }
}

static int gcm_tag(const wb_aes256 *a, uint8_t j0[16], wb_gf128 h,
                   const uint8_t *aad, size_t aad_len,
                   const uint8_t *ct, size_t ct_len, uint8_t tag[16]) {
    wb_gf128 x = ghash_message(h, aad, aad_len, ct, ct_len);
    uint8_t ej0[16];
    wb_aes256_block(a, j0, ej0);
    uint8_t xb[16];
    gf_to_block(x, xb);
    for (int i = 0; i < 16; i++) tag[i] = xb[i] ^ ej0[i];
    return 0;
}

/* `in` is the plaintext when sealing and the ciphertext when opening; `out` is
 * the other one. The plaintext is written only after the tag verifies. */
static int gcm_seal(const wb_aes256 *a,
                    const uint8_t *iv, size_t iv_len,
                    const uint8_t *aad, size_t aad_len,
                    const uint8_t *in, size_t len,
                    uint8_t *out, const uint8_t tag[16], int encrypt) {
    if (!a || !iv) return -1;
    if (len && (!in || !out)) return -1;
    if (aad_len && !aad) return -1;

    uint8_t zero[16] = {0};
    uint8_t h[16];
    wb_aes256_block(a, zero, h);    /* H = E(K, 0^128) */
    wb_gf128 H = gf_from_block(h);

    uint8_t j0[16];
    gcm_j0(iv, iv_len, H, j0);

    if (encrypt) {
        gcm_crypt(a, j0, in, len, out);
        uint8_t made[16];
        gcm_tag(a, j0, H, aad, aad_len, out, len, made);
        memcpy((uint8_t *)tag, made, 16);
        return 0;
    }

    /* Verify BEFORE releasing any plaintext: a packet that fails authentication
     * must not reach the caller at all. */
    uint8_t want[16];
    gcm_tag(a, j0, H, aad, aad_len, in, len, want);
    if (memcmp(want, tag, 16) != 0) return -1;
    gcm_crypt(a, j0, in, len, out);
    return 0;
}

int wb_aes_gcm_encrypt(const wb_aes256 *a,
                       const uint8_t *iv, size_t iv_len,
                       const uint8_t *aad, size_t aad_len,
                       const uint8_t *pt, size_t pt_len,
                       uint8_t *ct, uint8_t tag[16]) {
    return gcm_seal(a, iv, iv_len, aad, aad_len, pt, pt_len, ct, tag, 1);
}

int wb_aes_gcm_decrypt(const wb_aes256 *a,
                       const uint8_t *iv, size_t iv_len,
                       const uint8_t *aad, size_t aad_len,
                       const uint8_t *ct, size_t ct_len,
                       const uint8_t tag[16], uint8_t *pt) {
    return gcm_seal(a, iv, iv_len, aad, aad_len, ct, ct_len, pt, tag, 0);
}
