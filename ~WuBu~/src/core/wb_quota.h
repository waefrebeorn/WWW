/*
 * wb_quota.h -- per-volume capacity and quotas (LP-113 stone 1).
 *
 * LP-107 reserved volumes, but a reservation is BOOLEAN: held or free. Real
 * platforms do not work that way. Agones tracks counters and lists with a
 * capacity; a Second Life parcel has a prim allowance and an estate has a total;
 * an AWS capacity reservation is a number of units in an availability zone. In
 * every case the capacity is attached to a VOLUME, and the volumes nest.
 *
 * So the unit of capacity is not the ledger, it is the volume, and a reservation
 * consumes from the volume it lands in AND from every ancestor above it: a prim
 * on a parcel also spends the estate's allowance, and a stadium in a patch also
 * spends the patch's. The capacity a volume can actually offer is therefore the
 * smallest free capacity along its ancestor chain -- the bottleneck, not its own
 * number.
 *
 * A QUOTA is the same idea seen from the holder's side: a per-(volume, holder) cap
 * on what that holder may reserve in the volume's SUBTREE. It is how a parcel
 * owner gets an allowance without owning the parcel, and how one tenant is kept
 * from filling a shared world.
 *
 * A refusal never advances the epoch, so the epoch stays the fence a distributed
 * caller trusts.
 *
 * C11, no deps. Deterministic.
 */
#ifndef WB_QUOTA_H
#define WB_QUOTA_H

#include <stdint.h>

#define WB_QUOTA_MAX_VOLS   32
#define WB_QUOTA_MAX_USES   64

typedef struct {
    uint64_t volume;
    uint64_t parent;     /* 0 for a root */
    uint32_t capacity;   /* units this volume can hold, descendants included */
    uint32_t used;       /* units held in this volume, descendants included */
} wb_quota_vol;

typedef struct {
    uint64_t volume;
    uint64_t holder;
    uint32_t limit;      /* the cap in this volume's subtree; 0 = no quota */
    uint32_t used;       /* units this holder reserved against exactly this volume */
} wb_quota_use;

typedef struct {
    wb_quota_vol *vols;
    uint32_t      vcap;
    uint32_t      vn;
    wb_quota_use *uses;
    uint32_t      ucap;
    uint32_t      un;
    uint64_t      epoch;
} wb_quota_ledger;

int  wb_quota_init(wb_quota_ledger *l, uint32_t vcap, uint32_t ucap);
void wb_quota_free(wb_quota_ledger *l);
void wb_quota_clear(wb_quota_ledger *l);
uint32_t wb_quota_count(const wb_quota_ledger *l);
uint64_t wb_quota_epoch(const wb_quota_ledger *l);

/* Add a volume with a capacity. The parent must already be present (0 = a root),
 * and a volume may not be its own ancestor. Returns 0 on success, -1 if
 * refused. */
int  wb_quota_add(wb_quota_ledger *l, uint64_t volume, uint64_t parent, uint32_t capacity);

/* Set a holder's quota in a volume's subtree, or clear it with a limit of 0.
 * Refused if the volume is unknown, the holder is 0, or the new limit is below
 * what the holder already holds there. Returns 0 on success, -1 if refused. */
int  wb_quota_set(wb_quota_ledger *l, uint64_t volume, uint64_t holder, uint32_t limit);

/* Reserve `units` in `volume` for `holder`. Consumes capacity from the volume and
 * every ancestor, and counts against every quota the holder has on the chain.
 * Returns 0 on success, -1 if refused. */
int  wb_quota_reserve(wb_quota_ledger *l, uint64_t volume, uint64_t holder, uint32_t units);

/* Release `units` previously reserved in `volume` by `holder`. Returns 0 on
 * success, -1 if the holder has fewer than `units` reserved there. */
int  wb_quota_release(wb_quota_ledger *l, uint64_t volume, uint64_t holder, uint32_t units);

uint32_t wb_quota_capacity(const wb_quota_ledger *l, uint64_t volume);
uint32_t wb_quota_used(const wb_quota_ledger *l, uint64_t volume);
uint32_t wb_quota_room(const wb_quota_ledger *l, uint64_t volume);

/* The units that could still be reserved in `volume`: the smallest free capacity
 * along its ancestor chain. Returns 0 if the volume is unknown. */
uint32_t wb_quota_available(const wb_quota_ledger *l, uint64_t volume);

uint32_t wb_quota_limit(const wb_quota_ledger *l, uint64_t volume, uint64_t holder);
uint32_t wb_quota_holder_used(const wb_quota_ledger *l, uint64_t volume, uint64_t holder);

/* The holder's units anywhere in `volume`'s subtree. */
uint32_t wb_quota_subtree_used(const wb_quota_ledger *l, uint64_t volume, uint64_t holder);

#endif /* WB_QUOTA_H */
