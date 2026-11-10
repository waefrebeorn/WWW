/*
 * wb_snapshot.c -- snapshot interpolation (LP-29). See wb_snapshot.h.
 */
#include "wb_snapshot.h"

#include <math.h>

void wb_snapshot_normalize(wb_snapshot_state *s) {
    if (!s) return;
    float m = sqrtf(s->qx * s->qx + s->qy * s->qy + s->qz * s->qz + s->qw * s->qw);
    if (m > 0.0f) { s->qx /= m; s->qy /= m; s->qz /= m; s->qw /= m; }
}

void wb_snapshot_interp(const wb_snapshot_state *a, const wb_snapshot_state *b,
                        float t, wb_snapshot_state *out) {
    if (!a || !b || !out) return;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    out->px = a->px + (b->px - a->px) * t;
    out->py = a->py + (b->py - a->py) * t;
    out->pz = a->pz + (b->pz - a->pz) * t;

    /* shortest arc: if the two quaternions are on opposite hemispheres, flip b */
    float dot = a->qx * b->qx + a->qy * b->qy + a->qz * b->qz + a->qw * b->qw;
    float s = dot < 0.0f ? -1.0f : 1.0f;

    out->qx = a->qx + (s * b->qx - a->qx) * t;
    out->qy = a->qy + (s * b->qy - a->qy) * t;
    out->qz = a->qz + (s * b->qz - a->qz) * t;
    out->qw = a->qw + (s * b->qw - a->qw) * t;
    wb_snapshot_normalize(out);
}

float wb_snapshot_interp_error(const wb_snapshot_state *a, const wb_snapshot_state *b,
                               float t, const wb_snapshot_state *out) {
    (void)a; (void)b; (void)t;
    if (!out) return 0.0f;
    float m = sqrtf(out->qx * out->qx + out->qy * out->qy + out->qz * out->qz + out->qw * out->qw);
    return fabsf(m - 1.0f);
}
