/*
 * wb_snap.h -- per-tick world snapshot, digest + restore (LP-03 capstone).
 *
 * The substrate for replay, rollback and mesh authority:
 *   - wb_snap_capture packs the delivered state (every live entity in dense
 *     index order: index, generation, a presence mask, then the raw bytes of
 *     each caller-listed component type that is present) and digests it.
 *   - wb_snap_digest / wb_snap_equal: two runs that should agree, agree
 *     bit-for-bit; divergence shows as a different digest.
 *   - wb_snap_restore writes a captured snapshot back into a world that still
 *     holds the same entities (a rollback): a re-simulated run reproduces the
 *     captured digest, which is exactly the rollback-sync test LP-03 demands.
 *
 * Index order is construction-stable; the layout is read only through the
 * public ECS accessors. Which components are "state" is the caller's choice,
 * and restore() must be given the same types[] (same order) that captured.
 */
#ifndef WB_SNAP_H
#define WB_SNAP_H

#include "wb_ecs.h"
#include <stddef.h>
#include <stdint.h>

typedef struct wb_snap wb_snap;

wb_snap *wb_snap_capture(wb_world *w, const wb_comp_type *types, int ntypes);
void     wb_snap_free(wb_snap *s);

uint64_t wb_snap_digest(const wb_snap *s);
int      wb_snap_equal(const wb_snap *a, const wb_snap *b);
size_t   wb_snap_bytes(const wb_snap *s);   /* packed length */

/* Lightweight per-tick checksum: streams the same stable-ordered world state
 * through FNV-1a with no allocation. Identical to wb_snap_digest(capture()).
 * This is what a headless sim emits every tick to detect divergence. */
uint64_t wb_snap_digest_world(wb_world *w, const wb_comp_type *types, int ntypes);

/* Overwrite the component bytes of each captured entity that still exists at
 * the same index with the captured generation. Returns records written back;
 * entities gone/reallocated are skipped. */
int wb_snap_restore(wb_world *w, const wb_comp_type *types, int ntypes, const wb_snap *s);

#endif /* WB_SNAP_H */
