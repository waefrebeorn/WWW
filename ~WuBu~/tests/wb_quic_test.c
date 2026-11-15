/*
 * wb_quic_test.c -- LP-93 stone 1 gate: QUIC's wire primitives.
 *
 * Every value here is from RFC 9000 itself, not from this code:
 *   - the varint examples of appendix A.1 and its section 16 text;
 *   - the packet number examples of appendix A.2/A.3, including the worked
 *     "0x9b32 after 0xa82f30ea is 0xa82f9b32".
 */
#include "wb_quic.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)
#define CHECK_EQ(name, got, want) do { \
    unsigned long long _g = (unsigned long long)(got), _w = (unsigned long long)(want); \
    if (_g == _w) g_pass++; \
    else { g_fail++; printf("FAIL: %s: got 0x%llx want 0x%llx (line %d)\n", name, _g, _w, __LINE__); } \
} while (0)
#define CHECK_MEM(name, got, want, n) do { \
    if (memcmp((got), (want), (n)) == 0) g_pass++; \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

static void dec(const char *name, const uint8_t *b, size_t n, uint64_t want) {
    uint64_t v = 0;
    size_t used = 0;
    if (wb_quic_varint_decode(b, n, &v, &used) == 0 && v == want && used == n) {
        g_pass++;
    } else {
        g_fail++;
        printf("FAIL: %s: got %llu want %llu (line %d)\n", name,
               (unsigned long long)v, (unsigned long long)want, __LINE__);
    }
}

static void enc(const char *name, uint64_t v, const uint8_t *want, size_t n) {
    uint8_t got[8];
    size_t k = wb_quic_varint_encode(v, got);
    if (k == n && memcmp(got, want, n) == 0) {
        g_pass++;
    } else {
        g_fail++;
        printf("FAIL: %s: got %zu bytes, want %zu (line %d)\n", name, k, n, __LINE__);
    }
}

int main(void) {
    /* ---- the RFC's own varint examples (appendix A.1) ---- */
    dec("varint 0xc2197c5eff14e88c",
        (const uint8_t[]){0xc2,0x19,0x7c,0x5e,0xff,0x14,0xe8,0x8c}, 8,
        151288809941952652ull);
    dec("varint 0x9d7f3e7d",
        (const uint8_t[]){0x9d,0x7f,0x3e,0x7d}, 4, 494878333ull);
    dec("varint 0x7bbd", (const uint8_t[]){0x7b,0xbd}, 2, 15293ull);
    dec("varint 0x25", (const uint8_t[]){0x25}, 1, 37ull);
    dec("varint 0x4025", (const uint8_t[]){0x40,0x25}, 2, 37ull);

    /* and the encoder produces the same bytes */
    enc("encode 151288809941952652", 151288809941952652ull,
        (const uint8_t[]){0xc2,0x19,0x7c,0x5e,0xff,0x14,0xe8,0x8c}, 8);
    enc("encode 494878333", 494878333ull,
        (const uint8_t[]){0x9d,0x7f,0x3e,0x7d}, 4);
    enc("encode 15293", 15293ull, (const uint8_t[]){0x7b,0xbd}, 2);
    enc("encode 37", 37ull, (const uint8_t[]){0x25}, 1);

    /* the range table, at each boundary */
    enc("encode 0", 0ull, (const uint8_t[]){0x00}, 1);
    enc("encode 63", 63ull, (const uint8_t[]){0x3f}, 1);
    enc("encode 64", 64ull, (const uint8_t[]){0x40,0x40}, 2);
    enc("encode 16383", 16383ull, (const uint8_t[]){0x7f,0xff}, 2);
    enc("encode 16384", 16384ull, (const uint8_t[]){0x80,0x00,0x40,0x00}, 4);
    enc("encode 1073741823", 1073741823ull,
        (const uint8_t[]){0xbf,0xff,0xff,0xff}, 4);
    enc("encode 2^62-1", 4611686018427387903ull,
        (const uint8_t[]){0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff}, 8);

    /* a truncated buffer is refused, not short-read */
    {
        uint64_t v = 0;
        size_t used = 0;
        CHECK(wb_quic_varint_decode((const uint8_t[]){0xc2,0x19}, 2, &v, &used) == -1,
              "a truncated varint is refused");
        CHECK(wb_quic_varint_decode(NULL, 1, &v, &used) == -1,
              "a null buffer is refused");
        CHECK(wb_quic_varint_decode((const uint8_t[]){0x40}, 1, &v, &used) == -1,
              "a two-byte varint in one byte is refused");
    }

    /* ---- packet numbers: the RFC's own worked example ---- */
    CHECK_EQ("0x9b32 after 0xa82f30ea",
             wb_quic_pn_decode(0xa82f30eaull, 0x9b32ull, 16),
             0xa82f9b32ull);

    /* appendix A.2: 29,519 outstanding needs 16 bits */
    {
        uint8_t out[4];
        size_t n = wb_quic_pn_encode(0xac5c02ull, 1, 0xabe8b3ull, out);
        CHECK_EQ("0xac5c02 with 29519 unacked", n, 2);
        /* and the receiver recovers it from the truncated value */
        uint64_t raw = ((uint64_t)out[0] << 8) | out[1];
        CHECK_EQ("round trip at 16 bits",
                 wb_quic_pn_decode(0xabe8b3ull, raw, 16), 0xac5c02ull);
    }
    /* 131,222 outstanding needs 24 bits */
    {
        uint8_t out[4];
        size_t n = wb_quic_pn_encode(0xace8feull, 1, 0xabe8b3ull, out);
        CHECK_EQ("0xace8fe with 131222 unacked", n, 3);
        uint64_t raw = ((uint64_t)out[0] << 16) | ((uint64_t)out[1] << 8) | out[2];
        CHECK_EQ("round trip at 24 bits",
                 wb_quic_pn_decode(0xabe8b3ull, raw, 24), 0xace8feull);
    }

    /* with nothing acknowledged, the packet number itself sets the size */
    {
        uint8_t out[4];
        /* the boundary is 128 unacknowledged packets: 2^7 covers twice that */
        CHECK_EQ("packet 0 with nothing acked", wb_quic_pn_encode(0, 0, 0, out), 1);
        CHECK_EQ("packet 127", wb_quic_pn_encode(127, 0, 0, out), 1);
        CHECK_EQ("packet 128", wb_quic_pn_encode(128, 0, 0, out), 2);
        CHECK_EQ("packet 255", wb_quic_pn_encode(255, 0, 0, out), 2);
        CHECK_EQ("packet 16383", wb_quic_pn_encode(16383, 0, 0, out), 2);
        CHECK_EQ("packet 16384", wb_quic_pn_encode(16384, 0, 0, out), 2);
        CHECK_EQ("packet 32767", wb_quic_pn_encode(32767, 0, 0, out), 2);
        CHECK_EQ("packet 32768", wb_quic_pn_encode(32768, 0, 0, out), 3);
        CHECK_EQ("packet 65535", wb_quic_pn_encode(65535, 0, 0, out), 3);
    }

    /* the decoder handles a wrap of the truncated value */
    {
        /* largest 0x0000ffff, a 16-bit field holding 0x0001: the next packet
         * after the wrap is 0x00010001 */
        CHECK_EQ("across a 16-bit wrap",
                 wb_quic_pn_decode(0x0000ffffull, 0x0001ull, 16),
                 0x00010001ull);
        /* and one just below the largest is not pushed backwards */
        CHECK_EQ("just below the largest",
                 wb_quic_pn_decode(0x0000ffffull, 0xfffeull, 16),
                 0x0000fffeull);
    }

    /* ---- the long header ---- */
    {
        uint8_t out[64];
        uint8_t dcid[8] = {1,2,3,4,5,6,7,8};
        uint8_t scid[4] = {9,10,11,12};
        size_t n = wb_quic_build_long_header(out, sizeof out, WB_QUIC_INITIAL,
                                             WB_QUIC_VERSION_1, dcid, 8, scid, 4, 2);
        CHECK_EQ("a long header with 8 and 4 byte CIDs", n, 1 + 4 + 1 + 8 + 1 + 4);
        CHECK((out[0] & 0x80u) != 0, "the header form bit is set");
        CHECK((out[0] & 0x40u) != 0, "the fixed bit is set");
        CHECK_EQ("the type is Initial", (out[0] >> 4) & 3, WB_QUIC_INITIAL);
        CHECK_EQ("the packet number length is 2", (out[0] & 3) + 1, 2);
        CHECK_EQ("the version is version 1",
                 ((uint32_t)out[1] << 24) | ((uint32_t)out[2] << 16) |
                 ((uint32_t)out[3] << 8) | out[4],
                 WB_QUIC_VERSION_1);

        wb_quic_hdr h;
        CHECK(wb_quic_parse_long_header(out, n, &h) == 0, "it parses back");
        CHECK_EQ("the type survives", h.type, WB_QUIC_INITIAL);
        CHECK_EQ("the version survives", h.version, WB_QUIC_VERSION_1);
        CHECK_EQ("the dcid length survives", h.dcid_len, 8);
        CHECK_MEM("the dcid survives", h.dcid, dcid, 8);
        CHECK_EQ("the scid length survives", h.scid_len, 4);
        CHECK_MEM("the scid survives", h.scid, scid, 4);
        CHECK_EQ("and the header length", h.header_len, n);

        /* every long packet type round-trips */
        CHECK(wb_quic_build_long_header(out, sizeof out, WB_QUIC_ZERO_RTT,
                                        WB_QUIC_VERSION_1, dcid, 8, scid, 4, 1) > 0,
              "a 0-RTT header builds");
        wb_quic_parse_long_header(out, 1 + 4 + 1 + 8 + 1 + 4, &h);
        CHECK_EQ("and its type survives", h.type, WB_QUIC_ZERO_RTT);
        CHECK(wb_quic_build_long_header(out, sizeof out, WB_QUIC_HANDSHAKE,
                                        WB_QUIC_VERSION_1, dcid, 8, scid, 4, 4) > 0,
              "a Handshake header builds");
        wb_quic_parse_long_header(out, 1 + 4 + 1 + 8 + 1 + 4, &h);
        CHECK_EQ("and its type survives", h.type, WB_QUIC_HANDSHAKE);
        CHECK(wb_quic_build_long_header(out, sizeof out, WB_QUIC_RETRY,
                                        WB_QUIC_VERSION_1, dcid, 0, scid, 0, 1) > 0,
              "a Retry header builds");
        wb_quic_parse_long_header(out, 1 + 4 + 1 + 0 + 1 + 0, &h);
        CHECK_EQ("and its type survives", h.type, WB_QUIC_RETRY);
    }

    /* ---- malformed headers ---- */
    {
        uint8_t out[64];
        uint8_t cid[4] = {1,2,3,4};
        wb_quic_hdr h;
        wb_quic_build_long_header(out, sizeof out, WB_QUIC_INITIAL,
                                  WB_QUIC_VERSION_1, cid, 4, cid, 4, 1);
        CHECK(wb_quic_parse_long_header(out, 5, &h) == -1,
              "a header without the CIDs is refused");
        out[0] &= 0xBFu;                       /* clear the fixed bit */
        CHECK(wb_quic_parse_long_header(out, 12, &h) == -1,
              "a zero fixed bit is refused");
        wb_quic_build_long_header(out, sizeof out, WB_QUIC_INITIAL,
                                  WB_QUIC_VERSION_1, cid, 4, cid, 4, 1);
        out[5] = 21;                           /* a CID longer than 20 */
        CHECK(wb_quic_parse_long_header(out, 12, &h) == -1,
              "a CID longer than 20 bytes is refused");
        CHECK(wb_quic_build_long_header(out, sizeof out, WB_QUIC_INITIAL,
                                        WB_QUIC_VERSION_1, cid, 21, cid, 4, 1) == 0,
              "and such a header is not built");
        CHECK(wb_quic_build_long_header(out, sizeof out, WB_QUIC_INITIAL,
                                        WB_QUIC_VERSION_1, cid, 4, cid, 4, 5) == 0,
              "a packet number length over 4 is refused");
        /* a short header is not this parser's business */
        uint8_t shorth = 0x30;
        CHECK(wb_quic_parse_long_header(&shorth, 1, &h) == -1,
              "a short header is refused here");
    }

    /* ---- the Initial packet ---- */
    {
        uint8_t out[256];
        uint8_t dcid[8] = {1,2,3,4,5,6,7,8};
        uint8_t scid[4] = {9,10,11,12};
        uint8_t token[12] = {0xaa,0xbb,0xcc,0xdd,0xee,0xff,0,1,2,3,4,5};
        uint8_t payload[] = "the TLS ClientHello lives in here";
        wb_quic_initial in;
        memset(&in, 0, sizeof in);
        in.token = token;
        in.token_len = sizeof token;
        in.payload = payload;
        in.payload_len = sizeof payload;
        in.packet_number = 0x1234;

        size_t n = wb_quic_build_initial(out, sizeof out, dcid, 8, scid, 4, &in);
        CHECK(n > 0, "an Initial packet builds");

        wb_quic_hdr h;
        wb_quic_initial got;
        memset(&got, 0, sizeof got);
        CHECK(wb_quic_parse_initial(out, n, WB_QUIC_VERSION_1, &h, &got) == 0,
              "and parses back");
        CHECK_EQ("the type is Initial", h.type, WB_QUIC_INITIAL);
        CHECK_EQ("the token length survives", got.token_len, sizeof token);
        CHECK_MEM("the token survives", got.token, token, sizeof token);
        CHECK_EQ("the payload length survives", got.payload_len, sizeof payload);
        CHECK_MEM("the payload survives", got.payload, payload, sizeof payload);
        CHECK_EQ("the packet number is recoverable",
                 wb_quic_pn_decode(0x1234ull - 1, got.packet_number,
                                   8 * ((out[0] & 3) + 1)),
                 0x1234ull);

        /* no token, no payload */
        wb_quic_initial bare;
        memset(&bare, 0, sizeof bare);
        bare.packet_number = 3;
        size_t n2 = wb_quic_build_initial(out, sizeof out, dcid, 8, scid, 4, &bare);
        CHECK(n2 > 0, "a bare Initial builds");
        CHECK(wb_quic_parse_initial(out, n2, WB_QUIC_VERSION_1, &h, &got) == 0,
              "and parses");
        CHECK_EQ("with no token", got.token_len, 0);
        CHECK_EQ("and no payload", got.payload_len, 0);

        /* the length field really covers the packet number and payload */
        CHECK_EQ("an Initial is header + token fields + body", n2,
                 1 + 4 + 1 + 8 + 1 + 4 + 1 + 1 + 1);
    }

    /* ---- a packet that does not fit is refused ---- */
    {
        uint8_t tiny[8];
        uint8_t cid[4] = {1,2,3,4};
        wb_quic_initial in;
        memset(&in, 0, sizeof in);
        in.payload = (const uint8_t *)"x";
        in.payload_len = 1;
        CHECK(wb_quic_build_initial(tiny, sizeof tiny, cid, 4, cid, 4, &in) == 0,
              "an Initial that does not fit is refused");
        CHECK(wb_quic_build_long_header(tiny, sizeof tiny, WB_QUIC_INITIAL,
                                        WB_QUIC_VERSION_1, cid, 4, cid, 4, 1) == 0,
              "a header that does not fit is refused");
    }

    /* ---- a truncated Initial is refused ---- */
    {
        uint8_t out[256];
        uint8_t cid[4] = {1,2,3,4};
        wb_quic_initial in;
        memset(&in, 0, sizeof in);
        in.payload = (const uint8_t *)"payload";
        in.payload_len = 7;
        in.packet_number = 1;
        size_t n = wb_quic_build_initial(out, sizeof out, cid, 4, cid, 4, &in);
        wb_quic_hdr h;
        wb_quic_initial got;
        CHECK(wb_quic_parse_initial(out, n - 1, WB_QUIC_VERSION_1, &h, &got) == -1,
              "a truncated Initial is refused");
        out[n - 1] ^= 0xff;                     /* corrupt the last byte */
        /* the length still matches, so this parses -- corruption is the AEAD's
         * job, not the header parser's */
        CHECK(wb_quic_parse_initial(out, n, WB_QUIC_VERSION_1, &h, &got) == 0,
              "a corrupted payload byte still parses; integrity is the AEAD's job");
    }

    printf("\n=== QUIC TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail != 0;
}
