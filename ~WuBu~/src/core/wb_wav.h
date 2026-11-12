/*
 * wb_wav.h -- 16-bit PCM WAV writer.
 *
 * Taken from the Big Mac repo (waefrebeorn/big-mac, src/wb_wav.c), which is the
 * WuBu musical agent's own hand-written writer. Same author, same "no third
 * party if we can write it" rule, so it comes in rather than being reinvented.
 *
 * The engine writes frames as PPM and sound as WAV: both are the SHIPPED output
 * formats, and both are written by code the space owns. A caller that wants to
 * hear a run opens the file in any player -- no device, no driver, no codec.
 *
 * C11, no deps beyond stdio.
 */
#ifndef WB_WAV_H
#define WB_WAV_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Write a mono 16-bit PCM WAV. `samples` are floats in [-1,1]; anything outside
 * is clamped rather than wrapped, so an over-loud mix distorts instead of
 * aliasing into a different sound. Returns 0 on success, -1 on any write or open
 * failure. */
int wb_wav_write(const char *path, const double *samples, size_t n,
                 int sample_rate);

#ifdef __cplusplus
}
#endif

#endif /* WB_WAV_H */
