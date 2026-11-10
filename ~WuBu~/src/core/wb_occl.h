/*
 * wb_occl.h -- screen-space occlusion culling (LP-14 stone 1).
 *
 * A depth buffer that records the nearest surface per cell; a bounding box is
 * visible if any cell it covers is nearer than the box. This is the CPU-side
 * half of occlusion culling -- the same test a GPU occlusion query answers, but
 * runnable headless, which is what makes it usable as a build gate (a culling
 * regression turns red instead of shipping).
 *
 * Coordinates are screen-space pixels; depth follows the usual convention that
 * smaller is nearer. C11, no deps.
 */
#ifndef WB_OCCL_H
#define WB_OCCL_H

#include <stddef.h>

typedef struct {
    int   w, h;
    float *depth;   /* w*h, nearest surface per cell */
} wb_occl;

int  wb_occl_init(wb_occl *o, int w, int h);
void wb_occl_free(wb_occl *o);
void wb_occl_clear(wb_occl *o, float far_depth);

/* Rasterize an occluder rectangle at `depth` (nearest wins). */
void wb_occl_rasterize(wb_occl *o, int x, int y, int w, int h, float depth);

/* 1 if the rectangle is visible at `depth` (any covered cell is nearer than it). */
int  wb_occl_visible(const wb_occl *o, int x, int y, int w, int h, float depth);

/* The nearest depth in a rectangle, or `far_depth` when nothing covers it. */
float wb_occl_depth(const wb_occl *o, int x, int y, int w, int h, float far_depth);

#endif /* WB_OCCL_H */
