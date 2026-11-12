/*
 * wb_audio.c -- a synthesiser and a mixer. See wb_audio.h for why this is the
 * seam between the game and Big Mac's DSP units.
 */
#include "wb_audio.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define WB_TWO_PI 6.283185307179586476925286766559

typedef struct {
    double f0, f1;   /* glide, Hz */
    double dur;      /* seconds, total */
    double amp;      /* peak amplitude, before the envelope */
    int    wave;     /* WB_WAVE_* */
    float  a, d;     /* attack, decay -- seconds */
    float  s;        /* sustain level, 0..1 */
    float  r;        /* release -- seconds */
} wb_snd_def;

/* The envelope times are chosen so a + d + hold + r == dur, where hold is
 * computed at emit time. A one-shot sound is an attack, a decay, a brief hold,
 * then a release -- the same shape Big Mac uses for a note, cut to a length. */
static const wb_snd_def WB_SNDS[WB_SND_COUNT] = {
    /* A footfall is a short low thud on a triangle: no glide to speak of. It
     * lasts long enough to have a pitch at all -- a footfall shorter than about
     * four periods is a click, and neither a player nor a pitch detector can
     * tell a low thud from a whine. */
    [WB_SND_FOOTSTEP] = { 110.0,  90.0, 0.10, 0.34, WB_WAVE_TRIANGLE,
                          0.002f, 0.060f, 0.0f, 0.020f },
    /* A jump rises on a square -- the glide IS the meaning. */
    [WB_SND_JUMP]     = { 300.0, 520.0, 0.12, 0.22, WB_WAVE_SQUARE,
                          0.003f, 0.045f, 0.0f, 0.055f },
    /* A landing is the same family as a footstep but harder and longer. */
    [WB_SND_LAND]     = {  95.0,  60.0, 0.10, 0.40, WB_WAVE_SINE,
                          0.002f, 0.035f, 0.0f, 0.045f },
    /* A tag is a bright, steady ping on a square: no glide, so it reads as a note. */
    [WB_SND_TAG]      = { 660.0, 660.0, 0.16, 0.26, WB_WAVE_SQUARE,
                          0.002f, 0.050f, 0.0f, 0.090f },
    /* A score is the longest sound, and rises on a saw: the reward should feel
     * bigger than the action that caused it. It is the one sound that SUSTAINS. */
    [WB_SND_SCORE]    = { 523.0, 784.0, 0.40, 0.34, WB_WAVE_SAW,
                          0.010f, 0.100f, 0.20f, 0.150f },
};

void wb_audio_init(wb_audio *a, int rate) {
    if (!a) return;
    memset(a, 0, sizeof *a);
    a->rate = rate > 0 ? rate : 24000;
    wb_biquad_init(&a->filter_l, (float)a->rate);
    wb_biquad_init(&a->filter_r, (float)a->rate);
    wb_audio_set_filter(a, 0, 8000.0f, 0.7f);
    wb_reverb_init(&a->reverb, (uint32_t)a->rate);
    wb_audio_set_reverb(a, 0.7f, 0.18f);
    /* Every voice gets its own panner: a voice is a thing in a place, and two
     * sounds at two places cannot share one set of ear delays. */
    for (int v = 0; v < WB_AUDIO_MAX_VOICES; v++) {
        a->voices[v].atmos = wb_atmos_create((uint32_t)a->rate);
    }
    wb_audio_set_room(a, 0.15f, 8.0f);
    /* The noise source is a global xorshift; seed it so a run replays. */
    wb_noise_seed(0x9E3779B9u);
}

void wb_audio_free(wb_audio *a) {
    if (!a) return;
    for (int v = 0; v < WB_AUDIO_MAX_VOICES; v++) {
        wb_atmos_free(a->voices[v].atmos);
        a->voices[v].atmos = NULL;
    }
    wb_reverb_free(&a->reverb);
    free(a->scratch);
    a->scratch = NULL;
    a->scratch_n = 0;
}

int wb_audio_emit(wb_audio *a, wb_snd s) {
    return wb_audio_emit_at(a, s, 0.0f, 0.0f, 1.0f);
}

int wb_audio_emit_at(wb_audio *a, wb_snd s, float az, float el, float dist) {
    if (!a || s < 0 || s >= WB_SND_COUNT) return -1;
    const wb_snd_def *d = &WB_SNDS[s];
    for (int v = 0; v < WB_AUDIO_MAX_VOICES; v++) {
        if (a->voices[v].active) continue;
        wb_voice *vo = &a->voices[v];
        vo->active = 1;
        vo->sound = s;
        vo->t = 0.0;
        vo->inc = 0.0;
        vo->azimuth = az;
        vo->elevation = el;
        vo->distance = dist;
        wb_osc_reset(&vo->osc);
        wb_env_init(&vo->env, (float)a->rate);
        wb_env_note_on(&vo->env, d->a, d->d, d->s, d->r);
        /* Hold whatever time is left after the attack, decay and release, so the
         * envelope's total length is exactly the sound's duration. */
        double fixed = (double)d->a + (double)d->d + (double)d->r;
        vo->hold = (d->dur > fixed) ? d->dur - fixed : 0.0;
        a->emitted++;
        return 0;
    }
    a->dropped++;
    return -1;
}

uint64_t wb_audio_emitted(const wb_audio *a) { return a ? a->emitted : 0; }
uint64_t wb_audio_dropped(const wb_audio *a)  { return a ? a->dropped : 0; }

int wb_audio_active(const wb_audio *a) {
    if (!a) return 0;
    int n = 0;
    for (int v = 0; v < WB_AUDIO_MAX_VOICES; v++)
        if (a->voices[v].active) n++;
    return n;
}

void wb_audio_set_listener(wb_audio *a, float yaw, float pitch, float roll) {
    if (!a) return;
    a->yaw = yaw;
    a->pitch = pitch;
    a->roll = roll;
}

void wb_audio_set_room(wb_audio *a, float reverb_level, float room_size) {
    if (!a) return;
    for (int v = 0; v < WB_AUDIO_MAX_VOICES; v++)
        if (a->voices[v].atmos)
            wb_atmos_set_room(a->voices[v].atmos, reverb_level, room_size);
}

void wb_audio_set_filter(wb_audio *a, int type, float freq, float q) {
    if (!a) return;
    wb_biquad_set(&a->filter_l, type, freq, q, 0.0f);
    wb_biquad_set(&a->filter_r, type, freq, q, 0.0f);
}

void wb_audio_set_reverb(wb_audio *a, float feedback, float mix) {
    if (!a) return;
    wb_reverb_set(&a->reverb, 0, feedback);
    wb_reverb_set(&a->reverb, 1, mix);
}

void wb_audio_mix(wb_audio *a, float *out_l, float *out_r, int nframes) {
    if (!a || !out_l || !out_r || nframes <= 0) return;
    /* Scratch: the mono render, then the two panned channels. Three times the
     * block, reused for every voice rather than allocated per call. */
    if (!a->scratch || a->scratch_n < nframes) {
        free(a->scratch);
        a->scratch = (float *)malloc((size_t)nframes * 3 * sizeof(float));
        a->scratch_n = a->scratch ? nframes : 0;
    }
    if (!a->scratch) return;
    float *mono  = a->scratch;
    float *tmp_l = a->scratch + nframes;
    float *tmp_r = a->scratch + 2 * (size_t)nframes;

    for (int i = 0; i < nframes; i++) { out_l[i] = 0.0f; out_r[i] = 0.0f; }

    double dt = 1.0 / (double)a->rate;
    for (int v = 0; v < WB_AUDIO_MAX_VOICES; v++) {
        wb_voice *vo = &a->voices[v];
        if (!vo->active) continue;
        const wb_snd_def *d = &WB_SNDS[vo->sound];
        /* Re-place the voice once per block: the listener may have turned their
         * head since the sound started, and a voice that stays in front of you
         * while you turn is a voice that is not in the world. */
        if (vo->atmos)
            wb_atmos_set_position(vo->atmos, vo->azimuth - a->yaw,
                                  vo->elevation, vo->distance);

        for (int i = 0; i < nframes; i++) {
            double u = (d->dur > 0.0) ? vo->t / d->dur : 1.0;
            if (u > 1.0) u = 1.0;
            double f = d->f0 + (d->f1 - d->f0) * u;
            vo->inc = (float)(WB_TWO_PI * f / (double)a->rate);
            float osc = wb_osc_process(&vo->osc, (float)vo->inc, d->wave, 0.5f);
            float env = wb_env_process(&vo->env);
            mono[i] = (float)(d->amp * osc * env);
            vo->t += dt;
            if (vo->hold > 0.0 &&
                vo->t >= (double)d->a + (double)d->d + vo->hold) {
                wb_env_note_off(&vo->env);
                vo->hold = 0.0;
            }
            if (vo->env.stage == 0 && vo->t > dt) {
                vo->active = 0;
                vo->t = 0.0;
                for (int j = i + 1; j < nframes; j++) mono[j] = 0.0f;
                break;
            }
        }

        for (int i = 0; i < nframes; i++) { tmp_l[i] = 0.0f; tmp_r[i] = 0.0f; }
        if (vo->atmos)
            wb_atmos_process(vo->atmos, mono, tmp_l, tmp_r, (uint32_t)nframes);
        else
            for (int i = 0; i < nframes; i++) { tmp_l[i] = mono[i]; tmp_r[i] = mono[i]; }
        for (int i = 0; i < nframes; i++) { out_l[i] += tmp_l[i]; out_r[i] += tmp_r[i]; }
    }

    /* The master chain: a lowpass, then a stereo reverb send. Both Big Mac's. */
    for (int i = 0; i < nframes; i++) {
        out_l[i] = wb_biquad_process(&a->filter_l, out_l[i]);
        out_r[i] = wb_biquad_process(&a->filter_r, out_r[i]);
    }
    wb_reverb_process_stereo(&a->reverb, out_l, out_r, (uint32_t)nframes);
    /* Soft-clip: a loud moment must not wrap, it must saturate. */
    for (int i = 0; i < nframes; i++) {
        float l = out_l[i], r = out_r[i];
        if (l >  1.0f) l =  1.0f;
        if (l < -1.0f) l = -1.0f;
        if (r >  1.0f) r =  1.0f;
        if (r < -1.0f) r = -1.0f;
        out_l[i] = l;
        out_r[i] = r;
    }
}

double wb_audio_sound_freq0(wb_snd s) {
    return (s >= 0 && s < WB_SND_COUNT) ? WB_SNDS[s].f0 : 0.0;
}
double wb_audio_sound_freq1(wb_snd s) {
    return (s >= 0 && s < WB_SND_COUNT) ? WB_SNDS[s].f1 : 0.0;
}
double wb_audio_sound_dur(wb_snd s) {
    return (s >= 0 && s < WB_SND_COUNT) ? WB_SNDS[s].dur : 0.0;
}
double wb_audio_sound_amp(wb_snd s) {
    return (s >= 0 && s < WB_SND_COUNT) ? WB_SNDS[s].amp : 0.0;
}
int wb_audio_sound_wave(wb_snd s) {
    return (s >= 0 && s < WB_SND_COUNT) ? WB_SNDS[s].wave : WB_WAVE_SINE;
}
