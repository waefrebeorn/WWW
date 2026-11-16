/*
 * wb_quic_coalesce_test.c -- LP-93 stone 12 gate.
 *
 * A datagram can hold several long-header packets; a short-header packet can
 * only be last. The split uses the Length field, which is in the clear, so the
 * gate builds real packets with stone 7's builder and concatenates them.
 */
#include "wb_quic_coalesce.h"
#include "wb_quic_short.h"
#include "wb_quic_packet.h"

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

static uint8_t dcid[8] = {0x83,0x94,0xc8,0xf0,0x3e,0x51,0x57,0x08};
static uint8_t other[8] = {1,2,3,4,5,6,7,8};
static uint8_t scratch[8192];

static size_t build_initial(uint8_t *out, size_t cap, const uint8_t *dcid,
                            size_t dcid_len, uint64_t pn, int pn_len) {
    uint8_t frames[64];
    size_t fl = wb_quic_frame_ping(frames, sizeof frames);
    /* RFC 9001 section 5.4.2: the encoded number plus the payload must be at
     * least four bytes longer than the 16-byte sample */
    while ((size_t)pn_len + fl < 20) {
        size_t add = wb_quic_frame_padding(frames + fl, sizeof frames - fl, 1);
        if (!add) return 0;
        fl += add;
    }
    wb_quic_keys k;
    wb_quic_keys dummy;
    wb_quic_initial_keys(dcid, dcid_len, &k, &dummy);
    size_t n = 0;
    if (wb_quic_packet_build(out, &n, cap, &k, WB_QUIC_INITIAL, dcid, dcid_len,
                             NULL, 0, pn, pn_len, frames, fl, NULL, 0) != 0)
        return 0;
    return n;
}

int main(void) {
    wb_quic_keys client, server;
    wb_quic_initial_keys(dcid, sizeof dcid, &client, &server);

    /* ---- one packet in a datagram ---- */
    {
        uint8_t dg[1024];
        size_t a = build_initial(dg, sizeof dg, dcid, sizeof dcid, 0, 1);
        CHECK(a > 0, "an Initial builds");
        wb_quic_coalesced c[8];
        int n = 0;
        CHECK(wb_quic_coalesce_split(dg, a, c, 8, &n) == 0, "it splits");
        CHECK_EQ("into one packet", n, 1);
        CHECK_EQ("at the start", c[0].offset, 0);
        CHECK_EQ("and the whole datagram", c[0].len, a);
        CHECK_EQ("as a long header", c[0].is_short, 0);
        CHECK_EQ("of type Initial", c[0].type, WB_QUIC_INITIAL);
        CHECK_MEM("and it is the packet", dg + c[0].offset, dg, a);
    }

    /* ---- two packets, as a client's first flight ---- */
    {
        uint8_t dg[2048];
        size_t a = build_initial(dg, sizeof dg, dcid, sizeof dcid, 0, 1);
        size_t b = build_initial(dg + a, sizeof dg - a, dcid, sizeof dcid, 1, 1);
        CHECK(a > 0 && b > 0, "two Initials build");
        size_t total = a + b;
        wb_quic_coalesced c[8];
        int n = 0;
        CHECK(wb_quic_coalesce_split(dg, total, c, 8, &n) == 0, "they split");
        CHECK_EQ("into two", n, 2);
        CHECK_EQ("the second follows the first", c[1].offset, a);
        CHECK_EQ("and runs to the end", c[1].len, b);
        CHECK_EQ("the first is the whole first packet", c[0].len, a);
        CHECK_MEM("and the bytes are the first packet", dg + c[0].offset, dg, a);
        CHECK_MEM("and the second", dg + c[1].offset, dg + a, b);

        /* each half still opens, which is what "separate and complete" means */
        uint8_t copy[2048];
        memcpy(copy, dg, total);
        wb_quic_hdr h;
        uint64_t pn = 0;
        size_t off = 0;
        CHECK(wb_quic_packet_open(copy, &c[0].len, scratch, sizeof scratch,
                                  &client, WB_QUIC_VERSION_1, &h, &pn, &off) == 0,
              "the first still opens");
        CHECK_EQ("with its number", pn, 0);
        memcpy(copy + a, dg + a, b);
        size_t bl = b;
        CHECK(wb_quic_packet_open(copy + a, &bl, scratch, sizeof scratch,
                                  &client, WB_QUIC_VERSION_1, &h, &pn, &off) == 0,
              "and the second");
        CHECK_EQ("with its own number", pn, 1);
    }

    /* ---- three at increasing levels ---- */
    {
        uint8_t dg[2048];
        size_t p = 0;
        p += build_initial(dg + p, sizeof dg - p, dcid, sizeof dcid, 0, 1);
        p += build_initial(dg + p, sizeof dg - p, dcid, sizeof dcid, 1, 1);
        p += build_initial(dg + p, sizeof dg - p, dcid, sizeof dcid, 2, 1);
        wb_quic_coalesced c[8];
        int n = 0;
        CHECK(wb_quic_coalesce_split(dg, p, c, 8, &n) == 0, "three split");
        CHECK_EQ("into three", n, 3);
        size_t at = 0;
        for (int i = 0; i < 3; i++) {
            CHECK_EQ("each starts where the last ended", c[i].offset, at);
            at += c[i].len;
        }
        CHECK_EQ("and they cover the datagram", at, p);
    }

    /* ---- a short header is last ---- */
    {
        uint8_t dg[2048];
        size_t a = build_initial(dg, sizeof dg, dcid, sizeof dcid, 0, 1);
        uint8_t sp[1024];
        size_t sl = 0;
        uint8_t sf[64];
        size_t sfl = wb_quic_frame_ping(sf, sizeof sf);
        while (1 + sfl < 20) {
            size_t add = wb_quic_frame_padding(sf + sfl, sizeof sf - sfl, 1);
            if (!add) break;
            sfl += add;
        }
        CHECK(wb_quic_short_build(sp, &sl, sizeof sp, &client, 0, 0, dcid,
                                  sizeof dcid, 7, 1, sf, sfl) == 0,
              "a short packet builds");
        memcpy(dg + a, sp, sl);
        size_t total = a + sl;

        wb_quic_coalesced c[8];
        int n = 0;
        CHECK(wb_quic_coalesce_split(dg, total, c, 8, &n) == 0,
              "a long then a short splits");
        CHECK_EQ("into two", n, 2);
        CHECK_EQ("the first is long", c[0].is_short, 0);
        CHECK_EQ("the second is short", c[1].is_short, 1);
        CHECK_EQ("and is last", c[1].offset, a);
        CHECK_EQ("and runs to the end", c[1].len, sl);
        CHECK_MEM("with its own bytes", dg + c[1].offset, sp, sl);

        /* alone */
        memcpy(dg, sp, sl);
        CHECK(wb_quic_coalesce_split(dg, sl, c, 8, &n) == 0,
              "a short packet alone splits");
        CHECK_EQ("into one", n, 1);
        CHECK_EQ("as a short header", c[0].is_short, 1);
        CHECK_EQ("covering the whole datagram", c[0].len, sl);

        /* a sender must not put anything after a short header, but the split
         * cannot tell the difference: the rest of the datagram is that
         * packet's payload */
        memcpy(dg, sp, sl);
        size_t b = build_initial(dg + sl, sizeof dg - sl, dcid, sizeof dcid, 1, 1);
        CHECK(wb_quic_coalesce_split(dg, sl + b, c, 8, &n) == 0,
              "and the rest is taken as its payload");
        CHECK_EQ("as one packet", n, 1);
        CHECK_EQ("of the whole datagram", c[0].len, sl + b);
    }

    /* ---- malformed datagrams ---- */
    {
        uint8_t dg[1024];
        size_t a = build_initial(dg, sizeof dg, dcid, sizeof dcid, 0, 1);
        wb_quic_coalesced c[8];
        int n = 0;
        CHECK(wb_quic_coalesce_split(dg, a - 1, c, 8, &n) == -1,
              "a Length field running past the datagram");
        CHECK(wb_quic_coalesce_split(dg, 0, c, 8, &n) == 0,
              "an empty datagram has no packets");
        CHECK_EQ("and reports none", n, 0);

        /* two packets with different connection IDs */
        uint8_t dg2[2048];
        size_t x = build_initial(dg2, sizeof dg2, dcid, sizeof dcid, 0, 1);
        size_t y = build_initial(dg2 + x, sizeof dg2 - x, other, sizeof other, 1, 1);
        CHECK(x > 0 && y > 0, "two packets build");
        CHECK(wb_quic_coalesce_split(dg2, x + y, c, 8, &n) == -1,
              "different connection IDs are refused");

        /* a truncated long header */
        uint8_t tiny[4] = {0xc0, 0, 0, 1};
        CHECK(wb_quic_coalesce_split(tiny, sizeof tiny, c, 8, &n) == -1,
              "a header that is too short");
        CHECK(wb_quic_coalesce_split(NULL, 4, c, 8, &n) == -1, "a NULL datagram");
        CHECK(wb_quic_coalesce_split(dg, a, c, 0, &n) == -1, "and no room");
    }

    printf("\n=== QUIC COALESCE TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail != 0;
}
