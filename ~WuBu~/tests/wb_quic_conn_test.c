/*
 * wb_quic_conn_test.c -- LP-93 stone 6 gate.
 *
 * The three spaces are independent sequences, each with its own keys, and
 * discarding a space abandons its loss recovery state while the packet number
 * keeps climbing -- RFC 9000 section 17.1 forbids reuse.
 */
#include "wb_quic_conn.h"

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

static void keys_for(uint8_t *secret, uint8_t *key, uint8_t *iv, uint8_t *hp,
                     int tag) {
    memset(secret, 0x10 + (uint8_t)tag, WB_QUIC_SECRET_LEN);
    memset(key, 0x20 + (uint8_t)tag, WB_QUIC_KEY_LEN);
    memset(iv, 0x30 + (uint8_t)tag, WB_QUIC_IV_LEN);
    memset(hp, 0x40 + (uint8_t)tag, WB_QUIC_HP_LEN);
}

int main(void) {
    /* ---- a fresh connection ---- */
    {
        wb_quic_conn c;
        wb_quic_conn_init(&c, 1234);
        CHECK_EQ("the clock is injected once", c.now_us, 1234);
        CHECK_EQ("Initial starts at 0", c.sp[WB_QUIC_SP_INITIAL].next_pn, 0);
        CHECK_EQ("Handshake starts at 0", c.sp[WB_QUIC_SP_HANDSHAKE].next_pn, 0);
        CHECK_EQ("Application starts at 0", c.sp[WB_QUIC_SP_APPLICATION].next_pn, 0);
        CHECK(!wb_quic_conn_has_keys(&c, WB_QUIC_SP_INITIAL), "no Initial keys yet");
        CHECK(!wb_quic_conn_has_keys(&c, WB_QUIC_SP_HANDSHAKE), "no Handshake keys yet");
        CHECK(!wb_quic_conn_has_keys(&c, WB_QUIC_SP_APPLICATION), "no application keys yet");
        CHECK(wb_quic_conn_keys(&c, WB_QUIC_SP_INITIAL) == NULL, "and no key set");
        CHECK(!wb_quic_conn_pn_exhausted(&c, WB_QUIC_SP_APPLICATION),
              "and numbers are available");
    }

    /* ---- the three spaces are independent ---- */
    {
        wb_quic_conn c;
        wb_quic_conn_init(&c, 0);
        CHECK_EQ("one in the application space",
                 wb_quic_conn_send(&c, WB_QUIC_SP_APPLICATION, 1200, 1, 0), 0);
        CHECK_EQ("two", wb_quic_conn_send(&c, WB_QUIC_SP_APPLICATION, 1200, 1, 0), 1);
        CHECK_EQ("and three",
                 wb_quic_conn_send(&c, WB_QUIC_SP_APPLICATION, 100, 1, 0), 2);
        CHECK_EQ("the application counter is three",
                 c.sp[WB_QUIC_SP_APPLICATION].next_pn, 3);
        CHECK_EQ("Initial is untouched", c.sp[WB_QUIC_SP_INITIAL].next_pn, 0);
        CHECK_EQ("and Handshake", c.sp[WB_QUIC_SP_HANDSHAKE].next_pn, 0);
        CHECK_EQ("and its in-flight set is empty",
                 c.sp[WB_QUIC_SP_INITIAL].n_sent, 0);
    }

    /* ---- installing keys ---- */
    {
        wb_quic_conn c;
        wb_quic_conn_init(&c, 0);
        uint8_t s[WB_QUIC_SECRET_LEN], k[WB_QUIC_KEY_LEN], iv[WB_QUIC_IV_LEN],
                hp[WB_QUIC_HP_LEN];
        keys_for(s, k, iv, hp, 1);
        wb_quic_conn_install_keys(&c, WB_QUIC_SP_INITIAL, s, k, iv, hp);
        CHECK(wb_quic_conn_has_keys(&c, WB_QUIC_SP_INITIAL), "Initial keys are in");
        const wb_quic_key_set *got = wb_quic_conn_keys(&c, WB_QUIC_SP_INITIAL);
        CHECK(got != NULL, "and the set is returned");
        CHECK_MEM("the secret", got->secret, s, WB_QUIC_SECRET_LEN);
        CHECK_MEM("the key", got->key, k, WB_QUIC_KEY_LEN);
        CHECK_MEM("the iv", got->iv, iv, WB_QUIC_IV_LEN);
        CHECK_MEM("and the header protection key", got->hp, hp, WB_QUIC_HP_LEN);
        CHECK(!wb_quic_conn_has_keys(&c, WB_QUIC_SP_HANDSHAKE),
              "the other spaces are untouched");
        CHECK(wb_quic_conn_keys(&c, WB_QUIC_SP_HANDSHAKE) == NULL, "and have none");
    }

    /* ---- discarding Initial abandons its loss state ---- */
    {
        wb_quic_conn c;
        wb_quic_conn_init(&c, 0);
        uint8_t s[WB_QUIC_SECRET_LEN], k[WB_QUIC_KEY_LEN], iv[WB_QUIC_IV_LEN],
                hp[WB_QUIC_HP_LEN];
        keys_for(s, k, iv, hp, 2);
        wb_quic_conn_install_keys(&c, WB_QUIC_SP_INITIAL, s, k, iv, hp);

        wb_quic_conn_send(&c, WB_QUIC_SP_INITIAL, 1200, 1, 0);
        wb_quic_conn_send(&c, WB_QUIC_SP_INITIAL, 1200, 1, 0);
        wb_quic_conn_tick(&c, 1000);
        CHECK_EQ("the first packet is acknowledged",
                 wb_quic_conn_acked(&c, WB_QUIC_SP_INITIAL, 0), 0);
        CHECK_EQ("so the space has an RTT",
                 c.sp[WB_QUIC_SP_INITIAL].smoothed_rtt_us, 1000);

        /* RFC 9001 4.9.1: the client discards Initial keys when it first sends
         * a Handshake packet. */
        wb_quic_conn_discard_space(&c, WB_QUIC_SP_INITIAL);

        CHECK(!wb_quic_conn_has_keys(&c, WB_QUIC_SP_INITIAL), "the keys are gone");
        CHECK(wb_quic_conn_keys(&c, WB_QUIC_SP_INITIAL) == NULL, "and the set is NULL");
        CHECK_EQ("the in-flight set is abandoned",
                 c.sp[WB_QUIC_SP_INITIAL].n_sent, 0);
        CHECK_EQ("the largest acknowledgement is forgotten",
                 c.sp[WB_QUIC_SP_INITIAL].has_largest_acked, 0);
        CHECK_EQ("the RTT estimate is forgotten",
                 c.sp[WB_QUIC_SP_INITIAL].smoothed_rtt_us, -1);
        uint64_t pns[8], lt;
        CHECK_EQ("and nothing is lost",
                 wb_quic_conn_lost(&c, WB_QUIC_SP_INITIAL, pns, 8, &lt), 0);
        CHECK_EQ("with no deadline", lt, WB_QUIC_NO_LOSS);
    }

    /* ---- a discarded space does not touch the others ---- */
    {
        wb_quic_conn c;
        wb_quic_conn_init(&c, 0);
        uint8_t s[WB_QUIC_SECRET_LEN], k[WB_QUIC_KEY_LEN], iv[WB_QUIC_IV_LEN],
                hp[WB_QUIC_HP_LEN];
        keys_for(s, k, iv, hp, 3);
        wb_quic_conn_install_keys(&c, WB_QUIC_SP_APPLICATION, s, k, iv, hp);
        wb_quic_conn_send(&c, WB_QUIC_SP_APPLICATION, 100, 1, 0);
        wb_quic_conn_send(&c, WB_QUIC_SP_APPLICATION, 100, 1, 0);
        wb_quic_conn_discard_space(&c, WB_QUIC_SP_INITIAL);
        CHECK(wb_quic_conn_has_keys(&c, WB_QUIC_SP_APPLICATION),
              "application keys survive");
        CHECK_EQ("and its packets survive",
                 c.sp[WB_QUIC_SP_APPLICATION].n_sent, 2);
        CHECK_EQ("and its counter", c.sp[WB_QUIC_SP_APPLICATION].next_pn, 2);
    }

    /* ---- the packet number is never reused ---- */
    {
        wb_quic_conn c;
        wb_quic_conn_init(&c, 0);
        wb_quic_conn_send(&c, WB_QUIC_SP_INITIAL, 10, 1, 0);
        wb_quic_conn_send(&c, WB_QUIC_SP_INITIAL, 10, 1, 0);
        wb_quic_conn_send(&c, WB_QUIC_SP_INITIAL, 10, 1, 0);
        wb_quic_conn_discard_space(&c, WB_QUIC_SP_INITIAL);
        CHECK_EQ("the counter continues at 3",
                 c.sp[WB_QUIC_SP_INITIAL].next_pn, 3);
        CHECK_EQ("and the next packet is 3, not 0",
                 wb_quic_conn_send(&c, WB_QUIC_SP_INITIAL, 10, 1, 0), 3);
    }

    /* ---- discarding a space with nothing in it is harmless ---- */
    {
        wb_quic_conn c;
        wb_quic_conn_init(&c, 77);
        wb_quic_conn_discard_space(&c, WB_QUIC_SP_HANDSHAKE);
        CHECK_EQ("the counter stays", c.sp[WB_QUIC_SP_HANDSHAKE].next_pn, 0);
        CHECK_EQ("and the clock stays", c.sp[WB_QUIC_SP_HANDSHAKE].now_us, 77);
    }

    /* ---- routing ---- */
    {
        wb_quic_conn c;
        wb_quic_conn_init(&c, 0);
        wb_quic_conn_send(&c, WB_QUIC_SP_HANDSHAKE, 1000, 1, 0);
        wb_quic_conn_tick(&c, 1000);
        CHECK_EQ("an ack routes to the right space",
                 wb_quic_conn_acked(&c, WB_QUIC_SP_HANDSHAKE, 0), 0);
        CHECK_EQ("and updates its estimator",
                 c.sp[WB_QUIC_SP_HANDSHAKE].smoothed_rtt_us, 1000);
        CHECK_EQ("the Initial estimator is untouched",
                 c.sp[WB_QUIC_SP_INITIAL].smoothed_rtt_us, -1);
        CHECK_EQ("and the PTO comes from the right space",
                 wb_quic_conn_pto(&c, WB_QUIC_SP_HANDSHAKE, 0, 1, 0), 3000);
        CHECK_EQ("a space with no RTT still has a floor",
                 wb_quic_conn_pto(&c, WB_QUIC_SP_INITIAL, 0, 1, 0),
                 WB_QUIC_GRANULARITY_US);
    }

    /* ---- out-of-range spaces are refused, not crashed ---- */
    {
        wb_quic_conn c;
        wb_quic_conn_init(&c, 0);
        uint8_t s[WB_QUIC_SECRET_LEN], k[WB_QUIC_KEY_LEN], iv[WB_QUIC_IV_LEN],
                hp[WB_QUIC_HP_LEN];
        keys_for(s, k, iv, hp, 4);
        CHECK_EQ("a bad space sends nothing",
                 wb_quic_conn_send(&c, (wb_quic_sp)99, 10, 1, 0), 0);
        CHECK(!wb_quic_conn_has_keys(&c, (wb_quic_sp)99), "or takes keys");
        CHECK(wb_quic_conn_keys(&c, (wb_quic_sp)99) == NULL, "or returns them");
        CHECK_EQ("or loses anything",
                 wb_quic_conn_lost(&c, (wb_quic_sp)99, NULL, 0, NULL), 0);
        CHECK(!wb_quic_conn_pn_exhausted(&c, (wb_quic_sp)99), "or claims exhaustion");
        wb_quic_conn_discard_space(&c, (wb_quic_sp)99);
        wb_quic_conn_init(NULL, 0);
        wb_quic_conn_tick(NULL, 0);
        CHECK(1, "NULL is safe");
    }

    /* ---- the clock reaches all three ---- */
    {
        wb_quic_conn c;
        wb_quic_conn_init(&c, 0);
        wb_quic_conn_tick(&c, 555);
        CHECK_EQ("Initial", c.sp[WB_QUIC_SP_INITIAL].now_us, 555);
        CHECK_EQ("Handshake", c.sp[WB_QUIC_SP_HANDSHAKE].now_us, 555);
        CHECK_EQ("Application", c.sp[WB_QUIC_SP_APPLICATION].now_us, 555);
    }

    printf("\n=== QUIC CONN TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail != 0;
}
