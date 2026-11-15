/*
 * wb_net_crypto_test.c -- the sealed channel: handshake agreement, seal/open,
 * and every way a packet can be wrong being refused.
 */
#include "../src/net/wb_net_crypto.h"
#include "../src/core/wb_x25519.h"

#include <stdio.h>
#include <string.h>

static int fails = 0;
static int checks = 0;

static void eqint(const char *name, long long got, long long want) {
    checks++;
    if (got != want) {
        fails++;
        printf("FAIL %s: got %lld want %lld\n", name, got, want);
    }
}

static void eqmem(const char *name, const void *a, const void *b, size_t n) {
    checks++;
    if (memcmp(a, b, n) != 0) {
        fails++;
        printf("FAIL %s: bytes differ\n", name);
    }
}

static void neqmem(const char *name, const void *a, const void *b, size_t n) {
    checks++;
    if (memcmp(a, b, n) == 0) {
        fails++;
        printf("FAIL %s: bytes are equal but should differ\n", name);
    }
}

static void key(uint8_t out[32], uint8_t seed) {
    for (int i = 0; i < 32; i++) out[i] = (uint8_t)(seed * 7 + i * 13 + 1);
}

int main(void) {
    uint8_t ka[32], kb[32], pa[32], pb[32];
    key(ka, 1);
    key(kb, 2);

    wb_net_crypto *a = wb_net_crypto_new(ka, pa);
    wb_net_crypto *b = wb_net_crypto_new(kb, pb);

    /* --- a channel that has not shaken hands is inert --- */
    eqint("seal before handshake", wb_net_crypto_seal(a, 0, NULL, 0, "x", 1,
                                                     (uint8_t[1]){0}, (uint8_t[16]){0}), -1);
    {
        uint8_t pt[8] = {0};
        uint8_t tag[16];
        memset(tag, 0, sizeof tag);
        eqint("open before handshake", wb_net_crypto_open(a, 0, NULL, 0,
                                                         "x", 1, tag, pt), -1);
        eqint("not ready", wb_net_crypto_ready(a), 0);
    }

    /* --- public keys are what the handshake exchanges --- */
    {
        uint8_t got[32];
        wb_net_crypto_public(a, got);
        eqmem("public(a) round trips", got, pa, 32);
        uint8_t raw[32];
        wb_x25519_public(raw, ka);
        eqmem("public(a) is x25519(ka)", got, raw, 32);
        wb_x25519_public(raw, kb);
        eqmem("public(b) is x25519(kb)", pb, raw, 32);
    }

    /* --- the handshake --- */
    eqint("handshake a<-b", wb_net_crypto_handshake(a, pb), 0);
    eqint("handshake b<-a", wb_net_crypto_handshake(b, pa), 0);
    eqint("a ready", wb_net_crypto_ready(a), 1);
    eqint("b ready", wb_net_crypto_ready(b), 1);
    eqint("handshake twice refused", wb_net_crypto_handshake(a, pb), -1);

    /* --- both directions carry --- */
    {
        const char *msg = "the quick brown fox jumps over the lazy dog";
        uint8_t aad[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
        uint8_t ct[64], tag[16], pt[64];
        memset(pt, 0xee, sizeof pt);
        size_t n = strlen(msg);

        eqint("seal a->b", wb_net_crypto_seal(a, 7, aad, sizeof aad,
                                              msg, n, ct, tag), 0);
        eqint("open b", wb_net_crypto_open(b, 7, aad, sizeof aad,
                                           ct, n, tag, pt), 0);
        eqmem("plaintext survives", pt, msg, n);
        /* The same packet at the same sequence opens again: the channel
         * authenticates the sequence, it does not order packets. Replay
         * defence belongs to the transport's sequence numbers, which already
         * drop a duplicate. */
        eqint("same packet at same seq opens (ordering is the transport's job)",
              wb_net_crypto_open(b, 7, aad, sizeof aad, ct, n, tag, pt), 0);

        /* the reverse direction, with the same channel */
        eqint("seal b->a", wb_net_crypto_seal(b, 7, aad, sizeof aad,
                                              msg, n, ct, tag), 0);
        eqint("open a", wb_net_crypto_open(a, 7, aad, sizeof aad,
                                           ct, n, tag, pt), 0);
        eqmem("plaintext survives (reverse)", pt, msg, n);
    }

    /* --- an empty packet --- */
    {
        uint8_t tag[16], pt[1];
        eqint("seal empty", wb_net_crypto_seal(a, 8, NULL, 0, NULL, 0,
                                               NULL, tag), 0);
        eqint("open empty", wb_net_crypto_open(b, 8, NULL, 0, NULL, 0,
                                               tag, pt), 0);
    }

    /* --- every way a packet can be wrong --- */
    {
        const char *msg = "authenticated, so it cannot be edited";
        uint8_t aad[8] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x11, 0x22};
        uint8_t ct[64], tag[16], pt[64];
        size_t n = strlen(msg);
        eqint("seal", wb_net_crypto_seal(a, 9, aad, sizeof aad, msg, n, ct, tag), 0);

        uint8_t bad[64];
        memcpy(bad, ct, n);
        bad[0] ^= 1;
        memset(pt, 0xee, sizeof pt);
        eqint("tampered ciphertext refused",
              wb_net_crypto_open(b, 9, aad, sizeof aad, bad, n, tag, pt), -1);
        eqint("tampered ciphertext left pt alone", pt[0], 0xee);

        uint8_t badtag[16];
        memcpy(badtag, tag, 16);
        badtag[0] ^= 1;
        eqint("tampered tag refused",
              wb_net_crypto_open(b, 9, aad, sizeof aad, ct, n, badtag, pt), -1);

        uint8_t badaad[8];
        memcpy(badaad, aad, sizeof aad);
        badaad[0] ^= 1;
        eqint("tampered aad refused",
              wb_net_crypto_open(b, 9, badaad, sizeof aad, ct, n, tag, pt), -1);

        eqint("wrong sequence refused",
              wb_net_crypto_open(b, 10, aad, sizeof aad, ct, n, tag, pt), -1);

        /* truncated: the tag covers the whole ciphertext */
        eqint("truncated ciphertext refused",
              wb_net_crypto_open(b, 9, aad, sizeof aad, ct, n - 1, tag, pt), -1);

        /* a different channel, same sequence, same plaintext: no key */
        uint8_t kc[32], pc[32];
        key(kc, 3);
        wb_net_crypto *c = wb_net_crypto_new(kc, pc);
        eqint("third party handshake", wb_net_crypto_handshake(c, pb), 0);
        memset(pt, 0xee, sizeof pt);
        eqint("third party cannot open",
              wb_net_crypto_open(c, 9, aad, sizeof aad, ct, n, tag, pt), -1);
        wb_net_crypto_free(c);
    }

    /* --- a low-order peer is refused, not connected --- */
    {
        uint8_t kd[32], pd[32];
        key(kd, 4);
        wb_net_crypto *d = wb_net_crypto_new(kd, pd);
        uint8_t zero[32];
        memset(zero, 0, sizeof zero);
        eqint("zero peer refused", wb_net_crypto_handshake(d, zero), -1);
        eqint("zero peer not ready", wb_net_crypto_ready(d), 0);
        wb_net_crypto_free(d);
    }

    /* --- the two sides really derived the same key: a packet sealed by one is
     *     openable by the other, and a packet sealed by the other by the one --- */
    {
        uint8_t buf[32], tag[16], pt[32];
        for (int i = 0; i < 32; i++) buf[i] = (uint8_t)i;
        for (uint32_t seq = 100; seq < 140; seq++) {
            eqint("seal seq", wb_net_crypto_seal(a, seq, NULL, 0, buf, 32, pt, tag), 0);
            uint8_t rt[32];
            eqint("open seq", wb_net_crypto_open(b, seq, NULL, 0, pt, 32, tag, rt), 0);
            eqmem("round trip seq", rt, buf, 32);
        }
        /* and the same plaintext at a different sequence authenticates
         * differently, so the sequence really is in the nonce */
        uint8_t t1[16], t2[16], c1[32], c2[32];
        wb_net_crypto_seal(a, 1, NULL, 0, buf, 32, c1, t1);
        wb_net_crypto_seal(a, 2, NULL, 0, buf, 32, c2, t2);
        neqmem("tag depends on sequence", t1, t2, 16);
        neqmem("ciphertext depends on sequence", c1, c2, 32);
    }

    wb_net_crypto_free(a);
    wb_net_crypto_free(b);
    printf("\n=== NET CRYPTO TESTS: %d pass, %d fail ===\n", checks, fails);
    return fails != 0;
}
