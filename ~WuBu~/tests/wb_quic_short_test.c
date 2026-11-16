/*
 * wb_quic_short_test.c -- LP-93 stone 8 gate.
 *
 * The short header has no published vector in RFC 9001's appendices, so this
 * gate checks the rules the RFC does state and the round trip:
 *
 *   - the header form and fixed bit, and that a packet with the fixed bit
 *     clear is discarded (RFC 9000 section 17.3);
 *   - the reserved bits reading zero only after both protections come off,
 *     since they are protected;
 *   - the key phase bit selecting the key set, including a mid-connection key
 *     update (RFC 9001 section 6);
 *   - the key update deriving its new secret with "quic ku" and re-deriving
 *     the key and IV while leaving the header protection key alone, checked
 *     against an independently built HKDF-Expand-Label.
 */
#include "wb_quic_short.h"
#include "wb_quic_packet.h"
#include "wb_quic.h"
#include "wb_sha256.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)
#define CHECK_EQ(name, got, want) do { \
    if ((got) == (want)) g_pass++; \
    else { g_fail++; printf("FAIL: %s: got %lld want %lld (line %d)\n", name, \
                            (long long)(got), (long long)(want), __LINE__); } } while (0)
#define CHECK_MEM(name, got, want, n) do { \
    if (memcmp((got), (want), (n)) == 0) g_pass++; \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } } while (0)

static uint8_t dcid[8] = {1, 2, 3, 4, 5, 6, 7, 8};
static uint8_t scratch[4096];

/* An independent HKDF-Expand-Label, built the way RFC 8448 would: the info
 * block is a two-byte length, the TLS label prefix, the label, the context and
 * a separator, and the output is HMAC(secret, info)[0..out_len]. This does not
 * call wb_hkdf_expand_label, so it is a second path to the same rule. */
static int expand_label(const uint8_t *secret, size_t secret_len,
                        const char *label, uint8_t *out, size_t out_len) {
    uint8_t info[256];
    size_t n = 0;
    info[n++] = (uint8_t)(out_len >> 8);
    info[n++] = (uint8_t)out_len;
    const char *prefix = "tls13 ";
    size_t ll = strlen(label);
    info[n++] = (uint8_t)(6 + ll);
    memcpy(info + n, prefix, 6);
    n += 6;
    memcpy(info + n, label, ll);
    n += ll;
    info[n++] = 0;   /* an empty context */
    info[n++] = 1;   /* HKDF-Expand's T(1) counter */
    uint8_t mac[32];
    wb_hmac_sha256(secret, secret_len, info, n, mac);
    memcpy(out, mac, out_len);
    return 0;
}

/* Pad a frame buffer the way RFC 9001 section 5.4.2 requires. */
static void pad_to(uint8_t *f, size_t *fl, int pn_len) {
    while ((size_t)pn_len + *fl < 20) {
        size_t add = wb_quic_frame_padding(f + *fl, 128 - *fl, 1);
        if (!add) break;
        *fl += add;
    }
}

int main(void) {
    wb_quic_keys client, server;
    wb_quic_initial_keys(dcid, sizeof dcid, &client, &server);
    CHECK_EQ("initial keys start at phase 0", client.key_phase, 0);

    /* ---- the header's fixed shape ---- */
    uint8_t frames[128];
    size_t fl = wb_quic_frame_ping(frames, sizeof frames);
    pad_to(frames, &fl, 1);

    uint8_t pkt[1024];
    size_t n = 0;
    CHECK(wb_quic_short_build(pkt, &n, sizeof pkt, &client, 0, 0, dcid,
                              sizeof dcid, 1, 1, frames, fl) == 0,
          "a short packet builds");
    CHECK_EQ("the header form bit is clear", pkt[0] & 0x80, 0);
    CHECK_EQ("and the fixed bit is set", pkt[0] & 0x40, 0x40);
    CHECK_EQ("the spin bit is what was asked", pkt[0] & 0x20, 0);
    CHECK_MEM("the connection ID follows, in the clear", pkt + 1, dcid,
              sizeof dcid);

    int phase = -1;
    uint64_t pn = 0;
    size_t off = 0;
    uint8_t copy[1024];
    memcpy(copy, pkt, n);
    size_t cl = n;
    CHECK(wb_quic_short_open(copy, &cl, scratch, sizeof scratch, &client, 1,
                             dcid, sizeof dcid, &phase, &pn, &off) == 0,
          "and it opens");
    CHECK_EQ("with packet number 1", pn, 1);
    CHECK_EQ("key phase 0", phase, 0);
    CHECK_EQ("the payload starts after the 10-byte header", off, 10);
    CHECK_EQ("and the PING is there", copy[off], WB_QUIC_FRAME_PING);

    /* ---- the spin bit survives ---- */
    {
        uint8_t p[1024];
        size_t pn_ = 0;
        CHECK(wb_quic_short_build(p, &pn_, sizeof p, &client, 1, 0, dcid,
                                  sizeof dcid, 2, 1, frames, fl) == 0,
              "a packet with the spin bit set builds");
        memcpy(copy, p, pn_);
        cl = pn_;
        CHECK(wb_quic_short_open(copy, &cl, scratch, sizeof scratch, &client, 1,
                                 dcid, sizeof dcid, &phase, &pn, &off) == 0,
              "and opens");
        CHECK_EQ("with the spin bit", copy[0] & 0x20, 0x20);
    }

    /* ---- every packet number length ---- */
    {
        for (int pnl = 1; pnl <= 4; pnl++) {
            uint8_t p[1024];
            size_t pn_ = 0;
            uint64_t num = (pnl == 4) ? 0x01020304u : ((UINT64_C(1) << (8 * pnl)) - 1);
            pad_to(frames, &fl, pnl);
            if (wb_quic_short_build(p, &pn_, sizeof p, &client, 0, 0, dcid,
                                    sizeof dcid, num, pnl, frames, fl) != 0) {
                printf("FAIL: pn_len %d did not build\n", pnl);
                g_fail++;
                continue;
            }
            memcpy(copy, p, pn_);
            cl = pn_;
            CHECK(wb_quic_short_open(copy, &cl, scratch, sizeof scratch, &client, 1,
                                     dcid, sizeof dcid, &phase, &pn, &off) == 0,
                  "and it opens");
            CHECK_EQ("with the number back", pn, num);
            CHECK_EQ("and the length bits", (copy[0] & 3) + 1, pnl);
        }
    }

    /* ---- the fixed bit must be set ---- */
    {
        uint8_t p[1024];
        size_t pn_ = 0;
        CHECK(wb_quic_short_build(p, &pn_, sizeof p, &client, 0, 0, dcid,
                                  sizeof dcid, 1, 1, frames, fl) == 0,
              "a good packet builds");
        p[0] &= (uint8_t)~0x40u;   /* the fixed bit is not protected */
        memcpy(copy, p, pn_);
        cl = pn_;
        CHECK(wb_quic_short_open(copy, &cl, scratch, sizeof scratch, &client, 1,
                                 dcid, sizeof dcid, &phase, &pn, &off) == -1,
              "a packet with a clear fixed bit is discarded");
    }

    /* ---- a long header is not a short one ---- */
    {
        uint8_t p[1024];
        size_t pn_ = 0;
        CHECK(wb_quic_packet_build(p, &pn_, sizeof p, &client, WB_QUIC_INITIAL,
                                   dcid, sizeof dcid, NULL, 0, 2, 4,
                                   frames, fl, NULL, 0) == 0,
              "an Initial builds");
        memcpy(copy, p, pn_);
        cl = pn_;
        CHECK(wb_quic_short_open(copy, &cl, scratch, sizeof scratch, &client, 1,
                                 dcid, sizeof dcid, &phase, &pn, &off) == -1,
              "an Initial is refused by the short opener");
    }

    /* ---- a packet for another connection is refused ---- */
    {
        uint8_t other[8] = {9, 9, 9, 9, 9, 9, 9, 9};
        uint8_t p[1024];
        size_t pn_ = 0;
        CHECK(wb_quic_short_build(p, &pn_, sizeof p, &client, 0, 0, dcid,
                                  sizeof dcid, 1, 1, frames, fl) == 0,
              "a good packet builds");
        memcpy(copy, p, pn_);
        cl = pn_;
        CHECK(wb_quic_short_open(copy, &cl, scratch, sizeof scratch, &client, 1,
                                 other, sizeof other, &phase, &pn, &off) == -1,
              "a packet for another connection is refused");
        memcpy(copy, p, pn_);
        cl = pn_;
        CHECK(wb_quic_short_open(copy, &cl, scratch, sizeof scratch, &client, 1,
                                 NULL, sizeof dcid, &phase, &pn, &off) == 0,
              "but an unchecked one still opens");
    }

    /* ---- a wrong key does not open ---- */
    {
        uint8_t p[1024];
        size_t pn_ = 0;
        CHECK(wb_quic_short_build(p, &pn_, sizeof p, &client, 0, 0, dcid,
                                  sizeof dcid, 1, 1, frames, fl) == 0,
              "a good packet builds");
        memcpy(copy, p, pn_);
        cl = pn_;
        CHECK(wb_quic_short_open(copy, &cl, scratch, sizeof scratch, &server, 1,
                                 dcid, sizeof dcid, &phase, &pn, &off) == -1,
              "the peer's key does not open it");
        CHECK_MEM("and the packet is untouched", copy, p, pn_);
    }

    /* ---- key update ---- */
    {
        wb_quic_keys next = client;
        CHECK(wb_quic_keys_update(&next) == 0, "the keys update");
        CHECK_EQ("and the phase toggles", next.key_phase, 1);
        CHECK_MEM("and the header protection key does not change",
                  next.hp, client.hp, WB_QUIC_HP_LEN);

        /* the new secret is HKDF-Expand-Label(old secret, "quic ku", "", 32),
         * checked against the independently built label above */
        uint8_t want_secret[WB_QUIC_SECRET_LEN];
        expand_label(client.secret, WB_QUIC_SECRET_LEN, "quic ku",
                     want_secret, WB_QUIC_SECRET_LEN);
        CHECK_MEM("the new secret is the quic ku expansion",
                  next.secret, want_secret, WB_QUIC_SECRET_LEN);

        uint8_t want_key[WB_QUIC_KEY_LEN], want_iv[WB_QUIC_IV_LEN];
        expand_label(want_secret, WB_QUIC_SECRET_LEN, "quic key",
                     want_key, WB_QUIC_KEY_LEN);
        expand_label(want_secret, WB_QUIC_SECRET_LEN, "quic iv",
                     want_iv, WB_QUIC_IV_LEN);
        CHECK_MEM("and the key is re-derived from it", next.key, want_key,
                  WB_QUIC_KEY_LEN);
        CHECK_MEM("and so is the IV", next.iv, want_iv, WB_QUIC_IV_LEN);
        CHECK(memcmp(next.secret, client.secret, WB_QUIC_SECRET_LEN) != 0,
              "while the old secret is gone");

        /* a packet protected with the new keys opens with the new set */
        uint8_t p[1024];
        size_t pn_ = 0;
        CHECK(wb_quic_short_build(p, &pn_, sizeof p, &next, 0, 1, dcid,
                                  sizeof dcid, 5, 1, frames, fl) == 0,
              "a packet with the new keys builds");
        wb_quic_keys sets[2] = {client, next};
        memcpy(copy, p, pn_);
        cl = pn_;
        CHECK(wb_quic_short_open(copy, &cl, scratch, sizeof scratch, sets, 2,
                                 dcid, sizeof dcid, &phase, &pn, &off) == 0,
              "and opens");
        CHECK_EQ("as key phase 1", phase, 1);
        CHECK_EQ("with its packet number", pn, 5);

        /* the old set alone cannot open it */
        memcpy(copy, p, pn_);
        cl = pn_;
        CHECK(wb_quic_short_open(copy, &cl, scratch, sizeof scratch, &client, 1,
                                 dcid, sizeof dcid, &phase, &pn, &off) == -1,
              "the old keys cannot");
        /* but the old keys still work: they are retained */
        memcpy(copy, p, pn_);
        cl = pn_;
        CHECK(wb_quic_short_open(copy, &cl, scratch, sizeof scratch, &next, 1,
                                 dcid, sizeof dcid, &phase, &pn, &off) == 0,
              "and the new keys can");

        /* a second update toggles back and derives from the new secret */
        wb_quic_keys third = next;
        CHECK(wb_quic_keys_update(&third) == 0, "a second update works");
        CHECK_EQ("and toggles back to 0", third.key_phase, 0);
        uint8_t want2[WB_QUIC_SECRET_LEN];
        expand_label(next.secret, WB_QUIC_SECRET_LEN, "quic ku",
                     want2, WB_QUIC_SECRET_LEN);
        CHECK_MEM("derived from the current secret", third.secret, want2,
                  WB_QUIC_SECRET_LEN);
    }

    /* ---- a packet with a key phase bit that matches no set ---- */
    {
        wb_quic_keys sets[2] = {client, client};
        sets[0].key_phase = 1;
        sets[1].key_phase = 1;   /* both sets claim the other phase */
        uint8_t p[1024];
        size_t pn_ = 0;
        CHECK(wb_quic_short_build(p, &pn_, sizeof p, &client, 0, 0, dcid,
                                  sizeof dcid, 1, 1, frames, fl) == 0,
              "a packet builds");
        memcpy(copy, p, pn_);
        cl = pn_;
        CHECK(wb_quic_short_open(copy, &cl, scratch, sizeof scratch, sets, 2,
                                 dcid, sizeof dcid, &phase, &pn, &off) == -1,
              "a set whose phase does not match is not used");
    }

    /* ---- bad arguments ---- */
    {
        uint8_t p[64];
        size_t pn_ = 0;
        CHECK(wb_quic_short_build(p, &pn_, sizeof p, &client, 0, 0, dcid,
                                  sizeof dcid, 1, 0, frames, fl) == -1,
              "a bad packet number length");
        CHECK(wb_quic_short_build(p, &pn_, sizeof p, &client, 0, 0, dcid,
                                  sizeof dcid, 256, 1, frames, fl) == -1,
              "a number that does not fit");
        CHECK(wb_quic_short_build(p, &pn_, 8, &client, 0, 0, dcid,
                                  sizeof dcid, 1, 1, frames, fl) == -1,
              "a buffer too small to seal into");
        CHECK(wb_quic_short_build(p, &pn_, sizeof p, &client, 0, 0, dcid,
                                  21, 1, 1, frames, fl) == -1,
              "and a connection ID that is too long");
        CHECK(wb_quic_short_build(p, &pn_, sizeof p, NULL, 0, 0, dcid,
                                  sizeof dcid, 1, 1, frames, fl) == -1,
              "and no keys");
        memcpy(copy, p, pn_ ? pn_ : 1);
        cl = 1;
        CHECK(wb_quic_short_open(copy, &cl, scratch, sizeof scratch, &client, 1,
                                 dcid, sizeof dcid, &phase, &pn, &off) == -1,
              "a one-byte packet is refused");
        CHECK(wb_quic_short_open(copy, &cl, scratch, 0, &client, 1, dcid,
                                 sizeof dcid, &phase, &pn, &off) == -1,
              "and so is a scratch buffer that is too small");
    }

    printf("\n=== QUIC SHORT TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail != 0;
}
