/*
 * wb_wav.c -- 16-bit PCM WAV writer. Taken from Big Mac (waefrebeorn/big-mac),
 * which wrote it to have an output format it owned. Unmodified apart from the
 * header comment.
 */
#include "wb_wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void wr32(unsigned char *b, unsigned long v) {
    b[0] = (unsigned char)(v & 0xFF);
    b[1] = (unsigned char)((v >> 8) & 0xFF);
    b[2] = (unsigned char)((v >> 16) & 0xFF);
    b[3] = (unsigned char)((v >> 24) & 0xFF);
}

static void wr16(unsigned char *b, unsigned v) {
    b[0] = (unsigned char)(v & 0xFF);
    b[1] = (unsigned char)((v >> 8) & 0xFF);
}

int wb_wav_write(const char *path, const double *samples, size_t n,
                 int sample_rate) {
    if (!path || !samples) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    unsigned long data_bytes = (unsigned long)n * 2;
    unsigned long riff_size = 36 + data_bytes;

    unsigned char hdr[44];
    memset(hdr, 0, sizeof(hdr));
    memcpy(hdr + 0, "RIFF", 4);
    wr32(hdr + 4, riff_size);
    memcpy(hdr + 8, "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);
    wr32(hdr + 16, 16);                 /* fmt chunk size */
    wr16(hdr + 20, 1);                  /* PCM */
    wr16(hdr + 22, 1);                  /* mono */
    wr32(hdr + 24, (unsigned long)sample_rate);
    wr32(hdr + 28, (unsigned long)sample_rate * 2); /* byte rate */
    wr16(hdr + 32, 2);                  /* block align */
    wr16(hdr + 34, 16);                 /* bits per sample */
    memcpy(hdr + 36, "data", 4);
    wr32(hdr + 40, data_bytes);

    if (fwrite(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) { fclose(f); return -1; }

    /* Convert and write in chunks: a full run is millions of samples, and the
     * point of a writer is that it does not need them all in memory again. */
    unsigned char buf[4096 * 2];
    size_t i = 0;
    while (i < n) {
        size_t chunk = n - i; if (chunk > 4096) chunk = 4096;
        for (size_t k = 0; k < chunk; k++) {
            double s = samples[i + k];
            if (s < -1) s = -1;
            if (s > 1) s = 1;
            short v = (short)(s * 32767);
            buf[k * 2] = (unsigned char)(v & 0xFF);
            buf[k * 2 + 1] = (unsigned char)((v >> 8) & 0xFF);
        }
        if (fwrite(buf, 1, chunk * 2, f) != chunk * 2) { fclose(f); return -1; }
        i += chunk;
    }
    fclose(f);
    return 0;
}

int wb_wav_write_stereo(const char *path, const double *left,
                        const double *right, size_t n, int sample_rate) {
    if (!path || !left || !right) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    unsigned long data_bytes = (unsigned long)n * 4;   /* two 16-bit channels */
    unsigned long riff_size = 36 + data_bytes;

    unsigned char hdr[44];
    memset(hdr, 0, sizeof(hdr));
    memcpy(hdr + 0, "RIFF", 4);
    wr32(hdr + 4, riff_size);
    memcpy(hdr + 8, "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);
    wr32(hdr + 16, 16);                 /* fmt chunk size */
    wr16(hdr + 20, 1);                  /* PCM */
    wr16(hdr + 22, 2);                  /* stereo */
    wr32(hdr + 24, (unsigned long)sample_rate);
    wr32(hdr + 28, (unsigned long)sample_rate * 4); /* byte rate */
    wr16(hdr + 32, 4);                  /* block align: 2 ch x 2 bytes */
    wr16(hdr + 34, 16);                 /* bits per sample */
    memcpy(hdr + 36, "data", 4);
    wr32(hdr + 40, data_bytes);

    if (fwrite(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) { fclose(f); return -1; }

    unsigned char buf[4096 * 4];
    size_t i = 0;
    while (i < n) {
        size_t chunk = n - i; if (chunk > 4096) chunk = 4096;
        for (size_t k = 0; k < chunk; k++) {
            double s[2];
            s[0] = left[i + k];
            s[1] = right[i + k];
            for (int c = 0; c < 2; c++) {
                if (s[c] < -1) s[c] = -1;
                if (s[c] > 1) s[c] = 1;
                short v = (short)(s[c] * 32767);
                buf[(k * 2 + c) * 2] = (unsigned char)(v & 0xFF);
                buf[(k * 2 + c) * 2 + 1] = (unsigned char)((v >> 8) & 0xFF);
            }
        }
        if (fwrite(buf, 1, chunk * 4, f) != chunk * 4) { fclose(f); return -1; }
        i += chunk;
    }
    fclose(f);
    return 0;
}
