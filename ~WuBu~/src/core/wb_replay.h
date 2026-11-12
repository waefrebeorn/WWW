/*
 * wb_replay.h -- replay recorder (LP-47 stone 1).
 *
 * A replay is the per-tick input stream plus a checksum chain: every frame
 * carries its own CRC and a running total over the whole log. Replaying a log
 * must reproduce the run bit for bit, so a corrupted or truncated log has to be
 * detectable before it is trusted -- the running total covers the tick order and
 * the payload length, not just the bytes.
 *
 * C11, no deps.
 */
#ifndef WB_REPLAY_H
#define WB_REPLAY_H

#include <stdint.h>

#define WB_REPLAY_MAX_PAYLOAD 64

typedef struct {
    uint32_t tick;
    uint32_t len;
    uint32_t crc;                      /* CRC-32 of (tick, len, data) */
    uint8_t  data[WB_REPLAY_MAX_PAYLOAD];
} wb_replay_frame;

typedef struct {
    wb_replay_frame *frames;
    uint32_t         n, cap;
    uint32_t         tick;             /* the next tick to record */
    uint32_t         crc;              /* the running chain over every frame */
} wb_replay;

int  wb_replay_init(wb_replay *r, uint32_t cap);
void wb_replay_free(wb_replay *r);
void wb_replay_clear(wb_replay *r);

/* Record a frame for `tick`. Returns -1 if the log is full, the payload is too
 * large, or the tick goes backwards. */
int  wb_replay_record(wb_replay *r, uint32_t tick, const void *data, uint32_t len);

/* Recompute the chain over the stored frames. Returns 1 if it matches the stored
 * total, 0 if the log was altered. */
int  wb_replay_verify(const wb_replay *r);
uint32_t wb_replay_crc(const wb_replay *r);

/* The raw CRC-32 of a byte range (no header). */
uint32_t wb_replay_crc32(const void *data, uint32_t len);

/* The CRC of a single frame's contents. */
uint32_t wb_replay_frame_crc(uint32_t tick, const void *data, uint32_t len);

/* File IO. The log is an 8-byte magic, a frame count, then the frames themselves
 * (tick, length, CRC, payload). `wb_replay_load` re-verifies the chain, so a
 * corrupted or truncated file is REFUSED rather than replayed -- a replay that
 * silently starts from a damaged log is worse than no replay. A failed load
 * leaves `r` empty. */
int wb_replay_save(const wb_replay *r, const char *path);
int wb_replay_load(wb_replay *r, const char *path);

#endif /* WB_REPLAY_H */
