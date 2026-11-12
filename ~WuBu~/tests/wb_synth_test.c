/*
 * wb_synth_test.c -- gate for the units taken from Big Mac.
 *
 * Each unit is checked against a signal whose answer is known by construction.
 * The oscillator must produce the waveform it claims (a sine's peak is 1, a
 * square's is 1, a saw spans the full range, a triangle's mean is positive); the
 * envelope must reach its sustain level and fall to zero on release; the biquad
 * must attenuate what it says it attenuates; the reverb must make a sound last
 * LONGER than the note that caused it -- that last one is the whole reason a game
 * engine wants a reverb.
 */
#include "wb_osc.h"
#include "wb_env.h"
#include "wb_biquad.h"
#include "wb_reverb.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } \
} while (0)

#define SR 24000

int main(void) {
    /* ---- oscillator ------------------------------------------------------ */
    /* Run one cycle at a known frequency and measure the waveform. */
    enum { N = 2400 };                      /* 0.1 s */
    const float inc = (float)(2.0 * 3.14159265358979 * 1000.0 / SR);

    wb_osc o;
    wb_osc_reset(&o);
    float sine_min = 1e9f, sine_max = -1e9f;
    for (int i = 0; i < N; i++) {
        float v = wb_osc_process(&o, inc, WB_WAVE_SINE, 0.5f);
        if (v < sine_min) sine_min = v;
        if (v > sine_max) sine_max = v;
    }
    CHECK(sine_max > 0.99f && sine_max < 1.01f, "a sine reaches +1");
    CHECK(sine_min < -0.99f && sine_min > -1.01f, "a sine reaches -1");
    /* One cycle is 24 samples at 1000 Hz; 2400 samples must be exactly 100
     * cycles, so the phase must come back to where it started. */
    CHECK(fabs(o.phase) < 1e-4, "the phase wraps cleanly after whole cycles");

    wb_osc_reset(&o);
    float sq_hi = 0.0f, sq_lo = 0.0f;
    for (int i = 0; i < N; i++) {
        float v = wb_osc_process(&o, inc, WB_WAVE_SQUARE, 0.5f);
        if (v > sq_hi) sq_hi = v;
        if (v < sq_lo) sq_lo = v;
    }
    CHECK(sq_hi > 0.99f && sq_lo < -0.99f, "a square swings the full range");
    /* A square at 50% duty spends half its time high. */
    wb_osc_reset(&o);
    int high = 0;
    for (int i = 0; i < N; i++)
        if (wb_osc_process(&o, inc, WB_WAVE_SQUARE, 0.5f) > 0.0f) high++;
    CHECK(high > N * 45 / 100 && high < N * 55 / 100,
          "a 50% square is high half the time");

    /* The pulse width must actually change the duty cycle -- a square that
     * ignores `shape` is a two-level oscillator, not a pulse generator. */
    wb_osc_reset(&o);
    int narrow = 0;
    for (int i = 0; i < N; i++)
        if (wb_osc_process(&o, inc, WB_WAVE_SQUARE, 0.1f) > 0.0f) narrow++;
    CHECK(narrow < high, "a narrower pulse spends less time high");

    wb_osc_reset(&o);
    float saw_min = 1e9f, saw_max = -1e9f;
    for (int i = 0; i < N; i++) {
        float v = wb_osc_process(&o, inc, WB_WAVE_SAW, 0.5f);
        if (v < saw_min) saw_min = v;
        if (v > saw_max) saw_max = v;
    }
    CHECK(saw_max > 0.9f && saw_min < -0.9f, "a saw spans the full range");

    /* A triangle is symmetric about zero: its mean over whole cycles is ~0. */
    wb_osc_reset(&o);
    double tri_sum = 0.0;
    for (int i = 0; i < N; i++)
        tri_sum += wb_osc_process(&o, inc, WB_WAVE_TRIANGLE, 0.5f);
    CHECK(fabs(tri_sum / N) < 0.05, "a triangle is symmetric about zero");

    /* Noise must be roughly zero-mean and bounded, and must not be a constant. */
    wb_noise_seed(12345u);
    double nz = 0.0;
    int distinct = 0;
    float prev = 0.0f;
    for (int i = 0; i < N; i++) {
        float v = wb_noise_next();
        nz += v;
        if (fabs(v) > 1.0f) distinct = -9999;      /* out of range */
        if (i > 0 && v != prev) distinct++;
        prev = v;
    }
    CHECK(fabs(nz / N) < 0.2, "noise is roughly zero-mean");
    CHECK(distinct > N / 2, "noise is not a constant");

    /* Seeding the same value must reproduce the same sequence: the engine's
     * output has to replay. */
    float a1, a2, b1, b2;
    wb_noise_seed(99u); a1 = wb_noise_next(); a2 = wb_noise_next();
    wb_noise_seed(99u); b1 = wb_noise_next(); b2 = wb_noise_next();
    CHECK(a1 == b1 && a2 == b2, "the noise source is reproducible once seeded");
    wb_noise_seed(0u);
    CHECK(wb_noise_next() != 0.0f, "seeding with zero falls back, not to silence");

    /* An unknown waveform is silence, not a crash. */
    wb_osc_reset(&o);
    CHECK(wb_osc_process(&o, inc, 999, 0.5f) == 0.0f, "an unknown waveform is silent");
    CHECK(wb_osc_process(NULL, inc, WB_WAVE_SINE, 0.5f) == 0.0f, "a NULL oscillator is silent");

    /* ---- ADSR envelope --------------------------------------------------- */
    wb_env e;
    wb_env_init(&e, (float)SR);
    CHECK(e.stage == 0 && e.level == 0.0, "a fresh envelope is idle and silent");

    wb_env_note_on(&e, 0.01f, 0.05f, 0.5f, 0.1f);
    CHECK(e.stage == 1, "note_on starts the attack");

    /* Run the attack: 0.01 s is 240 samples. The level must climb to 1. */
    float peak = 0.0f;
    for (int i = 0; i < 240; i++) {
        float v = wb_env_process(&e);
        if (v > peak) peak = v;
    }
    CHECK(peak > 0.99f, "the attack reaches full level");

    /* Then the decay must bring it to the sustain level. d is 0.05 s, which is
     * 1200 samples; run past it so the stage has ended, not just progressed. */
    for (int i = 0; i < 1300; i++) wb_env_process(&e);
    CHECK(e.stage == 3, "decay settles into sustain");
    CHECK(fabs(e.level - 0.5f) < 0.02f, "the sustain level is held");

    /* note_off must release to silence. The release runs three time constants in
     * r seconds, so it crosses the silence floor at ~2.8*r = ~6800 samples. */
    wb_env_note_off(&e);
    CHECK(e.stage == 4, "note_off starts the release");
    int released = 0;
    for (int i = 0; i < 8000; i++) {
        float v = wb_env_process(&e);
        if (v == 0.0f && e.stage == 0) { released = 1; break; }
    }
    CHECK(released, "the release reaches silence");

    /* A zero-length attack must not divide by zero or hang. */
    wb_env_init(&e, (float)SR);
    wb_env_note_on(&e, 0.0f, 0.0f, 0.0f, 0.0f);
    float v = wb_env_process(&e);
    CHECK(!isnan(v) && !isinf(v), "a zero-length envelope does not produce NaN");

    /* ---- biquad filter --------------------------------------------------- */
    wb_biquad f;
    wb_biquad_init(&f, (float)SR);
    wb_biquad_set(&f, 0, 200.0f, 0.7f, 0.0f);      /* lowpass at 200 Hz */

    /* A 100 Hz sine is passband for this filter: its amplitude must survive.
     * A 2000 Hz sine is stopband: it must be attenuated. */
    const float inc_lo = (float)(2.0 * 3.14159265358979 * 100.0 / SR);
    const float inc_hi = (float)(2.0 * 3.14159265358979 * 2000.0 / SR);
    wb_osc_reset(&o);
    double lo_in = 0.0, lo_out = 0.0;
    for (int i = 0; i < N; i++) {
        float x = wb_osc_process(&o, inc_lo, WB_WAVE_SINE, 0.5f);
        float y = wb_biquad_process(&f, x);
        if (i > 200) { lo_in += (double)x * x; lo_out += (double)y * y; }
    }
    CHECK(lo_out > 0.5 * lo_in, "a lowpass passes its passband");

    wb_biquad_init(&f, (float)SR);
    wb_biquad_set(&f, 0, 200.0f, 0.7f, 0.0f);
    wb_osc_reset(&o);
    double hi_in = 0.0, hi_out = 0.0;
    for (int i = 0; i < N; i++) {
        float x = wb_osc_process(&o, inc_hi, WB_WAVE_SINE, 0.5f);
        float y = wb_biquad_process(&f, x);
        if (i > 200) { hi_in += (double)x * x; hi_out += (double)y * y; }
    }
    CHECK(hi_out < 0.1 * hi_in, "a lowpass attenuates its stopband");

    /* A highpass must do the opposite: it kills the low tone. */
    wb_biquad_init(&f, (float)SR);
    wb_biquad_set(&f, 1, 2000.0f, 0.7f, 0.0f);
    wb_osc_reset(&o);
    double hp_lo_out = 0.0;
    for (int i = 0; i < N; i++) {
        float x = wb_osc_process(&o, inc_lo, WB_WAVE_SINE, 0.5f);
        float y = wb_biquad_process(&f, x);
        if (i > 200) hp_lo_out += (double)y * y;
    }
    CHECK(hp_lo_out < 0.1 * lo_in, "a highpass kills its stopband");

    /* A silent input must produce silence, and the filter must not blow up. */
    wb_biquad_init(&f, (float)SR);
    wb_biquad_set(&f, 0, 200.0f, 0.7f, 0.0f);
    float quiet = 0.0f;
    for (int i = 0; i < N; i++) quiet = wb_biquad_process(&f, 0.0f);
    CHECK(quiet == 0.0f, "a silent input stays silent");

    /* ---- reverb ---------------------------------------------------------- */
    wb_reverb r;
    wb_reverb_init(&r, SR);

    /* Feed one impulse, then measure how long the output keeps going. Without
     * reverb an impulse is one sample; with an FDN it is a tail. */
    float impulse[4000];
    memset(impulse, 0, sizeof impulse);
    impulse[0] = 1.0f;
    wb_reverb_set(&r, 1, 1.0f);            /* fully wet, to measure the tail */
    wb_reverb_process(&r, impulse, 4000);

    int last_nonzero = 0;
    double tail_energy = 0.0;
    for (int i = 1; i < 4000; i++) {
        if (impulse[i] != 0.0f) last_nonzero = i;
        tail_energy += (double)impulse[i] * impulse[i];
    }
    CHECK(last_nonzero > 100, "a reverb makes a sound outlast its note");
    CHECK(tail_energy > 0.0, "the reverb tail carries energy");
    /* The tail must DECAY, not ring forever. The window is split into quarters
     * and the last is compared with the LOUDEST: a reverb that rang would have
     * them equal. A fixed pair of offsets is wrong here -- the combs are
     * 29.7-43.7 ms, so the first echoes land near sample 700 and an "early"
     * window starting at sample 1 measures silence, which proves nothing. */
    double q[4] = { 0.0, 0.0, 0.0, 0.0 };
    for (int i = 1; i < 4000; i++)
        q[i / 1000] += (double)impulse[i] * impulse[i];
    int loudest = 0;
    for (int i = 1; i < 4; i++) if (q[i] > q[loudest]) loudest = i;
    CHECK(q[3] < q[loudest], "the reverb tail decays rather than ringing forever");
    /* And it must stay bounded: an FDN with feedback < 1 cannot run away. */
    float peak_tail = 0.0f;
    for (int i = 0; i < 4000; i++) {
        float a = fabsf(impulse[i]);
        if (a > peak_tail) peak_tail = a;
    }
    CHECK(peak_tail <= 1.0f, "the reverb tail stays within full scale");

    /* A mix of 0 must leave the signal exactly alone. */
    wb_reverb_init(&r, SR);
    wb_reverb_set(&r, 1, 0.0f);
    float dry[64];
    for (int i = 0; i < 64; i++) dry[i] = sinf((float)i * 0.1f);
    float saved[64];
    memcpy(saved, dry, sizeof dry);
    wb_reverb_process(&r, dry, 64);
    int identical = 1;
    for (int i = 0; i < 64; i++) if (dry[i] != saved[i]) identical = 0;
    CHECK(identical, "a zero wet mix leaves the signal untouched");

    /* Silence in, silence out -- a reverb does not invent sound. */
    wb_reverb_init(&r, SR);
    wb_reverb_set(&r, 1, 1.0f);
    float sil[512];
    memset(sil, 0, sizeof sil);
    wb_reverb_process(&r, sil, 512);
    double sil_sum = 0.0;
    for (int i = 0; i < 512; i++) sil_sum += fabs((double)sil[i]);
    CHECK(sil_sum == 0.0, "a silent input produces a silent tail");

    /* A freed reverb must not be used again. */
    wb_reverb_free(&r);
    float x = 1.0f;
    wb_reverb_process(&r, &x, 1);
    CHECK(x == 1.0f, "a freed reverb is inert rather than undefined");
    /* A NULL reverb is refused without touching the signal. */
    x = 1.0f;
    wb_reverb_process(NULL, &x, 1);
    CHECK(x == 1.0f, "a NULL reverb is refused");

    printf("=== SYNTH TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
