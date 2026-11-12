/*
 * wb_audio_test.c -- gate for the synthesiser and mixer.
 *
 * The checks are about the three things that make this audio rather than noise:
 * a sound has a PITCH (asserted with YIN, not by loudness -- a loud fault is
 * still a fault), a DURATION (a sound that never ends is a drone), and a PLACE
 * (the same sound left and right is not the same sound, which is the whole
 * reason the mix is stereo).
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
#define FS   2400                     /* 0.1 s */

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

static double energy(const float *b, int n) {
    double e = 0.0;
    for (int i = 0; i < n; i++) e += (double)b[i] * b[i];
    return e;
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
    wb_audio_mix(NULL, NULL, NULL, 16);
    CHECK(1, "NULL is harmless");

    /* ---- silence in, silence out ----------------------------------------- */
    float l[512], r[512];
    wb_audio_mix(&a, l, r, 512);
    CHECK(nonzero(l, 512) == 0 && nonzero(r, 512) == 0,
          "a fresh mix is exactly zero on both channels");

    /* ---- a footstep is audible, low, short, and centred ------------------ */
    CHECK(wb_audio_emit(&a, WB_SND_FOOTSTEP) == 0, "a footstep is accepted");
    CHECK(wb_audio_emitted(&a) == 1, "the event is counted");
    CHECK(wb_audio_active(&a) == 1, "one voice is sounding");

    float *fl = malloc((size_t)FS * sizeof(float));
    float *fr = malloc((size_t)FS * sizeof(float));
    wb_audio_mix(&a, fl, fr, FS);
    CHECK(nonzero(fl, FS) > 0 && nonzero(fr, FS) > 0,
          "a footstep is audible in both ears");
    CHECK(peak(fl, FS) <= 1.0 && peak(fr, FS) <= 1.0,
          "a footstep never exceeds full scale");

    /* A footfall is an EVENT, not a drone: the end of the window is much
     * quieter than the beginning. (The room still rings, so this is a level
     * check -- exactly zero is the fresh-mix check above.) */
    double head = 0.0, tailw = 0.0;
    for (int i = 0; i < 400; i++) head += (double)fl[i] * fl[i];
    for (int i = FS - 400; i < FS; i++) tailw += (double)fl[i] * fl[i];
    CHECK(tailw < head * 0.25,
          "a footfall does not ring for the whole window");

    /* The pitch is the point: a footfall is a LOW thud. YIN must say so. The
     * pitch is measured over the sound's BODY, not the whole window: the room
     * keeps ringing after the footfall is over, and a tail is not a period. */
    double *d = malloc((size_t)FS * sizeof(double));
    for (int i = 0; i < FS / 2; i++) d[i] = fl[i];
    double f0 = wb_yin_f0(d, (size_t)(FS / 2), RATE);
    CHECK(f0 > 0.0, "a footstep is voiced");
    CHECK(f0 > 60.0 && f0 < 170.0,
          "a footstep is a low thud, not a whine");

    /* A centred sound has no head shadow and no interaural delay, so the two
     * ears must agree -- this is what makes a later left/right check mean
     * something. */
    double diff = 0.0, sum = 0.0;
    for (int i = 0; i < FS; i++) {
        diff += fabs((double)fl[i] - (double)fr[i]);
        sum += fabs((double)fl[i]);
    }
    CHECK(diff <= 0.02 * (sum + 1e-9),
          "a centred sound is the same in both ears");

    /* After its duration the VOICE is gone. The room still rings -- that is what
     * a reverb is for -- so the check is that what remains is the room rather
     * than the sound: at least 16 dB below the footfall's peak. Exactly zero is
     * the fresh-mix check above; a tail is not silence, but it is not the sound
     * either. */
    double fs_peak = peak(fl, FS);
    wb_audio_mix(&a, l, r, 512);
    double tail = 0.0;
    for (int i = 0; i < 512; i++)
        tail += (double)l[i] * l[i] + (double)r[i] * r[i];
    CHECK(wb_audio_active(&a) == 0, "no voices remain after the duration");
    CHECK(tail / 1024.0 < (fs_peak * 0.15) * (fs_peak * 0.15),
          "a finished voice leaves the room ringing, not the sound");
    free(d); free(fl); free(fr);

    /* ---- a sound is PLACED, not just played ------------------------------- */
    /* The same sound at the same distance and opposite azimuths must swap
     * ears. This is the entire difference between a mix and a space. */
    float *ll = malloc((size_t)FS * sizeof(float));
    float *lr = malloc((size_t)FS * sizeof(float));
    float *rl = malloc((size_t)FS * sizeof(float));
    float *rr = malloc((size_t)FS * sizeof(float));

    wb_audio left, right;
    wb_audio_init(&left, RATE);
    wb_audio_init(&right, RATE);
    CHECK(wb_audio_emit_at(&left, WB_SND_TAG, -90.0f, 0.0f, 1.0f) == 0,
          "a sound on the left is accepted");
    CHECK(wb_audio_emit_at(&right, WB_SND_TAG, 90.0f, 0.0f, 1.0f) == 0,
          "a sound on the right is accepted");
    wb_audio_mix(&left, ll, lr, FS);
    wb_audio_mix(&right, rl, rr, FS);
    double e_ll = energy(ll, FS), e_lr = energy(lr, FS);
    double e_rl = energy(rl, FS), e_rr = energy(rr, FS);
    CHECK(e_ll > e_lr * 4.0, "a sound on the left is louder in the left ear");
    CHECK(e_rr > e_rl * 4.0, "a sound on the right is louder in the right ear");

    /* Distance attenuates: twice as far away is a quarter of the pressure, so
     * a sixteenth of the energy. */
    wb_audio near, far;
    wb_audio_init(&near, RATE);
    wb_audio_init(&far, RATE);
    wb_audio_emit_at(&near, WB_SND_SCORE, 0.0f, 0.0f, 1.0f);
    wb_audio_emit_at(&far, WB_SND_SCORE, 0.0f, 0.0f, 4.0f);
    float *nl = malloc((size_t)FS * sizeof(float));
    float *nr = malloc((size_t)FS * sizeof(float));
    float *far_l = malloc((size_t)FS * sizeof(float));
    float *far_r = malloc((size_t)FS * sizeof(float));
    wb_audio_mix(&near, nl, nr, FS);
    wb_audio_mix(&far, far_l, far_r, FS);
    double e_near = energy(nl, FS) + energy(nr, FS);
    double e_far  = energy(far_l, FS) + energy(far_r, FS);
    CHECK(e_near > e_far * 16.0, "a sound four times as far is much quieter");

    /* Turning the head moves the sound. A source dead ahead becomes a source
     * on the left once the listener has turned right. */
    wb_audio still, turned;
    wb_audio_init(&still, RATE);
    wb_audio_init(&turned, RATE);
    wb_audio_emit_at(&still, WB_SND_TAG, 0.0f, 0.0f, 1.0f);
    wb_audio_emit_at(&turned, WB_SND_TAG, 0.0f, 0.0f, 1.0f);
    wb_audio_set_listener(&turned, 90.0f, 0.0f, 0.0f);
    float *sl = malloc((size_t)FS * sizeof(float));
    float *sr = malloc((size_t)FS * sizeof(float));
    float *tl = malloc((size_t)FS * sizeof(float));
    float *tr = malloc((size_t)FS * sizeof(float));
    wb_audio_mix(&still, sl, sr, FS);
    wb_audio_mix(&turned, tl, tr, FS);
    CHECK(energy(sl, FS) > 0.0 && energy(sr, FS) > 0.0,
          "a centred sound reaches both ears before the head turns");
    CHECK(energy(tl, FS) > energy(tr, FS) * 4.0,
          "turning the head right moves an ahead sound to the left ear");

    free(ll); free(lr); free(rl); free(rr);
    free(nl); free(nr); free(far_l); free(far_r);
    free(sl); free(sr); free(tl); free(tr);

    /* ---- the sounds are different from each other ------------------------ */
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
    wb_audio_mix(&b, l, r, 512);
    CHECK(peak(l, 512) <= 1.0 && peak(r, 512) <= 1.0,
          "the mix saturates at full scale on both channels");

    /* A ninth event is DROPPED, not silently truncating a sounding voice. */
    CHECK(wb_audio_emit(&b, WB_SND_TAG) == -1, "a full mixer refuses an event");
    CHECK(wb_audio_dropped(&b) == 1, "the refusal is counted");

    /* ---- an unknown sound id is refused ---------------------------------- */
    CHECK(wb_audio_emit(&a, (wb_snd)999) == -1, "an unknown sound is refused");
    CHECK(wb_audio_emit(&a, (wb_snd)-1) == -1, "a negative sound is refused");
    CHECK(wb_audio_emit_at(&a, (wb_snd)999, 0.0f, 0.0f, 1.0f) == -1,
          "an unknown sound is refused when placed");
    CHECK(wb_audio_sound_dur((wb_snd)999) == 0.0, "an unknown sound has no duration");

    /* ---- the rate is configurable --------------------------------------- */
    wb_audio c;
    wb_audio_init(&c, 0);
    CHECK(c.rate > 0, "a zero rate falls back to a sane default");
    wb_audio_init(&c, 48000);
    CHECK(c.rate == 48000, "a custom rate is honoured");

    wb_audio_free(&a); wb_audio_free(&b); wb_audio_free(&c);
    wb_audio_free(&left); wb_audio_free(&right);
    wb_audio_free(&near); wb_audio_free(&far);
    wb_audio_free(&still); wb_audio_free(&turned);

    printf("=== AUDIO TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
