/*
 * wb_occl.c -- screen-space occlusion culling (LP-14 stone 1). See wb_occl.h.
 */
#include "wb_occl.h"

#include <stdlib.h>

static int wb_occl_clip(const wb_occl *o, int x, int y, int w, int h,
                        int *cx, int *cy, int *cw, int *ch) {
    if (!o || !o->depth || w <= 0 || h <= 0) return 0;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w; if (x1 > o->w) x1 = o->w;
    int y1 = y + h; if (y1 > o->h) y1 = o->h;
    if (x0 >= x1 || y0 >= y1) return 0;
    *cx = x0; *cy = y0; *cw = x1 - x0; *ch = y1 - y0;
    return 1;
}

int wb_occl_init(wb_occl *o, int w, int h) {
    if (!o || w <= 0 || h <= 0) return -1;
    o->depth = (float *)malloc((size_t)w * (size_t)h * sizeof(float));
    if (!o->depth) { o->w = o->h = 0; return -1; }
    o->w = w;
    o->h = h;
    return 0;
}

void wb_occl_free(wb_occl *o) {
    if (!o) return;
    free(o->depth);
    o->depth = NULL;
    o->w = o->h = 0;
}

void wb_occl_clear(wb_occl *o, float far_depth) {
    if (!o || !o->depth) return;
    size_t n = (size_t)o->w * (size_t)o->h;
    for (size_t i = 0; i < n; i++) o->depth[i] = far_depth;
}

void wb_occl_rasterize(wb_occl *o, int x, int y, int w, int h, float depth) {
    int cx, cy, cw, ch;
    if (!wb_occl_clip(o, x, y, w, h, &cx, &cy, &cw, &ch)) return;
    for (int j = 0; j < ch; j++) {
        float *row = o->depth + (size_t)(cy + j) * o->w + cx;
        for (int i = 0; i < cw; i++)
            if (depth < row[i]) row[i] = depth;   /* nearest wins */
    }
}

int wb_occl_visible(const wb_occl *o, int x, int y, int w, int h, float depth) {
    int cx, cy, cw, ch;
    if (!wb_occl_clip(o, x, y, w, h, &cx, &cy, &cw, &ch)) return 0;
    for (int j = 0; j < ch; j++) {
        const float *row = o->depth + (size_t)(cy + j) * o->w + cx;
        for (int i = 0; i < cw; i++)
            if (row[i] > depth) return 1;   /* nothing nearer in front of it */
    }
    return 0;
}

float wb_occl_depth(const wb_occl *o, int x, int y, int w, int h, float far_depth) {
    int cx, cy, cw, ch;
    if (!wb_occl_clip(o, x, y, w, h, &cx, &cy, &cw, &ch)) return far_depth;
    float best = far_depth;
    for (int j = 0; j < ch; j++) {
        const float *row = o->depth + (size_t)(cy + j) * o->w + cx;
        for (int i = 0; i < cw; i++)
            if (row[i] < best) best = row[i];
    }
    return best;
}
