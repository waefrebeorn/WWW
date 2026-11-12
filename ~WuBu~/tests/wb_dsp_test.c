/*
 * wb_dsp_test.c -- gate for the analysis primitives.
 *
 * Each primitive is checked against a signal whose answer is known by
 * construction: a sine of a known frequency, a constant, a silent buffer. The
 * point is that a pitch estimator which merely returns "something" passes a
 * loudness test and fails here.
 */
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

#define SR 24000

int main(void) {
    enum { N = 4096 };                      /* a power of two, for the FFT */
    double *x = malloc((size_t)N * sizeof(double));

    /* ---- autocorrelation ------------------------------------------------- */
    /* A sine correlates with itself perfectly at its own period, so R[period]
     * must be near R[0] and the first strong peak must land on the period. */
    const double f = 440.0;
    const size_t period = (size_t)llround((double)SR / f);   /* ~54.5 -> 55 */
    for (int i = 0; i < N; i++)
        x[i] = sin(2.0 * 3.14159265358979 * f * (double)i / SR);

    double *R = malloc((size_t)N * sizeof(double));
    wb_acorr(x, (size_t)N, R, (size_t)N / 2);
    CHECK(R[0] > 0.0, "autocorrelation of a tone has energy at lag 0");
    /* The first local maximum after the initial fall must be at the period. */
    size_t first_peak = 0;
    for (size_t k = 2; k < (size_t)N / 2; k++) {
        if (R[k] > R[k - 1] && R[k] >= R[k + 1]) { first_peak = k; break; }
    }
    CHECK(first_peak == period,
          "the first autocorrelation peak lands on the signal's period");
    CHECK(R[first_peak] > 0.5 * R[0],
          "a periodic signal correlates strongly with itself");

    /* A silent buffer has no energy anywhere. */
    for (int i = 0; i < N; i++) x[i] = 0.0;
    wb_acorr(x, (size_t)N, R, 100);
    CHECK(R[0] == 0.0, "a silent buffer has zero autocorrelation");

    /* ---- FFT ------------------------------------------------------------- */
    /* A real sine's spectrum is two impulses; in a one-sided view the positive
     * frequency is the largest bin, and its index times SR/N must be the
     * frequency we put in. */
    for (int i = 0; i < N; i++)
        x[i] = sin(2.0 * 3.14159265358979 * f * (double)i / SR);
    double *Re = malloc((size_t)N * sizeof(double));
    double *Im = malloc((size_t)N * sizeof(double));
    CHECK(wb_rfft(x, (size_t)N, Re, Im) == 0, "the FFT runs on a power of two");
    size_t bin = wb_argmax_abs(Re, 1, (size_t)N / 2);
    double bin_hz = (double)bin * (double)SR / (double)N;
    CHECK(fabs(bin_hz - f) < 2.0 * (double)SR / (double)N,
          "the spectral peak is the frequency that was put in");
    /* A real signal's transform is conjugate-symmetric: Re[N-k] == Re[k]. */
    CHECK(fabs(fabs(Re[N - bin]) - fabs(Re[bin])) <=
              1e-9 * (fabs(Re[bin]) + 1.0),
          "a real signal's spectrum is symmetric");
    /* And it is a PEAK, not a plateau: the bins either side are far smaller. */
    CHECK(fabs(Re[bin]) > 5.0 * fabs(Re[bin - 1]),
          "the peak is a sharp spectral line");

    /* A length that is not a power of two is refused, not mis-transformed. */
    CHECK(wb_rfft(x, 1000, Re, Im) == -1, "a non-power-of-two length is refused");
    CHECK(wb_rfft(x, 1, Re, Im) == -1, "a length below two is refused");

    /* ---- YIN pitch ------------------------------------------------------- */
    /* The whole point: a pitch estimate, not a loudness. A 440 Hz sine must come
     * back as 440 Hz, and a silent buffer must come back unvoiced. */
    double f0 = wb_yin_f0(x, (size_t)N, SR);
    CHECK(f0 > 0.0, "a tone is voiced");
    CHECK(fabs(f0 - f) / f < 0.02, "YIN recovers the frequency within 2%");

    /* A different frequency must give a different answer: an estimator that
     * returns a constant would pass the check above and fail this. YIN's search
     * range is 50-500 Hz by construction (tau_min = rate/500), so both test
     * tones sit inside it. */
    for (int i = 0; i < N; i++)
        x[i] = sin(2.0 * 3.14159265358979 * 220.0 * (double)i / SR);
    double f0_lo = wb_yin_f0(x, (size_t)N, SR);
    CHECK(fabs(f0_lo - 220.0) / 220.0 < 0.02, "YIN tracks a different frequency");
    CHECK(f0_lo < f0 * 0.75, "a lower pitch estimates lower");

    /* Above the estimator's range it returns a SUBHARMONIC rather than the
     * right answer -- it locks onto twice the period. This is a property of the
     * algorithm, recorded so a caller does not trust it past 500 Hz. */
    for (int i = 0; i < N; i++)
        x[i] = sin(2.0 * 3.14159265358979 * 880.0 * (double)i / SR);
    double f0_out = wb_yin_f0(x, (size_t)N, SR);
    CHECK(f0_out > 0.0 && f0_out < 880.0,
          "above its range YIN returns a subharmonic, not the true pitch");

    /* Silence and a constant are both unvoiced: a constant has no period to
     * find, and YIN must not invent one. */
    for (int i = 0; i < N; i++) x[i] = 0.0;
    CHECK(wb_yin_f0(x, (size_t)N, SR) == 0.0, "silence is unvoiced");
    for (int i = 0; i < N; i++) x[i] = 1.0;
    CHECK(wb_yin_f0(x, (size_t)N, SR) == 0.0, "a constant is unvoiced");

    /* A window too short to hold a period is unvoiced rather than wrong. */
    for (int i = 0; i < 8; i++) x[i] = sin(2.0 * 3.14159265358979 * f * (double)i / SR);
    CHECK(wb_yin_f0(x, 8, SR) == 0.0, "a window shorter than a period is unvoiced");

    /* ---- LPC ------------------------------------------------------------- */
    /* A pure sine is exactly predictable from its own past, so a well-chosen
     * order must leave a residual far smaller than the signal's energy. A
     * silent buffer is singular and must be refused. */
    for (int i = 0; i < N; i++)
        x[i] = sin(2.0 * 3.14159265358979 * f * (double)i / SR);
    wb_acorr(x, (size_t)N, R, 8);
    double a[8];
    double E = 0.0;
    CHECK(wb_lpc(R, 8, a, &E) == 0, "LPC solves on a tone");
    /* A pure sine is almost entirely predicted by its own past. The residual is
     * relative to the signal's energy, not absolute: the autocorrelation is in
     * sample units, so an absolute threshold would be meaningless at any other
     * gain. */
    CHECK(E >= 0.0 && E < 1e-3 * R[0],
          "a pure tone is almost entirely predicted by its own past");

    for (int i = 0; i < N; i++) x[i] = 0.0;
    wb_acorr(x, (size_t)N, R, 8);
    CHECK(wb_lpc(R, 8, a, &E) == -1, "LPC refuses a singular (silent) input");
    CHECK(wb_lpc(R, 0, a, &E) == -1, "LPC refuses order zero");

    /* ---- peak picking ---------------------------------------------------- */
    double small[5] = { 1.0, -3.0, 2.0, -0.5, 0.25 };
    CHECK(wb_argmax_abs(small, 0, 4) == 1, "the peak is the largest magnitude");
    CHECK(wb_argmax_abs(small, 2, 4) == 2, "the search honours its bounds");

    free(Re); free(Im); free(R); free(x);
    printf("=== DSP TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
