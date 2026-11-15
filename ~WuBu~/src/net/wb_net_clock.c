/*
 * wb_net_clock.c -- LP-96: clock synchronisation.
 */
#include "wb_net_clock.h"

#include <math.h>
#include <stdlib.h>

#define WB_NET_CLOCK_WINDOW 8
#define WB_NET_CLOCK_DRIFT_ALPHA 0.1
/* A clock more than a thousand parts per million off is not a drift, it is a
 * jump; treat it as a new offset rather than a rate. */
#define WB_NET_CLOCK_MAX_DRIFT 1e-3

typedef struct {
    int    used;
    double offset;
    double delay;
    double t_mid;               /* (t0+t3)/2: the instant the offset describes */
} wb_net_clock_sample_slot;

struct wb_net_clock {
    wb_net_clock_sample_slot win[WB_NET_CLOCK_WINDOW];
    int   n;
    int   head;

    int      ready;
    double   offset;            /* at t_anchor */
    double   t_anchor;
    double   delay;
    double   drift;
    int      have_drift;
};

wb_net_clock *wb_net_clock_new(void) {
    return (wb_net_clock *)calloc(1, sizeof(wb_net_clock));
}

void wb_net_clock_free(wb_net_clock *c) { free(c); }

int wb_net_clock_samples(const wb_net_clock *c) { return c->n; }
int wb_net_clock_ready(const wb_net_clock *c) { return c->ready; }
double wb_net_clock_offset(const wb_net_clock *c) { return c->offset; }
double wb_net_clock_drift(const wb_net_clock *c) { return c->drift; }
double wb_net_clock_delay(const wb_net_clock *c) { return c->delay; }

double wb_net_clock_remote(const wb_net_clock *c, double local_now) {
    if (!c->ready) return local_now;
    return local_now + c->offset + c->drift * (local_now - c->t_anchor);
}

double wb_net_clock_sample(wb_net_clock *c, double t0, double t1,
                           double t2, double t3) {
    double nan = NAN;
    if (!isfinite(t0) || !isfinite(t1) || !isfinite(t2) || !isfinite(t3)) {
        return nan;
    }

    double delay = (t3 - t0) - (t2 - t1);
    if (delay < 0.0) return nan;            /* impossible exchange */

    double offset = ((t1 - t0) + (t2 - t3)) / 2.0;

    wb_net_clock_sample_slot *s = &c->win[c->head];
    s->used = 1;
    s->offset = offset;
    s->delay = delay;
    s->t_mid = (t0 + t3) / 2.0;
    c->head = (c->head + 1) % WB_NET_CLOCK_WINDOW;
    if (c->n < WB_NET_CLOCK_WINDOW) c->n++;

    /* The least-delayed sample is the closest to a symmetric path, so its
     * offset carries the least asymmetry error. Ties go to the newer sample:
     * with nothing to choose between them, the more recent one is the better
     * estimate. */
    const wb_net_clock_sample_slot *best = NULL;
    int start = (c->n < WB_NET_CLOCK_WINDOW) ? 0 : c->head;
    for (int i = 0; i < c->n; i++) {
        const wb_net_clock_sample_slot *q =
            &c->win[(start + i) % WB_NET_CLOCK_WINDOW];
        if (!q->used) continue;
        if (!best || q->delay <= best->delay) best = q;
    }
    if (!best) return nan;

    if (!c->ready) {
        c->offset = best->offset;
        c->t_anchor = best->t_mid;
        c->delay = best->delay;
        c->drift = 0.0;
        c->ready = 1;
    } else if (best->offset != c->offset) {
        double dt = best->t_mid - c->t_anchor;
        if (dt > 1e-6) {
            double rate = (best->offset - c->offset) / dt;
            if (!isfinite(rate)) rate = 0.0;
            if (rate > WB_NET_CLOCK_MAX_DRIFT) rate = WB_NET_CLOCK_MAX_DRIFT;
            if (rate < -WB_NET_CLOCK_MAX_DRIFT) rate = -WB_NET_CLOCK_MAX_DRIFT;
            /* the first rate is the estimate; after that it is smoothed, so a
             * single noisy exchange cannot throw the drift off */
            if (!c->have_drift) {
                c->drift = rate;
                c->have_drift = 1;
            } else {
                c->drift += (rate - c->drift) * WB_NET_CLOCK_DRIFT_ALPHA;
            }
        }
        c->offset = best->offset;
        c->t_anchor = best->t_mid;
        c->delay = best->delay;
    }
    return offset;
}
