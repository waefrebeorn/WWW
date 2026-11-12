/*
 * wb_reverb.h -- Schroeder feedback-delay-network reverb.
 *
 * Taken from the Big Mac repo (waefrebeorn/big-mac, src/wb_reverb.c), the WuBu
 * musical agent's DAW. Same author, same no-third-party rule.
 *
 * What was DROPPED, and why: Big Mac's reverb is stereo and its two
 * `*_inplace_wet` helpers copy into fixed 4096-sample stack buffers -- an
 * overflow waiting for any block larger than 4096. The engine's mix is mono, so
 * it gets a mono entry point instead: the same signal drives both channels and
 * the result is averaged. The algorithm is unchanged.
 *
 * Why a game engine wants reverb at all: without it every sound is dry and
 * frontal, and a stadium sounds like a beeper. A short FDN tail puts the same
 * sound in a place.
 *
 * C11, no deps beyond libm.
 */
#ifndef WB_REVERB_H
#define WB_REVERB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WB_REVERB_NCOMBS    4
#define WB_REVERB_NALLPASS  2

typedef struct wb_reverb {
    uint32_t sr;
    float   *combs[WB_REVERB_NCOMBS][2];   /* [comb][channel] */
    uint32_t comb_len[WB_REVERB_NCOMBS];
    uint32_t comb_pos[WB_REVERB_NCOMBS][2];
    float   *allpass[WB_REVERB_NALLPASS][2];
    uint32_t ap_len[WB_REVERB_NALLPASS];
    uint32_t ap_pos[WB_REVERB_NALLPASS][2];
    float    feedback;
    float    mix;                           /* 0 = dry, 1 = wet */
} wb_reverb;

void  wb_reverb_init(wb_reverb *r, uint32_t sr);
void  wb_reverb_free(wb_reverb *r);

/* Process `n` samples of a STEREO pair in place: the two channels keep their own
 * comb and allpass state, so a sound panned left does not smear into the right. */
void  wb_reverb_process_stereo(wb_reverb *r, float *l, float *rr, uint32_t n);

/* Process a MONO block in place. The same signal drives both channels and the
 * result is averaged back to one. */
void  wb_reverb_process(wb_reverb *r, float *x, uint32_t n);

/* Parameter setters: 0 = feedback, 1 = mix. */
void  wb_reverb_set(wb_reverb *r, int param, float v);

#ifdef __cplusplus
}
#endif

#endif /* WB_REVERB_H */
