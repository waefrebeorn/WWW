/*
 * wb_rollback_test.c -- LP-31 stone 1 gate: rollback netcode.
 *
 * Devil's-advocate cases: a rollback reproduces the same state as a fresh
 * simulation with the corrected input, the correction propagates, rolling back
 * to the future or an overwritten tick fails, and the result is deterministic.
 */
#include "wb_rollback.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

#define CAP 32
#define NTICK 12

/* a deterministic, order-sensitive step so a wrong input visibly diverges */
static uint64_t step_fn(uint64_t state, uint32_t input) {
    return state * 1103515245u + input * 2654435761u + 12345u;
}

int main(void) {
    wb_rollback r;
    CHECK(wb_rollback_init(&r, CAP, step_fn) == 0, "rollback buffer created");

    /* ---- a clean run: predicted inputs ---- */
    uint32_t inputs[NTICK];
    for (int i = 0; i < NTICK; i++) inputs[i] = (uint32_t)(i + 1);
    for (int i = 0; i < NTICK; i++) wb_rollback_advance(&r, inputs[i]);
    CHECK(wb_rollback_tick(&r) == NTICK, "advanced to the expected tick");
    uint64_t predicted = wb_rollback_state(&r);

    /* ---- a delayed input arrives for tick 4 ---- */
    uint32_t corrected[NTICK];
    memcpy(corrected, inputs, sizeof inputs);
    corrected[4] = 999u;                       /* what the server actually sent */

    int n = wb_rollback_apply(&r, 4, &corrected[4]);
    CHECK(n == NTICK - 4, "the correct number of ticks was re-simulated");
    uint64_t rolled = wb_rollback_state(&r);

    /* ---- the rollback must match a fresh simulation with the real inputs ---- */
    wb_rollback fresh;
    wb_rollback_init(&fresh, CAP, step_fn);
    for (int i = 0; i < NTICK; i++) wb_rollback_advance(&fresh, corrected[i]);
    CHECK(rolled == wb_rollback_state(&fresh), "rollback reproduces the fresh simulation exactly");

    /* ---- and it must differ from the un-corrected prediction ---- */
    CHECK(rolled != predicted, "the correction changes the state");

    /* ---- a second delayed input for a later tick ---- */
    corrected[8] = 777u;
    n = wb_rollback_apply(&r, 8, &corrected[8]);
    CHECK(n == NTICK - 8, "the second rollback re-simulated the right range");
    wb_rollback fresh2;
    wb_rollback_init(&fresh2, CAP, step_fn);
    for (int i = 0; i < NTICK; i++) wb_rollback_advance(&fresh2, corrected[i]);
    CHECK(wb_rollback_state(&r) == wb_rollback_state(&fresh2), "the second rollback matches too");

    /* ---- invalid rollbacks ---- */
    CHECK(wb_rollback_apply(&r, NTICK, inputs) == -1, "rolling back to the present fails");
    CHECK(wb_rollback_apply(&r, NTICK + 5, inputs) == -1, "rolling back to the future fails");
    CHECK(wb_rollback_apply(&r, 0xFFFFFFFFu, inputs) == -1, "a far-future tick fails");
    CHECK(wb_rollback_apply(&r, 4, NULL) == -1, "NULL inputs are rejected");

    /* ---- a tick overwritten by the ring fails ---- */
    wb_rollback small;
    wb_rollback_init(&small, 4, step_fn);
    for (int i = 0; i < 10; i++) wb_rollback_advance(&small, (uint32_t)i);
    CHECK(wb_rollback_apply(&small, 0, inputs) == -1, "a tick overwritten by the ring fails");
    CHECK(wb_rollback_apply(&small, 8, inputs) == 2, "a tick still in the ring succeeds");

    /* ---- determinism ---- */
    wb_rollback a, b;
    wb_rollback_init(&a, CAP, step_fn);
    wb_rollback_init(&b, CAP, step_fn);
    for (int i = 0; i < NTICK; i++) { wb_rollback_advance(&a, inputs[i]); wb_rollback_advance(&b, inputs[i]); }
    wb_rollback_apply(&a, 4, &corrected[4]);
    wb_rollback_apply(&b, 4, &corrected[4]);
    CHECK(wb_rollback_state(&a) == wb_rollback_state(&b), "rollback is deterministic");

    /* ---- clear ---- */
    wb_rollback_clear(&a);
    CHECK(wb_rollback_tick(&a) == 0 && wb_rollback_state(&a) == 0, "clear resets the buffer");
    CHECK(wb_rollback_apply(&a, 0, inputs) == -1, "an empty buffer rejects a rollback");

    /* ---- degenerate input ---- */
    CHECK(wb_rollback_init(NULL, CAP, step_fn) == -1, "a NULL buffer is rejected");
    CHECK(wb_rollback_init(&a, 0, step_fn) == -1, "a zero capacity is rejected");
    CHECK(wb_rollback_init(&a, CAP, NULL) == -1, "a NULL step function is rejected");

    wb_rollback_free(&r);
    wb_rollback_free(&fresh);
    wb_rollback_free(&fresh2);
    wb_rollback_free(&small);
    wb_rollback_free(&a);
    wb_rollback_free(&b);

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
