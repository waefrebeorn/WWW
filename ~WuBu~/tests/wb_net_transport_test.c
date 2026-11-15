/*
 * wb_net_transport_test.c -- LP-70 stone 1 gate: the transport and reliability.
 *
 * Proves the plumbing the rest of the netcode is shaped like:
 *   - a transport moves datagrams and nothing else: send returns 0 even when it
 *     drops, delivery is ordered, and the clock only moves when ticked
 *   - REL arrives complete and IN ORDER over a 40% lossy wire, is retransmitted
 *     until acked, and leaves nothing outstanding
 *   - RED arrives over a 60% lossy wire with no retransmit, and the repeats are
 *     deduped so the caller sees each input exactly once
 *   - UNS is not reliability: over a total-loss wire nothing arrives
 *   - a datagram over the wire budget is refused, not silently fragmented
 */
#include "wb_net_transport.h"
#include "wb_net_reliable.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

/* ------------------------------------------------------------------ sinks */

#define SINK_MAX 512

typedef struct {
    int          n;
    wb_net_class cls[SINK_MAX];
    int          peer[SINK_MAX];
    uint16_t     len[SINK_MAX];
    uint8_t      data[SINK_MAX][32];
} sink;

static void on_msg(void *ud, int peer, wb_net_class cls,
                   const void *data, uint16_t len) {
    sink *s = (sink *)ud;
    if (s->n >= SINK_MAX) return;
    s->cls[s->n] = cls;
    s->peer[s->n] = peer;
    s->len[s->n] = len;
    memcpy(s->data[s->n], data, len <= 32 ? len : 32);
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

/* ------------------------------------------------------- the pair alone */

static void test_pair_delivery(void) {
    wb_net_transport *a, *b;
    a = wb_net_transport_pair_new(0.0f, 0.0, &b);
    CHECK(a && b, "a lossless pair opens");

    wb_net_datagram d;
    d.len = 5;
    memcpy(d.data, "hello", 5);
    CHECK(a->send(a, 1, &d) == 0, "the pair accepts a datagram");
    CHECK(b->send(b, 0, &d) == 0, "the far end can send back");

    raw_sink rb = {0}, ra = {0};
    CHECK(b->poll(b, on_raw, &rb) == 1, "the datagram arrives at the far end");
    CHECK(rb.n == 1 && rb.len[0] == 5 && memcmp(rb.data[0], "hello", 5) == 0,
          "the datagram arrives intact");
    CHECK(a->poll(a, on_raw, &ra) == 1, "the reply arrives back");
    CHECK(a->poll(a, on_raw, &ra) == 0, "nothing is delivered twice");

    a->destroy(a);
    b->destroy(b);
}

static void test_pair_loss_and_latency(void) {
    wb_net_transport *a, *b;
    /* Total loss: send must still report success. */
    a = wb_net_transport_pair_new(1.0f, 0.0, &b);
    wb_net_datagram d;
    d.len = 4;
    memcpy(d.data, "lost", 4);
    CHECK(a->send(a, 1, &d) == 0, "a dropped datagram is not an error");
    raw_sink rb = {0};
    b->tick(b, 1.0);
    CHECK(b->poll(b, on_raw, &rb) == 0, "a 100% lossy wire delivers nothing");
    a->destroy(a);
    b->destroy(b);

    /* Latency: the datagram is in flight until the clock passes. */
    a = wb_net_transport_pair_new(0.0f, 0.25, &b);
    d.len = 4;
    memcpy(d.data, "wait", 4);
    a->send(a, 1, &d);
    b->tick(b, 0.1);
    rb = (raw_sink){0};
    CHECK(b->poll(b, on_raw, &rb) == 0, "a datagram is not early");
    b->tick(b, 0.2);
    CHECK(b->poll(b, on_raw, &rb) == 1, "a datagram arrives once the latency passes");
    a->destroy(a);
    b->destroy(b);

    /* Refusals. */
    a = wb_net_transport_pair_new(0.0f, 0.0, &b);
    d.len = 0;
    CHECK(a->send(a, 1, &d) == -1, "an empty datagram is refused");
    d.len = WB_NET_MAX_WIRE + 1;
    CHECK(a->send(a, 1, &d) == -1, "an oversized datagram is refused");
    CHECK(a->send(a, 2, &d) == -1, "an unknown peer is refused");
    CHECK(a->send(a, 0, &d) == -1, "sending to itself is refused");
    a->destroy(a);
    b->destroy(b);
}

/* ------------------------------------------------- REL over a lossy wire */

static void test_reliable_ordered(void) {
    enum { N = 200 };
    wb_net_transport *a, *b;
    a = wb_net_transport_pair_new(0.4f, 0.01, &b);
    wb_net_reliable *ra = wb_net_reliable_new(a, 0.05);
    wb_net_reliable *rb = wb_net_reliable_new(b, 0.05);
    CHECK(ra && rb, "both ends wrap the pair in reliability");

    sink sb = {0};
    for (int i = 0; i < N; i++) {
        char msg[32];
        int n = snprintf(msg, sizeof msg, "m%03d", i);
        CHECK(wb_net_reliable_send(ra, 1, WB_NET_CLASS_REL, msg, (uint16_t)n) == 0,
              "a REL message is accepted");
        for (int s = 0; s < 10; s++) {
            wb_net_reliable_tick(ra, 0.01);
            wb_net_reliable_tick(rb, 0.01);
            wb_net_reliable_poll(rb, on_msg, &sb);
            wb_net_reliable_poll(ra, on_msg, &sb);
        }
    }
    /* Drain: let the last retransmits and acks land. */
    for (int s = 0; s < 800; s++) {
        wb_net_reliable_tick(ra, 0.01);
        wb_net_reliable_tick(rb, 0.01);
        wb_net_reliable_poll(rb, on_msg, &sb);
        wb_net_reliable_poll(ra, on_msg, &sb);
    }

    CHECK(sb.n == N, "every REL message arrives exactly once");
    int in_order = 1;
    for (int i = 0; i < sb.n && i < N; i++) {
        char want[32];
        int n = snprintf(want, sizeof want, "m%03d", i);
        if (sb.cls[i] != WB_NET_CLASS_REL || sb.len[i] != (uint16_t)n ||
            memcmp(sb.data[i], want, (size_t)n) != 0) { in_order = 0; break; }
    }
    CHECK(in_order, "REL messages are delivered in send order");
    CHECK(wb_net_reliable_pending(ra) == 0,
          "every REL message is acked, so nothing is outstanding");
    CHECK(wb_net_reliable_resent(ra) > 0,
          "the lossy wire actually forced retransmission");
    CHECK(wb_net_reliable_delivered(rb) == N, "the receiver counts N deliveries");

    wb_net_reliable_destroy(ra);
    wb_net_reliable_destroy(rb);
    a->destroy(a);
    b->destroy(b);
}

/* ------------------------------------------------ RED over a lossy wire */

static void test_redundant_inputs(void) {
    enum { N = 100 };
    wb_net_transport *a, *b;
    a = wb_net_transport_pair_new(0.4f, 0.01, &b);
    wb_net_reliable *ra = wb_net_reliable_new(a, 0.05);
    wb_net_reliable *rb = wb_net_reliable_new(b, 0.05);

    sink sb = {0};
    for (int i = 0; i < N; i++) {
        char msg[32];
        int n = snprintf(msg, sizeof msg, "i%03d", i);
        wb_net_reliable_send(ra, 1, WB_NET_CLASS_RED, msg, (uint16_t)n);
        for (int s = 0; s < 8; s++) {
            wb_net_reliable_tick(ra, 0.01);
            wb_net_reliable_tick(rb, 0.01);
            wb_net_reliable_poll(rb, on_msg, &sb);
        }
    }
    for (int s = 0; s < 400; s++) {
        wb_net_reliable_tick(ra, 0.01);
        wb_net_reliable_tick(rb, 0.01);
        wb_net_reliable_poll(rb, on_msg, &sb);
    }

    CHECK(sb.n == N, "every input arrives exactly once despite 40% loss");
    int in_order = 1;
    for (int i = 0; i < sb.n && i < N; i++) {
        char want[32];
        int n = snprintf(want, sizeof want, "i%03d", i);
        if (sb.cls[i] != WB_NET_CLASS_RED || sb.len[i] != (uint16_t)n ||
            memcmp(sb.data[i], want, (size_t)n) != 0) { in_order = 0; break; }
    }
    CHECK(in_order, "inputs are delivered in send order");
    CHECK(wb_net_reliable_resent(ra) == 0,
          "redundancy never retransmits -- the repeats ARE the reliability");

    wb_net_reliable_destroy(ra);
    wb_net_reliable_destroy(rb);
    a->destroy(a);
    b->destroy(b);
}

/* -------------------------------------------------- UNS is not reliability */

static void test_unreliable_is_unreliable(void) {
    enum { N = 64 };
    wb_net_transport *a, *b;
    a = wb_net_transport_pair_new(1.0f, 0.0, &b);
    wb_net_reliable *ra = wb_net_reliable_new(a, 0.05);
    wb_net_reliable *rb = wb_net_reliable_new(b, 0.05);

    sink sb = {0};
    for (int i = 0; i < N; i++) {
        wb_net_reliable_send(ra, 1, WB_NET_CLASS_UNS, "x", 1);
        wb_net_reliable_tick(ra, 0.01);
        wb_net_reliable_tick(rb, 0.01);
        wb_net_reliable_poll(rb, on_msg, &sb);
    }
    CHECK(sb.n == 0, "UNS over a total-loss wire delivers nothing");
    CHECK(wb_net_reliable_resent(ra) == 0, "UNS is never retransmitted");
    CHECK(wb_net_reliable_pending(ra) == 0, "UNS leaves nothing outstanding");

    wb_net_reliable_destroy(ra);
    wb_net_reliable_destroy(rb);
    a->destroy(a);
    b->destroy(b);
}

static void test_refusals(void) {
    wb_net_transport *a, *b;
    a = wb_net_transport_pair_new(0.0f, 0.0, &b);
    wb_net_reliable *r = wb_net_reliable_new(a, 0.05);
    CHECK(wb_net_reliable_send(r, 1, WB_NET_CLASS_REL, NULL, 0) == 0,
          "a zero-length REL message is legal");
    CHECK(wb_net_reliable_send(r, 1, WB_NET_CLASS_REL, "x", WB_NET_MAX_PAYLOAD + 1) == -1,
          "a message over the payload budget is refused");
    CHECK(wb_net_reliable_send(r, -1, WB_NET_CLASS_REL, "x", 1) == -1,
          "a bad peer is refused");
    CHECK(wb_net_reliable_send(r, WB_NET_MAX_PEERS, WB_NET_CLASS_REL, "x", 1) == -1,
          "a peer past the table is refused");
    CHECK(wb_net_reliable_new(NULL, 0.05) == NULL, "a layer without a transport is refused");
    CHECK(wb_net_reliable_new(a, 0.0) == NULL, "a zero rto is refused");

    wb_net_reliable_destroy(r);
    a->destroy(a);
    b->destroy(b);
}

int main(void) {
    test_pair_delivery();
    test_pair_loss_and_latency();
    test_reliable_ordered();
    test_redundant_inputs();
    test_unreliable_is_unreliable();
    test_refusals();

    printf("\n=== NET TRANSPORT TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
