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

    printf("\n=== QUIC ACK TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail != 0;
}
