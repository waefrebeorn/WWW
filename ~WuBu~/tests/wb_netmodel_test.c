/*
 * wb_netmodel_test.c -- LP-32 stone 1 gate: the deterministic network model.
 *
 * Devil's-advocate cases: a packet is not delivered before its latency elapses,
 * a 100% loss model drops everything, a 0% loss model drops nothing, jitter
 * keeps the delay inside its band, the same seed reproduces the same drops, and
 * degenerate configuration is safe.
 */
#include "wb_netmodel.h"

#include <stdio.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

int main(void) {
    uint32_t seq;

    /* ---- a clean link delivers exactly at the latency ---- */
    wb_netmodel m;
    wb_netmodel_cfg cfg = { 5.0f, 0.0f, 0.0f, 12345u };
    wb_netmodel_init(&m, &cfg);
    CHECK(wb_netmodel_send(&m, 1, 0.0) == 1, "a clean link accepts the packet");
    CHECK(wb_netmodel_pending(&m) == 1, "the packet is in flight");
    CHECK(wb_netmodel_poll(&m, 4.9, &seq) == 0, "nothing is delivered before the latency");
    CHECK(wb_netmodel_poll(&m, 5.0, &seq) == 1 && seq == 1, "the packet is delivered at the latency");
    CHECK(wb_netmodel_pending(&m) == 0, "the queue is drained");

    /* ---- a 100% loss link drops everything ---- */
    wb_netmodel_cfg lossy = { 5.0f, 0.0f, 1.0f, 999u };
    wb_netmodel_init(&m, &lossy);
    int dropped = 0;
    for (int i = 0; i < 20; i++) dropped += (wb_netmodel_send(&m, (uint32_t)i, 0.0) == 0);
    CHECK(dropped == 20, "a 100% loss link drops every packet");
    CHECK(wb_netmodel_pending(&m) == 0, "nothing is in flight");

    /* ---- a 0% loss link drops nothing ---- */
    wb_netmodel_cfg clean = { 1.0f, 0.0f, 0.0f, 7u };
    wb_netmodel_init(&m, &clean);
    int sent = 0;
    for (int i = 0; i < 20; i++) sent += (wb_netmodel_send(&m, (uint32_t)i, 0.0) == 1);
    CHECK(sent == 20, "a 0% loss link drops nothing");

    /* ---- jitter keeps the delay inside its band ---- */
    wb_netmodel_cfg jit = { 10.0f, 3.0f, 0.0f, 4242u };
    wb_netmodel_init(&m, &jit);
    int in_band = 1;
    for (int i = 0; i < 50; i++) {
        wb_netmodel_clear(&m);
        wb_netmodel_send(&m, (uint32_t)i, 0.0);
        /* find the earliest time it can be polled */
        double t = 0.0;
        for (; t <= 15.0; t += 0.25) {
            if (wb_netmodel_poll(&m, t, &seq) == 1) break;
        }
        if (t < 6.5 || t > 13.5) in_band = 0;   /* latency +/- jitter, within polling granularity */
    }
    CHECK(in_band, "jitter keeps every delay inside the band");

    /* ---- reordering: a later packet with a shorter delay arrives first ---- */
    wb_netmodel_clear(&m);
    wb_netmodel_cfg base = { 10.0f, 0.0f, 0.0f, 1u };
    wb_netmodel_init(&m, &base);
    wb_netmodel_send(&m, 100, 0.0);            /* delay 10 */
    wb_netmodel_send(&m, 101, 1.0);            /* delay 10 -> delivered at 11 */
    CHECK(wb_netmodel_poll(&m, 10.5, &seq) == 1 && seq == 100, "the earlier packet is delivered first");
    CHECK(wb_netmodel_poll(&m, 11.0, &seq) == 1 && seq == 101, "the later packet follows");

    /* ---- determinism: the same seed reproduces the same drops ---- */
    wb_netmodel_cfg fl = { 1.0f, 0.0f, 0.3f, 31337u };
    int a[20], b[20];
    for (int pass = 0; pass < 2; pass++) {
        wb_netmodel mm;
        wb_netmodel_init(&mm, &fl);
        for (int i = 0; i < 20; i++) {
            int r = wb_netmodel_send(&mm, (uint32_t)i, 0.0);
            if (pass == 0) a[i] = r; else b[i] = r;
        }
        wb_netmodel_free(&mm);
    }
    int same = 1;
    for (int i = 0; i < 20; i++) same &= (a[i] == b[i]);
    CHECK(same, "the same seed reproduces the same drop pattern");

    /* ---- a different seed gives a different pattern ---- */
    wb_netmodel_cfg fl2 = { 1.0f, 0.0f, 0.3f, 99999u };
    int differs = 0;
    wb_netmodel mm1, mm2;
    wb_netmodel_init(&mm1, &fl);
    wb_netmodel_init(&mm2, &fl2);
    for (int i = 0; i < 20; i++)
        if (wb_netmodel_send(&mm1, (uint32_t)i, 0.0) != wb_netmodel_send(&mm2, (uint32_t)i, 0.0)) differs = 1;
    CHECK(differs, "a different seed gives a different pattern");

    /* ---- clear ---- */
    wb_netmodel_clear(&mm1);
    CHECK(wb_netmodel_pending(&mm1) == 0, "clear drains the queue");

    /* ---- degenerate configuration ---- */
    wb_netmodel_cfg bad = { 0.0f, 0.0f, 0.0f, 0u };
    wb_netmodel_init(&m, &bad);
    CHECK(wb_netmodel_send(&m, 1, 0.0) == 1, "a zero seed falls back to a default");
    wb_netmodel_poll(&m, 0.0, &seq);
    wb_netmodel_init(NULL, &bad);             /* must be a safe no-op */
    CHECK(wb_netmodel_send(NULL, 1, 0.0) == 0, "a NULL model rejects a send");
    CHECK(wb_netmodel_poll(NULL, 0.0, &seq) == 0, "a NULL model rejects a poll");

    wb_netmodel_free(&m);
    wb_netmodel_free(&mm1);
    wb_netmodel_free(&mm2);

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
