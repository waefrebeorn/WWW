/*
 * wb_net_sig_test.c -- LP-95 stone 4 gate: candidate exchange and the
 * connect-token gateway.
 */
#include "wb_net_sig.h"
#include "wb_net_transport.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static wb_stun_addr a4(int a, int b, int c, int d, uint16_t port) {
    wb_stun_addr x;
    memset(&x, 0, sizeof x);
    x.family = 4;
    x.addr[0] = (uint8_t)a; x.addr[1] = (uint8_t)b;
    x.addr[2] = (uint8_t)c; x.addr[3] = (uint8_t)d;
    x.port = port;
    return x;
}

static wb_ice_candidate cand(int type, int a, int b, int c, int d, uint16_t port) {
    wb_ice_candidate x;
    memset(&x, 0, sizeof x);
    x.type = type;
    x.component = 1;
    x.priority = wb_ice_candidate_priority(type, 65535, 1);
    x.foundation = (uint32_t)(0x1111u + (uint32_t)type);
    x.addr = a4(a, b, c, d, port);
    x.base = a4(a, b, c, d, port);
    return x;
}

static wb_ice_creds creds(const char *u, const char *p, int controlling) {
    wb_ice_creds c;
    memset(&c, 0, sizeof c);
    strncpy(c.ufrag, u, WB_ICE_UFRAG_MAX - 1);
    strncpy(c.password, p, WB_ICE_PASSWORD_MAX - 1);
    c.controlling = controlling;
    return c;
}

int main(void) {
    /* ---- an empty offer ---- */
    {
        uint8_t buf[WB_NET_MAX_WIRE];
        wb_ice_creds cr = creds("ufrag-a", "a-very-long-password", 1);
        size_t n = wb_ice_sig_encode(buf, sizeof buf, &cr, NULL, 0, WB_ICE_SIG_OFFER);
        CHECK(n == WB_ICE_SIG_HEADER, "an empty offer is just the header");
        CHECK(buf[0] == 'I' && buf[1] == 'C', "with the magic");
        CHECK(buf[2] == 1, "and the version");
        CHECK(buf[3] == 0, "an offer is not an answer");
        CHECK(memcmp(buf + 6, "ufrag-a", 7) == 0, "carrying the username fragment");
        CHECK(memcmp(buf + 30, "a-very-long-password", 21) == 0, "and the password");
        CHECK(buf[94] == 1, "and the controlling flag");

        wb_ice_creds got_c;
        size_t got_n = 99;
        wb_ice_candidate got[4];
        CHECK(wb_ice_sig_decode(buf, n, &got_c, got, 4, &got_n) == 0, "it decodes");
        CHECK(got_n == 0, "with no candidates");
        CHECK(strcmp(got_c.ufrag, "ufrag-a") == 0, "the ufrag survives");
        CHECK(strcmp(got_c.password, "a-very-long-password") == 0, "the password survives");
        CHECK(got_c.controlling == 1, "and the role");
    }

    /* ---- a full offer round-trips ---- */
    {
        wb_ice_candidate c[6];
        c[0] = cand(WB_ICE_HOST, 192, 0, 2, 1, 50000);
        c[1] = cand(WB_ICE_SERVER_REFLEXIVE, 203, 0, 113, 1, 8000);
        c[2] = cand(WB_ICE_PEER_REFLEXIVE, 203, 0, 113, 2, 8001);
        c[3] = cand(WB_ICE_RELAYED, 198, 51, 100, 7, 3478);
        c[4] = cand(WB_ICE_HOST, 192, 0, 2, 2, 50001);
        c[5] = cand(WB_ICE_RELAYED, 198, 51, 100, 8, 3479);
        wb_ice_creds cr = creds("abcd", "efgh", 0);

        uint8_t buf[WB_NET_MAX_WIRE];
        size_t n = wb_ice_sig_encode(buf, sizeof buf, &cr, c, 6, WB_ICE_SIG_ANSWER);
        CHECK(n == WB_ICE_SIG_HEADER + 6 * WB_ICE_SIG_RECORD, "the length is the header plus the records");
        CHECK(buf[3] == 1, "an answer is flagged");

        wb_ice_candidate out[8];
        wb_ice_creds oc;
        size_t on = 0;
        CHECK(wb_ice_sig_decode(buf, n, &oc, out, 8, &on) == 0, "a full offer decodes");
        CHECK(on == 6, "all six candidates");
        int same = 1;
        for (size_t i = 0; i < on; i++) {
            if (out[i].type != c[i].type || out[i].component != c[i].component ||
                out[i].priority != c[i].priority ||
                out[i].foundation != c[i].foundation ||
                out[i].addr.family != c[i].addr.family ||
                out[i].addr.port != c[i].addr.port ||
                memcmp(out[i].addr.addr, c[i].addr.addr, 4) != 0 ||
                out[i].base.port != c[i].base.port ||
                memcmp(out[i].base.addr, c[i].base.addr, 4) != 0) {
                same = 0;
            }
        }
        CHECK(same, "every field of every candidate survives");
        CHECK(strcmp(oc.ufrag, "abcd") == 0 && strcmp(oc.password, "efgh") == 0,
              "and the credentials");
        CHECK(oc.controlling == 0, "and the role");
    }

    /* ---- the size bound is enforced ---- */
    {
        wb_ice_candidate many[WB_ICE_SIG_MAX_CANDIDATES + 2];
        for (size_t i = 0; i < sizeof many / sizeof many[0]; i++) {
            many[i] = cand(WB_ICE_HOST, 10, 0, 0, (int)(i % 250), (uint16_t)(50000 + i));
        }
        uint8_t buf[WB_NET_MAX_WIRE];
        wb_ice_creds cr = creds("u", "p", 1);
        size_t n = wb_ice_sig_encode(buf, sizeof buf, &cr, many, WB_ICE_SIG_MAX_CANDIDATES, WB_ICE_SIG_OFFER);
        CHECK(n > 0 && n <= WB_NET_MAX_WIRE,
              "the maximum candidate list still fits in one datagram");
        CHECK(wb_ice_sig_encode(buf, sizeof buf, &cr, many,
                                WB_ICE_SIG_MAX_CANDIDATES + 1, WB_ICE_SIG_OFFER) == 0,
              "one more is refused, not truncated");
        CHECK(wb_ice_sig_encode(buf, 10, &cr, many, 1, WB_ICE_SIG_OFFER) == 0,
              "and a buffer too small is refused");
    }

    /* ---- malformed input is refused ---- */
    {
        wb_ice_candidate c[1] = { 0 };
        c[0] = cand(WB_ICE_HOST, 10, 0, 0, 1, 50000);
        wb_ice_creds cr = creds("u", "p", 1);
        uint8_t buf[WB_NET_MAX_WIRE];
        size_t n = wb_ice_sig_encode(buf, sizeof buf, &cr, c, 1, WB_ICE_SIG_OFFER);
        wb_ice_candidate out[4];
        size_t on;

        CHECK(wb_ice_sig_decode(buf, WB_ICE_SIG_HEADER - 1, NULL, NULL, 0, NULL) == -1,
              "a short buffer is refused");
        CHECK(wb_ice_sig_decode(NULL, n, NULL, NULL, 0, NULL) == -1,
              "a null buffer is refused");
        buf[0] = 'X';
        CHECK(wb_ice_sig_decode(buf, n, NULL, out, 4, &on) == -1,
              "a wrong magic is refused");
        buf[0] = 'I';
        buf[2] = 99;
        CHECK(wb_ice_sig_decode(buf, n, NULL, out, 4, &on) == -1,
              "a wrong version is refused");
        buf[2] = 1;
        buf[5] = 2;                      /* claims two candidates, carries one */
        CHECK(wb_ice_sig_decode(buf, n, NULL, out, 4, &on) == -1,
              "a count that disagrees with the length is refused");
        buf[5] = 1;
        buf[WB_ICE_SIG_HEADER] = 9;      /* a bad address family */
        CHECK(wb_ice_sig_decode(buf, n, NULL, out, 4, &on) == -1,
              "a bad address family is refused");
        CHECK(wb_ice_sig_decode(buf, n, NULL, out, 0, &on) == -1,
              "and a list too long for the caller's array is refused");
    }

    /* ---- the connect-token gateway ---- */
    {
        wb_net_gateway *g = wb_net_gateway_new(600);
        CHECK(g != NULL, "a gateway allocates");
        uint8_t t1[16], t2[16], other[16];
        wb_net_gateway_issue(g, "stadium-7", t1);
        wb_net_gateway_issue(g, "stadium-7", t2);
        wb_net_gateway_issue(g, "other-room", other);
        CHECK(memcmp(t1, t2, 16) == 0, "a room always yields the same token");
        CHECK(memcmp(t1, other, 16) != 0, "two rooms do not share a token");
        CHECK(wb_net_gateway_issued(g) == 3, "three issues are counted");

        CHECK(!wb_net_gateway_connected(g, t1), "one peer is not a connection");
        CHECK(wb_net_gateway_redeem(g, t1, 1) == 1, "the first peer redeems");
        CHECK(!wb_net_gateway_connected(g, t1), "still not connected");
        CHECK(wb_net_gateway_peer(g, t1, 1) == -1, "and has no peer yet");
        CHECK(wb_net_gateway_redeem(g, t1, 2) == 1, "the second peer redeems");
        CHECK(wb_net_gateway_connected(g, t1), "and the room is connected");
        CHECK(wb_net_gateway_peer(g, t1, 1) == 2, "each resolves the other");
        CHECK(wb_net_gateway_peer(g, t1, 2) == 1, "in both directions");
        CHECK(wb_net_gateway_connected_rooms(g) == 1, "one room connected");

        /* one redemption per peer, one room of two */
        CHECK(wb_net_gateway_redeem(g, t1, 1) == 0, "a peer cannot redeem twice");
        CHECK(wb_net_gateway_redeem(g, t1, 3) == 0, "a third peer does not fit");
        CHECK(wb_net_gateway_refused(g) == 2, "and both are counted");

        /* a token from another room does not join this one */
        CHECK(wb_net_gateway_redeem(g, other, 3) == 1, "another room accepts its own peer");
        CHECK(wb_net_gateway_peer(g, t1, 1) == 2, "and does not disturb the first");
        CHECK(wb_net_gateway_peer(g, other, 3) == -1, "it has no peer yet");

        /* an unknown token is refused */
        uint8_t junk[16] = {0};
        CHECK(wb_net_gateway_redeem(g, junk, 4) == 0, "an unknown token is refused");

        wb_net_gateway_free(g);
    }

    /* ---- tokens expire ---- */
    {
        wb_net_gateway *g = wb_net_gateway_new(30);
        uint8_t t[16];
        wb_net_gateway_issue(g, "short", t);
        CHECK(wb_net_gateway_redeem(g, t, 1) == 1, "redeemable at once");
        wb_net_gateway_tick(g, 31.0);
        CHECK(wb_net_gateway_redeem(g, t, 2) == 0, "but not after the lifetime");
        CHECK(wb_net_gateway_connected(g, t) == 0, "and the room is gone");
        CHECK(wb_net_gateway_peer(g, t, 1) == -1, "with no peer to resolve");

        /* re-issuing brings the room back */
        wb_net_gateway_issue(g, "short", t);
        CHECK(wb_net_gateway_redeem(g, t, 2) == 1, "a re-issued token works");
        wb_net_gateway_free(g);
    }

    /* ---- the credential lengths are bounded by the buffer ---- */
    {
        wb_ice_creds big;
        memset(&big, 'x', sizeof big);
        big.controlling = 1;
        uint8_t buf[WB_NET_MAX_WIRE];
        size_t n = wb_ice_sig_encode(buf, sizeof buf, &big, NULL, 0, WB_ICE_SIG_OFFER);
        CHECK(n == WB_ICE_SIG_HEADER, "over-long credentials still fit the header");
        wb_ice_creds out;
        size_t on;
        CHECK(wb_ice_sig_decode(buf, n, &out, NULL, 0, &on) == 0, "and decode");
        CHECK(strlen(out.ufrag) < WB_ICE_UFRAG_MAX, "the ufrag is NUL terminated");
        CHECK(strlen(out.password) < WB_ICE_PASSWORD_MAX, "and so is the password");
    }

    printf("\n=== NET SIG TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail != 0;
}
