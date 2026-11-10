/*
 * wb_lod.h -- level-of-detail selection with hysteresis (LP-15 stone 1).
 *
 * A LOD chain is a descending list of screen-space sizes: LOD 0 is used while
 * the object covers more than sizes[0] pixels, LOD 1 between sizes[1] and
 * sizes[0], and so on. Without hysteresis a camera drifting across a threshold
 * makes the object pop between two levels every frame; the margin keeps the
 * current level until the size is clearly inside the next one's band.
 *
 * Deterministic. C11, no deps.
 */
#ifndef WB_LOD_H
#define WB_LOD_H

#include <stddef.h>

typedef struct {
    const float *sizes;   /* descending: sizes[0] > sizes[1] > ... */
    int          n;
    float        hysteresis;  /* 0..0.5; 0 disables the margin */
    int          current;
} wb_lod;

void wb_lod_init(wb_lod *l, const float *sizes, int n, float hysteresis);

/* Update and return the current level for a screen-space size. */
int  wb_lod_pick(wb_lod *l, float screen_size);
int  wb_lod_current(const wb_lod *l);

/* The level a fresh chain would choose (no hysteresis, no state). */
int  wb_lod_level_for(const wb_lod *l, float screen_size);

#endif /* WB_LOD_H */
