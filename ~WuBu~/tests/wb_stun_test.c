/*
 * wb_stun_test.c -- LP-95 stone 1 gate: the STUN binding transaction.
 *
 * The address arithmetic is checked against a hand-computed vector rather than
 * against the code's own idea of itself:
 *
 *   the client is 192.0.2.1:40000; the server sees 203.0.113.1:8000.
 *   XOR-MAPPED-ADDRESS: port 8000 ^ 0x2112 = 0x3E52,
 *                       addr  CB.00.71.01 ^ 21.12.A4.42 = EA.12.D5.43.
 */
#include "wb_stun.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)
#define CHECK_MEM(name, got, n, ...) do { \
    const uint8_t _w[] = { __VA_ARGS__ }; \
    if (memcmp((got), _w, (n)) == 0) g_pass++; \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); \
           printf("  got  "); for (size_t i_ = 0; i_ < (size_t)(n); i_++) printf("%02x", ((const uint8_t *)(got))[i_]); \
           printf("\n  want "); for (size_t i_ = 0; i_ < (size_t)(n); i_++) printf("%02x", _w[i_]); \
           printf("\n"); } \
} while (0)

static int sink_send(void *ud, int peer, const uint8_t *data, size_t len) {
    (void)peer;
    uint8_t *dst = (uint8_t *)ud;
    memcpy(dst, data, len);
    return 0;
}

int main(void) {
    static const uint8_t txid[12] = {0,1,2,3,4,5,6,7,8,9,10,11};

    /* ---- the header is big-endian and carries the cookie ---- */
    {
        uint8_t buf[64];
        size_t n = wb_stun_build_binding_request(buf, sizeof buf, txid);
        CHECK(n == WB_STUN_HEADER, "a bare request is just the header");
        CHECK_MEM("message type", buf + 0, 2, 0x00, 0x01);
        CHECK_MEM("message length", buf + 2, 2, 0x00, 0x00);
        CHECK_MEM("magic cookie", buf + 4, 4, 0x21, 0x12, 0xA4, 0x42);
        CHECK(memcmp(buf + 8, txid, 12) == 0, "transaction id");
        CHECK((buf[0] & 0xC0u) == 0, "the top two bits are zero");
    }

    /* ---- a request parses back ---- */
    {
        uint8_t buf[64];
        wb_stun_build_binding_request(buf, sizeof buf, txid);
        uint16_t type = 0;
        const uint8_t *t = NULL;
        CHECK(wb_stun_parse_header(buf, WB_STUN_HEADER, &type, &t) == 0,
              "a request parses");
        CHECK(type == WB_STUN_BINDING_REQUEST, "as a Binding request");
        CHECK(t && memcmp(t, txid, 12) == 0, "with its transaction id");
        CHECK(wb_stun_is_binding(type), "and is recognised as Binding");
    }

    /* ---- the hand-computed XOR-MAPPED-ADDRESS vector ---- */
    {
        wb_stun_addr mapped;
        memset(&mapped, 0, sizeof mapped);
        mapped.family = 4;
        mapped.port = 8000;                       /* 0x1F40 */
        memcpy(mapped.addr, (const uint8_t[]){203, 0, 113, 1}, 4);

        uint8_t buf[64];
        size_t n = wb_stun_build_binding_success(buf, sizeof buf, txid, &mapped);
        CHECK(n == WB_STUN_HEADER + 12, "a response with one address is 32 bytes");

        /* the attribute header */
        CHECK_MEM("attr type", buf + 20, 2, 0x00, 0x20);
        CHECK_MEM("attr length", buf + 22, 2, 0x00, 0x08);
        CHECK(buf[24] == 0, "the reserved byte is zero");
        CHECK(buf[25] == 0x01, "the family is IPv4");
        /* port 0x1F40 ^ 0x2112 = 0x3E52 */
        CHECK_MEM("xor'd port", buf + 26, 2, 0x3E, 0x52);
        /* CB.00.71.01 ^ 21.12.A4.42 = EA.12.F5.43 */
        CHECK_MEM("xor'd address", buf + 28, 4, 0xEA, 0x12, 0xD5, 0x43);

        /* and it reads back to the address that was put in */
        wb_stun_addr got;
        memset(&got, 0, sizeof got);
        CHECK(wb_stun_find_mapped(buf, n, &got) == 0, "the response yields a mapped address");
        CHECK(got.family == 4, "with the right family");
        CHECK(got.port == 8000, "the right port");
        CHECK(memcmp(got.addr, mapped.addr, 4) == 0, "and the right address");
    }

    /* ---- the port XOR uses only the top 16 bits of the cookie ---- */
    {
        wb_stun_addr a, b;
        memset(&a, 0, sizeof a);
        a.family = 4;
        a.port = 0x1234;
        memcpy(a.addr, (const uint8_t[]){1, 2, 3, 4}, 4);
        b = a;
        b.port = (uint16_t)(a.port ^ 0xFFFF);
        uint8_t ba[64], bb[64];
        wb_stun_build_binding_success(ba, sizeof ba, txid, &a);
        wb_stun_build_binding_success(bb, sizeof bb, txid, &b);
        /* the encoded ports differ by the cookie's top half, not by 0xFFFF */
        CHECK_MEM("port encoding", ba + 26, 2, 0x12 ^ 0x21, 0x34 ^ 0x12);
        (void)bb;
    }

    /* ---- IPv6: the cookie then the transaction id ---- */
    {
        wb_stun_addr mapped;
        memset(&mapped, 0, sizeof mapped);
        mapped.family = 6;
        mapped.port = 6000;                       /* 0x1770 ^ 0x2112 = 0x3662 */
        memcpy(mapped.addr,
               (const uint8_t[]){0x20,0x01,0x0D,0xB8, 0,0,0,0, 0,0,0,0, 0,0,0,0x01}, 16);
        uint8_t buf[64];
        size_t n = wb_stun_build_binding_success(buf, sizeof buf, txid, &mapped);
        CHECK(n == WB_STUN_HEADER + 24, "an IPv6 address attribute is 24 bytes");
        CHECK(buf[25] == 0x02, "the family is IPv6");
        CHECK_MEM("ipv6 xor'd port", buf + 26, 2, 0x36, 0x62);
        /* 20^21=01  01^12=13  0D^A4=A9  B8^42=FA  then the txid bytes 00..0B */
        CHECK_MEM("ipv6 xor'd address", buf + 28, 16, 0x01, 0x13, 0xA9, 0xFA, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0x01^0x0B);

        wb_stun_addr got;
        memset(&got, 0, sizeof got);
        CHECK(wb_stun_find_mapped(buf, n, &got) == 0, "an IPv6 mapped address reads back");
        CHECK(got.family == 6 && got.port == 6000, "with the right family and port");
        CHECK(memcmp(got.addr, mapped.addr, 16) == 0, "and the right address");
    }

    /* ---- MAPPED-ADDRESS is the fallback when there is no XOR form ---- */
    {
        uint8_t buf[64];
        size_t n = wb_stun_build_binding_success(buf, sizeof buf, txid,
                                                 &(wb_stun_addr){ .family = 4, .port = 8000,
                                                    .addr = {203, 0, 113, 1} });
        /* rewrite the attribute as a plain MAPPED-ADDRESS, unmasked */
        buf[20] = 0x00; buf[21] = 0x01;
        buf[26] = 0x1F; buf[27] = 0x40;
        memcpy(buf + 28, (const uint8_t[]){203, 0, 113, 1}, 4);
        wb_stun_addr got;
        memset(&got, 0, sizeof got);
        CHECK(wb_stun_find_mapped(buf, n, &got) == 0,
              "a plain MAPPED-ADDRESS is still found");
        CHECK(got.port == 8000 && memcmp(got.addr, (const uint8_t[]){203,0,113,1}, 4) == 0,
              "and is read unmasked");
    }

    /* ---- malformed input is refused, not guessed at ---- */
    {
        uint8_t buf[64];
        wb_stun_build_binding_request(buf, sizeof buf, txid);
        CHECK(wb_stun_parse_header(buf, WB_STUN_HEADER - 1, NULL, NULL) == -1,
              "a short buffer is refused");
        CHECK(wb_stun_parse_header(NULL, WB_STUN_HEADER, NULL, NULL) == -1,
              "a null buffer is refused");
        uint8_t bad[64];
        memcpy(bad, buf, sizeof bad);
        bad[6] = 0x22;                          /* wrong cookie */
        CHECK(wb_stun_parse_header(bad, WB_STUN_HEADER, NULL, NULL) == -1,
              "a wrong magic cookie is refused");
        memcpy(bad, buf, sizeof bad);
        bad[0] = 0xC0;                          /* the top bits set */
        CHECK(wb_stun_parse_header(bad, WB_STUN_HEADER, NULL, NULL) == -1,
              "a non-zero top two bits are refused");
        memcpy(bad, buf, sizeof bad);
        bad[5] = 3;                             /* length not a multiple of four */
        CHECK(wb_stun_parse_header(bad, WB_STUN_HEADER, NULL, NULL) == -1,
              "a length that is not a multiple of four is refused");
        memcpy(bad, buf, sizeof bad);
        bad[5] = 4;                             /* claims an attribute that is not there */
        CHECK(wb_stun_parse_header(bad, WB_STUN_HEADER, NULL, NULL) == -1,
              "a length that does not match the buffer is refused");
        CHECK(wb_stun_is_binding(WB_STUN_BINDING_SUCCESS), "a success is Binding");
        CHECK(wb_stun_is_binding(WB_STUN_BINDING_ERROR), "an error is Binding");
        CHECK(!wb_stun_is_binding(0x0002), "another method is not");
    }

    /* ---- the server answers what it saw ---- */
    {
        wb_stun_server *s = wb_stun_server_new();
        uint8_t req[64], resp[64];
        wb_stun_build_binding_request(req, sizeof req, txid);
        wb_stun_addr observed;
        memset(&observed, 0, sizeof observed);
        observed.family = 4;
        observed.port = 8000;
        memcpy(observed.addr, (const uint8_t[]){203, 0, 113, 1}, 4);

        uint8_t delivered[64];
        memset(delivered, 0, sizeof delivered);
        size_t n = wb_stun_server_handle(s, req, WB_STUN_HEADER, 1, &observed,
                                         sink_send, delivered, resp, sizeof resp);
        CHECK(n == WB_STUN_HEADER + 12, "a request gets a response");
        CHECK(wb_stun_server_requests(s) == 1, "and is counted");
        CHECK(memcmp(delivered, resp, n) == 0, "which was handed to the sender");
        CHECK(memcmp(delivered + 8, txid, 12) == 0, "the response echoes the transaction id");
        wb_stun_addr got;
        memset(&got, 0, sizeof got);
        CHECK(wb_stun_find_mapped(delivered, n, &got) == 0, "the response parses");
        CHECK(got.port == 8000 && memcmp(got.addr, observed.addr, 4) == 0,
              "and reports the address the server saw");

        /* a response is not something a server answers */
        size_t r2 = wb_stun_server_handle(s, delivered, n, 1, &observed,
                                          sink_send, delivered, resp, sizeof resp);
        CHECK(r2 == 0, "a response gets no answer");
        CHECK(wb_stun_server_requests(s) == 1, "and is not counted");

        /* an indication gets no response either */
        uint8_t ind[64];
        wb_stun_build_binding_request(ind, sizeof ind, txid);
        ind[1] = 0x11;
        CHECK(wb_stun_server_handle(s, ind, WB_STUN_HEADER, 1, &observed,
                                    sink_send, delivered, resp, sizeof resp) == 0,
              "an indication gets no answer");

        /* garbage gets no answer */
        uint8_t junk[8] = {1,2,3,4,5,6,7,8};
        CHECK(wb_stun_server_handle(s, junk, sizeof junk, 1, &observed,
                                    sink_send, delivered, resp, sizeof resp) == 0,
              "garbage gets no answer");
        CHECK(wb_stun_server_requests(s) == 1, "and nothing was counted");
        wb_stun_server_free(s);
    }

    /* ---- a buffer too small is refused rather than truncated ---- */
    {
        uint8_t tiny[8];
        CHECK(wb_stun_build_binding_request(tiny, sizeof tiny, txid) == 0,
              "a request does not fit in 8 bytes");
        wb_stun_addr a;
        memset(&a, 0, sizeof a);
        a.family = 4; a.port = 1; memcpy(a.addr, (const uint8_t[]){1,2,3,4}, 4);
        CHECK(wb_stun_build_binding_success(tiny, sizeof tiny, txid, &a) == 0,
              "a response does not fit in 8 bytes");
    }

    printf("\n=== STUN TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail != 0;
}
