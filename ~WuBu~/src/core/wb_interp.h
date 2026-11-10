/*
 * wb_interp.h -- fixed-timestep render interpolation (LP-04).
 *
 * The simulation steps at a fixed rate; the renderer runs at the display rate
 * and interpolates between the previous and current simulation state by the
 * accumulator's fractional step (alpha in [0,1)). Pure float, deterministic,
 * no allocation -- the cosmetic transform is never allowed to affect the sim.
 */
#ifndef WB_INTERP_H
#define WB_INTERP_H

/* out = prev + (cur - prev) * alpha, per component */
void wb_interp_lerp3(const float *prev, const float *cur, float alpha, float *out);

/* the render-interpolation alpha from a fixed-step accumulator's remainder */
float wb_interp_alpha(double remainder, double step);

#endif /* WB_INTERP_H */
