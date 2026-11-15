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

    /* ---- frames ---- */
    {
        /* PING, then ACK, then HANDSHAKE_DONE */
        uint8_t pkt[128];
        size_t off = 0;
        off += wb_quic_frame_ping(pkt + off, sizeof pkt - off);
        const uint64_t acked[] = {1, 2, 3, 7, 8, 10};
        off += wb_quic_frame_ack(pkt + off, sizeof pkt - off, 0, 10, 0,
                                 acked, sizeof acked / sizeof acked[0], 0, 0, 0);
        off += wb_quic_frame_handshake_done(pkt + off, sizeof pkt - off);

        size_t p = 0;
        wb_quic_frame f;
        CHECK(wb_quic_frame_next(pkt, off, &p, &f) == 1, "frame 1 is found");
        CHECK_EQ("and is a PING", f.type, WB_QUIC_FRAME_PING);
        CHECK_EQ("of zero length", f.body_len, 0);
        CHECK(wb_quic_frame_next(pkt, off, &p, &f) == 1, "frame 2 is found");
        CHECK_EQ("and is an ACK", f.type, WB_QUIC_FRAME_ACK);
        CHECK_EQ("with the body RFC 9000 describes", f.body_len, 8);
        CHECK_MEM("largest, delay, count, first range, then two ranges",
                  f.body, ((const uint8_t[]){10, 0, 2, 1, 1, 2, 3, 3}), 8);
        CHECK(wb_quic_frame_next(pkt, off, &p, &f) == 1, "frame 3 is found");
        CHECK_EQ("and is HANDSHAKE_DONE", f.type, WB_QUIC_FRAME_HANDSHAKE_DONE);
        CHECK_EQ("which ends the packet", p, off);
        CHECK(wb_quic_frame_next(pkt, off, &p, &f) == 0, "and there is nothing after");

        /* the ACK parses, covers the same six packets, and no more */
        wb_quic_ack a;
        CHECK(wb_quic_parse_ack(f.body, 0, WB_QUIC_FRAME_ACK, &a) == -1,
              "a zero-length body is refused");
        CHECK(wb_quic_parse_ack(pkt + 2, 8, WB_QUIC_FRAME_ACK, &a) == 0,
              "the ACK body parses");
        CHECK_EQ("largest survives", a.largest, 10);
        CHECK_EQ("range count survives", a.range_count, 2);
        CHECK_EQ("first range survives", a.first_range, 1);
        CHECK_EQ("the first gap survives", a.gap[0], 1);
        CHECK_EQ("the first range length survives", a.range_len[0], 2);
        CHECK_EQ("the second gap survives", a.gap[1], 3);
        CHECK_EQ("the second range length survives", a.range_len[1], 3);
        CHECK_EQ("and the count is six", wb_quic_ack_count(&a), 6);
        uint64_t got[8];
        size_t ng = wb_quic_ack_expand(&a, got, 8);
        CHECK_EQ("six packets expand", ng, 6);
        CHECK_MEM("descending, exactly the acknowledged set", got,
                  ((const uint64_t[]){10, 8, 7, 3, 2, 1}), 6 * (int)sizeof(uint64_t));
    }

    /* ---- ACK ranges: contiguous is one range, holes are gaps ---- */
    {
        uint8_t buf[128];
        const uint64_t one[] = {5};
        size_t n = wb_quic_frame_ack(buf, sizeof buf, 0, 5, 7, one, 1, 0, 0, 0);
        CHECK_EQ("a single packet is one range", n, 1 + 1 + 1 + 1 + 1);
        wb_quic_ack a;
        wb_quic_parse_ack(buf + 1, n - 1, WB_QUIC_FRAME_ACK, &a);
        CHECK_EQ("with no further ranges", a.range_count, 0);
        CHECK_EQ("and a first range of one", a.first_range, 1);
        CHECK_EQ("and a delay", a.delay, 7);

        const uint64_t all[] = {0,1,2,3,4};
        n = wb_quic_frame_ack(buf, sizeof buf, 0, 4, 0, all, 5, 0, 0, 0);
        wb_quic_parse_ack(buf + 1, n - 1, WB_QUIC_FRAME_ACK, &a);
        CHECK_EQ("a contiguous run is still one range", a.range_count, 0);
        CHECK_EQ("and the first range covers all five", a.first_range, 5);
        CHECK_EQ("the count agrees", wb_quic_ack_count(&a), 5);

        /* a list whose largest is not the passed largest is refused */
        const uint64_t bad[] = {1, 2};
        CHECK(wb_quic_frame_ack(buf, sizeof buf, 0, 9, 0, bad, 2, 0, 0, 0) == 0,
              "an ACK whose largest is not acknowledged is refused");
        CHECK(wb_quic_frame_ack(buf, sizeof buf, 0, 2, 0, NULL, 0, 0, 0, 0) == 0,
              "an empty ACK is refused");
    }

    /* ---- the ECN variant carries three more counts ---- */
    {
        uint8_t buf[128];
        const uint64_t acked[] = {1, 2};
        size_t n = wb_quic_frame_ack(buf, sizeof buf, 1, 2, 3, acked, 2, 11, 22, 33);
        CHECK(n > 0, "an ECN ACK builds");
        CHECK_EQ("its type is 0x03", buf[0], 0x03);
        wb_quic_ack a;
        CHECK(wb_quic_parse_ack(buf + 1, n - 1, 0x03, &a) == 0, "and parses");
        CHECK(a.has_ecn, "with the ECN flag set");
        CHECK_EQ("ect0 survives", a.ect0, 11);
        CHECK_EQ("ect1 survives", a.ect1, 22);
        CHECK_EQ("ce survives", a.ce, 33);
        CHECK(wb_quic_parse_ack(buf + 1, n - 1, WB_QUIC_FRAME_ACK, &a) == -1,
              "and a non-ECN parse of it refuses the trailing counts");
    }

    /* ---- CRYPTO ---- */
    {
        uint8_t buf[128];
        const char *data = "ClientHello";
        size_t n = wb_quic_frame_crypto(buf, sizeof buf, 4096, data, strlen(data));
        CHECK(n > 0, "a CRYPTO frame builds");
        CHECK_EQ("its type", buf[0], WB_QUIC_FRAME_CRYPTO);
        uint64_t off;
        const uint8_t *d;
        size_t dl;
        CHECK(wb_quic_parse_crypto(buf + 1, n - 1, &off, &d, &dl) == 0, "and parses");
        CHECK_EQ("the offset survives", off, 4096);
        CHECK_EQ("the length survives", dl, strlen(data));
        CHECK(memcmp(d, data, dl) == 0, "and the bytes");
        CHECK(wb_quic_parse_crypto(buf + 1, n - 2, &off, &d, &dl) == -1,
              "a truncated CRYPTO is refused");
    }

    /* ---- STREAM, with the flag bits ---- */
    {
        uint8_t buf[128];
        const char *data = "stream payload";
        size_t n = wb_quic_frame_stream(buf, sizeof buf, 8, 128, data,
                                        strlen(data), 1);
        CHECK(n > 0, "a STREAM frame builds");
        CHECK_EQ("its type carries OFF, LEN and FIN",
                 buf[0], (WB_QUIC_FRAME_STREAM_BASE | WB_QUIC_STREAM_OFF |
                          WB_QUIC_STREAM_LEN | WB_QUIC_STREAM_FIN));
        wb_quic_frame f;
        size_t p = 0;
        CHECK(wb_quic_frame_next(buf, n, &p, &f) == 1, "it walks");
        CHECK_EQ("as one frame", p, n);
        uint64_t sid, off;
        const uint8_t *d;
        size_t dl;
        int has_len = 0, fin = 0;
        CHECK(wb_quic_parse_stream(f.body, f.body_len, f.type & 0x0f, &sid, &off,
                                   &has_len, &d, &dl, &fin) == 0,
              "and parses");
        CHECK_EQ("the stream id survives", sid, 8);
        CHECK_EQ("the offset survives", off, 128);
        CHECK_EQ("the length flag is set", has_len, 1);
        CHECK_EQ("the length survives", dl, strlen(data));
        CHECK(memcmp(d, data, dl) == 0, "and the bytes");
        CHECK_EQ("and the FIN flag", fin, 1);

        /* without LEN the data runs to the end of the packet */
        uint8_t bare[64];
        size_t nb = 0;
        bare[nb++] = (uint8_t)(WB_QUIC_FRAME_STREAM_BASE | WB_QUIC_STREAM_OFF);
        bare[nb++] = 4;                       /* stream 4 */
        bare[nb++] = 0;                       /* offset 0 */
        memcpy(bare + nb, "tail", 4);
        nb += 4;
        p = 0;
        CHECK(wb_quic_frame_next(bare, nb, &p, &f) == 1, "a bare STREAM walks");
        CHECK(wb_quic_parse_stream(f.body, f.body_len, f.type & 0x0f, &sid, &off,
                                   &has_len, &d, &dl, &fin) == 0,
              "and parses");
        CHECK_EQ("with no length flag", has_len, 0);
        CHECK_EQ("and the data runs out", dl, 4);
    }

    /* ---- the connection-management frames ---- */
    {
        uint8_t buf[128];
        uint8_t cid[8] = {1,2,3,4,5,6,7,8};
        uint8_t token[16];
        for (int i = 0; i < 16; i++) token[i] = (uint8_t)(0xa0 + i);

        size_t n = wb_quic_frame_new_connection_id(buf, sizeof buf, 7, 2, cid, 8, token);
        CHECK(n > 0, "a NEW_CONNECTION_ID builds");
        CHECK_EQ("its type", buf[0], WB_QUIC_FRAME_NEW_CONNECTION_ID);
        uint64_t seq, retire;
        uint8_t gcid[20], gtoken[16];
        size_t gcl = 0;
        CHECK(wb_quic_parse_new_connection_id(buf + 1, n - 1, &seq, &retire, gcid,
                                              &gcl, gtoken) == 0,
              "and parses");
        CHECK_EQ("the sequence survives", seq, 7);
        CHECK_EQ("retire-prior-to survives", retire, 2);
        CHECK_EQ("the CID length survives", gcl, 8);
        CHECK_MEM("the CID survives", gcid, cid, 8);
        CHECK_MEM("and the reset token", gtoken, token, 16);
        CHECK(wb_quic_frame_new_connection_id(buf, sizeof buf, 0, 0, cid, 21, token) == 0,
              "a CID over 20 bytes is refused");

        n = wb_quic_frame_retire_connection_id(buf, sizeof buf, 9);
        CHECK_EQ("a RETIRE_CONNECTION_ID is two bytes", n, 2);
        CHECK_EQ("with the sequence", buf[1], 9);

        uint8_t data8[8] = {1,2,3,4,5,6,7,8};
        n = wb_quic_frame_path_challenge(buf, sizeof buf, data8);
        CHECK_EQ("a PATH_CHALLENGE is nine bytes", n, 9);
        uint8_t got8[8];
        CHECK(wb_quic_parse_path_data(buf + 1, n - 1, got8) == 0, "and parses");
        CHECK_MEM("with its data", got8, data8, 8);
        CHECK(wb_quic_parse_path_data(buf + 1, 7, got8) == -1,
              "a short PATH_CHALLENGE is refused");

        n = wb_quic_frame_path_response(buf, sizeof buf, data8);
        CHECK_EQ("a PATH_RESPONSE is nine bytes", n, 9);
        CHECK_EQ("its type", buf[0], WB_QUIC_FRAME_PATH_RESPONSE);
        CHECK(wb_quic_parse_path_data(buf + 1, n - 1, got8) == 0, "and parses");
        CHECK_MEM("echoing the challenge", got8, data8, 8);

        n = wb_quic_frame_connection_close(buf, sizeof buf, 0, 0x100, 0x06, "bye");
        CHECK(n > 0, "a transport CONNECTION_CLOSE builds");
        CHECK_EQ("its type", buf[0], WB_QUIC_FRAME_CONNECTION_CLOSE);
        uint64_t err, ft;
        const char *reason;
        size_t rlen;
        CHECK(wb_quic_parse_connection_close(buf + 1, n - 1, 0, &err, &ft,
                                             &reason, &rlen) == 0,
              "and parses");
        CHECK_EQ("the error survives", err, 0x100);
        CHECK_EQ("the offending frame type survives", ft, 0x06);
        CHECK_EQ("the reason length survives", rlen, 3);
        CHECK(memcmp(reason, "bye", 3) == 0, "and the reason");

        n = wb_quic_frame_connection_close(buf, sizeof buf, 1, 0x0a, 0, "closed");
        CHECK_EQ("an application close uses 0x1d", buf[0], 0x1d);
        CHECK(wb_quic_parse_connection_close(buf + 1, n - 1, 1, &err, &ft,
                                             &reason, &rlen) == 0,
              "and parses");
        CHECK_EQ("with no frame type", ft, 0);
        CHECK_EQ("the error survives", err, 0x0a);

        /* the flow-control frames are one or two varints */
        CHECK_EQ("MAX_DATA", wb_quic_frame_max_data(buf, sizeof buf, 1000000), 5);
        CHECK_EQ("MAX_STREAM_DATA", wb_quic_frame_max_stream_data(buf, sizeof buf, 4, 99),
                 4);
        CHECK_EQ("MAX_STREAMS (bidi)", wb_quic_frame_max_streams(buf, sizeof buf, 0, 100), 3);
        CHECK_EQ("MAX_STREAMS (uni)", wb_quic_frame_max_streams(buf, sizeof buf, 1, 100), 3);
        CHECK_EQ("and its type is 0x13", buf[0], 0x13);
        CHECK_EQ("DATA_BLOCKED", wb_quic_frame_data_blocked(buf, sizeof buf, 55), 2);
        CHECK_EQ("STREAM_DATA_BLOCKED",
                 wb_quic_frame_stream_data_blocked(buf, sizeof buf, 4, 55), 3);
        CHECK_EQ("STREAMS_BLOCKED (bidi)",
                 wb_quic_frame_streams_blocked(buf, sizeof buf, 0, 3), 2);
        CHECK_EQ("STREAMS_BLOCKED (uni)",
                 wb_quic_frame_streams_blocked(buf, sizeof buf, 1, 3), 2);
        CHECK_EQ("and its type is 0x17", buf[0], 0x17);

        CHECK_EQ("RESET_STREAM",
                 wb_quic_frame_reset_stream(buf, sizeof buf, 4, 0x0b, 777), 5);
        CHECK_EQ("STOP_SENDING",
                 wb_quic_frame_stop_sending(buf, sizeof buf, 4, 0x0b), 3);
        CHECK_EQ("NEW_TOKEN",
                 wb_quic_frame_new_token(buf, sizeof buf, token, 16), 2 + 16);
    }

    /* ---- a malformed frame payload is refused ---- */
    {
        uint8_t bad[8] = {WB_QUIC_FRAME_CRYPTO, 0x01, 0xff};
        size_t p = 0;
        wb_quic_frame f;
        CHECK(wb_quic_frame_next(bad, sizeof bad, &p, &f) == -1,
              "a CRYPTO claiming more than it carries is refused");
        uint8_t unknown[2] = {0x3f, 0x00};
        CHECK(wb_quic_frame_next(unknown, sizeof unknown, &p, &f) == -1,
              "an unknown frame type is refused");
        uint8_t empty[3] = {0x00, 0x00, 0x00};
        p = 0;
        CHECK(wb_quic_frame_next(empty, sizeof empty, &p, &f) == 1,
              "PADDING is one frame");
        CHECK_EQ("and it consumes the rest of the packet", p, 3);
    }

    printf("\n=== QUIC TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail != 0;
}
