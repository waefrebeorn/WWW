/*
 * wb_dsp.h -- audio analysis primitives: autocorrelation, Levinson-Durbin LPC,
 * a radix-2 FFT, YIN pitch and peak picking.
 *
 * Taken from the Big Mac repo (waefrebeorn/big-mac, src/wb_dsp.c), the WuBu
 * musical agent's measurement layer. Same author, same no-third-party rule.
 *
 * The engine needs these for one job: proving that what it synthesised is what it
 * meant. A test that says "the buffer is non-silent" cannot tell a footstep from
 * a fault; a pitch estimate can. So the gate for the audio path asserts the
 * dominant frequency of a footstep, not merely that it is loud.
 *
 * C11, no deps beyond libm.
 */
#ifndef WB_DSP_H
#define WB_DSP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* R[k] = sum_n x[n]*x[n+k] for k=0..maxlag. x has n samples. */
void wb_acorr(const double *x, size_t n, double *R, size_t maxlag);

/* Solve order-p LPC from autocorrelation R[0..p]. Writes the prediction
 * coefficients a[0..p-1] and the residual energy E. Returns 0 on success, -1 if
 * the matrix is singular (a silent or constant input does this). */
int wb_lpc(const double *R, int p, double *a, double *E);

/* Forward real FFT. n must be a power of two >= 2. Re and Im are n-entry WORK
 * buffers; on return Re[0..n/2] and Im[0..n/2] hold the spectrum. Returns 0 on
 * success, -1 on a bad length. */
int wb_rfft(const double *x, size_t n, double *Re, double *Im);

/* Estimate F0 (Hz) of a window of samples at the given rate. Returns F0, or 0.0
 * when the window looks unvoiced. YIN's two steps: the first trough below a
 * threshold, then the local minimum with parabolic interpolation. */
double wb_yin_f0(const double *x, size_t n, int sample_rate);

/* Index of the maximum of |x| in [lo, hi]. */
size_t wb_argmax_abs(const double *x, size_t lo, size_t hi);

#ifdef __cplusplus
}
#endif

#endif /* WB_DSP_H */
