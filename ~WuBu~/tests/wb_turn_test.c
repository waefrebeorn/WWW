/*
 * wb_turn_test.c -- LP-95 stone 3 gate: the TURN relay.
 *
 * Proves the four methods do what they exist for, and that the relay refuses
 * what it must: a permission is what stops it being an open relay.
 */
#include "wb_turn.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static const uint8_t txid_a[12] = {0,1,2,3,4,5,6,7,8,9,10,11};
static const uint8_t txid_b[12] = {1,2,3,4,5,6,7,8,9,10,11,12};

typedef struct {
    int  n;
    uint16_t type[8];
    size_t len[8];
    uint8_t data[8][128];
} resp_sink;

static int on_send(void *ud, int peer, const uint8_t *data, size_t len) {
    (void)peer;
    resp_sink *r = (resp_sink *)ud;
    if (r->n >= 8) return -1;
    r->type[r->n] = (uint16_t)((data[0] << 8) | data[1]);
    r->len[r->n] = len;
    memcpy(r->data[r->n], data, len);
    r->n++;
    return 0;
}

typedef struct {
    int n;
    int peer[8];
    uint16_t from_port[8];
    uint8_t data[8][64];
    size_t len[8];
} fwd_sink;

static void on_deliver(void *ud, int peer, const wb_stun_addr *from,
                       const uint8_t *data, size_t len) {
    fwd_sink *f = (fwd_sink *)ud;
    if (f->n >= 8) return;
    f->peer[f->n] = peer;
    f->from_port[f->n] = from->port;
    f->len[f->n] = len;
    memcpy(f->data[f->n], data, len);
    f->n++;
}

static wb_stun_addr relay_base;
static int relay_base_ready = 0;

static wb_stun_addr a4(int a, int b, int c, int d, uint16_t port) {
    wb_stun_addr x;
    memset(&x, 0, sizeof x);
    x.family = 4;
    x.addr[0] = (uint8_t)a; x.addr[1] = (uint8_t)b;
    x.addr[2] = (uint8_t)c; x.addr[3] = (uint8_t)d;
    x.port = port;
    return x;
}

int main(void) {
    relay_base = a4(198, 51, 100, 1, 0);
    relay_base_ready = 1;

    /* ---- message classes ---- */
    {
        CHECK(wb_turn_is_request(WB_TURN_ALLOCATE_REQUEST), "Allocate is a request");
        CHECK(wb_turn_is_success(WB_TURN_ALLOCATE_SUCCESS), "Allocate success is a success");
        CHECK(wb_turn_is_error(WB_TURN_ALLOCATE_ERROR), "Allocate error is an error");
        CHECK(wb_turn_is_indication(WB_TURN_SEND_INDICATION), "Send is an indication");
        CHECK(wb_turn_is_indication(WB_TURN_DATA_INDICATION), "Data is an indication");
        CHECK(!wb_turn_is_request(WB_TURN_SEND_INDICATION), "an indication is not a request");
        CHECK(!wb_turn_is_indication(WB_TURN_ALLOCATE_REQUEST), "a request is not an indication");
    }

    /* ---- the client builds a well-formed Allocate ---- */
    {
        uint8_t buf[64];
        size_t n = wb_turn_build_allocate(buf, sizeof buf, txid_a, 1);
        CHECK(n == WB_STUN_HEADER + 8, "an Allocate request is 28 bytes");
        CHECK(buf[0] == 0x00 && buf[1] == 0x03, "with the Allocate request type");
        CHECK(buf[27] == WB_TURN_TRANSPORT_UDP, "asking for UDP");
        CHECK(buf[20] == 0x00 && buf[21] == 0x19, "carrying REQUESTED-TRANSPORT");
    }

    /* ---- Allocate gets a relayed address ---- */
    {
        wb_turn_server *s = wb_turn_server_new(&relay_base, 40000, 600);
        CHECK(s != NULL, "a server allocates");
        resp_sink r = {0};
        uint8_t req[64], resp[128];
        wb_stun_addr observed = a4(203, 0, 113, 1, 50000);
        wb_stun_addr relay;

        size_t n = wb_turn_build_allocate(req, sizeof req, txid_a, 1);
        CHECK(wb_turn_server_handle(s, req, n, 1, &observed, on_send, &r,
                                    resp, sizeof resp, NULL, NULL) == 1,
              "an Allocate is answered");
        CHECK(r.n == 1, "with one response");
        CHECK(r.type[0] == WB_TURN_ALLOCATE_SUCCESS, "a success response");

        uint32_t lifetime = 0;
        CHECK(wb_turn_parse_allocate_success(r.data[0], r.len[0], &relay, &lifetime) == 0,
              "the response parses");
        CHECK(relay.family == 4, "as an IPv4 address");
        CHECK(relay.addr[0] == 198 && relay.addr[1] == 51 &&
              relay.addr[2] == 100 && relay.addr[3] == 1, "the relay's own address");
        CHECK(relay.port == 40000, "with the first allocated port");
        CHECK(lifetime == 600, "and the lifetime");
        CHECK(wb_turn_server_allocations(s) == 1, "one allocation is live");

        /* a second client gets the next port */
        resp_sink r2 = {0};
        n = wb_turn_build_allocate(req, sizeof req, txid_b, 1);
        wb_turn_server_handle(s, req, n, 2, &observed, on_send, &r2,
                              resp, sizeof resp, NULL, NULL);
        uint32_t life2;
        wb_stun_addr relay2;
        wb_turn_parse_allocate_success(r2.data[0], r2.len[0], &relay2, &life2);
        CHECK(relay2.port == 40001, "the second allocation gets the next port");
        CHECK(wb_turn_server_allocations(s) == 2, "two allocations are live");
        wb_turn_server_free(s);
    }

    /* ---- permissions gate the relay ---- */
    {
        wb_turn_server *s = wb_turn_server_new(&relay_base, 40000, 600);
        resp_sink r = {0};
        fwd_sink f = {0};
        uint8_t req[64], resp[128];
        wb_stun_addr observed = a4(203, 0, 113, 1, 50000);
        wb_stun_addr peer = a4(203, 0, 113, 9, 55000);

        /* a permission with no allocation is refused */
        size_t n = wb_turn_build_create_permission(req, sizeof req, txid_a, &peer);
        wb_turn_server_handle(s, req, n, 1, &observed, on_send, &r,
                              resp, sizeof resp, on_deliver, &f);
        CHECK(r.n == 1 && r.type[0] == WB_TURN_CREATE_PERMISSION_ERROR,
              "a permission without an allocation is an error");

        /* allocate, then the permission succeeds */
        n = wb_turn_build_allocate(req, sizeof req, txid_a, 1);
        wb_turn_server_handle(s, req, n, 1, &observed, on_send, &r,
                              resp, sizeof resp, on_deliver, &f);
        n = wb_turn_build_create_permission(req, sizeof req, txid_a, &peer);
        wb_turn_server_handle(s, req, n, 1, &observed, on_send, &r,
                              resp, sizeof resp, on_deliver, &f);
        CHECK(r.type[r.n - 1] == WB_TURN_CREATE_PERMISSION_SUCCESS,
              "a permission with an allocation succeeds");
        CHECK(wb_turn_server_permissions(s) == 1, "one permission is live");

        /* the same permission again refreshes rather than duplicating */
        n = wb_turn_build_create_permission(req, sizeof req, txid_a, &peer);
        wb_turn_server_handle(s, req, n, 1, &observed, on_send, &r,
                              resp, sizeof resp, on_deliver, &f);
        CHECK(wb_turn_server_permissions(s) == 1, "a repeated permission is not a second one");

        /* a Send from a permitted peer is forwarded */
        uint8_t msg[] = "hello through the relay";
        n = wb_turn_build_send(req, sizeof req, &peer, msg, sizeof msg);
        CHECK(wb_turn_server_handle(s, req, n, 1, &observed, on_send, &r,
                                    resp, sizeof resp, on_deliver, &f) == 1,
              "a Send indication is handled");
        CHECK(f.n == 1, "and the payload is forwarded");
        CHECK(f.peer[0] == 1 && f.from_port[0] == 55000, "to the client, from the peer");
        CHECK(f.len[0] == sizeof msg && memcmp(f.data[0], msg, sizeof msg) == 0,
              "byte-identical");

        /* a Send from a peer nobody permitted is refused, not forwarded */

        wb_stun_addr stranger = a4(203, 0, 113, 10, 56000);
        n = wb_turn_build_send(req, sizeof req, &stranger, msg, sizeof msg);
        int before = f.n;
        wb_turn_server_handle(s, req, n, 1, &observed, on_send, &r,
                              resp, sizeof resp, on_deliver, &f);
        CHECK(f.n == before, "an unpermitted peer is not forwarded");
        CHECK(wb_turn_server_refused(s) >= 1, "and the refusal is counted");

        /* a Data indication from a permitted peer reaches the client */
        n = wb_turn_build_send(req, sizeof req, &peer, msg, sizeof msg);
        /* Data uses the same attributes with a different type; build it by
         * rewriting the type of the Send we just made */
        req[1] = 0x17;
        int before2 = f.n;
        wb_turn_server_handle(s, req, n, 1, &observed, on_send, &r,
                              resp, sizeof resp, on_deliver, &f);
        CHECK(f.n == before2 + 1, "a Data indication reaches the client");
        CHECK(f.peer[0] == 1, "addressed to the allocating client");

        /* an allocation expiry takes its permissions with it */
        wb_turn_server_tick(s, 700.0);
        CHECK(wb_turn_server_allocations(s) == 0, "an allocation expires");
        n = wb_turn_build_send(req, sizeof req, &peer, msg, sizeof msg);
        req[1] = 0x16;
        int before3 = f.n;
        wb_turn_server_handle(s, req, n, 1, &observed, on_send, &r,
                              resp, sizeof resp, on_deliver, &f);
        CHECK(f.n == before3, "and its permissions expire with it");
        wb_turn_server_free(s);
    }

    /* ---- channel binding ---- */
    {
        wb_turn_server *s = wb_turn_server_new(&relay_base, 40000, 600);
        resp_sink r = {0};
        uint8_t req[64], resp[128];
        wb_stun_addr observed = a4(203, 0, 113, 1, 50000);
        wb_stun_addr peer = a4(203, 0, 113, 9, 55000);

        /* binding without a permission is refused */
        size_t n = wb_turn_build_channel_bind(req, sizeof req, txid_a, 0x4000, &peer);
        wb_turn_server_handle(s, req, n, 1, &observed, on_send, &r,
                              resp, sizeof resp, NULL, NULL);
        CHECK(r.n == 1 && r.type[0] == WB_TURN_CHANNEL_BIND_ERROR,
              "a bind without a permission is refused");
        CHECK(wb_turn_server_refused(s) >= 1, "and counted");

        size_t na = wb_turn_build_allocate(req, sizeof req, txid_a, 1);
        wb_turn_server_handle(s, req, na, 1, &observed, on_send, &r,
                              resp, sizeof resp, NULL, NULL);
        size_t np = wb_turn_build_create_permission(req, sizeof req, txid_a, &peer);
        wb_turn_server_handle(s, req, np, 1, &observed, on_send, &r,
                              resp, sizeof resp, NULL, NULL);

        /* a channel in range binds */
        n = wb_turn_build_channel_bind(req, sizeof req, txid_a, 0x4000, &peer);
        wb_turn_server_handle(s, req, n, 1, &observed, on_send, &r,
                              resp, sizeof resp, NULL, NULL);
        CHECK(r.type[r.n - 1] == WB_TURN_CHANNEL_BIND_SUCCESS,
              "a channel in range binds");

        /* a channel outside the usable range is refused */
        n = wb_turn_build_channel_bind(req, sizeof req, txid_a, 0x5000, &peer);
        wb_turn_server_handle(s, req, n, 1, &observed, on_send, &r,
                              resp, sizeof resp, NULL, NULL);
        CHECK(r.type[r.n - 1] == WB_TURN_CHANNEL_BIND_ERROR,
              "a channel outside 0x4000..0x4FFF is refused");

        /* the same channel to a different peer is refused */
        wb_stun_addr other = a4(203, 0, 113, 11, 57000);
        size_t no = wb_turn_build_create_permission(req, sizeof req, txid_a, &other);
        wb_turn_server_handle(s, req, no, 1, &observed, on_send, &r,
                              resp, sizeof resp, NULL, NULL);
        n = wb_turn_build_channel_bind(req, sizeof req, txid_a, 0x4000, &other);
        wb_turn_server_handle(s, req, n, 1, &observed, on_send, &r,
                              resp, sizeof resp, NULL, NULL);
        CHECK(r.type[r.n - 1] == WB_TURN_CHANNEL_BIND_ERROR,
              "one channel cannot serve two peers");
        wb_turn_server_free(s);
    }

    /* ---- refresh keeps an allocation alive ---- */
    {
        wb_turn_server *s = wb_turn_server_new(&relay_base, 40000, 10);
        resp_sink r = {0};
        uint8_t req[64], resp[128];
        wb_stun_addr observed = a4(203, 0, 113, 1, 50000);

        size_t n = wb_turn_build_allocate(req, sizeof req, txid_a, 1);
        wb_turn_server_handle(s, req, n, 1, &observed, on_send, &r,
                              resp, sizeof resp, NULL, NULL);
        CHECK(wb_turn_server_allocations(s) == 1, "allocated");

        /* a lifetime of 10: at t=5 it is still alive, and a refresh then
         * extends it to t=15 */
        wb_turn_server_tick(s, 5.0);
        CHECK(wb_turn_server_allocations(s) == 1, "alive at t=5");
        n = wb_turn_build_refresh(req, sizeof req, txid_a, 10);
        wb_turn_server_handle(s, req, n, 1, &observed, on_send, &r,
                              resp, sizeof resp, NULL, NULL);
        CHECK(r.type[r.n - 1] == WB_TURN_REFRESH_SUCCESS, "a refresh succeeds");
        wb_turn_server_tick(s, 9.0);
        CHECK(wb_turn_server_allocations(s) == 1, "and the allocation survives to t=14");
        wb_turn_server_tick(s, 5.0);
        CHECK(wb_turn_server_allocations(s) == 0, "and still expires on schedule");

        /* a refresh with no allocation is an allocation-mismatch error */
        n = wb_turn_build_refresh(req, sizeof req, txid_b, 10);
        wb_turn_server_handle(s, req, n, 2, &observed, on_send, &r,
                              resp, sizeof resp, NULL, NULL);
        CHECK(r.type[r.n - 1] == WB_TURN_REFRESH_ERROR, "a stray refresh is refused");
        wb_turn_server_free(s);
    }

    /* ---- malformed and non-TURN input ---- */
    {
        wb_turn_server *s = wb_turn_server_new(&relay_base, 40000, 600);
        resp_sink r = {0};
        uint8_t junk[8] = {1,2,3,4,5,6,7,8};
        wb_stun_addr observed = a4(203, 0, 113, 1, 50000);
        CHECK(wb_turn_server_handle(s, junk, sizeof junk, 1, &observed,
                                    on_send, &r, NULL, 0, NULL, NULL) == 0,
              "garbage is ignored");
        uint8_t shortm[16] = {0};
        CHECK(wb_turn_server_handle(s, shortm, sizeof shortm, 1, &observed,
                                    on_send, &r, NULL, 0, NULL, NULL) == 0,
              "a short message is ignored");
        CHECK(r.n == 0, "and nothing was answered");
        wb_turn_server_free(s);
        CHECK(wb_turn_server_new(NULL, 40000, 600) == NULL, "a server needs a relay address");
    }

    printf("\n=== TURN TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail != 0;
}
