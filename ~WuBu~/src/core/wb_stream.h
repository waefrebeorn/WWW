/*
 * wb_stream.h -- streaming budget selection (LP-13 stone 1).
 *
 * A streamer has more requests than bandwidth: each texture/mesh wants a
 * resolution based on its screen size and distance, and only a byte budget is
 * available this frame. This module answers the question "which requests do we
 * load now" -- the highest-priority requests that fit, ties broken by request
 * order so the result is deterministic.
 *
 * C11, no deps.
 */
#ifndef WB_STREAM_H
#define WB_STREAM_H

#include <stdint.h>

typedef struct {
    uint32_t id;
    float    priority;   /* higher = more needed right now */
    uint32_t bytes;      /* the cost to load it */
} wb_stream_req;

/* Select requests into `out_indices` (n entries) in priority order until the
 * byte budget is exhausted. Returns the number selected. */
uint32_t wb_stream_select(const wb_stream_req *reqs, uint32_t n,
                          uint32_t budget, uint32_t *out_indices);

#endif /* WB_STREAM_H */
