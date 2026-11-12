/* wb_env.c — ADSR envelope generator. */

#include "wb_env.h"

/* The per-sample fraction of the way to the stage's target. Clamped to 1/2 so a
 * very short stage cannot overshoot and ring -- a decay must approach, never
 * pass through, its target. */
static float wb_env_rate(float seconds, float sr) {
    float k = WB_ENV_SETTLE / (seconds * sr + 1e-9f);
    return k > 0.5f ? 0.5f : k;
}

void wb_env_init(wb_env *e, float sr) {
    e->sr = sr;
    e->level = 0.0;
    e->stage = 0;
    e->note_on = 0;
    e->a = e->d = e->s = e->r = 0.0;
    e->cur = 0.0;
}

void wb_env_note_on(wb_env *e, float a, float d, float s, float r) {
    e->a = a; e->d = d; e->s = s; e->r = r;
    e->stage = 1;           /* attack */
    e->cur = 0.0;
    e->note_on = 1;
}

void wb_env_note_off(wb_env *e) {
    if (e->stage > 0 && e->stage < 5) {
        e->stage = 4;       /* release */
        e->cur = 0.0;
    }
    e->note_on = 0;
}

float wb_env_process(wb_env *e) {
    float sr = e->sr;
    switch (e->stage) {
    case 0: /* idle */
        e->level = 0.0;
        break;
    case 1: /* attack: linear to 1 over a seconds */
        e->level += (1.0f / (e->a * sr + 1e-9f));
        if (e->level >= 1.0f) { e->level = 1.0f; e->stage = 2; e->cur = 0; }
        break;
    case 2: /* decay: exponential to sustain, settled within d seconds */
        if (e->d > 0) {
            e->level += (e->s - e->level) * wb_env_rate(e->d, sr);
            if (e->cur > e->d * sr) { e->level = e->s; e->stage = 3; }
        } else { e->stage = 3; e->level = e->s; }
        e->cur += 1.0;
        break;
    case 3: /* sustain */
        e->level = e->s;
        break;
    case 4: /* release: exponential to the silence floor, over within ~2r */
        if (e->r > 0) {
            e->level *= 1.0f - wb_env_rate(e->r, sr);
            if (e->level < 1e-4f) { e->level = 0; e->stage = 0; }
        } else { e->level = 0; e->stage = 0; }
        break;
    default:
        break;
    }
    return (float)e->level;
}
