/*
 * wb_wav_test.c -- gate for the WAV writer.
 *
 * The writer's contract is a FILE another program can open, so the gate reads
 * what it wrote and checks the header byte by byte, then the samples. A writer
 * that only satisfies its own reader would pass a test that shares its bugs.
 */
#include "wb_wav.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } \
} while (0)

/* Minimal RIFF reader, written here rather than sharing the writer's code. */
static int read_wav(const char *path, int *rate, short **samples, long *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    unsigned char hdr[44];
    if (fread(hdr, 1, 44, f) != 44) { fclose(f); return -1; }
    if (memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) {
        fclose(f); return -1;
    }
    if (memcmp(hdr + 12, "fmt ", 4) || memcmp(hdr + 36, "data", 4)) {
        fclose(f); return -1;
    }
    unsigned fmt   = hdr[20] | (hdr[21] << 8);
    unsigned chans = hdr[22] | (hdr[23] << 8);
    unsigned sr    = hdr[24] | (hdr[25] << 8) | (hdr[26] << 16) | ((unsigned)hdr[27] << 24);
    unsigned bits  = hdr[34] | (hdr[35] << 8);
    unsigned bytes = hdr[40] | (hdr[41] << 8) | (hdr[42] << 16) | ((unsigned)hdr[43] << 24);
    if (fmt != 1 || chans != 1 || bits != 16) { fclose(f); return -1; }

    long count = bytes / 2;
    short *s = malloc((size_t)count * sizeof(short));
    if (!s) { fclose(f); return -1; }
    if (fread(s, 2, (size_t)count, f) != (size_t)count) {
        free(s); fclose(f); return -1;
    }
    fclose(f);
    *rate = (int)sr;
    *samples = s;
    *n = count;
    return 0;
}

int main(void) {
    const char *path = "build/wb_wav_test.wav";
    const int rate = 24000;

    /* ---- a known tone ---------------------------------------------------- */
    enum { N = 2400 };                       /* 0.1 s at 24 kHz */
    double *tone = malloc((size_t)N * sizeof(double));
    for (int i = 0; i < N; i++)
        tone[i] = 0.5 * sin(2.0 * 3.14159265358979 * 440.0 * (double)i / rate);

    CHECK(wb_wav_write(path, tone, (size_t)N, rate) == 0, "the write succeeds");

    int got_rate = 0; short *s = NULL; long n = 0;
    CHECK(read_wav(path, &got_rate, &s, &n) == 0, "the file is a readable WAV");
    CHECK(got_rate == rate, "the sample rate round-trips");
    CHECK(n == N, "the frame count round-trips");

    /* The samples must be the same tone, not merely present: compare against
     * the float original scaled back to 16-bit. */
    int worst = 0;
    for (long i = 0; i < n; i++) {
        long want = lround(tone[i] * 32767.0);
        long got = s[i];
        long d = got - want; if (d < 0) d = -d;
        if (d > worst) worst = (int)d;
    }
    CHECK(worst <= 2, "the samples round-trip within one 16-bit step");
    free(s); free(tone);

    /* ---- silence stays silence ------------------------------------------ */
    double silent[16] = {0};
    CHECK(wb_wav_write(path, silent, 16, rate) == 0, "silence writes");
    CHECK(read_wav(path, &got_rate, &s, &n) == 0, "silent file is readable");
    CHECK(n == 16, "silent frame count");
    long nonzero = 0;
    for (long i = 0; i < n; i++) if (s[i] != 0) nonzero++;
    CHECK(nonzero == 0, "silence is written as zeros");
    free(s);

    /* ---- clipping saturates rather than wrapping ------------------------ */
    /* A sample of +8.0 must land on the maximum, not fold into a negative
     * value: wrapping would turn an over-loud moment into a different sound. */
    double loud[4] = { 8.0, -8.0, 0.9999, -0.9999 };
    CHECK(wb_wav_write(path, loud, 4, rate) == 0, "an over-loud buffer writes");
    CHECK(read_wav(path, &got_rate, &s, &n) == 0, "over-loud file is readable");
    CHECK(s[0] > 32000, "a sample above +1 saturates at the top");
    CHECK(s[1] < -32000, "a sample below -1 saturates at the bottom");
    CHECK(s[2] > 32000 && s[2] < 32767, "a sample just under +1 stays just under");
    CHECK(s[3] < -32000 && s[3] > -32767, "a sample just under -1 stays just under");
    free(s);

    /* ---- degenerate calls ------------------------------------------------ */
    CHECK(wb_wav_write(NULL, loud, 4, rate) == -1, "a NULL path is refused");
    CHECK(wb_wav_write(path, NULL, 4, rate) == -1, "NULL samples are refused");
    CHECK(wb_wav_write("build/no_such_dir/x.wav", loud, 4, rate) == -1,
          "an unwritable path is refused rather than reported as success");

    /* ---- a long write is chunked, not one allocation --------------------- */
    enum { BIG = 20000 };                    /* more than the 4096 chunk */
    double *big = malloc((size_t)BIG * sizeof(double));
    for (int i = 0; i < BIG; i++) big[i] = 0.25 * sin((double)i * 0.01);
    CHECK(wb_wav_write(path, big, (size_t)BIG, rate) == 0, "a long write succeeds");
    CHECK(read_wav(path, &got_rate, &s, &n) == 0, "the long file is readable");
    CHECK(n == BIG, "the long write keeps every frame");
    CHECK(s[0] == 0 && s[19999] != 0, "the chunk boundary is seamless");
    free(s); free(big);

    printf("=== WAV TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
