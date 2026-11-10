/*
 * wb_eventlog.c -- snapshot + tail event log (LP-50 stone 1). See wb_eventlog.h.
 */
#include "wb_eventlog.h"
#include "wb_replay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define wb_eventlog_truncate(f, n) _chsize_s(_fileno(f), (__int64)(n))
#else
#include <unistd.h>
#define wb_eventlog_truncate(f, n) ftruncate(fileno(f), (off_t)(n))
#endif

_Static_assert(sizeof(wb_eventlog_header) == 32, "the on-disk header must be 32 bytes");
_Static_assert(sizeof(wb_eventlog_rec) == 16, "the on-disk record header must be 16 bytes");

/* CRC-32 over a concatenation, so a record's CRC covers the tick, the type and
 * the payload as one continuous stream. */
static uint32_t wb_eventlog_crc_bytes(const void *data, uint32_t len, uint32_t seed) {
    static uint32_t table[256];
    static int ready = 0;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        ready = 1;
    }
    const uint8_t *p = (const uint8_t *)data;
    uint32_t c = ~seed;
    for (uint32_t i = 0; i < len; i++)
        c = table[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return ~c;
}

static uint32_t wb_eventlog_rec_crc(uint32_t tick, uint32_t type, const void *data, uint32_t len) {
    uint32_t crc = wb_eventlog_crc_bytes(&tick, sizeof tick, 0u);
    crc = wb_eventlog_crc_bytes(&type, sizeof type, crc);
    if (data && len) crc = wb_eventlog_crc_bytes(data, len, crc);
    return crc;
}

static int wb_eventlog_read_header_fp(FILE *f, wb_eventlog_header *h) {
    if (fseek(f, 0, SEEK_SET) != 0) return -1;
    if (fread(h, sizeof *h, 1, f) != 1) return -1;
    if (h->magic != WB_EVENTLOG_MAGIC || h->version != WB_EVENTLOG_VERSION) return -1;
    return 0;
}

/* Read one record header. Returns 1 on success, 0 on a clean EOF at a record
 * boundary, -1 on a partial header -- a torn write, which must not be mistaken
 * for the end of the log. */
static int wb_eventlog_read_rec_header(FILE *f, wb_eventlog_rec *r) {
    size_t got = fread(r, 1, sizeof *r, f);
    if (got == 0) return 0;
    if (got != sizeof *r) return -1;
    return 1;
}

int wb_eventlog_read_header(const char *path, wb_eventlog_header *out) {
    if (!path || !out) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int rc = wb_eventlog_read_header_fp(f, out);
    fclose(f);
    return rc;
}

int wb_eventlog_create(const char *path, const void *snapshot, uint32_t snap_len) {
    if (!path || (snap_len && !snapshot)) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    wb_eventlog_header h;
    memset(&h, 0, sizeof h);
    h.seq = 1;
    h.magic = WB_EVENTLOG_MAGIC;
    h.version = WB_EVENTLOG_VERSION;
    h.flags = snap_len ? WB_EVENTLOG_HAS_SNAPSHOT : 0u;
    h.snap_len = snap_len;
    h.snap_crc = snap_len ? wb_replay_crc32(snapshot, snap_len) : 0u;
    h.last_tick = 0;
    int rc = 0;
    if (fwrite(&h, sizeof h, 1, f) != 1) rc = -1;
    if (rc == 0 && snap_len && fwrite(snapshot, 1, snap_len, f) != snap_len) rc = -1;
    if (rc != 0) fclose(f);
    else if (fclose(f) != 0) rc = -1;
    return rc;
}

int wb_eventlog_append(const char *path, uint32_t tick, uint32_t type,
                       const void *data, uint32_t len) {
    if (!path || (len && !data)) return -1;
    FILE *f = fopen(path, "r+b");
    if (!f) return -1;
    wb_eventlog_header h;
    if (wb_eventlog_read_header_fp(f, &h) != 0) { fclose(f); return -1; }
    if (tick <= h.last_tick) { fclose(f); return -1; }   /* replay order is the meaning */

    wb_eventlog_rec r;
    r.tick = tick;
    r.type = type;
    r.len = len;
    r.crc = wb_eventlog_rec_crc(tick, type, data, len);

    int rc = 0;
    if (fseek(f, 0, SEEK_END) != 0) rc = -1;
    if (rc == 0 && fwrite(&r, sizeof r, 1, f) != 1) rc = -1;
    if (rc == 0 && len && fwrite(data, 1, len, f) != len) rc = -1;
    if (rc == 0 && fflush(f) != 0) rc = -1;
    if (rc == 0) {
        h.last_tick = tick;
        if (fseek(f, 0, SEEK_SET) != 0) rc = -1;
        if (rc == 0 && fwrite(&h, sizeof h, 1, f) != 1) rc = -1;
        if (rc == 0 && fflush(f) != 0) rc = -1;
    }
    if (rc != 0) { fclose(f); return -1; }
    return fclose(f) == 0 ? 0 : -1;
}

int wb_eventlog_rotate(const char *path, const void *snapshot, uint32_t snap_len) {
    if (!path || (snap_len && !snapshot)) return -1;
    FILE *f = fopen(path, "r+b");
    if (!f) return -1;
    wb_eventlog_header h;
    if (wb_eventlog_read_header_fp(f, &h) != 0) { fclose(f); return -1; }
    int rc = 0;
    if (fseek(f, 0, SEEK_SET) != 0) rc = -1;
    wb_eventlog_header n = h;
    n.seq = h.seq + 1;
    n.flags = snap_len ? WB_EVENTLOG_HAS_SNAPSHOT : 0u;
    n.snap_len = snap_len;
    n.snap_crc = snap_len ? wb_replay_crc32(snapshot, snap_len) : 0u;
    n.last_tick = 0;
    if (rc == 0 && fwrite(&n, sizeof n, 1, f) != 1) rc = -1;
    if (rc == 0 && snap_len && fwrite(snapshot, 1, snap_len, f) != snap_len) rc = -1;
    if (rc == 0 && fflush(f) != 0) rc = -1;
    if (rc == 0) {
        /* drop the old tail: truncate just past the new snapshot */
        long end = (long)(sizeof n + snap_len);
        if (wb_eventlog_truncate(f, end) != 0) rc = -1;
    }
    if (rc != 0) { fclose(f); return -1; }
    return fclose(f) == 0 ? 0 : -1;
}

long wb_eventlog_read_snapshot(const char *path, void *out, uint32_t cap) {
    if (!path || !out) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    wb_eventlog_header h;
    if (wb_eventlog_read_header_fp(f, &h) != 0) { fclose(f); return -1; }
    if (!(h.flags & WB_EVENTLOG_HAS_SNAPSHOT)) { fclose(f); return -1; }
    if (h.snap_len > cap) { fclose(f); return -1; }
    int rc = -1;
    if (fseek(f, (long)sizeof h, SEEK_SET) == 0 && h.snap_len &&
        fread(out, 1, h.snap_len, f) == h.snap_len) rc = (long)h.snap_len;
    else if (h.snap_len == 0) rc = 0;
    fclose(f);
    return rc;
}

long wb_eventlog_replay(const char *path, uint32_t until_tick,
                        int (*fn)(const wb_eventlog_rec *rec, const void *data, void *user),
                        void *user) {
    if (!path || !fn) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    wb_eventlog_header h;
    if (wb_eventlog_read_header_fp(f, &h) != 0) { fclose(f); return -1; }
    if (fseek(f, (long)(sizeof h + h.snap_len), SEEK_SET) != 0) { fclose(f); return -1; }

    long visited = 0;
    int rc = -1;
    for (;;) {
        wb_eventlog_rec r;
        int hr = wb_eventlog_read_rec_header(f, &r);
        if (hr == 0) { rc = 0; break; }        /* a clean EOF ends the log */
        if (hr < 0) { rc = -1; break; }        /* a torn record header */
        unsigned char *buf = NULL;
        if (r.len) {
            buf = (unsigned char *)malloc(r.len);
            if (!buf || fread(buf, 1, r.len, f) != r.len) { free(buf); rc = -1; break; }
        }
        if (wb_eventlog_rec_crc(r.tick, r.type, buf, r.len) != r.crc) { free(buf); rc = -1; break; }
        if (r.tick > until_tick) { free(buf); rc = 0; break; }   /* the tail is ordered */
        visited++;
        int stop = fn(&r, buf, user);
        free(buf);
        if (stop) { rc = 0; break; }
    }
    fclose(f);
    return rc == 0 ? visited : -1;
}

int wb_eventlog_verify(const char *path) {
    if (!path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    wb_eventlog_header h;
    if (wb_eventlog_read_header_fp(f, &h) != 0) { fclose(f); return -1; }
    int rc = 0;
    if (h.flags & WB_EVENTLOG_HAS_SNAPSHOT) {
        unsigned char *buf = (unsigned char *)malloc(h.snap_len ? h.snap_len : 1);
        if (!buf) { fclose(f); return -1; }
        if (fseek(f, (long)sizeof h, SEEK_SET) != 0 ||
            (h.snap_len && fread(buf, 1, h.snap_len, f) != h.snap_len)) {
            free(buf); fclose(f); return -1;
        }
        if (wb_replay_crc32(buf, h.snap_len) != h.snap_crc) rc = -1;
        free(buf);
        if (rc != 0) { fclose(f); return -1; }
    }
    if (fseek(f, (long)(sizeof h + h.snap_len), SEEK_SET) != 0) { fclose(f); return -1; }
    for (;;) {
        wb_eventlog_rec r;
        int hr = wb_eventlog_read_rec_header(f, &r);
        if (hr == 0) break;                    /* a clean EOF ends the log */
        if (hr < 0) { rc = -1; break; }        /* a torn record header */
        unsigned char *buf = NULL;
        if (r.len) {
            buf = (unsigned char *)malloc(r.len);
            if (!buf || fread(buf, 1, r.len, f) != r.len) { free(buf); rc = -1; break; }
        }
        if (wb_eventlog_rec_crc(r.tick, r.type, buf, r.len) != r.crc) { free(buf); rc = -1; break; }
        free(buf);
    }
    fclose(f);
    return rc;
}
