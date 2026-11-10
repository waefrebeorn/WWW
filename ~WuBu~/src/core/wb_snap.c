/*
 * wb_snap.c -- per-tick world snapshot, digest + restore (LP-03 capstone).
 * See wb_snap.h.
 *
 * Packed record layout (dense-index order):
 *   { u32 index; u32 gen; u8 present_mask;  [component elem bytes, in types[]
 *   order, for each present bit] ... }
 * The whole buffer is FNV-1a'd into the digest. restore() replays the records
 * and writes the component bytes back into the same slots, guarded by the
 * generation so a reallocated entity is not clobbered.
 */
#include "wb_snap.h"
#include "wb_det.h"

#include <stdlib.h>
#include <string.h>

struct wb_snap {
    uint8_t *bytes;
    size_t   len;
    size_t   cap;
    uint64_t digest;
};

static int snap_push(wb_snap *s, const void *p, size_t n) {
    if (s->len + n > s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 256;
        while (nc < s->len + n) nc *= 2;
        uint8_t *nb = (uint8_t *)realloc(s->bytes, nc);
        if (!nb) return 0;
        s->bytes = nb;
        s->cap = nc;
    }
    memcpy(s->bytes + s->len, p, n);
    s->len += n;
    return 1;
}

wb_snap *wb_snap_capture(wb_world *w, const wb_comp_type *types, int ntypes) {
    if (!w || !types || ntypes < 0 || ntypes > 64) return NULL;
    wb_snap *s = (wb_snap *)calloc(1, sizeof(*s));
    if (!s) return NULL;

    uint32_t ni = wb_world_next_index(w);
    for (uint32_t i = 0; i < ni; i++) {
        if (!wb_world_slot_alive(w, i)) continue;
        uint32_t gen = wb_world_slot_gen(w, i);
        uint8_t mask = 0;
        for (int t = 0; t < ntypes; t++)
            if (wb_world_comp_present(w, types[t], i)) mask |= (uint8_t)(1u << t);
        if (!snap_push(s, &i, sizeof(i))) goto fail;
        if (!snap_push(s, &gen, sizeof(gen))) goto fail;
        if (!snap_push(s, &mask, sizeof(mask))) goto fail;
        for (int t = 0; t < ntypes; t++) {
            if (!(mask & (1u << t))) continue;
            size_t sz = wb_world_comp_elem_size(w, types[t]);
            const unsigned char *base = (const unsigned char *)wb_world_comp_data(w, types[t]);
            if (!snap_push(s, base + (size_t)i * sz, sz)) goto fail;
        }
    }
    s->digest = wb_det_checksum(s->bytes, s->len);
    return s;
fail:
    wb_snap_free(s);
    return NULL;
}

void wb_snap_free(wb_snap *s) {
    if (!s) return;
    free(s->bytes);
    free(s);
}

uint64_t wb_snap_digest(const wb_snap *s) { return s ? s->digest : 0; }
int      wb_snap_equal(const wb_snap *a, const wb_snap *b) {
    return a && b ? (a->digest == b->digest) : (a == b);
}
size_t   wb_snap_bytes(const wb_snap *s) { return s ? s->len : 0; }

int wb_snap_first_diff(const wb_snap *a, const wb_snap *b, size_t *out_off) {
    if (!a || !b) return 0;
    size_t n = a->len < b->len ? a->len : b->len;
    for (size_t i = 0; i < n; i++) {
        if (a->bytes[i] != b->bytes[i]) { if (out_off) *out_off = i; return 1; }
    }
    if (a->len != b->len) { if (out_off) *out_off = n; return 1; }  /* prefix equal, length differs */
    return 0;
}

uint64_t wb_snap_digest_world(wb_world *w, const wb_comp_type *types, int ntypes) {
    if (!w || !types || ntypes < 0) return 0;
    uint64_t h = WB_DET_FNV_OFFSET;
    uint32_t ni = wb_world_next_index(w);
    for (uint32_t i = 0; i < ni; i++) {
        if (!wb_world_slot_alive(w, i)) continue;
        uint32_t gen = wb_world_slot_gen(w, i);
        uint8_t mask = 0;
        for (int t = 0; t < ntypes; t++)
            if (wb_world_comp_present(w, types[t], i)) mask |= (uint8_t)(1u << t);
        /* identical byte stream to wb_snap_capture, so the digests match */
        h = wb_det_checksum_stream(h, &i, sizeof(i));
        h = wb_det_checksum_stream(h, &gen, sizeof(gen));
        h = wb_det_checksum_stream(h, &mask, sizeof(mask));
        for (int t = 0; t < ntypes; t++) {
            if (!(mask & (uint8_t)(1u << t))) continue;
            size_t sz = wb_world_comp_elem_size(w, types[t]);
            const unsigned char *base = (const unsigned char *)wb_world_comp_data(w, types[t]);
            h = wb_det_checksum_stream(h, base + (size_t)i * sz, sz);
        }
    }
    return h;
}

int wb_snap_restore(wb_world *w, const wb_comp_type *types, int ntypes, const wb_snap *s) {
    if (!w || !s || !types || ntypes < 0) return 0;
    const uint8_t *p = s->bytes;
    size_t left = s->len;
    int written = 0;
    while (left >= sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint8_t)) {
        uint32_t idx, gen;
        uint8_t mask;
        memcpy(&idx, p, sizeof(idx));
        memcpy(&gen, p + sizeof(uint32_t), sizeof(gen));
        mask = p[sizeof(uint32_t) + sizeof(uint32_t)];
        p += sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint8_t);
        left -= sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint8_t);

        int restore_here = wb_world_slot_alive(w, idx) && wb_world_slot_gen(w, idx) == gen;
        int ok = 1;
        for (int t = 0; t < ntypes && ok; t++) {
            if (!(mask & (uint8_t)(1u << t))) continue;
            size_t sz = wb_world_comp_elem_size(w, types[t]);
            if (left < sz) { ok = 0; break; }       /* truncated pack */
            if (restore_here && wb_world_comp_present(w, types[t], idx)) {
                unsigned char *base = (unsigned char *)wb_world_comp_data(w, types[t]);
                memcpy(base + (size_t)idx * sz, p, sz);
            }
            p += sz;
            left -= sz;
        }
        if (ok) written += restore_here ? 1 : 0;
    }
    return written;
}
