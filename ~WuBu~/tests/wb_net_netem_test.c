/*
 * wb_net_netem_test.c -- LP-97 stone 2 gate: the link emulator.
 *
 * Proves each knob does the thing it says, and that the whole thing is
 * reproducible: the same seed and the same send sequence give the same arrivals.
 */
#include "wb_net_netem.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

#define SINK_MAX 64
typedef struct {
    int      n;
    int      peer[SINK_MAX];
    uint16_t len[SINK_MAX];
    uint8_t  data[SINK_MAX][WB_NET_MAX_WIRE];
} sink;

static void on_msg(void *ud, int peer, const wb_net_datagram *d) {
    sink *s = (sink *)ud;
    if (s->n >= SINK_MAX) return;
    s->peer[s->n] = peer;
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

/* A one-peer stub so the emulator can be driven without the pair. */
typedef struct { wb_net_transport vt; } stub;
static int stub_send(wb_net_transport *t, int peer, const wb_net_datagram *d) {
    (void)t; (void)peer; (void)d; return 0;
}
static int stub_poll(wb_net_transport *t, wb_net_recv_fn cb, void *ud) {
    (void)t; (void)cb; (void)ud; return 0;
}
static void stub_tick(wb_net_transport *t, double dt) { (void)t; (void)dt; }
static void stub_destroy(wb_net_transport *t) { (void)t; }

int main(void) {
    stub st;
    memset(&st, 0, sizeof st);
    st.vt.send = stub_send;
    st.vt.poll = stub_poll;
    st.vt.tick = stub_tick;
    st.vt.destroy = stub_destroy;

    /* ---- a clean link is immediate ---- */
    {
        wb_net_netem *n = wb_net_netem_new(&st.vt, 1);
        wb_net_datagram d = mk(5, 9);
        sink s = {0};
        wb_net_netem_send(n, 1, &d);
        CHECK(wb_net_netem_poll(n, on_msg, &s) == 1, "a clean link delivers at once");
        CHECK(s.n == 1 && s.len[0] == 5 && memcmp(s.data[0], d.data, 5) == 0,
              "byte-identical");
        CHECK(wb_net_netem_dropped(n) == 0, "nothing dropped");
        wb_net_netem_free(n);
    }

    /* ---- delay: not early, then on time ---- */
    {
        wb_net_netem *n = wb_net_netem_new(&st.vt, 2);
        wb_net_netem_set(n, 0.2, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
        sink s = {0};
        wb_net_datagram d = mk(5, 1);
        wb_net_netem_send(n, 1, &d);
        wb_net_netem_tick(n, 0.19);
        CHECK(wb_net_netem_poll(n, on_msg, &s) == 0, "a packet is not early");
        CHECK(wb_net_netem_pending(n) == 1, "it is still on the link");
        wb_net_netem_tick(n, 0.02);
        CHECK(wb_net_netem_poll(n, on_msg, &s) == 1, "and arrives once the delay passes");
        CHECK(wb_net_netem_pending(n) == 0, "and the link is empty again");
        wb_net_netem_free(n);
    }

    /* ---- jitter: the same seed gives the same arrival times ---- */
    {
        double due[2];
        for (int run = 0; run < 2; run++) {
            wb_net_netem *n = wb_net_netem_new(&st.vt, 77);
            wb_net_netem_set(n, 0.1, 0.05, 0.0, 0.0, 0.0, 0.0, 0.0);
            wb_net_netem_send(n, 1, &((wb_net_datagram){1, {1}}));
            double t = 0.0;
            sink s = {0};
            while (wb_net_netem_pending(n) > 0 && t < 1.0) {
                wb_net_netem_tick(n, 0.005);
                wb_net_netem_poll(n, on_msg, &s);
                t += 0.005;
            }
            due[run] = t;
            wb_net_netem_free(n);
        }
        CHECK(due[0] == due[1], "jitter is a seeded, reproducible draw");
        CHECK(due[0] >= 0.05 && due[0] <= 0.15,
              "and lands inside the delay +/- jitter window");
    }

    /* ---- loss ---- */
    {
        wb_net_netem *n = wb_net_netem_new(&st.vt, 3);
        wb_net_netem_set(n, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0);
        sink s = {0};
        for (int i = 0; i < 8; i++) wb_net_netem_send(n, 1, &((wb_net_datagram){2, {1, 2}}));
        wb_net_netem_tick(n, 0.0);
        CHECK(wb_net_netem_poll(n, on_msg, &s) == 0, "a total-loss link delivers nothing");
        CHECK(wb_net_netem_dropped(n) == 8, "and counts every drop");
        CHECK(wb_net_netem_pending(n) == 0, "nothing is queued");
        wb_net_netem_free(n);

        /* a partial rate drops some and keeps the rest, reproducibly */
        int kept[2];
        for (int run = 0; run < 2; run++) {
            n = wb_net_netem_new(&st.vt, 11);
            wb_net_netem_set(n, 0.0, 0.0, 0.5, 0.0, 0.0, 0.0, 0.0);
            s.n = 0;
            for (int i = 0; i < 20; i++) wb_net_netem_send(n, 1, &((wb_net_datagram){2, {1, 2}}));
            wb_net_netem_tick(n, 0.0);
            wb_net_netem_poll(n, on_msg, &s);
            kept[run] = s.n;
            wb_net_netem_free(n);
        }
        CHECK(kept[0] == kept[1], "a given seed drops the same packets");
        CHECK(kept[0] > 0 && kept[0] < 20, "a 50% link drops some, not all");
        printf("     (a 50%% link delivered %d of 20)\n", kept[0]);
    }

    /* ---- duplication ---- */
    {
        wb_net_netem *n = wb_net_netem_new(&st.vt, 4);
        wb_net_netem_set(n, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0);
        sink s = {0};
        wb_net_datagram d = mk(4, 2);
        wb_net_netem_send(n, 1, &d);
        wb_net_netem_tick(n, 0.0);
        wb_net_netem_poll(n, on_msg, &s);
        CHECK(s.n == 2, "a 100% duplicate link delivers both copies");
        CHECK(s.len[0] == 4 && s.len[1] == 4, "both the same length");
        CHECK(memcmp(s.data[0], d.data, 4) == 0 && memcmp(s.data[1], d.data, 4) == 0,
              "and both intact");
        CHECK(wb_net_netem_duplicated(n) == 1, "and counts the duplicate");
        wb_net_netem_free(n);
    }

    /* ---- corruption ---- */
    {
        wb_net_netem *n = wb_net_netem_new(&st.vt, 5);
        wb_net_netem_set(n, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0);
        sink s = {0};
        wb_net_datagram d = mk(16, 0);
        wb_net_netem_send(n, 1, &d);
        wb_net_netem_tick(n, 0.0);
        wb_net_netem_poll(n, on_msg, &s);
        CHECK(s.n == 1, "a corrupt packet still arrives");
        int diff = 0;
        for (int i = 0; i < 16; i++) if (s.data[0][i] != d.data[i]) diff++;
        CHECK(diff == 1, "with exactly one bit flipped");
        CHECK(wb_net_netem_corrupted(n) == 1, "and counts it");
        wb_net_netem_free(n);
    }

    /* ---- reordering ---- */
    {
        /* seed 2 draws "reorder" for the first packet and not the second */
        wb_net_netem *n = wb_net_netem_new(&st.vt, 2);
        wb_net_netem_set(n, 0.0, 0.0, 0.0, 0.0, 0.0, 0.5, 0.0);
        sink s = {0};
        wb_net_datagram a = mk(1, 1);
        wb_net_datagram b = mk(1, 2);
        wb_net_netem_send(n, 1, &a);
        wb_net_netem_send(n, 1, &b);
        wb_net_netem_tick(n, 0.0005);
        wb_net_netem_poll(n, on_msg, &s);
        CHECK(s.n == 1 && s.data[0][0] == 2,
              "the packet sent second arrives first");
        wb_net_netem_tick(n, 0.001);
        wb_net_netem_poll(n, on_msg, &s);
        CHECK(s.n == 2 && s.data[1][0] == 1, "and the held one follows");
        CHECK(wb_net_netem_reordered(n) == 1, "and counts the reorder");
        wb_net_netem_free(n);
    }

    /* ---- the rate limit spaces packets out ---- */
    {
        wb_net_netem *n = wb_net_netem_new(&st.vt, 8);
        /* 1 kbit/s = 125 B/s. Ten 100-byte packets need 8 kbit = 8 seconds. */
        wb_net_netem_set(n, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0);
        sink s = {0};
        for (int i = 0; i < 10; i++) {
            wb_net_netem_send(n, 1, &((wb_net_datagram){100, {0}}));
            wb_net_netem_tick(n, 0.05);
        }
        wb_net_netem_poll(n, on_msg, &s);
        int early = s.n;
        CHECK(early <= 2, "a 1 kbit/s link carries about 125 B a second");
        while (wb_net_netem_pending(n) > 0) {
            wb_net_netem_tick(n, 0.5);
            wb_net_netem_poll(n, on_msg, &s);
        }
        CHECK(s.n == 10, "and every packet eventually gets through");
        printf("     (10 x 100 B at 1 kbit/s: %d in the first half second)\n", early);
        wb_net_netem_free(n);
    }

    /* ---- the link is bounded, and a full link drops ---- */
    {
        wb_net_netem *n = wb_net_netem_new(&st.vt, 9);
        wb_net_netem_set(n, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
        int refused = 0;
        for (int i = 0; i < 2000; i++) {
            if (wb_net_netem_send(n, 1, &((wb_net_datagram){10, {0}})) != 0) refused++;
        }
        CHECK(refused > 0, "a link with a full queue refuses more");
        CHECK(wb_net_netem_pending(n) <= 1024, "and the queue stays bounded");
        wb_net_netem_free(n);
    }

    /* ---- a two-ended link over the pair ---- */
    {
        wb_net_transport *a, *b;
        a = wb_net_transport_pair_new(0.0f, 0.0, &b);
        wb_net_netem *na = wb_net_netem_new(a, 21);
        wb_net_netem *nb = wb_net_netem_new(b, 22);
        wb_net_netem_set(na, 0.05, 0.0, 0.3, 0.0, 0.0, 0.0, 0.0);
        wb_net_netem_set(nb, 0.05, 0.0, 0.3, 0.0, 0.0, 0.0, 0.0);
        sink s = {0};
        for (int i = 0; i < 40; i++) {
            wb_net_datagram d = mk(8, (uint8_t)i);
            wb_net_netem_send(na, 1, &d);
            wb_net_netem_tick(na, 0.01);
            wb_net_netem_tick(nb, 0.01);
            wb_net_netem_poll(na, on_msg, &s);
            wb_net_netem_poll(nb, on_msg, &s);
        }
        while (wb_net_netem_pending(na) > 0 || wb_net_netem_pending(nb) > 0) {
            wb_net_netem_tick(na, 0.05);
            wb_net_netem_poll(na, on_msg, &s);
        }
        CHECK(s.n > 0, "a lossy emulated link still carries traffic");
        CHECK(s.n < 40, "and loses some of it");
        printf("     (40 packets over a 30%% lossy 50 ms link: %d arrived)\n", s.n);
        wb_net_netem_free(na);
        wb_net_netem_free(nb);
        a->destroy(a);
        b->destroy(b);
    }

    printf("\n=== NET NETEM TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail != 0;
}
