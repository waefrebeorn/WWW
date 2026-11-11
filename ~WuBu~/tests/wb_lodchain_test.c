/*
 * wb_lodchain_test.c -- LP-101 stone 1 gate: level-of-detail across levels.
 *
 * Devil's-advocate cases: a cell AT a level's depth is represented by THAT level,
 * not the one below it; a level can never invent detail, so a cell shallower than
 * the level's depth has no imposter there; a cell shallower than the coarsest
 * level has no representation at all; depths must strictly increase or the chain
 * stops being ordered; and the budget that applies to a cell is the budget of the
 * level that represents it, not the finest level's.
 */
#include "wb_lodchain.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

/* universe 2, world 6, continent 12, city 18. */
static int build(wb_lodchain *c) {
    wb_lodchain_init(c);
    if (wb_lodchain_add(c, "universe", 2, 100) != 0) return -1;
    if (wb_lodchain_add(c, "world", 6, 1000) != 0) return -1;
    if (wb_lodchain_add(c, "continent", 12, 10000) != 0) return -1;
    if (wb_lodchain_add(c, "city", 18, 100000) != 0) return -1;
    return 0;
}

int main(void) {
    wb_lodchain c;
    CHECK(build(&c) == 0, "the chain is built");
    CHECK(wb_lodchain_count(&c) == 4, "four levels");

    /* ---- add refuses bad input ---- */
    CHECK(wb_lodchain_add(&c, "planet", 18, 1) == -1, "an equal depth is refused");
    CHECK(wb_lodchain_add(&c, "planet", 17, 1) == -1, "a decreasing depth is refused");
    CHECK(wb_lodchain_add(&c, "", 20, 1) == -1, "an empty name is refused");
    CHECK(wb_lodchain_add(&c, NULL, 20, 1) == -1, "a NULL name is refused");
    CHECK(wb_lodchain_add(&c, "this-name-is-far-too-long-for-the-field", 20, 1) == -1,
          "an over-long name is refused");
    CHECK(wb_lodchain_add(&c, "deep", WB_QKEY_MAX, 1) == -1, "a depth past the quadkey limit is refused");
    CHECK(wb_lodchain_count(&c) == 4, "refused adds change nothing");
    {
        wb_lodchain small;
        wb_lodchain_init(&small);
        for (int i = 0; i < WB_LODCHAIN_MAX; i++)
            CHECK(wb_lodchain_add(&small, "l", (uint32_t)(1 + i), (uint32_t)i) == 0,
                  "the chain fills");
        CHECK(wb_lodchain_count(&small) == WB_LODCHAIN_MAX, "the chain is full");
        CHECK(wb_lodchain_add(&small, "l", 99, 1) == -1, "a full chain refuses an add");
    }

    /* ---- names ---- */
    CHECK(strcmp(wb_lodchain_name(&c, 0), "universe") == 0, "level 0 is the universe");
    CHECK(strcmp(wb_lodchain_name(&c, 3), "city") == 0, "level 3 is the city");
    CHECK(wb_lodchain_name(&c, 4) == NULL, "an out-of-range level has no name");
    CHECK(wb_lodchain_name(NULL, 0) == NULL, "a NULL chain has no names");

    /* ---- which level represents a depth ---- */
    CHECK(wb_lodchain_level_for_depth(&c, 0) == -1, "a depth below the coarsest level has no representation");
    CHECK(wb_lodchain_level_for_depth(&c, 1) == -1, "depth 1 is still below it");
    CHECK(wb_lodchain_level_for_depth(&c, 2) == 0, "a cell AT the universe depth is the universe's");
    CHECK(wb_lodchain_level_for_depth(&c, 3) == 0, "a depth just below the world is still the universe's");
    CHECK(wb_lodchain_level_for_depth(&c, 6) == 1, "a cell AT the world depth is the world's");
    CHECK(wb_lodchain_level_for_depth(&c, 11) == 1, "just below the continent is the world's");
    CHECK(wb_lodchain_level_for_depth(&c, 12) == 2, "a cell AT the continent depth is the continent's");
    CHECK(wb_lodchain_level_for_depth(&c, 16) == 2, "depth 16 is the continent's");
    CHECK(wb_lodchain_level_for_depth(&c, 17) == 2, "depth 17 is still the continent's");
    CHECK(wb_lodchain_level_for_depth(&c, 18) == 3, "a cell AT the city depth is the city's");
    CHECK(wb_lodchain_level_for_depth(&c, 40) == 3, "deeper than every level is the finest level's");
    CHECK(wb_lodchain_level_for_depth(NULL, 12) == -1, "a NULL chain has no levels");

    /* ---- imposters ---- */
    char out[WB_QKEY_MAX];
    const char *deep = "0123012301230123";        /* depth 16 */
    CHECK(wb_lodchain_imposter(&c, 0, deep, out, sizeof out) == 0 && strcmp(out, "01") == 0,
          "the universe imposter is the depth-2 ancestor");
    CHECK(wb_lodchain_imposter(&c, 1, deep, out, sizeof out) == 0 && strcmp(out, "012301") == 0,
          "the world imposter is the depth-6 ancestor");
    CHECK(wb_lodchain_imposter(&c, 2, deep, out, sizeof out) == 0 && strcmp(out, "012301230123") == 0,
          "the continent imposter is the depth-12 ancestor");
    CHECK(wb_lodchain_imposter(&c, 3, deep, out, sizeof out) == -1,
          "a level finer than the cell cannot represent it (no invented detail)");

    CHECK(wb_lodchain_imposter(&c, 0, "01", out, sizeof out) == 0 && strcmp(out, "01") == 0,
          "a cell at the level's own depth is its own imposter");
    CHECK(wb_lodchain_imposter(&c, 0, "0", out, sizeof out) == -1,
          "a cell shallower than the level has no imposter there");
    CHECK(wb_lodchain_imposter(&c, 4, deep, out, sizeof out) == -1, "an out-of-range level is refused");
    CHECK(wb_lodchain_imposter(&c, 0, "bad", out, sizeof out) == -1, "an invalid cell is refused");
    CHECK(wb_lodchain_imposter(&c, 0, deep, NULL, 10) == -1, "a NULL output is refused");
    CHECK(wb_lodchain_imposter(&c, 0, deep, out, 1) == -1, "a one-byte buffer is refused");
    CHECK(wb_lodchain_imposter(NULL, 0, deep, out, sizeof out) == -1, "a NULL chain is refused");

    /* ---- budgets follow the representing level ---- */
    uint32_t b = 0;
    CHECK(wb_lodchain_budget(&c, "01", &b) == 0 && b == 100, "a universe-depth cell gets the universe budget");
    CHECK(wb_lodchain_budget(&c, "012301", &b) == 0 && b == 1000, "a world-depth cell gets the world budget");
    CHECK(wb_lodchain_budget(&c, deep, &b) == 0 && b == 10000,
          "a depth-16 cell gets the CONTINENT budget, not the city's");
    CHECK(wb_lodchain_budget(&c, "012301230123012301", &b) == 0 && b == 100000,
          "a city-depth cell gets the city budget");
    CHECK(wb_lodchain_budget(&c, "0", &b) == -1, "a cell with no representation has no budget");
    CHECK(wb_lodchain_budget(&c, deep, NULL) == -1, "a NULL budget is refused");
    CHECK(wb_lodchain_budget(NULL, deep, &b) == -1, "a NULL chain is refused");

    /* ---- a single-level chain represents everything at or below it ---- */
    {
        wb_lodchain one;
        wb_lodchain_init(&one);
        CHECK(wb_lodchain_add(&one, "world", 4, 7) == 0, "a one-level chain is built");
        CHECK(wb_lodchain_level_for_depth(&one, 3) == -1, "shallower than it: no representation");
        CHECK(wb_lodchain_level_for_depth(&one, 4) == 0, "at its depth: represented");
        CHECK(wb_lodchain_level_for_depth(&one, 30) == 0, "deeper: still represented");
        CHECK(wb_lodchain_imposter(&one, 0, "01230123", out, sizeof out) == 0 && strcmp(out, "0123") == 0,
              "the imposter is the depth-4 ancestor");
    }

    /* ---- determinism ---- */
    {
        wb_lodchain a, b2;
        CHECK(build(&a) == 0 && build(&b2) == 0, "two chains are built");
        int same = 1;
        for (uint32_t d = 0; d <= 24; d++)
            same &= (wb_lodchain_level_for_depth(&a, d) == wb_lodchain_level_for_depth(&b2, d));
        CHECK(same, "level selection is deterministic");
        char oa[WB_QKEY_MAX], ob[WB_QKEY_MAX];
        same = 1;
        for (uint32_t i = 0; i < 4; i++) {
            wb_lodchain_imposter(&a, i, deep, oa, sizeof oa);
            wb_lodchain_imposter(&b2, i, deep, ob, sizeof ob);
            same &= (strcmp(oa, ob) == 0);
        }
        CHECK(same, "imposters are deterministic");
        uint32_t ba = 0, bb = 0;
        same = 1;
        for (uint32_t d = 1; d <= 24; d++) {
            char cell[WB_QKEY_MAX];
            /* build a cell of exactly depth d by cycling digits */
            for (uint32_t k = 0; k < d; k++) cell[k] = (char)('0' + ((k * 7 + 3) & 3));
            cell[d] = '\0';
            wb_lodchain_budget(&a, cell, &ba);
            wb_lodchain_budget(&b2, cell, &bb);
            same &= (ba == bb);
        }
        CHECK(same, "budgets are deterministic");
    }

    /* ---- degenerate input ---- */
    wb_lodchain empty;
    wb_lodchain_init(&empty);
    CHECK(wb_lodchain_count(&empty) == 0, "an empty chain has no levels");
    CHECK(wb_lodchain_level_for_depth(&empty, 12) == -1, "an empty chain represents nothing");
    CHECK(wb_lodchain_imposter(&empty, 0, deep, out, sizeof out) == -1, "an empty chain has no imposters");
    CHECK(wb_lodchain_budget(&empty, deep, &b) == -1, "an empty chain has no budgets");
    CHECK(wb_lodchain_count(NULL) == 0, "a NULL chain has no levels");
    wb_lodchain_init(NULL);

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
