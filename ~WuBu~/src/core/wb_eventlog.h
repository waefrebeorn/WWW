/*
 * wb_eventlog.h -- snapshot + tail event log (LP-50 stone 1).
 *
 * Streaming is transport, not durability. A durable store is a snapshot of the
 * world plus an append-only log of everything that happened after it: on load,
 * read the snapshot and replay the tail. The tail is the whole point -- without
 * it a server crash loses every tick since the last snapshot.
 *
 * A record carries its own CRC over (tick, type, data) and the snapshot carries
 * one over its bytes, so a torn write or a corrupted file is detected before the
 * state is trusted rather than surfacing later as a bad pointer. Ticks are
 * monotonic within a log: an out-of-order append is refused, not silently
 * written, because replay order is the meaning of the log.
 *
 * C11, no deps beyond the CRC in wb_replay.
 */
#ifndef WB_EVENTLOG_H
#define WB_EVENTLOG_H

#include <stdint.h>

#define WB_EVENTLOG_MAGIC   0x474F4C57u   /* 'WLOG' */
#define WB_EVENTLOG_VERSION 1u

/* Header flags. */
#define WB_EVENTLOG_HAS_SNAPSHOT 1u

typedef struct {
    uint64_t seq;            /* bumped on every rotation */
    uint32_t magic;
    uint32_t version;
    uint32_t flags;
    uint32_t snap_len;
    uint32_t snap_crc;
    uint32_t last_tick;      /* the tick of the most recent record */
} wb_eventlog_header;

typedef struct {
    uint32_t tick;
    uint32_t type;
    uint32_t len;
    uint32_t crc;            /* CRC-32 of (tick, type, data) */
} wb_eventlog_rec;

/* Create (or overwrite) a log with an optional snapshot (snap_len 0 = none).
 * Returns 0 on success, -1 on error. */
int  wb_eventlog_create(const char *path, const void *snapshot, uint32_t snap_len);

/* Append a record. `tick` must be strictly greater than the last appended tick.
 * Returns 0 on success, -1 on error or an out-of-order tick. */
int  wb_eventlog_append(const char *path, uint32_t tick, uint32_t type,
                        const void *data, uint32_t len);

/* Rotate: install a new snapshot and drop the whole tail. Returns 0 on success. */
int  wb_eventlog_rotate(const char *path, const void *snapshot, uint32_t snap_len);

int  wb_eventlog_read_header(const char *path, wb_eventlog_header *out);

/* Read the snapshot into `out` (cap bytes). Returns the length, or -1 if there is
 * no snapshot, the buffer is too small, or the file is unreadable. */
long wb_eventlog_read_snapshot(const char *path, void *out, uint32_t cap);

/* Replay every record with tick <= until_tick, in log order. The callback
 * returns nonzero to stop early. Returns the number of records visited, or -1 on
 * a read error or a checksum failure. */
long wb_eventlog_replay(const char *path, uint32_t until_tick,
                        int (*fn)(const wb_eventlog_rec *rec, const void *data, void *user),
                        void *user);

/* Verify the header, the snapshot checksum and every record checksum. Returns 0
 * when the log is intact, -1 otherwise. */
int  wb_eventlog_verify(const char *path);

#endif /* WB_EVENTLOG_H */
