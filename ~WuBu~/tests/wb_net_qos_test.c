/*
 * wb_net_qos_test.c -- LP-97 stone 1 gate: the per-client cap and DSCP marking.
 *
 * Proves:
 *   - the token bucket starts full, refuses past its burst, refills at its rate,
 *     and over a long run never lets the average exceed the cap
 *   - `wait` says how long until a datagram fits, and refuses one that never
 *     will
 *   - the shaper queues what the cap defers and flushes it as tokens arrive
 *   - the shaper refuses a datagram larger than the burst rather than stalling
 *     the peer forever, and counts what it drops
 *   - a rate of 0 means no shaping at all
 *   - the DSCP classes map to EF / AF41 / CS0 / CS1, and the inner-copy mode
 *     carries an encapsulated packet's marking out with it (WireGuard's rule)
 */
#include "wb_net_qos.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)
#define CHECK_NEAR(got, want, tol, msg) do { \
    double _g = (got), _w = (want), _t = (tol); \
    if (isfinite(_g) && fabs(_g - _w) <= _t) g_pass++; \
    else { g_fail++; printf("FAIL: %s: got %.9f want %.9f +/- %.9f (line %d)\n", msg, _g, _w, _t, __LINE__); } \
} while (0)

#define SINK_MAX 64
typedef struct {
    int      n;
    uint16_t len[SINK_MAX];
    uint8_t  data[SINK_MAX][WB_NET_MAX_WIRE];
} sink;

static void on_msg(void *ud, int peer, const wb_net_datagram *d) {
    sink *s = (sink *)ud;
    (void)peer;
    if (s->n >= SINK_MAX) return;
    s->len[s->n] = d->len;
    memcpy(s->data[s->n], d->data, d->len);
    s->n++;
}

static wb_net_datagram mk(uint16_t n, uint8_t seed) {
    wb_net_datagram d;
    d.len = n;
    for (uint16_t i = 0; i < n; i++) d.data[i] = (uint8_t)(seed + i);
    return d;
}

/* A two-peer stub transport, so the shaper can be tested on its own without
 * borrowing the pair's two-endpoint addressing. */
typedef struct {
    wb_net_transport vt;
    int  n;
    int  peer[32];
    uint16_t len[32];
} stub;

static int stub_send(wb_net_transport *t, int peer, const wb_net_datagram *d) {
    stub *s = (stub *)t;
    if (!s || peer < 0 || peer > 1) return -1;
    if (s->n >= 32) return -1;
    s->peer[s->n] = peer;
    s->len[s->n] = d->len;
    s->n++;
    return 0;
}
static int stub_poll(wb_net_transport *t, wb_net_recv_fn cb, void *ud) {
    (void)t; (void)cb; (void)ud;
    return 0;
}
static void stub_tick(wb_net_transport *t, double dt) { (void)t; (void)dt; }
static void stub_destroy(wb_net_transport *t) { (void)t; }

int main(void) {
    /* ---- the bucket ---- */
    {
        wb_net_bucket *b = wb_net_bucket_new(0.0, 0.0);
        CHECK(wb_net_bucket_fits(b, 1 << 20), "rate 0 is unlimited");
        CHECK(wb_net_bucket_take(b, 1 << 20) == 0, "and takes anything");
        CHECK_NEAR(wb_net_bucket_wait(b, 1 << 20), 0.0, 0.0, "and never waits");
        wb_net_bucket_free(b);

        b = wb_net_bucket_new(100.0, 50.0);
        CHECK_NEAR(wb_net_bucket_tokens(b), 50.0, 1e-9, "starts full at the burst");
        CHECK(wb_net_bucket_take(b, 50) == 0, "takes exactly the burst");
        CHECK(wb_net_bucket_take(b, 1) == -1, "then refuses");
        wb_net_bucket_tick(b, 0.08);
        CHECK_NEAR(wb_net_bucket_tokens(b), 8.0, 1e-9, "refills at its rate");
        CHECK(wb_net_bucket_fits(b, 8) && !wb_net_bucket_fits(b, 9),
              "fits is exact at the boundary");
        wb_net_bucket_tick(b, 10.0);
        CHECK_NEAR(wb_net_bucket_tokens(b), 50.0, 1e-9, "and saturates at the burst");
        wb_net_bucket_free(b);

        /* over a long run the average never exceeds the cap */
        b = wb_net_bucket_new(100.0, 100.0);
        double spent = 0.0;
        for (int i = 0; i < 1000; i++) {
            wb_net_bucket_tick(b, 0.05);            /* 5 bytes per step */
            while (wb_net_bucket_fits(b, 10)) {
                wb_net_bucket_take(b, 10);
                spent += 10.0;
            }
        }
        /* 1000 steps of 0.05 s is 50 s; at 100 B/s that is 5000 bytes, and the
         * burst adds at most one more. */
        CHECK(spent <= 5000.0 + 100.0 + 1e-6,
              "a long run never exceeds rate x time + one burst");
        CHECK(spent >= 5000.0 - 100.0, "and gets close to it");
        printf("     (spent %.0f bytes over 50 s at 100 B/s)\n", spent);
        wb_net_bucket_free(b);
    }

    /* ---- wait ---- */
    {
        wb_net_bucket *b = wb_net_bucket_new(50.0, 50.0);
        wb_net_bucket_take(b, 25);
        CHECK_NEAR(wb_net_bucket_wait(b, 25), 0.0, 1e-9, "a fitting send waits 0");
        CHECK_NEAR(wb_net_bucket_wait(b, 50), 0.5, 1e-9,
                   "a 50-byte send waits for 25 bytes at 50 B/s");
        CHECK_NEAR(wb_net_bucket_wait(b, 51), -1.0, 1e-9,
                   "more than the burst never fits");
        wb_net_bucket_free(b);
    }

    /* ---- the classes ---- */
    {
        CHECK(wb_net_qos_class_dscp(WB_NET_QOS_CLASS_INPUT) == 0x2E,
              "a lockstep input is EF");
        CHECK(wb_net_qos_class_dscp(WB_NET_QOS_CLASS_STATE) == 0x22,
              "reliable state is AF41");
        CHECK(wb_net_qos_class_dscp(WB_NET_QOS_CLASS_BULK) == 0x00,
              "ordinary traffic is CS0");
        CHECK(wb_net_qos_class_dscp(WB_NET_QOS_CLASS_BACKGROUND) == 0x08,
              "background is CS1");
        CHECK(wb_net_qos_class_dscp(99) == 0x00, "an unknown class falls back to CS0");
    }

    /* ---- the inner copy ---- */
    {
        /* IPv4: version 4 in the high nibble, DSCP in the top six bits of
         * the second octet. */
        uint8_t cs0[2] = {0x45, 0x00};
        CHECK(wb_net_qos_inner_dscp(cs0, 2, WB_NET_QOS_CLASS_STATE) == 0x00,
              "an IPv4 CS0 packet is read as CS0, not as its class");
        uint8_t ef4[2] = {0x45, 0xB8};         /* 0xB8 >> 2 = 0x2E */
        CHECK(wb_net_qos_inner_dscp(ef4, 2, WB_NET_QOS_CLASS_BULK) == 0x2E,
              "an IPv4 EF packet is carried out as EF");
        /* IPv6: the traffic class straddles the first two octets. */
        uint8_t cs1_6[2] = {0x62, 0x20};       /* (0x2 << 4) | 0x2 = 0x22 */
        CHECK(wb_net_qos_inner_dscp(cs1_6, 2, WB_NET_QOS_CLASS_BULK) == 0x22,
              "an IPv6 AF41 packet is carried out as AF41");
        uint8_t v4[2] = {0x45, 0x00};
        CHECK(wb_net_qos_inner_dscp(v4, 1, WB_NET_QOS_CLASS_INPUT) == 0x2E,
              "too short to read falls back to the class");
        CHECK(wb_net_qos_inner_dscp(NULL, 0, WB_NET_QOS_CLASS_INPUT) == 0x2E,
              "no inner packet falls back to the class");
        uint8_t unk[2] = {0x35, 0x00};         /* version 3: not IP */
        CHECK(wb_net_qos_inner_dscp(unk, 2, WB_NET_QOS_CLASS_STATE) == 0x22,
              "a non-IP payload falls back to the class");
    }

    /* ---- the shaper ---- */
    {
        wb_net_transport *a, *b;
        a = wb_net_transport_pair_new(0.0f, 0.0, &b);
        wb_net_qos *qa = wb_net_qos_new(a, 4096);
        wb_net_qos *qb = wb_net_qos_new(b, 4096);
        CHECK(qa && qb, "a shaper wraps a transport");

        /* rate 0: everything goes straight out */
        wb_net_datagram d = mk(100, 1);
        sink s = {0};
        CHECK(wb_net_qos_send(qa, 1, &d) == 0, "rate 0 sends immediately");
        b->tick(b, 0.0);
        wb_net_qos_poll(qb, on_msg, &s);
        CHECK(s.n == 1 && s.len[0] == 100, "and it arrives");

        /* a real cap: 100 B/s, burst 100. Two 100-byte datagrams: the first
         * goes, the second waits. */
        wb_net_qos_set_rate(qa, 1, 100.0, 100.0);
        CHECK_NEAR(wb_net_qos_rate(qa, 1), 100.0, 1e-9, "the rate is set");
        CHECK_NEAR(wb_net_qos_tokens(qa, 1), 100.0, 1e-9, "and the burst starts full");
        wb_net_datagram one = mk(100, 2);
        wb_net_datagram two = mk(100, 3);
        s.n = 0;
        CHECK(wb_net_qos_send(qa, 1, &one) == 0, "the first datagram sends");
        CHECK(wb_net_qos_send(qa, 1, &two) == 0, "the second queues");
        CHECK(wb_net_qos_queued(qa, 1) == 100, "100 bytes are queued");
        b->tick(b, 0.0);
        wb_net_qos_poll(qb, on_msg, &s);
        CHECK(s.n == 1, "only the first arrived");

        /* after a second the bucket is full again and the queue flushes */
        wb_net_qos_tick(qa, 1.0);
        CHECK(wb_net_qos_queued(qa, 1) == 0, "the queue is empty after a second");
        b->tick(b, 0.0);
        wb_net_qos_poll(qb, on_msg, &s);
        CHECK(s.n == 2 && s.len[1] == 100, "and the queued one arrives");
        CHECK(memcmp(s.data[1], two.data, 100) == 0, "byte-identical");

        /* a datagram larger than the burst is refused, not stalled */
        wb_net_datagram big = mk(200, 4);
        CHECK(wb_net_qos_send(qa, 1, &big) == -1,
              "a datagram over the burst is refused");
        CHECK(wb_net_qos_dropped(qa, 1) == 1, "and counted as dropped");
        CHECK(wb_net_qos_queued(qa, 1) == 0, "and nothing is stuck behind it");
        /* the peer is still usable */
        wb_net_datagram small = mk(50, 5);
        CHECK(wb_net_qos_send(qa, 1, &small) == 0, "a later small one goes");

        /* the queue bound drops the newest */
        wb_net_qos_set_rate(qa, 1, 1.0, 100.0); /* a byte a second: everything queues */
        unsigned long long drops = wb_net_qos_dropped(qa, 1);
        int queued_ok = 1;
        for (int i = 0; i < 40; i++) {
            wb_net_datagram z = mk(100, (uint8_t)i);
            int r = wb_net_qos_send(qa, 1, &z);
            if (r == -1) queued_ok = 0;
        }
        CHECK(queued_ok, "the queue absorbs what fits in its byte budget");
        CHECK(wb_net_qos_dropped(qa, 1) == drops, "nothing is dropped while there is room");
        /* exceed the byte budget */
        for (int i = 0; i < 200; i++) {
            wb_net_datagram z = mk(100, (uint8_t)i);
            wb_net_qos_send(qa, 1, &z);
        }
        CHECK(wb_net_qos_dropped(qa, 1) > drops,
              "past the byte budget the newest is dropped");
        wb_net_qos_set_rate(qa, 1, 0.0, 0.0);   /* unlimited */
        wb_net_qos_tick(qa, 0.0);
        CHECK(wb_net_qos_queued(qa, 1) == 0, "clearing the cap flushes the queue");

        /* DSCP through the shaper */
        wb_net_qos_set_class(qa, 1, WB_NET_QOS_CLASS_INPUT);
        CHECK(wb_net_qos_dscp(qa, 1, NULL, 0) == 0x2E, "class marking");
        uint8_t inner[2] = {0x45, 0x80};        /* 0x80 >> 2 = 0x20 */
        wb_net_qos_set_copy_inner(qa, 1, 1);
        CHECK(wb_net_qos_dscp(qa, 1, inner, 2) == 0x20,
              "copy-from-inner replaces the class");
        wb_net_qos_set_override(qa, 1, 0x3F);
        CHECK(wb_net_qos_dscp(qa, 1, inner, 2) == 0x3F, "an override wins");
        wb_net_qos_set_override(qa, 1, 0x00);
        CHECK(wb_net_qos_dscp(qa, 1, inner, 2) == 0x20, "override 0 turns it off");

        wb_net_qos_free(qa);
        wb_net_qos_free(qb);
        a->destroy(a);
        b->destroy(b);
    }

    /* ---- the queue is FIFO, and a peer's queue is separate ---- */
    {
        stub st;
        memset(&st, 0, sizeof st);
        st.vt.send = stub_send;
        st.vt.poll = stub_poll;
        st.vt.tick = stub_tick;
        st.vt.destroy = stub_destroy;
        wb_net_qos *qa = wb_net_qos_new(&st.vt, 1 << 20);
        wb_net_qos_set_rate(qa, 0, 1.0, 10.0);
        wb_net_qos_set_rate(qa, 1, 1.0, 10.0);
        wb_net_datagram x = mk(10, 10);
        wb_net_datagram y = mk(10, 20);
        wb_net_datagram z = mk(10, 30);
        wb_net_datagram w = mk(10, 40);
        wb_net_qos_send(qa, 1, &x);        /* fits: the burst is exactly 10 */
        wb_net_qos_send(qa, 1, &y);        /* queued */
        wb_net_qos_send(qa, 0, &z);        /* fits */
        wb_net_qos_send(qa, 0, &w);        /* queued */
        CHECK(wb_net_qos_queued(qa, 1) == 10, "peer 1 queues one");
        CHECK(wb_net_qos_queued(qa, 0) == 10, "peer 0 queues one, separately");
        /* flush peer 0 only */
        wb_net_qos_set_rate(qa, 0, 0.0, 0.0);
        wb_net_qos_tick(qa, 0.0);
        CHECK(wb_net_qos_queued(qa, 0) == 0, "peer 0 flushes");
        CHECK(wb_net_qos_queued(qa, 1) == 10, "peer 1 is untouched");
        CHECK(st.n == 3, "three datagrams went out: x, z, then the flushed w");
        CHECK(st.peer[0] == 1 && st.peer[1] == 0 && st.peer[2] == 0,
              "to the right peers");
        wb_net_qos_free(qa);
    }

    printf("\n=== NET QOS TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail != 0;
}
