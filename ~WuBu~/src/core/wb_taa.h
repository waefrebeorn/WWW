/*
 * wb_taa.h -- temporal upscaling / accumulation (LP-18 stone 1).
 *
 * Render at a lower resolution, then resolve to the target with temporal
 * accumulation: each output texel reprojects into the history with its motion
 * vector, the history sample is clipped to the current frame's neighbourhood
 * (so a stale ghost cannot survive), and the result is blended with the current
 * frame. The clip is what makes the feedback stable; the reprojection is what
 * makes it sharp.
 *
 * This is the CPU-side algorithm, runnable headless and deterministically.
 * C11, no deps.
 */
#ifndef WB_TAA_H
#define WB_TAA_H

#include <stdint.h>

typedef struct {
    int   w, h;        /* the target (output) resolution */
    float *history[2]; /* double buffered: [flip] is read, [1-flip] is written */
    int   flip;
    float *weight;     /* w*h, 0 = no valid history for this texel */
} wb_taa;

int  wb_taa_init(wb_taa *t, int w, int h);
void wb_taa_free(wb_taa *t);
void wb_taa_reset(wb_taa *t);

/* Resolve one frame. `src` is sw*sh*3 (low-res, RGB), `motion` is w*h*2 (a
 * per-output-texel offset into the history), `dst` is w*h*3. `alpha` is the
 * history feedback in [0,1]; 0 disables temporal accumulation. */
void wb_taa_resolve(wb_taa *t, const float *src, int sw, int sh,
                    const float *motion, float alpha, float *dst);

#endif /* WB_TAA_H */
