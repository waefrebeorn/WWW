/*
 * wb_audio.c -- a synthesiser and a mixer. See wb_audio.h for why this is small.
 */
#include "wb_audio.h"
#include <math.h>
#include <string.h>

#define WB_TWO_PI 6.283185307179586476925286766559
#define WB_ATTACK  0.005   /* seconds; short enough not to click */

typedef struct {
    double f0, f1;   /* glide, Hz */
    double dur;      /* seconds */
    double amp;      /* peak amplitude, before the envelope */
} wb_snd_def;

static const wb_snd_def WB_SNDS[WB_SND_COUNT] = {
    /* A footfall is a short low thud: no glide to speak of, 80 ms. */
    [WB_SND_FOOTSTEP] = { 110.0,  90.0, 0.08, 0.30 },
    /* A jump rises -- the glide IS the meaning. */
    [WB_SND_JUMP]     = { 300.0, 520.0, 0.12, 0.35 },
    /* A landing is the same family as a footstep but harder and longer. */
    [WB_SND_LAND]     = {  95.0,  60.0, 0.10, 0.40 },
    /* A tag is a bright, steady ping: no glide, so it reads as a note. */
    [WB_SND_TAG]      = { 660.0, 660.0, 0.16, 0.45 },
    /* A score is the longest sound, and rises: the reward should feel bigger
     * than the action that caused it. */
    [WB_SND_SCORE]    = { 523.0, 784.0, 0.40, 0.50 },
};

void wb_audio_init(wb_audio *a, int rate) {
    if (!a) return;
    memset(a, 0, sizeof *a);
    a->rate = rate > 0 ? rate : 24000;
}

int wb_audio_emit(wb_audio *a, wb_snd s) {
    if (!a || s < 0 || s >= WB_SND_COUNT) return -1;
    for (int v = 0; v < WB_AUDIO_MAX_VOICES; v++) {
        if (a->voices[v].active) continue;
        a->voices[v].active = 1;
        a->voices[v].sound = s;
        a->voices[v].t = 0.0;
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

/* One voice at its current time. The phase is the analytic integral of the glide
 * f0 -> f1, so the frequency is continuous across the note rather than stepping
 * between samples -- a stepped glide is audible as a buzz. */
static double wb_voice_sample(const wb_voice *v) {
    const wb_snd_def *d = &WB_SNDS[v->sound];
    double u = v->t / d->dur;
    if (u < 0.0 || u >= 1.0) return 0.0;
    double phase = WB_TWO_PI * (d->f0 * v->t
                                + (d->f1 - d->f0) * v->t * v->t
                                  / (2.0 * d->dur));
    double attack = v->t < WB_ATTACK ? v->t / WB_ATTACK : 1.0;
    double decay = (1.0 - u) * (1.0 - u);
    return d->amp * attack * decay * sin(phase);
}

void wb_audio_mix(wb_audio *a, float *out, int nframes) {
    if (!a || !out || nframes <= 0) return;
    double dt = 1.0 / (double)a->rate;
    for (int i = 0; i < nframes; i++) {
        double s = 0.0;
        for (int v = 0; v < WB_AUDIO_MAX_VOICES; v++) {
            wb_voice *vo = &a->voices[v];
            if (!vo->active) continue;
            s += wb_voice_sample(vo);
            vo->t += dt;
            if (vo->t >= WB_SNDS[vo->sound].dur) {
                vo->active = 0;
                vo->t = 0.0;
            }
        }
        /* Soft-clip: a loud moment must not wrap, it must saturate. */
        if (s >  1.0) s =  1.0;
        if (s < -1.0) s = -1.0;
        out[i] = (float)s;
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
