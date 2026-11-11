/*
 * wb_resv.h -- the nested-volume reservation ledger (LP-107 stone 1).
 *
 * Every platform reserves a different unit in its own silo: a server slot, a
 * place GUID, a compute node, a parcel. WWW reserves SPATIAL VOLUMES, and the
 * volumes nest -- universe > world > continent > city > patch > stadium -- so
 * the ledger has to answer a spatial question, not just a table lookup: may this
 * holder take this volume while that one holds its parent?
 *
 * The rule is exclusive-by-default with an explicit delegation escape:
 *
 *   - A volume has at most ONE holder.
 *   - Holding a volume excludes every DESCENDANT of it, unless the volume is
 *     marked WB_RESV_DELEGABLE.
 *   - Holding a volume excludes every ANCESTOR of it, unless that ancestor is
 *     marked WB_RESV_DELEGABLE.
 *
 * So reserving "Earth" blocks "Europe" outright; reserving "Europe" while
 * "Earth" is held is allowed only if Earth was added as delegable -- the shape
 * of a planet server that hands continents to other nodes.
 *
 * Every mutation bumps a ledger-wide epoch, which is what a distributed caller
 * fences on: a reservation that returns success at epoch N is stale the moment
 * the epoch moves.
 *
 * C11, no deps. Deterministic: a refusal never depends on entry order.
 */
#ifndef WB_RESV_H
#define WB_RESV_H

#include <stdint.h>

#define WB_RESV_DELEGABLE (1u << 0)

typedef struct {
    uint64_t volume;     /* a volume id, unique in the ledger */
    uint64_t parent;     /* the parent volume id, 0 for a root */
    uint64_t holder;    /* 0 = free */
    uint32_t flags;      /* WB_RESV_* */
} wb_resv_entry;

typedef struct {
    wb_resv_entry *entries;
    uint32_t       cap;
    uint32_t       n;
    uint64_t       epoch;      /* bumped on every successful mutation */
} wb_resv_ledger;

int  wb_resv_init(wb_resv_ledger *l, uint32_t cap);
void wb_resv_free(wb_resv_ledger *l);
void wb_resv_clear(wb_resv_ledger *l);

/* Add a volume. Returns 0 on success, -1 if the id is a duplicate, unknown
 * (including its own parent not being present yet), the ledger is full, or a
 * volume would be its own ancestor. */
int  wb_resv_add(wb_resv_ledger *l, uint64_t volume, uint64_t parent, uint32_t flags);

/* Reserve a volume for a holder. Returns 0 on success, -1 if refused. */
int  wb_resv_reserve(wb_resv_ledger *l, uint64_t volume, uint64_t holder);

/* Release a volume. Returns 0 on success, -1 if the volume is free or the caller
 * is not its holder. */
int  wb_resv_release(wb_resv_ledger *l, uint64_t volume, uint64_t holder);

uint64_t wb_resv_holder(const wb_resv_ledger *l, uint64_t volume);
int      wb_resv_is_held(const wb_resv_ledger *l, uint64_t volume);
int      wb_resv_is_delegable(const wb_resv_ledger *l, uint64_t volume);

/* 1 if `maybe_ancestor` is `volume` or an ancestor of it. */
int      wb_resv_is_ancestor(const wb_resv_ledger *l, uint64_t volume, uint64_t maybe_ancestor);

/* 1 if `holder` holds `volume` or any ancestor of it -- i.e. the holder's
 * reservation covers this volume. */
int      wb_resv_covers(const wb_resv_ledger *l, uint64_t volume, uint64_t holder);

uint64_t wb_resv_epoch(const wb_resv_ledger *l);
uint32_t wb_resv_count(const wb_resv_ledger *l);

#endif /* WB_RESV_H */
