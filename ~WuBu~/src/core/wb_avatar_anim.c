/*
 * wb_avatar_anim.c -- animation sampling: TRS -> mat4 and keyframe
 * interpolation. See wb_anim.h.
 */
#include "wb_anim.h"

#include <math.h>

static void mat4_from_trs(float m[16], const float t[3], const float q[4], const float s[3]) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float x2 = x + x, y2 = y + y, z2 = z + z;
    float xx = x * x2, xy = x * y2, xz = x * z2;
    float yy = y * y2, yz = y * z2, zz = z * z2;
    float wx = w * x2, wy = w * y2, wz = w * z2;
    m[0] = (1.0f - (yy + zz)) * s[0];
    m[1] = (xy + wz) * s[0];
    m[2] = (xz - wy) * s[0];
    m[3] = 0.0f;
    m[4] = (xy - wz) * s[1];
    m[5] = (1.0f - (xx + zz)) * s[1];
    m[6] = (yz + wx) * s[1];
    m[7] = 0.0f;
    m[8] = (xz + wy) * s[2];
    m[9] = (yz - wx) * s[2];
    m[10] = (1.0f - (xx + yy)) * s[2];
    m[11] = 0.0f;
    m[12] = t[0]; m[13] = t[1]; m[14] = t[2]; m[15] = 1.0f;
}

static void quat_nlerp(const float a[4], const float b[4], float t, float out[4]) {
    float dot = a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3];
    float s = dot < 0.0f ? -1.0f : 1.0f;
    float x = a[0] + (b[0]*s - a[0]) * t;
    float y = a[1] + (b[1]*s - a[1]) * t;
    float z = a[2] + (b[2]*s - a[2]) * t;
    float w = a[3] + (b[3]*s - a[3]) * t;
    float len = sqrtf(x*x + y*y + z*z + w*w);
    if (len > 1e-9f) { x/=len; y/=len; z/=len; w/=len; }
    out[0]=x; out[1]=y; out[2]=z; out[3]=w;
}

int wb_avatar_sample(const wb_animation *a, float t,
                     float out_local[][16], int max_nodes) {
    if (!a || !out_local || max_nodes <= 0) return 0;

    /* Accumulate each node's TRS starting from its rest pose, so a channel
     * that only drives rotation leaves the authored translation alone. */
    wb_trs trs[256];
    int n = max_nodes < 256 ? max_nodes : 256;
    for (int i = 0; i < n; i++) trs[i] = a->rest[i];

    if (t < 0.0f) t = 0.0f;
    if (a->duration > 0.0f && t > a->duration) t = a->duration;

    int written = 0;
    for (int c = 0; c < a->nchannels; c++) {
        const wb_anim_channel *ch = &a->channels[c];
        if (ch->node < 0 || ch->node >= n || ch->nkeys == 0) continue;

        /* find the surrounding keyframes */
        int k = 0;
        while (k + 1 < ch->nkeys && ch->times[k + 1] < t) k++;
        int k2 = (k + 1 < ch->nkeys) ? k + 1 : k;

        float alpha = 0.0f;
        if (k2 != k && ch->times[k2] > ch->times[k])
            alpha = (t - ch->times[k]) / (ch->times[k2] - ch->times[k]);

        wb_trs *r = &trs[ch->node];
        if (ch->path == 0) {
            for (int i = 0; i < 3; i++)
                r->t[i] = ch->values[k][i] + (ch->values[k2][i] - ch->values[k][i]) * alpha;
        } else if (ch->path == 1) {
            quat_nlerp(ch->values[k], ch->values[k2], alpha, r->q);
        } else {
            for (int i = 0; i < 3; i++)
                r->s[i] = ch->values[k][i] + (ch->values[k2][i] - ch->values[k][i]) * alpha;
        }
        written++;
    }

    for (int i = 0; i < n; i++)
        mat4_from_trs(out_local[i], trs[i].t, trs[i].q, trs[i].s);
    return written;
}
