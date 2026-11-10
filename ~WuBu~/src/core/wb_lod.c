/*
 * wb_lod.c -- LOD selection with hysteresis (LP-15 stone 1). See wb_lod.h.
 */
#include "wb_lod.h"

#include <math.h>

void wb_lod_init(wb_lod *l, const float *sizes, int n, float hysteresis) {
    if (!l) return;
    l->sizes = sizes;
    l->n = n > 0 ? n : 0;
    l->hysteresis = hysteresis < 0.0f ? 0.0f : (hysteresis > 0.5f ? 0.5f : hysteresis);
    l->current = 0;
}

int wb_lod_level_for(const wb_lod *l, float screen_size) {
    if (!l || l->n <= 0) return 0;
    int i = 0;
    while (i + 1 < l->n && screen_size <= l->sizes[i + 1]) i++;
    return i;
}

int wb_lod_pick(wb_lod *l, float screen_size) {
    if (!l || l->n <= 0) return 0;
    float h = l->hysteresis;
    int i = l->current;
    /* coarser: the size must fall clearly below the next level's threshold */
    while (i + 1 < l->n && screen_size < l->sizes[i + 1] * (1.0f - h)) i++;
    /* finer: the size must rise clearly above the previous level's threshold */
    while (i > 0 && screen_size > l->sizes[i - 1] * (1.0f + h)) i--;
    l->current = i;
    return i;
}

int wb_lod_current(const wb_lod *l) { return l ? l->current : 0; }
