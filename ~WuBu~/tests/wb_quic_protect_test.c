/*
 * wb_quic_protect_test.c -- LP-93 stone 3 gate.
 *
 * Every value here is from RFC 9001's own sample packets (appendix A), not
 * from this implementation: the Initial secrets and keys of A.1, and the
 * client's Initial packet of A.2, protected with a packet number of 2.
 */
#include "wb_quic_protect.h"
#include "wb_hkdf.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)
#define CHECK_MEM(name, got, want, n) do { \
    if (memcmp((got), (want), (n)) == 0) g_pass++; \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } } while (0)
#define CHECK_EQ_BYTES(name, got, want) do { \
    if ((got) == (want)) g_pass++; \
    else { g_fail++; printf("FAIL: %s: got %lu want %lu (line %d)\n", name, \
                            (unsigned long)(got), (unsigned long)(want), __LINE__); } } while (0)

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static size_t unhex(const char *s, uint8_t *out, size_t cap) {
    size_t n = 0;
    while (s[0] && s[1]) {
        if (n >= cap) break;
        out[n++] = (uint8_t)((hexval(s[0]) << 4) | hexval(s[1]));
        s += 2;
    }
    return n;
}

static const uint8_t initial_secret_ref[32] = {
    0x7d, 0xb5, 0xdf, 0x06, 0xe7, 0xa6, 0x9e, 0x43, 0x24, 0x96, 0xad, 0xed,
    0xb0, 0x08, 0x51, 0x92, 0x35, 0x95, 0x22, 0x15, 0x96, 0xae, 0x2a, 0xe9,
    0xfb, 0x81, 0x15, 0xc1, 0xe9, 0xed, 0x0a, 0x44
};

static const uint8_t client_secret_ref[32] = {
    0xc0, 0x0c, 0xf1, 0x51, 0xca, 0x5b, 0xe0, 0x75, 0xed, 0x0e, 0xbf, 0xb5,
    0xc8, 0x03, 0x23, 0xc4, 0x2d, 0x6b, 0x7d, 0xb6, 0x78, 0x81, 0x28, 0x9a,
    0xf4, 0x00, 0x8f, 0x1f, 0x6c, 0x35, 0x7a, 0xea
};

static const uint8_t client_key_ref[16] = {
    0x1f, 0x36, 0x96, 0x13, 0xdd, 0x76, 0xd5, 0x46, 0x77, 0x30, 0xef, 0xcb,
    0xe3, 0xb1, 0xa2, 0x2d
};

static const uint8_t client_iv_ref[12] = {
    0xfa, 0x04, 0x4b, 0x2f, 0x42, 0xa3, 0xfd, 0x3b, 0x46, 0xfb, 0x25, 0x5c
};

static const uint8_t client_hp_ref[16] = {
    0x9f, 0x50, 0x44, 0x9e, 0x04, 0xa0, 0xe8, 0x10, 0x28, 0x3a, 0x1e, 0x99,
    0x33, 0xad, 0xed, 0xd2
};

static const uint8_t server_secret_ref[32] = {
    0x3c, 0x19, 0x98, 0x28, 0xfd, 0x13, 0x9e, 0xfd, 0x21, 0x6c, 0x15, 0x5a,
    0xd8, 0x44, 0xcc, 0x81, 0xfb, 0x82, 0xfa, 0x8d, 0x74, 0x46, 0xfa, 0x7d,
    0x78, 0xbe, 0x80, 0x3a, 0xcd, 0xda, 0x95, 0x1b
};

static const uint8_t server_key_ref[16] = {
    0xcf, 0x3a, 0x53, 0x31, 0x65, 0x3c, 0x36, 0x4c, 0x88, 0xf0, 0xf3, 0x79,
    0xb6, 0x06, 0x7e, 0x37
};

static const uint8_t server_iv_ref[12] = {
    0x0a, 0xc1, 0x49, 0x3c, 0xa1, 0x90, 0x58, 0x53, 0xb0, 0xbb, 0xa0, 0x3e
};

static const uint8_t server_hp_ref[16] = {
    0xc2, 0x06, 0xb8, 0xd9, 0xb9, 0xf0, 0xf3, 0x76, 0x44, 0x43, 0x0b, 0x49,
    0x0e, 0xea, 0xa3, 0x14
};

static const uint8_t MASK_REF[5] = {
    0x43, 0x7b, 0x9a, 0xec, 0x36
};


static const char *SAMPLE_HEX = "d1b1c98dd7689fb8ec11d242b123dc9b";
static const char *UNPROTECTED_HDR = "c300000001088394c8f03e5157080000449e00000002";
static const char *PROTECTED_PKT = "c000000001088394c8f03e5157080000449e7b9aec34d1b1c98dd7689fb8ec11d242b123dc9bd8bab936b47d92ec356c0bab7df5976d27cd449f63300099f3991c260ec4c60d17b31f8429157bb35a1282a643a8d2262cad67500cadb8e7378c8eb7539ec4d4905fed1bee1fc8aafba17c750e2c7ace01e6005f80fcb7df621230c83711b39343fa028cea7f7fb5ff89eac2308249a02252155e2347b63d58c5457afd84d05dfffdb20392844ae812154682e9cf012f9021a6f0be17ddd0c2084dce25ff9b06cde535d0f920a2db1bf362c23e596d11a4f5a6cf3948838a3aec4e15daf8500a6ef69ec4e3feb6b1d98e610ac8b7ec3faf6ad760b7bad1db4ba3485e8a94dc250ae3fdb41ed15fb6a8e5eba0fc3dd60bc8e30c5c4287e53805db059ae0648db2f64264ed5e39be2e20d82df566da8dd5998ccabdae053060ae6c7b4378e846d29f37ed7b4ea9ec5d82e7961b7f25a9323851f681d582363aa5f89937f5a67258bf63ad6f1a0b1d96dbd4faddfcefc5266ba6611722395c906556be52afe3f565636ad1b17d508b73d8743eeb524be22b3dcbc2c7468d54119c7468449a13d8e3b95811a198f3491de3e7fe942b330407abf82a4ed7c1b311663ac69890f4157015853d91e923037c227a33cdd5ec281ca3f79c44546b9d90ca00f064c99e3dd97911d39fe9c5d0b23a229a234cb36186c4819e8b9c5927726632291d6a418211cc2962e20fe47feb3edf330f2c603a9d48c0fcb5699dbfe5896425c5bac4aee82e57a85aaf4e2513e4f05796b07ba2ee47d80506f8d2c25e50fd14de71e6c418559302f939b0e1abd576f279c4b2e0feb85c1f28ff18f58891ffef132eef2fa09346aee33c28eb130ff28f5b766953334113211996d20011a198e3fc433f9f2541010ae17c1bf202580f6047472fb36857fe843b19f5984009ddc324044e847a4f4a0ab34f719595de37252d6235365e9b84392b061085349d73203a4a13e96f5432ec0fd4a1ee65accdd5e3904df54c1da510b0ff20dcc0c77fcb2c0e0eb605cb0504db87632cf3d8b4dae6e705769d1de354270123cb11450efc60ac47683d7b8d0f811365565fd98c4c8eb936bcab8d069fc33bd801b03adea2e1fbc5aa463d08ca19896d2bf59a071b851e6c239052172f296bfb5e72404790a2181014f3b94a4e97d117b438130368cc39dbb2d198065ae3986547926cd2162f40a29f0c3c8745c0f50fba3852e566d44575c29d39a03f0cda721984b6f440591f355e12d439ff150aab7613499dbd49adabc8676eef023b15b65bfc5ca06948109f23f350db82123535eb8a7433bdabcb909271a6ecbcb58b936a88cd4e8f2e6ff5800175f113253d8fa9ca8885c2f552e657dc603f252e1a8e308f76f0be79e2fb8f5d5fbbe2e30ecadd220723c8c0aea8078cdfcb3868263ff8f0940054da48781893a7e49ad5aff4af300cd804a6b6279ab3ff3afb64491c85194aab760d58a606654f9f4400e8b38591356fbf6425aca26dc85244259ff2b19c41b9f96f3ca9ec1dde434da7d2d392b905ddf3d1f9af93d1af5950bd493f5aa731b4056df31bd267b6b90a079831aaf579be0a39013137aac6d404f518cfd46840647e78bfe706ca4cf5e9c5453e9f7cfd2b8b4c8d169a44e55c88d4a9a7f9474241e221af44860018ab0856972e194cd934";
static const char *PAYLOAD = "060040f1010000ed0303ebf8fa56f12939b9584a3896472ec40bb863cfd3e86804fe3a47f06a2b69484c00000413011302010000c000000010000e00000b6578616d706c652e636f6dff01000100000a00080006001d0017001800100007000504616c706e000500050100000000003300260024001d00209370b2c9caa47fbabaf4559fedba753de171fa71f50f1ce15d43e994ec74d748002b0003020304000d0010000e0403050306030203080408050806002d00020101001c00024001003900320408ffffffffffffffff05048000ffff07048000ffff0801100104800075300901100f088394c8f03e51570806048000ffff0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";

static const uint8_t DCID[8] = {0x83,0x94,0xc8,0xf0,0x3e,0x51,0x57,0x08};

int main(void) {
    /* ---- A.1: the Initial secrets and keys ---- */
    wb_quic_keys client, server;
    wb_quic_initial_keys(DCID, sizeof DCID, &client, &server);

    CHECK_MEM("client initial secret", client.secret, client_secret_ref, 32);
    CHECK_MEM("client key", client.key, client_key_ref, 16);
    CHECK_MEM("client iv", client.iv, client_iv_ref, 12);
    CHECK_MEM("client hp", client.hp, client_hp_ref, 16);
    CHECK_MEM("server initial secret", server.secret, server_secret_ref, 32);
    CHECK_MEM("server key", server.key, server_key_ref, 16);
    CHECK_MEM("server iv", server.iv, server_iv_ref, 12);
    CHECK_MEM("server hp", server.hp, server_hp_ref, 16);
    CHECK_MEM("the two directions differ", client.secret, server_secret_ref, 0);
    CHECK(memcmp(client.key, server.key, 16) != 0, "and so do their keys");

    /* the common secret is HKDF-Extract(salt, dcid); recover it and check the
     * labels against A.1's table */
    uint8_t secret[32], lab[16];
    wb_hkdf_extract_sha256(wb_quic_initial_salt_v1, WB_QUIC_INITIAL_SALT_LEN,
                           DCID, sizeof DCID, secret);
    CHECK_MEM("initial secret", secret, initial_secret_ref, 32);
    wb_hkdf_expand_label(secret, 32, "client in", NULL, 0, lab, 32);
    CHECK_MEM("the 'client in' label", lab, client_secret_ref, 32);
    wb_hkdf_expand_label(client.secret, 32, "quic key", NULL, 0, lab, 16);
    CHECK_MEM("the 'quic key' label", lab, client_key_ref, 16);

    /* ---- A.2: the header protection mask ---- */
    uint8_t smp[16], mk[5];
    CHECK_EQ_BYTES("the sample is 16 bytes", unhex(SAMPLE_HEX, smp, sizeof smp), 16);
    wb_quic_hp_mask(client.hp, smp, mk);
    CHECK_MEM("the mask for A.2's sample", mk, MASK_REF, 5);

    /* ---- A.2: protect the packet and compare byte for byte ---- */
    uint8_t payload_buf[1216];
    CHECK_EQ_BYTES("the payload is 1162 bytes",
                   unhex(PAYLOAD, payload_buf, sizeof payload_buf), 1162);

    uint8_t pkt[2048];
    size_t n = unhex(UNPROTECTED_HDR, pkt, sizeof pkt);
    CHECK_EQ_BYTES("the header is 22 bytes", n, 22);
    memcpy(pkt + n, payload_buf, 1162);
    n += 1162;
    CHECK_EQ_BYTES("the unprotected packet is 1184 bytes", n, 1184);

    CHECK(wb_quic_protect(&client, pkt, &n, sizeof pkt, 18, 4, 2) == 0,
          "the packet protects");
    CHECK_EQ_BYTES("and grows by the 16-byte tag", n, 1200);

    uint8_t want[2048];
    size_t wn = unhex(PROTECTED_PKT, want, sizeof want);
    CHECK_EQ_BYTES("the reference packet is 1200 bytes", wn, 1200);
    CHECK_MEM("the protected packet is exactly RFC 9001 A.2's", pkt, want, 1200);

    /* ---- and back ---- */
    uint8_t back[2048];
    memcpy(back, want, wn);
    size_t bl = wn;
    CHECK(wb_quic_unprotect(&client, back, &bl, 18, 4, 1) == 0, "it unprotects");
    CHECK_EQ_BYTES("to the original length", bl, 1184);
    uint8_t orig[2048];
    size_t on = unhex(UNPROTECTED_HDR, orig, sizeof orig);
    memcpy(orig + on, payload_buf, 1162);
    CHECK_MEM("to the original bytes", back, orig, 1184);

    /* ---- a wrong key does not open it ---- */
    uint8_t bad[2048];
    size_t bl2;
    wb_quic_keys wrong;

    memcpy(bad, want, wn); bl2 = wn; wrong = client; wrong.key[0] ^= 1;
    CHECK(wb_quic_unprotect(&wrong, bad, &bl2, 18, 4, 1) == -1,
          "a wrong key refuses");
    memcpy(bad, want, wn); bl2 = wn; wrong = client; wrong.iv[0] ^= 1;
    CHECK(wb_quic_unprotect(&wrong, bad, &bl2, 18, 4, 1) == -1,
          "a wrong iv refuses");
    memcpy(bad, want, wn); bl2 = wn; wrong = client; wrong.hp[0] ^= 1;
    CHECK(wb_quic_unprotect(&wrong, bad, &bl2, 18, 4, 1) == -1,
          "a wrong hp key refuses");

    /* a truncated packet is refused rather than opened */
    memcpy(bad, want, wn - 1); bl2 = wn - 1;
    CHECK(wb_quic_unprotect(&client, bad, &bl2, 18, 4, 1) == -1,
          "a truncated packet is refused");

    /* ---- the sample comes from the ciphertext, so a payload too short to
     * sample cannot be protected ---- */
    uint8_t tiny[64];
    size_t tn = unhex("c300000001088394c8f03e5157080000449e", tiny, sizeof tiny);
    CHECK_EQ_BYTES("an 18-byte header", tn, 18);
    CHECK(wb_quic_protect(&client, tiny, &tn, sizeof tiny, 18, 4, 1) == -1,
          "a payload too short to sample is refused");

    /* the packet number must fit the header's encoding */
    uint8_t pkt2[2048];
    size_t n2 = unhex(UNPROTECTED_HDR, pkt2, sizeof pkt2);
    memcpy(pkt2 + n2, payload_buf, 1162);
    n2 += 1162;
    CHECK(wb_quic_protect(&client, pkt2, &n2, sizeof pkt2, 18, 1, 300) == -1,
          "a packet number that does not fit is refused");

    /* the server's keys protect the server's packets, and a client packet
     * does not open under them */
    memcpy(bad, want, wn); bl2 = wn;
    CHECK(wb_quic_unprotect(&server, bad, &bl2, 18, 4, 1) == -1,
          "the server's keys do not open a client packet");

    printf("\n=== QUIC PROTECT TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail != 0;
}
