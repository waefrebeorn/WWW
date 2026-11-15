/*
 * wb_net_frag_test.c -- LP-94 gate: fragmentation, reassembly, MTU/PSN.
 *
 * Proves:
 *   - a datagram under the MTU crosses whole, and is byte-identical
 *   - a datagram over the MTU is split, carried under the MTU, and reassembled
 *     byte-identical -- including at the smallest MTU the layer accepts
 *   - the MTU is clamped, and the budget is a budget: a datagram over it is
 *     refused rather than truncated
 *   - a message with a missing fragment is never delivered (loss amplification)
 *   - a duplicate fragment does not double-deliver
 *   - a stale PSN is dropped, an ahead PSN abandons the incomplete group
 *   - the 16-bit PSN wraps: 65535 then 0 both reassemble
 */
#include "wb_net_frag.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

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

static void pump(wb_net_transport *t) {
    for (int i = 0; i < 8; i++) t->tick(t, 0.05);
}

/* Build a datagram of `n` bytes with a recognisable pattern. */
static void fill(wb_net_datagram *d, uint16_t n, uint8_t seed) {
    d->len = n;
    for (uint16_t i = 0; i < n; i++) d->data[i] = (uint8_t)(seed + i * 31u);
}

/* Build one fragment by hand. */
static uint16_t make_frag(uint8_t *out, uint16_t psn, uint16_t idx,
                          uint16_t cnt, const uint8_t *payload, uint16_t n) {
    out[0] = 1;
    wb_net_put_u16(out + 1, psn);
    wb_net_put_u16(out + 3, idx);
    wb_net_put_u16(out + 5, cnt);
    memcpy(out + 7, payload, n);
    return (uint16_t)(7 + n);
}

int main(void) {
    /* ---- the MTU is clamped ---- */
    {
        wb_net_transport *a, *b;
        a = wb_net_transport_pair_new(0.0f, 0.0, &b);
        wb_net_frag *fa = wb_net_frag_new(a, 0);
        CHECK(wb_net_frag_mtu(fa) == 1200, "mtu 0 means the conservative 1200");
        wb_net_frag_free(fa);
        fa = wb_net_frag_new(a, 9999);
        CHECK(wb_net_frag_mtu(fa) == WB_NET_MAX_WIRE,
              "mtu is capped by the datagram budget, not the network's 1472");
        wb_net_frag_free(fa);
        fa = wb_net_frag_new(a, 4);
        CHECK(wb_net_frag_mtu(fa) == 8, "mtu is raised to the smallest usable");
        wb_net_frag_free(fa);
        CHECK(wb_net_frag_new(NULL, 1200) == NULL, "a frag layer needs a transport");
        a->destroy(a);
        b->destroy(b);
    }

    /* ---- under the MTU: whole, and identical ---- */
    {
        wb_net_transport *a, *b;
        a = wb_net_transport_pair_new(0.0f, 0.0, &b);
        wb_net_frag *fa = wb_net_frag_new(a, 1200);
        wb_net_frag *fb = wb_net_frag_new(b, 1200);
        sink s; s.n = 0;

        wb_net_datagram d;
        fill(&d, 100, 7);
        CHECK(wb_net_frag_send(fa, 1, &d) == 0, "a small datagram sends");
        pump(b);
        CHECK(wb_net_frag_poll(fb, on_msg, &s) == 1, "and is delivered once");
        CHECK(s.n == 1 && s.len[0] == 100, "with its length intact");
        CHECK(memcmp(s.data[0], d.data, 100) == 0, "and its bytes intact");

        /* a datagram of exactly the whole-packet budget also crosses whole */
        wb_net_datagram full;
        fill(&full, 1199, 3);
        CHECK(wb_net_frag_send(fa, 1, &full) == 0, "a 1199-byte datagram sends whole");
        pump(b);
        s.n = 0;
        wb_net_frag_poll(fb, on_msg, &s);
        CHECK(s.n == 1 && s.len[0] == 1199, "and arrives at 1199 bytes");
        CHECK(memcmp(s.data[0], full.data, 1199) == 0, "byte-identical");

        /* one byte more than the whole-packet budget must fragment */
        fill(&d, 1200, 5);
        CHECK(wb_net_frag_send(fa, 1, &d) == 0, "a 1200-byte datagram sends");
        pump(b);
        s.n = 0;
        wb_net_frag_poll(fb, on_msg, &s);
        CHECK(s.n == 1 && s.len[0] == 1200, "and reassembles at 1200 bytes");
        CHECK(memcmp(s.data[0], d.data, 1200) == 0, "byte-identical across fragments");

        wb_net_frag_free(fa);
        wb_net_frag_free(fb);
        a->destroy(a);
        b->destroy(b);
    }

    /* ---- the largest datagram the type allows ---- */
    {
        wb_net_transport *a, *b;
        a = wb_net_transport_pair_new(0.0f, 0.0, &b);
        wb_net_frag *fa = wb_net_frag_new(a, 1200);
        wb_net_frag *fb = wb_net_frag_new(b, 1200);
        sink s; s.n = 0;

        wb_net_datagram d;
        fill(&d, WB_NET_MAX_WIRE, 11);
        CHECK(wb_net_frag_send(fa, 1, &d) == 0, "the largest datagram sends");
        pump(b);
        wb_net_frag_poll(fb, on_msg, &s);
        CHECK(s.n == 1 && s.len[0] == WB_NET_MAX_WIRE, "and reassembles whole");
        CHECK(memcmp(s.data[0], d.data, WB_NET_MAX_WIRE) == 0, "byte-identical");

        /* the same idea at the smallest MTU: many fragments, same result.
         * 200 bytes at an 8-byte MTU is 200 fragments, which stays under the
         * reference pair's 256-slot inbound queue -- a larger burst than that
         * is the transport's bound, not this layer's. */
        wb_net_frag_free(fa);
        wb_net_frag_free(fb);
        fa = wb_net_frag_new(a, 8);
        fb = wb_net_frag_new(b, 8);
        s.n = 0;
        wb_net_datagram small;
        fill(&small, 200, 11);
        CHECK(wb_net_frag_send(fa, 1, &small) == 0, "sends at mtu 8");
        pump(b);
        wb_net_frag_poll(fb, on_msg, &s);
        CHECK(s.n == 1 && s.len[0] == 200, "reassembles at mtu 8");
        CHECK(memcmp(s.data[0], small.data, 200) == 0, "byte-identical at mtu 8");

        wb_net_frag_free(fa);
        wb_net_frag_free(fb);
        a->destroy(a);
        b->destroy(b);
    }

    /* ---- the budget is a budget ---- */
    {
        wb_net_transport *a, *b;
        a = wb_net_transport_pair_new(0.0f, 0.0, &b);
        wb_net_frag *fa = wb_net_frag_new(a, 1200);
        wb_net_datagram d;
        d.len = (uint16_t)(WB_NET_MAX_WIRE + 1);
        CHECK(wb_net_frag_send(fa, 1, &d) == -1, "an over-budget datagram is refused");
        CHECK(wb_net_frag_send(fa, -1, &d) == -1, "a bad peer is refused");
        CHECK(wb_net_frag_send(fa, WB_NET_MAX_PEERS, &d) == -1, "peer 64 is refused");
        wb_net_frag_free(fa);
        a->destroy(a);
        b->destroy(b);
    }

    /* ---- loss amplification: a missing fragment loses the whole message ---- */
    {
        wb_net_transport *a, *b;
        /* total loss: nothing gets through, whole or fragmented */
        a = wb_net_transport_pair_new(1.0f, 0.0, &b);
        wb_net_frag *fa = wb_net_frag_new(a, 1200);
        wb_net_frag *fb = wb_net_frag_new(b, 1200);
        sink s; s.n = 0;
        wb_net_datagram d;
        fill(&d, WB_NET_MAX_WIRE, 2);
        wb_net_frag_send(fa, 1, &d);
        pump(b);
        wb_net_frag_poll(fb, on_msg, &s);
        CHECK(s.n == 0, "a total-loss wire delivers nothing");
        wb_net_frag_free(fa);
        wb_net_frag_free(fb);
        a->destroy(a);
        b->destroy(b);

        /* 40% loss: feed the fragments by hand, drop one, show the message is
         * lost -- and that the NEXT message still reassembles. */
        a = wb_net_transport_pair_new(0.0f, 0.0, &b);
        fa = wb_net_frag_new(a, 1200);
        fb = wb_net_frag_new(b, 1200);
        s.n = 0;

        uint8_t payload[1200], wire[1300];
        for (int i = 0; i < 1200; i++) payload[i] = (uint8_t)(i * 7 + 1);

        /* message at PSN 100, two fragments; deliver only the first */
        uint16_t n0 = make_frag(wire, 100, 0, 2, payload, 1193);
        wb_net_datagram out;
        CHECK(wb_net_frag_feed(fb, 0, wire, n0, &out) == 0,
              "one of two fragments completes nothing");
        /* the second fragment is lost: a later, complete message at PSN 101
         * must still arrive, and must not be corrupted by the abandoned group */
        uint16_t m0 = make_frag(wire, 101, 0, 2, payload, 1193);
        CHECK(wb_net_frag_feed(fb, 0, wire, m0, &out) == 0, "first fragment of 101");
        uint16_t m1 = make_frag(wire, 101, 1, 2, payload + 1193, 7);
        CHECK(wb_net_frag_feed(fb, 0, wire, m1, &out) == 1, "second completes 101");
        CHECK(out.len == 1200, "reassembled length");
        CHECK(memcmp(out.data, payload, 1200) == 0, "reassembled bytes");

        wb_net_frag_free(fa);
        wb_net_frag_free(fb);
        a->destroy(a);
        b->destroy(b);
    }

    /* ---- a duplicate fragment does not double-deliver ---- */
    {
        wb_net_transport *a, *b;
        a = wb_net_transport_pair_new(0.0f, 0.0, &b);
        wb_net_frag *fb = wb_net_frag_new(b, 1200);
        uint8_t payload[1216], wire[1300];
        for (int i = 0; i < 1216; i++) payload[i] = (uint8_t)i;
        wb_net_datagram out;
        /* two fragments at the receiver's 1193-byte payload size */
        uint16_t n0 = make_frag(wire, 5, 0, 2, payload, 1193);
        CHECK(wb_net_frag_feed(fb, 0, wire, n0, &out) == 0, "first fragment");
        n0 = make_frag(wire, 5, 0, 2, payload, 1193);
        CHECK(wb_net_frag_feed(fb, 0, wire, n0, &out) == 0, "the same fragment again");
        uint16_t n1 = make_frag(wire, 5, 1, 2, payload + 1193, 23);
        CHECK(wb_net_frag_feed(fb, 0, wire, n1, &out) == 1, "the real second completes");
        CHECK(out.len == 1216, "length is the two distinct fragments");
        n1 = make_frag(wire, 5, 1, 2, payload + 1193, 23);
        CHECK(wb_net_frag_feed(fb, 0, wire, n1, &out) == 0,
              "a duplicate after completion starts nothing");
        wb_net_frag_free(fb);
        a->destroy(a);
        b->destroy(b);
    }

    /* ---- stale and ahead PSNs ---- */
    {
        wb_net_transport *a, *b;
        a = wb_net_transport_pair_new(0.0f, 0.0, &b);
        wb_net_frag *fb = wb_net_frag_new(b, 1200);
        uint8_t payload[16], wire[1300];
        memset(payload, 0x5a, sizeof payload);
        wb_net_datagram out;

        /* start a group at PSN 200 */
        uint16_t n0 = make_frag(wire, 200, 0, 2, payload, 16);
        CHECK(wb_net_frag_feed(fb, 0, wire, n0, &out) == 0, "group 200 starts");
        /* a fragment from 199 is behind: dropped, group 200 survives */
        uint16_t stale = make_frag(wire, 199, 0, 2, payload, 16);
        CHECK(wb_net_frag_feed(fb, 0, wire, stale, &out) == 0, "a stale PSN is dropped");
        uint16_t n1 = make_frag(wire, 200, 1, 2, payload, 16);
        CHECK(wb_net_frag_feed(fb, 0, wire, n1, &out) == 1, "group 200 still completes");

        /* a fragment from 300 is ahead: it abandons the incomplete group 201 */
        uint16_t p201 = make_frag(wire, 201, 0, 2, payload, 16);
        CHECK(wb_net_frag_feed(fb, 0, wire, p201, &out) == 0, "group 201 starts");
        uint16_t p300a = make_frag(wire, 300, 0, 2, payload, 16);
        CHECK(wb_net_frag_feed(fb, 0, wire, p300a, &out) == 0, "group 300 starts");
        uint16_t p300b = make_frag(wire, 300, 1, 2, payload, 16);
        CHECK(wb_net_frag_feed(fb, 0, wire, p300b, &out) == 1, "group 300 completes");
        /* and the abandoned 201 can no longer complete */
        uint16_t p201b = make_frag(wire, 201, 1, 2, payload, 16);
        CHECK(wb_net_frag_feed(fb, 0, wire, p201b, &out) == 0,
              "a fragment of the abandoned group is dropped");

        /* a far-behind PSN (more than half the space) is NOT treated as stale:
         * it is a new generation. This is the wrap case below. */
        wb_net_frag_free(fb);
        a->destroy(a);
        b->destroy(b);
    }

    /* ---- the 16-bit PSN wraps ---- */
    {
        wb_net_transport *a, *b;
        a = wb_net_transport_pair_new(0.0f, 0.0, &b);
        wb_net_frag *fb = wb_net_frag_new(b, 1200);
        uint8_t payload[16], wire[1300];
        memset(payload, 0x11, sizeof payload);
        wb_net_datagram out;

        /* finish a group at 65535 */
        uint16_t n0 = make_frag(wire, 65535, 0, 2, payload, 16);
        CHECK(wb_net_frag_feed(fb, 0, wire, n0, &out) == 0, "fragment at 65535");
        uint16_t n1 = make_frag(wire, 65535, 1, 2, payload, 16);
        CHECK(wb_net_frag_feed(fb, 0, wire, n1, &out) == 1, "completes at 65535");

        /* the next generation is PSN 0: it must not be read as stale */
        uint16_t w0 = make_frag(wire, 0, 0, 2, payload, 16);
        CHECK(wb_net_frag_feed(fb, 0, wire, w0, &out) == 0, "fragment at 0");
        uint16_t w1 = make_frag(wire, 0, 1, 2, payload, 16);
        CHECK(wb_net_frag_feed(fb, 0, wire, w1, &out) == 1,
              "PSN 0 after 65535 is a new generation, not stale");

        /* 65534 is now behind 0, so it is stale */
        uint16_t s0 = make_frag(wire, 65534, 0, 2, payload, 16);
        CHECK(wb_net_frag_feed(fb, 0, wire, s0, &out) == 0,
              "65534 after the wrap is stale");
        wb_net_frag_free(fb);
        a->destroy(a);
        b->destroy(b);
    }

    /* ---- a whole packet is delivered even while a group is half-built ---- */
    {
        wb_net_transport *a, *b;
        a = wb_net_transport_pair_new(0.0f, 0.0, &b);
        wb_net_frag *fb = wb_net_frag_new(b, 1200);
        uint8_t payload[16], wire[1300];
        memset(payload, 0x33, sizeof payload);
        wb_net_datagram out;

        uint16_t n0 = make_frag(wire, 42, 0, 2, payload, 16);
        wb_net_frag_feed(fb, 0, wire, n0, &out);
        uint8_t whole[5] = {0, 'h', 'i', '!', '!'};
        CHECK(wb_net_frag_feed(fb, 0, whole, 5, &out) == 1,
              "a whole packet is delivered immediately");
        CHECK(out.len == 4 && memcmp(out.data, "hi!!", 4) == 0, "with its bytes");
        n0 = make_frag(wire, 42, 0, 2, payload, 16);
        CHECK(wb_net_frag_feed(fb, 0, wire, n0, &out) == 0,
              "a duplicate of the half-built fragment is still dropped");
        wb_net_frag_free(fb);
        a->destroy(a);
        b->destroy(b);
    }

    /* ---- malformed input is refused, not crashed on ---- */
    {
        wb_net_transport *a, *b;
        a = wb_net_transport_pair_new(0.0f, 0.0, &b);
        wb_net_frag *fb = wb_net_frag_new(b, 1200);
        wb_net_datagram out;
        uint8_t junk[8] = {1, 0, 0, 0, 0, 0, 0};           /* frag, short */
        CHECK(wb_net_frag_feed(fb, 0, junk, 0, &out) == 0, "zero length");
        CHECK(wb_net_frag_feed(fb, 0, junk, 1, &out) == 0, "type only");
        CHECK(wb_net_frag_feed(fb, 0, junk, 7, &out) == 0, "header only");
        uint8_t bad[8] = {1, 0, 0, 0, 9, 0, 2, 0};          /* idx 9 of cnt 2 */
        CHECK(wb_net_frag_feed(fb, 0, bad, 8, &out) == 0, "index past the count");
        uint8_t bad2[8] = {1, 0, 0, 0, 0, 0, 0, 0};         /* cnt 0 */
        CHECK(wb_net_frag_feed(fb, 0, bad2, 8, &out) == 0, "zero count");
        uint8_t bad3[8] = {7, 0, 0, 0, 0, 0, 0, 0};         /* unknown type */
        CHECK(wb_net_frag_feed(fb, 0, bad3, 8, &out) == 0, "unknown type");
        uint8_t bad4[1] = {0};                              /* whole, empty */
        CHECK(wb_net_frag_feed(fb, 0, bad4, 1, &out) == 1,
              "an empty whole packet is still a delivery");
        CHECK(out.len == 0, "and is empty");
        CHECK(wb_net_frag_feed(fb, 99, bad4, 1, &out) == 0, "bad peer");
        wb_net_frag_free(fb);
        a->destroy(a);
        b->destroy(b);
    }

    printf("\n=== NET FRAG TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail != 0;
}
