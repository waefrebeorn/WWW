/*
 * wb_compact.h -- draw compaction (LP-16 stone 1).
 *
 * A GPU-driven pipeline culls on the GPU and then compacts the surviving draws
 * into one contiguous buffer that the GPU iterates. The CPU-side half is the
 * same operation: given an arbitrary visibility mask, copy the survivors into a
 * dense output buffer in their original order, so the result is deterministic
 * and the draw count is known exactly.
 *
 * Generic over the item type via a byte stride. C11, no deps.
 */
#ifndef WB_COMPACT_H
#define WB_COMPACT_H

#include <stdint.h>

/* Copy items where `visible[i]` is nonzero into `out` (n * stride bytes).
 * Returns the number of items written. */
uint32_t wb_compact(const void *items, uint32_t n, uint32_t stride,
                    const uint8_t *visible, void *out);

#endif /* WB_COMPACT_H */
