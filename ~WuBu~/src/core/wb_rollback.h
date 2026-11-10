/*
 * wb_rollback.h -- rollback netcode (LP-31 stone 1).
 *
 * Rollback lets a client predict remote players' inputs and correct them when
 * the real input arrives: restore the world to the tick the input belonged to
 * and re-simulate to the present. The expensive part is the re-simulation; the
 * bookkeeping is this module -- a ring of per-tick states plus the restore step.
 *
 * The simulation itself is a caller-supplied pure function, so the rollback
 * logic is testable without a world: any deterministic step function works.
 *
 * C11, no deps.
 */
#ifndef WB_ROLLBACK_H
#define WB_ROLLBACK_H

#include <stdint.h>

typedef uint64_t (*wb_rollback_step_fn)(uint64_t state, uint32_t input);

typedef struct {
    uint32_t tick;         /* the next tick to simulate */
    uint64_t state;        /* the current state */
    wb_rollback_step_fn step;

    struct wb_rollback_saved {
        uint32_t tick;
        uint64_t state;    /* the state at the START of this tick */
    } *saved;              /* indexed by tick % cap */
    uint32_t cap;
} wb_rollback;

int  wb_rollback_init(wb_rollback *r, uint32_t cap, wb_rollback_step_fn step);
void wb_rollback_free(wb_rollback *r);
void wb_rollback_clear(wb_rollback *r);

/* Save the current state and advance one tick with `input`. */
void wb_rollback_advance(wb_rollback *r, uint32_t input);

/* A delayed input arrives for `tick`. `inputs[0..]` are the corrected inputs for
 * ticks tick, tick+1, ... Re-simulates to the present. Returns the number of
 * ticks re-simulated, or -1 if `tick` is not in the buffer. */
int  wb_rollback_apply(wb_rollback *r, uint32_t tick, const uint32_t *inputs);

uint32_t wb_rollback_tick(const wb_rollback *r);
uint64_t wb_rollback_state(const wb_rollback *r);

#endif /* WB_ROLLBACK_H */
