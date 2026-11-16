/*
 * wb_quic_packet_test.c -- LP-93 stone 7 gate.
 *
 * This is the first place a frame becomes a packet. The reference is RFC 9001
 * appendix A.2's client Initial, byte for byte: the same connection ID, the
 * same 1162-byte payload, packet number 2 in four bytes, no token.
 */
#include "wb_quic_packet.h"
#include "wb_hkdf.h"

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

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static size_t unhex(const char *s, uint8_t *out, size_t cap) {
    size_t n = 0;
    while (s[0] && s[1]) { if (n >= cap) break;
        out[n++] = (uint8_t)((hexval(s[0]) << 4) | hexval(s[1])); s += 2; }
    return n;
}

static const char *PAYLOAD_HEX = "060040f1010000ed0303ebf8fa56f12939b9584a3896472ec40bb863cfd3e86804fe3a47f06a2b69484c00000413011302010000c000000010000e00000b6578616d706c652e636f6dff01000100000a00080006001d0017001800100007000504616c706e000500050100000000003300260024001d00209370b2c9caa47fbabaf4559fedba753de171fa71f50f1ce15d43e994ec74d748002b0003020304000d0010000e0403050306030203080408050806002d00020101001c00024001003900320408ffffffffffffffff05048000ffff07048000ffff0801100104800075300901100f088394c8f03e51570806048000ffff0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";
static const char *PROT_HEX = "c000000001088394c8f03e5157080000449e7b9aec34d1b1c98dd7689fb8ec11d242b123dc9bd8bab936b47d92ec356c0bab7df5976d27cd449f63300099f3991c260ec4c60d17b31f8429157bb35a1282a643a8d2262cad67500cadb8e7378c8eb7539ec4d4905fed1bee1fc8aafba17c750e2c7ace01e6005f80fcb7df621230c83711b39343fa028cea7f7fb5ff89eac2308249a02252155e2347b63d58c5457afd84d05dfffdb20392844ae812154682e9cf012f9021a6f0be17ddd0c2084dce25ff9b06cde535d0f920a2db1bf362c23e596d11a4f5a6cf3948838a3aec4e15daf8500a6ef69ec4e3feb6b1d98e610ac8b7ec3faf6ad760b7bad1db4ba3485e8a94dc250ae3fdb41ed15fb6a8e5eba0fc3dd60bc8e30c5c4287e53805db059ae0648db2f64264ed5e39be2e20d82df566da8dd5998ccabdae053060ae6c7b4378e846d29f37ed7b4ea9ec5d82e7961b7f25a9323851f681d582363aa5f89937f5a67258bf63ad6f1a0b1d96dbd4faddfcefc5266ba6611722395c906556be52afe3f565636ad1b17d508b73d8743eeb524be22b3dcbc2c7468d54119c7468449a13d8e3b95811a198f3491de3e7fe942b330407abf82a4ed7c1b311663ac69890f4157015853d91e923037c227a33cdd5ec281ca3f79c44546b9d90ca00f064c99e3dd97911d39fe9c5d0b23a229a234cb36186c4819e8b9c5927726632291d6a418211cc2962e20fe47feb3edf330f2c603a9d48c0fcb5699dbfe5896425c5bac4aee82e57a85aaf4e2513e4f05796b07ba2ee47d80506f8d2c25e50fd14de71e6c418559302f939b0e1abd576f279c4b2e0feb85c1f28ff18f58891ffef132eef2fa09346aee33c28eb130ff28f5b766953334113211996d20011a198e3fc433f9f2541010ae17c1bf202580f6047472fb36857fe843b19f5984009ddc324044e847a4f4a0ab34f719595de37252d6235365e9b84392b061085349d73203a4a13e96f5432ec0fd4a1ee65accdd5e3904df54c1da510b0ff20dcc0c77fcb2c0e0eb605cb0504db87632cf3d8b4dae6e705769d1de354270123cb11450efc60ac47683d7b8d0f811365565fd98c4c8eb936bcab8d069fc33bd801b03adea2e1fbc5aa463d08ca19896d2bf59a071b851e6c239052172f296bfb5e72404790a2181014f3b94a4e97d117b438130368cc39dbb2d198065ae3986547926cd2162f40a29f0c3c8745c0f50fba3852e566d44575c29d39a03f0cda721984b6f440591f355e12d439ff150aab7613499dbd49adabc8676eef023b15b65bfc5ca06948109f23f350db82123535eb8a7433bdabcb909271a6ecbcb58b936a88cd4e8f2e6ff5800175f113253d8fa9ca8885c2f552e657dc603f252e1a8e308f76f0be79e2fb8f5d5fbbe2e30ecadd220723c8c0aea8078cdfcb3868263ff8f0940054da48781893a7e49ad5aff4af300cd804a6b6279ab3ff3afb64491c85194aab760d58a606654f9f4400e8b38591356fbf6425aca26dc85244259ff2b19c41b9f96f3ca9ec1dde434da7d2d392b905ddf3d1f9af93d1af5950bd493f5aa731b4056df31bd267b6b90a079831aaf579be0a39013137aac6d404f518cfd46840647e78bfe706ca4cf5e9c5453e9f7cfd2b8b4c8d169a44e55c88d4a9a7f9474241e221af44860018ab0856972e194cd934";
static const char *HDR_HEX = "c300000001088394c8f03e5157080000449e00000002";
static const uint8_t DCID[8] = {0x83,0x94,0xc8,0xf0,0x3e,0x51,0x57,0x08};
static uint8_t scratch[2048];   /* the opener tries each packet number length */
/* RFC 9001 section 5.4.2: the encoded packet number plus the protected
 * payload must be at least four bytes longer than the 16-byte sample. */
static void pad_to(uint8_t *f, size_t *fl, int pn_len) {
    while ((size_t)pn_len + *fl < 20) {
        size_t add = wb_quic_frame_padding(f + *fl, 2048 - *fl, 1);
        if (!add) break;
        *fl += add;
    }
}

int main(void) {
    uint8_t payload[2048], want_prot[2048], want_hdr[64];
    size_t plen = unhex(PAYLOAD_HEX, payload, sizeof payload);
    size_t wlen = unhex(PROT_HEX, want_prot, sizeof want_prot);
    size_t hlen = unhex(HDR_HEX, want_hdr, sizeof want_hdr);
    CHECK_EQ("A.2's unprotected header is 22 bytes", hlen, 22);

    wb_quic_keys client, server;
    wb_quic_initial_keys(DCID, sizeof DCID, &client, &server);

    /* ---- A.2, byte for byte ---- */
    uint8_t pkt[2048];
    size_t n = 0;
    CHECK(wb_quic_packet_build(pkt, &n, sizeof pkt, &client, WB_QUIC_INITIAL,
                               DCID, sizeof DCID, NULL, 0, 2, 4,
                               payload, plen, NULL, 0) == 0,
          "the client Initial builds");
    CHECK_EQ("to 1200 bytes", n, 1200);
    CHECK_MEM("and is exactly RFC 9001 A.2's packet", pkt, want_prot, 1200);

    /* ---- and it opens ---- */
    {
        uint8_t copy[2048];
        memcpy(copy, want_prot, wlen);
        size_t cl = wlen;
        wb_quic_hdr h;
        uint64_t pn = 0;
        size_t off = 0;
        CHECK(wb_quic_packet_open(copy, &cl, scratch, sizeof scratch, &client, WB_QUIC_VERSION_1, &h,
                                  &pn, &off) == 0, "it opens");
        CHECK_EQ("as a long packet", h.form, 1);
        CHECK_EQ("of type Initial", h.type, WB_QUIC_INITIAL);
        CHECK_EQ("version 1", h.version, WB_QUIC_VERSION_1);
        CHECK_EQ("with the DCID", h.dcid_len, 8);
        CHECK_MEM("and its bytes", h.dcid, DCID, 8);
        CHECK_EQ("no SCID", h.scid_len, 0);
        CHECK_EQ("packet number 2", pn, 2);
        CHECK_EQ("the payload starts after the 22-byte header", off, 22);
        CHECK_EQ("and is 1162 bytes", cl - off, 1162);
        CHECK_MEM("and is the RFC's payload", copy + off, payload, 1162);
        /* the restored header is the RFC's unprotected header */
        CHECK_MEM("the unprotected header is A.2's", copy, want_hdr, 22);
    }

    /* ---- the packet number length is the sender's choice ---- */
    {
        for (int pnl = 1; pnl <= 4; pnl++) {
            uint8_t p[2048];
            size_t pn_ = 0;
            uint64_t num = (pnl == 4) ? 0x01020304u : ((UINT64_C(1) << (8 * pnl)) - 1);
            if (wb_quic_packet_build(p, &pn_, sizeof p, &client, WB_QUIC_INITIAL,
                                     DCID, sizeof DCID, NULL, 0, num, pnl,
                                     payload, plen, NULL, 0) != 0) {
                printf("FAIL: pn_len %d did not build\n", pnl);
                g_fail++;
                continue;
            }
            uint8_t cp[2048];
            memcpy(cp, p, pn_);
            size_t cl = pn_;
            wb_quic_hdr h;
            uint64_t got = 0;
            size_t off = 0;
            CHECK(wb_quic_packet_open(cp, &cl, scratch, sizeof scratch, &client, WB_QUIC_VERSION_1, &h,
                                      &got, &off) == 0, "and it opens");
            CHECK_EQ("with the number back", got, num);
            /* the low two bits are themselves protected, so they only read
             * correctly once the packet is open */
            CHECK_EQ("and the low two bits carry the length", (cp[0] & 3) + 1, pnl);
        }
    }

    /* ---- a number too large for its encoding is refused ---- */
    {
        uint8_t p[2048];
        size_t pn_ = 0;
        CHECK(wb_quic_packet_build(p, &pn_, sizeof p, &client, WB_QUIC_INITIAL,
                                   DCID, sizeof DCID, NULL, 0, 256, 1,
                                   payload, plen, NULL, 0) == -1,
              "256 does not fit one byte");
        CHECK(wb_quic_packet_build(p, &pn_, sizeof p, &client, WB_QUIC_INITIAL,
                                   DCID, sizeof DCID, NULL, 0, 65536, 2,
                                   payload, plen, NULL, 0) == -1,
              "nor 65536 in two");
        CHECK(wb_quic_packet_build(p, &pn_, sizeof p, &client, WB_QUIC_INITIAL,
                                   DCID, sizeof DCID, NULL, 0, 255, 1,
                                   payload, plen, NULL, 0) == 0,
              "but 255 does");
    }

    /* ---- a Handshake packet ---- */
    {
        uint8_t p[1024];
        size_t pn_ = 0;
        uint8_t ping[64];
        size_t pl = wb_quic_frame_ping(ping, sizeof ping);
        pad_to(ping, &pl, 2);
        CHECK(wb_quic_packet_build(p, &pn_, sizeof p, &server, WB_QUIC_HANDSHAKE,
                                   DCID, sizeof DCID, DCID, sizeof DCID, 7, 2,
                                   ping, pl, NULL, 0) == 0,
              "a Handshake packet builds");
        CHECK_EQ("with the Handshake type", (p[0] >> 4) & 3, WB_QUIC_HANDSHAKE);
        uint8_t cp[1024];
        memcpy(cp, p, pn_);
        size_t cl = pn_;
        wb_quic_hdr h;
        uint64_t got = 0;
        size_t off = 0;
        CHECK(wb_quic_packet_open(cp, &cl, scratch, sizeof scratch, &server, WB_QUIC_VERSION_1, &h, &got,
                                  &off) == 0, "and opens");
        CHECK_EQ("with packet number 7", got, 7);
        CHECK_EQ("and both connection IDs", h.scid_len, 8);
        CHECK_MEM("the SCID", h.scid, DCID, 8);
        CHECK_EQ("and the PING frame is there", cp[off], WB_QUIC_FRAME_PING);
    }

    /* ---- a token on an Initial ---- */
    {
        uint8_t p[1024];
        size_t pn_ = 0;
        const uint8_t tok[5] = {9, 8, 7, 6, 5};
        uint8_t ping[64];
        size_t pl = wb_quic_frame_ping(ping, sizeof ping);
        pad_to(ping, &pl, 1);
        CHECK(wb_quic_packet_build(p, &pn_, sizeof p, &client, WB_QUIC_INITIAL,
                                   DCID, sizeof DCID, NULL, 0, 1, 1,
                                   ping, pl, tok, sizeof tok) == 0,
              "an Initial with a token builds");
        uint8_t cp[1024];
        memcpy(cp, p, pn_);
        size_t cl = pn_;
        wb_quic_hdr h;
        uint64_t got = 0;
        size_t off = 0;
        CHECK(wb_quic_packet_open(cp, &cl, scratch, sizeof scratch, &client, WB_QUIC_VERSION_1, &h, &got,
                                  &off) == 0, "and opens");
        CHECK_EQ("with the number", got, 1);
        /* the token sits between the SCID and the Length field */
        uint64_t tlen = 0;
        size_t used = 0;
        CHECK(wb_quic_varint_decode(cp + h.header_len, cl - h.header_len,
                                    &tlen, &used) == 0, "the token length reads");
        CHECK_EQ("as five", tlen, 5);
        CHECK_MEM("and the token follows", cp + h.header_len + used, tok, 5);
    }

    /* ---- a wrong key does not open it ---- */
    {
        uint8_t cp[2048];
        memcpy(cp, want_prot, wlen);
        size_t cl = wlen;
        wb_quic_hdr h;
        uint64_t got = 0;
        size_t off = 0;
        CHECK(wb_quic_packet_open(cp, &cl, scratch, sizeof scratch, &server, WB_QUIC_VERSION_1, &h, &got,
                                  &off) == -1, "the server key does not open a client packet");
        CHECK_EQ("and the packet is untouched", cl, wlen);
        CHECK_MEM("and still the same bytes", cp, want_prot, wlen);
    }

    /* ---- a truncated packet does not open ---- */
    {
        uint8_t cp[2048];
        memcpy(cp, want_prot, wlen);
        size_t cl = 40;
        wb_quic_hdr h;
        uint64_t got = 0;
        size_t off = 0;
        CHECK(wb_quic_packet_open(cp, &cl, scratch, sizeof scratch, &client, WB_QUIC_VERSION_1, &h, &got,
                                  &off) == -1, "a truncated packet is refused");
    }

    /* ---- the wrong version does not open ---- */
    {
        uint8_t cp[2048];
        memcpy(cp, want_prot, wlen);
        size_t cl = wlen;
        wb_quic_hdr h;
        uint64_t got = 0;
        size_t off = 0;
        CHECK(wb_quic_packet_open(cp, &cl, scratch, sizeof scratch, &client, 0x00000002u, &h, &got,
                                  &off) == -1, "another version is refused");
    }

    /* ---- a buffer that is too small ---- */
    {
        uint8_t p[64];
        size_t pn_ = 0;
        CHECK(wb_quic_packet_build(p, &pn_, sizeof p, &client, WB_QUIC_INITIAL,
                                   DCID, sizeof DCID, NULL, 0, 2, 4,
                                   payload, plen, NULL, 0) == -1,
              "a small buffer is refused");
        CHECK(wb_quic_packet_build(p, &pn_, 40, &client, WB_QUIC_INITIAL,
                                   DCID, sizeof DCID, NULL, 0, 2, 1,
                                   payload, plen, NULL, 0) == -1,
              "as is one too small to hold the sample");
        CHECK(wb_quic_packet_build(p, &pn_, sizeof p, &client, WB_QUIC_INITIAL,
                                   DCID, sizeof DCID, NULL, 0, 2, 0,
                                   payload, plen, NULL, 0) == -1,
              "as is a bad packet number length");
        CHECK(wb_quic_packet_build(p, &pn_, sizeof p, &client, 7,
                                   DCID, sizeof DCID, NULL, 0, 2, 1,
                                   payload, plen, NULL, 0) == -1,
              "as is a bad type");
    }

    /* ---- a real frame, end to end ---- */
    {
        uint8_t frames[64];
        size_t fl = 0;
        fl += wb_quic_frame_ping(frames + fl, sizeof frames - fl);
        fl += wb_quic_frame_max_data(frames + fl, sizeof frames - fl, 9000);
        pad_to(frames, &fl, 1);
        uint8_t p[512];
        size_t pn_ = 0;
        CHECK(wb_quic_packet_build(p, &pn_, sizeof p, &client, WB_QUIC_INITIAL,
                                   DCID, sizeof DCID, NULL, 0, 0, 1,
                                   frames, fl, NULL, 0) == 0, "it builds");
        uint8_t cp[512];
        memcpy(cp, p, pn_);
        size_t cl = pn_;
        wb_quic_hdr h;
        uint64_t got = 0;
        size_t off = 0;
        CHECK(wb_quic_packet_open(cp, &cl, scratch, sizeof scratch, &client, WB_QUIC_VERSION_1, &h, &got,
                                  &off) == 0, "and opens");
        size_t o = off;
        wb_quic_frame f;
        CHECK(wb_quic_frame_next(cp, cl, &o, &f) == 1, "the PING is first");
        CHECK_EQ("as a PING", f.type, WB_QUIC_FRAME_PING);
        CHECK(wb_quic_frame_next(cp, cl, &o, &f) == 1, "then the MAX_DATA");
        CHECK_EQ("as a MAX_DATA", f.type, WB_QUIC_FRAME_MAX_DATA);
        /* RFC 9001 section 5.4.2 requires enough padding for the sample, and
         * the rest of the payload is PADDING frames */
        int only_padding = 1, saw_any = 0;
        while (wb_quic_frame_next(cp, cl, &o, &f) == 1) {
            saw_any = 1;
            if (f.type != WB_QUIC_FRAME_PADDING) only_padding = 0;
        }
        CHECK(saw_any, "and the padding follows");
        CHECK(only_padding, "and it is nothing but PADDING");
    }

    printf("\n=== QUIC PACKET TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail != 0;
}
