/*
 * wb_taa.c -- temporal upscaling / accumulation (LP-18 stone 1). See wb_taa.h.
 */
#include "wb_taa.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

int wb_taa_init(wb_taa *t, int w, int h) {
    if (!t || w <= 0 || h <= 0) return -1;
    size_t n = (size_t)w * (size_t)h;
    t->history[0] = (float *)calloc(n * 3, sizeof(float));
    t->history[1] = (float *)calloc(n * 3, sizeof(float));
    t->weight = (float *)calloc(n, sizeof(float));
    if (!t->history[0] || !t->history[1] || !t->weight) {
        free(t->history[0]); free(t->history[1]); free(t->weight);
        t->history[0] = t->history[1] = NULL;
        t->weight = NULL;
        t->w = t->h = 0;
        return -1;
    }
    t->w = w;
    t->h = h;
    t->flip = 0;
    return 0;
}

void wb_taa_free(wb_taa *t) {
    if (!t) return;
    free(t->history[0]);
    free(t->history[1]);
    free(t->weight);
    t->history[0] = t->history[1] = NULL;
    t->weight = NULL;
    t->w = t->h = 0;
}

void wb_taa_reset(wb_taa *t) {
    if (!t || !t->history[0]) return;
    memset(t->history[0], 0, (size_t)t->w * (size_t)t->h * 3 * sizeof(float));
    memset(t->history[1], 0, (size_t)t->w * (size_t)t->h * 3 * sizeof(float));
    if (t->weight) memset(t->weight, 0, (size_t)t->w * (size_t)t->h * sizeof(float));
    t->flip = 0;
}

static void wb_taa_sample(const float *src, int sw, int sh, int x, int y, float *out) {
    if (x < 0) x = 0; if (x >= sw) x = sw - 1;
    if (y < 0) y = 0; if (y >= sh) y = sh - 1;
    const float *p = src + ((size_t)y * sw + x) * 3;
    out[0] = p[0]; out[1] = p[1]; out[2] = p[2];
}

void wb_taa_resolve(wb_taa *t, const float *src, int sw, int sh,
                    const float *motion, float alpha, float *dst) {
    if (!t || !t->history[0] || !t->history[1] || !src || !motion || !dst) return;
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;

    /* read the previous frame's history, write the new one: updating in place
     * would let a texel read a value this same frame already overwrote. */
    float *src_hist = t->history[t->flip];
    float *dst_hist = t->history[1 - t->flip];

    for (int y = 0; y < t->h; y++) {
        for (int x = 0; x < t->w; x++) {
            /* the current frame, point-sampled up to the target resolution */
            float cur[3];
            wb_taa_sample(src, sw, sh, x * sw / t->w, y * sh / t->h, cur);

            /* the first frame for a texel is copied, not accumulated: there is
             * no history to reproject and the clip would treat 0 as a ghost. */
            size_t ti = (size_t)y * t->w + x;
            if (t->weight[ti] == 0.0f) {
                float *out = dst + ti * 3;
                for (int c = 0; c < 3; c++) {
                    out[c] = cur[c];
                    dst_hist[ti * 3 + c] = cur[c];
                }
                t->weight[ti] = 1.0f;
                continue;
            }

            /* neighbourhood min/max for the history clip */
            float lo[3] = { 1e30f, 1e30f, 1e30f };
            float hi[3] = { -1e30f, -1e30f, -1e30f };
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if (nx < 0) nx = 0; if (nx >= t->w) nx = t->w - 1;
                    if (ny < 0) ny = 0; if (ny >= t->h) ny = t->h - 1;
                    float s[3];
                    wb_taa_sample(src, sw, sh, nx * sw / t->w, ny * sh / t->h, s);
                    for (int c = 0; c < 3; c++) {
                        if (s[c] < lo[c]) lo[c] = s[c];
                        if (s[c] > hi[c]) hi[c] = s[c];
                    }
                }
            }

            /* reproject into the history */
            const float *mv = motion + ((size_t)y * t->w + x) * 2;
            int hx = (int)floorf((float)x + mv[0]);
            int hy = (int)floorf((float)y + mv[1]);
            float hist[3];
            if (hx < 0 || hy < 0 || hx >= t->w || hy >= t->h) {
                hist[0] = cur[0]; hist[1] = cur[1]; hist[2] = cur[2];
            } else {
                const float *hp = src_hist + ((size_t)hy * t->w + hx) * 3;
                hist[0] = hp[0]; hist[1] = hp[1]; hist[2] = hp[2];
            }

            /* clip the history to the current neighbourhood, then blend */
            float *out = dst + ((size_t)y * t->w + x) * 3;
            for (int c = 0; c < 3; c++) {
                float h = hist[c];
                if (h < lo[c]) h = lo[c];
                if (h > hi[c]) h = hi[c];
                float o = cur[c] + (h - cur[c]) * alpha;
                out[c] = o;
                dst_hist[ti * 3 + c] = o;
            }
        }
    }

    t->flip = 1 - t->flip;   /* the new history becomes the current one */
}
