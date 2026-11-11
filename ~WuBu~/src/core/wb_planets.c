/*
 * wb_planets.c -- LOD across planets (LP-109 stone 1). See wb_planets.h.
 */
#include "wb_planets.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

int wb_planets_init(wb_planets *p, uint32_t cap) {
    if (!p || cap == 0) return -1;
    p->planets = (wb_planet *)calloc(cap, sizeof(wb_planet));
    if (!p->planets) return -1;
    p->cap = cap;
    p->n = 0;
    p->universe_depth = 0;
    return 0;
}

void wb_planets_free(wb_planets *p) {
    if (!p) return;
    free(p->planets);
    p->planets = NULL;
    p->cap = 0;
    p->n = 0;
}

void wb_planets_clear(wb_planets *p) {
    if (!p) return;
    p->n = 0;
    p->universe_depth = 0;
}

uint32_t wb_planets_count(const wb_planets *p) {
    return p ? p->n : 0;
}

int wb_planets_add(wb_planets *p, const char *name, uint64_t id) {
    if (!p || !p->planets) return -1;
    if (!name || !*name) return -1;
    if (id == 0) return -1;                       /* 0 means "unknown" */
    if (strlen(name) >= WB_PLANET_NAME) return -1;
    if (p->n >= p->cap) return -1;
    for (uint32_t i = 0; i < p->n; i++) {
        if (p->planets[i].id == id) return -1;    /* ids are unique */
        if (strcmp(p->planets[i].name, name) == 0) return -1;
    }

    wb_planet *pl = &p->planets[p->n++];
    memset(pl, 0, sizeof *pl);
    strncpy(pl->name, name, WB_PLANET_NAME - 1);
    pl->id = id;
    wb_lodchain_init(&pl->chain);
    return (int)(p->n - 1);
}

int wb_planets_find(const wb_planets *p, const char *name) {
    if (!p || !p->planets || !name) return -1;
    for (uint32_t i = 0; i < p->n; i++)
        if (strcmp(p->planets[i].name, name) == 0) return (int)i;
    return -1;
}

const char *wb_planets_name(const wb_planets *p, uint32_t i) {
    if (!p || !p->planets || i >= p->n) return NULL;
    return p->planets[i].name;
}

int wb_planets_set_universe_depth(wb_planets *p, uint32_t depth) {
    if (!p || !p->planets) return -1;
    /* The universe level must stay above every planet level, or a cell could be
     * asked to resolve to a level that is not its ancestor. */
    for (uint32_t i = 0; i < p->n; i++) {
        if (p->planets[i].chain.n > 0 &&
            depth >= p->planets[i].chain.levels[0].depth)
            return -1;
    }
    p->universe_depth = depth;
    return 0;
}

uint32_t wb_planets_universe_depth(const wb_planets *p) {
    return p ? p->universe_depth : 0;
}

int wb_planets_add_level(wb_planets *p, uint32_t planet, const char *name,
                         uint32_t depth, uint32_t budget) {
    if (!p || !p->planets || planet >= p->n) return -1;
    if (depth <= p->universe_depth) return -1;    /* a planet level is below the universe */
    return wb_lodchain_add(&p->planets[planet].chain, name, depth, budget);
}

uint32_t wb_planets_levels(const wb_planets *p, uint32_t planet) {
    if (!p || !p->planets || planet >= p->n) return 0;
    return 1 + p->planets[planet].chain.n;
}

int wb_planets_level_for(const wb_planets *p, uint32_t planet, const char *cell) {
    if (!p || !p->planets || planet >= p->n) return -1;
    if (!cell || !wb_qkey_valid(cell)) return -1;
    uint32_t depth = wb_qkey_level(cell);
    if (depth < p->universe_depth) return -1;     /* above the universe: nowhere */

    const wb_lodchain *c = &p->planets[planet].chain;
    int best = 0;                                 /* the universe level is always a candidate */
    for (uint32_t i = 0; i < c->n; i++) {
        if (c->levels[i].depth > depth) break;
        best = (int)i + 1;
    }
    return best;
}

uint32_t wb_planets_depth(const wb_planets *p, uint32_t planet, uint32_t level) {
    if (!p || !p->planets || planet >= p->n) return 0;
    if (level == 0) return p->universe_depth;
    const wb_lodchain *c = &p->planets[planet].chain;
    if (level > c->n) return 0;
    return c->levels[level - 1].depth;
}

const char *wb_planets_level_name(const wb_planets *p, uint32_t planet, uint32_t level) {
    if (!p || !p->planets || planet >= p->n) return NULL;
    if (level == 0) return "universe";
    const wb_lodchain *c = &p->planets[planet].chain;
    if (level > c->n) return NULL;
    return c->levels[level - 1].name;
}

int wb_planets_imposter(const wb_planets *p, uint32_t planet, const char *cell,
                        uint32_t level, char *out, size_t cap) {
    if (!p || !p->planets || planet >= p->n) return -1;
    if (!cell || !wb_qkey_valid(cell)) return -1;
    if (!out || cap == 0) return -1;
    if (level >= wb_planets_levels(p, planet)) return -1;

    if (level == 0) {
        /* At the universe level the representing cell is the planet itself. A
         * quadkey would be the same empty string for every planet and identify
         * nothing, so the imposter is the planet's name. */
        strncpy(out, p->planets[planet].name, cap - 1);
        out[cap - 1] = '\0';
        return 0;
    }
    /* A planet level cannot invent detail: a cell shallower than the level's
     * depth has no representation there. */
    return wb_lodchain_imposter(&p->planets[planet].chain, (uint32_t)(level - 1),
                                cell, out, cap);
}
