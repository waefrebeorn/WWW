/*
 * wb_interp.c -- fixed-timestep render interpolation (LP-04). See wb_interp.h.
 */
#include "wb_interp.h"

void wb_interp_lerp3(const float *prev, const float *cur, float alpha, float *out) {
    out[0] = prev[0] + (cur[0] - prev[0]) * alpha;
    out[1] = prev[1] + (cur[1] - prev[1]) * alpha;
    out[2] = prev[2] + (cur[2] - prev[2]) * alpha;
}

float wb_interp_alpha(double remainder, double step) {
    if (step <= 0.0) return 0.0f;
    double a = remainder / step;
    if (a < 0.0) a = 0.0;
    if (a > 1.0) a = 1.0;
    return (float)a;
}
