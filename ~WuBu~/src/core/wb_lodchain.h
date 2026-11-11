/*
 * wb_lodchain.h -- level-of-detail ACROSS levels (LP-101 stone 1).
 *
 * Distinct from wb_lod.h, which switches a single object between screen-space LODs
 * with hysteresis. This module is about the LOD chain that spans nested PLACE
 * levels, which is a different problem.
 *
 * LOD is normally defined WITHIN a level: the quadkey length is the LOD. That
 * stops at the level boundary. A cell deep in a city has no representation in the
 * world its city belongs to, so a server holding the world cannot draw an
 * imposter for it -- it has nothing to draw.
 *
 * A LOD CHAIN fixes that by naming each level's resolution. Given a chain
 * (universe at depth 2, world at 6, continent at 12, city at 18), every cell has a
 * representation at EVERY level at or above its own depth: its ancestor at that
 * level's depth. That ancestor is the imposter -- the aggregate that stands in for
 * the whole subtree.
 *
 *   cell "0123012301230123" (depth 16)
 *     at the universe level  -> "01"              (depth 2)
 *     at the world level      -> "012301"          (depth 6)
 *     at the continent level  -> "012301230123"   (depth 12)
 *     at its own level        -> itself
 *
 * Each level also carries a detail budget, so the chain doubles as the per-level
 * detail cap: the budget of the level that represents a cell is the budget that
 * applies to it.
 *
 * C11, no deps. Deterministic.
 */
#ifndef WB_LODCHAIN_H
#define WB_LODCHAIN_H

#include <stddef.h>
#include <stdint.h>

#include "wb_qkey.h"

#define WB_LODCHAIN_MAX 8
#define WB_LODCHAIN_NAME 32

typedef struct {
    char     name[WB_LODCHAIN_NAME];
    uint32_t depth;      /* the quadkey depth this level resolves to */
    uint32_t budget;     /* the detail budget at this level */
} wb_lodchain_level;

typedef struct {
    wb_lodchain_level levels[WB_LODCHAIN_MAX];
    uint32_t          n;
} wb_lodchain;

void wb_lodchain_init(wb_lodchain *c);
uint32_t wb_lodchain_count(const wb_lodchain *c);

/* Add a level. Depths must strictly increase, so the chain stays ordered from the
 * coarsest level to the finest. Returns 0 on success, -1 if refused. */
int  wb_lodchain_add(wb_lodchain *c, const char *name, uint32_t depth, uint32_t budget);

/* The level that represents a cell of `depth`: the deepest level whose depth is
 * < the cell's depth, or the finest level if the cell is deeper than all of them.
 * Returns -1 if the chain is empty or the cell is shallower than every level. */
int  wb_lodchain_level_for_depth(const wb_lodchain *c, uint32_t depth);

/* The imposter: the ancestor of `cell` at level `i`'s depth. Returns 0 on
 * success, -1 if the index is out of range or the cell is shallower than that
 * level's depth (a level cannot invent detail it does not have). */
int  wb_lodchain_imposter(const wb_lodchain *c, uint32_t i, const char *cell,
                          char *out, size_t cap);

/* The detail budget of the level that represents `cell`. Returns 0 on success. */
int  wb_lodchain_budget(const wb_lodchain *c, const char *cell, uint32_t *budget);

const char *wb_lodchain_name(const wb_lodchain *c, uint32_t i);

#endif /* WB_LODCHAIN_H */
