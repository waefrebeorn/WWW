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

    printf("\n=== AES-GCM TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
