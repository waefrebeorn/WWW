/*
 * wb_biquad.h -- biquad filters (lowpass / highpass / bandpass / notch) via the
 * RBJ audio EQ cookbook.
 *
 * Taken from the Big Mac repo (waefrebeorn/big-mac, src/wb_filter.c), the WuBu
 * musical agent's DAW. Same author, same no-third-party rule.
 *
 * This is what makes the engine's audio a SYNTHESISER rather than a tone
 * generator: a raw oscillator at a fixed frequency is a beep, and the same
 * oscillator through a lowpass that opens and closes is a sound with a body.
 *
 * C11, no deps beyond libm.
 */
#ifndef WB_BIQUAD_H
#define WB_BIQUAD_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wb_biquad {
    float sr;
    float b0, b1, b2, a1, a2;
    float x1, x2, y1, y2;
} wb_biquad;

void wb_biquad_init(wb_biquad *f, float sr);

/* type: 0 lowpass, 1 highpass, 2 bandpass, 3 notch. `gain_db` is reserved for
 * the shelf/peaking types and is currently ignored. */
void wb_biquad_set(wb_biquad *f, int type, float freq, float q, float gain_db);

float wb_biquad_process(wb_biquad *f, float x);

#ifdef __cplusplus
}
#endif

#endif /* WB_BIQUAD_H */
