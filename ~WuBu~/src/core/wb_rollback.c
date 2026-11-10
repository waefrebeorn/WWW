/*
 * wb_rollback.c -- rollback netcode (LP-31 stone 1). See wb_rollback.h.
 */
#include "wb_rollback.h"

#include <stdlib.h>

int wb_rollback_init(wb_rollback *r, uint32_t cap, wb_rollback_step_fn step) {
    if (!r || cap == 0 || !step) return -1;
    r->saved = (struct wb_rollback_saved *)calloc(cap, sizeof(*r->saved));
    if (!r->saved) return -1;
    r->cap = cap;
    r->tick = 0;
    r->state = 0;
    r->step = step;
    return 0;
}

void wb_rollback_free(wb_rollback *r) {
    if (!r) return;
    free(r->saved);
    r->saved = NULL;
    r->cap = 0;
}

void wb_rollback_clear(wb_rollback *r) {
    if (!r || !r->saved) return;
    for (uint32_t i = 0; i < r->cap; i++) { r->saved[i].tick = 0; r->saved[i].state = 0; }
    r->tick = 0;
    r->state = 0;
}

void wb_rollback_advance(wb_rollback *r, uint32_t input) {
    if (!r || !r->saved) return;
    r->saved[r->tick % r->cap].tick = r->tick;   /* the state at the start of this tick */
    r->saved[r->tick % r->cap].state = r->state;
    r->state = r->step(r->state, input);
    r->tick++;
}

int wb_rollback_apply(wb_rollback *r, uint32_t tick, const uint32_t *inputs) {
    if (!r || !r->saved || !inputs) return -1;
    if (tick >= r->tick) return -1;                       /* the future or the present */
    if (tick + r->cap < r->tick) return -1;               /* overwritten */
    if (r->saved[tick % r->cap].tick != tick) return -1;  /* slot reused by a newer tick */

    r->state = r->saved[tick % r->cap].state;
    uint32_t target = r->tick;
    uint32_t n = 0;
    for (uint32_t k = tick; k < target; k++) {
        r->state = r->step(r->state, inputs[k - tick]);
        r->saved[(k + 1) % r->cap].tick = k + 1;
        r->saved[(k + 1) % r->cap].state = r->state;
        n++;
    }
    return (int)n;
}

uint32_t wb_rollback_tick(const wb_rollback *r) { return r ? r->tick : 0; }
uint64_t wb_rollback_state(const wb_rollback *r) { return r ? r->state : 0; }
