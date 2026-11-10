/*
 * wb_compact.c -- draw compaction (LP-16 stone 1). See wb_compact.h.
 */
#include "wb_compact.h"

#include <string.h>

uint32_t wb_compact(const void *items, uint32_t n, uint32_t stride,
                    const uint8_t *visible, void *out) {
    if (!items || !visible || !out || stride == 0 || n == 0) return 0;
    const unsigned char *src = (const unsigned char *)items;
    unsigned char *dst = (unsigned char *)out;
    uint32_t written = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (!visible[i]) continue;
        memcpy(dst + (size_t)written * stride, src + (size_t)i * stride, stride);
        written++;
    }
    return written;
}
