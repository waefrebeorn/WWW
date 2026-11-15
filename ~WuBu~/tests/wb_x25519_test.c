/*
 * wb_x25519_test.c -- LP-70 stone 4 gate: the Curve25519 key exchange.
 *
 * Every vector is RFC 7748: the two section 5.2 single-shot vectors, the
 * iterated vector (1 and 1,000 rounds) from the same section, and the
 * section 6.1 Diffie-Hellman exchange.
 */
#include "wb_x25519.h"

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
static int eq32(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, 32) == 0; }

static void test_single(void) {
    uint8_t s[32], u[32], out[32], want[32];

    unhex("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4", s, 32);
    unhex("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c", u, 32);
    unhex("c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552", want, 32);
    wb_x25519(out, s, u);
    CHECK(eq32(out, want), "rfc 7748 5.2: first single-shot vector");

    unhex("4b66e9d4d1b4673c5ad22691957d6af5c11b6421e0ea01d42ca4169e7918ba0d", s, 32);
    unhex("e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493", u, 32);
    unhex("95cbde9476e8907d7aade45cb4b873f88b595a68799fa152e6f8f7647aac7957", want, 32);
    wb_x25519(out, s, u);
    CHECK(eq32(out, want), "rfc 7748 5.2: second single-shot vector");
}

static void test_iterated(void) {
    uint8_t k[32], u[32], want[32];
    memset(k, 0, 32);
    k[0] = 9;
    memcpy(u, k, 32);

    /* One iteration. */
    wb_x25519(k, k, u);
    unhex("422c8e7a6227d7bca1350b3e2bb7279f7897b87bb6854b783c60e80311ae3079", want, 32);
    CHECK(eq32(k, want), "rfc 7748 5.2: one iteration");

    /* On to 1,000: each round sets k to X25519(old k, old u) and u to old k. */
    for (int i = 1; i < 1000; i++) {
        uint8_t old_k[32], old_u[32];
        memcpy(old_k, k, 32);
        memcpy(old_u, u, 32);
        wb_x25519(k, old_k, old_u);
        memcpy(u, old_k, 32);
    }
    unhex("684cf59ba83309552800ef566f2f4d3c1c3887c49360e3875f2eb94d99532c51", want, 32);
    CHECK(eq32(k, want), "rfc 7748 5.2: 1,000 iterations");
}

static void test_dh(void) {
    uint8_t a[32], b[32], ka[32], kb[32], s1[32], s2[32], want[32];

    unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", a, 32);
    unhex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", b, 32);
    unhex("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", want, 32);
    wb_x25519_public(ka, a);
    CHECK(eq32(ka, want), "rfc 7748 6.1: Alice's public key");
    unhex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", want, 32);
    wb_x25519_public(kb, b);
    CHECK(eq32(kb, want), "rfc 7748 6.1: Bob's public key");

    unhex("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", want, 32);
    CHECK(wb_x25519_shared(s1, a, kb) == 0, "Alice derives a shared secret");
    CHECK(eq32(s1, want), "rfc 7748 6.1: Alice's shared secret");
    CHECK(wb_x25519_shared(s2, b, ka) == 0, "Bob derives a shared secret");
    CHECK(eq32(s2, want), "rfc 7748 6.1: Bob's shared secret");
    CHECK(eq32(s1, s2), "both sides agree");
}

static void test_low_order(void) {
    /* A low-order point yields the all-zero shared secret, which the API must
     * report as a refusal rather than hand to the cipher. */
    uint8_t priv[32], zero[32], out[32];
    memset(zero, 0, sizeof zero);
    unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", priv, 32);
    CHECK(wb_x25519_shared(out, priv, zero) == -1,
          "an all-zero public key is refused");
    uint8_t acc = 0;
    for (int i = 0; i < 32; i++) acc |= out[i];
    CHECK(acc == 0, "the all-zero result is the low-order sentinel");

    /* A non-canonical u-coordinate (the high bit set) must be masked, not
     * rejected -- the RFC requires accepting it. */
    uint8_t s[32], u[32], a[32], b[32];
    unhex("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4", s, 32);
    unhex("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c", u, 32);
    wb_x25519(a, s, u);
    u[31] |= 0x80;
    wb_x25519(b, s, u);
    CHECK(eq32(a, b), "the high bit of u is masked");
}

int main(void) {
    test_single();
    test_iterated();
    test_dh();
    test_low_order();

    printf("\n=== X25519 TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
