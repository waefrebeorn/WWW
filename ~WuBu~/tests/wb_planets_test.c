/*
 * wb_planets_test.c -- LP-109 stone 1 gate: LOD across planets.
 *
 * Devil's-advocate cases: a cell on Mars must never resolve to a level on Earth,
 * so each planet carries its own chain and the two are never merged; the universe
 * level sits ABOVE every planet level, so setting it at or below one is refused
 * rather than letting a cell resolve to a level that is not its ancestor; a cell
 * shallower than the planet's coarsest level still has a representation -- the
 * planet itself at the universe level -- because the universe is a level like any
 * other; the universe level's imposter is the planet's NAME, not a quadkey, since
 * the empty string would identify every planet at once; and a level finer than the
 * cell is refused, because a level cannot invent detail it does not have.
 */
#include "wb_planets.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

#define P_EARTH 0
#define P_MARS  1

/* depth 30 */
static const char *DEEP = "01230123012301230123012301230123012301";

static void build(wb_planets *p) {
    wb_planets_init(p, 4);
    wb_planets_add(p, "earth", 1);
    wb_planets_add(p, "mars", 2);
    wb_planets_set_universe_depth(p, 0);
    wb_planets_add_level(p, P_EARTH, "continent", 6, 1000);
    wb_planets_add_level(p, P_EARTH, "city", 12, 500);
    wb_planets_add_level(p, P_EARTH, "building", 18, 100);
    wb_planets_add_level(p, P_EARTH, "room", 24, 20);
    wb_planets_add_level(p, P_MARS, "continent", 6, 1000);
    wb_planets_add_level(p, P_MARS, "city", 12, 500);
    wb_planets_add_level(p, P_MARS, "building", 18, 100);
    wb_planets_add_level(p, P_MARS, "room", 24, 20);
}

int main(void) {
    wb_planets p;
    build(&p);

    /* ---- the registry ---- */
    CHECK(wb_planets_count(&p) == 2, "two planets are registered");
    CHECK(wb_planets_find(&p, "earth") == P_EARTH, "a planet is found by name");
    CHECK(wb_planets_find(&p, "mars") == P_MARS, "the second planet is found too");
    CHECK(wb_planets_find(&p, "venus") == -1, "an unknown planet is not found");
    CHECK(wb_planets_find(&p, NULL) == -1, "a NULL name is not found");
    CHECK(strcmp(wb_planets_name(&p, P_MARS), "mars") == 0, "a planet keeps its name");
    CHECK(wb_planets_name(&p, 9) == NULL, "an out-of-range planet has no name");
    CHECK(wb_planets_universe_depth(&p) == 0, "the universe sits at depth 0");

    /* ---- adding planets ---- */
    CHECK(wb_planets_add(&p, "earth", 3) == -1, "a duplicate name is refused");
    CHECK(wb_planets_add(&p, "venus", 1) == -1, "a duplicate id is refused");
    CHECK(wb_planets_add(&p, "", 3) == -1, "an empty name is refused");
    CHECK(wb_planets_add(&p, "a-name-far-too-long-for-the-registry", 3) == -1,
          "an over-long name is refused");
    CHECK(wb_planets_add(&p, "venus", 0) == -1, "id 0 is refused (it means unknown)");
    CHECK(wb_planets_add(&p, NULL, 3) == -1, "a NULL name is refused");
    CHECK(wb_planets_add(NULL, "venus", 3) == -1, "a NULL registry refuses a planet");
    CHECK(wb_planets_count(&p) == 2, "the refused adds changed nothing");

    /* ---- the universe level stays above every planet ---- */
    CHECK(wb_planets_set_universe_depth(&p, 6) == -1,
          "the universe cannot move down to a planet level");
    CHECK(wb_planets_set_universe_depth(&p, 7) == -1,
          "the universe cannot move below a planet level");
    CHECK(wb_planets_set_universe_depth(&p, 0) == 0,
          "the universe can stay where it is");
    CHECK(wb_planets_universe_depth(&p) == 0, "and it did not move");
    CHECK(wb_planets_add_level(&p, P_EARTH, "habitat", 0, 10) == -1,
          "a level at the universe depth is refused");
    CHECK(wb_planets_add_level(&p, P_EARTH, "habitat", 1, 10) == -1,
          "a level above the planet's coarsest is refused");
    CHECK(wb_planets_add_level(&p, P_EARTH, "habitat", 6, 10) == -1,
          "a level at a depth the chain already uses is refused");
    CHECK(wb_planets_add_level(&p, P_EARTH, "habitat", 12, 10) == -1,
          "a level at a depth the chain already uses is refused");
    CHECK(wb_planets_add_level(&p, 9, "habitat", 30, 10) == -1,
          "an out-of-range planet refuses a level");
    CHECK(wb_planets_add_level(NULL, P_EARTH, "habitat", 30, 10) == -1,
          "a NULL registry refuses a level");
    CHECK(wb_planets_levels(&p, P_EARTH) == 5, "the refused levels changed nothing");

    /* ---- the combined chain ---- */
    CHECK(wb_planets_levels(&p, P_EARTH) == 5,
          "the combined chain is the universe plus the planet's four levels");
    CHECK(wb_planets_levels(&p, P_MARS) == 5, "mars has the same shape");
    CHECK(wb_planets_levels(&p, 9) == 0, "an out-of-range planet has no chain");
    CHECK(wb_planets_levels(NULL, P_EARTH) == 0, "a NULL registry has no chain");
    CHECK(wb_planets_depth(&p, P_EARTH, 0) == 0, "level 0 is the universe depth");
    CHECK(wb_planets_depth(&p, P_EARTH, 1) == 6, "level 1 is the continent");
    CHECK(wb_planets_depth(&p, P_EARTH, 4) == 24, "level 4 is the room");
    CHECK(wb_planets_depth(&p, P_EARTH, 5) == 0, "an out-of-range level has no depth");
    CHECK(strcmp(wb_planets_level_name(&p, P_EARTH, 0), "universe") == 0,
          "level 0 is named universe");
    CHECK(strcmp(wb_planets_level_name(&p, P_EARTH, 1), "continent") == 0,
          "level 1 is named continent");
    CHECK(strcmp(wb_planets_level_name(&p, P_MARS, 4), "room") == 0,
          "a planet level keeps its own name");
    CHECK(wb_planets_level_name(&p, P_EARTH, 5) == NULL,
          "an out-of-range level has no name");

    /* ---- which level represents a cell ---- */
    CHECK(wb_planets_level_for(&p, P_EARTH, "") == 0, "the root is the universe");
    CHECK(wb_planets_level_for(&p, P_EARTH, "0") == 0,
          "a cell above the continent is the universe");
    CHECK(wb_planets_level_for(&p, P_EARTH, "01") == 0,
          "a depth-2 cell is the universe");
    CHECK(wb_planets_level_for(&p, P_EARTH, "012301") == 1,
          "a continent cell is the continent");
    CHECK(wb_planets_level_for(&p, P_EARTH, "0123012301") == 1,
          "a cell between the continent and the city is the continent");
    CHECK(wb_planets_level_for(&p, P_EARTH, "012301230123") == 2, "a city cell is the city");
    CHECK(wb_planets_level_for(&p, P_EARTH, "012301230123012301") == 3,
          "a building cell is the building");
    CHECK(wb_planets_level_for(&p, P_EARTH, "012301230123012301230123") == 4,
          "a room cell is the room");
    CHECK(wb_planets_level_for(&p, P_EARTH, DEEP) == 4,
          "a cell below the finest level is the finest level");
    CHECK(wb_planets_level_for(&p, P_MARS, DEEP) == 4,
          "the same cell on mars resolves the same way");
    CHECK(wb_planets_level_for(&p, 9, DEEP) == -1, "an out-of-range planet has no level");
    CHECK(wb_planets_level_for(&p, P_EARTH, NULL) == -1, "a NULL cell has no level");
    CHECK(wb_planets_level_for(&p, P_EARTH, "abc") == -1,
          "a malformed quadkey has no level");
    CHECK(wb_planets_level_for(NULL, P_EARTH, DEEP) == -1,
          "a NULL registry has no level");

    /* ---- the imposter ---- */
    char out[WB_PLANET_NAME + WB_QKEY_MAX];
    CHECK(wb_planets_imposter(&p, P_EARTH, DEEP, 0, out, sizeof out) == 0 &&
          strcmp(out, "earth") == 0,
          "at the universe level the imposter is the planet's name");
    CHECK(wb_planets_imposter(&p, P_MARS, DEEP, 0, out, sizeof out) == 0 &&
          strcmp(out, "mars") == 0,
          "a cell on mars is represented by mars, not by earth");
    CHECK(wb_planets_imposter(&p, P_EARTH, "01", 0, out, sizeof out) == 0 &&
          strcmp(out, "earth") == 0,
          "even a coarse cell is the planet at the universe level");
    CHECK(wb_planets_imposter(&p, P_EARTH, DEEP, 1, out, sizeof out) == 0 &&
          strcmp(out, "012301") == 0,
          "the continent imposter is the ancestor at depth 6");
    CHECK(wb_planets_imposter(&p, P_EARTH, DEEP, 2, out, sizeof out) == 0 &&
          strcmp(out, "012301230123") == 0,
          "the city imposter is the ancestor at depth 12");
    CHECK(wb_planets_imposter(&p, P_EARTH, DEEP, 3, out, sizeof out) == 0 &&
          strcmp(out, "012301230123012301") == 0,
          "the building imposter is the ancestor at depth 18");
    CHECK(wb_planets_imposter(&p, P_EARTH, DEEP, 4, out, sizeof out) == 0 &&
          strcmp(out, "012301230123012301230123") == 0,
          "the room imposter is the ancestor at depth 24");
    CHECK(wb_planets_imposter(&p, P_MARS, DEEP, 1, out, sizeof out) == 0 &&
          strcmp(out, "012301") == 0,
          "mars has its own ancestor, not earth's");
    CHECK(wb_planets_imposter(&p, P_EARTH, "01", 1, out, sizeof out) == -1,
          "a level finer than the cell is refused");
    CHECK(wb_planets_imposter(&p, P_EARTH, "0123012301", 2, out, sizeof out) == -1,
          "the city cannot represent a cell above it");
    CHECK(wb_planets_imposter(&p, P_EARTH, DEEP, 5, out, sizeof out) == -1,
          "an out-of-range level is refused");
    CHECK(wb_planets_imposter(&p, 9, DEEP, 1, out, sizeof out) == -1,
          "an out-of-range planet is refused");
    CHECK(wb_planets_imposter(&p, P_EARTH, "abc", 1, out, sizeof out) == -1,
          "a malformed cell is refused");
    CHECK(wb_planets_imposter(&p, P_EARTH, DEEP, 1, NULL, sizeof out) == -1,
          "a NULL buffer is refused");
    CHECK(wb_planets_imposter(&p, P_EARTH, DEEP, 1, out, 0) == -1,
          "a zero-capacity buffer is refused");

    /* ---- planets are independent ---- */
    CHECK(wb_planets_add_level(&p, P_EARTH, "habitat", 30, 5) == 0,
          "a level is added to earth");
    CHECK(wb_planets_levels(&p, P_EARTH) == 6, "earth has one more level");
    CHECK(wb_planets_levels(&p, P_MARS) == 5,
          "and mars is untouched: a planet's chain is its own");
    CHECK(wb_planets_level_for(&p, P_EARTH, DEEP) == 5,
          "the new level represents the deep cell on earth");
    CHECK(wb_planets_level_for(&p, P_MARS, DEEP) == 4,
          "the same cell on mars still resolves to the room");

    /* ---- a planet with no levels of its own ---- */
    wb_planets q;
    wb_planets_init(&q, 4);
    wb_planets_add(&q, "void", 9);
    CHECK(wb_planets_levels(&q, 0) == 1, "a bare planet still has the universe level");
    CHECK(wb_planets_level_for(&q, 0, DEEP) == 0,
          "every cell on a bare planet is the universe level");
    CHECK(wb_planets_imposter(&q, 0, DEEP, 0, out, sizeof out) == 0 &&
          strcmp(out, "void") == 0,
          "and its imposter is the planet's name");
    CHECK(wb_planets_set_universe_depth(&q, 4) == 0,
          "a bare planet has no level for the universe to collide with");
    CHECK(wb_planets_level_for(&q, 0, "01") == -1,
          "a cell above the universe level has no representation");
    CHECK(wb_planets_level_for(&q, 0, DEEP) == 0,
          "a deep cell is still the universe level");
    wb_planets_clear(&q);
    CHECK(wb_planets_count(&q) == 0, "a cleared registry has no planets");
    CHECK(wb_planets_universe_depth(&q) == 0, "and the universe depth is reset");

    /* ---- the ceiling ---- */
    wb_planets full;
    wb_planets_init(&full, 2);
    CHECK(wb_planets_add(&full, "a", 1) == 0, "a planet is added");
    CHECK(wb_planets_add(&full, "b", 2) == 1, "a second planet is added");
    CHECK(wb_planets_add(&full, "c", 3) == -1, "the ceiling refuses another planet");
    CHECK(wb_planets_count(&full) == 2, "the registry is full");

    /* ---- degenerate input ---- */
    CHECK(wb_planets_init(NULL, 4) == -1, "a NULL registry is rejected");
    CHECK(wb_planets_init(&q, 0) == -1, "a zero capacity is rejected");
    CHECK(wb_planets_count(NULL) == 0, "a NULL registry has no planets");
    CHECK(wb_planets_find(NULL, "earth") == -1, "a NULL registry finds nothing");
    CHECK(wb_planets_name(NULL, 0) == NULL, "a NULL registry has no names");
    CHECK(wb_planets_universe_depth(NULL) == 0, "a NULL registry has no universe depth");
    CHECK(wb_planets_set_universe_depth(NULL, 0) == -1,
          "a NULL registry refuses a universe depth");
    CHECK(wb_planets_depth(NULL, 0, 0) == 0, "a NULL registry has no depths");
    CHECK(wb_planets_level_name(NULL, 0, 0) == NULL,
          "a NULL registry has no level names");
    CHECK(wb_planets_imposter(NULL, 0, DEEP, 0, out, sizeof out) == -1,
          "a NULL registry has no imposters");
    wb_planets_free(&q);
    wb_planets_free(&full);
    wb_planets_free(NULL);

    /* ---- determinism: the same registry agrees with itself ---- */
    wb_planets r, s;
    build(&r);
    build(&s);
    const char *cells[] = {"", "0", "01", "012301", "0123012301",
                           "012301230123", "012301230123012301",
                           "012301230123012301230123", DEEP};
    int same = 1;
    for (size_t i = 0; i < sizeof cells / sizeof cells[0]; i++) {
        if (wb_planets_level_for(&r, P_EARTH, cells[i]) !=
            wb_planets_level_for(&s, P_EARTH, cells[i]))
            same = 0;
        for (uint32_t lv = 0; lv < wb_planets_levels(&r, P_EARTH); lv++) {
            char a[sizeof out], b[sizeof out];
            if (wb_planets_imposter(&r, P_EARTH, cells[i], lv, a, sizeof a) !=
                wb_planets_imposter(&s, P_EARTH, cells[i], lv, b, sizeof b))
                same = 0;
            else if (strcmp(a, b) != 0)
                same = 0;
        }
    }
    CHECK(same, "two registries built the same way agree on every cell");
    wb_planets_free(&r);
    wb_planets_free(&s);

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
