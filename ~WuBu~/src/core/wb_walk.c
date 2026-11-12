/*
 * wb_walk.c -- procedural walk cycle. See wb_walk.h for why this exists.
 */
#include "wb_walk.h"
#include <math.h>

#define WB_TWO_PI 6.283185307179586476925286766559

void wb_walk_init(wb_walk *w, double rate) {
    if (!w) return;
    w->phase = 0.0;
    w->rate = rate;
    w->speed = 0.0;
}

void wb_walk_step(wb_walk *w, double dt, double speed) {
    if (!w) return;
    if (dt < 0.0) dt = 0.0;
    w->speed = speed;
    w->phase += speed * dt * w->rate;
    /* Wrap rather than accumulate: a long run would otherwise lose precision in
     * the phase and the stride would visibly stutter after an hour. */
    if (w->phase >= WB_TWO_PI || w->phase < 0.0)
        w->phase = fmod(w->phase, WB_TWO_PI);
    if (w->phase < 0.0) w->phase += WB_TWO_PI;
}

double wb_walk_swing(const wb_walk *w) {
    return w ? sin(w->phase) : 0.0;
}

double wb_walk_lift(const wb_walk *w) {
    if (!w) return 0.0;
    double s = sin(w->phase);
    return s > 0.0 ? s : 0.0;
}
