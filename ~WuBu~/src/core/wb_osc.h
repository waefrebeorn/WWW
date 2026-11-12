/*
 * wb_osc.h -- oscillators: sine, saw, square, triangle, noise.
 *
 * Taken from the Big Mac repo (waefrebeorn/big-mac, src/wb_osc.c), the WuBu
 * musical agent's DAW. Same author, same no-third-party rule.
 *
 * What was DROPPED, and why: Big Mac's oscillator also carries a mipmapped
 * wavetable path and an SSE2 batch path (wb_osc_process_wt4). The wavetable is
 * built at first use by a 2048-point FFT -- a heavy init for a sound effect, and
 * the SSE2 path needs intrinsics the engine's portable floor does not assume.
 * The scalar waveforms here are the same oscillators; a game effect does not
 * need a wavetable to be audible.
 *
 * The saw and square use poly-BLEP (a parabolic residual at the discontinuity)
 * rather than a naive edge. A naive saw aliases audibly; at 24 kHz the alias
 * lands inside the audible band, which is the difference between a synth and a
 * buzz.
 *
 * C11, no deps beyond libm.
 */
#ifndef WB_OSC_H
#define WB_OSC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wb_osc {
    double phase;     /* radians, wrapped to [0, 2pi) */
    double phase_inc; /* radians per sample, set by the caller */
    double last_out;
} wb_osc;

#define WB_WAVE_SINE     0
#define WB_WAVE_SAW      1
#define WB_WAVE_SQUARE   2
#define WB_WAVE_TRIANGLE 3
#define WB_WAVE_NOISE    4

void  wb_osc_reset(wb_osc *o);

/* One sample of the requested waveform, advancing the phase by `inc` radians.
 * `shape` is the square's pulse width (0..1, default 0.5). */
float wb_osc_process(wb_osc *o, float inc, int waveform, float shape);

/* The noise source is a global xorshift32, so it must be SEEDED for a run to be
 * reproducible. The engine seeds it from wb_audio_init. */
void   wb_noise_seed(unsigned long s);
float  wb_noise_next(void);

#ifdef __cplusplus
}
#endif

#endif /* WB_OSC_H */
