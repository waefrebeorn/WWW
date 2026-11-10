/*
 * wb_history.h -- lag-compensation rewind buffer (LP-30 stone 1).
 *
 * Lag compensation answers "where was the target when the shooter pulled the
 * trigger?", not "where is it now". That needs the past: a ring of past world
 * states with timestamps, and a sampler that interpolates between the two
 * snapshots bracketing the requested time.
 *
 * The sampler is the same nlerp used for snapshot interpolation (LP-29), so the
 * rewind is visually continuous rather than stepped.
 *
 * C11, no deps.
 */
#ifndef WB_HISTORY_H
#define WB_HISTORY_H

#include <stdint.h>

#include "wb_snapshot.h"

typedef struct {
    double              time;
    wb_snapshot_state  *states;
} wb_history_frame;

typedef struct {
    wb_history_frame *frames;   /* a ring of `cap` frames */
    uint32_t         cap;
    uint32_t         nentities;
    uint32_t         count;     /* frames currently stored */
    uint32_t         head;      /* the oldest frame's index */
} wb_history;

int  wb_history_init(wb_history *h, uint32_t cap, uint32_t nentities);
void wb_history_free(wb_history *h);
void wb_history_clear(wb_history *h);

/* Record a frame at `time` (states is nentities entries). */
int  wb_history_push(wb_history *h, double time, const wb_snapshot_state *states);

/* Interpolate the state at `time` into `out` (nentities entries). Returns 0 on
 * success, -1 on bad input. Times outside the recorded range clamp to the
 * nearest frame. */
int  wb_history_sample(const wb_history *h, double time, wb_snapshot_state *out);

uint32_t wb_history_count(const wb_history *h);

#endif /* WB_HISTORY_H */
