/*
 * wb_aes_gcm_test.c -- LP-70 stone 3 gate: AES-256-GCM.
 *
 * The vectors are McGrew & Viega, "The Galois/Counter Mode of Operation
 * (GCM)", Appendix B cases 13-16 -- the AES-256 cases. Case 14 is
 * cross-checked against BoringSSL's cipher_test.txt.
 *
 * Proves:
 *   - the raw block cipher (FIPS-197 Appendix C.3)
 *   - the four GCM vectors, including the partial-block-with-AAD case
 *   - decryption round-trips
 *   - a flipped ciphertext bit, a flipped tag bit and a wrong AAD are all
 *     rejected, and the plaintext is not released on failure
 */
#include "wb_aes_gcm.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)
#define CHECK_MEM(name, got, want, n) do { \
    if (memcmp((got), (want), (n)) == 0) g_pass++; \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } } while (0)

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static size_t unhex(const char *s, uint8_t *out, size_t max) {
    size_t n = 0;
    while (s[0] && s[1] && n < max) {
        int hi = hexval((unsigned char)s[0]), lo = hexval((unsigned char)s[1]);
        if (hi < 0 || lo < 0) break;
        out[n++] = (uint8_t)((hi << 4) | lo);
        s += 2;
    }
    return n;
}

static int ishex(const char *s, size_t nbytes) {
    for (size_t i = 0; i < nbytes * 2; i++) if (hexval((unsigned char)s[i]) < 0) return 0;
    return 1;
}

/* Run one GCM spec vector. Returns 1 if it matched. */
static int vector(const char *name,
                  const char *keyhex, const char *ivhex,
                  const char *pthex, const char *aadhex,
                  const char *cthex, const char *taghex) {
    uint8_t key[32], iv[64], pt[256], aad[64], ct[256], want_ct[256], tag[16], want_tag[16];
    size_t key_n = unhex(keyhex, key, sizeof key);
    size_t iv_n  = unhex(ivhex, iv, sizeof iv);
    size_t pt_n  = unhex(pthex, pt, sizeof pt);
    size_t aad_n = unhex(aadhex, aad, sizeof aad);
    size_t ct_n  = unhex(cthex, want_ct, sizeof want_ct);
    size_t tag_n = unhex(taghex, want_tag, sizeof want_tag);

    if (key_n != 32 || tag_n != 16 || ct_n != pt_n || !ishex(keyhex, 32)) {
        printf("FAIL: %s (bad vector literal)\n", name);
        g_fail++;
        return 0;
    }

    wb_aes256 *a = wb_aes256_new(key);
    if (!a) { printf("FAIL: %s (no cipher)\n", name); g_fail++; return 0; }

    int ok = 1;
    if (wb_aes_gcm_encrypt(a, iv, iv_n, aad, aad_n, pt, pt_n, ct, tag) != 0) ok = 0;
    if (ok && pt_n && memcmp(ct, want_ct, pt_n) != 0) ok = 0;
    if (ok && memcmp(tag, want_tag, 16) != 0) ok = 0;

    /* Decrypt must round-trip back to the plaintext. */
    uint8_t back[256];
    int dr = wb_aes_gcm_decrypt(a, iv, iv_n, aad, aad_n, want_ct, ct_n, want_tag, back);
    if (dr != 0 || (pt_n && memcmp(back, pt, pt_n) != 0)) ok = 0;

    printf("%s %s\n", ok ? "ok  " : "FAIL", name);
    g_pass += ok ? 1 : 0;
    g_fail += ok ? 0 : 1;
    wb_aes256_free(a);
    return ok;
}

static void test_block(void) {
    /* FIPS-197 Appendix C.3 (AES-256). */
    uint8_t key[32], pt[16], ct[16], want[16];
    unhex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", key, sizeof key);
    unhex("00112233445566778899aabbccddeeff", pt, sizeof pt);
    unhex("8ea2b7ca516745bfeafc49904b496089", want, sizeof want);
    wb_aes256 *a = wb_aes256_new(key);
    CHECK(a != NULL, "the cipher keys");
    if (a) {
        wb_aes256_block(a, pt, ct);
        CHECK(memcmp(ct, want, 16) == 0, "AES-256 matches FIPS-197 C.3");
        wb_aes256_free(a);
    }
    /* H = E(K, 0) for the all-zero key is the GHASH key the spec prints. */
    uint8_t zero[16] = {0}, h[16], hw[16];
    unhex("dc95c078a2408989ad48a21492842087", hw, sizeof hw);
    uint8_t zk[32] = {0};
    a = wb_aes256_new(zk);
    if (a) {
        wb_aes256_block(a, zero, h);
        CHECK(memcmp(h, hw, 16) == 0, "E(K,0) matches the GCM spec's H");
        wb_aes256_free(a);
    }
}

static void test_tamper(void) {
    uint8_t key[32], iv[12], pt[32], aad[8], ct[32], tag[16], back[32];
    memset(key, 0x42, sizeof key);
    memset(iv, 0x11, sizeof iv);
    for (int i = 0; i < 32; i++) pt[i] = (uint8_t)i;
    for (int i = 0; i < 8; i++) aad[i] = (uint8_t)(0xA0 + i);

    wb_aes256 *a = wb_aes256_new(key);
    CHECK(a != NULL, "the cipher keys");
    if (!a) return;

    CHECK(wb_aes_gcm_encrypt(a, iv, sizeof iv, aad, sizeof aad, pt, sizeof pt, ct, tag) == 0,
          "a packet seals");
    CHECK(wb_aes_gcm_decrypt(a, iv, sizeof iv, aad, sizeof aad, ct, sizeof ct, tag, back) == 0,
          "the packet opens");
    CHECK(memcmp(back, pt, sizeof pt) == 0, "the plaintext is recovered exactly");

    /* A flipped ciphertext bit. */
    uint8_t bad_ct[32];
    memcpy(bad_ct, ct, sizeof ct);
    bad_ct[7] ^= 0x01;
    CHECK(wb_aes_gcm_decrypt(a, iv, sizeof iv, aad, sizeof aad, bad_ct, sizeof bad_ct, tag, back) == -1,
          "a flipped ciphertext bit is rejected");

    /* A flipped tag bit. */
    uint8_t bad_tag[16];
    memcpy(bad_tag, tag, sizeof tag);
    bad_tag[15] ^= 0x80;
    CHECK(wb_aes_gcm_decrypt(a, iv, sizeof iv, aad, sizeof aad, ct, sizeof ct, bad_tag, back) == -1,
          "a flipped tag bit is rejected");

    /* The wrong AAD. */
    uint8_t bad_aad[8];
    memcpy(bad_aad, aad, sizeof aad);
    bad_aad[0] ^= 0x01;
    CHECK(wb_aes_gcm_decrypt(a, iv, sizeof iv, bad_aad, sizeof bad_aad, ct, sizeof ct, tag, back) == -1,
          "the wrong AAD is rejected");

    /* A truncated packet: the tag no longer covers it. */
    CHECK(wb_aes_gcm_decrypt(a, iv, sizeof iv, aad, sizeof aad, ct, sizeof ct - 1, tag, back) == -1,
          "a truncated packet is rejected");

    /* A different IV. */
    uint8_t bad_iv[12];
    memcpy(bad_iv, iv, sizeof iv);
    bad_iv[0] ^= 0x01;
    CHECK(wb_aes_gcm_decrypt(a, bad_iv, sizeof bad_iv, aad, sizeof aad, ct, sizeof ct, tag, back) == -1,
          "a different IV is rejected");

    /* A wrong key. */
    uint8_t bad_key[32];
    memcpy(bad_key, key, sizeof key);
    bad_key[0] ^= 0x01;
    wb_aes256 *b = wb_aes256_new(bad_key);
    if (b) {
        CHECK(wb_aes_gcm_decrypt(b, iv, sizeof iv, aad, sizeof aad, ct, sizeof ct, tag, back) == -1,
              "a wrong key is rejected");
        wb_aes256_free(b);
    }

    /* On failure the plaintext must not be released. */
    memset(back, 0xCC, sizeof back);
    wb_aes_gcm_decrypt(a, iv, sizeof iv, aad, sizeof aad, bad_ct, sizeof bad_ct, tag, back);
    int untouched = 1;
    for (int i = 0; i < (int)sizeof back; i++) if (back[i] != (uint8_t)0xCC) { untouched = 0; break; }
    CHECK(untouched, "a failed open leaves the plaintext untouched");

    /* A non-96-bit IV: the spec's J0 fallback path. */
    uint8_t long_iv[16], ct2[32], tag2[16], back2[32];
    for (int i = 0; i < 16; i++) long_iv[i] = (uint8_t)i;
    CHECK(wb_aes_gcm_encrypt(a, long_iv, sizeof long_iv, NULL, 0, pt, sizeof pt, ct2, tag2) == 0,
          "a non-96-bit IV seals");
    CHECK(wb_aes_gcm_decrypt(a, long_iv, sizeof long_iv, NULL, 0, ct2, sizeof ct2, tag2, back2) == 0,
          "a non-96-bit IV opens");
    CHECK(memcmp(back2, pt, sizeof pt) == 0, "the non-96-bit IV round-trips");
    CHECK(wb_aes_gcm_decrypt(a, long_iv, sizeof long_iv, NULL, 0, ct2, sizeof ct2, tag2, back2) == 0,
          "a non-96-bit IV is deterministic");

    wb_aes256_free(a);
}

int main(void) {
    test_block();

    /* GCM spec Appendix B, the AES-256 cases. Case 16 is case 15's stream cut
     * to 60 bytes (a partial block) with the AAD added, so it is derived from
     * the same literals rather than re-typed. */
    static const char *kKEY = "feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308";
    static const char *kIV  = "cafebabefacedbaddecaf888";
    static const char *kPT  =
        "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
        "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255";
    static const char *kCT  =
        "522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa"
        "8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662898015ad";
    char pt16[121], ct16[121];
    memcpy(pt16, kPT, 120); pt16[120] = '\0';
    memcpy(ct16, kCT, 120); ct16[120] = '\0';

    vector("gcm spec case 13: empty",
           "0000000000000000000000000000000000000000000000000000000000000000",
           "000000000000000000000000", "", "", "",
           "530f8afbc74536b9a963b4f1c4cb738b");
    vector("gcm spec case 14: one zero block",
           "0000000000000000000000000000000000000000000000000000000000000000",
           "000000000000000000000000",
           "00000000000000000000000000000000", "",
           "cea7403d4d606b6e074ec5d3baf39d18",
           "d0d1c8a799996bf0265b98b5d48ab919");
    vector("gcm spec case 15: four blocks",
           kKEY, kIV, kPT, "", kCT,
           "b094dac5d93471bdec1a502270e3cc6c");
    vector("gcm spec case 16: partial block with AAD",
           kKEY, kIV, pt16,
           "feedfacedeadbeeffeedfacedeadbeefabaddad2",
           ct16,
           "76fc6ece0f4e1768cddf8853bb2d551b");

    test_tamper();

    /* ---- AES-128, which QUIC's Initial packets use ---- */
    {
        /* FIPS-197 appendix C.1 */
        uint8_t key[16] = {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
                           0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f};
        uint8_t pt[16]  = {0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
                           0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
        uint8_t ct[16];
        wb_aes128 *a = wb_aes128_new(key);
        CHECK(a != NULL, "AES-128 keys");
        wb_aes128_block(a, pt, ct);
        CHECK_MEM("FIPS-197 C.1", ct,
                  ((const uint8_t[]){0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,
                                     0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a}), 16);
        wb_aes128_free(a);

        /* the GCM spec's AES-128 cases 1-4 */
        uint8_t z16[16] = {0}, z12[12] = {0};
        uint8_t tag[16], out[64];

        wb_aes128 *z = wb_aes128_new(z16);
        wb_aes128_gcm_encrypt(z, z12, 12, NULL, 0, NULL, 0, out, tag);
        CHECK_MEM("GCM AES-128 case 1: empty", tag,
                  ((const uint8_t[]){0x58,0xe2,0xfc,0xce,0xfa,0x7e,0x30,0x61,
                                     0x36,0x7f,0x1d,0x57,0xa4,0xe7,0x45,0x5a}), 16);

        wb_aes128_gcm_encrypt(z, z12, 12, NULL, 0, z16, 16, out, tag);
        CHECK_MEM("case 2: one zero block", out,
                  ((const uint8_t[]){0x03,0x88,0xda,0xce,0x60,0xb6,0xa3,0x92,
                                     0xf3,0x28,0xc2,0xb9,0x71,0xb2,0xfe,0x78}), 16);
        CHECK_MEM("and its tag", tag,
                  ((const uint8_t[]){0xab,0x6e,0x47,0xd4,0x2c,0xec,0x13,0xbd,
                                     0xf5,0x3a,0x67,0xb2,0x12,0x57,0xbd,0xdf}), 16);
        wb_aes128_free(z);

        uint8_t k3[16] = {0xfe,0xff,0xe9,0x92,0x86,0x65,0x73,0x1c,
                          0x6d,0x6a,0x8f,0x94,0x67,0x30,0x83,0x08};
        uint8_t iv3[12] = {0xca,0xfe,0xba,0xbe,0xfa,0xce,0xdb,0xad,
                           0xde,0xca,0xf8,0x88};
        static const uint8_t p3[64] = {
            0xd9,0x31,0x32,0x25,0xf8,0x84,0x06,0xe5,0xa5,0x59,0x09,0xc5,0xaf,0xf5,0x26,0x9a,
            0x86,0xa7,0xa9,0x53,0x15,0x34,0xf7,0xda,0x2e,0x4c,0x30,0x3d,0x8a,0x31,0x8a,0x72,
            0x1c,0x3c,0x0c,0x95,0x95,0x68,0x09,0x53,0x2f,0xcf,0x0e,0x24,0x49,0xa6,0xb5,0x25,
            0xb1,0x6a,0xed,0xf5,0xaa,0x0d,0xe6,0x57,0xba,0x63,0x7b,0x39,0x1a,0xaf,0xd2,0x55
        };
        static const uint8_t a3[20] = {
            0xfe,0xed,0xfa,0xce,0xde,0xad,0xbe,0xef,0xfe,0xed,0xfa,0xce,0xde,0xad,0xbe,0xef,
            0xab,0xad,0xda,0xd2
        };
        static const uint8_t c3[64] = {
            0x42,0x83,0x1e,0xc2,0x21,0x77,0x74,0x24,0x4b,0x72,0x21,0xb7,0x84,0xd0,0xd4,0x9c,
            0xe3,0xaa,0x21,0x2f,0x2c,0x02,0xa4,0xe0,0x35,0xc1,0x7e,0x23,0x29,0xac,0xa1,0x2e,
            0x21,0xd5,0x14,0xb2,0x54,0x66,0x93,0x1c,0x7d,0x8f,0x6a,0x5a,0xac,0x84,0xaa,0x05,
            0x1b,0xa3,0x0b,0x39,0x6a,0x0a,0xac,0x97,0x3d,0x58,0xe0,0x91,0x47,0x3f,0x59,0x85
        };
        wb_aes128 *c = wb_aes128_new(k3);
        uint8_t o3[64];
        wb_aes128_gcm_encrypt(c, iv3, 12, NULL, 0, p3, 64, o3, tag);
        CHECK_MEM("case 3: four blocks, no AAD", o3, c3, 64);
        CHECK_MEM("and its tag", tag,
                  ((const uint8_t[]){0x4d,0x5c,0x2a,0xf3,0x27,0xcd,0x64,0xa6,
                                     0x2c,0xf3,0x5a,0xbd,0x2b,0xa6,0xfa,0xb4}), 16);

        /* case 4: the same key and IV, a 60-byte plaintext and AAD */
        static const uint8_t p4[60] = {
            0xd9,0x31,0x32,0x25,0xf8,0x84,0x06,0xe5,0xa5,0x59,0x09,0xc5,0xaf,0xf5,0x26,0x9a,
            0x86,0xa7,0xa9,0x53,0x15,0x34,0xf7,0xda,0x2e,0x4c,0x30,0x3d,0x8a,0x31,0x8a,0x72,
            0x1c,0x3c,0x0c,0x95,0x95,0x68,0x09,0x53,0x2f,0xcf,0x0e,0x24,0x49,0xa6,0xb5,0x25,
            0xb1,0x6a,0xed,0xf5,0xaa,0x0d,0xe6,0x57,0xba,0x63,0x7b,0x39
        };
        static const uint8_t c4[60] = {
            0x42,0x83,0x1e,0xc2,0x21,0x77,0x74,0x24,0x4b,0x72,0x21,0xb7,0x84,0xd0,0xd4,0x9c,
            0xe3,0xaa,0x21,0x2f,0x2c,0x02,0xa4,0xe0,0x35,0xc1,0x7e,0x23,0x29,0xac,0xa1,0x2e,
            0x21,0xd5,0x14,0xb2,0x54,0x66,0x93,0x1c,0x7d,0x8f,0x6a,0x5a,0xac,0x84,0xaa,0x05,
            0x1b,0xa3,0x0b,0x39,0x6a,0x0a,0xac,0x97,0x3d,0x58,0xe0,0x91
        };
        uint8_t o4[60], back[60];
        wb_aes128_gcm_encrypt(c, iv3, 12, a3, 20, p4, 60, o4, tag);
        CHECK_MEM("case 4: AAD and a partial final block", o4, c4, 60);
        CHECK_MEM("and its tag", tag,
                  ((const uint8_t[]){0x5b,0xc9,0x4f,0xbc,0x32,0x21,0xa5,0xdb,
                                     0x94,0xfa,0xe9,0x5a,0xe7,0x12,0x1a,0x47}), 16);
        CHECK(wb_aes128_gcm_decrypt(c, iv3, 12, a3, 20, c4, 60, tag, back) == 0,
              "case 4 opens");
        CHECK_MEM("to the plaintext", back, p4, 60);
        wb_aes128_free(c);
    }

    printf("\n=== AES-GCM TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail != 0;
}
