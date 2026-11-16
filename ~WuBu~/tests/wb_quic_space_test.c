/*
 * wb_quic_space_test.c -- LP-93 stone 4 gate.
 *
 * The clock is injected, so every loss decision below is exact. The constants
 * are RFC 9002's: a packet three numbers below the largest acknowledged one is
 * lost, and the time threshold is 9/8 of the larger of the smoothed and latest
 * RTT, floored at a millisecond and measured back from now().
 */
#include "wb_quic_space.h"

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

int main(void) {
    /* ---- a fresh space ---- */
    {
        wb_quic_space s;
        wb_quic_space_init(&s, 5000);
        CHECK_EQ("the first packet number is 0", s.next_pn, 0);
        CHECK_EQ("nothing is in flight", s.n_sent, 0);
        CHECK_EQ("no largest acked yet", s.has_largest_acked, 0);
        CHECK_EQ("no RTT yet", s.smoothed_rtt_us, -1);
        uint64_t lt, pns[8];
        CHECK_EQ("and nothing is lost", wb_quic_space_lost(&s, pns, 8, &lt), 0);
        CHECK_EQ("so no loss time", lt, WB_QUIC_NO_LOSS);
        CHECK_EQ("the clock was injected", s.now_us, 5000);
    }

    /* ---- numbers are handed out in order, and flight is recorded ---- */
    {
        wb_quic_space s;
        wb_quic_space_init(&s, 0);
        CHECK_EQ("first send", wb_quic_space_send(&s, 1200, 1, 0), 0);
        CHECK_EQ("second send", wb_quic_space_send(&s, 1200, 1, 0), 1);
        CHECK_EQ("a padding-only packet", wb_quic_space_send(&s, 32, 0, 1), 2);
        CHECK_EQ("an ack-only packet", wb_quic_space_send(&s, 16, 0, 0), 3);
        CHECK_EQ("and the counter is four", s.next_pn, 4);
        CHECK_EQ("the first is ack-eliciting", s.sent[0].ack_eliciting, 1);
        CHECK_EQ("and in flight", s.sent[0].in_flight, 1);
        CHECK_EQ("padding alone is in flight", s.sent[2].in_flight, 1);
        CHECK_EQ("but is not ack-eliciting", s.sent[2].ack_eliciting, 0);
        CHECK_EQ("an ack-only packet is not in flight", s.sent[3].in_flight, 0);
        CHECK_EQ("the sizes are kept", s.sent[0].bytes, 1200);
        CHECK_EQ("with their send time", s.sent[0].sent_us, 0);
    }

    /* ---- RFC 9002 5.3: the first RTT sample seeds both estimators ---- */
    {
        wb_quic_space s;
        wb_quic_space_init(&s, 0);
        wb_quic_space_send(&s, 1000, 1, 0);
        wb_quic_space_tick(&s, 1000);
        CHECK(wb_quic_space_acked(&s, 0) == 0, "the first ack is a sample");
        CHECK_EQ("the smoothed RTT is the sample", s.smoothed_rtt_us, 1000);
        CHECK_EQ("the variance is half of it", s.rttvar_us, 500);
        CHECK_EQ("and the minimum", s.min_rtt_us, 1000);
        CHECK_EQ("the latest is the sample", s.latest_rtt_us, 1000);
        CHECK_EQ("the largest acknowledged is 0", s.largest_acked_pn, 0);
        CHECK_EQ("and it is set", s.has_largest_acked, 1);
        CHECK_EQ("the loss delay is 9/8 of it",
                 wb_quic_space_loss_delay(&s), 1125);
        CHECK_EQ("the packet left flight", s.n_sent, 0);
    }

    /* ---- a later sample moves both, and the minimum ---- */
    {
        wb_quic_space s;
        wb_quic_space_init(&s, 0);
        wb_quic_space_send(&s, 1000, 1, 0);
        wb_quic_space_tick(&s, 1000);
        wb_quic_space_acked(&s, 0);
        wb_quic_space_send(&s, 1000, 1, 0);
        wb_quic_space_tick(&s, 1600);
        CHECK(wb_quic_space_acked(&s, 1) == 0, "a later sample");
        CHECK_EQ("the smoothed RTT is 7/8 of the old plus the new",
                 s.smoothed_rtt_us, (7 * 1000 + 600) / 8);
        CHECK_EQ("the latest RTT is the new sample", s.latest_rtt_us, 600);
        CHECK_EQ("the variance moves by a quarter of the error",
                 s.rttvar_us, (3 * 500 + 400) / 4);
        CHECK_EQ("the minimum falls to the new sample", s.min_rtt_us, 600);
    }

    /* ---- a retransmission's sample is not trusted ---- */
    {
        wb_quic_space s;
        wb_quic_space_init(&s, 0);
        wb_quic_space_send(&s, 1000, 1, 0);   /* pn 0 */
        wb_quic_space_send(&s, 1000, 1, 0);   /* pn 1, a retransmission */
        wb_quic_space_tick(&s, 1000);
        wb_quic_space_acked(&s, 1);
        int64_t sm = s.smoothed_rtt_us;
        CHECK(wb_quic_space_acked(&s, 0) == -1,
              "an ack for the older copy is ignored");
        CHECK_EQ("and does not move the estimator", s.smoothed_rtt_us, sm);
    }

    /* ---- both thresholds together ---- */
    {
        wb_quic_space s;
        wb_quic_space_init(&s, 0);
        wb_quic_space_send(&s, 100, 1, 0);          /* pn 0, at t=0 */
        wb_quic_space_tick(&s, 1000);
        for (int i = 0; i < 10; i++) wb_quic_space_send(&s, 100, 1, 0);
        wb_quic_space_tick(&s, 2000);
        wb_quic_space_acked(&s, 10);
        /* rtt 1000 -> delay 1125 -> anything sent at or before 875 is lost by
         * time, which is pn 0 alone. pn 1..7 are lost by the packet threshold,
         * since 10 >= pn + 3. pn 8 and 9 survive. */
        uint64_t pns[16], lt;
        size_t n = wb_quic_space_lost(&s, pns, 16, &lt);
        CHECK_EQ("eight packets are lost", n, 8);
        CHECK_MEM("0 by time, 1 to 7 by the packet threshold", pns,
                  ((const uint64_t[]){0, 1, 2, 3, 4, 5, 6, 7}), 8);
        CHECK_EQ("and two stay in flight", s.n_sent, 2);
        CHECK_EQ("the next deadline is pn 8's", lt, 1000 + 1125);
    }

    /* ---- the time threshold alone ---- */
    {
        wb_quic_space s;
        wb_quic_space_init(&s, 0);
        wb_quic_space_send(&s, 100, 1, 0);          /* pn 0, at t=0 */
        wb_quic_space_tick(&s, 10000);
        wb_quic_space_send(&s, 100, 1, 0);          /* pn 1 */
        wb_quic_space_acked(&s, 1);
        /* rtt 0, so the granularity floor of 1000 applies; pn 0 was sent
         * 10000 ago and the packet threshold does not reach it. */
        CHECK_EQ("the delay floors at the granularity",
                 wb_quic_space_loss_delay(&s), 1000);
        uint64_t pns[8], lt;
        size_t n = wb_quic_space_lost(&s, pns, 8, &lt);
        CHECK_EQ("the old packet is lost on time", n, 1);
        CHECK_EQ("and it is packet 0", pns[0], 0);
    }

    /* ---- recent packets are not lost, and the deadline is theirs ---- */
    {
        wb_quic_space s;
        wb_quic_space_init(&s, 1000);
        wb_quic_space_send(&s, 100, 1, 0);          /* pn 0 */
        wb_quic_space_send(&s, 100, 1, 0);          /* pn 1 */
        wb_quic_space_acked(&s, 1);
        uint64_t pns[8], lt;
        CHECK_EQ("nothing is lost", wb_quic_space_lost(&s, pns, 8, &lt), 0);
        CHECK_EQ("and the deadline is pn 0's", lt, 1000 + 1000);
    }

    /* ---- a padding-only packet is in flight and can be lost ---- */
    {
        wb_quic_space s;
        wb_quic_space_init(&s, 0);
        wb_quic_space_send(&s, 1200, 0, 1);         /* pn 0, PADDING only */
        wb_quic_space_send(&s, 100, 1, 0);          /* pn 1 */
        wb_quic_space_tick(&s, 5000);
        wb_quic_space_acked(&s, 1);
        uint64_t pns[8], lt;
        size_t n = wb_quic_space_lost(&s, pns, 8, &lt);
        CHECK_EQ("the padding packet is lost", n, 1);
        CHECK_EQ("and it is packet 0", pns[0], 0);
    }

    /* ---- nothing is lost before any acknowledgement ---- */
    {
        wb_quic_space s;
        wb_quic_space_init(&s, 0);
        for (int i = 0; i < 10; i++) wb_quic_space_send(&s, 100, 1, 0);
        wb_quic_space_tick(&s, 100000);
        uint64_t pns[16], lt;
        CHECK_EQ("no ack, no loss", wb_quic_space_lost(&s, pns, 16, &lt), 0);
        CHECK_EQ("and no deadline", lt, WB_QUIC_NO_LOSS);
        CHECK_EQ("all ten remain", s.n_sent, 10);
    }

    /* ---- an ack for a packet already gone is ignored ---- */
    {
        wb_quic_space s;
        wb_quic_space_init(&s, 0);
        wb_quic_space_send(&s, 100, 1, 0);
        wb_quic_space_tick(&s, 1000);
        CHECK(wb_quic_space_acked(&s, 0) == 0, "the first ack lands");
        CHECK(wb_quic_space_acked(&s, 0) == -1, "the second does not");
        CHECK(wb_quic_space_acked(&s, 7) == -1, "nor an unknown number");
        CHECK_EQ("and the estimator is untouched", s.smoothed_rtt_us, 1000);
    }

    /* ---- packets above the largest acked are never candidates ---- */
    {
        wb_quic_space s;
        wb_quic_space_init(&s, 0);
        wb_quic_space_send(&s, 100, 1, 0);
        wb_quic_space_send(&s, 100, 1, 0);
        wb_quic_space_acked(&s, 0);                 /* ack the earlier one */
        wb_quic_space_tick(&s, 999999);
        uint64_t pns[8], lt;
        CHECK_EQ("a later packet is not lost", wb_quic_space_lost(&s, pns, 8, &lt), 0);
    }

    /* ---- the probe timeout ---- */
    {
        wb_quic_space s;
        wb_quic_space_init(&s, 0);
        wb_quic_space_send(&s, 1000, 1, 0);
        wb_quic_space_tick(&s, 1000);
        wb_quic_space_acked(&s, 0);
        /* smoothed 1000, rttvar 500 -> 1000 + max(2000, 1000) = 3000 */
        CHECK_EQ("the base PTO", wb_quic_space_pto(&s, 0, 1, 0), 3000);
        CHECK_EQ("with a peer ack delay once confirmed",
                 wb_quic_space_pto(&s, 50, 1, 0), 3050);
        CHECK_EQ("and none before it", wb_quic_space_pto(&s, 50, 0, 0), 3000);
        CHECK_EQ("doubled by the backoff", wb_quic_space_pto(&s, 0, 1, 1), 6000);
        CHECK_EQ("and again", wb_quic_space_pto(&s, 0, 1, 2), 12000);
        CHECK_EQ("the backoff multiplies only the RTT term",
                 wb_quic_space_pto(&s, 50, 1, 1), 6050);
    }

    /* ---- the packet number ceiling ---- */
    {
        wb_quic_space s;
        wb_quic_space_init(&s, 0);
        CHECK(!wb_quic_space_pn_exhausted(&s), "a fresh space has numbers left");
        s.next_pn = WB_QUIC_PN_MAX;
        CHECK(!wb_quic_space_pn_exhausted(&s),
              "the last number is still usable");
        CHECK_EQ("and one packet takes it", wb_quic_space_send(&s, 10, 1, 0),
                 WB_QUIC_PN_MAX);
        CHECK(wb_quic_space_pn_exhausted(&s),
              "then the space is exhausted");
        CHECK(wb_quic_space_pn_exhausted(NULL) == 0, "and NULL is safe");
    }

    printf("\n=== QUIC SPACE TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail != 0;
}
