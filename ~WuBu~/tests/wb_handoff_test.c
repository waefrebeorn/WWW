/*
 * wb_handoff_test.c -- LP-33 stone 1 gate: the authority handoff state machine.
 *
 * Devil's-advocate cases: the full accept lifecycle, the reject path, illegal
 * transitions from every state, the epoch fence dropping stale messages, and
 * determinism.
 */
#include "wb_handoff.h"

#include <stdio.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

int main(void) {
    wb_handoff h;
    wb_handoff_init(&h);
    CHECK(h.state == WB_HANDOFF_IDLE && h.epoch == 0, "a fresh handoff is idle at epoch 0");

    /* ---- the accept lifecycle ---- */
    CHECK(wb_handoff_begin(&h, 42, 1, 2) == 0, "begin a handoff");
    CHECK(h.state == WB_HANDOFF_FREEZING, "the source freezes the entity");
    CHECK(h.entity == 42 && h.source_server == 1 && h.target_server == 2, "the handoff records its parties");
    CHECK(h.epoch == 1, "the epoch bumps on begin");
    CHECK(wb_handoff_begin(&h, 43, 1, 3) == -1, "a second begin while running is rejected");

    CHECK(wb_handoff_accept(&h) == -1, "accepting before the freeze completes is rejected");
    CHECK(wb_handoff_freeze_done(&h) == 0, "the freeze completes");
    CHECK(h.state == WB_HANDOFF_HANDSHAKE, "the handshake begins");
    CHECK(wb_handoff_freeze_done(&h) == -1, "a second freeze-done is rejected");

    CHECK(wb_handoff_accept(&h) == 0, "the target accepts");
    CHECK(h.state == WB_HANDOFF_ACCEPTED, "authority has moved");
    CHECK(wb_handoff_rollback(&h) == -1, "rolling back an accepted handoff is rejected");
    CHECK(wb_handoff_commit(&h) == 0, "the source releases authority");
    CHECK(h.state == WB_HANDOFF_IDLE, "the handoff completes back at idle");

    /* ---- the reject path ---- */
    CHECK(wb_handoff_begin(&h, 7, 1, 2) == 0, "begin another handoff");
    uint32_t epoch = h.epoch;
    CHECK(epoch == 2, "the epoch bumped again");
    wb_handoff_freeze_done(&h);
    CHECK(wb_handoff_reject(&h) == 0, "the target rejects");
    CHECK(h.state == WB_HANDOFF_REJECTED, "the handoff is rejected");
    CHECK(wb_handoff_commit(&h) == -1, "committing a rejected handoff is rejected");
    CHECK(wb_handoff_rollback(&h) == 0, "the source resumes authority");
    CHECK(h.state == WB_HANDOFF_IDLE, "the rejected handoff returns to idle");

    /* ---- the epoch fence drops stale messages ---- */
    wb_handoff_begin(&h, 9, 1, 2);
    CHECK(wb_handoff_is_stale(&h, 0) == 1, "a message from the previous epoch is stale");
    CHECK(wb_handoff_is_stale(&h, h.epoch) == 0, "a message from the current epoch is fresh");
    wb_handoff_commit(&h);

    /* ---- illegal transitions from idle ---- */
    CHECK(wb_handoff_accept(&h) == -1, "accepting from idle is rejected");
    CHECK(wb_handoff_reject(&h) == -1, "rejecting from idle is rejected");
    CHECK(wb_handoff_commit(&h) == -1, "committing from idle is rejected");
    CHECK(wb_handoff_rollback(&h) == -1, "rolling back from idle is rejected");

    /* ---- a handoff to the same server is meaningless ---- */
    CHECK(wb_handoff_begin(&h, 1, 5, 5) == -1, "a handoff to the same server is rejected");

    /* ---- attempts count the handoffs ---- */
    CHECK(h.attempts == 3, "every begin counts as an attempt");

    /* ---- determinism ---- */
    wb_handoff a, b;
    wb_handoff_init(&a);
    wb_handoff_init(&b);
    int same = 1;
    same &= (wb_handoff_begin(&a, 1, 1, 2) == wb_handoff_begin(&b, 1, 1, 2));
    same &= (a.epoch == b.epoch) && (a.state == b.state);
    same &= (wb_handoff_freeze_done(&a) == wb_handoff_freeze_done(&b));
    same &= (wb_handoff_accept(&a) == wb_handoff_accept(&b));
    same &= (a.state == b.state);
    same &= (wb_handoff_commit(&a) == wb_handoff_commit(&b));
    same &= (a.state == b.state && a.epoch == b.epoch);
    CHECK(same, "the handoff state machine is deterministic");

    /* ---- degenerate input ---- */
    CHECK(wb_handoff_begin(NULL, 1, 1, 2) == -1, "a NULL handoff is rejected");
    CHECK(wb_handoff_is_stale(NULL, 0) == 1, "a NULL handoff treats every message as stale");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
