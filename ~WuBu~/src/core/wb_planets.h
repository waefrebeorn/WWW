/*
 * wb_planets.h -- LOD across planets (LP-109 stone 1).
 *
 * LP-101 laid a LOD chain across the levels of ONE place: universe, world,
 * continent, city. It stops at the planet, because a chain is a single ordered
 * list and a planet is not a depth -- it is a BRANCH. Mars and Earth both have a
 * continent level at depth 6, and a cell on Mars must never resolve to a level on
 * Earth.
 *
 * So the chain is two-tier: one UNIVERSE level above every planet, and each planet
 * carrying its own chain below it (continent, city, building, room). A planet's
 * combined chain is the universe level followed by its own levels, and the
 * representation rule is unchanged from LP-101:
 *
 *   the deepest level at or above the cell.
 *
 * The difference is what the universe level's cell IS. At the universe level the
 * representing cell is the planet itself, so the imposter of anything on Mars is
 * "mars" -- not a quadkey, which at that depth would be the same empty string for
 * every planet and therefore identify nothing. Below the universe level the
 * imposter is the ancestor quadkey, exactly as before.
 *
 * C11, no deps beyond the quadkey in wb_qkey and the chain in wb_lodchain.
 * Deterministic.
 */
#ifndef WB_PLANETS_H
#define WB_PLANETS_H

#include <stddef.h>
#include <stdint.h>

#include "wb_lodchain.h"

#define WB_PLANETS_MAX 8
#define WB_PLANET_NAME 32

typedef struct {
    char     name[WB_PLANET_NAME];
    uint64_t id;
    wb_lodchain chain;    /* the planet's own levels, coarsest first */
} wb_planet;

typedef struct {
    wb_planet *planets;
    uint32_t   cap;
    uint32_t   n;
    uint32_t   universe_depth;   /* the depth of the universe level */
} wb_planets;

int  wb_planets_init(wb_planets *p, uint32_t cap);
void wb_planets_free(wb_planets *p);
void wb_planets_clear(wb_planets *p);
uint32_t wb_planets_count(const wb_planets *p);

/* Add a planet. Names and ids must be unique and non-zero. Returns the index, or
 * -1 if refused. */
int  wb_planets_add(wb_planets *p, const char *name, uint64_t id);
int  wb_planets_find(const wb_planets *p, const char *name);
const char *wb_planets_name(const wb_planets *p, uint32_t i);

/* Set the universe level's depth. It must be strictly shallower than every
 * planet level already added, or the universe would no longer be above them.
 * Returns 0 on success, -1 if refused. */
int  wb_planets_set_universe_depth(wb_planets *p, uint32_t depth);
uint32_t wb_planets_universe_depth(const wb_planets *p);

/* Add a level to a planet's own chain. The depth must be strictly greater than
 * the universe depth. Returns 0 on success, -1 if refused. */
int  wb_planets_add_level(wb_planets *p, uint32_t planet, const char *name,
                          uint32_t depth, uint32_t budget);

/* The number of levels in a planet's COMBINED chain: the universe level plus the
 * planet's own. Returns 0 if the planet is out of range. */
uint32_t wb_planets_levels(const wb_planets *p, uint32_t planet);

/* The level that represents `cell` on `planet`: 0 is the universe level, 1..n are
 * the planet's own levels, coarsest first. Returns -1 if the planet or cell is
 * invalid, or the cell is shallower than the universe level. */
int  wb_planets_level_for(const wb_planets *p, uint32_t planet, const char *cell);

/* The depth of a combined level: the universe depth for 0, otherwise the depth of
 * the planet level at that index. Returns 0 if the index is out of range. */
uint32_t wb_planets_depth(const wb_planets *p, uint32_t planet, uint32_t level);

/* The name of a combined level: "universe" for 0, otherwise the planet level's
 * name. Returns NULL if the index is out of range. */
const char *wb_planets_level_name(const wb_planets *p, uint32_t planet, uint32_t level);

/* The imposter: the representation of `cell` at `level`. At the universe level
 * the representing cell is the planet itself, so the imposter is the planet's
 * name; at a planet level it is the ancestor quadkey. Returns 0 on success, -1 if
 * the planet, cell or level is invalid, or the level is finer than the cell (a
 * level cannot invent detail it does not have). */
int  wb_planets_imposter(const wb_planets *p, uint32_t planet, const char *cell,
                         uint32_t level, char *out, size_t cap);

#endif /* WB_PLANETS_H */
