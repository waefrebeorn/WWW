/*
 * wb_net_clock_test.c -- LP-96 gate: the shared time base.
 *
 * Proves:
 *   - NTP's symmetric-delay offset formula on an exactly symmetric exchange
 *   - a known offset is recovered
 *   - an asymmetric path biases the estimate, and the minimum-delay selection
 *     of the last eight samples removes most of that bias -- which is the whole
 *     reason the selection exists
 *   - the delay reported is the round trip
 *   - an impossible exchange is refused, not averaged in
 *   - the drift is recovered, and `remote` extrapolates between syncs
 */
#include "wb_net_clock.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)
#define CHECK_NEAR(got, want, tol, msg) do { \
    double _g = (got), _w = (want), _t = (tol); \
    if (isfinite(_g) && fabs(_g - _w) <= _t) g_pass++; \
    else { g_fail++; printf("FAIL: %s: got %.9f want %.9f +/- %.9f (line %d)\n", msg, _g, _w, _t, __LINE__); } \
} while (0)

/* Build the four timestamps of one exchange.
 *   t0 local send, t1 remote receive, t2 remote send, t3 local receive.
 * With a remote clock ahead by theta, forward delay d1, reverse d2 and
 * processing time proc:
 *   t1 = t0 + d1 + theta
 *   t2 = t1 + proc
 *   t3 = t2 - theta + d2          <-- theta subtracted, not added */
static void exchange(double t0, double theta, double d1, double d2, double proc,
                     double *o0, double *o1, double *o2, double *o3) {
    *o0 = t0;
    *o1 = t0 + d1 + theta;
    *o2 = *o1 + proc;
    *o3 = *o2 - theta + d2;
}

int main(void) {
    /* ---- before any sample there is no base ---- */
    {
        wb_net_clock *c = wb_net_clock_new();
        CHECK(c != NULL, "a clock allocates");
        CHECK(wb_net_clock_ready(c) == 0, "a fresh clock is not ready");
        CHECK(wb_net_clock_samples(c) == 0, "a fresh clock has no samples");
        CHECK_NEAR(wb_net_clock_remote(c, 1234.0), 1234.0, 0.0,
                   "before a sample, remote == local");
        wb_net_clock_free(c);
    }

    /* ---- an exactly symmetric exchange recovers a known offset ---- */
    {
        wb_net_clock *c = wb_net_clock_new();
        double t0, t1, t2, t3;
        exchange(1000.0, 5.0, 10.0, 10.0, 0.0, &t0, &t1, &t2, &t3);
        double off = wb_net_clock_sample(c, t0, t1, t2, t3);
        CHECK_NEAR(off, 5.0, 1e-9, "the raw offset of a symmetric exchange");
        CHECK(wb_net_clock_ready(c) == 1, "one sample is enough to be ready");
        CHECK(wb_net_clock_samples(c) == 1, "one sample is counted");
        CHECK_NEAR(wb_net_clock_offset(c), 5.0, 1e-9, "the estimated offset");
        CHECK_NEAR(wb_net_clock_delay(c), 20.0, 1e-9, "the delay is the round trip");
        CHECK_NEAR(wb_net_clock_remote(c, 2000.0), 2005.0, 1e-9,
                   "remote time is local plus the offset");
        wb_net_clock_free(c);
    }

    /* ---- zero offset, nonzero processing time ---- */
    {
        wb_net_clock *c = wb_net_clock_new();
        double t0, t1, t2, t3;
        exchange(0.0, 0.0, 7.0, 7.0, 3.0, &t0, &t1, &t2, &t3);
        wb_net_clock_sample(c, t0, t1, t2, t3);
        CHECK_NEAR(wb_net_clock_offset(c), 0.0, 1e-9,
                   "a zero offset stays zero through processing time");
        CHECK_NEAR(wb_net_clock_delay(c), 14.0, 1e-9,
                   "processing time does not count as delay");
        wb_net_clock_free(c);
    }

    /* ---- asymmetry biases a single exchange ---- */
    {
        wb_net_clock *c = wb_net_clock_new();
        double t0, t1, t2, t3;
        exchange(0.0, 5.0, 10.0, 30.0, 0.0, &t0, &t1, &t2, &t3);
        wb_net_clock_sample(c, t0, t1, t2, t3);
        /* the estimate is theta + (d1-d2)/2 = 5 - 10 = -5 */
        CHECK_NEAR(wb_net_clock_offset(c), -5.0, 1e-9,
                   "an asymmetric path biases the raw estimate");
        wb_net_clock_free(c);
    }

    /* ---- the minimum-delay selection removes most of that bias ---- */
    {
        /* Eight samples. Every path has the same 10% asymmetry fraction, so
         * the bias is proportional to the round trip: the least-delayed
         * sample is the closest to symmetric. */
        wb_net_clock *c = wb_net_clock_new();
        const double theta = 100.0;
        double bias_min = 0.0, bias_mean = 0.0;
        double offs[8];
        for (int i = 0; i < 8; i++) {
            double rtt = 20.0 + (double)i * 20.0;
            double asym = 0.1;                       /* fraction of the rtt */
            double d1 = rtt * (0.5 + asym), d2 = rtt * (0.5 - asym);
            double t0 = (double)i * 1000.0;
            double a, b, e, d;
            exchange(t0, theta, d1, d2, 0.0, &a, &b, &e, &d);
            offs[i] = wb_net_clock_sample(c, a, b, e, d);
        }
        bias_mean = 0.0;
        for (int i = 0; i < 8; i++) bias_mean += (offs[i] - theta);
        bias_mean /= 8.0;
        double est = wb_net_clock_offset(c);
        bias_min = est - theta;
        printf("     (min-delay bias %.6f vs mean-of-samples bias %.6f)\n",
               bias_min, bias_mean);
        CHECK(fabs(bias_min) < fabs(bias_mean),
              "the selected offset is closer to the truth than the sample mean");
        CHECK(fabs(bias_min) < 3.0, "and within 3 ms of the true offset");
        CHECK(wb_net_clock_delay(c) <= 20.0 + 1e-9,
              "the delay reported is the least-delayed sample's");
        wb_net_clock_free(c);
    }

    /* ---- the window is bounded ---- */
    {
        wb_net_clock *c = wb_net_clock_new();
        double t0, t1, t2, t3;
        for (int i = 0; i < 64; i++) {
            exchange((double)i, 1.0, 5.0, 5.0, 0.0, &t0, &t1, &t2, &t3);
            wb_net_clock_sample(c, t0, t1, t2, t3);
        }
        CHECK(wb_net_clock_samples(c) == 8, "the window holds eight samples");
        CHECK_NEAR(wb_net_clock_offset(c), 1.0, 1e-9, "and still estimates");
        wb_net_clock_free(c);
    }

    /* ---- impossible exchanges are refused ---- */
    {
        wb_net_clock *c = wb_net_clock_new();
        double bad = wb_net_clock_sample(c, 10.0, 10.0, 12.0, 11.0);
        CHECK(!isfinite(bad), "a negative round trip is refused");
        CHECK(wb_net_clock_samples(c) == 0, "and is not counted");
        CHECK(!isfinite(wb_net_clock_sample(c, 0.0, 5.0, 6.0, 0.5)),
              "a reply faster than the remote took to process is refused");
        CHECK(!isfinite(wb_net_clock_sample(c, 1.0, NAN, 2.0, 3.0)),
              "a non-finite timestamp is refused");
        CHECK(wb_net_clock_samples(c) == 0, "still no samples");
        /* a good one afterwards works */
        double t0, t1, t2, t3;
        exchange(100.0, 2.0, 5.0, 5.0, 0.0, &t0, &t1, &t2, &t3);
        CHECK(isfinite(wb_net_clock_sample(c, t0, t1, t2, t3)),
              "a good sample after bad ones is accepted");
        wb_net_clock_free(c);
    }

    /* ---- drift, and extrapolation between syncs ---- */
    {
        wb_net_clock *c = wb_net_clock_new();
        double t0, t1, t2, t3;
        /* remote clock ahead by 0 at local 0, and by 0.1 at local 100:
         * it gains a millisecond per second */
        exchange(0.0, 0.0, 5.0, 5.0, 0.0, &t0, &t1, &t2, &t3);
        wb_net_clock_sample(c, t0, t1, t2, t3);
        exchange(100.0, 0.1, 5.0, 5.0, 0.0, &t0, &t1, &t2, &t3);
        wb_net_clock_sample(c, t0, t1, t2, t3);
        CHECK_NEAR(wb_net_clock_drift(c), 0.001, 1e-9, "the drift is recovered");
        /* The offset describes the exchange midpoint (local 105), so at local
         * 200 the remote is 0.1 + 0.001 * 95 = 0.195 ahead. */
        CHECK_NEAR(wb_net_clock_remote(c, 200.0) - 200.0, 0.195, 1e-6,
                   "remote time extrapolates between syncs");
        CHECK_NEAR(wb_net_clock_remote(c, 1200.0) - 1200.0, 1.195, 1e-6,
                   "and keeps extrapolating");
        wb_net_clock_free(c);
    }

    /* ---- a negative drift (the remote clock loses time) ---- */
    {
        wb_net_clock *c = wb_net_clock_new();
        double t0, t1, t2, t3;
        exchange(0.0, 1.0, 5.0, 5.0, 0.0, &t0, &t1, &t2, &t3);
        wb_net_clock_sample(c, t0, t1, t2, t3);
        exchange(100.0, 0.9, 5.0, 5.0, 0.0, &t0, &t1, &t2, &t3);
        wb_net_clock_sample(c, t0, t1, t2, t3);
        CHECK_NEAR(wb_net_clock_drift(c), -0.001, 1e-9, "a losing clock is negative");
        CHECK_NEAR(wb_net_clock_remote(c, 200.0) - 200.0, 0.805, 1e-6,
                   "and extrapolates the other way");
        wb_net_clock_free(c);
    }

    /* ---- an implausible rate is a jump, not a drift ---- */
    {
        wb_net_clock *c = wb_net_clock_new();
        double t0, t1, t2, t3;
        exchange(0.0, 0.0, 5.0, 5.0, 0.0, &t0, &t1, &t2, &t3);
        wb_net_clock_sample(c, t0, t1, t2, t3);
        exchange(100.0, 500.0, 5.0, 5.0, 0.0, &t0, &t1, &t2, &t3);
        wb_net_clock_sample(c, t0, t1, t2, t3);
        CHECK(fabs(wb_net_clock_drift(c)) <= 1e-3 + 1e-12,
              "a 0.5 s/s rate is clamped, not believed");
        wb_net_clock_free(c);
    }

    /* ---- the estimate is monotone-safe: an out-of-order sample cannot pull
     *      the offset backwards ---- */
    {
        wb_net_clock *c = wb_net_clock_new();
        double t0, t1, t2, t3;
        exchange(1000.0, 10.0, 5.0, 5.0, 0.0, &t0, &t1, &t2, &t3);
        wb_net_clock_sample(c, t0, t1, t2, t3);
        /* an exchange that claims the remote is a second behind: only the
         * delay decides, and this one has a larger round trip, so it loses */
        exchange(1100.0, -990.0, 50.0, 50.0, 0.0, &t0, &t1, &t2, &t3);
        wb_net_clock_sample(c, t0, t1, t2, t3);
        CHECK_NEAR(wb_net_clock_offset(c), 10.0, 1e-9,
                   "a worse sample does not displace a better one");
        wb_net_clock_free(c);
    }

    printf("\n=== NET CLOCK TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail != 0;
}
