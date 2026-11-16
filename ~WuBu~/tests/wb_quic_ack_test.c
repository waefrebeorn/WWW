/*
 * wb_quic_ack_test.c -- LP-93 stone 5 gate.
 *
 * The rules are RFC 9000's: acknowledge immediately on an out-of-order or
 * gapped ack-eliciting packet, and on a congestion-marked one; otherwise after
 * two ack-eliciting packets or the peer's max_ack_delay, whichever is first;
 * and never answer a non-ack-eliciting packet with a non-ack-eliciting one.
 */
#include "wb_quic_ack.h"
#include "wb_quic.h"

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

#define DELAY 25000

int main(void) {
    /* ---- a fresh tracker ---- */
    {
        wb_quic_ack_track t;
        wb_quic_ack_track_init(&t, 0);
        CHECK_EQ("nothing tracked", wb_quic_ack_track_count(&t), 0);
        CHECK_EQ("nothing to send", wb_quic_ack_track_should_send(&t, DELAY, 0), 0);
    }

    /* ---- one ack-eliciting packet is not enough ---- */
    {
        wb_quic_ack_track t;
        wb_quic_ack_track_init(&t, 0);
        wb_quic_ack_track_note(&t, 0, 1, 0, 0);
        CHECK_EQ("one is tracked", wb_quic_ack_track_count(&t), 1);
        CHECK_EQ("but does not ask for an ACK yet",
                 wb_quic_ack_track_should_send(&t, DELAY, 0), 0);
        wb_quic_ack_track_note(&t, 1, 1, 0, 0);
        CHECK_EQ("the second does",
                 wb_quic_ack_track_should_send(&t, DELAY, 0), 1);
        wb_quic_ack_track_sent(&t);
        CHECK_EQ("and sending clears it",
                 wb_quic_ack_track_should_send(&t, DELAY, 0), 0);
        CHECK_EQ("and empties the set", wb_quic_ack_track_count(&t), 0);
    }

    /* ---- out of order, and a gap, are immediate ---- */
    {
        wb_quic_ack_track t;
        wb_quic_ack_track_init(&t, 0);
        wb_quic_ack_track_note(&t, 5, 1, 0, 0);
        CHECK_EQ("a lone packet waits", wb_quic_ack_track_should_send(&t, DELAY, 0), 0);
        wb_quic_ack_track_note(&t, 3, 1, 0, 0);
        CHECK_EQ("a lower number is out of order",
                 wb_quic_ack_track_should_send(&t, DELAY, 0), 1);

        wb_quic_ack_track_init(&t, 0);
        wb_quic_ack_track_note(&t, 3, 1, 0, 0);
        CHECK_EQ("the first waits", wb_quic_ack_track_should_send(&t, DELAY, 0), 0);
        wb_quic_ack_track_note(&t, 5, 1, 0, 0);
        CHECK_EQ("a gap below it is immediate",
                 wb_quic_ack_track_should_send(&t, DELAY, 0), 1);
    }

    /* ---- a congestion-marked packet is immediate ---- */
    {
        wb_quic_ack_track t;
        wb_quic_ack_track_init(&t, 0);
        wb_quic_ack_track_note(&t, 0, 1, 1, 0);
        CHECK_EQ("an ECN-CE packet is acknowledged at once",
                 wb_quic_ack_track_should_send(&t, DELAY, 0), 1);
    }

    /* ---- a non-ack-eliciting packet cannot ask for an ACK ---- */
    {
        wb_quic_ack_track t;
        wb_quic_ack_track_init(&t, 0);
        wb_quic_ack_track_note(&t, 1, 0, 0, 0);
        wb_quic_ack_track_note(&t, 2, 0, 0, 100000);
        CHECK_EQ("only ACK or PADDING to acknowledge",
                 wb_quic_ack_track_should_send(&t, DELAY, 100000), 0);
        CHECK_EQ("but they are tracked for later",
                 wb_quic_ack_track_count(&t), 2);
    }

    /* ---- the peer's max_ack_delay ---- */
    {
        wb_quic_ack_track t;
        wb_quic_ack_track_init(&t, 0);
        wb_quic_ack_track_note(&t, 0, 1, 0, 0);
        CHECK_EQ("not before the delay",
                 wb_quic_ack_track_should_send(&t, DELAY, DELAY - 1), 0);
        CHECK_EQ("and at it", wb_quic_ack_track_should_send(&t, DELAY, DELAY), 1);
    }

    /* ---- duplicates are not counted twice ---- */
    {
        wb_quic_ack_track t;
        wb_quic_ack_track_init(&t, 0);
        wb_quic_ack_track_note(&t, 0, 1, 0, 0);
        wb_quic_ack_track_note(&t, 0, 1, 0, 0);
        CHECK_EQ("a duplicate is stored once", wb_quic_ack_track_count(&t), 1);
        CHECK_EQ("and counted once", wb_quic_ack_track_should_send(&t, DELAY, 0), 0);
    }

    /* ---- the frame carries the tracked set ---- */
    {
        wb_quic_ack_track t;
        wb_quic_ack_track_init(&t, 0);
        wb_quic_ack_track_note(&t, 1, 1, 0, 0);
        wb_quic_ack_track_note(&t, 2, 1, 0, 0);
        wb_quic_ack_track_note(&t, 3, 1, 0, 0);
        uint8_t buf[64];
        size_t n = wb_quic_ack_track_frame(&t, 3, 0, buf, sizeof buf);
        CHECK(n > 0, "a frame is built");
        CHECK_EQ("its type is ACK", buf[0], 0x02);
        wb_quic_ack a;
        CHECK(wb_quic_parse_ack(buf + 1, n - 1, WB_QUIC_FRAME_ACK, &a) == 0,
              "and it parses");
        CHECK_EQ("the largest is 3", a.largest, 3);
        CHECK_EQ("and it covers three packets", wb_quic_ack_count(&a), 3);
        uint64_t got[8];
        CHECK_EQ("which expand to three", wb_quic_ack_expand(&a, got, 8), 3);
        CHECK_MEM("1, 2 and 3", got, ((const uint64_t[]){3, 2, 1}),
                  3 * (int)sizeof(uint64_t));

        /* a largest that is not the newest tracked is refused */
        CHECK(wb_quic_ack_track_frame(&t, 2, 0, buf, sizeof buf) == 0,
              "a stale largest is refused");
    }

    /* ---- a frame that does not fit drops the oldest ranges ---- */
    {
        wb_quic_ack_track t;
        wb_quic_ack_track_init(&t, 0);
        wb_quic_ack_track_note(&t, 1, 1, 0, 0);
        wb_quic_ack_track_note(&t, 2, 1, 0, 0);
        wb_quic_ack_track_note(&t, 3, 1, 0, 0);
        wb_quic_ack_track_note(&t, 7, 1, 0, 0);
        wb_quic_ack_track_note(&t, 8, 1, 0, 0);
        wb_quic_ack_track_note(&t, 9, 1, 0, 0);
        uint8_t buf[64];
        /* The whole set needs a type byte plus six varints. */
        size_t full = wb_quic_ack_track_frame(&t, 9, 0, buf, 7);
        CHECK(full == 7, "the whole set fits in seven bytes");
        wb_quic_ack a;
        CHECK(wb_quic_parse_ack(buf + 1, full - 1, WB_QUIC_FRAME_ACK, &a) == 0,
              "and parses");
        CHECK_EQ("covering all six", wb_quic_ack_count(&a), 6);
        /* One byte short: the oldest three are dropped and the newest three
         * remain, because section 13.2.3 says the most recently received
         * packets are always included. */
        size_t n = wb_quic_ack_track_frame(&t, 9, 0, buf, 6);
        CHECK(n > 0, "a smaller frame is still built");
        CHECK(wb_quic_parse_ack(buf + 1, n - 1, WB_QUIC_FRAME_ACK, &a) == 0,
              "and parses");
        CHECK_EQ("and it keeps the newest three", wb_quic_ack_count(&a), 3);
    }

    /* ---- the tracked set is bounded, keeping the newest ---- */
    {
        wb_quic_ack_track t;
        wb_quic_ack_track_init(&t, 0);
        for (uint64_t pn = 0; pn < WB_QUIC_ACK_MAX + 20; pn++)
            wb_quic_ack_track_note(&t, pn, 1, 0, 0);
        CHECK_EQ("the set is bounded", wb_quic_ack_track_count(&t),
                 WB_QUIC_ACK_MAX);
        uint8_t buf[512];
        size_t n = wb_quic_ack_track_frame(&t, WB_QUIC_ACK_MAX + 19, 0,
                                           buf, sizeof buf);
        CHECK(n > 0, "and a frame still builds");
        wb_quic_ack a;
        CHECK(wb_quic_parse_ack(buf + 1, n - 1, WB_QUIC_FRAME_ACK, &a) == 0,
              "and parses");
        CHECK_EQ("with the newest largest", a.largest, WB_QUIC_ACK_MAX + 19);
    }


    /* ---- applying an ACK to a packet number space (stone 10) ---- */
    {
        wb_quic_space sp;
        wb_quic_space_init(&sp, 1000);
        for (int i = 0; i < 5; i++)
            wb_quic_space_send(&sp, 100, 1, 0);
        CHECK_EQ("five packets in flight", sp.n_sent, 5);

        /* an ACK covering only the largest */
        uint8_t body[32];
        const uint64_t one[1] = {4};
        size_t bl = wb_quic_frame_ack(body, sizeof body, 0, 4, 0, one, 1, 0, 0, 0);
        wb_quic_ack a;
        CHECK(wb_quic_parse_ack(body + 1, bl - 1, WB_QUIC_FRAME_ACK, &a) == 0,
              "the ACK parses");
        int sampled = 0;
        CHECK_EQ("one packet acknowledged", wb_quic_ack_apply(&a, &sp, &sampled), 1);
        CHECK(sampled, "and it took an RTT sample");
        CHECK_EQ("so four remain in flight", sp.n_sent, 4);
        int has = 0;
        CHECK_EQ("the largest acknowledged is 4",
                 wb_quic_space_largest_acked(&sp, &has), 4);
        CHECK(has, "and there is one");

        /* RFC 9002 section 6.1: with the largest at 4, packets at or below
         * 4 - 3 are lost by the packet threshold */
        uint64_t lost[16];
        uint64_t lt = 0;
        size_t nl = wb_quic_space_lost(&sp, lost, 16, &lt);
        CHECK_EQ("two are declared lost", nl, 2);
        CHECK_EQ("the oldest first", lost[0], 0);
        CHECK_EQ("then the next", lost[1], 1);
        CHECK_EQ("and only two remain in flight", sp.n_sent, 2);

        /* acknowledging a lower packet is not a new largest, so it must not
         * take a sample -- that is what rules out retransmission ambiguity */
        uint8_t b2[32];
        const uint64_t two[1] = {2};
        size_t bl2 = wb_quic_frame_ack(b2, sizeof b2, 0, 2, 0, two, 1, 0, 0, 0);
        wb_quic_ack a2;
        CHECK(wb_quic_parse_ack(b2 + 1, bl2 - 1, WB_QUIC_FRAME_ACK, &a2) == 0,
              "a lower ACK parses");
        sampled = 0;
        CHECK_EQ("it acknowledges what is in flight",
                 wb_quic_ack_apply(&a2, &sp, &sampled), 1);
        CHECK(!sampled, "but takes no RTT sample");
        CHECK_EQ("and the largest does not move",
                 wb_quic_space_largest_acked(&sp, &has), 4);

        /* the same ACK twice: the second is a no-op */
        sampled = 0;
        CHECK_EQ("a repeated ACK acknowledges nothing",
                 wb_quic_ack_apply(&a2, &sp, &sampled), 0);
        CHECK(!sampled, "and samples nothing");

        /* an ACK for a packet that was never sent is not an error */
        uint8_t b3[32];
        const uint64_t far[1] = {900};
        size_t bl3 = wb_quic_frame_ack(b3, sizeof b3, 0, 900, 0, far, 1, 0, 0, 0);
        wb_quic_ack a3;
        CHECK(wb_quic_parse_ack(b3 + 1, bl3 - 1, WB_QUIC_FRAME_ACK, &a3) == 0,
              "an unknown ACK parses");
        CHECK_EQ("and acknowledges nothing", wb_quic_ack_apply(&a3, &sp, NULL), 0);

        /* a range that covers several packets at once */
        wb_quic_space s2;
        wb_quic_space_init(&s2, 1000);
        for (int i = 0; i < 8; i++)
            wb_quic_space_send(&s2, 100, 1, 0);
        uint8_t b4[32];
        const uint64_t four[4] = {4, 5, 6, 7};
        size_t bl4 = wb_quic_frame_ack(b4, sizeof b4, 0, 7, 0, four, 4, 0, 0, 0);
        wb_quic_ack a4;
        CHECK(wb_quic_parse_ack(b4 + 1, bl4 - 1, WB_QUIC_FRAME_ACK, &a4) == 0,
              "a ranged ACK parses");
        CHECK_EQ("it covers four packets", wb_quic_ack_count(&a4), 4);
        sampled = 0;
        CHECK_EQ("and acknowledges all four",
                 wb_quic_ack_apply(&a4, &s2, &sampled), 4);
        CHECK(sampled, "with one sample");

        /* a non-ack-eliciting packet is acknowledged but never samples */
        wb_quic_space s3;
        wb_quic_space_init(&s3, 1000);
        wb_quic_space_send(&s3, 100, 0, 1);   /* padding only: in flight, not
                                                ack-eliciting */
        uint8_t b5[32];
        const uint64_t zero[1] = {0};
        size_t bl5 = wb_quic_frame_ack(b5, sizeof b5, 0, 0, 0, zero, 1, 0, 0, 0);
        wb_quic_ack a5;
        CHECK(wb_quic_parse_ack(b5 + 1, bl5 - 1, WB_QUIC_FRAME_ACK, &a5) == 0,
              "the ACK parses");
        sampled = 0;
        CHECK_EQ("it is acknowledged", wb_quic_ack_apply(&a5, &s3, &sampled), 1);
        CHECK(!sampled, "but takes no sample");

        CHECK(wb_quic_ack_apply(NULL, &s3, NULL) == -1, "a NULL ACK is refused");
        CHECK(wb_quic_ack_apply(&a5, NULL, NULL) == -1, "a NULL space is refused");
    }

    printf("\n=== QUIC ACK TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail != 0;
}
