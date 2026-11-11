/*
 * wb_travel.h -- cross-level authority and travel (LP-100 stone 1).
 *
 * Roblox teleports by PlaceId; Second Life crosses sims by leaving one server and
 * entering another. Either way a traveler reaches a boundary and the question is
 * which level's server owns them THERE, and whether the handover is atomic.
 *
 * A transfer is a two-phase escrow:
 *
 *   begin   -- the source keeps ownership, a pending target is recorded, and the
 *              traveler is marked IN_FLIGHT. Nothing has moved yet.
 *   commit  -- ownership flips to the target atomically and the epoch advances.
 *   abort   -- the pending target is dropped and the source keeps ownership.
 *
 * The source keeps simulating through the handover, so a traveler is never
 * ownerless; the epoch is what makes the flip safe against a stale message from an
 * earlier attempt -- a commit that does not match the recorded pending target is
 * refused rather than obeyed.
 *
 * C11, no deps. Deterministic.
 */
#ifndef WB_TRAVEL_H
#define WB_TRAVEL_H

#include <stdint.h>

typedef enum {
    WB_TRAVEL_SETTLED = 0,   /* owned by exactly one level */
    WB_TRAVEL_IN_FLIGHT = 1  /* authority is committed to moving */
} wb_travel_status;

typedef struct {
    uint64_t traveler;
    uint64_t owner;          /* the level holding authority right now */
    uint64_t pending;        /* the target level, or 0 when settled */
    uint32_t state;          /* a wb_travel_status */
    uint64_t epoch;          /* this traveler's own fence */
} wb_travel_rec;

typedef struct {
    wb_travel_rec *recs;
    uint32_t       cap;
    uint32_t       n;
    uint64_t       epoch;    /* ledger-wide fence, bumped on every mutation */
} wb_travel_ledger;

int  wb_travel_init(wb_travel_ledger *l, uint32_t cap);
void wb_travel_free(wb_travel_ledger *l);
void wb_travel_clear(wb_travel_ledger *l);

/* Settle a traveler at a level. Returns 0 on success, -1 if the traveler is
 * already known (settle is once-only; use begin/commit to move). */
int  wb_travel_arrive(wb_travel_ledger *l, uint64_t traveler, uint64_t level);

/* Begin a transfer to `to_level`. On success `epoch_out`, if non-NULL, receives
 * the ledger epoch to pass to wb_travel_commit -- the transfer is valid only if
 * the ledger has not moved since. Returns 0 on success, -1 if refused. */
int  wb_travel_begin(wb_travel_ledger *l, uint64_t traveler, uint64_t to_level,
                     uint64_t *epoch_out);

/* Complete a transfer. `expect_epoch` must be the ledger epoch returned by the
 * matching begin: if any other transfer, arrival or abort has moved the ledger
 * since, the commit is refused rather than applied to a stale intent.
 * Returns 0 on success, -1 if refused. */
int  wb_travel_commit(wb_travel_ledger *l, uint64_t traveler, uint64_t expect_epoch);

/* Cancel a transfer. Returns 0 on success, -1 if refused. */
int  wb_travel_abort(wb_travel_ledger *l, uint64_t traveler);

/* The level holding authority, or 0 if the traveler is unknown. */
uint64_t wb_travel_owner(const wb_travel_ledger *l, uint64_t traveler);

int      wb_travel_state(const wb_travel_ledger *l, uint64_t traveler, uint32_t *state);
uint64_t wb_travel_pending(const wb_travel_ledger *l, uint64_t traveler);
uint64_t wb_travel_epoch(const wb_travel_ledger *l);
uint32_t wb_travel_count(const wb_travel_ledger *l);

#endif /* WB_TRAVEL_H */
