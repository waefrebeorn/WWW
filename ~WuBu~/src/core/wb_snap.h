/*
 * wb_snap.h -- per-tick world snapshot + digest (LP-03 stone 3).
 *
 * The checksum a replay, rollback or mesh authority compares every tick:
 * a snapshot packs the delivered state of a world -- every live entity in
 * dense-index order, its generation, then the raw bytes of each caller-listed
 * component type that is present -- and digests the whole pack with FNV-1a.
 *
 * The index order is construction-stable: an identically built and stepped
 * world yields the identical digest, so two runs that disagree show up as two
 * different digests. Which components are "state" is the caller's choice.
 */
#ifndef WB_SNAP_H
#define WB_SNAP_H

#include "wb_ecs.h"
#include <stdint.h>

typedef struct wb_snap wb_snap;

/* Pack + digest the live world. types[] names the component types that count
 * as state (typically position/velocity). Returns NULL on alloc failure. */
wb_snap *wb_snap_capture(wb_world *w, const wb_comp_type *types, int ntypes);
void     wb_snap_free(wb_snap *s);

uint64_t wb_snap_digest(const wb_snap *s);
int      wb_snap_equal(const wb_snap *a, const wb_snap *b);

#endif /* WB_SNAP_H */
