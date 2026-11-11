/*
 * wb_travel_test.c -- LP-100 stone 1 gate: cross-level authority and travel.
 *
 * Devil's-advocate cases: the source keeps ownership THROUGH the handover, so a
 * traveler is never ownerless; a commit whose epoch fence is stale is refused
 * rather than applied to an old intent; committing or aborting when nothing is in
 * flight is refused; a traveler cannot begin a transfer to the level it is already
 * on; and a refusal never advances the ledger epoch, because the epoch is the
 * fence every caller optimistically trusts.
 */
#include "wb_travel.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

#define L_EARTH 10
#define L_MOON  20
#define L_MARS  30

int main(void) {
    wb_travel_ledger l;
    CHECK(wb_travel_init(&l, 8) == 0, "the ledger is created");
    CHECK(wb_travel_count(&l) == 0, "no travelers are known");

    /* ---- arrive ---- */
    CHECK(wb_travel_arrive(&l, 1, L_EARTH) == 0, "a traveler arrives at earth");
    CHECK(wb_travel_count(&l) == 1, "one traveler is known");
    CHECK(wb_travel_owner(&l, 1) == L_EARTH, "earth owns it");
    uint32_t st = 0;
    CHECK(wb_travel_state(&l, 1, &st) == 1 && st == WB_TRAVEL_SETTLED, "and it is settled");
    CHECK(wb_travel_pending(&l, 1) == 0, "with no pending target");
    CHECK(wb_travel_arrive(&l, 1, L_MOON) == -1, "arriving twice is refused");
    CHECK(wb_travel_owner(&l, 1) == L_EARTH, "and the first arrival stands");
    CHECK(wb_travel_arrive(&l, 0, L_EARTH) == -1, "traveler 0 is refused (it means unknown)");
    CHECK(wb_travel_arrive(&l, 2, 0) == -1, "level 0 is refused");
    CHECK(wb_travel_owner(&l, 99) == 0, "an unknown traveler has no owner");
    CHECK(wb_travel_state(&l, 99, &st) == 0, "and no state");

    /* ---- begin ---- */
    uint64_t e0 = wb_travel_epoch(&l);
    uint64_t fence = 0;
    CHECK(wb_travel_begin(&l, 1, L_MOON, &fence) == 0, "a transfer begins");
    CHECK(fence == e0 + 1, "the begin returns the ledger epoch to fence on");
    CHECK(wb_travel_state(&l, 1, &st) == 1 && st == WB_TRAVEL_IN_FLIGHT, "the traveler is in flight");
    CHECK(wb_travel_owner(&l, 1) == L_EARTH, "the source KEEPS ownership during the handover");
    CHECK(wb_travel_pending(&l, 1) == L_MOON, "and the target is recorded");
    CHECK(wb_travel_begin(&l, 1, L_MARS, &fence) == -1, "a second begin while in flight is refused");
    CHECK(wb_travel_pending(&l, 1) == L_MOON, "and the original target stands");
    CHECK(wb_travel_begin(&l, 1, L_EARTH, &fence) == -1, "a transfer to the current level is refused");
    CHECK(wb_travel_begin(&l, 99, L_MOON, &fence) == -1, "an unknown traveler cannot begin");
    CHECK(wb_travel_begin(&l, 2, 0, &fence) == -1, "a transfer to level 0 is refused");

    /* ---- commit ---- */
    CHECK(wb_travel_commit(&l, 1, fence) == 0, "the commit with the matching fence succeeds");
    CHECK(wb_travel_owner(&l, 1) == L_MOON, "ownership flipped atomically to the moon");
    CHECK(wb_travel_pending(&l, 1) == 0, "the pending target is cleared");
    CHECK(wb_travel_state(&l, 1, &st) == 1 && st == WB_TRAVEL_SETTLED, "and the traveler is settled again");
    CHECK(wb_travel_commit(&l, 1, fence) == -1, "committing again is refused (nothing in flight)");
    CHECK(wb_travel_commit(&l, 99, fence) == -1, "committing an unknown traveler is refused");

    /* ---- a stale fence is refused ---- */
    CHECK(wb_travel_arrive(&l, 2, L_EARTH) == 0, "a second traveler arrives");
    uint64_t f2 = 0;
    CHECK(wb_travel_begin(&l, 2, L_MARS, &f2) == 0, "it begins a transfer");
    CHECK(wb_travel_begin(&l, 1, L_EARTH, NULL) == 0, "an unrelated transfer moves the ledger");
    CHECK(wb_travel_commit(&l, 2, f2) == -1, "the stale fence is refused");
    CHECK(wb_travel_owner(&l, 2) == L_EARTH, "and the traveler never moved");
    CHECK(wb_travel_pending(&l, 2) == L_MARS, "with its intent still recorded");
    CHECK(wb_travel_abort(&l, 2) == 0, "it aborts instead");
    CHECK(wb_travel_owner(&l, 2) == L_EARTH, "and the source keeps ownership");

    /* ---- abort ---- */
    uint64_t f3 = 0;
    CHECK(wb_travel_begin(&l, 2, L_MARS, &f3) == 0, "a transfer begins again");
    CHECK(wb_travel_abort(&l, 2) == 0, "and aborts");
    CHECK(wb_travel_owner(&l, 2) == L_EARTH, "the source kept ownership");
    CHECK(wb_travel_pending(&l, 2) == 0, "the intent is cleared");
    CHECK(wb_travel_state(&l, 2, &st) == 1 && st == WB_TRAVEL_SETTLED, "and it is settled");
    CHECK(wb_travel_abort(&l, 2) == -1, "aborting when settled is refused");
    CHECK(wb_travel_abort(&l, 99) == -1, "aborting an unknown traveler is refused");

    /* ---- the epoch is the fence, and only successes move it ---- */
    uint64_t before = wb_travel_epoch(&l);
    CHECK(wb_travel_arrive(&l, 3, L_EARTH) == 0, "an arrival succeeds");
    CHECK(wb_travel_epoch(&l) == before + 1, "an arrival advances it");
    before = wb_travel_epoch(&l);
    CHECK(wb_travel_begin(&l, 3, L_MOON, &fence) == 0, "a begin succeeds");
    CHECK(wb_travel_epoch(&l) == before + 1, "a begin advances it");
    before = wb_travel_epoch(&l);
    CHECK(wb_travel_abort(&l, 3) == 0, "an abort succeeds");
    CHECK(wb_travel_epoch(&l) == before + 1, "an abort advances it");
    before = wb_travel_epoch(&l);
    CHECK(wb_travel_begin(&l, 3, L_MOON, &fence) == 0, "a begin succeeds again");
    uint64_t fb = fence;
    CHECK(wb_travel_commit(&l, 3, fb) == 0, "a commit succeeds");
    CHECK(wb_travel_epoch(&l) == before + 2, "a commit advances it");
    /* refusals */
    const uint64_t frozen = wb_travel_epoch(&l);
    CHECK(wb_travel_commit(&l, 3, fb) == -1, "a repeated commit is refused");
    CHECK(wb_travel_begin(&l, 3, L_MOON, NULL) == -1, "a no-op begin is refused");
    CHECK(wb_travel_begin(&l, 99, L_MOON, NULL) == -1, "an unknown begin is refused");
    CHECK(wb_travel_abort(&l, 3) == -1, "an abort with nothing in flight is refused");
    CHECK(wb_travel_arrive(&l, 3, L_MARS) == -1, "a duplicate arrival is refused");
    CHECK(wb_travel_epoch(&l) == frozen, "and none of those refusals advanced the epoch");

    /* ---- a full ledger refuses ---- */
    {
        wb_travel_ledger one;
        CHECK(wb_travel_init(&one, 1) == 0, "a one-slot ledger is created");
        CHECK(wb_travel_arrive(&one, 1, L_EARTH) == 0, "the slot is taken");
        CHECK(wb_travel_arrive(&one, 2, L_EARTH) == -1, "a full ledger refuses an arrival");
        wb_travel_free(&one);
    }

    /* ---- clear ---- */
    wb_travel_clear(&l);
    CHECK(wb_travel_count(&l) == 0, "clear forgets every traveler");
    CHECK(wb_travel_owner(&l, 1) == 0, "and nothing is owned");
    CHECK(wb_travel_epoch(&l) == 1, "and resets the epoch");

    /* ---- determinism ---- */
    wb_travel_ledger x, y;
    CHECK(wb_travel_init(&x, 8) == 0 && wb_travel_init(&y, 8) == 0, "two ledgers are created");
    CHECK(wb_travel_arrive(&x, 1, L_EARTH) == 0 && wb_travel_arrive(&y, 1, L_EARTH) == 0,
          "both receive the same traveler");
    uint64_t ex = 0, ey = 0;
    CHECK(wb_travel_begin(&x, 1, L_MOON, &ex) == 0 && wb_travel_begin(&y, 1, L_MOON, &ey) == 0,
          "both begin the same transfer");
    CHECK(ex == ey, "and hand back the same fence");
    CHECK(wb_travel_commit(&x, 1, ex) == 0 && wb_travel_commit(&y, 1, ey) == 0,
          "both commit");
    CHECK(wb_travel_owner(&x, 1) == wb_travel_owner(&y, 1), "to the same owner");
    CHECK(wb_travel_epoch(&x) == wb_travel_epoch(&y), "and end on the same epoch");

    /* ---- degenerate input ---- */
    CHECK(wb_travel_init(NULL, 4) == -1, "a NULL ledger is rejected");
    CHECK(wb_travel_init(&x, 0) == -1, "a zero capacity is rejected");
    CHECK(wb_travel_arrive(NULL, 1, L_EARTH) == -1, "a NULL ledger refuses an arrival");
    CHECK(wb_travel_begin(NULL, 1, L_MOON, NULL) == -1, "a NULL ledger refuses a begin");
    CHECK(wb_travel_commit(NULL, 1, 1) == -1, "a NULL ledger refuses a commit");
    CHECK(wb_travel_abort(NULL, 1) == -1, "a NULL ledger refuses an abort");
    CHECK(wb_travel_owner(NULL, 1) == 0, "a NULL ledger owns no one");
    CHECK(wb_travel_pending(NULL, 1) == 0, "a NULL ledger has no pending target");
    CHECK(wb_travel_state(NULL, 1, &st) == 0, "a NULL ledger has no state");
    CHECK(wb_travel_epoch(NULL) == 0, "a NULL ledger has no epoch");
    CHECK(wb_travel_count(NULL) == 0, "a NULL ledger has no travelers");

    wb_travel_free(&l);
    wb_travel_free(&x);
    wb_travel_free(&y);
    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
