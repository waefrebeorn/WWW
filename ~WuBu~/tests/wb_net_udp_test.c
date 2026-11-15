/*
 * wb_net_udp_test.c -- LP-70 stone 2 gate: the UDP socket transport.
 *
 * Proves the socket endpoint is the same transport the pair is:
 *   - it binds a real port and addresses a peer by name
 *   - a datagram sent to a peer arrives, intact, at that peer
 *   - a datagram from an UNADDRESSED source is dropped (addressing is
 *     explicit, so an unnamed peer cannot inject traffic)
 *   - the wire budget is enforced at the socket, not silently truncated
 *   - `wb_net_reliable` runs over it unchanged: REL complete and ordered
 */
#include "wb_net_udp.h"
#include "wb_net_reliable.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

#define SINK_MAX 512
typedef struct {
    int          n;
    wb_net_class cls[SINK_MAX];
    int          peer[SINK_MAX];
    uint16_t     len[SINK_MAX];
    uint8_t      data[SINK_MAX][64];
} sink;

static void on_msg(void *ud, int peer, wb_net_class cls,
                   const void *data, uint16_t len) {
    sink *s = (sink *)ud;
    if (s->n >= SINK_MAX) return;
    s->cls[s->n] = cls;
    s->peer[s->n] = peer;
    s->len[s->n] = len;
    memcpy(s->data[s->n], data, len <= 64 ? len : 64);
    s->n++;
}

#define RAW_MAX 64
typedef struct {
    int      n;
    uint16_t len[RAW_MAX];
    uint8_t  data[RAW_MAX][WB_NET_MAX_WIRE];
} raw_sink;

static void on_raw(void *ud, int peer, const wb_net_datagram *d) {
    (void)peer;
    raw_sink *rs = (raw_sink *)ud;
    if (rs->n >= RAW_MAX) return;
    rs->len[rs->n] = d->len;
    memcpy(rs->data[rs->n], d->data, d->len);
    rs->n++;
}

/* Poll until something arrives or the tries run out. */
static int wait_raw(wb_net_transport *t, raw_sink *rs, int tries) {
    for (int i = 0; i < tries; i++) {
        if (t->poll(t, on_raw, rs) > 0) return 1;
    }
    return 0;
}

static void test_endpoint_binds(void) {
    char err[128] = {0};
    wb_net_udp *a = wb_net_udp_new(0, err, sizeof err);
    CHECK(a != NULL, "a UDP endpoint opens");
    if (!a) { printf("reason: %s\n", err); return; }
    CHECK(wb_net_udp_bound_port(a) != 0, "an ephemeral port is reported");

    wb_net_udp *b = wb_net_udp_new(0, err, sizeof err);
    CHECK(b != NULL, "a second endpoint opens");
    if (!b) { wb_net_udp_destroy(a); return; }
    CHECK(wb_net_udp_bound_port(b) != 0, "the second endpoint reports its port");
    CHECK(wb_net_udp_bound_port(a) != wb_net_udp_bound_port(b),
          "the two endpoints got different ports");

    /* Refusals. */
    CHECK(wb_net_udp_add_peer(a, 1, "127.0.0.1", 0) == -1,
          "a zero peer port is refused");
    CHECK(wb_net_udp_add_peer(a, -1, "127.0.0.1", 1234) == -1,
          "a bad peer id is refused");
    CHECK(wb_net_udp_add_peer(a, WB_NET_MAX_PEERS, "127.0.0.1", 1234) == -1,
          "a peer id past the table is refused");
    CHECK(wb_net_udp_add_peer(a, 1, "no.such.host.invalid", 1234) == -1,
          "an unresolvable host is refused");

    CHECK(wb_net_udp_add_peer(a, 1, "127.0.0.1", wb_net_udp_bound_port(b)) == 0,
          "a peer is addressed by name and port");
    CHECK(wb_net_udp_add_peer(b, 1, "127.0.0.1", wb_net_udp_bound_port(a)) == 0,
          "the far end addresses back");

    wb_net_udp_destroy(a);
    wb_net_udp_destroy(b);
}

static void test_datagram_delivery(void) {
    char err[128] = {0};
    wb_net_udp *a = wb_net_udp_new(0, err, sizeof err);
    wb_net_udp *b = wb_net_udp_new(0, err, sizeof err);
    if (!a || !b) { CHECK(0, "the endpoints open"); wb_net_udp_destroy(a); wb_net_udp_destroy(b); return; }
    wb_net_udp_add_peer(a, 1, "127.0.0.1", wb_net_udp_bound_port(b));
    wb_net_udp_add_peer(b, 1, "127.0.0.1", wb_net_udp_bound_port(a));

    wb_net_transport *ta = (wb_net_transport *)a;
    wb_net_transport *tb = (wb_net_transport *)b;

    wb_net_datagram d;
    d.len = 5;
    memcpy(d.data, "hello", 5);
    CHECK(ta->send(ta, 1, &d) == 0, "a datagram is sent");

    raw_sink rb = {0};
    CHECK(wait_raw(tb, &rb, 50), "the datagram arrives");
    CHECK(rb.n == 1 && rb.len[0] == 5 && memcmp(rb.data[0], "hello", 5) == 0,
          "the datagram arrives intact");

    /* A full-size datagram: the wire budget is enforced, not truncated. */
    wb_net_datagram big;
    big.len = WB_NET_MAX_WIRE;
    memset(big.data, 0xA5, sizeof big.data);
    CHECK(ta->send(ta, 1, &big) == 0, "a full-size datagram is sent");
    rb = (raw_sink){0};
    CHECK(wait_raw((wb_net_transport *)b, &rb, 50), "the full-size datagram arrives");
    CHECK(rb.n == 1 && rb.len[0] == WB_NET_MAX_WIRE, "the full-size datagram is not truncated");
    int same = 1;
    for (int i = 0; i < WB_NET_MAX_WIRE; i++) if (rb.data[0][i] != (uint8_t)0xA5) { same = 0; break; }
    CHECK(same, "the full-size payload is byte-identical");

    /* Refusals. */
    wb_net_datagram bad;
    bad.len = WB_NET_MAX_WIRE + 1;
    CHECK(ta->send(ta, 1, &bad) == -1,
          "an oversized datagram is refused");
    bad.len = 1;
    CHECK(ta->send(ta, 7, &bad) == -1,
          "an unaddressed peer is refused");

    /* An unaddressed source is dropped, not delivered. */
    char err2[128] = {0};
    wb_net_udp *c = wb_net_udp_new(0, err2, sizeof err2);
    if (c) {
        wb_net_udp_add_peer(c, 1, "127.0.0.1", wb_net_udp_bound_port(b));
        wb_net_datagram x;
        x.len = 3;
        memcpy(x.data, "hey", 3);
        ((wb_net_transport *)c)->send((wb_net_transport *)c, 1, &x);
        raw_sink rbb = {0};
        int got = tb->poll(tb, on_raw, &rbb);
        CHECK(got == 0, "a datagram from an unaddressed source is dropped");
    }

    wb_net_udp_destroy(a);
    wb_net_udp_destroy(b);
}

static void test_reliable_over_udp(void) {
    enum { N = 200 };
    char err[128] = {0};
    wb_net_udp *a = wb_net_udp_new(0, err, sizeof err);
    wb_net_udp *b = wb_net_udp_new(0, err, sizeof err);
    if (!a || !b) { CHECK(0, "the endpoints open"); wb_net_udp_destroy(a); wb_net_udp_destroy(b); return; }
    wb_net_udp_add_peer(a, 1, "127.0.0.1", wb_net_udp_bound_port(b));
    wb_net_udp_add_peer(b, 1, "127.0.0.1", wb_net_udp_bound_port(a));

    wb_net_reliable *ra = wb_net_reliable_new((wb_net_transport *)a, 0.05);
    wb_net_reliable *rb = wb_net_reliable_new((wb_net_transport *)b, 0.05);

    sink sb = {0};
    for (int i = 0; i < N; i++) {
        char msg[32];
        int n = snprintf(msg, sizeof msg, "u%03d", i);
        wb_net_reliable_send(ra, 1, WB_NET_CLASS_REL, msg, (uint16_t)n);
        for (int s = 0; s < 6; s++) {
            wb_net_reliable_tick(ra, 0.01);
            wb_net_reliable_tick(rb, 0.01);
            wb_net_reliable_poll(rb, on_msg, &sb);
            wb_net_reliable_poll(ra, on_msg, &sb);
        }
    }
    for (int s = 0; s < 600; s++) {
        wb_net_reliable_tick(ra, 0.01);
        wb_net_reliable_tick(rb, 0.01);
        wb_net_reliable_poll(rb, on_msg, &sb);
        wb_net_reliable_poll(ra, on_msg, &sb);
    }

    CHECK(sb.n == N, "every REL message crosses the socket exactly once");
    int in_order = 1;
    for (int i = 0; i < sb.n && i < N; i++) {
        char want[32];
        int n = snprintf(want, sizeof want, "u%03d", i);
        if (sb.len[i] != (uint16_t)n || memcmp(sb.data[i], want, (size_t)n) != 0) {
            in_order = 0; break;
        }
    }
    CHECK(in_order, "REL messages arrive in send order over UDP");
    CHECK(wb_net_reliable_pending(ra) == 0, "nothing is left outstanding");

    wb_net_reliable_destroy(ra);
    wb_net_reliable_destroy(rb);
    wb_net_udp_destroy(a);
    wb_net_udp_destroy(b);
}

int main(void) {
    test_endpoint_binds();
    test_datagram_delivery();
    test_reliable_over_udp();

    printf("\n=== NET UDP TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
