/*
 * wb_replay.c -- replay recorder (LP-47 stone 1). See wb_replay.h.
 */
#include "wb_replay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WB_REPLAY_MAGIC "WBREPLAY"
/* A run is minutes long; a million frames is 27 hours at 60 Hz. Refuse anything
 * larger rather than let a corrupt count exhaust memory. */
#define WB_REPLAY_MAX_FRAMES 1000000u

/* The standard reflected CRC-32 (IEEE 802.3, the zlib variant). */
static uint32_t wb_replay_crc_table[256];
static int      wb_replay_crc_ready = 0;

static void wb_replay_make_table(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        wb_replay_crc_table[i] = c;
    }
    wb_replay_crc_ready = 1;
}

static uint32_t wb_replay_crc32_impl(const void *data, size_t len, uint32_t crc) {
    if (!wb_replay_crc_ready) wb_replay_make_table();
    const uint8_t *p = (const uint8_t *)data;
    crc = ~crc;
    for (size_t i = 0; i < len; i++)
        crc = wb_replay_crc_table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

uint32_t wb_replay_crc32(const void *data, uint32_t len) {
    return wb_replay_crc32_impl(data, len, 0u);
}

uint32_t wb_replay_frame_crc(uint32_t tick, const void *data, uint32_t len) {
    uint32_t crc = wb_replay_crc32_impl(&tick, sizeof tick, 0u);
    crc = wb_replay_crc32_impl(&len, sizeof len, crc);
    if (data && len) crc = wb_replay_crc32_impl(data, len, crc);
    return crc;
}

int wb_replay_init(wb_replay *r, uint32_t cap) {
    if (!r || cap == 0) return -1;
    r->frames = (wb_replay_frame *)calloc(cap, sizeof(wb_replay_frame));
    if (!r->frames) return -1;
    r->n = 0;
    r->cap = cap;
    r->tick = 0;
    r->crc = 0;
    return 0;
}

void wb_replay_free(wb_replay *r) {
    if (!r) return;
    free(r->frames);
    r->frames = NULL;
    r->n = r->cap = 0;
}

void wb_replay_clear(wb_replay *r) {
    if (!r) return;
    r->n = 0;
    r->tick = 0;
    r->crc = 0;
}

int wb_replay_record(wb_replay *r, uint32_t tick, const void *data, uint32_t len) {
    if (!r || !r->frames) return -1;
    if (len > WB_REPLAY_MAX_PAYLOAD) return -1;
    if (r->n == r->cap) return -1;
    if (r->n > 0 && tick <= r->frames[r->n - 1].tick) return -1;   /* monotonic */
    wb_replay_frame *f = &r->frames[r->n];
    f->tick = tick;
    f->len = len;
    if (data && len) memcpy(f->data, data, len);
    f->crc = wb_replay_frame_crc(tick, data, len);
    r->n++;
    r->tick = tick + 1;
    /* the chain covers the frame CRC, so the order is part of the total */
    r->crc = wb_replay_crc32_impl(&f->crc, sizeof f->crc, r->crc);
    return 0;
}

int wb_replay_verify(const wb_replay *r) {
    if (!r || !r->frames) return 0;
    uint32_t crc = 0;
    for (uint32_t i = 0; i < r->n; i++) {
        const wb_replay_frame *f = &r->frames[i];
        if (wb_replay_frame_crc(f->tick, f->data, f->len) != f->crc) return 0;
        crc = wb_replay_crc32_impl(&f->crc, sizeof f->crc, crc);
    }
    return crc == r->crc;
}

uint32_t wb_replay_crc(const wb_replay *r) { return r ? r->crc : 0; }

int wb_replay_save(const wb_replay *r, const char *path) {
    if (!r || !r->frames || !path) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = 1;
    uint32_t n = r->n;
    if (fwrite(WB_REPLAY_MAGIC, 1, 8, f) != 8) ok = 0;
    if (ok && fwrite(&n, sizeof n, 1, f) != 1) ok = 0;
    for (uint32_t i = 0; ok && i < r->n; i++) {
        const wb_replay_frame *fr = &r->frames[i];
        if (fwrite(&fr->tick, sizeof fr->tick, 1, f) != 1) { ok = 0; break; }
        if (fwrite(&fr->len,  sizeof fr->len,  1, f) != 1) { ok = 0; break; }
        if (fwrite(&fr->crc,  sizeof fr->crc,  1, f) != 1) { ok = 0; break; }
        if (fr->len && fwrite(fr->data, 1, fr->len, f) != fr->len) { ok = 0; break; }
    }
    fclose(f);
    return ok ? 0 : -1;
}

int wb_replay_load(wb_replay *r, const char *path) {
    if (!r || !path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    char magic[8];
    uint32_t n = 0;
    int ok = fread(magic, 1, 8, f) == 8 &&
             memcmp(magic, WB_REPLAY_MAGIC, 8) == 0;
    if (ok && fread(&n, sizeof n, 1, f) != 1) ok = 0;
    if (ok && n > WB_REPLAY_MAX_FRAMES) ok = 0;
    if (!ok) { fclose(f); return -1; }

    if (wb_replay_init(r, n ? n : 1) != 0) { fclose(f); return -1; }
    for (uint32_t i = 0; i < n; i++) {
        wb_replay_frame *fr = &r->frames[i];
        if (fread(&fr->tick, sizeof fr->tick, 1, f) != 1) break;
        if (fread(&fr->len,  sizeof fr->len,  1, f) != 1) break;
        if (fread(&fr->crc,  sizeof fr->crc,  1, f) != 1) break;
        if (fr->len > WB_REPLAY_MAX_PAYLOAD) break;
        if (fr->len && fread(fr->data, 1, fr->len, f) != fr->len) break;
        r->n++;
        r->tick = fr->tick + 1;
        r->crc = wb_replay_crc32_impl(&fr->crc, sizeof fr->crc, r->crc);
    }
    fclose(f);
    if (r->n != n) { wb_replay_clear(r); return -1; }   /* short file */
    return wb_replay_verify(r) ? 0 : -1;                /* chain must verify */
}
