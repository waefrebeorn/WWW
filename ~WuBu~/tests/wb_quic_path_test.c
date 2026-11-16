/*
 * wb_quic_path_test.c -- LP-93 stone 13 gate.
 *
 * The rules are RFC 9000 section 8.2 and section 9.2/9.3, not a separate
 * document: RFC 9003 is BGP.
 */
#include "wb_quic_path.h"
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

static const uint64_t PTO = 100000;          /* 100 ms */
static const uint64_t TIMEOUT = 3 * PTO;     /* section 8.2.4 */

int main(void) {
    uint8_t a[WB_QUIC_PATH_DATA_LEN] = {1,2,3,4,5,6,7,8};
    uint8_t b[WB_QUIC_PATH_DATA_LEN] = {9,9,9,9,9,9,9,9};
    uint8_t c[WB_QUIC_PATH_DATA_LEN] = {1,2,3,4,5,6,7,8};   /* equal to a */

    /* ---- the exchange ---- */
    {
        wb_quic_path p;
        wb_quic_path_init(&p, 0);
        CHECK_EQ("starts unvalidated", wb_quic_path_validated(&p), 0);
        CHECK_EQ("and not abandoned", wb_quic_path_abandoned(&p), 0);
        CHECK_EQ("with no probes", wb_quic_path_probe_count(&p), 0);
        CHECK_EQ("and no MTU", wb_quic_path_mtu_validated(&p), 0);

        CHECK(wb_quic_path_start(&p, a, 1) == 0, "a challenge starts");
        CHECK_EQ("one probe", wb_quic_path_probe_count(&p), 1);
        CHECK_EQ("still unvalidated", wb_quic_path_validated(&p), 0);

        CHECK_EQ("a wrong response matches nothing",
                 wb_quic_path_response(&p, b), -1);
        CHECK_EQ("and does not validate", wb_quic_path_validated(&p), 0);

        /* the data is echoed exactly; an equal copy is the same challenge */
        CHECK_EQ("the right response validates", wb_quic_path_response(&p, c), 1);
        CHECK_EQ("the path", wb_quic_path_validated(&p), 1);
        CHECK_EQ("and the MTU, having been sent expanded",
                 wb_quic_path_mtu_validated(&p), 1);

        /* RFC 9000 section 8.2.2: an endpoint MUST NOT send more than one
         * PATH_RESPONSE for one PATH_CHALLENGE, so a repeat matches nothing
         * outstanding and is ignored rather than counting again */
        CHECK_EQ("a repeated response matches nothing outstanding",
                 wb_quic_path_response(&p, a), -1);
        CHECK_EQ("and an unrelated one is ignored", wb_quic_path_response(&p, b), -1);
        CHECK_EQ("with the probe still there", wb_quic_path_probe_count(&p), 1);
    }

    /* ---- an unexpanded challenge validates the address, not the MTU ---- */
    {
        wb_quic_path p;
        wb_quic_path_init(&p, 0);
        CHECK(wb_quic_path_start(&p, a, 0) == 0, "an unexpanded challenge starts");
        CHECK_EQ("it validates", wb_quic_path_response(&p, a), 1);
        CHECK_EQ("the address", wb_quic_path_validated(&p), 1);
        CHECK_EQ("but not the MTU", wb_quic_path_mtu_validated(&p), 0);
    }

    /* ---- the timer ---- */
    {
        wb_quic_path p;
        wb_quic_path_init(&p, 0);
        wb_quic_path_start(&p, a, 1);
        p.now_us = TIMEOUT - 1;
        CHECK_EQ("just inside the timer", wb_quic_path_tick(&p, PTO), 0);
        CHECK_EQ("not abandoned", wb_quic_path_abandoned(&p), 0);
        CHECK_EQ("the probe stands", wb_quic_path_probe_count(&p), 1);

        p.now_us = TIMEOUT;
        CHECK_EQ("at the timer it abandons", wb_quic_path_tick(&p, PTO), 1);
        CHECK_EQ("and reports it", wb_quic_path_abandoned(&p), 1);
        CHECK_EQ("with no probes left", wb_quic_path_probe_count(&p), 0);
        CHECK_EQ("and it is not a validation", wb_quic_path_validated(&p), 0);

        /* a validated path is never "abandoned" by the timer */
        wb_quic_path q;
        wb_quic_path_init(&q, 0);
        wb_quic_path_start(&q, a, 1);
        wb_quic_path_response(&q, a);
        q.now_us = 10 * TIMEOUT;
        CHECK_EQ("a validated path ignores the timer", wb_quic_path_tick(&q, PTO), 0);
        CHECK_EQ("and stays validated", wb_quic_path_validated(&q), 1);
    }

    /* ---- losing one challenge is not failure ---- */
    {
        wb_quic_path p;
        wb_quic_path_init(&p, 0);
        wb_quic_path_start(&p, a, 1);
        p.now_us = 500;
        wb_quic_path_start(&p, b, 1);
        CHECK_EQ("two probes", wb_quic_path_probe_count(&p), 2);

        p.now_us = TIMEOUT;          /* the first is now older than 3*PTO */
        CHECK_EQ("the first expires", wb_quic_path_tick(&p, PTO), 0);
        CHECK_EQ("but the path is not abandoned", wb_quic_path_abandoned(&p), 0);
        CHECK_EQ("one probe remains", wb_quic_path_probe_count(&p), 1);

        /* the survivor can still validate the path */
        CHECK_EQ("and it does", wb_quic_path_response(&p, b), 1);
        CHECK_EQ("validated", wb_quic_path_validated(&p), 1);
    }

    /* ---- abandoning early, as a migration does ---- */
    {
        wb_quic_path p;
        wb_quic_path_init(&p, 0);
        wb_quic_path_start(&p, a, 1);
        wb_quic_path_abandon(&p);
        CHECK_EQ("abandoned", wb_quic_path_abandoned(&p), 1);
        CHECK_EQ("and the probes are gone", wb_quic_path_probe_count(&p), 0);
        CHECK_EQ("a response after abandoning matches nothing",
                 wb_quic_path_response(&p, a), -1);
    }

    /* ---- the bound on concurrent probes ---- */
    {
        wb_quic_path p;
        wb_quic_path_init(&p, 0);
        for (int i = 0; i < WB_QUIC_PATH_MAX; i++) {
            uint8_t d[WB_QUIC_PATH_DATA_LEN] = {0};
            d[0] = (uint8_t)i;
            CHECK(wb_quic_path_start(&p, d, 1) == 0, "a probe fits");
        }
        CHECK_EQ("the bound is reached", wb_quic_path_probe_count(&p),
                 WB_QUIC_PATH_MAX);
        uint8_t d[WB_QUIC_PATH_DATA_LEN] = {0};
        d[0] = 0xff;
        CHECK(wb_quic_path_start(&p, d, 1) == -1, "and the next does not fit");
    }

    /* ---- the challenge data is the caller's, not invented here ---- */
    {
        wb_quic_path p;
        wb_quic_path_init(&p, 0);
        wb_quic_path_start(&p, a, 1);
        CHECK_MEM("it is stored verbatim", p.probes[0].data, a,
                  WB_QUIC_PATH_DATA_LEN);
        CHECK(wb_quic_path_start(NULL, a, 1) == -1, "and NULL is refused");
        CHECK(wb_quic_path_response(NULL, a) == -1, "as is a NULL response");
        CHECK_EQ("and the minimum datagram is 1200",
                 WB_QUIC_PATH_MIN_DATAGRAM, 1200);
    }

    printf("\n=== QUIC PATH TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail != 0;
}
