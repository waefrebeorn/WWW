/*
 * wb_sha256_test.c -- FIPS 180-4 SHA-256 vectors, RFC 5869 HKDF-SHA256 vectors,
 * and the incremental-vs-one-shot equivalence.
 */
#include "../src/core/wb_sha256.h"
#include "../src/core/wb_hkdf.h"

#include <stdio.h>
#include <string.h>

static int fails = 0;
static int checks = 0;

static void hex(const uint8_t *b, size_t n, char *out) {
    static const char *d = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = d[b[i] >> 4];
        out[2 * i + 1] = d[b[i] & 15];
    }
    out[2 * n] = 0;
}

static void eqhex(const char *name, const uint8_t *got, size_t n,
                  const char *want) {
    char g[256];
    hex(got, n, g);
    checks++;
    if (strcmp(g, want) != 0) {
        fails++;
        printf("FAIL %s\n  got  %s\n  want %s\n", name, g, want);
    }
}

static void eqint(const char *name, long long got, long long want) {
    checks++;
    if (got != want) {
        fails++;
        printf("FAIL %s: got %lld want %lld\n", name, got, want);
    }
}

static void hash_str(const char *name, const char *in, const char *want) {
    uint8_t out[32];
    wb_sha256_digest(in, strlen(in), out);
    eqhex(name, out, 32, want);
}

int main(void) {
    /* --- FIPS 180-4 --- */
    hash_str("sha256 empty",
             "",
             "e3b0c44298fc1c149afbf4c8996fb924"
             "27ae41e4649b934ca495991b7852b855");
    hash_str("sha256 abc",
             "abc",
             "ba7816bf8f01cfea414140de5dae2223"
             "b00361a396177a9cb410ff61f20015ad");
    hash_str("sha256 56 bytes",
             "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
             "248d6a61d20638b8e5c026930c3e6039"
             "a33ce45964ff2167f6ecedd419db06c1");
    hash_str("sha256 112 bytes",
             "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
             "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
             "cf5b16a778af8380036ce59e7b049237"
             "0b249b11e8f07a51afac45037afee9d1");

    /* --- incremental == one-shot --- */
    {
        /* 200 bytes, so the split points below are all inside the input. */
        char in[201];
        for (int i = 0; i < 200; i++) in[i] = (char)('a' + (i % 26));
        in[200] = 0;
        size_t n = 200;
        uint8_t one[32], inc[32];
        wb_sha256_digest(in, n, one);
        wb_sha256 s;
        wb_sha256_init(&s);
        for (size_t i = 0; i < n; i++) wb_sha256_update(&s, in + i, 1);
        wb_sha256_final(&s, inc);
        checks++;
        if (memcmp(one, inc, 32) != 0) {
            fails++;
            printf("FAIL sha256 incremental != one-shot\n");
        }
        /* split at a block boundary: 64 bytes then the rest */
        wb_sha256_init(&s);
        wb_sha256_update(&s, in, 64);
        wb_sha256_update(&s, in + 64, n - 64);
        wb_sha256_final(&s, inc);
        checks++;
        if (memcmp(one, inc, 32) != 0) {
            fails++;
            printf("FAIL sha256 split-at-64 != one-shot\n");
        }
        /* the long pad path: an input that lands on 55..63 bytes buffered */
        wb_sha256_init(&s);
        wb_sha256_update(&s, in, 55);
        wb_sha256_update(&s, in + 55, n - 55);
        wb_sha256_final(&s, inc);
        checks++;
        if (memcmp(one, inc, 32) != 0) {
            fails++;
            printf("FAIL sha256 split-at-55 != one-shot\n");
        }
        /* split at 63, the last byte before the buffer is full */
        wb_sha256_init(&s);
        wb_sha256_update(&s, in, 63);
        wb_sha256_update(&s, in + 63, n - 63);
        wb_sha256_final(&s, inc);
        checks++;
        if (memcmp(one, inc, 32) != 0) {
            fails++;
            printf("FAIL sha256 split-at-63 != one-shot\n");
        }
        /* empty update is a no-op */
        wb_sha256_init(&s);
        wb_sha256_update(&s, in, 0);
        wb_sha256_update(&s, in, n);
        wb_sha256_final(&s, inc);
        checks++;
        if (memcmp(one, inc, 32) != 0) {
            fails++;
            printf("FAIL sha256 empty-update != one-shot\n");
        }
    }

    /* --- RFC 5869 A.1 --- */
    {
        uint8_t ikm[22], salt[13], info[10], okm[42], prk[32];
        memset(ikm, 0x0b, sizeof ikm);
        for (int i = 0; i < 13; i++) salt[i] = (uint8_t)i;
        for (int i = 0; i < 10; i++) info[i] = (uint8_t)(0xf0 + i);

        /* PRK is the extract step; HKDF's expand is checked through OKM. */
        wb_hmac_sha256(salt, sizeof salt, ikm, sizeof ikm, prk);
        eqhex("hkdf A.1 PRK", prk, 32,
              "077709362c2e32df0ddc3f0dc47bba63"
              "90b6c73bb50f9c3122ec844ad7c2b3e5");

        eqint("hkdf A.1 rc", wb_hkdf_sha256(salt, sizeof salt, ikm, sizeof ikm,
                                            info, sizeof info, okm, sizeof okm), 0);
        eqhex("hkdf A.1 OKM", okm, 42,
              "3cb25f25faacd57a90434f64d0362f2a"
              "2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
              "34007208d5b887185865");
    }

    /* --- RFC 5869 A.2: long inputs, 82 bytes out (three blocks) --- */
    {
        uint8_t ikm[80], salt[80], info[80], okm[82];
        for (int i = 0; i < 80; i++) { ikm[i] = (uint8_t)i; salt[i] = (uint8_t)(0x60 + i); info[i] = (uint8_t)(0xb0 + i); }
        wb_hmac_sha256(salt, sizeof salt, ikm, sizeof ikm, (uint8_t *)okm);
        eqhex("hkdf A.2 PRK", okm, 32,
              "06a6b88c5853361a06104c9ceb35b45c"
              "ef760014904671014a193f40c15fc244");
        eqint("hkdf A.2 rc", wb_hkdf_sha256(salt, sizeof salt, ikm, sizeof ikm,
                                            info, sizeof info, okm, sizeof okm), 0);
        eqhex("hkdf A.2 OKM", okm, 82,
              "b11e398dc80327a1c8e7f78c596a4934"
              "4f012eda2d4efad8a050cc4c19afa97c"
              "59045a99cac7827271cb41c65e590e09"
              "da3275600c2f09b8367793a9aca3db71"
              "cc30c58179ec3e87c14c01d5c1f3434f"
              "1d87");
    }

    /* --- RFC 5869 A.3: zero-length salt and info --- */
    {
        uint8_t ikm[22], okm[42];
        memset(ikm, 0x0b, sizeof ikm);
        uint8_t prk[32];
        wb_hmac_sha256((const uint8_t *)"", 0, ikm, sizeof ikm, prk);
        eqhex("hkdf A.3 PRK", prk, 32,
              "19ef24a32c717b167f33a91d6f648bdf"
              "96596776afdb6377ac434c1c293ccb04");
        eqint("hkdf A.3 rc", wb_hkdf_sha256(NULL, 0, ikm, sizeof ikm,
                                            NULL, 0, okm, sizeof okm), 0);
        eqhex("hkdf A.3 OKM", okm, 42,
              "8da4e775a563c18f715f802a063c5a31"
              "b8a11f5c5ee1879ec3454e5f3c738d2d"
              "9d201395faa4b61a96c8");
    }

    /* --- bounds: too much output is refused, not truncated --- */
    {
        uint8_t ikm[8], okm[255 * 32];
        memset(ikm, 1, sizeof ikm);
        eqint("hkdf max output rc", wb_hkdf_sha256(NULL, 0, ikm, sizeof ikm,
                                                   NULL, 0, okm, sizeof okm), 0);
        eqint("hkdf over-long rc", wb_hkdf_sha256(NULL, 0, ikm, sizeof ikm,
                                                  NULL, 0, okm, sizeof okm + 1), -1);
        eqint("hkdf zero output rc", wb_hkdf_sha256(NULL, 0, ikm, sizeof ikm,
                                                    NULL, 0, okm, 0), -1);
    }

    printf("\n=== SHA256/HMAC/HKDF TESTS: %d pass, %d fail ===\n", checks, fails);
    return fails != 0;
}
