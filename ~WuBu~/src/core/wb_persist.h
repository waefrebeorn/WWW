/*
 * wb_persist.h -- per-level persistence (LP-102 stone 1).
 *
 * LP-50 shipped the durable store: a snapshot plus an append-only tail. What it
 * does not say is WHOSE bytes go in it. A world is a ladder of levels -- world
 * seed, continent geometry, city props, patch terrain, stadium session -- and
 * they are not equally durable. The seed is written once and never changes.
 * Continent geometry is not stored at all: it is derived from the seed, so
 * storing it would keep a copy that can disagree with its source. City props and
 * patch terrain are real durable bytes, owned by their own level. A stadium
 * session is not durable: it ends when the last player leaves, and persisting it
 * would resurrect a match that is over.
 *
 * So each level declares a CLASS, and the class answers three questions: does
 * this level own durable bytes, is it immutable once written, and is it stored
 * at all. The owner of a cell's durable state is then a rule rather than a
 * configuration:
 *
 *   the deepest level at or above the cell whose class owns durable bytes.
 *
 * A stadium cell is therefore owned by the patch below it -- its session is
 * ephemeral, so the durable state of that place lives one level up. A continent
 * cell is owned by the world, because continent geometry is derived and owns
 * nothing. A write is refused unless it targets the owner, which is what keeps a
 * coarse level's state from being persisted into a fine level, or the reverse.
 *
 * C11, no deps beyond the quadkey in wb_qkey. Deterministic.
 */
#ifndef WB_PERSIST_H
#define WB_PERSIST_H

#include <stdint.h>

#include "wb_qkey.h"

#define WB_PERSIST_MAX 8
#define WB_PERSIST_NAME 32

typedef enum {
    WB_PERSIST_SEED = 0,       /* the world seed: written once, never changed */
    WB_PERSIST_DERIVED = 1,    /* computed from a coarser level: owns no bytes */
    WB_PERSIST_AUTHORED = 2,   /* authored content: durable, mutable */
    WB_PERSIST_PATCHED = 3,    /* runtime-mutated state: durable, mutable */
    WB_PERSIST_EPHEMERAL = 4   /* a session: never durable */
} wb_persist_class;

typedef struct {
    char     name[WB_PERSIST_NAME];
    uint32_t depth;    /* the quadkey depth this level sits at */
    uint32_t cls;      /* a wb_persist_class */
    uint32_t written;  /* 1 once this level has accepted a durable write */
} wb_persist_level;

typedef struct {
    wb_persist_level levels[WB_PERSIST_MAX];
    uint32_t         n;
    uint64_t         epoch;    /* bumped on every accepted write */
} wb_persist_plan;

void wb_persist_init(wb_persist_plan *p);
uint32_t wb_persist_count(const wb_persist_plan *p);
uint64_t wb_persist_epoch(const wb_persist_plan *p);

/* 1 if the class owns durable bytes (SEED, AUTHORED, PATCHED). */
int wb_persist_class_durable(wb_persist_class cls);
/* 1 if the class is immutable once written (SEED). */
int wb_persist_class_immutable(wb_persist_class cls);
const char *wb_persist_class_name(wb_persist_class cls);

/* Add a level. Depths must strictly increase, so the plan stays ordered from the
 * coarsest level to the finest. A DERIVED level must have a coarser durable
 * level to derive from. Returns 0 on success, -1 if refused. */
int wb_persist_add(wb_persist_plan *p, const char *name, uint32_t depth,
                   wb_persist_class cls);

const char *wb_persist_name(const wb_persist_plan *p, uint32_t i);
uint32_t wb_persist_depth(const wb_persist_plan *p, uint32_t i);
wb_persist_class wb_persist_level_class(const wb_persist_plan *p, uint32_t i);
uint32_t wb_persist_written(const wb_persist_plan *p, uint32_t i);

/* The level a cell's durable state is stored at: the deepest durable level at
 * or above the cell's depth. Returns -1 if there is none -- the cell is
 * shallower than every durable level, or the plan has no durable level. */
int wb_persist_owner(const wb_persist_plan *p, const char *cell);
int wb_persist_owner_for_depth(const wb_persist_plan *p, uint32_t depth);

/* The level a DERIVED level is computed from: the nearest coarser durable
 * level, skipping any derived ones in between. Returns -1 if there is none. */
int wb_persist_source(const wb_persist_plan *p, uint32_t level);

/* Record a durable write of `cell` to `level`. Refused if the level is out of
 * range, owns no durable bytes, is not the cell's owner, or is immutable and
 * already written. On success `epoch_out`, if non-NULL, receives the plan epoch
 * and 0 is returned; a refusal advances nothing. */
int wb_persist_write(wb_persist_plan *p, const char *cell, uint32_t level,
                     uint64_t *epoch_out);

#endif /* WB_PERSIST_H */
