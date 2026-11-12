/*
 * wb_osc.c -- oscillators. Taken from Big Mac (waefrebeorn/big-mac, src/wb_osc.c);
 * the wavetable and SSE2 batch paths are dropped -- see wb_osc.h.
 */
#include "wb_osc.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void wb_osc_reset(wb_osc *o) {
    if (!o) return;
    o->phase = 0.0;
    o->phase_inc = 0.0;
    o->last_out = 0.0;
}

/* poly-BLEP residual for band-limited edges. Without it the discontinuity at the
 * wrap aliases into the audible band. */
static float blep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    } else if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

float wb_osc_process(wb_osc *o, float inc, int waveform, float shape) {
    if (!o) return 0.0f;
    o->phase += inc;
    if (o->phase >= 2.0 * M_PI) o->phase -= 2.0 * M_PI;
    if (o->phase < 0) o->phase += 2.0 * M_PI;

    float t = (float)(o->phase / (2.0 * M_PI));   /* 0..1 within the cycle */
    float dt = inc / (float)(2.0 * M_PI);
    float out = 0.0f;

    switch (waveform) {
    case WB_WAVE_SINE:
        out = (float)sin(o->phase);
        break;
    case WB_WAVE_SAW:
        out = 2.0f * t - 1.0f;      /* naive saw */
        out -= blep(t, dt);         /* poly-BLEP correction at the wrap */
        break;
    case WB_WAVE_SQUARE: {
        float pw = (shape < 0.01f) ? 0.5f : shape;   /* pulse width 0..1 */
        out = (t < pw) ? 1.0f : -1.0f;
        out += blep(t, dt);
        out -= blep(fmodf(t + 1.0f - pw, 1.0f), dt);
        break;
    }
    case WB_WAVE_TRIANGLE:
        /* Bipolar, like the other waveforms. Big Mac's version normalised this
         * to 0..1, which put a +0.5 DC offset on the one waveform that had it --
         * and DC through a synth is a click at the note boundary. */
        out = 4.0f * fabsf(t - 0.5f) - 1.0f;
        break;
    case WB_WAVE_NOISE:
        out = wb_noise_next();
        break;
    default:
        out = 0.0f;
        break;
    }
    o->last_out = out;
    return out;
}

static unsigned long rng_state = 0x9E3779B9u;

void wb_noise_seed(unsigned long s) { rng_state = s ? s : 0x9E3779B9u; }

float wb_noise_next(void) {
    /* xorshift32: no libm, no state beyond one word, and reproducible once
     * seeded -- which matters because the engine's output must replay. */
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return (float)((double)(rng_state & 0xFFFFFF) / 8388608.0 - 1.0);
}
