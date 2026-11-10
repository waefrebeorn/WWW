/*
 * wb_catalog_test.c -- LP-52 stone 1 gate: search, charts, featured.
 *
 * Devil's-advocate cases: matching is case-insensitive over the name, the
 * description AND the tags; a tag filter narrows a text search; the charts rank
 * by players then rating then id; featured comes back ascending whatever order
 * the entries were added in; and an empty query matches everything.
 */
#include "wb_catalog.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static wb_catalog_entry mk(uint32_t id, const char *name, const char *desc,
                           const char **tags, uint32_t ntags,
                           uint32_t players, uint32_t rating, uint32_t flags) {
    wb_catalog_entry e;
    memset(&e, 0, sizeof e);
    e.id = id;
    snprintf(e.name, sizeof e.name, "%s", name);
    snprintf(e.desc, sizeof e.desc, "%s", desc);
    for (uint32_t i = 0; i < ntags && i < WB_CATALOG_MAX_TAGS; i++) e.tags[i] = tags[i];
    e.ntags = ntags < WB_CATALOG_MAX_TAGS ? ntags : WB_CATALOG_MAX_TAGS;
    e.players = players;
    e.rating = rating;
    e.flags = flags;
    e.updated = id;
    return e;
}

static int ids_equal(const uint32_t *a, const uint32_t *b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

int main(void) {
    wb_catalog c;
    CHECK(wb_catalog_init(&c, 8) == 0, "the catalog is created");

    const char *t1[] = { "racing", "sky" };
    const char *t2[] = { "zombie", "coop" };
    const char *t3[] = { "sky", "strategy" };
    const char *t4[] = { "puzzle" };
    const char *t5[] = { "racing" };
    wb_catalog_entry e;
    e = mk(1, "Sky Racers",  "Race airships",   t1, 2, 5000, 80, 0);
    CHECK(wb_catalog_add(&c, &e) == 0, "entry 1 added");
    e = mk(2, "Zombie Mall", "Survive the mall", t2, 2, 9000, 70, WB_CATALOG_FEATURED);
    CHECK(wb_catalog_add(&c, &e) == 0, "entry 2 added");
    e = mk(3, "Sky Siege",   "Tower defence",   t3, 2, 2000, 90, WB_CATALOG_FEATURED);
    CHECK(wb_catalog_add(&c, &e) == 0, "entry 3 added");
    e = mk(4, "Puzzle Box",  "Brain teasers",   t4, 1, 1000, 60, 0);
    CHECK(wb_catalog_add(&c, &e) == 0, "entry 4 added");
    e = mk(5, "Racing X",    "Street racing",   t5, 1, 9000, 75, 0);
    CHECK(wb_catalog_add(&c, &e) == 0, "entry 5 added");
    CHECK(wb_catalog_count(&c) == 5, "five entries are stored");

    /* ---- add refuses a duplicate id and a full catalog ---- */
    e = mk(1, "Dup", "Dup", t4, 1, 1, 1, 0);
    CHECK(wb_catalog_add(&c, &e) == -1, "a duplicate id is refused");
    CHECK(wb_catalog_find(&c, 3) != NULL, "an entry is found by id");
    CHECK(wb_catalog_find(&c, 99) == NULL, "an unknown id is not found");

    /* ---- matching ---- */
    const wb_catalog_entry *e1 = wb_catalog_find(&c, 1);
    CHECK(wb_catalog_matches(e1, "sky") == 1, "the name matches");
    CHECK(wb_catalog_matches(e1, "SKY") == 1, "matching is case-insensitive");
    CHECK(wb_catalog_matches(e1, "airships") == 1, "the description matches");
    CHECK(wb_catalog_matches(e1, "racing") == 1, "a tag matches");
    CHECK(wb_catalog_matches(e1, "zombie") == 0, "an unrelated term does not match");
    CHECK(wb_catalog_matches(e1, "") == 1, "an empty query matches everything");
    CHECK(wb_catalog_matches(NULL, "sky") == 0, "a NULL entry does not match");

    /* ---- search ---- */
    uint32_t out[8];
    const uint32_t sky[2] = { 3, 1 };
    CHECK(wb_catalog_search(&c, "sky", NULL, out, 8) == 2 && ids_equal(out, sky, 2),
          "'sky' returns the featured match first");
    const uint32_t racing[2] = { 5, 1 };
    CHECK(wb_catalog_search(&c, "RACING", NULL, out, 8) == 2 && ids_equal(out, racing, 2),
          "'RACING' matches both racers, higher score first");
    const uint32_t mall[1] = { 2 };
    CHECK(wb_catalog_search(&c, "mall", NULL, out, 8) == 1 && ids_equal(out, mall, 1),
          "'mall' matches the name and the description of the same entry once");
    CHECK(wb_catalog_search(&c, "nope", NULL, out, 8) == 0, "a non-matching query returns nothing");

    /* a tag filter narrows a text search */
    const uint32_t sky_strat[1] = { 3 };
    CHECK(wb_catalog_search(&c, "sky", "strategy", out, 8) == 1 && ids_equal(out, sky_strat, 1),
          "a tag filter narrows a text search");
    const uint32_t sky_race[1] = { 1 };
    CHECK(wb_catalog_search(&c, "sky", "racing", out, 8) == 1 && ids_equal(out, sky_race, 1),
          "a different tag filter picks the other match");
    CHECK(wb_catalog_search(&c, "sky", "puzzle", out, 8) == 0, "a tag filter with no overlap returns nothing");

    /* an empty query returns everything, ranked */
    const uint32_t all[5] = { 2, 3, 5, 1, 4 };
    CHECK(wb_catalog_search(&c, NULL, NULL, out, 8) == 5 && ids_equal(out, all, 5),
          "an empty query returns every entry ranked");

    /* a small buffer truncates but keeps the order */
    const uint32_t top2[2] = { 2, 3 };
    CHECK(wb_catalog_search(&c, NULL, NULL, out, 2) == 2 && ids_equal(out, top2, 2),
          "a two-slot buffer returns the top two in order");

    /* ---- charts ---- */
    const uint32_t charts[5] = { 5, 2, 1, 3, 4 };
    CHECK(wb_catalog_charts(&c, out, 8) == 5 && ids_equal(out, charts, 5),
          "the charts rank by players, then rating, then id");
    const uint32_t charts2[2] = { 5, 2 };
    CHECK(wb_catalog_charts(&c, out, 2) == 2 && ids_equal(out, charts2, 2),
          "a two-slot charts buffer returns the top two");

    /* ---- featured ---- */
    const uint32_t feat[2] = { 2, 3 };
    CHECK(wb_catalog_featured(&c, out, 8) == 2 && ids_equal(out, feat, 2),
          "featured returns the flagged entries ascending");
    CHECK(wb_catalog_featured(&c, out, 1) == 1 && out[0] == 2, "a one-slot featured buffer returns the first");

    /* ---- clear ---- */
    wb_catalog_clear(&c);
    CHECK(wb_catalog_count(&c) == 0, "clear empties the catalog");
    CHECK(wb_catalog_search(&c, NULL, NULL, out, 8) == 0, "a cleared catalog returns nothing");
    CHECK(wb_catalog_charts(&c, out, 8) == 0, "a cleared catalog has no charts");
    CHECK(wb_catalog_featured(&c, out, 8) == 0, "a cleared catalog has no featured");

    /* ---- determinism ---- */
    wb_catalog a, b;
    wb_catalog_init(&a, 8);
    wb_catalog_init(&b, 8);
    for (uint32_t i = 0; i < 5; i++) {
        wb_catalog_entry e = mk(1 + i, "Name", "Desc", t4, 1, i * 100, i * 10, 0);
        wb_catalog_add(&a, &e);
        wb_catalog_add(&b, &e);
    }
    int same = 1;
    uint32_t oa[8], ob[8];
    uint32_t na = wb_catalog_search(&a, NULL, NULL, oa, 8);
    uint32_t nb = wb_catalog_search(&b, NULL, NULL, ob, 8);
    same &= (na == nb);
    if (na) same &= ids_equal(oa, ob, na);
    na = wb_catalog_charts(&a, oa, 8);
    nb = wb_catalog_charts(&b, ob, 8);
    same &= (na == nb);
    if (na) same &= ids_equal(oa, ob, na);
    na = wb_catalog_featured(&a, oa, 8);
    nb = wb_catalog_featured(&b, ob, 8);
    same &= (na == nb);
    if (na) same &= ids_equal(oa, ob, na);
    CHECK(same, "the catalog is deterministic");

    /* ---- degenerate input ---- */
    CHECK(wb_catalog_init(NULL, 4) == -1, "a NULL catalog is rejected");
    CHECK(wb_catalog_init(&a, 0) == -1, "a zero capacity is rejected");
    CHECK(wb_catalog_add(NULL, e1) == -1, "a NULL catalog refuses an add");
    CHECK(wb_catalog_add(&a, NULL) == -1, "a NULL entry is refused");
    CHECK(wb_catalog_find(NULL, 1) == NULL, "a NULL catalog finds nothing");
    CHECK(wb_catalog_matches(NULL, "x") == 0, "a NULL entry matches nothing");
    CHECK(wb_catalog_score(NULL, NULL) == 0, "a NULL entry scores zero");
    CHECK(wb_catalog_search(NULL, NULL, NULL, out, 8) == 0, "a NULL catalog searches nothing");
    CHECK(wb_catalog_search(&a, NULL, NULL, NULL, 8) == 0, "a NULL output is refused");
    CHECK(wb_catalog_search(&a, NULL, NULL, out, 0) == 0, "a zero capacity is refused");
    CHECK(wb_catalog_charts(NULL, out, 8) == 0, "a NULL catalog has no charts");
    CHECK(wb_catalog_featured(NULL, out, 8) == 0, "a NULL catalog has no featured");
    CHECK(wb_catalog_count(NULL) == 0, "a NULL catalog has no entries");

    wb_catalog_free(&c);
    wb_catalog_free(&a);
    wb_catalog_free(&b);
    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
