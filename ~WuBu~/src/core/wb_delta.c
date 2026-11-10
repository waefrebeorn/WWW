/*
 * wb_delta.c -- delta compression (LP-28). See wb_delta.h.
 *
 * Layout: [ceil(nwords/8) bitmap bytes][changed 32-bit words, little-endian].
 * The final word is zero-padded when `len` is not a multiple of four; the
 * decoder restores only `len` bytes, so the padding never leaks.
 */
#include "wb_delta.h"

#include <stdint.h>
#include <string.h>

size_t wb_delta_max_size(size_t len) {
    size_t nwords = (len + 3) / 4;
    return (nwords + 7) / 8 + nwords * 4;
}

size_t wb_delta_encode(const void *old_state, const void *new_state, size_t len,
                       void *out, size_t out_cap) {
    if (!old_state || !new_state || !out || len == 0) return 0;
    size_t nwords = (len + 3) / 4;
    size_t bitmap_bytes = (nwords + 7) / 8;
    size_t need = bitmap_bytes + nwords * 4;
    if (out_cap < need) return 0;

    const uint8_t *a = (const uint8_t *)old_state;
    const uint8_t *b = (const uint8_t *)new_state;
    uint8_t *dst = (uint8_t *)out;
    memset(dst, 0, bitmap_bytes);

    uint8_t *words = dst + bitmap_bytes;
    size_t n_changed = 0;
    for (size_t w = 0; w < nwords; w++) {
        size_t off = w * 4;
        uint32_t av = 0, bv = 0;
        memcpy(&av, a + off, (len - off) < 4 ? (len - off) : 4);
        memcpy(&bv, b + off, (len - off) < 4 ? (len - off) : 4);
        if (av == bv) continue;
        dst[w >> 3] |= (uint8_t)(1u << (w & 7));
        uint8_t *wp = words + n_changed * 4;
        memcpy(wp, &bv, 4);
        n_changed++;
    }
    return bitmap_bytes + n_changed * 4;
}

size_t wb_delta_apply(const void *old_state, const void *delta, size_t len,
                      void *out, size_t out_cap) {
    if (!old_state || !delta || !out || len == 0 || out_cap < len) return 0;
    size_t nwords = (len + 3) / 4;
    size_t bitmap_bytes = (nwords + 7) / 8;

    const uint8_t *d = (const uint8_t *)delta;
    memcpy(out, old_state, len);

    const uint8_t *words = d + bitmap_bytes;
    size_t n_changed = 0;
    for (size_t w = 0; w < nwords; w++) {
        if (!(d[w >> 3] & (1u << (w & 7)))) continue;
        size_t off = w * 4;
        size_t n = (len - off) < 4 ? (len - off) : 4;
        memcpy((uint8_t *)out + off, words + n_changed * 4, n);
        n_changed++;
    }
    return len;
}
