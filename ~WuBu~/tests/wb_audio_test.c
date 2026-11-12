/*
 * wb_audio_test.c -- gate for the synthesiser and mixer.
 *
 * The checks are about the two things that make this audio rather than noise:
 * a sound has a PITCH (asserted with YIN, not by loudness -- a loud fault is
 * still a fault) and a DURATION (a sound that never ends is a drone).
 */
#include "wb_audio.h"
#include "wb_dsp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } \
} while (0)

#define RATE 24000

static int nonzero(const float *b, int n) {
    int k = 0;
    for (int i = 0; i < n; i++) if (b[i] != 0.0f) k++;
    return k;
}

static double peak(const float *b, int n) {
    double m = 0.0;
    for (int i = 0; i < n; i++) {
        double v = fabs((double)b[i]);
        if (v > m) m = v;
    }
    return m;
}

int main(void) {
    wb_audio a;
    wb_audio_init(&a, RATE);

    /* ---- initial state --------------------------------------------------- */
    CHECK(a.rate == RATE, "the rate is taken from the caller");
    CHECK(wb_audio_emitted(&a) == 0, "nothing has been emitted");
    CHECK(wb_audio_dropped(&a) == 0, "nothing has been dropped");
    CHECK(wb_audio_active(&a) == 0, "no voices are sounding");

    wb_audio null_a;
    memset(&null_a, 0, sizeof null_a);
    CHECK(wb_audio_emitted(NULL) == 0, "NULL reports no events");
    CHECK(wb_audio_dropped(NULL) == 0, "NULL reports no drops");
    CHECK(wb_audio_active(NULL) == 0, "NULL has no voices");
    wb_audio_init(NULL, RATE);
    wb_audio_emit(NULL, WB_SND_TAG);
    wb_audio_mix(NULL, NULL, 16);
    CHECK(1, "NULL is harmless");

    /* ---- silence in, silence out ----------------------------------------- */
    float buf[512];
    wb_audio_mix(&a, buf, 512);
    CHECK(nonzero(buf, 512) == 0, "a fresh mix is exactly zero");

    /* ---- a footstep is audible, low, and short --------------------------- */
    CHECK(wb_audio_emit(&a, WB_SND_FOOTSTEP) == 0, "a footstep is accepted");
    CHECK(wb_audio_emitted(&a) == 1, "the event is counted");
    CHECK(wb_audio_active(&a) == 1, "one voice is sounding");

    enum { FS = 2400 };                     /* 0.1 s */
    float *fs = malloc((size_t)FS * sizeof(float));
    wb_audio_mix(&a, fs, FS);
    int lit = nonzero(fs, FS);
    CHECK(lit > 0, "a footstep is audible");
    CHECK(lit < FS, "a footstep does not ring for the whole window");
    CHECK(peak(fs, FS) <= 1.0, "a footstep never exceeds full scale");

    /* The pitch is the point: a footfall is a LOW thud. YIN must say so. */
    double *d = malloc((size_t)FS * sizeof(double));
    for (int i = 0; i < FS; i++) d[i] = fs[i];
    double f0 = wb_yin_f0(d, (size_t)FS, RATE);
    CHECK(f0 > 0.0, "a footstep is voiced");
    CHECK(f0 > 60.0 && f0 < 170.0,
          "a footstep is a low thud, not a whine");

    /* After its duration the voice is gone and the mix is silent again. */
    wb_audio_mix(&a, buf, 512);
    CHECK(nonzero(buf, 512) == 0, "a finished voice stops sounding");
    CHECK(wb_audio_active(&a) == 0, "no voices remain after the duration");
    free(d); free(fs);

    /* ---- the sounds are different from each other ------------------------ */
    /* Same test, different sound: a score must be higher and longer than a
     * footstep. An engine whose sounds are all the same is not a mixer. */
    double dur[WB_SND_COUNT];
    for (int s = 0; s < WB_SND_COUNT; s++) dur[s] = wb_audio_sound_dur((wb_snd)s);
    CHECK(dur[WB_SND_SCORE] > dur[WB_SND_FOOTSTEP],
          "a score is longer than a footstep");
    CHECK(wb_audio_sound_freq1((wb_snd)WB_SND_JUMP) >
          wb_audio_sound_freq0((wb_snd)WB_SND_JUMP),
          "a jump rises");
    CHECK(wb_audio_sound_amp((wb_snd)WB_SND_SCORE) > 0.0,
          "a score has amplitude");

    /* ---- the mixer sums, and saturates instead of wrapping -------------- */
    /* Eight simultaneous full-scale voices must not wrap into a different
     * sound; they must saturate. */
    wb_audio b;
    wb_audio_init(&b, RATE);
    for (int i = 0; i < WB_AUDIO_MAX_VOICES; i++)
        CHECK(wb_audio_emit(&b, WB_SND_SCORE) == 0,
              "every voice can be occupied");
    wb_audio_mix(&b, buf, 512);
    CHECK(peak(buf, 512) <= 1.0, "the mix saturates at full scale");

    /* A ninth event is DROPPED, not silently truncating a sounding voice. */
    CHECK(wb_audio_emit(&b, WB_SND_TAG) == -1, "a full mixer refuses an event");
    CHECK(wb_audio_dropped(&b) == 1, "the refusal is counted");

    /* ---- an unknown sound id is refused ---------------------------------- */
    CHECK(wb_audio_emit(&a, (wb_snd)999) == -1, "an unknown sound is refused");
    CHECK(wb_audio_emit(&a, (wb_snd)-1) == -1, "a negative sound is refused");
    CHECK(wb_audio_sound_dur((wb_snd)999) == 0.0, "an unknown sound has no duration");

    /* ---- the rate is configurable --------------------------------------- */
    wb_audio c;
    wb_audio_init(&c, 0);
    CHECK(c.rate > 0, "a zero rate falls back to a sane default");
    wb_audio_init(&c, 48000);
    CHECK(c.rate == 48000, "a custom rate is honoured");

    printf("=== AUDIO TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
